#ifndef TURBO_COMMANDRUNNER_H
#define TURBO_COMMANDRUNNER_H

#include <turbo/process.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

// Runs a shell command and streams its combined stdout+stderr line-by-line.
// Mirrors TerminalView's reader-thread + pump pattern, but over a plain pipe
// (turbo::Process) instead of a PTY: a dedicated thread does the blocking reads
// and parks bytes; the main loop drains them in pump() and emits whole lines.
//
// All turbo::Process calls happen on the main thread (start/pump/stop); the
// reader thread only calls readStdout(), so the shared pid/fd state is never
// touched from two threads at once.
class CommandRunner
{
public:
    // Called on the main thread (from pump) for each complete output line, and
    // once with the exit code when the process finishes on its own.
    std::function<void(std::string_view line)> onLine;
    std::function<void(int exitCode)> onExit;

    CommandRunner() = default;
    ~CommandRunner();
    CommandRunner(const CommandRunner &) = delete;
    CommandRunner &operator=(const CommandRunner &) = delete;

    // Start 'command' (an arbitrary shell command) in directory 'cwd'. Any
    // previous run is stopped first. Returns false if the process won't spawn.
    bool start(const std::string &command, const std::string &cwd) noexcept;

    // Drain buffered output and emit lines / the exit callback. Call each idle.
    void pump() noexcept;

    // Kill the process (if any) and join the reader thread. No onExit is fired.
    void stop() noexcept;

    bool running() const noexcept { return running_; }

private:
    turbo::Process proc;
    std::thread reader;
    std::mutex mx;
    std::string incoming;            // reader thread -> main thread (guarded by mx)
    std::string lineBuf;             // main-thread accumulator for partial lines
    std::atomic<bool> eof_ {false};
    bool reaped_ {true};
    bool running_ {false};
};

#endif // TURBO_COMMANDRUNNER_H
