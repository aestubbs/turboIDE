// turbo::mcp::runBridge — see include/turbo/mcp/bridge.h. Pure POSIX byte
// forwarder for `turboIDE mcp`; no tvision, no JSON. Kept out of unity/PCH.

#include <turbo/mcp/bridge.h>

#include <string>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#endif

namespace turbo {
namespace mcp {

#ifndef _WIN32

namespace {

bool writeAll(int fd, const char *data, size_t len) noexcept
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        off += (size_t) n;
    }
    return true;
}

} // namespace

int runBridge(int argc, const char **argv) noexcept
{
    // Resolve the socket path: --socket <path>, else $TURBOIDE_MCP_SOCKET, else
    // a project-local default.
    std::string path;
    for (int i = 2; i < argc; ++i) // argv[1] is "mcp"
    {
        std::string a = argv[i];
        if (a == "--socket" && i + 1 < argc)
            path = argv[++i];
    }
    if (path.empty())
        if (const char *e = ::getenv("TURBOIDE_MCP_SOCKET"))
            path = e;
    if (path.empty())
        path = ".turbo/mcp.sock";

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        std::fprintf(stderr, "turboIDE mcp: socket path too long: %s\n", path.c_str());
        return 1;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::fprintf(stderr, "turboIDE mcp: socket() failed\n");
        return 1;
    }
    // Retry briefly: the parent may still be binding when the agent launches us.
    int tries = 0;
    while (::connect(fd, (sockaddr *) &addr, sizeof addr) != 0)
    {
        if (++tries > 50) // ~5s
        {
            std::fprintf(stderr, "turboIDE mcp: cannot connect to %s\n", path.c_str());
            ::close(fd);
            return 1;
        }
        ::usleep(100000);
    }

    // Forward bytes both ways until either side closes. Single-threaded select
    // so there are no threads to join on exit.
    char buf[65536];
    bool stdinOpen = true;
    for (;;)
    {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        int maxfd = fd;
        if (stdinOpen)
        {
            FD_SET(STDIN_FILENO, &rf);
            if (STDIN_FILENO > maxfd)
                maxfd = STDIN_FILENO;
        }
        int r = ::select(maxfd + 1, &rf, nullptr, nullptr, nullptr);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (stdinOpen && FD_ISSET(STDIN_FILENO, &rf))
        {
            ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0)
            {
                stdinOpen = false;
                ::shutdown(fd, SHUT_WR); // tell the server our input ended
            }
            else if (!writeAll(fd, buf, (size_t) n))
                break;
        }
        if (FD_ISSET(fd, &rf))
        {
            ssize_t n = ::read(fd, buf, sizeof buf);
            if (n <= 0)
                break; // server closed
            if (!writeAll(STDOUT_FILENO, buf, (size_t) n))
                break;
        }
    }

    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
    return 0;
}

#else // _WIN32

int runBridge(int, const char **) noexcept
{
    return 1; // MCP bridge is POSIX-only for now.
}

#endif

} // namespace mcp
} // namespace turbo
