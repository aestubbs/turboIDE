// Lexer for Elixir.
//
// Written for turbo. Lexilla ships no Elixir lexer, and the two BEAM-adjacent
// ones are actively harmful as stand-ins: LexErlang treats '%' as a comment
// (Elixir writes %{} maps and %Struct{}), and LexRuby reads '%{' as a percent
// literal and '<<' as a heredoc opener. Both corrupt on ordinary Elixir.
//
// The token inventory is transcribed from the scope names in the Elixir
// TextMate grammar (elixir-tmbundle, Apache-2.0, (c) Plataformatec, by way of
// vscode-elixir-ls, MIT). The structure follows LexRuby, which already solves
// the hard shared problem: heredocs, delimited literals and #{} interpolation
// carried across lines through the line state.
//
// This file deliberately lives outside deps/lexilla so the submodule stays
// pristine and upgradable; it is registered through turbo::createLexer instead
// of Lexilla's generated catalogue.

#include <cstdlib>
#include <cassert>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <iterator>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"
#include "OptionSet.h"
#include "DefaultLexer.h"

#include <turbo/lexelixir.h>

using namespace Scintilla;
using namespace Lexilla;

namespace {

using namespace turbo;

constexpr bool IsIdentStart(int ch) noexcept {
    return IsLowerCase(ch) || ch == '_';
}

constexpr bool IsIdentChar(int ch) noexcept {
    return IsAlphaNumeric(ch) || ch == '_';
}

// def foo?, put!, is_nil? -- a trailing ? or ! belongs to the name.
constexpr bool IsIdentTail(int ch) noexcept {
    return ch == '?' || ch == '!';
}

constexpr bool IsOperatorChar(int ch) noexcept {
    return strchr("+-*/\\=<>!&|^~.,;:@%()[]{}", ch) != nullptr;
}

// Sigil delimiters. The bracketing ones nest; the rest close on themselves.
constexpr bool IsSigilDelim(int ch) noexcept {
    return ch == '/' || ch == '|' || ch == '"' || ch == '\'' ||
           ch == '(' || ch == '[' || ch == '{' || ch == '<';
}

constexpr int CloserFor(int opener) noexcept {
    switch (opener) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '<': return '>';
        default:  return opener;
    }
}

constexpr int OpenerFor(int closer) noexcept {
    switch (closer) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        case '>': return '<';
        default:  return 0;     // self-closing: no nesting
    }
}

// Only these bind a function name to the next identifier. defmodule/defstruct
// and friends are excluded -- what follows them is an alias or a keyword list,
// not a function.
bool IsDefKeyword(const char *s) noexcept {
    return strcmp(s, "def") == 0 || strcmp(s, "defp") == 0 ||
           strcmp(s, "defmacro") == 0 || strcmp(s, "defmacrop") == 0 ||
           strcmp(s, "defguard") == 0 || strcmp(s, "defguardp") == 0 ||
           strcmp(s, "defdelegate") == 0 || strcmp(s, "defn") == 0;
}

constexpr bool IsQuoteStyle(int style) noexcept {
    return style == SCE_ELIXIR_STRING || style == SCE_ELIXIR_CHARLIST ||
           style == SCE_ELIXIR_SIGIL;
}

// The quote (string / charlist / sigil) currently being lexed, packed into the
// per-line state so it survives a line boundary. Lexilla's test harness lexes
// each document whole AND line-by-line and diffs the two, so anything that must
// outlive a line has to live here rather than in a local.
//
//   bits 0..7   closing delimiter (0 = not inside a quote)
//   bit  8      is a sigil (styles the body SIGIL rather than STRING/CHARLIST)
//   bit  9      is a heredoc (""" / ''')
//   bit  10     interpolates (#{...} is live)
//   bits 11..16 nesting depth, for the bracketing delimiters
//   bits 17..22 brace depth inside #{...}; 0 = not currently in an interpolation
struct QuoteState {
    int closer = 0;
    bool isSigil = false;
    bool heredoc = false;
    bool interp = false;
    int nest = 0;
    int braces = 0;

    bool Active() const noexcept { return closer != 0; }
    void Clear() noexcept { *this = QuoteState(); }

