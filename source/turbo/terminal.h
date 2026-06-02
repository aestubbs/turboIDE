#ifndef TURBO_TERMINAL_H
#define TURBO_TERMINAL_H

#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include <turbo/pty.h>

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

// libvterm types (opaque here; the full C API is used only in terminal.cc).
struct VTerm;
struct VTermScreen;

struct TerminalWindow;

// A terminal emulator view: it runs a child shell on a pseudo-terminal (PTY),
// feeds the shell's output into libvterm (which maintains the screen grid), and
// renders that grid. Keystrokes are encoded by libvterm and written back to the
// PTY. A dedicated reader thread blocks on the PTY; it parks bytes in a buffer
// and wakes the UI loop, which drains and renders them from pump() on the main
// thread (libvterm and Turbo Vision are only ever touched from the main thread).
struct TerminalView : public TView
{
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

    // Callback sinks (invoked from C trampolines in terminal.cc, main thread).
    void onPtyOutput(const char *s, size_t len) noexcept;   // bytes for the PTY
    void onMoveCursor(int row, int col, bool visible) noexcept;
    void onTitleFragment(std::string_view frag, bool initial, bool final) noexcept;
    void onCursorVisible(bool visible) noexcept;

private:
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
