#ifndef TURBO_PTY_H
#define TURBO_PTY_H

#include <string>
#include <vector>
#include <cstddef>

namespace turbo {

// A child process attached to a pseudo-terminal (PTY). Unlike turbo::Process
// (which wires stdin/stdout to plain pipes and discards stderr), a PtyProcess
// gives the child a real controlling terminal, so interactive programs -- a
// shell, vim, less, top -- run normally: line editing, job control, terminal
// resize (SIGWINCH) and ANSI/VT output all work.
//
// The master side is a single bidirectional fd: read() returns whatever the
// child writes to its terminal, write() feeds the child's terminal input.
// Reads block, so drive them from a dedicated reader thread (see TerminalView).
//
// Two implementations are provided: POSIX (forkpty) and Windows (ConPTY,
// Windows 10 1809+). On older Windows, start() returns false and the caller is
// expected to report that the terminal is not available.
class PtyProcess
{
public:
    PtyProcess() = default;
    ~PtyProcess();
    PtyProcess(const PtyProcess &) = delete;
    PtyProcess &operator=(const PtyProcess &) = delete;

    // Spawn 'command' (looked up on PATH) with 'args' as additional arguments
    // (argv[0] is set to 'command') on a fresh PTY sized 'cols' x 'rows'. If
    // 'cwd' is non-empty the child starts there. The child's TERM is set to
    // "xterm-256color". Returns false on failure (or always, on Windows).
    bool start(const std::string &command, const std::vector<std::string> &args,
               const std::string &cwd, int cols, int rows);

    // Blocking read of terminal output. Returns bytes read, 0 on EOF (the child
    // closed its terminal / exited), or -1 on error.
    long read(char *buf, size_t len);
    // Write terminal input to the child. Returns false on error.
    bool write(const char *buf, size_t len);

    // Tell the kernel the terminal was resized; this also delivers SIGWINCH to
    // the child's foreground process group so it can redraw.
    void resize(int cols, int rows);

    void terminate();   // Hang up the child, then force-kill if needed.
    // Close the read side of the terminal. On Windows this MUST be called only
    // after the reader thread has been joined (closing the handle while a read
    // is in flight is a Win32 handle-recycling race); terminate() leaves it open
    // and merely unblocks the reader. On POSIX it is a no-op (terminate() already
    // closed the master fd, which is the race-safe way to unblock read() there).
    void closeRead();
    bool running();

private:
#ifdef _WIN32
    // Win32/ConPTY state, stored as void* so this header needn't include
    // <windows.h> (HPCON and HANDLE are both pointer types).
    void *hPC {nullptr};        // HPCON pseudoconsole
    void *hInWrite {nullptr};   // write end of the child's console input
    void *hOutRead {nullptr};   // read end of the child's console output
    void *hProcess {nullptr};   // child process handle
#else
    int pid {-1};
    int masterFd {-1};
#endif
};

} // namespace turbo

#endif // TURBO_PTY_H