    int Style() const noexcept {
        if (isSigil) return SCE_ELIXIR_SIGIL;
        if (closer == '\'') return SCE_ELIXIR_CHARLIST;
        return SCE_ELIXIR_STRING;
    }

    int Pack() const noexcept {
        return (closer & 0xFF)
             | (isSigil ? 1 << 8 : 0)
             | (heredoc ? 1 << 9 : 0)
             | (interp  ? 1 << 10 : 0)
             | ((nest & 0x3F) << 11)
             | ((braces & 0x3F) << 17);
    }

    static QuoteState Unpack(int v) noexcept {
        QuoteState q;
        q.closer  = v & 0xFF;
        q.isSigil = (v >> 8) & 1;
        q.heredoc = (v >> 9) & 1;
        q.interp  = (v >> 10) & 1;
        q.nest    = (v >> 11) & 0x3F;
        q.braces  = (v >> 17) & 0x3F;
        return q;
    }
};

struct OptionsElixir {
    bool fold = false;
    bool foldComment = false;
    bool foldCompact = false;
};

const char *const elixirWordListDesc[] = {
    "Keywords",
    "Guards and Kernel builtins",
    nullptr
};

struct OptionSetElixir : public OptionSet<OptionsElixir> {
    OptionSetElixir() {
        DefineProperty("fold", &OptionsElixir::fold);
        DefineProperty("fold.comment", &OptionsElixir::foldComment);
        DefineProperty("fold.compact", &OptionsElixir::foldCompact);
        DefineWordListSets(elixirWordListDesc);
    }
};

LexicalClass lexicalClasses[] = {
    {SCE_ELIXIR_DEFAULT,       "SCE_ELIXIR_DEFAULT",       "default",       "White space"},
    {SCE_ELIXIR_COMMENT,       "SCE_ELIXIR_COMMENT",       "comment line",  "Comment"},
    {SCE_ELIXIR_NUMBER,        "SCE_ELIXIR_NUMBER",        "literal numeric", "Number"},
    {SCE_ELIXIR_STRING,        "SCE_ELIXIR_STRING",        "literal string", "String"},
    {SCE_ELIXIR_CHARLIST,      "SCE_ELIXIR_CHARLIST",      "literal string", "Charlist"},
    {SCE_ELIXIR_SIGIL,         "SCE_ELIXIR_SIGIL",         "literal string", "Sigil body"},
    {SCE_ELIXIR_SIGIL_MARK,    "SCE_ELIXIR_SIGIL_MARK",    "literal string", "Sigil delimiter"},
    {SCE_ELIXIR_KEYWORD,       "SCE_ELIXIR_KEYWORD",       "keyword",       "Keyword"},
    {SCE_ELIXIR_KEYWORD2,      "SCE_ELIXIR_KEYWORD2",      "identifier",    "Guard or builtin"},
    {SCE_ELIXIR_ATOM,          "SCE_ELIXIR_ATOM",          "literal",       "Atom"},
    {SCE_ELIXIR_MODULE,        "SCE_ELIXIR_MODULE",        "identifier",    "Module alias"},
    {SCE_ELIXIR_ATTRIBUTE,     "SCE_ELIXIR_ATTRIBUTE",     "preprocessor",  "Module attribute"},
    {SCE_ELIXIR_OPERATOR,      "SCE_ELIXIR_OPERATOR",      "operator",      "Operator"},
    {SCE_ELIXIR_IDENTIFIER,    "SCE_ELIXIR_IDENTIFIER",    "identifier",    "Identifier"},
    {SCE_ELIXIR_FUNCTION,      "SCE_ELIXIR_FUNCTION",      "identifier",    "Function name"},
    {SCE_ELIXIR_INTERPOLATION, "SCE_ELIXIR_INTERPOLATION", "literal string interpolated", "Interpolation"},
    {SCE_ELIXIR_CHARACTER,     "SCE_ELIXIR_CHARACTER",     "literal string", "Character"},
    {SCE_ELIXIR_ERROR,         "SCE_ELIXIR_ERROR",         "error",         "Error"},
};

class LexerElixir : public DefaultLexer {
    WordList keywords;
    WordList keywords2;
    OptionsElixir options;
    OptionSetElixir osElixir;

public:
    LexerElixir() :
        DefaultLexer("elixir", SCLEX_TURBO_ELIXIR, lexicalClasses, std::size(lexicalClasses)) {}

