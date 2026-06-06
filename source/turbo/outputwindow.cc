#define Uses_TWindow
#define Uses_TView
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

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <regex>

namespace {

// The "normal" window background: the unified blue when the window is active, a
// dimmer shade when it is not -- matching the frame and the other windows.
TColorDesired windowBg(TView *owner, bool active) noexcept
{
    if (!owner)
        return TColorRGB(0x10182E);
    return getBack(owner->mapColor((active ? turbo::wndFrameActive
                                           : turbo::wndFramePassive) + 1));
}

// Mix a colour toward white by 'pct' percent.
TColorRGB lighten(TColorDesired c, int pct) noexcept
{
    uint32_t v = (uint32_t) c.asRGB();
    int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    auto m = [&] (int x) { return (uint8_t) (x + (255 - x) * pct / 100); };
    return TColorRGB(m(r), m(g), m(b));
}

} // namespace

// ---------------------------------------------------------------------------
// OutputView

OutputView::OutputView(const TRect &bounds, TScrollBar *vScrollBar) noexcept :
    TListViewer(bounds, 1, nullptr, vScrollBar)
{
    options |= ofFirstClick; // the first click on the pane still selects a line
    setRange(0);
}

void OutputView::addLine(int tab, OutputLine ln) noexcept
{
    if (tab < 0 || tab >= otTabCount)
        return;
    // Expand tabs to 8-column stops: a raw tab would otherwise be drawn through
    // the CP437 codepage as a glyph (e.g. git's tab-indented "modified:" lines).
    if (ln.text.find('\t') != std::string::npos)
    {
        std::string e;
        e.reserve(ln.text.size() + 8);
        for (char c : ln.text)
            if (c == '\t')
                do e.push_back(' '); while (e.size() % 8 != 0);
            else
                e.push_back(c);
        ln.text.swap(e);
    }
    Buffer &buf = buffers[tab];
    buf.lines.push_back(std::move(ln));
    // Bound memory: keep the most recent ~5000 lines per buffer.
    const size_t kMax = 5000;
    if (buf.lines.size() > kMax)
        buf.lines.erase(buf.lines.begin(),
                        buf.lines.begin() + (buf.lines.size() - kMax));
    if (tab == activeTab)
    {
        setRange((short) buf.lines.size());
        if (buf.followTail && range > 0)
            focusItemNum(range - 1); // scroll so the newest line is visible
        drawView();
    }
    // Appending to an inactive tab leaves the visible view untouched; switching
    // to that tab (showTab/setActiveTab) sets the range and follows its tail.
}

void OutputView::clear(int tab) noexcept
{
    if (tab < 0 || tab >= otTabCount)
        return;
    Buffer &buf = buffers[tab];
    buf.lines.clear();
    buf.followTail = true;
    buf.savedTop = 0;
    buf.savedFocused = 0;
    if (tab == activeTab)
    {
        setRange(0);
        focused = 0;
        topItem = 0;
        drawView();
    }
}

void OutputView::setActiveTab(int tab) noexcept
{
    if (tab < 0 || tab >= otTabCount)
        return;
    if (tab != activeTab)
    {
        // Remember where the outgoing tab was scrolled, then switch.
        buffers[activeTab].savedTop = topItem;
        buffers[activeTab].savedFocused = focused;
        activeTab = tab;
    }
    Buffer &buf = buffers[activeTab];
    setRange((short) buf.lines.size());
    if (buf.followTail && range > 0)
        focusItemNum(range - 1);
    else
    {
        int last = range > 0 ? range - 1 : 0;
        topItem = (short) min(max(buf.savedTop, 0), last);
        focused = (short) min(max(buf.savedFocused, 0), last);
    }
    drawView();
    if (tabBar)
        tabBar->drawView();
}

void OutputView::showTab(int tab) noexcept
{
    if (tab < 0 || tab >= otTabCount)
        return;
    // A fresh command's output should be visible, so resume tail-follow.
    buffers[tab].followTail = true;
    setActiveTab(tab);
}

void OutputView::draw()
{
    std::vector<OutputLine> &lines = active();
    // Background tracks the window's active state (matching the editor/tree).
    TColorDesired bg = windowBg(owner, owner && (owner->state & sfActive));
    TColorAttr cNormal {TColorRGB(0xCBD6F2), bg};
    TColorAttr cError  {TColorRGB(0xFF8B8B), bg};
    TColorAttr cWarn   {TColorRGB(0xEAC78A), bg};
    TColorAttr cInfo   {TColorRGB(0xAEC9FF), bg};
    TColorRGB focusBg = lighten(bg, 26); // current-line highlight on the blue bg

    int n = (int) lines.size();
    bool focusedView = (state & sfSelected) != 0;
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
            // Highlight the current line while the pane is focused, so it is
            // clear which line Enter will open.
            if (focusedView && idx == focused)
                ::setBack(c, focusBg);
            b.moveChar(0, ' ', c, size.x);
            std::string t = ln.text;
            if ((int) t.size() > size.x)
                t = t.substr(0, size.x);
            b.moveStr(0, t.c_str(), c);
        }
        writeLine(0, y, size.x, 1, b);
    }
}

