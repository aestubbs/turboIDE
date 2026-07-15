#define Uses_TWindow
#define Uses_TFrame
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
    // The two built-in tabs. Tool tabs are appended later by the app.
    buffers.push_back(Buffer{otBuild, "BUILD", {}, true, 0, 0});
    buffers.push_back(Buffer{otGit,   "GIT",   {}, true, 0, 0});
    setRange(0);
}

int OutputView::indexOfId(int id) const noexcept
{
    for (int i = 0; i < (int) buffers.size(); ++i)
        if (buffers[i].id == id)
            return i;
    return -1;
}

OutputView::Buffer *OutputView::bufferById(int id) noexcept
{
    int i = indexOfId(id);
    return i < 0 ? nullptr : &buffers[i];
}

void OutputView::ensureTab(int id, const std::string &title) noexcept
{
    if (Buffer *b = bufferById(id))
        b->title = title; // already present: just keep the label current
    else
        buffers.push_back(Buffer{id, title, {}, true, 0, 0});
    if (tabFrame)
        tabFrame->drawView();
}

void OutputView::removeTab(int id) noexcept
{
    if (id == otBuild || id == otGit) // never drop the built-in tabs
        return;
    int i = indexOfId(id);
    if (i < 0)
        return;
    bool wasActive = (activeId == id);
    buffers.erase(buffers.begin() + i);
    if (wasActive)
        setActiveTab(otBuild); // switches + redraws the tabs
    else
    {
        drawView();
        if (tabFrame)
            tabFrame->drawView();
    }
}

void OutputView::addLine(int id, OutputLine ln) noexcept
{
    Buffer *bp = bufferById(id);
    if (!bp)
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
    Buffer &buf = *bp;
    buf.lines.push_back(std::move(ln));
    // Bound memory: keep the most recent ~5000 lines per buffer.
    const size_t kMax = 5000;
    if (buf.lines.size() > kMax)
        buf.lines.erase(buf.lines.begin(),
                        buf.lines.begin() + (buf.lines.size() - kMax));
    if (id == activeId)
    {
        setRange((short) buf.lines.size());
        if (buf.followTail && range > 0)
            focusItemNum(range - 1); // scroll so the newest line is visible
        drawView();
    }
    // Appending to an inactive tab leaves the visible view untouched; switching
    // to that tab (showTab/setActiveTab) sets the range and follows its tail.
}

void OutputView::clear(int id) noexcept
{
    Buffer *bp = bufferById(id);
    if (!bp)
        return;
    Buffer &buf = *bp;
    buf.lines.clear();
    buf.followTail = true;
    buf.savedTop = 0;
    buf.savedFocused = 0;
    if (id == activeId)
    {
        setRange(0);
        focused = 0;
        topItem = 0;
        drawView();
    }
}

void OutputView::setActiveTab(int id) noexcept
{
    if (indexOfId(id) < 0)
        return;
    if (id != activeId)
    {
        // Remember where the outgoing tab was scrolled, then switch.
        if (Buffer *old = bufferById(activeId))
        {
            old->savedTop = topItem;
            old->savedFocused = focused;
        }
        activeId = id;
    }
    Buffer &buf = *bufferById(activeId);
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
    if (tabFrame)
        tabFrame->drawView();
}

void OutputView::showTab(int id) noexcept
{
    Buffer *bp = bufferById(id);
    if (!bp)
        return;
    // A fresh command's output should be visible, so resume tail-follow.
    bp->followTail = true;
    setActiveTab(id);
}

void OutputView::draw()
{
    std::vector<OutputLine> &lines = active();
    // Background tracks the window's active state (matching the editor/tree).
    bool winActive = owner && (owner->state & sfActive);
    TColorDesired bg = windowBg(owner, winActive);
    TColorAttr cNormal {TColorRGB(0xCBD6F2), bg};
    TColorAttr cError  {TColorRGB(0xFF8B8B), bg};
    TColorAttr cWarn   {TColorRGB(0xEAC78A), bg};
    TColorAttr cInfo   {TColorRGB(0xAEC9FF), bg};
    TColorRGB focusBg = lighten(bg, 26); // current-line highlight on the blue bg

    int n = (int) lines.size();
    // sfSelected alone is not enough: the list stays the window's selected view
    // even while the window is inactive, so keying the highlight off it left a
    // lit row on screen at all times -- which read as a persistent selection.
    // Show it only when this pane actually has the focus.
    bool focusedView = winActive && (state & sfSelected) != 0;
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
        if (Buffer *b = bufferById(activeId))
            b->followTail = (range == 0) || (topItem + size.y >= range);
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
    if (Buffer *b = bufferById(activeId))
        b->followTail = (range == 0) || (topItem + size.y >= range);
}

// ---------------------------------------------------------------------------
// OutputFrame -- the window border, with the tabs drawn into its bottom line

// The tab's shoulders: filled corner triangles that flare out from the border
// line the tab sits on, so it reads as one solid shape rising out of the border
// rather than as loose punctuation around a word. The fill has to be at the
// BOTTOM -- LOWER LEFT then LOWER RIGHT -- or the trapezoid comes out upside
// down, as a notch cut into the border instead of a tab standing on it.
//
// Both are Geometric Shapes with no emoji mapping, so they stay one column wide,
// as the hit-test ranges below assume.
static const char kTabLeft[]  = "\xE2\x97\xA3"; // U+25E3 BLACK LOWER LEFT TRIANGLE
static const char kTabRight[] = "\xE2\x97\xA2"; // U+25E2 BLACK LOWER RIGHT TRIANGLE

