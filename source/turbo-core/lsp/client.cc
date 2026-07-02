#include <turbo/lsp/client.h>

#ifdef TURBO_ENABLE_LSP

#include <turbo/lsp/jsonrpc.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

namespace turbo {
namespace lsp {

Client::Client(std::string languageId, std::string aCommand, std::vector<std::string> aArgs) noexcept :
    langId(std::move(languageId)),
    command(std::move(aCommand)),
    args(std::move(aArgs))
{
}

Client::~Client()
{
    stop();
}

static int currentProcessId() noexcept
{
#ifdef _WIN32
    return (int) _getpid();
#else
    return (int) ::getpid();
#endif
}

bool Client::start(const std::string &rootUri) noexcept
{
    if (!proc.start(command, args))
        return false;
    running.store(true);
    reader = std::thread(&Client::readerLoop, this);
    writer = std::thread(&Client::writerLoop, this);

    Json caps = {
        {"general", {
            {"positionEncodings", {"utf-8", "utf-16"}},
        }},
        {"textDocument", {
            {"synchronization", {
                {"dynamicRegistration", false},
                {"didSave", true},
            }},
            {"publishDiagnostics", {
                {"relatedInformation", false},
            }},
            {"completion", {
                {"completionItem", {
                    {"snippetSupport", false},
                }},
            }},
            {"hover", {
                {"contentFormat", {"plaintext", "markdown"}},
            }},
        }},
        {"workspace", {
            {"workspaceFolders", true},
            {"configuration", true},
        }},
    };

    Json params = {
        {"processId", currentProcessId()},
        {"clientInfo", {{"name", "turboIDE"}, {"version", "0.1"}}},
        {"rootUri", rootUri.empty() ? Json(nullptr) : Json(rootUri)},
        {"capabilities", caps},
    };
    if (!rootUri.empty())
        params["workspaceFolders"] = Json::array({
            Json{{"uri", rootUri}, {"name", "workspace"}},
        });
    if (!initializationOptions.is_null())
        params["initializationOptions"] = initializationOptions;

    // The initialize request bypasses the until-ready queue.
    int id = nextId++;
    handlers[id] = [this](const Json &result, const Json *error) {
        if (!error)
            onInitializeResult(result);
    };
    enqueueOut(makeRequest(id, "initialize", params));
    return true;
}

void Client::onInitializeResult(const Json &result) noexcept
{
    capabilities = result.value("capabilities", Json::object());
    auto enc = capabilities.value("positionEncoding", std::string("utf-16"));
    encoding = (enc == "utf-8") ? PositionEncoding::UTF8 : PositionEncoding::UTF16;
    isReady = true;
    enqueueOut(makeNotification("initialized", Json::object()));
    flushPending();
}

void Client::flushPending() noexcept
{
    for (auto &msg : pendingOutbound)
        enqueueOut(msg);
    pendingOutbound.clear();
}

Json Client::makeRequest(int id, const std::string &method, const Json &params) noexcept
{
    Json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
    if (!params.is_null())
        msg["params"] = params;
    return msg;
}

Json Client::makeNotification(const std::string &method, const Json &params) noexcept
{
    Json msg = {{"jsonrpc", "2.0"}, {"method", method}};
    if (!params.is_null())
        msg["params"] = params;
    return msg;
}

void Client::request(const std::string &method, Json params, ResponseHandler handler) noexcept
{
    int id = nextId++;
    handlers[id] = std::move(handler);
    Json msg = makeRequest(id, method, params);
    if (isReady)
        enqueueOut(msg);
    else
        pendingOutbound.push_back(std::move(msg));
}

void Client::notify(const std::string &method, Json params) noexcept
{
    Json msg = makeNotification(method, params);
    if (isReady)
        enqueueOut(msg);
    else
        pendingOutbound.push_back(std::move(msg));
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
            break; // EOF or error: the server has gone away.
        mr.feed(buf, (size_t) n);
        std::string body;
        while (mr.next(body))
        {
            Json msg = Json::parse(body, nullptr, /*allow_exceptions=*/false);
            if (!msg.is_discarded())
            {
                std::lock_guard<std::mutex> lock(inMx);
                inbound.push(std::move(msg));
            }
        }
    }
    running.store(false);
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

void Client::dispatch(const Json &msg, const ServerMessageHandler &onServerMessage) noexcept
{
    bool hasId = msg.contains("id") && !msg["id"].is_null();
    bool hasMethod = msg.contains("method");

    if (hasId && !hasMethod)
    {
        // Response to one of our requests.
        if (msg["id"].is_number_integer())
        {
            int id = msg["id"].get<int>();
            auto it = handlers.find(id);
            if (it != handlers.end())
            {
                ResponseHandler handler = std::move(it->second);
                handlers.erase(it);
                if (msg.contains("error"))
                {
                    Json err = msg["error"];
                    handler(Json(), &err);
                }
                else
                    handler(msg.value("result", Json()), nullptr);
            }
        }
        return;
    }

    if (hasMethod)
    {
        // Server-initiated message.
        if (onServerMessage)
            onServerMessage(msg);
        if (hasId)
        {
            // Server -> client request: reply so the server does not stall.
            // We do not implement any of these yet, so acknowledge with null.
            Json reply = {{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"result", nullptr}};
            enqueueOut(reply);
        }
    }
}

void Client::pump(const ServerMessageHandler &onServerMessage) noexcept
{
    std::queue<Json> local;
    {
        std::lock_guard<std::mutex> lock(inMx);
        std::swap(local, inbound);
    }
    while (!local.empty())
    {
        dispatch(local.front(), onServerMessage);
        local.pop();
    }
}

void Client::stop() noexcept
{
    if (!running.exchange(false) && !reader.joinable() && !writer.joinable())
        return;

    // Best-effort polite shutdown.
    if (proc.running())
    {
        enqueueOut(makeRequest(nextId++, "shutdown", Json()));
        enqueueOut(makeNotification("exit", Json()));
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

} // namespace lsp
} // namespace turbo

#endif // TURBO_ENABLE_LSP
