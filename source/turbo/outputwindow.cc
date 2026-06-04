#define Uses_TWindow
#define Uses_TListViewer
#define Uses_TScrollBar
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TProgram
#include <tvision/tv.h>

#include "outputwindow.h"
#include "cmds.h"

#include <turbo/basicwindow.h> // shared window-chrome scheme

// ---------------------------------------------------------------------------
// OutputView

OutputView::OutputView(const TRect &bounds, TScrollBar *vScrollBar) noexcept :
    TListViewer(bounds, 1, nullptr, vScrollBar)
{
    setRange(0);
}

void OutputView::addLine(OutputLine ln) noexcept
{
    lines.push_back(std::move(ln));
    // Bound memory: keep the most recent ~5000 lines.
    const size_t kMax = 5000;
    if (lines.size() > kMax)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - kMax));
    setRange((short) lines.size());
    if (followTail && range > 0)
        focusItemNum(range - 1); // scroll so the newest line is visible
    drawView();
}

void OutputView::clear() noexcept
{
    lines.clear();
    setRange(0);
    focused = 0;
    topItem = 0;
    followTail = true;
    drawView();
}

void OutputView::draw()
{
    TColorAttr cNormal {TColorRGB(0xC4C8D4), TColorRGB(0x10141E)};
    TColorAttr cError  {TColorRGB(0xFF6B6B), TColorRGB(0x10141E)};
    TColorAttr cWarn   {TColorRGB(0xE6C07B), TColorRGB(0x10141E)};
    TColorAttr cInfo   {TColorRGB(0x6FA8FF), TColorRGB(0x10141E)};

    int n = (int) lines.size();
    for (int y = 0; y < size.y; ++y)
    {
        int idx = topItem + y;
        TDrawBuffer b;
        b.moveChar(0, ' ', cNormal, size.x);
        if (idx >= 0 && idx < n)
        {
            const OutputLine &ln = lines[idx];
            TColorAttr c = cNormal;
            switch (ln.kind)
            {
                case okError:   c = cError; break;
                case okWarning: c = cWarn;  break;
                case okInfo:    c = cInfo;  break;
                default:        c = cNormal; break;
            }
            b.moveChar(0, ' ', c, size.x);
            std::string t = ln.text;
            if ((int) t.size() > size.x)
                t = t.substr(0, size.x);
            b.moveStr(0, t.c_str(), c);
        }
        writeLine(0, y, size.x, 1, b);
    }
}

void OutputView::handleEvent(TEvent &ev)
{
    TListViewer::handleEvent(ev);
    // Re-enable tail-follow only while scrolled to the bottom; pause it when the
    // user scrolls up to read.
    followTail = (range == 0) || (topItem + size.y >= range);
}

// ---------------------------------------------------------------------------
// OutputWindow

OutputWindow::OutputWindow(const TRect &bounds, OutputWindow **aptr) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, "Output", wnNoNumber),
    ptr(aptr)
{
    flags &= ~(wfZoom | wfGrow); // a docked pane: only the close box
    options |= ofFirstClick;
    auto *vsb = standardScrollBar(sbVertical | sbHandleKeyboard);
    TRect inner = getExtent();
    inner.grow(-1, -1);
    view = new OutputView(inner, vsb);
    view->growMode = gfGrowHiX | gfGrowHiY;
    insert(view);
}

TColorAttr OutputWindow::mapColor(uchar index) noexcept
{
    // Resolve chrome through the shared window scheme, like DocumentTreeWindow,
    // so the frame and scrollbar match the editor windows and the file tree.
    if (index > 0 && index - 1 < turbo::WindowPaletteItemCount)
        return turbo::windowSchemeActive[index - 1];
    return errorAttr;
}

void OutputWindow::close()
{
    // Closing the pane is the same as toggling it off.
    message(TProgram::application, evCommand, cmToggleOutput, 0);
}

void OutputWindow::shutDown()
{
    if (ptr)
        *ptr = nullptr;
    view = nullptr;
    TWindow::shutDown();
}
