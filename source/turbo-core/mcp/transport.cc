// turbo::mcp::SocketServer — see include/turbo/mcp/transport.h. Pure POSIX
// system code (AF_UNIX sockets + threads); no tvision, no JSON. Kept out of the
// unity batch / PCH, like pty.cc.

#include <turbo/mcp/transport.h>
#include <turbo/mcp/ndjson.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace turbo {
namespace mcp {

#ifndef _WIN32

namespace {

// Write all 'len' bytes to 'fd', retrying short writes and EINTR. Returns false
// on a hard error / hangup.
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

SocketServer::~SocketServer()
{
    stop();
}

bool SocketServer::start(const std::string &socketPath) noexcept
{
    if (running_.load())
        return false;
    path = socketPath;

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
        return false; // path too long for sun_path; caller must shorten it
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    // Stale-socket / ownership handling: if the path exists, probe it. A
    // successful connect means another live server already owns this project's
    // MCP endpoint -- don't double-bind. A failed connect means it is stale.
    if (::access(path.c_str(), F_OK) == 0)
    {
        int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe >= 0)
        {
            bool live = ::connect(probe, (sockaddr *) &addr, sizeof addr) == 0;
            ::close(probe);
            if (live)
                return false; // someone else owns it
        }
        ::unlink(path.c_str()); // stale: remove and rebind
    }

    listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0)
        return false;
    if (::bind(listenFd, (sockaddr *) &addr, sizeof addr) != 0 ||
        ::listen(listenFd, 4) != 0)
    {
        ::close(listenFd);
        listenFd = -1;
        return false;
    }

    running_.store(true);
    server = std::thread(&SocketServer::serverLoop, this);
    return true;
}

void SocketServer::serverLoop() noexcept
{
    while (running_.load())
    {
        int fd = ::accept(listenFd, nullptr, nullptr);
        if (fd < 0)
        {
            if (errno == EINTR)
                continue;
            break; // listenFd closed by stop(), or a hard error
        }

        uint64_t id;
        {
            std::lock_guard<std::mutex> lock(connMx);
            connFd = fd;
            id = connId = nextConnId++;
        }
        {
            std::lock_guard<std::mutex> lock(outMx);
            outClosed = false;
            std::queue<std::string> empty;
            std::swap(outbound, empty); // drop any stale queued output
        }
        std::thread writer(&SocketServer::writerLoop, this, fd);

        NdjsonReader mr;
        char buf[16384];
        while (running_.load())
        {
            ssize_t n = ::read(fd, buf, sizeof buf);
            if (n <= 0)
            {
                if (n < 0 && errno == EINTR)
                    continue;
                break; // EOF or error: client gone
            }
            mr.feed(buf, (size_t) n);
            std::string line;
            while (mr.next(line))
            {
                if (line.empty())
                    continue;
                {
                    std::lock_guard<std::mutex> lock(inMx);
                    inbound.push({id, std::move(line)});
                }
                if (onWake)
                    onWake();
                line.clear();
            }
        }

        // Tear this connection down before looping back to accept().
        {
            std::lock_guard<std::mutex> lock(outMx);
            outClosed = true;
        }
        outCv.notify_all();
        {
            std::lock_guard<std::mutex> lock(connMx);
            connFd = -1;
        }
        if (writer.joinable())
            writer.join();
        ::close(fd);
    }
}

void SocketServer::writerLoop(int fd) noexcept
{
    for (;;)
    {
        std::string msg;
        {
            std::unique_lock<std::mutex> lock(outMx);
            outCv.wait(lock, [this] { return outClosed || !outbound.empty(); });
            if (outbound.empty())
                return; // closed and drained
            msg = std::move(outbound.front());
            outbound.pop();
        }
        msg.push_back('\n');
        if (!writeAll(fd, msg.data(), msg.size()))
            return; // hangup: the read loop will notice EOF too
    }
}

void SocketServer::send(uint64_t aConnId, const std::string &msg) noexcept
{
    {
        std::lock_guard<std::mutex> lock(connMx);
        if (connFd < 0 || aConnId != connId)
            return; // no current connection, or a reply to a dead one
    }
    {
        std::lock_guard<std::mutex> lock(outMx);
        outbound.push(msg);
    }
    outCv.notify_one();
}

void SocketServer::pump(const std::function<void(uint64_t, const std::string &)> &handler) noexcept
{
    std::queue<std::pair<uint64_t, std::string>> local;
    {
        std::lock_guard<std::mutex> lock(inMx);
        std::swap(local, inbound);
    }
    while (!local.empty())
    {
        if (handler)
            handler(local.front().first, local.front().second);
        local.pop();
    }
}

void SocketServer::stop() noexcept
{
    if (!running_.exchange(false))
    {
        if (server.joinable())
            server.join();
        return;
    }
    // Unblock accept() (close the listen socket) and any blocked read() (shut
    // down the current connection).
    if (listenFd >= 0)
    {
        ::shutdown(listenFd, SHUT_RDWR);
        ::close(listenFd);
        listenFd = -1;
    }
    {
        std::lock_guard<std::mutex> lock(connMx);
        if (connFd >= 0)
            ::shutdown(connFd, SHUT_RDWR);
    }
    {
        std::lock_guard<std::mutex> lock(outMx);
        outClosed = true;
    }
    outCv.notify_all();
    if (server.joinable())
        server.join();
    if (!path.empty())
        ::unlink(path.c_str());
}

#else // _WIN32: AF_UNIX server not implemented; MCP is POSIX-only for now.

SocketServer::~SocketServer() {}
bool SocketServer::start(const std::string &) noexcept { return false; }
void SocketServer::serverLoop() noexcept {}
void SocketServer::writerLoop(int) noexcept {}
void SocketServer::send(uint64_t, const std::string &) noexcept {}
void SocketServer::pump(const std::function<void(uint64_t, const std::string &)> &) noexcept {}
void SocketServer::stop() noexcept {}

#endif

} // namespace mcp
} // namespace turbo