    int SCI_METHOD Version() const override { return lvRelease5; }
    void SCI_METHOD Release() override { delete this; }

    const char *SCI_METHOD PropertyNames() override { return osElixir.PropertyNames(); }
    int SCI_METHOD PropertyType(const char *name) override { return osElixir.PropertyType(name); }
    const char *SCI_METHOD DescribeProperty(const char *name) override {
        return osElixir.DescribeProperty(name);
    }
    const char *SCI_METHOD PropertyGet(const char *key) override { return osElixir.PropertyGet(key); }
    const char *SCI_METHOD DescribeWordListSets() override { return osElixir.DescribeWordListSets(); }

    Sci_Position SCI_METHOD PropertySet(const char *key, const char *val) override;
    Sci_Position SCI_METHOD WordListSet(int n, const char *wl) override;

    void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position length, int initStyle,
                        IDocument *pAccess) override;
    void SCI_METHOD Fold(Sci_PositionU startPos, Sci_Position length, int initStyle,
                         IDocument *pAccess) override;

    void *SCI_METHOD PrivateCall(int, void *) override { return nullptr; }

    static ILexer5 *LexerFactoryElixir() { return new LexerElixir(); }
};

Sci_Position SCI_METHOD LexerElixir::PropertySet(const char *key, const char *val) {
    if (osElixir.PropertySet(&options, key, val))
        return 0;
    return -1;
}

Sci_Position SCI_METHOD LexerElixir::WordListSet(int n, const char *wl) {
    WordList *wordListN = nullptr;
    switch (n) {
        case 0: wordListN = &keywords; break;
        case 1: wordListN = &keywords2; break;
    }
    if (!wordListN)
        return -1;
    if (wordListN->Set(wl))
        return 0;
    return -1;
}

