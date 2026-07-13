// The lexer catalogue for turbo-owned lexers.
//
// Lexilla resolves a lexer name against a generated catalogue in
// deps/lexilla/src/Lexilla.cxx. Adding a lexer there would mean carrying a
// patch against the submodule, so instead turbo keeps its own tiny catalogue in
// front of Lexilla's and installs the result through SCI_SETILEXER exactly as
// before -- applyTheming (editstates.cc) is typed on ILexer5*, so it neither
// knows nor cares which side of this function a lexer came from.

#include <cstring>

#include "ILexer.h"
#include "Scintilla.h"
#include "LexerModule.h"

#include <Lexilla.h>

#include <turbo/lexelixir.h>

namespace Lexilla {
extern const LexerModule lmTurboElixir;
}

namespace turbo {

Scintilla::ILexer5 *createLexer(const char *name)
{
    if (!name)
        return nullptr;
    if (strcmp(name, "elixir") == 0)
        return Lexilla::lmTurboElixir.Create();
    return ::CreateLexer(name);
}

} // namespace turbo
