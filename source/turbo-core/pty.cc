#include <turbo/pty.h>

#ifdef _WIN32

// No ConPTY backend yet: report failure so callers can surface a clear message
// instead of silently doing nothing.
namespace turbo {

bool PtyProcess::start(const std::string &, const std::vector<std::string> &,
                       const std::string &, int, int) { return false; }
long PtyProcess::read(char *, size_t) { return -1; }
bool PtyProcess::write(const char *, size_t) { return false; }
void PtyProcess::resize(int, int) {}
void PtyProcess::terminate() {}
bool PtyProcess::running() { return false; }
PtyProcess::~PtyProcess() {}

} // namespace turbo

#else // POSIX

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__)
#include <util.h>          // forkpty
#elif defined(__linux__)
#include <pty.h>           // forkpty (links against -lutil)
#else
#include <pty.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>

extern char **environ;

namespace turbo {

bool PtyProcess::start(const std::string &command,
                       const std::vector<std::string> &args,
                       const std::string &cwd, int cols, int rows)
{
    struct winsize ws {};
    ws.ws_col = (unsigned short) (cols > 0 ? cols : 80);
    ws.ws_row = (unsigned short) (rows > 0 ? rows : 24);

    // Build argv and the child environment in the PARENT, before fork(). After
    // a fork in a multithreaded process only async-signal-safe work is safe
    // (another thread may hold the malloc lock), so the child below does no
    // allocation -- it only assigns 'environ', chdir()s and exec()s.
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(command.c_str()));
    for (auto &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    // Copy our environment, overriding TERM/COLORTERM so the child advertises a
    // capable terminal that our emulator understands.
    std::vector<std::string> envStrings;
    auto skip = [] (const char *e, const char *name) {
        size_t n = strlen(name);
        return strncmp(e, name, n) == 0 && e[n] == '=';
    };
    for (char **e = environ; e && *e; ++e)
        if (!skip(*e, "TERM") && !skip(*e, "COLORTERM") && !skip(*e, "TERM_PROGRAM"))
            envStrings.emplace_back(*e);
    envStrings.emplace_back("TERM=xterm-256color");
    envStrings.emplace_back("COLORTERM=truecolor");
    envStrings.emplace_back("TERM_PROGRAM=turbo");
    std::vector<char *> envp;
    envp.reserve(envStrings.size() + 1);
    for (auto &s : envStrings)
        envp.push_back(const_cast<char *>(s.c_str()));
    envp.push_back(nullptr);

    int master = -1;
    pid_t child = forkpty(&master, nullptr, nullptr, &ws);
    if (child < 0)
        return false;
    if (child == 0)
    {
        // --- Child: forkpty has already set up the controlling tty and dup2'd
        // the slave onto fd 0/1/2. Keep to async-signal-safe operations only.
        if (!cwd.empty())
            (void) ::chdir(cwd.c_str());
        environ = envp.data();
        ::execvp(command.c_str(), argv.data());
        _exit(127); // exec failed
    }

    pid = child;
    masterFd = master;
    return true;
}

long PtyProcess::read(char *buf, size_t len)
{
    if (masterFd < 0)
        return -1;
    for (;;)
    {
        ssize_t n = ::read(masterFd, buf, len);
        if (n < 0 && errno == EINTR)
            continue;
        return (long) n;
    }
}

bool PtyProcess::write(const char *buf, size_t len)
{
    if (masterFd < 0)
        return false;
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = ::write(masterFd, buf + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        written += (size_t) n;
    }
    return true;
}

void PtyProcess::resize(int cols, int rows)
{
    if (masterFd < 0)
        return;
    struct winsize ws {};
    ws.ws_col = (unsigned short) (cols > 0 ? cols : 1);
    ws.ws_row = (unsigned short) (rows > 0 ? rows : 1);
    ::ioctl(masterFd, TIOCSWINSZ, &ws);
}

void PtyProcess::terminate()
{
    if (pid > 0)
    {
        // SIGHUP is what a real terminal sends when its window closes; most
        // shells exit cleanly on it. Escalate to SIGKILL if the child lingers.
        ::kill(pid, SIGHUP);
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
            int status;
            ::waitpid(pid, &status, 0); // final blocking reap; avoid a zombie
            pid = -1;
        }
    }
    if (masterFd >= 0)
    {
        ::close(masterFd); // unblocks the reader thread's read() with EOF/error
        masterFd = -1;
    }
}

bool PtyProcess::running()
{
    if (pid <= 0)
        return false;
    int status;
    pid_t r = ::waitpid(pid, &status, WNOHANG);
    return r == 0;
}

PtyProcess::~PtyProcess() { terminate(); }

} // namespace turbo

#endif // _WIN32
