#ifndef TURBO_TERMINAL_H
#define TURBO_TERMINAL_H

#define Uses_TView
#define Uses_TWindow
#define Uses_TColorAttr
#include <tvision/tv.h>

#include <turbo/pty.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// libvterm types (opaque here; the full C API is used only in terminal.cc).
struct VTerm;
struct VTermScreen;

class TScrollBar;
struct TerminalWindow;

// A terminal emulator view: it runs a child shell on a pseudo-terminal (PTY),
// feeds the shell's output into libvterm (which maintains the screen grid), and
// renders that grid. Keystrokes are encoded by libvterm and written back to the
// PTY. A dedicated reader thread blocks on the PTY; it parks bytes in a buffer
// and wakes the UI loop, which drains and renders them from pump() on the main
// thread (libvterm and Turbo Vision are only ever touched from the main thread).
struct TerminalView : public TView
{
    // One stored scrollback cell: a rendered code point + colour. 'ch' is the
    // libvterm code point (0 = blank; (uint32_t)-1 marks the trailing half of a
    // double-width glyph, skipped when drawing).
    struct SbCell { uint32_t ch; TColorAttr attr; };

    TerminalView(const TRect &bounds) noexcept;
    ~TerminalView();

    void draw() override;
    void changeBounds(const TRect &bounds) override;
    void handleEvent(TEvent &ev) override;
    void setState(ushort aState, Boolean enable) override;

    // Drain any PTY output produced since the last call, feed it to libvterm,
    // and repaint if the screen changed. Called every idle tick by TurboApp.
    void pump() noexcept;

    bool processExited() const noexcept { return childDone.load(); }

    // The owning window wires up the vertical scrollbar after construction.
    void setScrollBar(TScrollBar *sb) noexcept;

    // Callback sinks (invoked from C trampolines in terminal.cc, main thread).
    void onPtyOutput(const char *s, size_t len) noexcept;   // bytes for the PTY
    void onMoveCursor(int row, int col, bool visible) noexcept;
    void onTitleFragment(std::string_view frag, bool initial, bool final) noexcept;
    void onCursorVisible(bool visible) noexcept;
    void pushScrollbackLine(std::vector<SbCell> &&line) noexcept; // line scrolled off
    void onScrollbackClear() noexcept;                            // app cleared history

private:
    // Scroll the view by 'delta' lines into history (+) or toward the live
    // bottom (-); snapToBottom() returns to the live view (offset 0).
    void scrollLines(int delta) noexcept;
    void scrollToBottom() noexcept;
    void updateScrollbar() noexcept;

    void startShell() noexcept;
    void stopShell() noexcept;
    void recomputeSize() noexcept;
    void updateCursor() noexcept;
    bool sendKey(const KeyDownEvent &k) noexcept;     // returns true if consumed
    void sendText(TStringView utf8, ushort mod) noexcept;
    bool sendCommandKey(ushort command) noexcept;     // editor-accelerator -> ctrl byte
    void handlePaste(TEvent &ev) noexcept;

    VTerm *vt {nullptr};
    VTermScreen *screen {nullptr};
    turbo::PtyProcess pty;

    std::thread reader;
    std::mutex mx;                  // guards 'incoming'
    std::string incoming;           // PTY bytes awaiting parse (reader -> main)
    std::atomic<bool> childDone {false};

    int cols {0}, rows {0};
    int curRow {0}, curCol {0};
    bool curVisible {true};
    bool spawnFailed {false};
    bool exitNoticeShown {false};
    bool registered {false};

    std::string titlePending;       // OSC title fragments, until 'final'

    // Scrollback: lines that have scrolled off the top, oldest at the front.
    // 'scrollOffset' is how many lines back the view is scrolled (0 = following
    // the live screen). Capped at scrollbackMax lines.
    static constexpr int scrollbackMax = 10000;
    std::deque<std::vector<SbCell>> scrollback;
    int scrollOffset {0};
    TScrollBar *vScrollBar {nullptr};
};

struct TerminalWindow : public TWindow
{
    TerminalView *view {nullptr};
    std::string titleBuf {"Terminal"};

    TerminalWindow(const TRect &bounds) noexcept;

    const char *getTitle(short) override;
    void sizeLimits(TPoint &min, TPoint &max) override;
    // Update the caption from an OSC window-title sequence ("" restores default).
    void setTermTitle(std::string_view text) noexcept;
};

#endif // TURBO_TERMINAL_H
