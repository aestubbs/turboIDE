#ifndef TURBO_LEXELIXIR_H
#define TURBO_LEXELIXIR_H

namespace Scintilla { class ILexer5; }

namespace turbo {

// Lexer ids for turbo-owned lexers. Lexilla's own ids run up to SCLEX_NIX
// (140); starting well above that means a lexer added to the submodule later
// can never collide with one of ours.
constexpr int SCLEX_TURBO_ELIXIR = 1001;

// Style ids produced by the Elixir lexer. Kept small and contiguous:
// LexerSettings::StyleMapping::id is a uchar, so these must stay under 256.
enum {
    SCE_ELIXIR_DEFAULT = 0,
    SCE_ELIXIR_COMMENT = 1,
    SCE_ELIXIR_NUMBER = 2,
    SCE_ELIXIR_STRING = 3,        // "..." and the """ heredoc
    SCE_ELIXIR_CHARLIST = 4,      // '...' and the ''' heredoc
    SCE_ELIXIR_SIGIL = 5,         // the body of ~s(...), ~r/.../, ~H"""..."""
    SCE_ELIXIR_SIGIL_MARK = 6,    // the ~x and its delimiters
    SCE_ELIXIR_KEYWORD = 7,       // def, do, end, fn, case, ...
    SCE_ELIXIR_KEYWORD2 = 8,      // guards and Kernel builtins
    SCE_ELIXIR_ATOM = 9,          // :foo, and the key in  foo: bar
    SCE_ELIXIR_MODULE = 10,       // aliases: MyApp.User, IO, Enum
    SCE_ELIXIR_ATTRIBUTE = 11,    // @moduledoc, @spec, @my_attr
    SCE_ELIXIR_OPERATOR = 12,
    SCE_ELIXIR_IDENTIFIER = 13,
    SCE_ELIXIR_FUNCTION = 14,     // the name bound by def/defp/defmacro/...
    SCE_ELIXIR_INTERPOLATION = 15,// the #{ and } delimiters
    SCE_ELIXIR_CHARACTER = 16,    // ?a, ?\n
    SCE_ELIXIR_ERROR = 17,
};

// Create a lexer by name, checking turbo's own lexers before falling back to
// Lexilla's catalogue. Returns nullptr when the name is unknown to both.
//
// This exists so a turbo-owned lexer can be installed without patching the
// Lexilla submodule (whose catalogue in src/Lexilla.cxx is a generated list).
Scintilla::ILexer5 *createLexer(const char *name);

} // namespace turbo

#endif
