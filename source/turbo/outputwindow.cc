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

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <regex>

// ---------------------------------------------------------------------------
// OutputView

OutputView::OutputView(const TRect &bounds, TScrollBar *vScrollBar) noexcept :
    TListViewer(bounds, 1, nullptr, vScrollBar)
{
    options |= ofFirstClick; // the first click on the pane still selects a line
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
    bool active = (state & sfSelected) != 0;
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
            if (active && idx == focused)
                ::setBack(c, TColorRGB(0x2A3350));
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
        if (idx >= 0 && idx < (int) lines.size())
        {
            focused = (short) idx;
            if (ev.mouse.eventFlags & meDoubleClick)
                activate(idx);
            drawView();
        }
        followTail = (range == 0) || (topItem + size.y >= range);
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
    followTail = (range == 0) || (topItem + size.y >= range);
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
    view = new OutputView(inner, vsb);
    view->growMode = gfGrowHiX | gfGrowHiY;
    insert(view);
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
    min.y = 3;
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
    view = nullptr;
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
