#include <turbo/lsp/process.h>

#ifdef TURBO_ENABLE_LSP

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace turbo {
namespace lsp {

bool Process::start(const std::string &command, const std::vector<std::string> &args)
{
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) ||
        !CreatePipe(&outRead, &outWrite, &sa, 0))
        return false;
    // The parent's ends must not be inherited by the child.
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    // Build the command line (command + args), quoting naively.
    std::string cmdline = "\"" + command + "\"";
    for (auto &a : args)
        cmdline += " \"" + a + "\"";

    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = nul;

    PROCESS_INFORMATION pi {};
    std::vector<char> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (nul) CloseHandle(nul);
    if (!ok)
    {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return false;
    }
    CloseHandle(pi.hThread);
    hProcess = pi.hProcess;
    hStdinWrite = inWrite;
    hStdoutRead = outRead;
    return true;
}

long Process::readStdout(char *buf, size_t len)
{
    if (!hStdoutRead) return -1;
    DWORD got = 0;
    if (!ReadFile(hStdoutRead, buf, (DWORD) len, &got, nullptr))
        return 0; // Pipe closed.
    return (long) got;
}

bool Process::writeStdin(const char *buf, size_t len)
{
    if (!hStdinWrite) return false;
    size_t written = 0;
    while (written < len)
    {
        DWORD n = 0;
        if (!WriteFile(hStdinWrite, buf + written, (DWORD) (len - written), &n, nullptr))
            return false;
        written += n;
    }
    return true;
}

void Process::closeStdin()
{
    if (hStdinWrite) { CloseHandle(hStdinWrite); hStdinWrite = nullptr; }
}

void Process::terminate()
{
    closeStdin();
    if (hProcess)
    {
        if (WaitForSingleObject(hProcess, 1000) != WAIT_OBJECT_0)
            TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        hProcess = nullptr;
    }
    if (hStdoutRead) { CloseHandle(hStdoutRead); hStdoutRead = nullptr; }
}

bool Process::running()
{
    if (!hProcess) return false;
    return WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT;
}

Process::~Process() { terminate(); }

} // namespace lsp
} // namespace turbo

#else // POSIX

#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>

extern char **environ;

namespace turbo {
namespace lsp {

bool Process::start(const std::string &command, const std::vector<std::string> &args)
{
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) != 0)
        return false;
    if (pipe(outPipe) != 0)
    {
        ::close(inPipe[0]); ::close(inPipe[1]);
        return false;
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    // Child stdin <- read end of inPipe; child stdout -> write end of outPipe.
    posix_spawn_file_actions_adddup2(&fa, inPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, outPipe[1], STDOUT_FILENO);
    // Silence the server's stderr so it cannot draw over the terminal UI.
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    // Close the inherited pipe fds in the child.
    posix_spawn_file_actions_addclose(&fa, inPipe[0]);
    posix_spawn_file_actions_addclose(&fa, inPipe[1]);
    posix_spawn_file_actions_addclose(&fa, outPipe[0]);
    posix_spawn_file_actions_addclose(&fa, outPipe[1]);

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(command.c_str()));
    for (auto &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    int rc = posix_spawnp(&pid, command.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);

    // Parent keeps the write end of inPipe and the read end of outPipe.
    ::close(inPipe[0]);
    ::close(outPipe[1]);

    if (rc != 0)
    {
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        pid = -1;
        return false;
    }
    stdinFd = inPipe[1];
    stdoutFd = outPipe[0];
    return true;
}

long Process::readStdout(char *buf, size_t len)
{
    if (stdoutFd < 0) return -1;
    for (;;)
    {
        ssize_t n = ::read(stdoutFd, buf, len);
        if (n < 0 && errno == EINTR)
            continue;
        return (long) n;
    }
}

bool Process::writeStdin(const char *buf, size_t len)
{
    if (stdinFd < 0) return false;
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = ::write(stdinFd, buf + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        written += (size_t) n;
    }
    return true;
}

void Process::closeStdin()
{
    if (stdinFd >= 0) { ::close(stdinFd); stdinFd = -1; }
}

void Process::terminate()
{
    closeStdin();
    if (pid > 0)
    {
        ::kill(pid, SIGTERM);
        // Reap, escalating to SIGKILL if the server ignores SIGTERM.
        for (int i = 0; i < 20; ++i)
        {
            int status;
            pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid || r < 0)
            {
                pid = -1;
                break;
            }
            if (i == 10)
                ::kill(pid, SIGKILL);
            usleep(10000); // 10 ms
        }
        if (pid > 0)
        {
            // Final blocking reap to avoid a zombie.
            int status;
            ::waitpid(pid, &status, 0);
            pid = -1;
        }
    }
    if (stdoutFd >= 0) { ::close(stdoutFd); stdoutFd = -1; }
}

bool Process::running()
{
    if (pid <= 0) return false;
    int status;
    pid_t r = ::waitpid(pid, &status, WNOHANG);
    return r == 0;
}

Process::~Process() { terminate(); }

} // namespace lsp
} // namespace turbo

#endif // _WIN32

#endif // TURBO_ENABLE_LSP
