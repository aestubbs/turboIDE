#ifndef TURBO_DAPMANAGER_H
#define TURBO_DAPMANAGER_H

#ifdef TURBO_ENABLE_DAP

#include <turbo/dap/client.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Orchestrates a single debug session on top of the thin turbo::dap::Client
// transport. Owns the adapter process, drives the DAP handshake
// (initialize -> launch/attach -> configurationDone), and routes adapter events
// to host callbacks that the app wires to the UI. All public methods run on the
// main thread; pump() is called from the application's idle loop.
//
// One session at a time (v1): starting a new session replaces any prior one.
class DapManager
{
public:
    DapManager() noexcept;
    ~DapManager();

    DapManager(const DapManager &) = delete;
    DapManager &operator=(const DapManager &) = delete;

    // --- Host callbacks (wired by TurboApp). All fire on the main thread. ---
    // Ask the event loop to wake so pump() runs promptly (TEventQueue::wakeUp).
    std::function<void()> onWake;
    // Adapter 'output' event: category is "stdout"/"stderr"/"console"/etc.
    std::function<void(const std::string &category, const std::string &text)> onOutput;
    // Execution stopped (breakpoint/step/entry). file/line may be empty/0 until
    // the stack frame is resolved (M2). 'reason' is the DAP stop reason.
    std::function<void(const std::string &file, int line, const std::string &reason)> onStopped;
    // Execution resumed.
    std::function<void()> onContinued;
    // Session became active / ended (for status line, panels, markers).
    std::function<void(bool active)> onSessionState;

    // The workspace root, used as the default cwd for launched programs.
    void setRootPath(const char *path) noexcept;

    bool sessionActive() const noexcept { return active; }

    // Start a debug session for 'languageId' (e.g. "python", "cpp"), debugging
    // 'program'. The adapter and request kind (launch/attach) are resolved from
    // config/defaults. Returns false if no adapter is available or spawn fails.
    bool start(const std::string &languageId, const std::string &program) noexcept;

    // Toggle a breakpoint at 'file':'line' (0-based). Returns whether the line
    // now has a breakpoint (so the caller can update the editor gutter). The
    // breakpoint set persists across sessions and is (re)sent to the adapter on
    // the next session's 'initialized', or immediately if a session is live.
    bool toggleBreakpoint(const std::string &file, int line) noexcept;

    // Session controls. No-ops when no session is active.
    void terminate() noexcept;       // stop debugging (disconnect + teardown)
    void continueExec() noexcept;    // resume
    void stepOver() noexcept;        // DAP 'next'
    void stepIn() noexcept;          // DAP 'stepIn'
    void stepOut() noexcept;         // DAP 'stepOut'
    void pause() noexcept;           // interrupt a running debuggee

    // Deliver queued adapter messages. Cheap to call every idle tick.
    void pump() noexcept;
    // End any session and tear down. Call on app shutdown.
    void shutdown() noexcept;

private:
    // A resolved adapter launch specification.
    struct AdapterSpec
    {
        std::string languageId;          // e.g. "python", "cpp"
        std::string command;             // adapter executable (on PATH)
        std::vector<std::string> args;   // adapter process arguments
        std::string request {"launch"};  // "launch" | "attach"
        bool stopOnEntry {false};
        std::string host {"127.0.0.1"};  // attach
        int port {0};                    // attach
        bool valid() const noexcept { return !command.empty(); }
    };

    AdapterSpec resolveAdapter(const std::string &languageId) noexcept;
    void sendLaunchOrAttach() noexcept;
    void sendBreakpoints(const std::string &file) noexcept; // setBreakpoints for one file
    void sendAllBreakpoints() noexcept;                     // replay every file (on 'initialized')
    void onEvent(const std::string &event, const turbo::dap::Json &body) noexcept;
    void onReverseRequest(int seq, const std::string &command,
                          const turbo::dap::Json &arguments) noexcept;
    void endSession() noexcept;

    std::string rootPath;

    std::unique_ptr<turbo::dap::Client> client;
    bool active {false};
    turbo::dap::Json adapterCaps;
    AdapterSpec pending;               // spec for the in-flight launch/attach
    std::string pendingProgram;
    int currentThreadId {0};           // from the most recent 'stopped' event
    // Breakpoints: file -> set of 0-based lines. Persists across sessions.
    std::map<std::string, std::set<int>> breakpoints;
};

#else // !TURBO_ENABLE_DAP

#include <functional>
#include <string>

// Stub so the rest of the app compiles unchanged when DAP is disabled.
class DapManager
{
public:
    std::function<void()> onWake;
    std::function<void(const std::string &, const std::string &)> onOutput;
    std::function<void(const std::string &, int, const std::string &)> onStopped;
    std::function<void()> onContinued;
    std::function<void(bool)> onSessionState;
    void setRootPath(const char *) noexcept {}
    bool sessionActive() const noexcept { return false; }
    bool start(const std::string &, const std::string &) noexcept { return false; }
    bool toggleBreakpoint(const std::string &, int) noexcept { return false; }
    void terminate() noexcept {}
    void continueExec() noexcept {}
    void stepOver() noexcept {}
    void stepIn() noexcept {}
    void stepOut() noexcept {}
    void pause() noexcept {}
    void pump() noexcept {}
    void shutdown() noexcept {}
};

#endif // TURBO_ENABLE_DAP

#endif // TURBO_DAPMANAGER_H
