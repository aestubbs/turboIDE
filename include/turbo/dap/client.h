#ifndef TURBO_DAP_CLIENT_H
#define TURBO_DAP_CLIENT_H

#ifdef TURBO_ENABLE_DAP

#include <turbo/process.h>

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
namespace dap {

using Json = nlohmann::json;

// A Debug Adapter Protocol client driving a single debug-adapter process over
// stdio. DAP uses the same Content-Length base-protocol framing as LSP, but a
// different message envelope: {seq, type:"request"|"response"|"event", ...}
// instead of JSON-RPC 2.0.
//
// This class is a *thin transport*. It sends requests and routes the three
// inbound message kinds to callbacks -- responses to the per-request handler,
// adapter-originated events to onEvent, adapter->client reverse requests to
// onReverseRequest. The DAP handshake sequencing (initialize -> launch/attach
// -> configurationDone) lives one layer up, in DapManager.
//
// Threading model mirrors turbo::lsp::Client: a reader thread parses framed
// messages off the adapter's stdout onto a mutex-guarded queue; a writer thread
// drains a mutex-guarded outbound queue to stdin; everything else (seq/handler
// bookkeeping, dispatch) runs on the main thread via pump(). Unlike the LSP
// client, a wake callback (setWake) is fired from the reader thread so an
// asynchronous event -- notably 'stopped' at a breakpoint, while the user is
// idle -- reaches the UI promptly instead of on the next keystroke.
class Client
{
public:
    // 'success' is the DAP response success flag; on failure 'message' holds the
    // adapter's short error text. 'body' is the response body (may be null).
    using ResponseHandler = std::function<void(bool success, const Json &body, const std::string &message)>;
    // Adapter-originated event, e.g. 'stopped', 'output', 'terminated'.
    using EventHandler = std::function<void(const std::string &event, const Json &body)>;
    // Adapter->client reverse request, e.g. 'runInTerminal', 'startDebugging'.
    // The handler must reply via respondToReverseRequest(seq, command, ...).
    using ReverseRequestHandler = std::function<void(int seq, const std::string &command, const Json &arguments)>;

    Client(std::string command, std::vector<std::string> args,
           std::string cwd = {}, std::vector<std::string> env = {}) noexcept;
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Fired (from the reader thread) when inbound messages become available, so
    // the UI event loop can be woken to pump() promptly. Set before start().
    void setWake(std::function<void()> wake) noexcept { onWake = std::move(wake); }

    // Spawns the adapter and starts the reader/writer threads. Returns false if
    // the process could not be started.
    bool start() noexcept;
    // Best-effort 'disconnect' then teardown of threads/process.
    void stop() noexcept;

    bool alive() const noexcept { return running.load(); }

    // Send a DAP request. 'handler' (if set) runs on the main thread during
    // pump() when the matching response arrives. A null 'arguments' is omitted.
    void sendRequest(const std::string &command, Json arguments, ResponseHandler handler = {}) noexcept;
    // Reply to an adapter->client reverse request delivered via onReverseRequest.
    void respondToReverseRequest(int requestSeq, const std::string &command,
                                 bool success, Json body = {}, const std::string &message = {}) noexcept;

    // Drain inbound messages on the main thread. Events go to 'onEvent', reverse
    // requests to 'onReverseRequest'; responses are routed to their stored
    // handler regardless.
    void pump(const EventHandler &onEvent, const ReverseRequestHandler &onReverseRequest) noexcept;

private:
    void readerLoop() noexcept;
    void writerLoop() noexcept;
    void enqueueOut(const Json &msg) noexcept;
    void dispatch(const Json &msg, const EventHandler &onEvent,
                  const ReverseRequestHandler &onReverseRequest) noexcept;

    std::string command;
    std::vector<std::string> args;
    std::string cwd;
    std::vector<std::string> env;

    turbo::Process proc;
    std::thread reader;
    std::thread writer;
    std::atomic<bool> running {false};
    std::function<void()> onWake;

    // Main-thread-only state.
    int nextSeq {1};
    std::unordered_map<int, ResponseHandler> handlers; // request seq -> handler

    // Reader thread -> main thread.
    std::mutex inMx;
    std::queue<Json> inbound;

    // Main thread -> writer thread.
    std::mutex outMx;
    std::condition_variable outCv;
    std::queue<std::string> outbound; // pre-framed
    bool outClosed {false};
};

} // namespace dap
} // namespace turbo

#endif // TURBO_ENABLE_DAP
#endif // TURBO_DAP_CLIENT_H
