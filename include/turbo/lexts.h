#ifndef TURBO_LEXTS_H
#define TURBO_LEXTS_H

namespace Scintilla { class ILexer5; }

namespace turbo {

// Lexer ids for turbo-owned lexers. Lexilla's own ids run up to SCLEX_NIX
// (140); starting well above that means a lexer added to the submodule later
// can never collide with one of ours.
constexpr int SCLEX_TURBO_ELIXIR = 1001;
constexpr int SCLEX_TURBO_HEEX = 1002;
constexpr int SCLEX_TURBO_BLADE = 1003;
constexpr int SCLEX_TURBO_JS = 1004;
constexpr int SCLEX_TURBO_TSX = 1005;    // serves .ts as well as .tsx
constexpr int SCLEX_TURBO_PHP = 1006;

// Style ids emitted by the tree-sitter lexer. One set is shared by Elixir and
// HEEx, because a single buffer contains both: an .ex file embeds HEEx through
// the ~H sigil, and a .heex file embeds Elixir through {...} and <%= %>.
//
// The grammars' capture names are mapped onto these *per grammar*, which is not
// a nicety -- "attribute" means a module attribute (@moduledoc) in Elixir but an
// HTML attribute name in HEEx, and the two want different colours. See
// captureStyles() in TSLexer.cxx.
//
// LexerSettings::StyleMapping::id is a uchar, so these must stay under 256.
enum {
    SCE_TS_DEFAULT = 0,
    SCE_TS_COMMENT = 1,
    SCE_TS_COMMENT_DOC = 2,       // @moduledoc / @doc bodies
    SCE_TS_KEYWORD = 3,
    SCE_TS_OPERATOR = 4,
    SCE_TS_PUNCTUATION = 5,
    SCE_TS_STRING = 6,
    SCE_TS_STRING_ESCAPE = 7,
    SCE_TS_STRING_SPECIAL = 8,    // sigil bodies
    SCE_TS_SYMBOL = 9,            // atoms: :ok, and the key in  foo: bar
    SCE_TS_REGEX = 10,            // ~r// bodies
    SCE_TS_NUMBER = 11,
    SCE_TS_CONSTANT = 12,         // nil, true, false; HEEx doctype
    SCE_TS_VARIABLE = 13,
    SCE_TS_FUNCTION = 14,
    SCE_TS_PROPERTY = 15,         // map/struct keys
    SCE_TS_MODULE = 16,           // aliases: MyApp.User, IO, Enum
    SCE_TS_ATTRIBUTE = 17,        // Elixir module attributes: @spec, @moduledoc
    SCE_TS_TAG = 18,              // HEEx: <div>, <:slot>
    SCE_TS_TAG_ATTR = 19,         // HEEx: class=, phx-click=
    SCE_TS_EMBEDDED = 20,         // the #{ } and { } / <%= %> delimiters
    SCE_TS_ERROR = 21,
};

// Create a lexer by name, checking turbo's own lexers before falling back to
// Lexilla's catalogue. Returns nullptr when the name is unknown to both.
//
// This exists so a turbo-owned lexer can be installed without patching the
// Lexilla submodule (whose catalogue in src/Lexilla.cxx is a generated list).
Scintilla::ILexer5 *createLexer(const char *name);

} // namespace turbo

#endif
