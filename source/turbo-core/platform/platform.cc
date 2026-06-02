#include <turbo/scintilla.h>
#include <turbo/scintilla/internals.h>

namespace Scintilla::Internal {

namespace Platform {

ColourRGBA Chrome()
{
    return black;
}

ColourRGBA ChromeHighlight()
{
    return black;
}

const char *DefaultFont()
{
    return "";
}

int DefaultFontSize()
{
    return 1;
}

unsigned int DoubleClickTime()
{
    return 500;
}

// Debugging hooks (declared in Debugging.h, only used by PLATFORM_ASSERT in
// non-NDEBUG builds). turbo has no console to print to, so these are inert.
void DebugDisplay(const char *) noexcept
{
}

void DebugPrintf(const char *, ...) noexcept
{
}

bool ShowAssertionPopUps(bool) noexcept
{
    return false;
}

void Assert(const char *, const char *, int) noexcept
{
}

} // namespace Platform

} // namespace Scintilla::Internal
