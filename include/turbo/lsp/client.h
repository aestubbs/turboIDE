#ifndef TURBO_LSP_CLIENT_H
#define TURBO_LSP_CLIENT_H

#ifdef TURBO_ENABLE_LSP

#include <turbo/lsp/process.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace turbo {
namespace lsp {

using Json = nlohmann::json;

// How the server expects line positions to count columns.
enum class PositionEncoding { UTF16, UTF8 };

// A JSON-RPC client driving a single language-server process over stdio.
//
// Threading model:
//  * A reader thread parses framed messages off the server's stdout and pushes
//    them onto a mutex-guarded queue.
//  * A writer thread drains a mutex-guarded outbound queue to the server stdin.
//  * Everything else (request/notify bookkeeping, response dispatch) happens on
//    the main thread. Call pump() from the application's idle loop to deliver
//    server messages and responses on the main thread.
class Client
{
public:
    // result is the JSON-RPC "result"; on error, 'error' points to the
    // JSON-RPC "error" object and 'result' is null.
    using ResponseHandler = std::function<void(const Json &result, const Json *error)>;
    // Receives server-initiated notifications and requests (e.g. diagnostics).
    using ServerMessageHandler = std::function<void(const Json &message)>;

    Client(std::string languageId, std::string command, std::vector<std::string> args) noexcept;
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Server-specific options sent as 'initializationOptions' in the
    // 'initialize' request. Set before start(); null (the default) omits them.
    // Some servers (e.g. intelephense, which needs a 'storagePath') will not
    // function without these.
    Json initializationOptions;

    // Spawns the server and sends the 'initialize' request. Returns false if the
    // process could not be started.
    bool start(const std::string &rootUri) noexcept;
    // Best-effort shutdown handshake and teardown of threads/process.
    void stop() noexcept;

    bool alive() const noexcept { return running.load(); }
    bool ready() const noexcept { return isReady; }
    PositionEncoding positionEncoding() const noexcept { return encoding; }
    const std::string &languageId() const noexcept { return langId; }
    const Json &serverCapabilities() const noexcept { return capabilities; }

    // Send a request. 'handler' runs on the main thread during pump() when the
    // matching response arrives. Requests issued before initialization completes
    // are queued and flushed afterwards.
    void request(const std::string &method, Json params, ResponseHandler handler) noexcept;
    // Send a notification (same queuing-until-ready behaviour).
    void notify(const std::string &method, Json params) noexcept;

    // Drain inbound messages on the main thread. 'onServerMessage' receives any
    // server-originated notification/request (responses are routed to their
    // stored handler instead).
    void pump(const ServerMessageHandler &onServerMessage) noexcept;

private:
    void readerLoop() noexcept;
    void writerLoop() noexcept;
    void enqueueOut(const Json &msg) noexcept;        // frame + hand to writer
    void dispatch(const Json &msg, const ServerMessageHandler &onServerMessage) noexcept;
    void onInitializeResult(const Json &result) noexcept;
    void flushPending() noexcept;
    Json makeRequest(int id, const std::string &method, const Json &params) noexcept;
    Json makeNotification(const std::string &method, const Json &params) noexcept;

    std::string langId;
    std::string command;
    std::vector<std::string> args;

    Process proc;
    std::thread reader;
    std::thread writer;
    std::atomic<bool> running {false};

    // Main-thread-only state.
    bool isReady {false};
    PositionEncoding encoding {PositionEncoding::UTF16};
    Json capabilities;
    int nextId {1};
    std::unordered_map<int, ResponseHandler> handlers;
    std::vector<Json> pendingOutbound; // queued until 'initialize' completes

    // Reader thread -> main thread.
    std::mutex inMx;
    std::queue<Json> inbound;

    // Main thread -> writer thread.
    std::mutex outMx;
    std::condition_variable outCv;
    std::queue<std::string> outbound;  // pre-framed
    bool outClosed {false};
};

} // namespace lsp
} // namespace turbo

#endif // TURBO_ENABLE_LSP
#endif // TURBO_LSP_CLIENT_H
