#define Uses_TKeys
#define Uses_TEvent
#define Uses_TView
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TDrawBuffer
#define Uses_TScreen
#define Uses_TEventQueue
#define Uses_TClipboard
#include <tvision/tv.h>

#include "terminal.h"
#include "app.h"
#include "cmds.h"

#include <vterm.h>

#include <cstring>
#include <filesystem>
#include <vector>

namespace {

// ---- small helpers ---------------------------------------------------------

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

// Default terminal colours: light grey on the classic Turbo blue, so an
// otherwise-unstyled terminal matches the editor and file-tree windows. Cells
// that request their own colour via SGR (e.g. `ls --color`) keep it.
const TColorDesired kTermDefaultFg = TColorBIOS(0x7);
const TColorDesired kTermDefaultBg = TColorBIOS(0x1);

// Map a libvterm colour to a Turbo Vision colour. Default fg/bg fall back to the
// Turbo window colours above; indexed colours go through the xterm-256 palette
// (Turbo Vision renders indices 0..15 as the BIOS colours); RGB colours pass
// straight through (Turbo Vision quantises if the backend can't show truecolour).
TColorDesired mapColor(const VTermColor &c, bool isFg) noexcept
{
    if (isFg && VTERM_COLOR_IS_DEFAULT_FG(&c))
        return kTermDefaultFg;
    if (!isFg && VTERM_COLOR_IS_DEFAULT_BG(&c))
        return kTermDefaultBg;
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
    else if (prop == VTERM_PROP_TITLE)
    {
        VTermStringFragment f = val->string;
        v->onTitleFragment(std::string_view(f.str, f.len), f.initial, f.final);
    }
    return 1;
}

int cbBell(void *) noexcept { return 1; } // no audible bell

const VTermScreenCallbacks screenCbs = {
    /* damage      */ nullptr,
    /* moverect    */ nullptr,
    /* movecursor  */ cbMoveCursor,
    /* settermprop */ cbSetTermProp,
    /* bell        */ cbBell,
    /* resize      */ nullptr,
    /* sb_pushline */ nullptr, // no scrollback retention in this version
    /* sb_popline  */ nullptr,
    /* sb_clear    */ nullptr,
    /* sb_pushline4*/ nullptr,
};

} // namespace

// ---------------------------------------------------------------------------
// TerminalView
// ---------------------------------------------------------------------------

TerminalView::TerminalView(const TRect &bounds) noexcept :
    TView(bounds)
{
    growMode = gfGrowHiX | gfGrowHiY;
    options |= ofSelectable | ofFirstClick;
    eventMask |= evKeyboard | evCommand | evBroadcast;
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

    // Resolve the shell: configured override, else $SHELL, else a sane default.
    std::string shellCmd;
    if (auto *app = (TurboApp *) TProgram::application)
        shellCmd = app->settings.terminalShell;
    if (shellCmd.empty())
    {
        const char *s = getenv("SHELL");
        shellCmd = (s && *s) ? s : "/bin/sh";
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
    pty.terminate();               // closes the master fd -> reader read() ends
    if (reader.joinable())
        reader.join();
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
        drawView();
        updateCursor();
    }
}

void TerminalView::draw()
{
    const TColorAttr defAttr = TColorAttr(kTermDefaultFg, kTermDefaultBg);
    int w = size.x, h = size.y;
    for (int y = 0; y < h; ++y)
    {
        TDrawBuffer b;
        b.moveChar(0, ' ', defAttr, (ushort) w); // default-fill, then overlay
        if (screen && y < rows)
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
            int x = 0;
            while (x < w && x < cols)
            {
                VTermScreenCell cell;
                VTermPos pos { y, x };
                if (!vterm_screen_get_cell(screen, pos, &cell))
                {
                    ++x;
                    continue;
                }
                // Right half of a double-width glyph: already covered by the
                // wide character emitted in the cell to its left, so skip it.
                if (cell.chars[0] == (uint32_t) -1)
                {
                    ++x;
                    continue;
                }
                TColorAttr a = mapCellAttr(cell);
                if (inRun && !sameAttr(a, runAttr))
                    flush();
                if (!inRun)
                {
                    runStart = x;
                    runAttr = a;
                    inRun = true;
                }
                appendUtf8(run, cell.chars[0] ? cell.chars[0] : uint32_t(' '));
                ++x;
            }
            flush();
        }
        writeLine(0, y, (ushort) w, 1, b);
    }
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
            // Once the child has exited the view is inert; any key dismisses it.
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
            if (ev.keyDown.controlKeyState & kbPaste)
                handlePaste(ev);
            else if (!sendKey(ev.keyDown))
                return; // not consumed: let it fall through (shouldn't happen)
            clearEvent(ev);
            break;
        case evCommand:
            if ((state & sfFocused) && !childDone.load() &&
                sendCommandKey(ev.message.command))
                clearEvent(ev);
            break;
        case evMouseDown:
            // Clicking focuses the terminal (in-view text selection is not yet
            // supported); swallow so the base view doesn't start a drag-select.
            select();
            clearEvent(ev);
            break;
    }
}

// ---------------------------------------------------------------------------
// TerminalWindow
// ---------------------------------------------------------------------------

TerminalWindow::TerminalWindow(const TRect &bounds) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, "Terminal", wnNoNumber)
{
    options |= ofTileable;
    // Drop the drop-shadow: the terminal is normally docked flush against the
    // file tree, and the shadow would darken the tree's left edge (it renders
    // two columns past the window). The tree and help windows do the same.
    state &= ~sfShadow;
    view = new TerminalView(getExtent().grow(-1, -1));
    insert(view);
}

const char *TerminalWindow::getTitle(short)
{
    return titleBuf.c_str();
}

void TerminalWindow::sizeLimits(TPoint &min, TPoint &max)
{
    TWindow::sizeLimits(min, max);
    min.x = 20;
    min.y = 4;
}

void TerminalWindow::setTermTitle(std::string_view text) noexcept
{
    titleBuf = text.empty() ? "Terminal"
                            : "Terminal: " + std::string(text);
    if (frame)
        frame->drawView();
}