void SCI_METHOD LexerElixir::Lex(Sci_PositionU startPos, Sci_Position length, int initStyle,
                                 IDocument *pAccess) {
    Accessor styler(pAccess, nullptr);

    const Sci_Position lineFirst = styler.GetLine(startPos);
    QuoteState q;
    if (lineFirst > 0)
        q = QuoteState::Unpack(styler.GetLineState(lineFirst - 1));

    // If the carried state says we are not inside a quote, don't trust a quote
    // style handed to us in initStyle -- an unterminated single-line string was
    // dropped at the end of the previous line (see the atLineEnd handling), and
    // resuming inside it would make a partial relex disagree with a whole-file
    // one.
    if (!q.Active() && IsQuoteStyle(initStyle))
        initStyle = SCE_ELIXIR_DEFAULT;

    StyleContext sc(startPos, length, initStyle, styler);

    // The name bound by a preceding def/defp/... Lives only within a line;
    // `def` and its name are always on the same line.
    bool expectFuncName = false;
    // A quote opened *inside* an interpolation, e.g. the ", " in
    //   "#{Enum.join(list, ", ")}"
    // These cannot span lines, so they need no room in the packed line state.
    int innerCloser = 0;
    // Whether only whitespace has been seen on this line so far. A heredoc
    // closes on a """ that starts its line, which is Elixir's actual rule --
    // and notably NOT "the line ends after it", the assumption that makes
    // VS Code's HEEx grammar break on mix format's `""")`.
    bool blankSoFar = true;

    for (; sc.More(); sc.Forward()) {

        if (sc.atLineStart) {
            blankSoFar = true;
            innerCloser = 0;
            if (!q.Active() && IsQuoteStyle(sc.state))
                sc.SetState(SCE_ELIXIR_DEFAULT);
        }

        switch (sc.state) {

        case SCE_ELIXIR_OPERATOR:
            // One character per operator token: drop straight back to default so
            // the start-block below re-examines this character. Elixir's
            // multi-character operators (|>, <>, ::, ->) all colour the same, so
            // there is nothing to gain from grouping them.
            sc.SetState(SCE_ELIXIR_DEFAULT);
            break;

        case SCE_ELIXIR_COMMENT:
            if (sc.atLineStart)
                sc.SetState(SCE_ELIXIR_DEFAULT);
            break;

        case SCE_ELIXIR_NUMBER:
            if (!(IsADigit(sc.ch) || sc.ch == '_'
                  || IsAHeXDigit(sc.ch) || sc.ch == 'x' || sc.ch == 'X'
                  || sc.ch == 'o' || sc.ch == 'O'
                  || (sc.ch == '.' && IsADigit(sc.chNext))
                  || ((sc.ch == '+' || sc.ch == '-')
                      && (sc.chPrev == 'e' || sc.chPrev == 'E'))))
                sc.SetState(SCE_ELIXIR_DEFAULT);
            break;

        case SCE_ELIXIR_ATTRIBUTE:
        case SCE_ELIXIR_MODULE:
            if (!IsIdentChar(sc.ch)) {
                expectFuncName = false;
                sc.SetState(SCE_ELIXIR_DEFAULT);
            }
            break;

        case SCE_ELIXIR_ATOM:
            if (IsIdentTail(sc.ch))
                break;                                  // :foo? / :foo!
            if (!IsIdentChar(sc.ch))
                sc.SetState(SCE_ELIXIR_DEFAULT);
            break;

        case SCE_ELIXIR_CHARACTER:
            // ?a, ?\n, ?\\ -- entered on '?', so the literal is the escape
            // marker (if any) plus exactly one more character.
            if (!(sc.chPrev == '?' && sc.ch == '\\'))
                sc.ForwardSetState(SCE_ELIXIR_DEFAULT);
            break;

        case SCE_ELIXIR_IDENTIFIER:
            if (IsIdentTail(sc.ch))
                break;                                  // is_nil?, put!
            if (!IsIdentChar(sc.ch)) {
                char s[128];
                sc.GetCurrent(s, sizeof(s));
                if (sc.ch == ':' && sc.chNext != ':') {
                    // A keyword-list key: `tone: :friendly`, `do: expr`. The
                    // colon belongs to the atom -- and styling it here is what
                    // keeps `do:` out of the folder's `do` count.
                    sc.ChangeState(SCE_ELIXIR_ATOM);
                    sc.ForwardSetState(SCE_ELIXIR_DEFAULT);
                } else if (keywords.InList(s)) {
                    sc.ChangeState(SCE_ELIXIR_KEYWORD);
                    expectFuncName = IsDefKeyword(s);
                    sc.SetState(SCE_ELIXIR_DEFAULT);
                } else if (keywords2.InList(s)) {
                    sc.ChangeState(SCE_ELIXIR_KEYWORD2);
                    sc.SetState(SCE_ELIXIR_DEFAULT);
                } else if (expectFuncName) {
                    sc.ChangeState(SCE_ELIXIR_FUNCTION);
                    expectFuncName = false;
                    sc.SetState(SCE_ELIXIR_DEFAULT);
                } else {
                    sc.SetState(SCE_ELIXIR_DEFAULT);
                }
            }
            break;

        case SCE_ELIXIR_STRING:
        case SCE_ELIXIR_CHARLIST:
        case SCE_ELIXIR_SIGIL: {
            if (innerCloser) {
                // A quote nested inside an interpolation. Simple: no sigils, no
                // nesting, must close on this line.
                if (sc.ch == '\\')
                    sc.Forward();
                else if (sc.ch == innerCloser) {
                    innerCloser = 0;
                    sc.ForwardSetState(SCE_ELIXIR_DEFAULT);
                }
                break;
            }
            if (!q.Active()) {          // defensive: nothing to close into
                sc.SetState(SCE_ELIXIR_DEFAULT);
                break;
            }
            if (sc.ch == '\\' && (q.interp || sc.chNext == q.closer)) {
                sc.Forward();           // escaped character; never a delimiter
                break;
            }
            if (q.interp && sc.ch == '#' && sc.chNext == '{') {
                sc.SetState(SCE_ELIXIR_INTERPOLATION);
                sc.Forward();                            // onto the '{'
                q.braces = 1;
                sc.ForwardSetState(SCE_ELIXIR_DEFAULT);  // Elixir resumes inside
                break;
            }
            if (q.heredoc) {
                // Close only on a delimiter that *opens* its own line, which is
                // Elixir's actual rule. Note it is NOT "the delimiter ends the
                // line": mix format emits `""")` when a sigil is a call
                // argument, and assuming otherwise is what breaks VS Code's HEEx
                // highlighting (phoenixframework/vscode-phoenix#7).
                if (blankSoFar && sc.ch == q.closer &&
                    sc.chNext == q.closer && sc.GetRelativeChar(2) == q.closer) {
                    sc.Forward(2);
                    const bool wasSigil = q.isSigil;
                    q.Clear();
                    if (wasSigil)
                        while (IsLowerCase(sc.chNext))
                            sc.Forward();       // trailing modifiers
                    sc.ForwardSetState(SCE_ELIXIR_DEFAULT);
                }
                break;
            }
            const int opener = OpenerFor(q.closer);
            if (opener && sc.ch == opener) {
                q.nest++;
            } else if (sc.ch == q.closer) {
                if (q.nest > 0) {
                    q.nest--;
                } else {
                    const bool wasSigil = q.isSigil;
                    q.Clear();
                    if (wasSigil)
                        while (IsLowerCase(sc.chNext))
                            sc.Forward();       // the 'i' in ~r/^a+$/i
                    sc.ForwardSetState(SCE_ELIXIR_DEFAULT);
                }
            }
            break;
        }
        }

        // ---- start a new token ----
        if (sc.state == SCE_ELIXIR_DEFAULT) {
            if (sc.ch == '#' && q.braces == 0) {
                // Inside #{...} a '#' cannot open a comment; treating it as one
                // would comment out the closing brace and run away.
                sc.SetState(SCE_ELIXIR_COMMENT);
            } else if (IsADigit(sc.ch)) {
                sc.SetState(SCE_ELIXIR_NUMBER);
            } else if (sc.ch == '?' && !IsASpace(sc.chNext) && sc.chNext != '\0') {
                // A trailing '?' is eaten by the identifier rule above, so a '?'
                // reaching here is always a character literal.
                sc.SetState(SCE_ELIXIR_CHARACTER);
            } else if (sc.ch == '@' && IsIdentStart(sc.chNext)) {
                sc.SetState(SCE_ELIXIR_ATTRIBUTE);
            } else if (sc.ch == ':' && (IsIdentStart(sc.chNext) || IsUpperCase(sc.chNext))) {
                sc.SetState(SCE_ELIXIR_ATOM);
            } else if (sc.ch == '~' && (IsLowerCase(sc.chNext) || IsUpperCase(sc.chNext))) {
                // A sigil name is one lowercase letter, or a run of uppercase
                // ones (~H, ~LVN). Uppercase sigils do not interpolate.
                const bool upper = IsUpperCase(sc.chNext);
                int nameLen = 1;
                if (upper)
                    while (IsUpperCase(sc.GetRelativeChar(1 + nameLen)))
                        nameLen++;
                const int delim = sc.GetRelativeChar(1 + nameLen);
                if (IsSigilDelim(delim)) {
                    const bool heredoc = (delim == '"' || delim == '\'')
                        && sc.GetRelativeChar(2 + nameLen) == delim
                        && sc.GetRelativeChar(3 + nameLen) == delim;
                    sc.SetState(SCE_ELIXIR_SIGIL);
                    q.Clear();
                    q.closer = CloserFor(delim);
                    q.isSigil = true;
                    q.interp = !upper;
                    q.heredoc = heredoc;
                    sc.Forward(1 + nameLen + (heredoc ? 2 : 0));   // land on the delimiter
                } else {
                    sc.SetState(SCE_ELIXIR_OPERATOR);
                }
            } else if (sc.ch == '"' || sc.ch == '\'') {
                const int closer = sc.ch;
                const bool heredoc = sc.chNext == closer && sc.GetRelativeChar(2) == closer;
                sc.SetState(closer == '"' ? SCE_ELIXIR_STRING : SCE_ELIXIR_CHARLIST);
                if (q.braces > 0) {
                    // Nested inside an interpolation; leave the enclosing quote
                    // state alone -- it is what we return to.
                    innerCloser = closer;
                } else {
                    q.Clear();
                    q.closer = closer;
                    q.interp = true;
                    q.heredoc = heredoc;
                    if (heredoc)
                        sc.Forward(2);
                }
            } else if (IsUpperCase(sc.ch)) {
                sc.SetState(SCE_ELIXIR_MODULE);
            } else if (IsIdentStart(sc.ch)) {
                sc.SetState(SCE_ELIXIR_IDENTIFIER);
            } else if (IsOperatorChar(sc.ch)) {
                sc.SetState(SCE_ELIXIR_OPERATOR);
                if (q.Active() && q.braces > 0) {
                    if (sc.ch == '{') {
                        q.braces++;
                    } else if (sc.ch == '}') {
                        q.braces--;
                        if (q.braces == 0) {
                            sc.ChangeState(SCE_ELIXIR_INTERPOLATION);
                            sc.ForwardSetState(q.Style());   // back into the quote
                        }
                    }
                }
            }
        }

        // Updated *after* the token handling above, so that while it runs
        // 'blankSoFar' still describes the characters strictly before sc.ch --
        // which is what the heredoc terminator has to ask about.
        if (!IsASpaceOrTab(sc.ch))
            blankSoFar = false;

        if (sc.atLineEnd) {
            // A plain "..." or '...' cannot span a line in Elixir. Dropping it
            // here stops one unterminated quote from swallowing the rest of the
            // file, and keeps a partial relex agreeing with a whole-file one.
            if (q.Active() && !q.heredoc && !q.isSigil) {
                q.Clear();
                if (IsQuoteStyle(sc.state) || sc.state == SCE_ELIXIR_INTERPOLATION)
                    sc.ChangeState(SCE_ELIXIR_DEFAULT);
            }
            innerCloser = 0;
            styler.SetLineState(sc.currentLine, q.Pack());
        }
    }
    sc.Complete();
}