OutputFrame::OutputFrame(const TRect &bounds) noexcept :
    TFrame(bounds)
{
}

void OutputFrame::draw()
{
    TFrame::draw(); // the border, title and close box, bottom line included
    drawTabs();     // ... then overlay the tabs onto that bottom line
}

void OutputFrame::drawTabs() noexcept
{
    if (!view || size.y < 2 || size.x < 4)
        return;
    // The bottom border already carries the window background, so the active tab
    // needs no fill of its own: bold + bright white is enough to own the line,
    // and the dim shoulders keep the inactive tabs quiet. Both track the window's
    // active state, like the frame they sit on.
    bool winActive = (state & sfActive) != 0;
    TColorDesired bg = windowBg(owner, winActive);
    TColorAttr cOn      {TColorRGB(winActive ? 0xFFFFFF : 0xC8D4F0), bg, slBold};
    TColorAttr cOnEdge  {TColorRGB(winActive ? 0xB9C6EA : 0x8E9AC0), bg};
    TColorAttr cOff     {TColorRGB(winActive ? 0x8F9BC4 : 0x6C7590), bg};
    TColorAttr cOffEdge {TColorRGB(winActive ? 0x5C6A93 : 0x4A5470), bg};

    int n = (int) view->buffers.size();
    tabX0.assign(n, 0);
    tabX1.assign(n, 0);
    const int y = size.y - 1;
    int x = 2; // clear of the corner: '+-<tab>'
    for (int t = 0; t < n; ++t)
    {
        const std::string &title = view->buffers[t].title;
        bool on = view->activeId == view->buffers[t].id;
        // The shoulders are one column each and butt straight up against the
        // label, with no padding: '<BUILD>'.
        int w = 1 + (int) strwidth(title.c_str()) + 1;
        if (x + w > size.x - 1)
            break; // out of border: the remaining tabs stay reachable by click
                   // on the ones shown, and by the Run menu
        std::string label = kTabLeft + title + kTabRight;
        TDrawBuffer b;
        b.moveStr(0, label.c_str(), on ? cOn : cOff);
        b.putAttribute(0, on ? cOnEdge : cOffEdge);         // the shoulders are
        b.putAttribute((ushort) (w - 1), on ? cOnEdge : cOffEdge); // never bold
        writeLine((short) x, (short) y, (ushort) w, 1, b);
        tabX0[t] = x;
        tabX1[t] = x + w;
        x = tabX1[t] + 1; // one column of border rule between tabs
    }
}

void OutputFrame::handleEvent(TEvent &ev)
{
    // A click on a tab in the bottom border selects it. Checked before TFrame,
    // which owns the rest of the border (close box, drag) -- though with the pane
    // docked (wfClose only) it has no business on the bottom line at all.
    if (ev.what == evMouseDown && view)
    {
        TPoint m = makeLocal(ev.mouse.where);
        int n = (int) min(view->buffers.size(), min(tabX0.size(), tabX1.size()));
        if (m.y == size.y - 1)
            for (int t = 0; t < n; ++t)
                if (m.x >= tabX0[t] && m.x < tabX1[t])
                {
                    view->setActiveTab(view->buffers[t].id); // redraws this frame
                    clearEvent(ev);
                    return;
                }
    }
    TFrame::handleEvent(ev);
}

// ---------------------------------------------------------------------------
// OutputWindow

TFrame *OutputWindow::initFrame(TRect r)
{
    return new OutputFrame(r);
}

OutputWindow::OutputWindow(const TRect &bounds, OutputWindow **aptr) noexcept :
    TWindowInit(&OutputWindow::initFrame),
    TWindow(bounds, "Output", wnNoNumber),
    ptr(aptr)
{
    flags = wfClose; // docked pane: no move/grow/zoom; top border resizes it
    options |= ofFirstClick;
    auto *vsb = standardScrollBar(sbVertical | sbHandleKeyboard);
    TRect inner = getExtent();
    inner.grow(-1, -1);

    // The list gets the whole interior: the tabs live in the bottom border, not
    // in a row of their own.
    view = new OutputView(inner, vsb);
    view->growMode = gfGrowHiX | gfGrowHiY;
    insert(view);

    // TWindowInit built the frame before 'view' existed, so wire the two up now.
    tabFrame = (OutputFrame *) frame;
    if (tabFrame)
    {
        tabFrame->view = view;
        view->tabFrame = tabFrame;
    }
}

void OutputWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    // Active/inactive changes the content background; repaint it (the frame,
    // tabs included, already repaints itself on activation).
    if ((aState & sfActive) && view)
        view->drawView();
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
    // Allow a short docked pane. TWindow's default min height (~6 rows) makes
    // locate() grow the pane's bottom edge downward past the desktop on smaller
    // terminals, pushing the bottom border off-screen under the status line.
    min.y = minOutputRows;
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
        view->tabFrame = nullptr; // drop the back-pointers before subviews are freed
    if (tabFrame)
        tabFrame->view = nullptr;
    view = nullptr;
    tabFrame = nullptr;
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
