#include "surface.h"

namespace Scintilla::Internal {

std::shared_ptr<Font> Font::Allocate(const FontParameters &fp)
{
    // The terminal has no real fonts; we only carry the TVision cell style
    // (bold/italic/...) that turbo stuffs into Scintilla's FontWeight field.
    auto f = std::make_shared<FontTV>();
    f->style = (ushort) (int) fp.weight;
    return f;
}

} // namespace Scintilla::Internal