void OutputView::activate(int idx) noexcept
{
    std::vector<OutputLine> &lines = active();
    if (idx >= 0 && idx < (int) lines.size() &&
        !lines[idx].file.empty() && onActivate)
        onActivate(lines[idx].file, lines[idx].line);
}

void OutputView::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown)
    {
        TPoint m = makeLocal(ev.mouse.where);
        int idx = topItem + m.y;
        if (idx >= 0 && idx < (int) active().size())
        {
            focused = (short) idx;
            if (ev.mouse.eventFlags & meDoubleClick)
                activate(idx);
            drawView();
        }
        buffers[activeTab].followTail = (range == 0) || (topItem + size.y >= range);
        clearEvent(ev);
        return;
    }
    if (ev.what == evKeyDown && ev.keyDown.keyCode == kbEnter)
    {
        activate(focused);
        clearEvent(ev);
        return;
    }
    TListViewer::handleEvent(ev);
    // Re-enable tail-follow only while scrolled to the bottom; pause it when the
    // user scrolls up to read.
    buffers[activeTab].followTail = (range == 0) || (topItem + size.y >= range);
}

// ---------------------------------------------------------------------------
// OutputTabBar

OutputTabBar::OutputTabBar(const TRect &bounds, OutputView *aview) noexcept :
    TView(bounds), view(aview)
{
    eventMask |= evMouseDown;
}

void OutputTabBar::draw()
{
    // The bar is a lighter shade across its width; the selected tab drops back to
    // the content background so it reads as part of the pane above it. Both track
    // the window's active state.
    bool winActive = owner && (owner->state & sfActive);
    TColorDesired contentBg = windowBg(owner, winActive);
    TColorRGB barBg = lighten(contentBg, 24);
    TColorAttr cBar    {TColorRGB(winActive ? 0x9AA6CE : 0x77819E), barBg};     // unselected tab
    TColorAttr cActive {TColorRGB(winActive ? 0xFFFFFF : 0xC8D4F0), contentBg}; // selected tab

    // Wrapped in white tortoise-shell brackets (U+3018/U+3019).
    static const char *const labels[otTabCount] = {"\xE3\x80\x94" "BUILD" "\xE3\x80\x95",
                                                   "\xE3\x80\x94" "GIT" "\xE3\x80\x95"};
    TDrawBuffer b;
    b.moveChar(0, ' ', cBar, size.x);
    int x = 1;
    for (int t = 0; t < otTabCount; ++t)
    {
        int len = (int) strwidth(labels[t]); // display cells (brackets are wide/multi-byte)
        bool on = view && view->activeTab == t;
        b.moveStr(x, labels[t], on ? cActive : cBar);
        tabX0[t] = x;
        tabX1[t] = x + len;
        x = tabX1[t] + 1; // one space between tabs
    }
    writeLine(0, 0, size.x, 1, b);
}

void OutputTabBar::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown)
    {
        TPoint m = makeLocal(ev.mouse.where);
        for (int t = 0; t < otTabCount; ++t)
            if (view && m.x >= tabX0[t] && m.x < tabX1[t])
            {
                view->setActiveTab(t); // also redraws this bar
                break;
            }
        clearEvent(ev); // the bar swallows its own clicks
        return;
    }
    TView::handleEvent(ev);
}

// ---------------------------------------------------------------------------
// OutputWindow

OutputWindow::OutputWindow(const TRect &bounds, OutputWindow **aptr) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, "Output", wnNoNumber),
    ptr(aptr)
{
    flags = wfClose; // docked pane: no move/grow/zoom; top border resizes it
    options |= ofFirstClick;
    auto *vsb = standardScrollBar(sbVertical | sbHandleKeyboard);
    TRect inner = getExtent();
    inner.grow(-1, -1);

    // The list fills the interior except for the bottom row, which holds the
    // tab bar. The list grows with the window; the bar stays docked at the foot.
    TRect viewR = inner;
    viewR.b.y -= 1;
    // The scrollbar belongs to the list only: end it where the tab row starts.
    {
        TRect sr = vsb->getBounds();
        sr.b.y = viewR.b.y;
        vsb->setBounds(sr);
        vsb->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    }
    view = new OutputView(viewR, vsb);
    view->growMode = gfGrowHiX | gfGrowHiY;
    insert(view);

    TRect tabR = inner;
    tabR.a.y = tabR.b.y - 1;
    tabBar = new OutputTabBar(tabR, view);
    tabBar->growMode = gfGrowLoY | gfGrowHiY | gfGrowHiX;
    insert(tabBar);
    view->tabBar = tabBar;
}

void OutputWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    // Active/inactive changes the content + tab-bar background; repaint them
    // (the frame already repaints itself on activation).
    if ((aState & sfActive) && view)
    {
        view->drawView();
        if (tabBar)
            tabBar->drawView();
    }
}

void OutputWindow::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown)
    {
        TPoint m = makeLocal(ev.mouse.where);
        // The top border (minus the close box on the left) is a vertical resize
        // handle: drag it up/down to grow/shrink the pane. onResizeTo re-lays
        // out the editors above on each step.
        if (m.y == 0 && m.x >= 5 && m.x < size.x - 1 && onResizeTo)
        {
            do {
                onResizeTo(ev.mouse.where.y);
            } while (mouseEvent(ev, evMouseMove | evMouseAuto));
            clearEvent(ev);
            return;
        }
    }
    TWindow::handleEvent(ev);
}

void OutputWindow::sizeLimits(TPoint &min, TPoint &max) noexcept
{
    TWindow::sizeLimits(min, max);
    // Allow a short docked pane. The default min height (~6 rows) makes locate()
    // grow the pane's bottom edge downward past the desktop on smaller
    // terminals, pushing the bottom border off-screen under the status line.
    // 4 rows = top frame + one list row + tab bar + bottom frame.
    min.y = 4;
    min.x = 10;
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
    if (view)
        view->tabBar = nullptr; // drop the back-pointer before subviews are freed
    view = nullptr;
    tabBar = nullptr;
    TWindow::shutDown();
}

// ---------------------------------------------------------------------------
// Build-output line parsing

namespace {

namespace fs = std::filesystem;

// Resolve 'file' against 'root' and confirm it is a real file; return the
// absolute, normalised path, or "" if it doesn't exist (which keeps incidental
// "a:b:c" text from becoming bogus clickable links).
std::string resolveExisting(const std::string &file, const std::string &root) noexcept
{
    if (file.empty())
        return {};
    std::error_code ec;
    fs::path p = fs::path(root) / file; // if 'file' is absolute, this yields 'file'
    p = p.lexically_normal();
    if (fs::exists(p, ec) && !fs::is_directory(p, ec))
        return p.string();
    return {};
}

std::string lower(std::string s) noexcept
{
    for (auto &c : s) c = (char) std::tolower((unsigned char) c);
    return s;
}

} // namespace

OutputLine parseBuildLine(const std::string &raw, const std::string &root) noexcept
{
    OutputLine out;
    out.text = raw;
    out.kind = okNormal;

    // gcc/clang/generic: file:line[:col][: (error|warning|note|fatal error)]
    static const std::regex reGcc(
        R"(^\s*(\S[^:]*):(\d+)(?::\d+)?:\s*(error|warning|note|fatal error)?)",
        std::regex::icase);
    // MSVC: file(line[,col]) : error|warning
    static const std::regex reMsvc(
        R"(^\s*(\S[^(]*)\((\d+)(?:,\d+)?\)\s*:\s*(error|warning))",
        std::regex::icase);
    // Python traceback frame: File "path", line N
    static const std::regex rePy(
        R"(^\s*File \"([^\"]+)\", line (\d+))");

    std::string file;
    long line = 0;
    std::smatch m;
    if (std::regex_search(raw, m, reGcc))
    {
        file = m[1].str();
        line = std::atol(m[2].str().c_str());
        std::string sev = m[3].matched ? lower(m[3].str()) : "";
        if (sev == "error" || sev == "fatal error") out.kind = okError;
        else if (sev == "warning")                  out.kind = okWarning;
        else if (sev == "note")                     out.kind = okInfo;
    }
    else if (std::regex_search(raw, m, reMsvc))
    {
        file = m[1].str();
        line = std::atol(m[2].str().c_str());
        out.kind = (lower(m[3].str()) == "error") ? okError : okWarning;
    }
    else if (std::regex_search(raw, m, rePy))
    {
        file = m[1].str();
        line = std::atol(m[2].str().c_str());
        out.kind = okError;
    }

    std::string abs = resolveExisting(file, root);
    if (!abs.empty())
    {
        out.file = abs;
        out.line = line > 0 ? line - 1 : 0; // 0-based for SCI_GOTOLINE
    }
    return out;
}
