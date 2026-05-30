#ifndef TURBO_LSP_PROCESS_H
#define TURBO_LSP_PROCESS_H

#ifdef TURBO_ENABLE_LSP

#include <string>
#include <vector>
#include <cstddef>

namespace turbo {
namespace lsp {

// A child process with its stdin/stdout redirected to pipes and its stderr
// sent to the null device (so a language server's logging cannot corrupt the
// terminal UI). Reads and writes are blocking and intended to be driven from
// dedicated reader/writer threads.
class Process
{
public:
    Process() = default;
    ~Process();
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;

    // Spawns 'command' (looked up on PATH) with 'args' as additional arguments
    // (argv[0] is set to 'command'). Returns false on failure.
    bool start(const std::string &command, const std::vector<std::string> &args);

    // Blocking read from the child's stdout. Returns bytes read, 0 on EOF,
    // or -1 on error.
    long readStdout(char *buf, size_t len);
    // Blocking write of all bytes to the child's stdin. Returns false on error.
    bool writeStdin(const char *buf, size_t len);

    void closeStdin();
    void terminate();      // Ask the child to exit, then force-kill if needed.
    bool running();

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
};

} // namespace lsp
} // namespace turbo

#endif // TURBO_ENABLE_LSP
#endif // TURBO_LSP_PROCESS_H
