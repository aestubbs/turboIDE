#define Uses_TEvent
#include <tvision/tv.h>

#include <turbo/editor.h>

namespace turbo {

LeftMarginView::LeftMarginView(int aDistance) noexcept :
    TSurfaceView(TRect(0, 0, 0, 0)),
    distanceFromView(aDistance)
{
    growMode = gfGrowHiY | gfFixed;
    // Receive clicks (e.g. on the fold column) without stealing focus from the
    // editor: ofSelectable is deliberately not set.
    eventMask |= evMouseDown;
}

void LeftMarginView::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown && editor)
    {
        TPoint where = makeLocal(ev.mouse.where);
        if (editor->marginClick(where.x, where.y))
        {
            clearEvent(ev);
            return;
        }
    }
    TSurfaceView::handleEvent(ev);
}

} // namespace turbo
