#include <turbo/pty.h>

#ifdef _WIN32

// Windows backend: ConPTY (CreatePseudoConsole, Windows 10 1809+). ConPTY hands
// the child a real console whose output is translated to VT/ANSI on a pipe --
// exactly what libvterm consumes -- and whose input pipe accepts the VT/key
// encodings libvterm produces, so the cross-platform TerminalView reader-thread
// model works unchanged. This TU is built standalone (no unity batch / PCH), so
// raising the API floor and including <windows.h> here is self-contained.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00     // expose HPCON / CreatePseudoConsole declarations
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace turbo {

namespace {

// ConPTY is resolved at runtime so the executable still loads on pre-1809
// Windows (there start() simply returns false and the caller shows a message),
// preserving the project's older-Windows floor for everything else.
struct ConPtyApi
{
    HRESULT (WINAPI *Create)(COORD, HANDLE, HANDLE, DWORD, HPCON *) {nullptr};
    HRESULT (WINAPI *Resize)(HPCON, COORD) {nullptr};
    void    (WINAPI *Close)(HPCON) {nullptr};
    bool ok() const { return Create && Resize && Close; }
};

const ConPtyApi &conpty()
{
    static const ConPtyApi api = [] {
        ConPtyApi a;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        {
            a.Create = (decltype(a.Create)) (void *) GetProcAddress(k, "CreatePseudoConsole");
            a.Resize = (decltype(a.Resize)) (void *) GetProcAddress(k, "ResizePseudoConsole");
            a.Close  = (decltype(a.Close))  (void *) GetProcAddress(k, "ClosePseudoConsole");
        }
        return a;
    }();
    return api;
}

// UTF-8 (used everywhere in turbo) -> UTF-16 for the wide Win32 APIs.
std::wstring widen(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w((size_t) n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}

// Quote one argv element per the CommandLineToArgvW rules so paths/args with
// spaces or quotes round-trip (the standard MSVCRT quoting algorithm).
void appendQuoted(std::wstring &out, const std::wstring &arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
    {
        out += arg;
        return;
    }
    out += L'"';
    for (auto it = arg.begin(); ; ++it)
    {
        unsigned slashes = 0;
        while (it != arg.end() && *it == L'\\') { ++it; ++slashes; }
        if (it == arg.end()) { out.append(slashes * 2, L'\\'); break; }
        if (*it == L'"')     { out.append(slashes * 2 + 1, L'\\'); out += L'"'; }
        else                 { out.append(slashes, L'\\'); out += *it; }
    }
    out += L'"';
}

} // namespace

bool PtyProcess::start(const std::string &command,
                       const std::vector<std::string> &args,
                       const std::string &cwd, int cols, int rows)
{
    if (!conpty().ok())
        return false;

    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0))
        return false;
    if (!CreatePipe(&outRead, &outWrite, nullptr, 0))
    {
        CloseHandle(inRead); CloseHandle(inWrite);
        return false;
    }

    COORD size;
    size.X = (SHORT) (cols > 0 ? cols : 80);
    size.Y = (SHORT) (rows > 0 ? rows : 24);
    HPCON pc = nullptr;
    HRESULT hr = conpty().Create(size, inRead, outWrite, 0, &pc);
    // The pseudoconsole duplicated the ends it owns; we keep only our sides.
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr))
    {
        CloseHandle(inWrite); CloseHandle(outRead);
        return false;
    }

    // STARTUPINFOEX carrying the pseudoconsole attribute.
    STARTUPINFOEXW si {};
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)
        HeapAlloc(GetProcessHeap(), 0, attrSize);
    // Track initialization separately: DeleteProcThreadAttributeList must be
    // called iff the list was successfully initialized -- never on an allocated-
    // but-uninitialized list, and it must not be skipped when Update fails.
    bool listInit = si.lpAttributeList &&
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize);
    bool attrOk = listInit &&
        UpdateProcThreadAttribute(si.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc), nullptr, nullptr);
    if (!attrOk)
    {
        if (listInit) DeleteProcThreadAttributeList(si.lpAttributeList);
        if (si.lpAttributeList) HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        conpty().Close(pc);
        CloseHandle(inWrite); CloseHandle(outRead);
        return false;
    }

    std::wstring cmdline;
    appendQuoted(cmdline, widen(command));
    for (auto &a : args) { cmdline += L' '; appendQuoted(cmdline, widen(a)); }
    std::wstring wcwd = widen(cwd);

    PROCESS_INFORMATION pi {};
    BOOL ok = CreateProcessW(
        nullptr, &cmdline[0], nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT, nullptr,
        wcwd.empty() ? nullptr : wcwd.c_str(),
        &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

    if (!ok)
    {
        conpty().Close(pc);
        CloseHandle(inWrite); CloseHandle(outRead);
        return false;
    }

    CloseHandle(pi.hThread);    // not needed; the process handle drives lifetime
    hPC = pc;
    hInWrite = inWrite;
    hOutRead = outRead;
    hProcess = pi.hProcess;
    return true;
}

