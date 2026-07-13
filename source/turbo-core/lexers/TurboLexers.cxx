// The lexer catalogue for turbo-owned lexers.
//
// Lexilla resolves a lexer name against a generated catalogue in
// deps/lexilla/src/Lexilla.cxx. Adding a lexer there would mean carrying a
// patch against the submodule, so instead turbo keeps its own tiny catalogue in
// front of Lexilla's and installs the result through SCI_SETILEXER exactly as
// before -- applyTheming (editstates.cc) is typed on ILexer5*, so it neither
// knows nor cares which side of this function a lexer came from. That is also
// what lets a tree-sitter-backed lexer sit alongside the hand-written Lexilla
// ones without either knowing about the other.

#include <cstring>

#include "ILexer.h"
#include "Scintilla.h"
#include "LexerModule.h"

#include <Lexilla.h>

#include <turbo/lexts.h>

#ifdef TURBO_ENABLE_TREESITTER
namespace Lexilla {
extern const LexerModule lmTurboTsElixir;
extern const LexerModule lmTurboTsHeex;
#ifdef TURBO_TS_BLADE
extern const LexerModule lmTurboTsBlade;
#endif
}
#endif

namespace turbo {

Scintilla::ILexer5 *createLexer(const char *name)
{
    if (!name)
        return nullptr;
#ifdef TURBO_ENABLE_TREESITTER
    if (strcmp(name, "elixir") == 0)
        return Lexilla::lmTurboTsElixir.Create();
    if (strcmp(name, "heex") == 0)
        return Lexilla::lmTurboTsHeex.Create();
#ifdef TURBO_TS_BLADE
    if (strcmp(name, "blade") == 0)
        return Lexilla::lmTurboTsBlade.Create();
#endif
#endif
    return ::CreateLexer(name);
}

} // namespace turbo
