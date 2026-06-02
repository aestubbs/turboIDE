#define Uses_TProgram
#define Uses_TDrawSurface
#include <tvision/tv.h>

#include <turbo/scintilla.h>
#include <turbo/scintilla/internals.h>
#include "surface.h"

namespace Scintilla::Internal {

Window::~Window() noexcept
{
}

void Window::Destroy() noexcept
{
}

PRectangle Window::GetPosition() const
{
    return PRectangle();
}

void Window::SetPosition(PRectangle)
{
}

void Window::SetPositionRelative(PRectangle, const Window *)
{
}

PRectangle Window::GetClientPosition() const
{
    auto *p = (turbo::TScintillaParent *) wid;
    if (p)
    {
        auto size = p->getEditorSize();
        return PRectangle::FromInts(0, 0, size.x, size.y);
    }
    return PRectangle();
}

void Window::Show(bool)
{
}

void Window::InvalidateAll()
{
    auto *p = (turbo::TScintillaParent *) wid;
    if (p)
        p->invalidate({{0, 0}, p->getEditorSize()});
}

void Window::InvalidateRectangle(PRectangle rc)
{
    auto *p = (turbo::TScintillaParent *) wid;
    if (p)
        p->invalidate(TPRect(rc));
}

void Window::SetCursor(Cursor)
{
}

PRectangle Window::GetMonitorRect(Point)
{
    if (TProgram::application)
    {
        auto size = TProgram::application->size;
        return PRectangle::FromInts(0, 0, size.x, size.y);
    }
    return PRectangle();
}

} // namespace Scintilla::Internal
