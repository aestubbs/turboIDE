#ifndef TURBO_STYLES_H
#define TURBO_STYLES_H

#define Uses_TColorAttr
#include <tvision/tv.h>
#include <turbo/scintilla.h>

namespace turbo {

struct Language
{
    TStringView lineComment {};
    TStringView blockCommentOpen {};
    TStringView blockCommentClose {};

    constexpr bool hasLineComments() const
    {
        return !lineComment.empty();
    }

    constexpr bool hasBlockComments() const
    {
        return !blockCommentOpen.empty() && !blockCommentClose.empty();
    }

    static const Language
        CPP,
        Makefile,
        Asm,
        JavaScript,
        Rust,
        Python,
        Bash,
        Diff,
        JSON,
        HTML,
        XML,
        VB,
        Perl,
        Batch,
        LaTex,
        Lua,
        Ada,
        Lisp,
        Ruby,
        Tcl,
        VBScript,
        MATLAB,
        CSS,
        YAML,
        Erlang,
        Smalltalk,
        Markdown,
        Properties,
        CSharp,
        Basic,
        Pascal,
        SQL,
        Go,
        PHP,
        Elixir;
};

enum TextStyle : uchar
{
    sNormal,
    sSelection,
    sWhitespace,
    sCtrlChar,
    sLineNums,
    sKeyword1,
    sKeyword2,
    sMisc,
    sPreprocessor,
    sOperator,
    sComment,
    sStringLiteral,
    sCharLiteral,
    sNumberLiteral,
    sEscapeSequence,
    sError,
    sBraceMatch,
    sReplaceHighlight,
    TextStyleCount,
};

using ColorScheme = TColorAttr[TextStyleCount];

// Returns a color attribute such that:
// * The foreground is taken from 'from' if it is not default, and from 'into' otherwise.
// * The background is taken from 'from' if it is not default, and from 'into' otherwise.
// * The style is taken from 'from'.
TColorAttr coalesce(TColorAttr from, TColorAttr into);

inline TColorAttr normalize(const ColorScheme &scheme, TextStyle index)
{
    return coalesce(scheme[index], scheme[sNormal]);
}

// The built-in 24-bit colour scheme (factory default). Immutable; used as the
// baseline for "Reset to defaults".
extern const ColorScheme schemeDefault;

// The scheme actually used by editors at runtime. Initialized to 'schemeDefault'
// and editable via the theme dialog / settings. 'applyTheming' falls back to
// this when an editor has no per-editor scheme of its own (the usual case).
extern ColorScheme schemeActive;

// Copy 'schemeDefault' over 'schemeActive' (the dialog's "Reset" action).
void resetSchemeToDefault() noexcept;

// Stable identifier used to persist a style (e.g. "sKeyword1"), and a
// human-readable label for the theme dialog (e.g. "Keyword"). 'styleName'
// values are part of the on-disk settings format, so don't rename them.
const char *styleName(TextStyle style) noexcept;
const char *styleDisplayName(TextStyle style) noexcept;
// Resolve a stable identifier back to its TextStyle. Returns false if unknown.
bool styleByName(TStringView name, TextStyle &out) noexcept;

struct LexerSettings
{
    struct StyleMapping { uchar id; TextStyle style; };
    struct KeywordMapping { uchar id; const char *keywords; };
    struct PropertyMapping { const char *name, *value; };

    int id;
    TSpan<const StyleMapping> styles;
    TSpan<const KeywordMapping> keywords;
    TSpan<const PropertyMapping> properties;
};

const Language *detectFileLanguage(const char *filePath);
const LexerSettings *findBuiltInLexer(const Language *language);

} // namespace turbo

#endif
