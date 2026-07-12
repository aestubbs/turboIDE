#define Uses_TKeys
#define Uses_TEvent
#define Uses_TView
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TDrawBuffer
#define Uses_TScreen
#define Uses_TScrollBar
#define Uses_TEventQueue
#define Uses_TClipboard
// Pull in <tvision/editors.h> for the cmFind/cmReplace command constants used
// below. Declared here so this file does not depend on a unity-build batch-mate
// (e.g. app.cc via Uses_TIndicator) having included editors.h first.
#define __INC_EDITORS_H
#include <tvision/tv.h>

#include "terminal.h"
#include "app.h"
#include "cmds.h"

#include <turbo/basicwindow.h> // shared window-chrome scheme (windowSchemeActive)
#include <turbo/styles.h>      // schemeActive/sNormal for theme-matched defaults

#include <vterm.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <regex>
#include <vector>

namespace {

// ---- small helpers ---------------------------------------------------------

// Resolve 'file' (absolute, or relative to 'root') to an existing file's
// absolute, normalised path, or "" if it is missing or a directory. (Same rule
// the output pane uses for clickable build errors.)
std::string resolveExistingPath(const std::string &file, const std::string &root) noexcept
{
    if (file.empty())
        return {};
    std::error_code ec;
    std::filesystem::path p = std::filesystem::path(root) / file; // abs 'file' wins
    p = p.lexically_normal();
    if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec))
        return p.string();
    return {};
}

