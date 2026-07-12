#ifndef TURBO_MCP_TRANSPORT_H
#define TURBO_MCP_TRANSPORT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace turbo {
namespace mcp {

// A minimal MCP server transport over an AF_UNIX stream socket. It listens on a
// socket path and exchanges newline-delimited JSON messages with one connected
// client at a time (the `turboIDE mcp` bridge). All payloads are opaque
// std::strings here -- JSON parsing lives in the app tier -- so this stays free
// of tvision and nlohmann/json.
//
// Threading mirrors turbo::lsp::Client: a background server thread accepts and
// reads (parking whole lines on a mutex-guarded queue), a per-connection writer
// thread drains an outbound queue, and the owner calls pump() from its UI idle
// loop to deliver received messages on the main thread. After enqueuing a
// message the server thread invokes onWake (if set) so the owner can nudge a
// blocked event loop (e.g. TEventQueue::wakeUp).
class SocketServer
{
public:
    // Invoked on the server thread after a message is enqueued. Optional.
    std::function<void()> onWake;

    SocketServer() = default;
    ~SocketServer();
    SocketServer(const SocketServer &) = delete;
    SocketServer &operator=(const SocketServer &) = delete;

    // Bind + listen on 'socketPath' and start accepting. Returns false if the
    // path is unusable, already owned by a live server, or binding fails.
    bool start(const std::string &socketPath) noexcept;

    // Deliver each message received since the last call to handler(connId, msg)
    // on the calling (main) thread. 'connId' identifies the originating client
    // connection (so a reply can be routed back with send()).
    void pump(const std::function<void(uint64_t connId,
                                       const std::string &msg)> &handler) noexcept;

    // Queue 'msg' (a single JSON line, no trailing newline) to connection
    // 'connId'. Dropped if that connection is no longer the current one.
    void send(uint64_t connId, const std::string &msg) noexcept;

    // Stop accepting, drop the connection, join threads, unlink the socket.
    void stop() noexcept;

    bool running() const noexcept { return running_.load(); }

private:
    void serverLoop() noexcept;      // accept + read one client at a time
    void writerLoop(int fd) noexcept; // drain outbound to the current client

    int listenFd {-1};
    std::string path;
    std::atomic<bool> running_ {false};
    std::thread server;

    std::mutex connMx;               // guards connFd / connId
    int connFd {-1};
    uint64_t connId {0};
    uint64_t nextConnId {1};

    std::mutex outMx;                // outbound queue -> writer thread
    std::condition_variable outCv;
    std::queue<std::string> outbound;
    bool outClosed {false};

    std::mutex inMx;                 // inbound queue -> main thread (pump)
    std::queue<std::pair<uint64_t, std::string>> inbound;
};

} // namespace mcp
} // namespace turbo

#endif // TURBO_MCP_TRANSPORT_H
