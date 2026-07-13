#include <tvision/tv.h>
#include <turbo/scintilla.h>
#include <turbo/styles.h>
#include <turbo/lexts.h>
#include <turbo/tpath.h>
#include <string_view>
#include "utils.h"

#ifdef HAVE_MAGIC
#include <magic.h>
#endif

namespace turbo {

constexpr Language
    Language::CPP {"//", "/*", "*/"},
    Language::Makefile {"#"},
    Language::Asm {";"},
    Language::JavaScript {"//", "/*", "*/"},
    Language::Rust {"//", "/*", "*/"},
    Language::Python {"#"},
    Language::Bash {"#"},
    Language::Diff,
    Language::JSON {"//", "/*", "*/"},
    Language::HTML {{}, "<!--", "-->"},
    Language::XML {{}, "<!--", "-->"},
    Language::VB {"'"},
    Language::Perl {"#"},
    Language::Batch {"rem "},
    Language::LaTex {"%"},
    Language::Lua {"--", "--[[", "]]"},
    Language::Ada {"--"},
    Language::Lisp {";"},
    Language::Ruby {"#"},
    Language::Tcl {"#"},
    Language::VBScript {"#"},
    Language::MATLAB {"%"},
    Language::CSS {{}, "/*", "*/"},
    Language::YAML {"#"},
    Language::Erlang {"%"},
    Language::Smalltalk {{}, "\"", "\""},
    Language::Markdown,
    Language::Properties {"#"},
    Language::CSharp {"//", "/*", "*/"},
    Language::Basic {"'"},
    Language::Pascal {"//", "{", "}"},
    Language::SQL {"--", "/*", "*/"},
    Language::Go {"//", "/*", "*/"},
    Language::PHP {"//", "/*", "*/"},
    Language::Elixir {"#"},
    Language::TypeScript {"//", "/*", "*/"},
    // {{-- --}} is Blade's own comment: unlike an HTML <!-- --> it is stripped
    // before render, so the PHP inside it is never evaluated. That makes it the
    // right thing for toggle-comment to insert.
    Language::Blade {{}, "{{--", "--}}"},
    // <%!-- --%> is HEEx's own comment: unlike an HTML <!-- --> it also suppresses
    // the Elixir inside the commented region, so it is the right thing for the
    // toggle-comment command to insert.
    Language::HEEx {{}, "<%!--", "--%>"};

static const const_unordered_map<std::string_view, const Language *> mime2lang = {
    {"text/x-c++",                  &Language::CPP},
    {"text/x-c",                    &Language::CPP},
    {"text/x-script.python",        &Language::Python},
    {"application/json",            &Language::JSON},
    {"text/x-shellscript",          &Language::Bash},
    {"text/x-makefile",             &Language::Makefile},
    {"text/x-diff",                 &Language::Diff},
    {"text/html",                   &Language::HTML},
    {"text/x-php",                  &Language::PHP},
};

static const const_unordered_map<std::string_view, const Language *> ext2lang = {
    {".js",                         &Language::JavaScript},
    {".jsx",                        &Language::JavaScript},
    {".mjs",                        &Language::JavaScript},
    {".cjs",                        &Language::JavaScript},
    // TypeScript rendered as plain text before this: nothing mapped .ts at all.
    // LexCPP does not understand type annotations deeply, but with the keyword
    // list below it is a long way better than no highlighting.
    {".ts",                         &Language::TypeScript},
    {".tsx",                        &Language::TypeScript},
    {".mts",                        &Language::TypeScript},
    {".cts",                        &Language::TypeScript},
    {".asm",                        &Language::Asm},
    {".s",                          &Language::Asm},
    {".S",                          &Language::Asm},
    {".c",                          &Language::CPP},
    {".cc",                         &Language::CPP},
    {".cpp",                        &Language::CPP},
    {".cxx",                        &Language::CPP},
    {".h",                          &Language::CPP},
    {".hh",                         &Language::CPP},
    {".hpp",                        &Language::CPP},
    {".hxx",                        &Language::CPP},
    {".py",                         &Language::Python},
    {".htm",                        &Language::HTML},
    {".html",                       &Language::HTML},
    {".mhtml",                      &Language::HTML},
    {".xml",                        &Language::XML},
    {".vb",                         &Language::VB},
    {".pl",                         &Language::Perl},
    {".pm",                         &Language::Perl},
    {".bat",                        &Language::Batch},
    {".tex",                        &Language::LaTex},
    {".lua",                        &Language::Lua},
    {".diff",                       &Language::Diff},
    {".ads",                        &Language::Ada},
    {".adb",                        &Language::Ada},
    {".lsp",                        &Language::Lisp},
    {".rb",                         &Language::Ruby},
    {".tcl",                        &Language::Tcl},
    {".vbs",                        &Language::VBScript},
    {".m",                          &Language::MATLAB},
    {".css",                        &Language::CSS},
    {".erl",                        &Language::Erlang},
    {".hrl",                        &Language::Erlang},
    {".st",                         &Language::Smalltalk},
    {".md",                         &Language::Markdown},
    {".rs",                         &Language::Rust},
    {".java",                       &Language::CPP},
    {"Makefile",                    &Language::Makefile},
    {"PKGBUILD",                    &Language::Bash},
    {".json",                       &Language::JSON},
    {"eslintrc",                    &Language::JSON},
    {".jshintrc",                   &Language::JSON},
    {".jsonld",                     &Language::JSON},
    {".ipynb",                      &Language::JSON},
    {".babelrc",                    &Language::JSON},
    {".prettierrc",                 &Language::JSON},
    {".stylelintrc",                &Language::JSON},
    {".jsonc",                      &Language::JSON},
    {".jscop",                      &Language::JSON},
    {".yml",                        &Language::YAML},
    {".yaml",                       &Language::YAML},
    {".clang-format",               &Language::YAML},
    {".clang-tidy",                 &Language::YAML},
    {".mir",                        &Language::YAML},
    {".apinotes",                   &Language::YAML},
    {".ifs",                        &Language::YAML},
    {".sh",                         &Language::Bash},
    {".bashrc",                     &Language::Bash},
    {".zshrc",                      &Language::Bash},
    {".ini",                        &Language::Properties},
    {".properties",                 &Language::Properties},
    {".cs",                         &Language::CSharp},
    {".bas",                        &Language::Basic},
    {".frm",                        &Language::Basic},
    {".cls",                        &Language::Basic},
    {".pas",                        &Language::Pascal},
    {".sql",                        &Language::SQL},
    {".go",                         &Language::Go},
    {".php",                        &Language::PHP},
    {".ex",                         &Language::Elixir},
    {".exs",                        &Language::Elixir},
    // Phoenix templates are usually named foo.html.heex; TPath::extname takes the
    // last component, so ".heex" covers both that and a bare foo.heex.
    {".heex",                       &Language::HEEx},
};

const Language *detectFileLanguage(const char *filePath)
{
    const Language *lang = nullptr;
    {
        // Laravel views are named foo.blade.php, and extname() only yields the
        // last component (".php"). Without matching the compound suffix here,
        // every Blade template in a project would be lexed as plain PHP.
        auto name = TPath::basename(filePath);
        const TStringView bladeSuffix = ".blade.php";
        if ( name.size() > bladeSuffix.size() &&
             name.substr(name.size() - bladeSuffix.size()) == bladeSuffix )
            return &Language::Blade;
    }
    {
        auto ext = TPath::extname(filePath);
        if (!ext.empty())
            lang = ext2lang[ext];
        else
        {
            auto name = TPath::basename(filePath);
            lang = ext2lang[name];
        }
    }
#ifdef HAVE_MAGIC
    if (!lang) {
        magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
        if (magic_cookie) {
            if (magic_load(magic_cookie, nullptr) == 0)
            {
                const char *mimeType = magic_file(magic_cookie, filePath);
                if (mimeType)
                    lang = mime2lang[mimeType];
            }
            if ( magic_setflags(magic_cookie, MAGIC_MIME_ENCODING) == 0 &&
                 magic_load(magic_cookie, nullptr) == 0 )
            {
                const char *mimeEncoding = magic_file(magic_cookie, filePath);
                if (mimeEncoding)
                    ;
            }
        }
        magic_close(magic_cookie);
    }
#endif
    return lang;
}

// 24-bit colour palette for the built-in scheme. A cohesive dark theme that
// sits alongside the Turbo-blue window chrome: a deep navy editor background
// with VS-Code-Dark-style syntax accents. {} (zero-init) means "terminal
// default / inherit"; for non-sNormal styles 'normalize' coalesces that onto
// sNormal's colours when drawing.
// Note: these are TColorDesired (not TColorRGB) because TColorDesired's int
// constructor is constexpr, whereas TColorRGB's uint32_t one is not -- and the
// scheme below is a constexpr aggregate.
namespace {
constexpr TColorDesired
    cBg          = 0x1E4D8C, // editor background -- matches the active window frame
    cFg          = 0xE4E8F4, // default foreground (near-white)
    cSelBg       = 0x3F6BB5, // selection background (lighter than bg, so it reads)
    cWhitespace  = 0x5C76AE, // dim dots/arrows for whitespace
    cLineNums    = 0x9098C8, // gutter line numbers (light blue-grey on blue)
    cKeyword1    = 0x6FB7FF, // primary keywords (bright blue, legible on blue bg)
    cKeyword2    = 0x66E0C4, // secondary keywords / types (teal)
    cMisc        = 0xEDE8B0, // misc / global classes (soft yellow)
    cPreproc     = 0xD79CD2, // preprocessor / directives (purple)
    cOperator    = 0xD2D6E6, // operators
    cComment     = 0x9AA6CE, // comments (light muted, italic)
    cString      = 0xF0A98C, // string literals (warm tan)
    cNumber      = 0xC4DCB4, // number literals (green)
    cEscape      = 0xE6C98C, // escape sequences (gold)
    cCtrlChar    = 0xD79CD2, // control characters
    cErrorFg     = 0xFFFFFF, // error text
    cErrorBg     = 0xC02437, // error background (red)
    cBraceMatch  = 0xFFD75F, // matching brace (gold)
    cReplaceFg   = 0x10182E, // replace-highlight text (on green)
    cReplaceBg   = 0x5FA84E, // replace-highlight background (green)
    cFunction    = 0xDCDCAA, // function/method names (soft yellow)
    cTypeName    = 0x4EC9B0, // user-defined types, classes, modules (teal)
    cConstant    = 0x62D0E8; // atoms, symbols, enum members (cyan)
} // namespace

extern constexpr ColorScheme schemeDefault =
{
    /* sNormal           */ {cFg        , cBg                    },
    /* sSelection        */ {{}         , cSelBg                 },
    /* sWhitespace       */ {cWhitespace, {}                     },
    /* sCtrlChar         */ {cCtrlChar  , {}                     },
    /* sLineNums         */ {cLineNums  , {}                     },
    /* sKeyword1         */ {cKeyword1  , {}                     },
    /* sKeyword2         */ {cKeyword2  , {}                     },
    /* sMisc             */ {cMisc      , {}      , slBold        },
    /* sPreprocessor     */ {cPreproc   , {}                     },
    /* sOperator         */ {cOperator  , {}                     },
    /* sComment          */ {cComment   , {}      , slItalic      },
    /* sStringLiteral    */ {cString    , {}                     },
    /* sCharLiteral      */ {cString    , {}                     },
    /* sNumberLiteral    */ {cNumber    , {}                     },
    /* sEscapeSequence   */ {cEscape    , {}                     },
    /* sError            */ {cErrorFg   , cErrorBg               },
    /* sBraceMatch       */ {cBraceMatch, {}      , slBold        },
    /* sReplaceHighlight */ {cReplaceFg , cReplaceBg             },
    // Identifiers default to the normal foreground: the role exists so themes can
    // pick them out, not to recolour every buffer by default. Tags/attributes
    // likewise default to the keyword colours markup already used, so enabling
    // these roles is visually inert until a lexer or theme asks for more.
    /* sIdentifier       */ {cFg        , {}                     },
    /* sFunctionName     */ {cFunction  , {}                     },
    /* sTypeName         */ {cTypeName  , {}                     },
    /* sConstant         */ {cConstant  , {}                     },
    /* sTag              */ {cKeyword1  , {}                     },
    /* sAttribute        */ {cKeyword2  , {}                     },
};

// Runtime-editable scheme. Filled from the factory default at static-init (see
// below) and again whenever the user resets; the theme dialog/settings loader
// mutate it, after which editors re-theme from it. Editors only ever read it at
// runtime, long after static initialization, so the init order is safe.
ColorScheme schemeActive;

void resetSchemeToDefault() noexcept
{
    for (int i = 0; i < TextStyleCount; ++i)
        schemeActive[i] = schemeDefault[i];
}

namespace { const bool schemeActiveInitialized = (resetSchemeToDefault(), true); }

namespace {
// Parallel tables, indexed by TextStyle. 'id' is persisted to ~/.turborc and is
// part of the on-disk format -- do not rename. 'label' is shown in the dialog.
struct StyleInfo { const char *id; const char *label; };
constexpr StyleInfo styleInfo[TextStyleCount] =
{
    /* sNormal           */ {"sNormal",           "Normal text"},
    /* sSelection        */ {"sSelection",        "Selection"},
    /* sWhitespace       */ {"sWhitespace",       "Whitespace"},
    /* sCtrlChar         */ {"sCtrlChar",         "Control char"},
    /* sLineNums         */ {"sLineNums",         "Line numbers"},
    /* sKeyword1         */ {"sKeyword1",         "Keyword"},
    /* sKeyword2         */ {"sKeyword2",         "Keyword (type)"},
    /* sMisc             */ {"sMisc",             "Misc"},
    /* sPreprocessor     */ {"sPreprocessor",     "Preprocessor"},
    /* sOperator         */ {"sOperator",         "Operator"},
    /* sComment          */ {"sComment",          "Comment"},
    /* sStringLiteral    */ {"sStringLiteral",    "String"},
    /* sCharLiteral      */ {"sCharLiteral",      "Character"},
    /* sNumberLiteral    */ {"sNumberLiteral",    "Number"},
    /* sEscapeSequence   */ {"sEscapeSequence",   "Escape sequence"},
    /* sError            */ {"sError",            "Error"},
    /* sBraceMatch       */ {"sBraceMatch",       "Matching brace"},
    /* sReplaceHighlight */ {"sReplaceHighlight", "Replace highlight"},
    /* sIdentifier       */ {"sIdentifier",       "Identifier"},
    /* sFunctionName     */ {"sFunctionName",     "Function name"},
    /* sTypeName         */ {"sTypeName",         "Type / class name"},
    /* sConstant         */ {"sConstant",         "Constant / symbol"},
    /* sTag              */ {"sTag",              "Markup tag"},
    /* sAttribute        */ {"sAttribute",        "Markup attribute"},
};
} // namespace

const char *styleName(TextStyle style) noexcept
{
    return (style < TextStyleCount) ? styleInfo[style].id : "";
}

const char *styleDisplayName(TextStyle style) noexcept
{
    return (style < TextStyleCount) ? styleInfo[style].label : "";
}

bool styleByName(TStringView name, TextStyle &out) noexcept
{
    for (int i = 0; i < TextStyleCount; ++i)
        if (name == styleInfo[i].id)
        {
            out = (TextStyle) i;
            return true;
        }
    return false;
}

constexpr LexerSettings::StyleMapping stylesLua[] =
{
    {SCE_LUA_DEFAULT,       sNormal},
    {SCE_LUA_COMMENT,       sComment},
    {SCE_LUA_COMMENTLINE,   sComment},
    {SCE_LUA_COMMENTDOC,    sComment},
    {SCE_LUA_NUMBER,        sNumberLiteral},
    {SCE_LUA_WORD,          sKeyword1},
    {SCE_LUA_STRING,        sStringLiteral},
    {SCE_LUA_CHARACTER,     sCharLiteral},
    {SCE_LUA_LITERALSTRING, sStringLiteral},
    {SCE_LUA_PREPROCESSOR,  sPreprocessor},
    {SCE_LUA_OPERATOR,      sOperator},
    {SCE_LUA_IDENTIFIER,    sNormal},
    {SCE_LUA_STRINGEOL,     sError},
    {SCE_LUA_WORD2,         sKeyword2},
    {SCE_LUA_WORD3,         sKeyword2},
    {SCE_LUA_WORD4,         sKeyword2},
    {SCE_LUA_WORD5,         sKeyword2},
    {SCE_LUA_WORD6,         sKeyword2},
    {SCE_LUA_WORD7,         sKeyword2},
    {SCE_LUA_WORD8,         sKeyword2},
    {SCE_LUA_LABEL,         sMisc},
};

constexpr LexerSettings::KeywordMapping keywordsLua[] =
{
    {0,
"and break do else elseif end false for function goto if in local nil not or "
"repeat return then true until while "
    },
    {1,
"assert collectgarbage dofile error getmetatable ipairs load loadfile next pairs "
"pcall print rawequal rawget rawlen rawset require select setmetatable tonumber "
"tostring type xpcall coroutine debug io math os package string table utf8 "
"_G _VERSION self "
    },
};

constexpr LexerSettings::StyleMapping stylesC[] =
{
    {SCE_C_DEFAULT,                 sNormal},
    {SCE_C_COMMENT,                 sComment},
    {SCE_C_COMMENTLINE,             sComment},
    {SCE_C_COMMENTDOC,              sComment},
    {SCE_C_NUMBER,                  sNumberLiteral},
    {SCE_C_WORD,                    sKeyword1},
    {SCE_C_STRING,                  sStringLiteral},
    {SCE_C_CHARACTER,               sCharLiteral},
    {SCE_C_PREPROCESSOR,            sPreprocessor},
    {SCE_C_OPERATOR,                sOperator},
    {SCE_C_COMMENTLINEDOC,          sComment},
    {SCE_C_WORD2,                   sKeyword2},
    {SCE_C_IDENTIFIER,              sIdentifier},
    {SCE_C_GLOBALCLASS,             sTypeName},
    {SCE_C_PREPROCESSORCOMMENT,     sComment},
    {SCE_C_PREPROCESSORCOMMENTDOC,  sComment},
    {SCE_C_ESCAPESEQUENCE,          sEscapeSequence},
};

constexpr LexerSettings::KeywordMapping keywordsC[] =
{
    {0,
"alignas alignof and and_eq asm auto bitand bitor break case catch class compl "
"concept consteval constexpr constinit const_cast continue co_await co_return "
"co_yield decltype default delete do dynamic_cast else enum explicit export "
"false final for friend goto if import inline module namespace new noexcept not "
"not_eq nullptr operator or or_eq override private protected public "
"reinterpret_cast return sizeof static_assert static_cast struct switch "
"template this throw true try typedef typeid typename union using virtual while "
"xor xor_eq "
    },
    {1,
"bool char char8_t char16_t char32_t const double extern float int long mutable "
"register static short signed unsigned thread_local void volatile wchar_t int8_t "
"uint8_t int16_t uint16_t int32_t uint32_t int64_t uint64_t size_t ptrdiff_t "
"intptr_t uintptr_t far near uchar ushort uint ulong "
    },
    {3,
"std"
    },
};

constexpr LexerSettings::PropertyMapping propertiesC[] =
{
    {"styling.within.preprocessor",         "1"},
    {"lexer.cpp.track.preprocessor",        "0"},
    {"lexer.cpp.escape.sequence",           "1"},
};

// JavaScript also runs on LexCPP, but it must not share C's tables: the two
// options below are actively wrong for C (allow.hashes would stop '#include'
// being a preprocessor line), and C has no use for the two extra styles. LexCPP
// already implements all of this -- turbo simply never asked for it.
constexpr LexerSettings::StyleMapping stylesJavaScript[] =
{
    {SCE_C_DEFAULT,                 sNormal},
    {SCE_C_COMMENT,                 sComment},
    {SCE_C_COMMENTLINE,             sComment},
    {SCE_C_COMMENTDOC,              sComment},
    {SCE_C_NUMBER,                  sNumberLiteral},
    {SCE_C_WORD,                    sKeyword1},
    {SCE_C_STRING,                  sStringLiteral},
    {SCE_C_CHARACTER,               sCharLiteral},
    {SCE_C_OPERATOR,                sOperator},
    {SCE_C_COMMENTLINEDOC,          sComment},
    {SCE_C_WORD2,                   sKeyword2},
    {SCE_C_IDENTIFIER,              sIdentifier},
    {SCE_C_GLOBALCLASS,             sTypeName},
    {SCE_C_ESCAPESEQUENCE,          sEscapeSequence},
    // LexCPP lexes JS regex literals correctly (it disambiguates them from
    // division), but turbo never mapped the style, so they were painted as plain
    // text.
    {SCE_C_REGEX,                   sMisc},
    // Template literals. With lexer.cpp.backquoted.strings=2 LexCPP styles the
    // backquoted string as STRINGRAW and lexes `${...}` as real code, with a
    // nesting stack -- so the interpolation is not part of this run.
    {SCE_C_STRINGRAW,               sStringLiteral},
    // An unterminated string/char literal. Worth colouring as an error rather
    // than leaving it to fall through as plain text.
    {SCE_C_STRINGEOL,               sError},
    // Deliberately no SCE_C_PREPROCESSOR: with allow.hashes below, a leading '#'
    // is an identifier character (a #private field), not a directive.
};

constexpr LexerSettings::PropertyMapping propertiesJavaScript[] =
{
    {"styling.within.preprocessor",         "1"},
    {"lexer.cpp.track.preprocessor",        "0"},
    {"lexer.cpp.escape.sequence",           "1"},
    // 2 = JavaScript template literal: `a ${b} c` with the interpolation lexed
    // as code. Left unset, a backtick is not a delimiter at all and an
    // apostrophe in template prose opens a bogus char literal to end of line.
    {"lexer.cpp.backquoted.strings",        "2"},
    // Let '#' start an identifier, so a `#private` class field at the start of a
    // line is not taken for a C preprocessor directive.
    {"lexer.cpp.allow.hashes",              "1"},
};

constexpr LexerSettings::StyleMapping stylesMake[] =
{
    {SCE_MAKE_DEFAULT,              sNormal},
    {SCE_MAKE_COMMENT,              sComment},
    {SCE_MAKE_TARGET,               sKeyword1},
    {SCE_MAKE_IDENTIFIER,           sPreprocessor},
    {SCE_MAKE_PREPROCESSOR,         sPreprocessor},
    {SCE_MAKE_OPERATOR,             sOperator},
};

constexpr LexerSettings::StyleMapping stylesAsm[] =
{
    {SCE_ASM_DEFAULT,               sNormal},
    {SCE_ASM_COMMENT,               sComment},
    {SCE_ASM_COMMENTBLOCK,          sComment},
    {SCE_ASM_NUMBER,                sNumberLiteral},
    {SCE_ASM_CPUINSTRUCTION,        sKeyword1},
    {SCE_ASM_MATHINSTRUCTION,       sKeyword1},
    {SCE_ASM_STRING,                sStringLiteral},
    {SCE_ASM_CHARACTER,             sCharLiteral},
    {SCE_ASM_DIRECTIVE,             sPreprocessor},
};

// Serves both JavaScript and TypeScript. TypeScript's extra keywords are not
// reserved in JavaScript, but they are also vanishingly rare as identifiers
// there, so one list for both is a better trade than a second near-duplicate.
constexpr LexerSettings::KeywordMapping keywordsJsTs[] =
{
    {0,
"await break case catch continue default do else export false finally "
"for get if import new null return set super switch this throw true try while "
"with yield "
// TypeScript's own keywords. Harmless in a .js file (they are not reserved
// there, but neither are they common as identifiers), which is why one list
// serves both.
"abstract as asserts declare implements infer interface is keyof namespace "
"override private protected public readonly satisfies type"
    },
    {1,
"async class const debugger delete enum eval extends function in instanceof let "
"static typeof var void "
// TS primitive/utility types, so annotations colour as types rather than
// as bare identifiers.
"any bigint boolean never null number object string symbol undefined unknown"
    },
    {3,
"arguments Array ArrayBuffer AsyncFunction Atomics BigInt BigInt64Array "
"BigUint64Array Boolean DataView Date Error EvalError Float32Array Float64Array "
"Function Generator GeneratorFunction globalThis Infinity Int8Array Int16Array "
"Int32Array InternalError Intl JSON Map Math NaN Number Object Promise Proxy "
"RangeError ReferenceError Reflect RegExp Set String SyntaxError TypeError URIError "
"SharedArrayBuffer Symbol Uint8Array Uint8ClampedArray Uint16Array Uint32Array "
"undefined WeakMap WeakSet WebAssembly"
    },
};

constexpr LexerSettings::StyleMapping stylesHTML[] =
{
    // HTML
    { SCE_H_DEFAULT, sNormal},
    { SCE_H_TAG, sTag},
    { SCE_H_TAGUNKNOWN, sError},
    { SCE_H_ATTRIBUTE, sAttribute},
    { SCE_H_ATTRIBUTEUNKNOWN, sAttribute},
    { SCE_H_NUMBER, sNumberLiteral},
    { SCE_H_DOUBLESTRING, sStringLiteral},
    { SCE_H_SINGLESTRING, sStringLiteral},
    { SCE_H_OTHER, sNormal},
    { SCE_H_COMMENT, sComment},
    { SCE_H_ENTITY, sNormal},
    { SCE_H_TAGEND, sTag},
    { SCE_H_XMLSTART, sTag},
    { SCE_H_XMLEND,  sTag},
    { SCE_H_SCRIPT, sStringLiteral},
    { SCE_H_ASP, sStringLiteral},
    { SCE_H_ASPAT, sStringLiteral},
    { SCE_H_CDATA, sStringLiteral},
    { SCE_H_QUESTION, sNormal},
    { SCE_H_VALUE, sCharLiteral},
    { SCE_H_XCCOMMENT, sComment},
    // SGML (DTD)
    { SCE_H_SGML_DEFAULT, sNormal},
    { SCE_H_SGML_COMMAND, sKeyword2},
    { SCE_H_SGML_1ST_PARAM, sNormal},
    { SCE_H_SGML_DOUBLESTRING, sStringLiteral},
    { SCE_H_SGML_SIMPLESTRING, sStringLiteral},
    { SCE_H_SGML_ERROR, sError},
    { SCE_H_SGML_SPECIAL, sKeyword1},
    { SCE_H_SGML_ENTITY, sNormal},
    { SCE_H_SGML_COMMENT, sComment},
    { SCE_H_SGML_1ST_PARAM_COMMENT, sComment},
    { SCE_H_SGML_BLOCK_DEFAULT, sNormal},
    // JavaScript
    { SCE_HJ_DEFAULT, sNormal },
    { SCE_HJ_COMMENT, sComment },
    { SCE_HJ_COMMENTLINE, sComment },
    { SCE_HJ_COMMENTDOC, sComment },
    { SCE_HJ_NUMBER, sNumberLiteral },
    { SCE_HJ_WORD, sNormal },
    { SCE_HJ_KEYWORD, sKeyword1 },
    { SCE_HJ_DOUBLESTRING, sStringLiteral },
    { SCE_HJ_SINGLESTRING, sCharLiteral },
    { SCE_HJ_SYMBOLS, sCtrlChar },
    { SCE_HJ_REGEX, sMisc },
    // PHP
    { SCE_HPHP_COMPLEX_VARIABLE, sKeyword1 },
    { SCE_HPHP_DEFAULT, sNormal },
    { SCE_HPHP_HSTRING, sStringLiteral },
    { SCE_HPHP_SIMPLESTRING, sCharLiteral },
    { SCE_HPHP_WORD, sKeyword2 },
    { SCE_HPHP_NUMBER, sNumberLiteral },
    { SCE_HPHP_VARIABLE, sKeyword1 },
    { SCE_HPHP_COMMENT, sComment },
    { SCE_HPHP_COMMENTLINE, sComment },
    { SCE_HPHP_HSTRING_VARIABLE, sKeyword1 },
    { SCE_HPHP_OPERATOR, sOperator }
};

constexpr LexerSettings::KeywordMapping keywordsHTML[] =
{
    {0, // HTML
"a abbr address area article aside audio b base bdi bdo blockquote "
"body br button canvas caption cite code col colgroup data datalist dd del "
"details dfn dialog div dl dt em embed fieldset figure footer form h1 h2 h3 "
"h4 h5 h6 head header hgroup hr html i iframe img input ins kbd keygen label "
"legend li link main map mark menu menuitem meta meter nav noscript object "
"ol optgroup option output p param pre progress q rb rp rt rtc ruby s samp "
"script section select small source span strong style sub summary sup table "
"tbody td template textarea tfoot th thead time title tr track u ul var video "
"wbr strike tt acronym applet basefont big center dir font frame frameset noframes "
    // SVG
"svg animate animateMotion animateTransform circle clipPath defs desc discard "
"ellipse feBlend feColorMatrix feComponentTrasfer feComposite feConvolveMatrix "
"feDiffuseLighting feDisplacementMap feDistantLight feDropShadow feFlood feFuncA "
"feFuncB feFuncG feFuncR feGaussianBlur feImage feMerge feMergeNode feMorphology "
"feOffset fePointLight feSpecularLighting feSpotLight feTile feTurbulence filter "
"foreignObject g hatch hatchpath image line linearGradient marker mask metadata "
"mpath path pattern polygon polyline radialGradient rect set stop style switch "
"symbol text textPath title tspan use view "
    },
    {1, // JavaScript
"of await break case catch continue default do else export false finally for "
"get if import new null return set super switch this throw true try while with "
"yield async class const debugger delete enum eval extends function in instanceof "
"let static typeof var void undefined "
    },
    {2, // VBScript
" "
    },
    {3, // Python
" "
    },
    {4, // PHP
// Keywords
"__halt_compiler abstract and array as break callable case catch class "
"clone const continue declare default die do echo else elseif "
"empty enddeclare endfor endforeach endif endswitch endwhile eval exit extends "
"final finally fn for foreach function global goto if implements "
"include include_once instanceof insteadof interface isset list match namespace new "
"or print private protected public readonly require require_once return static "
"switch throw trait try unset use var while xor yield "
// PHP 8.1: 'enum' and 'never' were missing, so enums and never-returning
// functions rendered unstyled. LexHTML classifies PHP words by a plain wordlist
// lookup, so this is entirely turbo's list to keep current.
"enum never "
"from __CLASS__ __DIR__ __FILE__ __FUNCTION__ __LINE__ __METHOD__ __NAMESPACE__ __TRAIT__ "

// Builtins
"abs acos acosh addcslashes addslashes array_change_key_case array_chunk array_column array_combine "
"array_count_values array_diff array_diff_assoc array_diff_key array_diff_uassoc array_diff_ukey "
"array_fill array_fill_keys array_filter array_flip array_intersect array_intersect_assoc "
"array_intersect_key array_intersect_uassoc array_intersect_ukey array_is_list array_key_exists "
"array_key_first array_key_last array_keys array_map array_merge array_merge_recursive array_multisort "
"array_pad array_pop array_product array_push array_rand array_reduce array_replace array_replace_recursive "
"array_reverse array_search array_shift array_slice array_splice array_sum array_udiff array_udiff_assoc "
"array_udiff_uassoc array_uintersect array_uintersect_assoc array_uintersect_uassoc array_unique "
"array_unshift array_values array_walk array_walk_recursive arsort asin asinh asort assert "
"assert_options atan atan2 atanh base64_decode base64_encode base_convert basename bin2hex "
"bindec boolval call_user_func call_user_func_array ceil chdir checkdate chgrp chmod chown chr "
"chroot chunk_split class_implements class_parents class_uses clearstatcache closedir closelog "
"compact config_get_hash connection_aborted connection_status constant convert_uudecode "
"convert_uuencode copy cos cosh count count_chars crc32 crypt current date date_add date_create "
"date_create_from_format date_create_immutable date_create_immutable_from_format date_date_set "
"date_default_timezone_get date_default_timezone_set date_diff date_format date_get_last_errors "
"date_interval_create_from_date_string date_interval_format date_isodate_set date_modify "
"date_offset_get date_parse date_parse_from_format date_sub date_sun_info date_sunrise date_sunset "
"date_time_set date_timestamp_get date_timestamp_set date_timezone_get date_timezone_set "
"debug_zval_dump decbin dechex decoct deg2rad dir dirname disk_free_space disk_total_space "
"dl dns_check_record dns_get_mx dns_get_record end error_clear_last error_get_last error_log "
"escapeshellarg escapeshellcmd exec exp explode expm1 extract fclose fdatasync fdiv feof fflush "
"fgetc fgetcsv fgets file file_get_contents file_put_contents filter_has_var filter_id filter_input "
"filter_input_array filter_list filter_var filter_var_array floatval flock floor flush fmod "
"fnmatch fopen forward_static_call forward_static_call_array fpassthru fprintf fputcsv fread "
"fscanf fseek fsockopen fstat fsync ftell ftok ftruncate fwrite get_browser get_cfg_var get_current_user "
"getcwd getdate get_debug_type getenv get_headers gethostbyaddr gethostbyname gethostbynamel "
"gethostname get_html_translation_table getimagesize getimagesizefromstring get_include_path "
"getlastmod get_meta_tags getmygid getmyinode getmypid getmyuid getopt getprotobyname getprotobynumber "
"getrusage getservbyname getservbyport gettimeofday gettype glob gmdate gmmktime gmstrftime "
"hash hash_algos hash_copy hash_equals hash_file hash_final hash_hkdf hash_hmac hash_hmac_algos "
"hash_hmac_file hash_init hash_pbkdf2 hash_update hash_update_file hash_update_stream header "
"header_remove headers_list headers_sent hebrev hex2bin hexdec highlight_file highlight_string "
"hrtime htmlentities html_entity_decode htmlspecialchars htmlspecialchars_decode http_build_query "
"http_response_code hypot idate ignore_user_abort image_type_to_extension image_type_to_mime_type "
"implode in_array inet_ntop inet_pton ini_get ini_get_all ini_parse_quantity ini_restore ini_set "
"intdiv intval ip2long iptcembed iptcparse is_array is_bool is_callable is_countable is_finite "
"is_float is_infinite is_int is_iterable is_nan is_null is_numeric is_object is_resource is_scalar "
"is_string is_uploaded_file iterator_apply iterator_count iterator_to_array json_decode "
"json_encode json_last_error json_last_error_msg json_validate key krsort ksort lcfirst lcg_value "
"lchgrp lchown levenshtein link linkinfo localeconv localtime log log10 log1p long2ip ltrim mail "
"max md5 md5_file memory_get_peak_usage memory_get_usage memory_reset_peak_usage metaphone "
"mhash mhash_count mhash_get_block_size mhash_get_hash_name mhash_keygen_s2k microtime min "
"mkdir mktime move_uploaded_file mt_getrandmax mt_rand mt_srand natcasesort natsort net_get_interfaces "
"next nl2br nl_langinfo number_format octdec opendir openlog ord pack parse_ini_file parse_ini_string "
"parse_str parse_url passthru password_algos password_get_info password_hash password_needs_rehash "
"password_verify pathinfo pclose pdo_drivers pfsockopen phpcredits phpinfo php_ini_loaded_file "
"php_ini_scanned_files php_sapi_name php_strip_whitespace php_uname phpversion pi popen pow "
"preg_filter preg_grep preg_last_error preg_last_error_msg preg_match preg_match_all preg_quote "
"preg_replace preg_replace_callback preg_replace_callback_array preg_split prev printf print_r "
"proc_close proc_get_status proc_nice proc_open proc_terminate putenv quoted_printable_decode "
"quoted_printable_encode quotemeta rad2deg rand random_bytes random_int range rawurldecode "
"rawurlencode readdir readfile readlink realpath realpath_cache_get realpath_cache_size register_shutdown_function "
"register_tick_function rename reset rewind rewinddir rmdir round rsort rtrim sapi_windows_vt100_support "
"scandir serialize session_abort session_cache_expire session_cache_limiter session_create_id "
"session_decode session_destroy session_encode session_gc session_get_cookie_params session_id "
"session_module_name session_name session_regenerate_id session_register_shutdown session_reset "
"session_save_path session_set_cookie_params session_set_save_handler session_start session_status "
"session_unset session_write_close setcookie set_include_path setlocale setrawcookie settype "
"sha1 sha1_file shell_exec shuffle similar_text sin sinh sleep sort soundex spl_autoload spl_autoload_call "
"spl_autoload_extensions spl_autoload_functions spl_autoload_register spl_autoload_unregister "
"spl_classes spl_object_hash spl_object_id sprintf sqrt sscanf strcoll str_contains strcspn "
"str_decrement stream_bucket_append stream_bucket_make_writeable stream_bucket_new stream_bucket_prepend "
"stream_context_create stream_context_get_default stream_context_get_options stream_context_get_params "
"stream_context_set_default stream_context_set_option stream_context_set_options stream_context_set_params "
"stream_copy_to_stream stream_filter_append stream_filter_prepend stream_filter_register "
"stream_filter_remove stream_get_contents stream_get_filters stream_get_line stream_get_meta_data "
"stream_get_transports stream_get_wrappers stream_isatty stream_is_local stream_resolve_include_path "
"stream_select stream_set_blocking stream_set_chunk_size stream_set_read_buffer stream_set_timeout "
"stream_set_write_buffer stream_socket_accept stream_socket_client stream_socket_enable_crypto "
"stream_socket_get_name stream_socket_pair stream_socket_recvfrom stream_socket_sendto "
"stream_socket_server stream_socket_shutdown stream_supports_lock str_ends_with strftime "
"str_getcsv str_increment stripcslashes stripos stripslashes strip_tags str_ireplace stristr "
"strnatcasecmp strnatcmp str_pad strpbrk strpos strptime strrchr str_repeat str_replace strrev "
"strripos str_rot13 strrpos str_shuffle str_split strspn str_starts_with strstr strtok strtolower "
"strtotime strtoupper strtr strval str_word_count substr substr_compare substr_count substr_replace "
"symlink sys_getloadavg sys_get_temp_dir syslog system tan tanh tempnam time time_nanosleep "
"time_sleep_until timezone_abbreviations_list timezone_identifiers_list timezone_location_get "
"timezone_name_from_abbr timezone_name_get timezone_offset_get timezone_open timezone_transitions_get "
"timezone_version_get tmpfile touch trim uasort ucfirst ucwords uksort umask uniqid unlink unpack "
"unregister_tick_function unserialize urldecode urlencode usleep usort utf8_decode utf8_encode "
"var_dump var_export version_compare vfprintf vprintf vsprintf wordwrap "

    },
    {5, // SGML (DTD)
"DOCTYPE ELEMENT ATTLIST ENTITY PCDATA CDATA EMPTY SHORTREF USEMAP NOTATION IMPLIED "
"NDATA SYSTEM SGML HTML PUBLIC "
    },
};

constexpr LexerSettings::PropertyMapping propertiesHTML[] =
{
    {"asp.default.language",                "1"},
    {"html.tags.case.sensitive",            "0"},
    {"lexer.xml.allow.scripts",             "1"},
    {"lexer.html.mako",                     "0"},
    {"lexer.html.django",                   "0"},
    {"fold",                                "0"},
    {"fold.html",                           "0"},
    {"fold.html.preprocessor",              "0"},
    {"fold.compact",                        "0"},
    {"fold.hypertext.comment",              "0"},
    {"fold.hypertext.heredoc",              "0"},
    {"fold.xml.at.tag.open",                "0"},
};

constexpr LexerSettings::StyleMapping  stylesRust[] =
{
    {SCE_RUST_DEFAULT,              sNormal},
    {SCE_RUST_COMMENTBLOCK,         sComment},
    {SCE_RUST_COMMENTLINE,          sComment},
    {SCE_RUST_COMMENTBLOCKDOC,      sComment},
    {SCE_RUST_COMMENTLINEDOC,       sComment},
    {SCE_RUST_NUMBER,               sNumberLiteral},
    {SCE_RUST_WORD,                 sKeyword1},
    {SCE_RUST_WORD2,                sKeyword2},
    {SCE_RUST_STRING,               sStringLiteral},
    {SCE_RUST_STRINGR,              sStringLiteral},
    {SCE_RUST_CHARACTER,            sCharLiteral},
    {SCE_RUST_MACRO,                sPreprocessor},
    {SCE_RUST_OPERATOR,             sOperator},
    {SCE_RUST_LIFETIME,             sMisc},
    {SCE_RUST_BYTESTRING,           sEscapeSequence},
    {SCE_RUST_BYTESTRINGR,          sEscapeSequence},
    {SCE_RUST_BYTECHARACTER,        sEscapeSequence},
};

constexpr LexerSettings::KeywordMapping keywordsRust[] =
{
    {0,
"as break const continue crate dyn else enum extern false fn for if impl in let "
"loop match mod move mut pub ref return self Self static struct super trait true "
"type unsafe use where while "
    },
    {1,
"bool u8 u16 u32 u64 u128 i8 i16 i32 i64 i128 f32 f64 usize isize char str Pair "
"Box box String List"
    },
};

constexpr LexerSettings::StyleMapping  stylesPython[] =
{
    {SCE_P_DEFAULT,                 sNormal},
    {SCE_P_COMMENTLINE,             sComment},
    {SCE_P_NUMBER,                  sNumberLiteral},
    {SCE_P_STRING,                  sStringLiteral},
    {SCE_P_CHARACTER,               sCharLiteral},
    {SCE_P_WORD,                    sKeyword1},
    {SCE_P_TRIPLE,                  sStringLiteral},
    {SCE_P_TRIPLEDOUBLE,            sStringLiteral},
    {SCE_P_CLASSNAME,               sTypeName},
    {SCE_P_DEFNAME,                 sFunctionName},
    {SCE_P_OPERATOR,                sOperator},
    {SCE_P_IDENTIFIER,              sIdentifier},
    {SCE_P_COMMENTBLOCK,            sComment},
    {SCE_P_STRINGEOL,               sNormal},
    {SCE_P_WORD2,                   sMisc},
    {SCE_P_DECORATOR,               sPreprocessor},
    {SCE_P_FSTRING,                 sStringLiteral},
    {SCE_P_FCHARACTER,              sCharLiteral},
    {SCE_P_FTRIPLE,                 sStringLiteral},
    {SCE_P_FTRIPLEDOUBLE,           sStringLiteral},
};

constexpr LexerSettings::KeywordMapping keywordsPython[] =
{
    {0,
"and as assert break class continue def del elif else except exec finally for "
"from global if import in is lambda not or pass print raise return try while "
"with yield"
    },
    {1,
"int float complex list tuple range str bytes bytearray memoryview set frozenset "
"dict "
    },
};

constexpr LexerSettings::PropertyMapping propertiesPython[] =
{
    {"lexer.python.keywords2.no.sub.identifiers",       "1"},
};

constexpr LexerSettings::StyleMapping  stylesBash[] =
{
    {SCE_SH_DEFAULT,                sNormal},
    {SCE_SH_ERROR,                  sError},
    {SCE_SH_COMMENTLINE,            sComment},
    {SCE_SH_NUMBER,                 sNumberLiteral},
    {SCE_SH_WORD,                   sKeyword2},
    {SCE_SH_STRING,                 sStringLiteral},
    {SCE_SH_CHARACTER,              sCharLiteral},
    {SCE_SH_OPERATOR,               sOperator},
    {SCE_SH_IDENTIFIER,             sNormal},
    {SCE_SH_SCALAR,                 sKeyword1},
    {SCE_SH_PARAM,                  sKeyword1},
    {SCE_SH_BACKTICKS,              sKeyword1},
    {SCE_SH_HERE_DELIM,             sMisc},
    {SCE_SH_HERE_Q,                 sMisc},
};

constexpr LexerSettings::KeywordMapping keywordsBash[] =
{
    {0,
// Keywords
"case do done elif else esac fi for function if in select then time until while "
// Builtins
"alias bg bind break builtin caller cd command compgen complete compopt continue "
"declare dirs disown echo enable eval exec exit export fc fg getopts hash help "
"history jobs kill let local logout mapfile popd printf pushd pwd read readarray "
"readonly return set shift shopt source suspend test times trap type typeset ulimit "
"umask unalias unset wait "
    },
};

constexpr LexerSettings::StyleMapping  stylesRuby[] =
{
    {SCE_RB_DEFAULT,                sNormal},
    {SCE_RB_ERROR,                  sError},
    {SCE_RB_COMMENTLINE,            sComment},
    {SCE_RB_POD,                    sMisc},
    {SCE_RB_NUMBER,                 sNumberLiteral},
    {SCE_RB_WORD,                   sKeyword1},
    {SCE_RB_STRING,                 sStringLiteral},
    {SCE_RB_CHARACTER,              sCharLiteral},
    {SCE_RB_CLASSNAME,              sTypeName},
    {SCE_RB_DEFNAME,                sFunctionName},
    {SCE_RB_OPERATOR,               sOperator},
    {SCE_RB_IDENTIFIER,             sIdentifier},
    {SCE_RB_REGEX,                  sNormal},
    {SCE_RB_GLOBAL,                 sNormal},
    {SCE_RB_SYMBOL,                 sConstant},
    {SCE_RB_MODULE_NAME,            sTypeName},
    {SCE_RB_INSTANCE_VAR,           sNormal},
    {SCE_RB_CLASS_VAR,              sNormal},
    {SCE_RB_BACKTICKS,              sKeyword1},
    {SCE_RB_HERE_DELIM,             sMisc},
    {SCE_RB_HERE_Q,                 sMisc},
    {SCE_RB_STRING_QQ,              sMisc},
    {SCE_RB_STRING_QX,              sMisc},
    {SCE_RB_STRING_QR,              sMisc},
    {SCE_RB_STRING_QW,              sMisc},
    {SCE_RB_WORD_DEMOTED,           sNormal},
    {SCE_RB_STDIN,                  sMisc},
    {SCE_RB_STDOUT,                 sMisc},
    {SCE_RB_STDERR,                 sMisc},
    {SCE_RB_UPPER_BOUND,            sMisc},
};

constexpr LexerSettings::KeywordMapping keywordsRuby[] =
{
    {0,
"__ENCODING__ __LINE__ __FILE__ BEGIN END "
"alias and begin break case class def defined? do else elsif end ensure false "
"for if in module next nil not or redo rescue retry return self super then true "
"undef unless until when while yield "
"public require require_relative "
    },
};

constexpr LexerSettings::StyleMapping stylesJSON[] =
{
    {SCE_JSON_DEFAULT,              sNormal},
    {SCE_JSON_NUMBER,               sNumberLiteral},
    {SCE_JSON_STRING,               sStringLiteral},
    {SCE_JSON_STRINGEOL,            sStringLiteral},
    {SCE_JSON_PROPERTYNAME,         sPreprocessor},
    {SCE_JSON_ESCAPESEQUENCE,       sEscapeSequence},
    {SCE_JSON_LINECOMMENT,          sComment},
    {SCE_JSON_BLOCKCOMMENT,         sComment},
    {SCE_JSON_OPERATOR,             sOperator},
    {SCE_JSON_URI,                  sStringLiteral},
    {SCE_JSON_COMPACTIRI,           sKeyword2},
    {SCE_JSON_KEYWORD,              sKeyword1},
    {SCE_JSON_LDKEYWORD,            sKeyword2},
    {SCE_JSON_ERROR,                sError},
};

constexpr LexerSettings::KeywordMapping keywordsJSON[] =
{
    {0, "false true null"},
    {1,
"@id @context @type @value @language @container @list @set @reverse @index "
"@base @vocab @graph "
    },
};

constexpr LexerSettings::PropertyMapping propertiesJSON[] =
{
    {"lexer.json.escape.sequence", "1"},
    {"lexer.json.allow.comments", "1"},
};

constexpr LexerSettings::StyleMapping stylesYAML[] =
{
    {SCE_YAML_DEFAULT,              sNormal},
    {SCE_YAML_COMMENT,              sComment},
    {SCE_YAML_IDENTIFIER,           sKeyword1},
    {SCE_YAML_KEYWORD,              sKeyword2},
    {SCE_YAML_NUMBER,               sNumberLiteral},
    {SCE_YAML_REFERENCE,            sMisc},
    {SCE_YAML_DOCUMENT,             sMisc},
    {SCE_YAML_TEXT,                 sStringLiteral},
    {SCE_YAML_ERROR,                sError},
    {SCE_YAML_OPERATOR,             sOperator},
};

constexpr LexerSettings::KeywordMapping keywordsYAML[] =
{
    {0, "true false yes no"},
};

constexpr LexerSettings::KeywordMapping keywordsCSharp[] =
{
    {0,
"abstract as base bool break case catch checked class const continue default "
"delegate do else enum event explicit extern false finally fixed for foreach "
"goto if implicit in interface internal is lock namespace new null object operator "
"out override params private protected public readonly ref return sealed sizeof "
"stackalloc static struct switch this throw true try typeof unchecked unsafe "
"using virtual void volatile while "
"add and alias ascending args async await by descending dynamic equals file from "
"get global group init into join let managed nameof not notnull on or orderby "
"partial record remove required scoped select set unmanaged value var when where "
"with yield "
    },
    {1,
"bool byte sbyte char decimal double float in uint nint nuint long ulong short ushort "
"object string dynamic "
    },
    {3,
"System"
    },
};

constexpr LexerSettings::StyleMapping stylesProperties[] =
{
    {SCE_PROPS_DEFAULT,             sNormal},
    {SCE_PROPS_COMMENT,             sComment},
    {SCE_PROPS_SECTION,             sKeyword2},
    {SCE_PROPS_ASSIGNMENT,          sOperator},
    {SCE_PROPS_DEFVAL,              sMisc},
    {SCE_PROPS_KEY,                 sKeyword1},
};

constexpr LexerSettings::StyleMapping stylesBasic[] =
{
    {SCE_B_DEFAULT,                 sNormal},
    {SCE_B_COMMENT,                 sComment},
    {SCE_B_NUMBER,                  sNumberLiteral},
    {SCE_B_KEYWORD,                 sKeyword1},
    {SCE_B_STRING,                  sStringLiteral},
    {SCE_B_PREPROCESSOR,            sPreprocessor},
    {SCE_B_OPERATOR,                sOperator},
    {SCE_B_IDENTIFIER,              sNormal},
    {SCE_B_DATE,                    sCharLiteral},
    {SCE_B_STRINGEOL,               sNormal},
    {SCE_B_KEYWORD2,                sKeyword2},
    {SCE_B_KEYWORD3,                sKeyword2},
    {SCE_B_KEYWORD4,                sKeyword2},
    {SCE_B_CONSTANT,                sNormal},
    {SCE_B_ASM,                     sMisc},
    {SCE_B_LABEL,                   sSelection},
    {SCE_B_ERROR,                   sError},
    {SCE_B_HEXNUMBER,               sNumberLiteral},
    {SCE_B_BINNUMBER,               sNumberLiteral},
    {SCE_B_COMMENTBLOCK,            sComment},
    {SCE_B_DOCLINE,                 sMisc},
    {SCE_B_DOCBLOCK,                sMisc},
    {SCE_B_DOCKEYWORD,              sMisc},
};

constexpr LexerSettings::KeywordMapping keywordsBasic[] =
{
    {0,
    // QBasic
"and as call case do loop if then else elseif end exit for next function gosub "
"goto not on or return select sub to type until wend while xor "
    // VB6
"false new true "
    },
    {1,
    // QBasic
"abs absolute access alias append asc atn beep binary bload bsave byval "
"cdbl chain chdir chr cint circle clear clng close cls color command common "
"const cos csng csrlin cvd cvdmbf cvi cvl cvs cvsmbf data date declare library "
"def seg defdbl defint deflng defsng defstr dim double draw "
"environ eof eqv erase erl err error exp field files fix free freefile "
"get hex imp inkey inp input instr int integer interrupt "
"interruptx key kill lbound lcase left len line list loc locate lock lof log "
"long lprint using lset ltrim mid mkd mkdir mkdmbf mki mkl mks mksmbf mod name "
"oct off strig open com out output paint palette pcopy peek "
"play pmap point poke pos preset print pset put random randomize read redim reset restore "
"resume right rmdir rnd rset rtrim run sadd screen seek sgn shared shell "
"sin single sleep sound space spc sqr step stick stop str string swap system "
"tab tan time timer ubound ucase unlock val varptr varseg view "
"wait width window write option explicit base "
    // VB6
"addressof attribute appactivate begin beginproperty boolean byref chdrive class "
"collection defbool defbyte defdate defdec defcur defobj defvar deletesetting "
"each endproperty enum event filecopy friend global implements in is load lib "
"like me nothing null object local optional compare module paramarray private "
"property public raiseevent savepicture savesetting sendkeys set setattr text "
"typeof unload variant version with withevents "
    // QB64
"_acceptfiledrop _acos _acosh _allowfullscreen _alpha _alpha32 _arrcot _arccsc _arcsec _asin _asinh "
"_assert _atan2 _atanh _autodisplay _axis _backgroundcolor _bit _bin _bin _blend _blink "
"_blue _blue32 _button _buttonchange _byte _capslock _ceil _cinp _clearcolor _clip _clipboard "
"_clipboardimage _colorchoosedialog _commandcount _connected _connectionaddress "
"_consoleinput _consoletitle _continue _controlchr _copyimage _copypalette _cot _coth _cosh "
"_csc _csch _cw _cwd _d2g _d2r _defaultcolor _define _deflate _delay _depthbuffer _desktopheight "
"_desktopwidth _dest _device _deviceinput _devices _dir _direxists _display _displayorder _dontblend "
"_dontwait _droppedfile _environcount _errorline _errormessage "
"_fileexists _finishdrop _float _font _fontheight _fontwidth _freefont _freeimage "
"_freetimer _fullscreen _g2d _g2r _glrender _green _green32 _height _hide _hypot _icon _inclerrofile "
"_inclerroline _inflate _inputbox _instrrev _integer64 _keyclear _keyhit _keydown _lastaxis "
"_lastbutton _lastwheel _limit _loadfont _loadimage _maptriangle _mapunicode _mem _memcopy _memelement "
"_memexists _memfill _memfree _memget _memimage _memnew _memput _memsound _messagebox _middle "
"_mk _mousebutton _mousehide _mouseinput _mousemove _mousemovementx _mousemovementy _mouseshow _mousewheel "
"_mousex _mousey _newimage _notifypopup _numlock _offset _openclient _openconnection "
"_openfiledialog _openhost _os _palettecolor _pi _pixelsize _preserve "
"_printimage _printmode _printstring _printwidth _putimage _r2d _r2g _red _red32 _readbit _resetbit "
"_resize _resizeheight _resizewidth _rgb _rgb32 _rgba _rgba32 _rol _ror _round _savefiledialog "
"_sec _sech _selectfolderdialog _screenclick _screenexists _screenhide _screenicon "
"_screenimage _screenmove _screenprint _screenshow _screenx _screeny _scrollock _setalpha "
"_setbit _shellhide _shl _shr _sinh _smooth _sndbal _sndclose _sndcopy _sndgetpos _sndlen _sndlimit _sndloop "
"_sndnew _sndopen _sndopenraw _sndpause _sndpaused _sndplay _sndplaycopy _sndplayfile _sndplaying "
"_sndrate _sndraw _sndrawdone _sndrawlen _sndsetpos _sndstop _sndvol _source _startdir _statuscode "
"_strcmp _stricmp _tanh _title _title _togglebit _totaldroppedfiles _trim _unsigned "
"_wheel _windowhandle _windowhasfocus _echo _exeicon _asserts _checking _console _debug _let "
"_midisoundfont _noprefix _unstable _versioninfo _virtualkeyboard _dynamic _include _static _explicitarray "
    },
};

constexpr LexerSettings::KeywordMapping keywordsVbNet[] =
{
    {0,
"and andalso as call case catch class continue do each else elseif end endif "
"enum exit false finally for function gosub goto if in loop new operator next "
"not or orelse return structure sub then throw to true try using wend when "
"while with xor "
    },
    {1,
"addhandler addressof alias boolean byref byte byval cbool "
"cbyte cchar cdate cdbl cdec char cint constraint clng cobj const csbyte "
"cshort csng cstr ctype cuint culng cushort date decimal declare default delegate dim "
"directcast double erase error event "
"friend get gettype getxmlnamespace global handles implements "
"statement imports inherits integer interface is isnot let lib like long me mod "
"module mustinherit mustoverride mybase myclass nameof namespace narrowing "
"nothing notinheritable notoverridable object of on option optional "
"out overloads overridable overrides paramarray partial private property protected public "
"raiseevent readonly redim removehandler resume sbyte select set shadows shared "
"short single static step stop string synclock trycast "
"typeof uinteger ulong ushort variant widening withevents writeonly "
    }
};

constexpr LexerSettings::StyleMapping  stylesPascal[] =
{
    {SCE_PAS_DEFAULT,               sNormal},
    {SCE_PAS_IDENTIFIER,            sNormal},
    {SCE_PAS_COMMENT,               sComment},
    {SCE_PAS_COMMENT2,              sComment},
    {SCE_PAS_COMMENTLINE,           sComment},
    {SCE_PAS_PREPROCESSOR,          sPreprocessor},
    {SCE_PAS_PREPROCESSOR2,         sPreprocessor},
    {SCE_PAS_NUMBER,                sNumberLiteral},
    {SCE_PAS_HEXNUMBER,             sNumberLiteral},
    {SCE_PAS_WORD,                  sKeyword1},
    {SCE_PAS_STRING,                sStringLiteral},
    {SCE_PAS_STRINGEOL,             sStringLiteral},
    {SCE_PAS_CHARACTER,             sCharLiteral},
    {SCE_PAS_OPERATOR,              sOperator},
    {SCE_PAS_ASM,                   sMisc}
};

constexpr LexerSettings::KeywordMapping keywordsPascal[] =
{
    {0,
// Pascal
"and array asm begin break case const constructor continue destructor div do "
"downto else end false file for function goto if implementation in inline interface "
"label mod nil not object of on operator or packed procedure program record "
"repeat set shl shr string then to true type unit until uses var while with xor "

"as class constref dispose except exit exports finalization finally inherited "
"initialization is library new on out property raise self threadvar try far near "

"absolute abstract alias assembler cdecl Cppdecl default export external forward "
"generic index local name nostackframe oldfpccall override pascal private protected "
"public published read register reintroduce safecall softfloat specialize stdcall "
"virtual write "

// Delphi
"resourcestring dispinterface strict nodefault stored automated final readonly "
"unsafe reference varargs contains helper overload implements winapi delayed package "
"requires deprecated resident writeonly dispid platform dynamic sealed experimental "
"message static "
    }
};

constexpr LexerSettings::StyleMapping  stylesTeX[] =
{
    {SCE_L_DEFAULT,                 sNormal},
    {SCE_L_COMMAND,                 sKeyword1},
    {SCE_L_TAG,                     sKeyword2},
    {SCE_L_MATH,                    sCharLiteral},
    {SCE_L_COMMENT,                 sComment},
    {SCE_L_TAG2,                    sKeyword2},
    {SCE_L_MATH2,                   sCharLiteral},
    {SCE_L_COMMENT2,                sComment},
    {SCE_L_VERBATIM,                sStringLiteral},
    {SCE_L_SHORTCMD,                sKeyword1},
    {SCE_L_SPECIAL,                 sCtrlChar},
    {SCE_L_CMDOPT,                  sOperator},
    {SCE_L_ERROR,                   sError},
};

constexpr LexerSettings::StyleMapping  stylesSQL[] =
{
    {SCE_SQL_DEFAULT,                sNormal},
    {SCE_SQL_COMMENT,                sComment},
    {SCE_SQL_COMMENTLINE,            sComment},
    {SCE_SQL_COMMENTDOC,             sComment},
    {SCE_SQL_NUMBER,                 sNumberLiteral},
    {SCE_SQL_WORD,                   sKeyword1},
    {SCE_SQL_STRING,                 sStringLiteral},
    {SCE_SQL_CHARACTER,              sCharLiteral},
    {SCE_SQL_SQLPLUS,                sMisc},
    {SCE_SQL_SQLPLUS_PROMPT,         sMisc},
    {SCE_SQL_OPERATOR,               sOperator},
    {SCE_SQL_IDENTIFIER,             sNormal},
    {SCE_SQL_SQLPLUS_COMMENT,        sComment},
    {SCE_SQL_COMMENTLINEDOC,         sComment},
    {SCE_SQL_WORD2,                  sKeyword2},
    {SCE_SQL_COMMENTDOCKEYWORD,      sKeyword2},
    {SCE_SQL_COMMENTDOCKEYWORDERROR, sError},
    {SCE_SQL_USER1,                  sKeyword2},
    {SCE_SQL_USER2,                  sKeyword2},
    {SCE_SQL_USER3,                  sKeyword2},
    {SCE_SQL_USER4,                  sKeyword2},
    {SCE_SQL_QUOTEDIDENTIFIER,       sNormal},
    {SCE_SQL_QOPERATOR,              sOperator},
};

constexpr LexerSettings::KeywordMapping keywordsSQL[] =
{
    {0,
"a abort abs absolute access action ada add admin after aggregate alias all allocate also alter always analyse "
"analyze and any are array as asc asensitive assertion assignment asymmetric at atomic attribute attributes audit "
"authorization auto_increment avg avg_row_length backup backward before begin bernoulli between bigint binary bit "
"bit_length bitvar blob bool boolean both breadth break browse bulk by c cache call called cardinality cascade "
"cascaded case cast catalog catalog_name ceil ceiling chain change char char_length character character_length "
"character_set_catalog character_set_name character_set_schema characteristics characters check checked checkpoint "
"checksum class class_origin clob close cluster clustered coalesce cobol collate collation collation_catalog "
"collation_name collation_schema collect column column_name columns command_function command_function_code comment "
"commit committed completion compress compute condition condition_number connect connection connection_name constraint "
"constraint_catalog constraint_name constraint_schema constraints constructor contains containstable continue "
"conversion convert copy corr corresponding count covar_pop covar_samp create createdb createrole createuser cross "
"csv cube cume_dist current current_date current_default_transform_group current_path current_role current_time "
"current_timestamp current_transform_group_for_type current_user cursor cursor_name cycle data database databases "
"date datetime datetime_interval_code datetime_interval_precision day day_hour day_microsecond day_minute day_second "
"dayofmonth dayofweek dayofyear dbcc deallocate dec decimal declare default defaults deferrable deferred defined "
"definer degree delay_key_write delayed delete delimiter delimiters dense_rank deny depth deref derived desc describe "
"descriptor destroy destructor deterministic diagnostics dictionary disable disconnect disk dispatch distinct "
"distinctrow distributed div do domain double drop dual dummy dump dynamic dynamic_function dynamic_function_code "
"each element else elseif enable enclosed encoding encrypted end end-exec enum equals errlvl escape escaped every "
"except exception exclude excluding exclusive exec execute existing exists exit exp explain external extract false "
"fetch fields file fillfactor filter final first float float4 float8 floor flush following for force foreign fortran "
"forward found free freetext freetexttable freeze from full fulltext function fusion g general generated get global "
"go goto grant granted grants greatest group grouping handler having header heap hierarchy high_priority hold holdlock "
"host hosts hour hour_microsecond hour_minute hour_second identified identity identity_insert identitycol if ignore "
"ilike immediate immutable implementation implicit in include including increment index indicator infile infix inherit "
"inherits initial initialize initially inner inout input insensitive insert insert_id instance instantiable instead "
"int int1 int2 int3 int4 int8 integer intersect intersection interval into invoker is isam isnull isolation iterate "
"join k key key_member key_type keys kill lancompiler language large last last_insert_id lateral lead leading least "
"leave left length less level like limit lineno lines listen ln load local localtime localtimestamp location locator "
"lock login logs long longblob longtext loop low_priority lower m map match matched max max_rows maxextents maxvalue "
"mediumblob mediumint mediumtext member merge message_length message_octet_length message_text method middleint "
"min min_rows minus minute minute_microsecond minute_second minvalue mlslabel mod mode modifies modify module month "
"monthname more move multiset mumps myisam name names national natural nchar nclob nesting new next no "
"no_write_to_binlog noaudit nocheck nocompress nocreatedb nocreaterole nocreateuser noinherit nologin nonclustered "
"none normalize normalized nosuperuser not nothing notify notnull nowait null nullable nullif nulls number numeric "
"object octet_length octets of off offline offset offsets oids old on online only open opendatasource openquery "
"openrowset openxml operation operator optimize option optionally options or order ordering ordinality others out "
"outer outfile output over overlaps overlay overriding owner pack_keys pad parameter parameter_mode parameter_name "
"parameter_ordinal_position parameter_specific_catalog parameter_specific_name parameter_specific_schema parameters "
"partial partition pascal password path pctfree percent percent_rank percentile_cont percentile_disc placing plan pli "
"position postfix power preceding precision prefix preorder prepare prepared preserve primary print prior privileges "
"proc procedural procedure process processlist public purge quote raid0 raiserror range rank raw read reads readtext "
"real recheck reconfigure recursive ref references referencing regexp regr_avgx regr_avgy regr_count regr_intercept "
"regr_r2 regr_slope regr_sxx regr_sxy regr_syy reindex relative release reload rename repeat repeatable replace "
"replication require reset resignal resource restart restore restrict result return returned_cardinality returned_length "
"returned_octet_length returned_sqlstate returns revoke right rlike role rollback rollup routine routine_catalog "
"routine_name routine_schema row row_count row_number rowcount rowguidcol rowid rownum rows rule save savepoint scale "
"schema schema_name schemas scope scope_catalog scope_name scope_schema scroll search second second_microsecond section "
"security select self sensitive separator sequence serializable server_name session session_user set setof sets setuser "
"share show shutdown signal similar simple size smallint some soname source space spatial specific specific_name "
"specifictype sql sql_big_result sql_big_selects sql_big_tables sql_calc_found_rows sql_log_off sql_log_update "
"sql_low_priority_updates sql_select_limit sql_small_result sql_warnings sqlca sqlcode sqlerror sqlexception sqlstate "
"sqlwarning sqrt ssl stable start starting state statement static statistics status stddev_pop stddev_samp stdin stdout "
"storage straight_join strict string structure style subclass_origin sublist submultiset substring successful sum "
"superuser symmetric synonym sysdate sysid system system_user table table_name tables tablesample tablespace temp "
"template temporary terminate terminated text textsize than then ties time timestamp timezone_hour timezone_minute "
"tinyblob tinyint tinytext to toast top top_level_count trailing tran transaction transaction_active "
"transactions_committed transactions_rolled_back transform transforms translate translation treat trigger "
"trigger_catalog trigger_name trigger_schema trim true truncate trusted tsequal type uescape uid unbounded uncommitted "
"under undo unencrypted union unique unknown unlisten unlock unnamed unnest unsigned until update updatetext upper "
"usage use user user_defined_type_catalog user_defined_type_code user_defined_type_name user_defined_type_schema "
"using utc_date utc_time utc_timestamp vacuum valid validate validator value values var_pop var_samp varbinary varchar "
"varchar2 varcharacter variable variables varying verbose view volatile waitfor when whenever where while width_bucket "
"window with within without work write writetext x509 xor year year_month zerofill zone "
    }
};

constexpr LexerSettings::PropertyMapping propertiesSQL[] =
{
    {"lexer.sql.backticks.identifier", "1"},
    {"lexer.sql.numbersign.comment", "1"},
};


constexpr LexerSettings::KeywordMapping keywordsGo[] =
{
    {0,
"break default func interface select "
"case defer go map struct "
"chan else goto package switch "
"const fallthrough if range type "
"continue for import return var "
    },
    {1,
"bool uint8 uint16 uint32 uint64 int8 int16 int32 int64 float32 float64 "
"complex64 complex128 byte rune uint int uintptr "
"string struct "
    },
    {3,
"std"
    },
};


constexpr LexerSettings::StyleMapping stylesMarkdown[] =
{
    {SCE_MARKDOWN_DEFAULT,          sNormal},
    {SCE_MARKDOWN_LINE_BEGIN,       sNormal},
    {SCE_MARKDOWN_STRONG1,          sMisc},
    {SCE_MARKDOWN_STRONG2,          sMisc},
    {SCE_MARKDOWN_EM1,              sKeyword2},
    {SCE_MARKDOWN_EM2,              sKeyword2},
    {SCE_MARKDOWN_HEADER1,          sKeyword1,      slBold},
    {SCE_MARKDOWN_HEADER2,          sKeyword1,      slBold},
    {SCE_MARKDOWN_HEADER3,          sKeyword1,      slBold},
    {SCE_MARKDOWN_HEADER4,          sKeyword1,      slBold},
    {SCE_MARKDOWN_HEADER5,          sKeyword1,      slBold},
    {SCE_MARKDOWN_HEADER6,          sKeyword1,      slBold},
    {SCE_MARKDOWN_PRECHAR,          sOperator},
    {SCE_MARKDOWN_ULIST_ITEM,       sKeyword2},
    {SCE_MARKDOWN_OLIST_ITEM,       sKeyword2},
    {SCE_MARKDOWN_BLOCKQUOTE,       sComment},
    {SCE_MARKDOWN_STRIKEOUT,        sComment},
    {SCE_MARKDOWN_HRULE,            sOperator},
    {SCE_MARKDOWN_LINK,             sStringLiteral},
    {SCE_MARKDOWN_CODE,             sNumberLiteral},
    {SCE_MARKDOWN_CODE2,            sNumberLiteral},
    {SCE_MARKDOWN_CODEBK,           sNumberLiteral},
};

constexpr LexerSettings::PropertyMapping propertiesMarkdown[] =
{
    // Style the whole ATX header line (the '#' marks *and* the text), not just
    // the '#' marks. Needed for the header styling (e.g. bold) to cover the text.
    {"lexer.markdown.header.eolfill", "1"},
};


// Elixir and HEEx are lexed by turbo's tree-sitter lexer
// (source/turbo-core/lexers/TSLexer.cxx). Lexilla ships neither, and HEEx cannot
// sensibly be hand-lexed: it is a three-level embedding, and one buffer holds
// both languages at once (an .ex file embeds HEEx through ~H, a .heex file
// embeds Elixir through {...} and <%= %>). Hence a single style table, shared by
// both entries below -- the grammars' capture names are mapped onto these ids
// per grammar inside the lexer, which is where "attribute" is resolved to a
// module attribute for Elixir and to an HTML attribute for HEEx.
//
// No keyword lists: tree-sitter gets keywords from the grammar, not SCI_SETKEYWORDS.
constexpr LexerSettings::StyleMapping stylesTS[] =
{
    {SCE_TS_DEFAULT,                sNormal},
    {SCE_TS_COMMENT,                sComment},
    {SCE_TS_COMMENT_DOC,            sComment},
    {SCE_TS_KEYWORD,                sKeyword1},
    {SCE_TS_OPERATOR,               sOperator},
    {SCE_TS_PUNCTUATION,            sOperator},
    {SCE_TS_STRING,                 sStringLiteral},
    {SCE_TS_STRING_ESCAPE,          sEscapeSequence},
    {SCE_TS_STRING_SPECIAL,         sStringLiteral},
    {SCE_TS_SYMBOL,                 sConstant},
    {SCE_TS_REGEX,                  sMisc},
    {SCE_TS_NUMBER,                 sNumberLiteral},
    {SCE_TS_CONSTANT,               sConstant},
    {SCE_TS_VARIABLE,               sIdentifier},
    {SCE_TS_FUNCTION,               sFunctionName},
    {SCE_TS_PROPERTY,               sKeyword2},
    {SCE_TS_MODULE,                 sTypeName},
    {SCE_TS_ATTRIBUTE,              sPreprocessor},
    {SCE_TS_TAG,                    sTag},
    {SCE_TS_TAG_ATTR,               sAttribute},
    {SCE_TS_EMBEDDED,               sEscapeSequence},
    {SCE_TS_ERROR,                  sNormal},
};

// CSS and XML were declared, had extensions mapped, and had no lexer -- so they
// rendered as plain text, even though Lexilla's lexers for both are already
// compiled into the binary (CMake globs deps/lexilla/lexers/*.cxx wholesale).
// Wiring them up costs two table rows each and no binary growth at all.
constexpr LexerSettings::StyleMapping stylesCSS[] =
{
    {SCE_CSS_DEFAULT,               sNormal},
    {SCE_CSS_TAG,                   sTag},
    {SCE_CSS_CLASS,                 sTypeName},
    {SCE_CSS_PSEUDOCLASS,           sTypeName},
    {SCE_CSS_OPERATOR,              sOperator},
    {SCE_CSS_IDENTIFIER,            sAttribute},   // property names
    // "Unknown" here only means "absent from the wordlist above", and that list
    // can never be complete: custom properties (--gap), vendor prefixes
    // (-webkit-*) and anything CSS added recently all land here. Styling them as
    // errors turns a modern stylesheet red, so they get the same colour as the
    // properties and pseudo-classes they are.
    {SCE_CSS_UNKNOWN_IDENTIFIER,    sAttribute},
    {SCE_CSS_UNKNOWN_PSEUDOCLASS,   sTypeName},
    {SCE_CSS_VALUE,                 sIdentifier},
    {SCE_CSS_COMMENT,               sComment},
    {SCE_CSS_ID,                    sTypeName},
    {SCE_CSS_IMPORTANT,             sKeyword1},
    {SCE_CSS_DIRECTIVE,             sPreprocessor},
    {SCE_CSS_DOUBLESTRING,          sStringLiteral},
    {SCE_CSS_SINGLESTRING,          sStringLiteral},
    {SCE_CSS_IDENTIFIER2,           sAttribute},
    {SCE_CSS_ATTRIBUTE,             sAttribute},
    {SCE_CSS_IDENTIFIER3,           sAttribute},
    {SCE_CSS_PSEUDOELEMENT,         sTypeName},
    {SCE_CSS_EXTENDED_IDENTIFIER,   sAttribute},
    {SCE_CSS_EXTENDED_PSEUDOCLASS,  sTypeName},
    {SCE_CSS_EXTENDED_PSEUDOELEMENT, sTypeName},
    {SCE_CSS_GROUP_RULE,            sKeyword1},   // @media, @supports
    {SCE_CSS_VARIABLE,              sConstant},   // --custom-property
};

constexpr LexerSettings::KeywordMapping keywordsCSS[] =
{
    {0, // CSS1 properties
"color background-color background-image background-repeat background-attachment "
"background-position background font-family font-style font-variant font-weight "
"font-size font word-spacing letter-spacing text-decoration vertical-align "
"text-transform text-align text-indent line-height margin-top margin-right "
"margin-bottom margin-left margin padding-top padding-right padding-bottom "
"padding-left padding border-top-width border-right-width border-bottom-width "
"border-left-width border-width border-top border-right border-bottom border-left "
"border border-color border-style width height float clear display white-space "
"list-style-type list-style-image list-style-position list-style"
    },
    {1, // pseudo-classes
"first-letter first-line link active visited hover focus lang left right first "
"first-child before after"
    },
    {2, // CSS2+ properties (a working subset; the lexer only uses these to
        // distinguish known from unknown, and unknown ones style as sError)
"azimuth border-collapse border-spacing bottom caption-side clip content "
"counter-increment counter-reset cue cue-after cue-before cursor direction "
"elevation empty-cells left max-height max-width min-height min-width opacity "
"orphans outline outline-color outline-style outline-width overflow padding "
"page-break-after page-break-before page-break-inside pause pause-after "
"pause-before pitch pitch-range play-during position quotes richness right "
"speak speak-header speak-numeral speak-punctuation speech-rate stress "
"table-layout text-shadow top unicode-bidi visibility voice-family volume "
"widows z-index "
// Modern layout and custom properties, so a stylesheet written this decade does
// not light up as a wall of sError.
"align-content align-items align-self appearance backdrop-filter border-radius "
"box-shadow box-sizing filter flex flex-basis flex-direction flex-flow flex-grow "
"flex-shrink flex-wrap gap grid grid-area grid-auto-columns grid-auto-flow "
"grid-auto-rows grid-column grid-column-end grid-column-start grid-gap grid-row "
"grid-row-end grid-row-start grid-template grid-template-areas "
"grid-template-columns grid-template-rows inset justify-content justify-items "
"justify-self object-fit object-position order overflow-x overflow-y place-items "
"pointer-events resize row-gap column-gap transform transform-origin transition "
"transition-delay transition-duration transition-property transition-timing-function "
"user-select will-change writing-mode aspect-ratio"
    },
};

constexpr struct { const Language *language; LexerSettings lexer; } builtInLexers[] =
{
    {&Language::CPP, {SCLEX_CPP, stylesC, keywordsC, propertiesC}},
    {&Language::Lua, {SCLEX_LUA, stylesLua, keywordsLua, nullptr}},
    {&Language::Makefile, {SCLEX_MAKEFILE, stylesMake, nullptr, nullptr}},
    {&Language::Asm, {SCLEX_ASM, stylesAsm, nullptr, nullptr}},
    // JavaScript, TypeScript and PHP go to tree-sitter when it is built, and fall
    // back to Lexilla otherwise. The fallbacks are not vestigial: LexCPP handles
    // plain modern JS well (template literals with ${} interpolation, regex
    // literals), and LexHTML's PHP is flat but not broken. What they cannot do is
    // JSX/TSX -- LexCPP has no notion of a tag at all -- and they cannot tell a
    // function from a class from a property, which is what the parse tree buys.
#ifdef TURBO_ENABLE_TREESITTER
    {&Language::JavaScript, {SCLEX_TURBO_JS, stylesTS, nullptr, nullptr}},
    {&Language::TypeScript, {SCLEX_TURBO_TSX, stylesTS, nullptr, nullptr}},
#else
    {&Language::JavaScript, {SCLEX_CPP, stylesJavaScript, keywordsJsTs, propertiesJavaScript}},
    {&Language::TypeScript, {SCLEX_CPP, stylesJavaScript, keywordsJsTs, propertiesJavaScript}},
#endif
    {&Language::CSS, {SCLEX_CSS, stylesCSS, keywordsCSS, nullptr}},
    // XML is LexHTML's other entry point (lmXML, "xml"), so it reuses HTML's
    // tables wholesale.
    {&Language::XML, {SCLEX_XML, stylesHTML, keywordsHTML, propertiesHTML}},
    {&Language::Rust, {SCLEX_RUST, stylesRust, keywordsRust, nullptr}},
    {&Language::Python, {SCLEX_PYTHON, stylesPython, keywordsPython, propertiesPython}},
    {&Language::Bash, {SCLEX_BASH, stylesBash, keywordsBash, nullptr}},
    {&Language::Ruby, {SCLEX_RUBY, stylesRuby, keywordsRuby, nullptr}},
    {&Language::JSON, {SCLEX_JSON, stylesJSON, keywordsJSON, propertiesJSON}},
    {&Language::YAML, {SCLEX_YAML, stylesYAML, keywordsYAML, nullptr}},
    {&Language::HTML, {SCLEX_HTML, stylesHTML, keywordsHTML, propertiesHTML}},
    {&Language::Properties, {SCLEX_PROPERTIES, stylesProperties, nullptr, nullptr}},
    {&Language::CSharp, {SCLEX_CPP, stylesC, keywordsCSharp, propertiesC}},
    {&Language::VB, {SCLEX_VB, stylesBasic, keywordsVbNet, nullptr}},
    {&Language::Basic, {SCLEX_VB, stylesBasic, keywordsBasic, nullptr}},
    {&Language::Pascal, {SCLEX_PASCAL, stylesPascal, keywordsPascal, nullptr}},
    {&Language::LaTex, {SCLEX_LATEX, stylesTeX, nullptr, nullptr}},
    {&Language::SQL, {SCLEX_SQL, stylesSQL, keywordsSQL, propertiesSQL}},
    {&Language::Go, {SCLEX_CPP, stylesC, keywordsGo, propertiesC}},
#ifdef TURBO_ENABLE_TREESITTER
    {&Language::PHP, {SCLEX_TURBO_PHP, stylesTS, nullptr, nullptr}},
#else
    {&Language::PHP, {SCLEX_HTML, stylesHTML, keywordsHTML, propertiesHTML}},
#endif
    {&Language::Markdown, {SCLEX_MARKDOWN, stylesMarkdown, nullptr, propertiesMarkdown}},
    {&Language::Elixir, {SCLEX_TURBO_ELIXIR, stylesTS, nullptr, nullptr}},
    {&Language::HEEx, {SCLEX_TURBO_HEEX, stylesTS, nullptr, nullptr}},
    // Built only when TURBO_TS_BLADE is on. If it is off, createLexer("blade")
    // falls through to Lexilla, finds nothing, and the file renders as plain text
    // rather than failing.
    {&Language::Blade, {SCLEX_TURBO_BLADE, stylesTS, nullptr, nullptr}},
};

TColorAttr coalesce(TColorAttr from, TColorAttr into)
{
    auto f_fg = ::getFore(from), f_bg = ::getBack(from),
         i_fg = ::getFore(into), i_bg = ::getBack(into);
    return {
        f_fg.isDefault() ? i_fg : f_fg,
        f_bg.isDefault() ? i_bg : f_bg,
        ::getStyle(from),
    };
}

const LexerSettings *findBuiltInLexer(const Language *language)
{
    for (const auto &l : builtInLexers)
        if (l.language == language)
            return &l.lexer;
    return nullptr;
}

} // namespace turbo
