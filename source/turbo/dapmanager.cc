#include "dapmanager.h"

#ifdef TURBO_ENABLE_DAP

#include <cctype>
#include <cstdlib>

using turbo::dap::Json;

namespace {

// Naive whitespace split of a command line into command + args (mirrors the
// LSP manager's splitArgs). Good enough for adapter overrides; no quoting.
std::vector<std::string> splitArgs(const std::string &s) noexcept
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (std::isspace((unsigned char) c))
        {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
        else
            cur.push_back(c);
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::string upper(std::string s) noexcept
{
    for (auto &c : s) c = (char) std::toupper((unsigned char) c);
    return s;
}

} // namespace

DapManager::DapManager() noexcept = default;

DapManager::~DapManager()
{
    shutdown();
}

void DapManager::setRootPath(const char *path) noexcept
{
    rootPath = path ? path : "";
}

DapManager::AdapterSpec DapManager::resolveAdapter(const std::string &languageId) noexcept
{
    AdapterSpec spec;
    spec.languageId = languageId;

    // 1. Environment override: TURBO_DAP_ADAPTER_<LANG> = "<cmd> <args...>".
    std::string envName = "TURBO_DAP_ADAPTER_" + upper(languageId);
    if (const char *ov = std::getenv(envName.c_str()); ov && *ov)
    {
        auto parts = splitArgs(ov);
        if (!parts.empty())
        {
            spec.command = parts.front();
            spec.args.assign(parts.begin() + 1, parts.end());
        }
        return spec;
    }

    // 2. Built-in defaults. (Project .turbo/debug.json config lands in M4.)
    if (languageId == "python")
    {
        spec.command = "python3";
        spec.args = {"-m", "debugpy.adapter"};
    }
    else if (languageId == "cpp" || languageId == "c")
    {
        spec.command = "lldb-dap"; // or `gdb --interpreter=dap` via env override
    }
    else if (languageId == "php")
    {
        // Requires a configured adapter path (php-debug); attach to Xdebug.
        spec.request = "attach";
        spec.port = 9003;
    }
    return spec;
}

bool DapManager::start(const std::string &languageId, const std::string &program) noexcept
{
    // Replace any prior session.
    if (client)
        endSession();

    AdapterSpec spec = resolveAdapter(languageId);
    if (!spec.valid())
    {
        if (onOutput)
            onOutput("console", "No debug adapter configured for '" + languageId +
                                 "'. Set TURBO_DAP_ADAPTER_" + upper(languageId) + ".\n");
        return false;
    }

    client = std::make_unique<turbo::dap::Client>(spec.command, spec.args, rootPath);
    client->setWake(onWake);
    if (!client->start())
    {
        if (onOutput)
            onOutput("console", "Failed to start debug adapter: " + spec.command + "\n");
        client.reset();
        return false;
    }

    active = true;
    pending = spec;
    pendingProgram = program;
    currentThreadId = 0;
    adapterCaps = Json();
    if (onSessionState)
        onSessionState(true);

    Json initArgs = {
        {"clientID", "turboIDE"},
        {"clientName", "turboIDE"},
        {"adapterID", languageId},
        {"locale", "en-US"},
        {"linesStartAt1", true},
        {"columnsStartAt1", true},
        {"pathFormat", "path"},
        {"supportsVariableType", true},
        {"supportsRunInTerminalRequest", true},
    };
    client->sendRequest("initialize", initArgs,
        [this](bool ok, const Json &body, const std::string &message)
        {
            if (!ok)
            {
                if (onOutput)
                    onOutput("console", "initialize failed: " + message + "\n");
                endSession();
                return;
            }
            adapterCaps = body;
            // Per the DAP handshake: send launch/attach after the initialize
            // response; the adapter then emits an 'initialized' event, at which
            // point we send configurationDone (and, from M2, breakpoints).
            sendLaunchOrAttach();
        });
    return true;
}

void DapManager::sendLaunchOrAttach() noexcept
{
    if (!client)
        return;

    Json args;
    if (pending.request == "attach")
    {
        args = {
            {"host", pending.host},
            {"port", pending.port},
        };
    }
    else // launch
    {
        args = {
            {"program", pendingProgram},
            {"cwd", rootPath},
            {"args", Json::array()},
            {"stopOnEntry", pending.stopOnEntry},
        };
        // Keep the debuggee's stdio flowing to us as 'output' events rather than
        // spawning a terminal (which would require runInTerminal support).
        if (pending.languageId == "python")
            args["console"] = "internalConsole";
    }

    client->sendRequest(pending.request, args,
        [this](bool ok, const Json &, const std::string &message)
        {
            if (!ok)
            {
                if (onOutput)
                    onOutput("console", pending.request + " failed: " + message + "\n");
                endSession();
            }
        });
}

void DapManager::onEvent(const std::string &event, const Json &body) noexcept
{
    if (event == "initialized")
    {
        // Adapter is ready for configuration. Breakpoints are registered here
        // from M2; for now just finish configuration so the debuggee runs.
        if (client)
            client->sendRequest("configurationDone", Json::object());
    }
    else if (event == "output")
    {
        if (onOutput)
            onOutput(body.value("category", std::string("console")),
                     body.value("output", std::string()));
    }
    else if (event == "stopped")
    {
        currentThreadId = body.value("threadId", 0);
        // Frame/source resolution (stackTrace) arrives in M2; surface the stop
        // so the UI can reflect the paused state now.
        if (onStopped)
            onStopped("", 0, body.value("reason", std::string()));
    }
    else if (event == "continued")
    {
        if (onContinued)
            onContinued();
    }
    else if (event == "terminated" || event == "exited")
    {
        endSession();
    }
    // 'thread', 'process', 'module', 'breakpoint' events are ignored for now.
}

void DapManager::onReverseRequest(int seq, const std::string &command,
                                  const Json &) noexcept
{
    // runInTerminal / startDebugging are not supported yet; refuse politely so
    // the adapter does not stall. (We use console:"internalConsole" launches to
    // avoid needing runInTerminal.)
    if (client)
        client->respondToReverseRequest(seq, command, false, Json(), "unsupported");
}

void DapManager::continueExec() noexcept
{
    if (client && currentThreadId)
        client->sendRequest("continue", Json{{"threadId", currentThreadId}});
}

void DapManager::stepOver() noexcept
{
    if (client && currentThreadId)
        client->sendRequest("next", Json{{"threadId", currentThreadId}});
}

void DapManager::stepIn() noexcept
{
    if (client && currentThreadId)
        client->sendRequest("stepIn", Json{{"threadId", currentThreadId}});
}

void DapManager::stepOut() noexcept
{
    if (client && currentThreadId)
        client->sendRequest("stepOut", Json{{"threadId", currentThreadId}});
}

void DapManager::pause() noexcept
{
    if (client)
        client->sendRequest("pause", Json{{"threadId", currentThreadId ? currentThreadId : 1}});
}

void DapManager::terminate() noexcept
{
    if (!client)
        return;
    // Ask the adapter to terminate the debuggee; teardown completes on the
    // 'terminated' event or, failing that, in shutdown()/endSession().
    client->sendRequest("terminate", Json::object());
    client->sendRequest("disconnect", Json{{"terminateDebuggee", true}});
}

void DapManager::endSession() noexcept
{
    bool wasActive = active;
    active = false;
    currentThreadId = 0;
    if (client)
    {
        client->stop();
        client.reset();
    }
    if (wasActive && onSessionState)
        onSessionState(false);
}

void DapManager::pump() noexcept
{
    if (!client)
        return;
    client->pump(
        [this](const std::string &event, const Json &body) { onEvent(event, body); },
        [this](int seq, const std::string &command, const Json &args)
            { onReverseRequest(seq, command, args); });

    // If the adapter's process died without a clean 'terminated' event, reap.
    if (client && !client->alive())
        endSession();
}

void DapManager::shutdown() noexcept
{
    endSession();
}

#endif // TURBO_ENABLE_DAP
