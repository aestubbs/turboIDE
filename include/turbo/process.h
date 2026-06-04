#ifndef TURBO_PROCESS_H
#define TURBO_PROCESS_H

#include <string>
#include <vector>
#include <cstddef>

namespace turbo {

// A child process with its stdin/stdout redirected to pipes and its stderr
// sent to the null device (so a child's logging cannot corrupt the terminal
// UI). Reads and writes are blocking and intended to be driven from dedicated
// reader/writer threads.
//
// Used both for long-lived stream children (language servers, via lsp::Client)
// and for short-lived "run a command and collect its output" children (git,
// via runToEnd / the GitClient).
class Process
{
public:
    Process() = default;
    ~Process();
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;

    // Spawns 'command' (looked up on PATH) with 'args' as additional arguments
    // (argv[0] is set to 'command'). Returns false on failure.
    // If 'cwd' is non-empty, the child runs in that working directory.
    // 'env' entries ("NAME=value") are added to the child's environment.
    bool start(const std::string &command, const std::vector<std::string> &args,
               const std::string &cwd = {},
               const std::vector<std::string> &env = {});

    // Blocking read from the child's stdout. Returns bytes read, 0 on EOF,
    // or -1 on error.
    long readStdout(char *buf, size_t len);
    // Blocking write of all bytes to the child's stdin. Returns false on error.
    bool writeStdin(const char *buf, size_t len);

    void closeStdin();
    void terminate();      // Ask the child to exit, then force-kill if needed.
    bool running();
    // Reap the child and return its exit code (blocking). Safe to call once
    // readStdout() has returned EOF (this is what runToEnd does internally).
    // Returns -1 if the process was never started or has already been reaped.
    int wait();

    // Convenience for short-lived commands: spawn, read stdout to EOF into
    // 'output', wait for exit, and return the exit code (or -1 if the process
    // could not be started). Blocking; call from a worker thread.
    static int runToEnd( const std::string &command,
                         const std::vector<std::string> &args,
                         std::string &output,
                         const std::string &cwd = {},
                         const std::vector<std::string> &env = {} );

private:
#ifdef _WIN32
    void *hProcess {nullptr};
    void *hStdinWrite {nullptr};
    void *hStdoutRead {nullptr};
#else
    int pid {-1};
    int stdinFd {-1};
    int stdoutFd {-1};
#endif
    int waitExit(); // blocking wait; returns exit code (or -1).
};

} // namespace turbo

#endif // TURBO_PROCESS_H