void appendUtf8(std::string &s, uint32_t cp) noexcept
{
    if (cp < 0x80)
        s += char(cp);
    else if (cp < 0x800)
    {
        s += char(0xC0 | (cp >> 6));
        s += char(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
    else
    {
        s += char(0xF0 | (cp >> 18));
        s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
}

// Default terminal colours follow the editor's normal text style (the active
// 24-bit theme), so an otherwise-unstyled terminal matches the editor windows
// and the rest of the app -- and re-themes along with them. Cells that request
// their own colour via SGR (e.g. `ls --color`) keep it.
TColorDesired termDefaultFg() noexcept
    { return ::getFore(turbo::schemeActive[turbo::sNormal]); }
TColorDesired termDefaultBg() noexcept
    { return ::getBack(turbo::schemeActive[turbo::sNormal]); }

// Map a libvterm colour to a Turbo Vision colour. Default fg/bg fall back to the
// Turbo window colours above; indexed colours go through the xterm-256 palette
// (Turbo Vision renders indices 0..15 as the BIOS colours); RGB colours pass
// straight through (Turbo Vision quantises if the backend can't show truecolour).
TColorDesired mapColor(const VTermColor &c, bool isFg) noexcept
{
    if (isFg && VTERM_COLOR_IS_DEFAULT_FG(&c))
        return termDefaultFg();
    if (!isFg && VTERM_COLOR_IS_DEFAULT_BG(&c))
        return termDefaultBg();
    if (VTERM_COLOR_IS_RGB(&c))
        return TColorDesired(int((c.rgb.red << 16) | (c.rgb.green << 8) | c.rgb.blue));
    return TColorDesired(TColorXTerm(c.indexed.idx));
}

TColorAttr mapCellAttr(const VTermScreenCell &cell) noexcept
{
    TColorDesired fg = mapColor(cell.fg, true);
    TColorDesired bg = mapColor(cell.bg, false);
    ushort style = 0;
    if (cell.attrs.bold)      style |= slBold;
    if (cell.attrs.underline) style |= slUnderline;
    if (cell.attrs.italic)    style |= slItalic;
    if (cell.attrs.blink)     style |= slBlink;
    if (cell.attrs.strike)    style |= slStrike;
    // Render reverse video by swapping fg/bg ourselves: slReverse is represented
    // inconsistently across terminals, while an explicit swap always looks right.
    if (cell.attrs.reverse)
    {
        TColorDesired t = fg; fg = bg; bg = t;
    }
    if (cell.attrs.conceal)
        fg = bg;
    return TColorAttr(fg, bg, style);
}

bool sameAttr(const TColorAttr &a, const TColorAttr &b) noexcept
{
    return ::getFore(a) == ::getFore(b) &&
           ::getBack(a) == ::getBack(b) &&
           ::getStyle(a) == ::getStyle(b);
}

VTermModifier modsOf( ushort controlKeyState ) noexcept
{
    int m = VTERM_MOD_NONE;
    if (controlKeyState & kbShift)
        m |= VTERM_MOD_SHIFT;
    if (controlKeyState & (kbLeftAlt | kbRightAlt))
        m |= VTERM_MOD_ALT;
    if (controlKeyState & (kbLeftCtrl | kbRightCtrl))
        m |= VTERM_MOD_CTRL;
    return VTermModifier(m);
}

// Turbo Vision key code -> libvterm named key (VTERM_KEY_NONE if not special).
VTermKey namedKey(ushort keyCode) noexcept
{
    switch (keyCode)
    {
        case kbEnter:    return VTERM_KEY_ENTER;
        case kbTab:
        case kbShiftTab: return VTERM_KEY_TAB;
        case kbBack:     return VTERM_KEY_BACKSPACE;
        case kbEsc:      return VTERM_KEY_ESCAPE;
        case kbUp:       return VTERM_KEY_UP;
        case kbDown:     return VTERM_KEY_DOWN;
        case kbLeft:     return VTERM_KEY_LEFT;
        case kbRight:    return VTERM_KEY_RIGHT;
        case kbHome:     return VTERM_KEY_HOME;
        case kbEnd:      return VTERM_KEY_END;
        case kbPgUp:     return VTERM_KEY_PAGEUP;
        case kbPgDn:     return VTERM_KEY_PAGEDOWN;
        case kbIns:      return VTERM_KEY_INS;
        case kbDel:      return VTERM_KEY_DEL;
        case kbF1:       return VTermKey(VTERM_KEY_FUNCTION(1));
        case kbF2:       return VTermKey(VTERM_KEY_FUNCTION(2));
        case kbF3:       return VTermKey(VTERM_KEY_FUNCTION(3));
        case kbF4:       return VTermKey(VTERM_KEY_FUNCTION(4));
        case kbF5:       return VTermKey(VTERM_KEY_FUNCTION(5));
        case kbF6:       return VTermKey(VTERM_KEY_FUNCTION(6));
        case kbF7:       return VTermKey(VTERM_KEY_FUNCTION(7));
        case kbF8:       return VTermKey(VTERM_KEY_FUNCTION(8));
        case kbF9:       return VTermKey(VTERM_KEY_FUNCTION(9));
        case kbF10:      return VTermKey(VTERM_KEY_FUNCTION(10));
        case kbF11:      return VTermKey(VTERM_KEY_FUNCTION(11));
        case kbF12:      return VTermKey(VTERM_KEY_FUNCTION(12));
        default:         return VTERM_KEY_NONE;
    }
}

// Split a configured shell command line ("zsh -l") into command + args. Naive
// whitespace splitting -- good enough for the shells people actually configure.
void splitCommand(const std::string &cmd, std::string &prog,
                  std::vector<std::string> &args) noexcept
{
    size_t i = 0, n = cmd.size();
    bool first = true;
    while (i < n)
    {
        while (i < n && (cmd[i] == ' ' || cmd[i] == '\t')) ++i;
        size_t start = i;
        while (i < n && cmd[i] != ' ' && cmd[i] != '\t') ++i;
        if (i > start)
        {
            std::string tok = cmd.substr(start, i - start);
            if (first) { prog = tok; first = false; }
            else        args.push_back(tok);
        }
    }
}

// ---- libvterm C callback trampolines ---------------------------------------

void cbOutput(const char *s, size_t len, void *user) noexcept
{
    ((TerminalView *) user)->onPtyOutput(s, len);
}

int cbMoveCursor(VTermPos pos, VTermPos, int visible, void *user) noexcept
{
    ((TerminalView *) user)->onMoveCursor(pos.row, pos.col, visible != 0);
    return 1;
}

int cbSetTermProp(VTermProp prop, VTermValue *val, void *user) noexcept
{
    auto *v = (TerminalView *) user;
    if (prop == VTERM_PROP_CURSORVISIBLE)
        v->onCursorVisible(val->boolean != 0);
    else if (prop == VTERM_PROP_MOUSE)
        v->onMouseMode(val->number);
    else if (prop == VTERM_PROP_TITLE)
    {
        VTermStringFragment f = val->string;
        v->onTitleFragment(std::string_view(f.str, f.len), f.initial, f.final);
    }
    return 1;
}

int cbBell(void *) noexcept { return 1; } // no audible bell

int cbPushLine(int cols, const VTermScreenCell *cells, void *user) noexcept
{
    // A line scrolled off the top of the live screen; capture it (already mapped
    // to render colours) for the scrollback buffer. Trailing never-written cells
    // are trimmed to save memory; the renderer default-fills the rest of the row.
    auto *v = (TerminalView *) user;
    std::vector<TerminalView::SbCell> line;
    line.reserve(cols);
    for (int i = 0; i < cols; ++i)
    {
        uint32_t ch = cells[i].chars[0];
        TColorAttr attr = (ch == (uint32_t) -1) ? TColorAttr() : mapCellAttr(cells[i]);
        line.push_back({ch, attr});
    }
    while (!line.empty() && line.back().ch == 0)
        line.pop_back();
    v->pushScrollbackLine(std::move(line));
    return 1;
}

int cbSbClear(void *user) noexcept
{
    ((TerminalView *) user)->onScrollbackClear();
    return 1;
}

const VTermScreenCallbacks screenCbs = {
    /* damage      */ nullptr,
    /* moverect    */ nullptr,
    /* movecursor  */ cbMoveCursor,
    /* settermprop */ cbSetTermProp,
    /* bell        */ cbBell,
    /* resize      */ nullptr,
    /* sb_pushline */ cbPushLine,
    /* sb_popline  */ nullptr, // on window-grow the new top rows are left blank
    /* sb_clear    */ cbSbClear,
    /* sb_pushline4*/ nullptr,
};

// Emit one screen row into 'b' using a per-column fetcher. fetch(x, ch, attr)
// returns 1 for a real cell, 0 for the trailing half of a wide glyph (skip the
// column -- the wide glyph already covers it), or -1 for no cell (leave the
// default fill). Equal-attribute runs are coalesced into one moveStr.
template <class Fetch>
void emitRow(TDrawBuffer &b, int w, const TColorAttr &defAttr, Fetch fetch) noexcept
{
    std::string run;
    int runStart = 0;
    TColorAttr runAttr = defAttr;
    bool inRun = false;
    auto flush = [&] {
        if (inRun)
        {
            b.moveStr((ushort) runStart, TStringView(run), runAttr);
            run.clear();
            inRun = false;
        }
    };
    for (int x = 0; x < w; ++x)
    {
        uint32_t ch = 0;
        TColorAttr attr = defAttr;
        int r = fetch(x, ch, attr);
        if (r == 0)
            continue;               // wide-glyph trailing half: already covered
        if (r < 0)
        {
            flush();                // gap / past end: keep the default fill
            continue;
        }
        if (inRun && !sameAttr(attr, runAttr))
            flush();
        if (!inRun)
        {
            runStart = x;
            runAttr = attr;
            inRun = true;
        }
        appendUtf8(run, ch ? ch : uint32_t(' '));
    }
    flush();
}

} // namespace

// ---------------------------------------------------------------------------
// TerminalView
// ---------------------------------------------------------------------------

TerminalView::TerminalView(const TRect &bounds, std::string command) noexcept :
    TView(bounds),
    launchCommand(std::move(command))
{
    growMode = gfGrowHiX | gfGrowHiY;
    options |= ofSelectable | ofFirstClick;
    // evMouse covers down/up/move/wheel: the extra up/move events are needed to
    // forward clicks and drags to a child that has enabled mouse reporting.
    eventMask |= evKeyboard | evCommand | evBroadcast | evMouse;
    cols = max(1, (int) size.x);
    rows = max(1, (int) size.y);
    startShell();
    if (auto *app = (TurboApp *) TProgram::application)
    {
        app->registerTerminal(this);
        registered = true;
    }
}

TerminalView::~TerminalView()
{
    if (registered)
        if (auto *app = (TurboApp *) TProgram::application)
            app->unregisterTerminal(this);
    stopShell();
}

void TerminalView::startShell() noexcept
{
    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    vterm_output_set_callback(vt, cbOutput, this);
    screen = vterm_obtain_screen(vt);
    vterm_screen_set_callbacks(screen, &screenCbs, this);
    vterm_screen_reset(screen, 1);
    // Bold text uses the bright (8..15) palette variants, as most terminals do.
    if (VTermState *st = vterm_obtain_state(vt))
        vterm_state_set_bold_highbright(st, 1);

    // Resolve what to run: an explicit launch command (e.g. a coding agent),
    // else the configured shell override, else $SHELL, else a sane default.
    std::string shellCmd = launchCommand;
    if (shellCmd.empty())
        if (auto *app = (TurboApp *) TProgram::application)
            shellCmd = app->settings.terminalShell;
    if (shellCmd.empty())
    {
#ifdef _WIN32
        const char *s = getenv("ComSpec"); // typically C:\Windows\System32\cmd.exe
        shellCmd = (s && *s) ? s : "cmd.exe";
#else
        const char *s = getenv("SHELL");
        shellCmd = (s && *s) ? s : "/bin/sh";
#endif
    }
    std::string prog;
    std::vector<std::string> args;
    splitCommand(shellCmd, prog, args);

    std::error_code ec;
    std::string cwd = std::filesystem::current_path(ec).string();
    if (ec)
        cwd.clear();

    if (!pty.start(prog, args, cwd, cols, rows))
    {
        spawnFailed = true;
        childDone = true;
        std::string msg = "\x1b[1;31mCould not start terminal: " + prog +
                          "\x1b[0m\r\n";
        vterm_input_write(vt, msg.data(), msg.size());
        return;
    }

    reader = std::thread([this] {
        char buf[8192];
        for (;;)
        {
            long n = pty.read(buf, sizeof buf);
            if (n <= 0)
                break;
            {
                std::lock_guard<std::mutex> lock(mx);
                incoming.append(buf, (size_t) n);
            }
            TEventQueue::wakeUp(); // nudge the idle loop to drain & repaint
        }
        childDone = true;
        TEventQueue::wakeUp();
    });
}

void TerminalView::stopShell() noexcept
{
    pty.terminate();               // hangs up the child and unblocks the reader
    if (reader.joinable())
        reader.join();             // reader has now returned from pty.read()
    pty.closeRead();               // safe to close the read side: no reader left
    if (vt)
    {
        vterm_free(vt);
        vt = nullptr;
        screen = nullptr;
    }
}

void TerminalView::onPtyOutput(const char *s, size_t len) noexcept
{
    // libvterm asks us to send these bytes (key encodings, query replies) to the
    // child. Called on the main thread, so writing to the PTY here is safe.
    if (!pty.running() && childDone.load())
        return;
    pty.write(s, len);
}

void TerminalView::onMoveCursor(int row, int col, bool visible) noexcept
{
    curRow = row;
    curCol = col;
    curVisible = visible;
}

void TerminalView::onCursorVisible(bool visible) noexcept
{
    curVisible = visible;
}

void TerminalView::onMouseMode(int mode) noexcept
{
    mouseMode = mode;
    if (mode == VTERM_PROP_MOUSE_NONE)
        mousePressedButton = 0;
}

// Translate tvision's control-key state into libvterm modifier flags.
static VTermModifier vtermMouseMod(ushort cks) noexcept
{
    int m = VTERM_MOD_NONE;
    if (cks & kbShift)      m |= VTERM_MOD_SHIFT;
    if (cks & kbCtrlShift)  m |= VTERM_MOD_CTRL; // kbCtrlShift == both Ctrl bits
    if (cks & kbAltShift)   m |= VTERM_MOD_ALT;  // kbAltShift  == both Alt bits
    return (VTermModifier) m;
}

bool TerminalView::forwardMouseEvent(TEvent &ev) noexcept
{
    if (mouseMode == VTERM_PROP_MOUSE_NONE || !vt)
        return false;
    TPoint p = makeLocal(ev.mouse.where);
    int col = max(0, min((int) p.x, cols - 1));
    int row = max(0, min((int) p.y, rows - 1));
    VTermModifier mod = vtermMouseMod(ev.mouse.controlKeyState);

    switch (ev.what)
    {
        case evMouseWheel:
        {
            // Wheel is reported as a momentary press of buttons 4-7 (xterm).
            int button = (ev.mouse.wheel & mwUp)   ? 4
                       : (ev.mouse.wheel & mwDown) ? 5
                       : (ev.mouse.wheel & mwLeft) ? 6
                       : (ev.mouse.wheel & mwRight)? 7 : 0;
            if (!button)
                return false;
            vterm_mouse_move(vt, row, col, mod);
            vterm_mouse_button(vt, button, true, mod);
            return true;
        }
        case evMouseDown:
        {
            int button = (ev.mouse.buttons & mbRightButton)  ? 3
                       : (ev.mouse.buttons & mbMiddleButton) ? 2 : 1;
            mousePressedButton = button;
            vterm_mouse_move(vt, row, col, mod);
            vterm_mouse_button(vt, button, true, mod);
            return true;
        }
        case evMouseUp:
        {
            int button = mousePressedButton ? mousePressedButton : 1;
            vterm_mouse_move(vt, row, col, mod);
            vterm_mouse_button(vt, button, false, mod);
            mousePressedButton = 0;
            return true;
        }
        case evMouseMove:
            // libvterm gates motion on the child's mode (drag vs any-motion).
            vterm_mouse_move(vt, row, col, mod);
            return true;
        default:
            return false;
    }
}

bool TerminalView::openPathAt(TPoint where) noexcept
{
    auto *app = (TurboApp *) TProgram::application;
    if (!app)
        return false;
    TPoint p = makeLocal(where);
    int y = p.y, clickCol = p.x;
    if (y < 0 || clickCol < 0 || clickCol >= cols)
        return false;

    // Reconstruct the clicked row's code points by column, from scrollback or the
    // live screen (same virtual-row mapping the renderer uses in draw()).
    int S = (int) scrollback.size();
    int vi = (S - scrollOffset) + y;
    std::vector<uint32_t> cps(cols, 0);
    if (vi >= 0 && vi < S)
    {
        const std::vector<SbCell> &line = scrollback[vi];
        for (int x = 0; x < cols && x < (int) line.size(); ++x)
        {
            uint32_t ch = line[x].ch;
            cps[x] = (ch == (uint32_t) -1) ? 0 : ch;
        }
    }
    else if (screen)
    {
        int liveRow = vi - S;
        if (liveRow < 0 || liveRow >= rows)
            return false;
        for (int x = 0; x < cols; ++x)
        {
            VTermScreenCell cell;
            VTermPos pos { liveRow, x };
            if (vterm_screen_get_cell(screen, pos, &cell))
            {
                uint32_t ch = cell.chars[0];
                cps[x] = (ch == (uint32_t) -1) ? 0 : ch;
            }
        }
    }
    else
        return false;

    // A path token is a maximal run of non-space characters, excluding the
    // punctuation that usually bounds a path in prose (quotes/brackets/commas).
    auto isPathChar = [](uint32_t c) -> bool {
        if (c < 33)
            return false; // control chars and space
        switch (c)
        {
            case '"': case '\'': case '`':
            case '(': case ')': case '[': case ']': case '{': case '}':
            case '<': case '>': case ',': case ';':
                return false;
        }
        return true;
    };
    if (!isPathChar(cps[clickCol]))
        return false;
    int lo = clickCol, hi = clickCol;
    while (lo > 0 && isPathChar(cps[lo - 1]))
        --lo;
    while (hi + 1 < cols && isPathChar(cps[hi + 1]))
        ++hi;

    std::string token;
    for (int x = lo; x <= hi; ++x)
        appendUtf8(token, cps[x] ? cps[x] : (uint32_t) ' ');
    // Trailing sentence punctuation is not part of the path.
    while (!token.empty() && (token.back() == '.' || token.back() == ':'))
        token.pop_back();
    if (token.empty())
        return false;

    const std::string &root = app->projectRoot;
    // Prefer a "path:line[:col]" reference, else the bare path.
    static const std::regex reSuffix(R"(^(.+?):(\d+)(?::\d+)?$)");
    std::smatch m;
    if (std::regex_match(token, m, reSuffix))
    {
        std::string abs = resolveExistingPath(m[1].str(), root);
        if (!abs.empty())
        {
            long ln = std::atol(m[2].str().c_str());
            app->openOrFocus(abs, ln > 0 ? ln - 1 : 0); // 0-based for SCI_GOTOLINE
            return true;
        }
    }
    std::string abs = resolveExistingPath(token, root);
    if (!abs.empty())
    {
        app->openOrFocus(abs); // no line: open at the top
        return true;
    }
    return false;
}

void TerminalView::onTitleFragment(std::string_view frag, bool initial, bool final) noexcept
{
    if (initial)
        titlePending.clear();
    titlePending.append(frag.data(), frag.size());
    if (final)
    {
        if (auto *w = (TerminalWindow *) owner)
            w->setTermTitle(titlePending);
        titlePending.clear();
    }
}

void TerminalView::pump() noexcept
{
    if (!vt)
        return;
    std::string data;
    {
        std::lock_guard<std::mutex> lock(mx);
        if (!incoming.empty())
            data.swap(incoming);
    }
    bool changed = false;
    if (!data.empty())
    {
        vterm_input_write(vt, data.data(), data.size());
        changed = true;
    }
    if (childDone.load() && !exitNoticeShown && !spawnFailed)
    {
        exitNoticeShown = true;
        const char notice[] =
            "\r\n\x1b[7m[process exited \xE2\x80\x93 press a key to close]\x1b[0m";
        vterm_input_write(vt, notice, sizeof notice - 1);
        changed = true;
    }
    if (changed)
    {
        updateScrollbar(); // scrollback may have grown
        drawView();
        updateCursor();
    }
}

void TerminalView::draw()
{
    const TColorAttr defAttr = TColorAttr(termDefaultFg(), termDefaultBg());
    int w = size.x, h = size.y;
    int S = (int) scrollback.size();
    for (int y = 0; y < h; ++y)
    {
        TDrawBuffer b;
        b.moveChar(0, ' ', defAttr, (ushort) w); // default-fill, then overlay
        // Map view row y to a virtual line: [0, S) are scrollback (oldest first),
        // [S, S+rows) are the live screen. scrollOffset shifts the window up.
        int vi = (S - scrollOffset) + y;
        if (vi >= 0 && vi < S)
        {
            const std::vector<SbCell> &line = scrollback[vi];
            emitRow(b, w, defAttr, [&] (int x, uint32_t &ch, TColorAttr &attr) -> int {
                if (x >= (int) line.size())
                    return -1;
                const SbCell &c = line[x];
                if (c.ch == (uint32_t) -1)
                    return 0;
                ch = c.ch;
                attr = c.attr;
                return 1;
            });
        }
        else if (screen)
        {
            int liveRow = vi - S;
            if (liveRow >= 0 && liveRow < rows)
                emitRow(b, w, defAttr, [&] (int x, uint32_t &ch, TColorAttr &attr) -> int {
                    if (x >= cols)
                        return -1;
                    VTermScreenCell cell;
                    VTermPos pos { liveRow, x };
                    if (!vterm_screen_get_cell(screen, pos, &cell))
                        return -1;
                    if (cell.chars[0] == (uint32_t) -1)
                        return 0; // wide-glyph trailing half
                    ch = cell.chars[0];
                    attr = mapCellAttr(cell);
                    return 1;
                });
        }
        writeLine(0, y, (ushort) w, 1, b);
    }
}

void TerminalView::pushScrollbackLine(std::vector<SbCell> &&line) noexcept
{
    scrollback.push_back(std::move(line));
    if ((int) scrollback.size() > scrollbackMax)
        scrollback.pop_front();
    // Keep a scrolled-up view looking at the same content as new lines arrive
    // below it (it only follows the live screen when at the bottom).
    if (scrollOffset > 0)
        scrollOffset = min(scrollOffset + 1, (int) scrollback.size());
}

void TerminalView::onScrollbackClear() noexcept
{
    scrollback.clear();
    scrollOffset = 0;
    updateScrollbar();
}

void TerminalView::scrollLines(int delta) noexcept
{
    int maxOff = (int) scrollback.size();
    int n = min(max(scrollOffset + delta, 0), maxOff);
    if (n == scrollOffset)
        return;
    scrollOffset = n;
    updateScrollbar();
    drawView();
    updateCursor();
}

void TerminalView::scrollToBottom() noexcept
{
    if (scrollOffset == 0)
        return;
    scrollOffset = 0;
    updateScrollbar();
    drawView();
    updateCursor();
}

void TerminalView::updateScrollbar() noexcept
{
    if (!vScrollBar)
        return;
    int S = (int) scrollback.size();
    // Thumb value runs 0 (oldest) .. S (live bottom); it sits at the bottom when
    // following. Setting the same value is a no-op, so this can't loop with the
    // cmScrollBarChanged handler.
    vScrollBar->setParams(S - scrollOffset, 0, S, max(rows - 1, 1), 1);
}

void TerminalView::setScrollBar(TScrollBar *sb) noexcept
{
    vScrollBar = sb;
    updateScrollbar();
}

void TerminalView::changeBounds(const TRect &bounds)
{
    TView::changeBounds(bounds);
    recomputeSize();
}

void TerminalView::recomputeSize() noexcept
{
    int nc = max(1, (int) size.x);
    int nr = max(1, (int) size.y);
    if (nc == cols && nr == rows)
        return;
    cols = nc;
    rows = nr;
    if (vt)
        vterm_set_size(vt, rows, cols); // libvterm reflows its grid
    pty.resize(cols, rows);             // also delivers SIGWINCH to the child
    scrollOffset = min(scrollOffset, (int) scrollback.size());
    updateScrollbar();                  // page step depends on the row count
    drawView();
}

void TerminalView::updateCursor() noexcept
{
    if (curVisible && (state & sfFocused) && !childDone.load() &&
        curCol >= 0 && curCol < size.x && curRow >= 0 && curRow < size.y)
    {
        setCursor(curCol, curRow);
        showCursor();
    }
    else
        hideCursor();
}

void TerminalView::setState(ushort aState, Boolean enable)
{
    TView::setState(aState, enable);
    if (aState & (sfFocused | sfActive | sfExposed))
        updateCursor();
}

void TerminalView::sendText(TStringView utf8, ushort mod) noexcept
{
    // Decode UTF-8 and feed each code point to libvterm, which encodes it for
    // the child (honouring application/normal modes, bracketed paste, etc.).
    const unsigned char *p = (const unsigned char *) utf8.data();
    size_t n = utf8.size();
    for (size_t i = 0; i < n; )
    {
        unsigned char c = p[i];
        uint32_t cp;
        int len;
        if (c < 0x80)            { cp = c;          len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { ++i; continue; } // invalid lead byte
        if (i + len > n) break;
        for (int k = 1; k < len; ++k)
            cp = (cp << 6) | (p[i + k] & 0x3F);
        i += len;
        vterm_keyboard_unichar(vt, cp, VTermModifier(mod));
    }
}

bool TerminalView::sendKey(const KeyDownEvent &k) noexcept
{
    VTermModifier mod = modsOf(k.controlKeyState);
    if (VTermKey vk = namedKey(k.keyCode))
    {
        if (k.keyCode == kbShiftTab)
            mod = VTermModifier(mod | VTERM_MOD_SHIFT);
        vterm_keyboard_key(vt, vk, mod);
        return true;
    }
    // Printable text (handles letters, digits, symbols and Unicode input). The
    // shift is already baked into the character, so don't also pass it as a mod.
    TStringView text = k.getText();
    if (text.size() > 0)
    {
        sendText(text, ushort(mod & ~VTERM_MOD_SHIFT));
        return true;
    }
    // Control combinations arrive with no text but a control char code (^A..^Z).
    uchar cc = k.charScan.charCode;
    if (cc >= 1 && cc <= 26)
    {
        vterm_keyboard_unichar(vt, uint32_t('a' + (cc - 1)),
                               VTermModifier(mod | VTERM_MOD_CTRL));
        return true;
    }
    if (cc >= 32 && cc < 127)
    {
        vterm_keyboard_unichar(vt, cc, VTermModifier(mod & ~VTERM_MOD_SHIFT));
        return true;
    }
    return false;
}

bool TerminalView::sendCommandKey(ushort command) noexcept
{
    // Some control keys the shell needs (^C, ^D, ^R, ^Z, ...) are claimed as
    // global menu/status-line accelerators and reach us as commands rather than
    // key events. When the terminal is focused, translate them back to the
    // control byte the shell expects. Paste pulls from the system clipboard.
    uint32_t ch = 0;
    switch (command)
    {
        case cmCopy:                 ch = 'c'; break; // ^C (interrupt)
        case cmCut:                  ch = 'x'; break; // ^X
        case cmUndo:                 ch = 'z'; break; // ^Z (suspend)
        case cmRedo:                 ch = 'y'; break; // ^Y
        case cmFind:                 ch = 'f'; break; // ^F
        case cmReplace:              ch = 'r'; break; // ^R (history search)
        case cmGoToLine:             ch = 'g'; break; // ^G
        case cmToggleComment:        ch = 'e'; break; // ^E
        case cmSelectNextOccurrence: ch = 'd'; break; // ^D (EOF)
        case cmCloseEditor:          ch = 'w'; break; // ^W (erase word)
        case cmPaste:
            TClipboard::requestText(); // arrives back as a kbPaste key event
            return true;
        default:
            return false;
    }
    vterm_keyboard_unichar(vt, ch, VTERM_MOD_CTRL);
    return true;
}

void TerminalView::handlePaste(TEvent &ev) noexcept
{
    char buf[4096];
    size_t len;
    vterm_keyboard_start_paste(vt);
    while (textEvent(ev, TSpan<char>(buf, sizeof buf), len))
        sendText(TStringView(buf, len), VTERM_MOD_NONE);
    vterm_keyboard_end_paste(vt);
}

void TerminalView::handleEvent(TEvent &ev)
{
    TView::handleEvent(ev);
    if (!vt)
        return;
    switch (ev.what)
    {
        case evKeyDown:
        {
            // Scrollback navigation (Shift+PgUp/PgDn). Works even after the child
            // exits, so the final output can be reviewed before dismissing.
            int page = max(rows - 1, 1);
            if (ev.keyDown.keyCode == kbPgUp &&
                (ev.keyDown.controlKeyState & kbShift))
            {
                scrollLines(page);
                clearEvent(ev);
                break;
            }
            if (ev.keyDown.keyCode == kbPgDn &&
                (ev.keyDown.controlKeyState & kbShift))
            {
                scrollLines(-page);
                clearEvent(ev);
                break;
            }
            // Once the child has exited the view is inert; any other key dismisses it.
            if (childDone.load())
            {
                clearEvent(ev);
                TEvent close;
                close.what = evCommand;
                close.message.command = cmClose;
                close.message.infoPtr = nullptr;
                putEvent(close); // deferred: closes the active (this) window
                return;
            }
            scrollToBottom(); // typing returns to the live view
            if (ev.keyDown.controlKeyState & kbPaste)
                handlePaste(ev);
            else if (!sendKey(ev.keyDown))
                return; // not consumed: let it fall through (shouldn't happen)
            clearEvent(ev);
            break;
        }
        case evMouseWheel:
            // A child in mouse mode (e.g. an agent, vim, less) gets the wheel;
            // otherwise it scrolls this terminal's own scrollback.
            if (mouseMode != VTERM_PROP_MOUSE_NONE && forwardMouseEvent(ev))
            {
                clearEvent(ev);
                break;
            }
            if (ev.mouse.wheel & mwUp)
                scrollLines(3);
            else if (ev.mouse.wheel & mwDown)
                scrollLines(-3);
            clearEvent(ev);
            break;
        case evCommand:
            if ((state & sfFocused) && !childDone.load() &&
                sendCommandKey(ev.message.command))
                clearEvent(ev);
            break;
        case evBroadcast:
            // Dragging the scrollbar sets the scroll position.
            if (ev.message.command == cmScrollBarChanged && vScrollBar &&
                ev.message.infoPtr == vScrollBar)
            {
                int S = (int) scrollback.size();
                int n = min(max(S - vScrollBar->value, 0), S);
                if (n != scrollOffset)
                {
                    scrollOffset = n;
                    drawView();
                    updateCursor();
                }
                clearEvent(ev);
            }
            break;
        case evMouseDown:
            // Clicking focuses the terminal. Ctrl+click opens a file path under
            // the cursor (kept out of the child); a plain click is forwarded to
            // the child when it has enabled mouse reporting.
            select();
            if (!((ev.mouse.controlKeyState & kbCtrlShift) && openPathAt(ev.mouse.where)))
                forwardMouseEvent(ev);
            clearEvent(ev);
            break;
        case evMouseMove:
        case evMouseUp:
            forwardMouseEvent(ev); // no-op unless the child is in mouse mode
            clearEvent(ev);
            break;
    }
}

// ---------------------------------------------------------------------------
// TerminalWindow
// ---------------------------------------------------------------------------

TerminalWindow::TerminalWindow(const TRect &bounds, std::string command,
                               std::string title, TerminalWindow **aBackPtr) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, title.c_str(), wnNoNumber),
    baseTitle(title),
    titleBuf(std::move(title)),
    backPtr(aBackPtr)
{
    options |= ofTileable;
    // Drop the drop-shadow: the terminal is normally docked flush against the
    // file tree, and the shadow would darken the tree's left edge (it renders
    // two columns past the window). The tree and help windows do the same.
    state &= ~sfShadow;
    // Vertical scrollbar for the scrollback, on the right border column (the
    // editor windows place theirs the same way). Insert it before the view so
    // the view keeps the initial focus.
    auto *vsb = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
    vsb->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    insert(vsb);
    view = new TerminalView(getExtent().grow(-1, -1), std::move(command));
    insert(view);
    view->setScrollBar(vsb);
}

void TerminalWindow::shutDown()
{
    if (backPtr)
        *backPtr = nullptr;
    view = nullptr;
    TWindow::shutDown();
}

const char *TerminalWindow::getTitle(short)
{
    return titleBuf.c_str();
}

TColorAttr TerminalWindow::mapColor(uchar index) noexcept
{
    // Resolve chrome through the shared window scheme, like the editor windows,
    // file tree and output pane, instead of TVision's default bright-blue palette.
    if (index > 0 && index - 1 < turbo::WindowPaletteItemCount)
        return turbo::windowSchemeActive[index - 1];
    return errorAttr;
}

void TerminalWindow::sizeLimits(TPoint &min, TPoint &max)
{
    TWindow::sizeLimits(min, max);
    min.x = 20;
    min.y = 4;
}

void TerminalWindow::setTermTitle(std::string_view text) noexcept
{
    titleBuf = text.empty() ? baseTitle
                            : baseTitle + ": " + std::string(text);
    if (frame)
        frame->drawView();
}