void SCI_METHOD LexerElixir::Fold(Sci_PositionU startPos, Sci_Position length, int initStyle,
                                  IDocument *pAccess) {
    if (!options.fold)
        return;

    Accessor styler(pAccess, nullptr);
    const Sci_PositionU endPos = startPos + length;
    Sci_Position lineCurrent = styler.GetLine(startPos);

    int levelCurrent = SC_FOLDLEVELBASE;
    if (lineCurrent > 0)
        levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
    int levelNext = levelCurrent;
    int visibleChars = 0;

    for (Sci_PositionU i = startPos; i < endPos; i++) {
        const char ch = styler[i];
        const int style = styler.StyleAt(i);

        if (style == SCE_ELIXIR_OPERATOR) {
            if (ch == '{' || ch == '[' || ch == '(')
                levelNext++;
            else if (ch == '}' || ch == ']' || ch == ')')
                levelNext--;
        } else if (style == SCE_ELIXIR_KEYWORD &&
                   (i == 0 || styler.StyleAt(i - 1) != SCE_ELIXIR_KEYWORD)) {
            // Only a KEYWORD-styled `do` counts, which is exactly why `do:` is
            // styled as an atom: the keyword-list form opens no block.
            char word[16];
            Sci_PositionU j = 0;
            while (j < sizeof(word) - 1 && i + j < endPos &&
                   styler.StyleAt(i + j) == SCE_ELIXIR_KEYWORD) {
                word[j] = styler[i + j];
                j++;
            }
            word[j] = '\0';
            if (strcmp(word, "do") == 0 || strcmp(word, "fn") == 0)
                levelNext++;
            else if (strcmp(word, "end") == 0)
                levelNext--;
        }

        if (!isspacechar(ch))
            visibleChars++;

        const bool atEOL = (ch == '\r' && styler.SafeGetCharAt(i + 1) != '\n') || (ch == '\n');
        if (atEOL || i == endPos - 1) {
            int lev = levelCurrent | (levelNext << 16);
            if (visibleChars == 0 && options.foldCompact)
                lev |= SC_FOLDLEVELWHITEFLAG;
            if (levelNext > levelCurrent)
                lev |= SC_FOLDLEVELHEADERFLAG;
            if (lev != styler.LevelAt(lineCurrent))
                styler.SetLevel(lineCurrent, lev);
            lineCurrent++;
            levelCurrent = levelNext;
            visibleChars = 0;
        }
    }
}

} // namespace

namespace Lexilla {
extern const LexerModule lmTurboElixir(SCLEX_TURBO_ELIXIR, LexerElixir::LexerFactoryElixir,
                                       "elixir", elixirWordListDesc);
}