long PtyProcess::read(char *buf, size_t len)
{
    if (!hOutRead)
        return -1;
    DWORD n = 0;
    if (!ReadFile((HANDLE) hOutRead, buf, (DWORD) len, &n, nullptr))
    {
        // Broken pipe == the child closed its console / exited; aborted == our
        // own terminate() cancelled the blocked read. Both are end-of-stream.
        DWORD e = GetLastError();
        return (e == ERROR_BROKEN_PIPE || e == ERROR_OPERATION_ABORTED) ? 0 : -1;
    }
    return (long) n;
}

bool PtyProcess::write(const char *buf, size_t len)
{
    if (!hInWrite)
        return false;
    size_t off = 0;
    while (off < len)
    {
        DWORD n = 0;
        if (!WriteFile((HANDLE) hInWrite, buf + off, (DWORD) (len - off), &n, nullptr))
            return false;
        off += n;
    }
    return true;
}

void PtyProcess::resize(int cols, int rows)
{
    if (!hPC)
        return;
    COORD size;
    size.X = (SHORT) (cols > 0 ? cols : 1);
    size.Y = (SHORT) (rows > 0 ? rows : 1);
    conpty().Resize((HPCON) hPC, size);
}

void PtyProcess::terminate()
{
    // Close the pseudoconsole FIRST: this hangs up the child and breaks the
    // output pipe, which makes the reader thread's blocked ReadFile return a
    // broken pipe -> EOF. Closing the read end before this can deadlock ConPTY,
    // so the ordering matters.
    if (hPC)      { conpty().Close((HPCON) hPC); hPC = nullptr; }
    if (hInWrite) { CloseHandle((HANDLE) hInWrite); hInWrite = nullptr; }
    if (hProcess)
    {
        if (WaitForSingleObject((HANDLE) hProcess, 2000) != WAIT_OBJECT_0)
            TerminateProcess((HANDLE) hProcess, 1);
        CloseHandle((HANDLE) hProcess);
        hProcess = nullptr;
    }
    // Force-unblock the reader even if conhost did NOT drop the output pipe's
    // write end (a documented ConPTY quirk: a lingering conhost, or a child we
    // had to TerminateProcess, can leave the pipe open so ReadFile never returns
    // and stopShell()'s join() would deadlock). CancelIoEx aborts the in-flight
    // synchronous read regardless of which thread issued it. We do NOT close
    // hOutRead here -- the reader may still be inside ReadFile on it, and closing
    // a handle in concurrent use is a Win32 handle-recycling race. closeRead()
    // closes it, after the reader has been joined.
    if (hOutRead)
        CancelIoEx((HANDLE) hOutRead, nullptr);
}

void PtyProcess::closeRead()
{
    if (hOutRead) { CloseHandle((HANDLE) hOutRead); hOutRead = nullptr; }
}

bool PtyProcess::running()
{
    if (!hProcess)
        return false;
    return WaitForSingleObject((HANDLE) hProcess, 0) == WAIT_TIMEOUT;
}

// The destructor has no concurrent reader thread, so closing the read handle
// straight after terminate() is safe here.
PtyProcess::~PtyProcess() { terminate(); closeRead(); }

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
    envStrings.emplace_back("TERM_PROGRAM=turboIDE");
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

// No-op on POSIX: terminate() already close()d the master fd, which is the
// race-safe way to unblock a concurrent read() here. Exists so the consumer can
// call it unconditionally (it is load-bearing only on Windows).
void PtyProcess::closeRead() {}

PtyProcess::~PtyProcess() { terminate(); }

} // namespace turbo

#endif // _WIN32
