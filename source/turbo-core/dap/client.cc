#include <turbo/dap/client.h>

#ifdef TURBO_ENABLE_DAP

#include <turbo/dap/jsonrpc.h>

namespace turbo {
namespace dap {

Client::Client(std::string aCommand, std::vector<std::string> aArgs,
               std::string aCwd, std::vector<std::string> aEnv) noexcept :
    command(std::move(aCommand)),
    args(std::move(aArgs)),
    cwd(std::move(aCwd)),
    env(std::move(aEnv))
{
}

Client::~Client()
{
    stop();
}

bool Client::start() noexcept
{
    if (!proc.start(command, args, cwd, env))
        return false;
    running.store(true);
    reader = std::thread(&Client::readerLoop, this);
    writer = std::thread(&Client::writerLoop, this);
    return true;
}

void Client::sendRequest(const std::string &command_, Json arguments, ResponseHandler handler) noexcept
{
    int seq = nextSeq++;
    if (handler)
        handlers[seq] = std::move(handler);
    Json msg = {
        {"seq", seq},
        {"type", "request"},
        {"command", command_},
    };
    if (!arguments.is_null())
        msg["arguments"] = std::move(arguments);
    enqueueOut(msg);
}

void Client::respondToReverseRequest(int requestSeq, const std::string &command_,
                                     bool success, Json body, const std::string &message) noexcept
{
    int seq = nextSeq++;
    Json msg = {
        {"seq", seq},
        {"type", "response"},
        {"request_seq", requestSeq},
        {"success", success},
        {"command", command_},
    };
    if (!body.is_null())
        msg["body"] = std::move(body);
    if (!message.empty())
        msg["message"] = message;
    enqueueOut(msg);
}

void Client::enqueueOut(const Json &msg) noexcept
{
    std::string framed = frameMessage(msg.dump());
    {
        std::lock_guard<std::mutex> lock(outMx);
        outbound.push(std::move(framed));
    }
    outCv.notify_one();
}

void Client::readerLoop() noexcept
{
    MessageReader mr;
    char buf[16384];
    while (running.load())
    {
        long n = proc.readStdout(buf, sizeof(buf));
        if (n <= 0)
            break; // EOF or error: the adapter has gone away.
        mr.feed(buf, (size_t) n);
        std::string body;
        bool pushedAny = false;
        while (mr.next(body))
        {
            Json msg = Json::parse(body, nullptr, /*allow_exceptions=*/false);
            if (!msg.is_discarded())
            {
                std::lock_guard<std::mutex> lock(inMx);
                inbound.push(std::move(msg));
                pushedAny = true;
            }
        }
        if (pushedAny && onWake)
            onWake();
    }
    running.store(false);
    // Wake once more so pump() (and the manager) observes the terminated
    // adapter promptly rather than on the next natural idle tick.
    if (onWake)
        onWake();
}

void Client::writerLoop() noexcept
{
    for (;;)
    {
        std::string framed;
        {
            std::unique_lock<std::mutex> lock(outMx);
            outCv.wait(lock, [this] { return outClosed || !outbound.empty(); });
            if (outbound.empty())
                return; // outClosed and drained.
            framed = std::move(outbound.front());
            outbound.pop();
        }
        if (!proc.writeStdin(framed.data(), framed.size()))
            return;
    }
}

void Client::dispatch(const Json &msg, const EventHandler &onEvent,
                      const ReverseRequestHandler &onReverseRequest) noexcept
{
    std::string type = msg.value("type", std::string());

    if (type == "response")
    {
        // Response to one of our requests, correlated by request_seq.
        if (msg.contains("request_seq") && msg["request_seq"].is_number_integer())
        {
            int seq = msg["request_seq"].get<int>();
            auto it = handlers.find(seq);
            if (it != handlers.end())
            {
                ResponseHandler handler = std::move(it->second);
                handlers.erase(it);
                bool success = msg.value("success", false);
                std::string message = msg.value("message", std::string());
                handler(success, msg.value("body", Json()), message);
            }
        }
        return;
    }

    if (type == "event")
    {
        if (onEvent)
            onEvent(msg.value("event", std::string()), msg.value("body", Json()));
        return;
    }

    if (type == "request")
    {
        // Adapter -> client reverse request (runInTerminal, startDebugging, ...).
        int seq = msg.value("seq", 0);
        std::string command_ = msg.value("command", std::string());
        if (onReverseRequest)
            onReverseRequest(seq, command_, msg.value("arguments", Json()));
        else
            // Nobody to handle it: refuse so the adapter does not stall.
            respondToReverseRequest(seq, command_, false, Json(), "unsupported");
    }
}

void Client::pump(const EventHandler &onEvent, const ReverseRequestHandler &onReverseRequest) noexcept
{
    std::queue<Json> local;
    {
        std::lock_guard<std::mutex> lock(inMx);
        std::swap(local, inbound);
    }
    while (!local.empty())
    {
        dispatch(local.front(), onEvent, onReverseRequest);
        local.pop();
    }
}

void Client::stop() noexcept
{
    if (!running.exchange(false) && !reader.joinable() && !writer.joinable())
        return;

    // Best-effort polite disconnect (terminate the debuggee with us).
    if (proc.running())
    {
        int seq = nextSeq++;
        Json msg = {
            {"seq", seq},
            {"type", "request"},
            {"command", "disconnect"},
            {"arguments", {{"terminateDebuggee", true}}},
        };
        enqueueOut(msg);
    }

    {
        std::lock_guard<std::mutex> lock(outMx);
        outClosed = true;
    }
    outCv.notify_all();

    proc.terminate(); // unblocks the reader's blocking read via EOF

    if (writer.joinable())
        writer.join();
    if (reader.joinable())
        reader.join();
}

} // namespace dap
} // namespace turbo

#endif // TURBO_ENABLE_DAP
