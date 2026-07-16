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

std::string baseName(const std::string &path) noexcept
{
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
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

void DapManager::setConfig(DebugConfig cfg) noexcept
{
    debugConfig = std::move(cfg);
}

DapManager::AdapterSpec DapManager::resolveAdapter(const std::string &languageId) noexcept
{
    AdapterSpec spec;
    spec.languageId = languageId;

    // 1. Environment override: TURBO_DAP_ADAPTER_<LANG> = "<cmd> <args...>".
    // Wins outright (used to point tests at a mock adapter).
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

    // 2. Built-in defaults for the language.
    if (languageId == "python")
    {
        spec.command = "python3";
        spec.args = {"-m", "debugpy.adapter"};
    }
    else if (languageId == "cpp" || languageId == "c")
    {
        spec.command = "lldb-dap"; // or `gdb --interpreter=dap` via config/env
    }
    else if (languageId == "php")
    {
        // Xdebug: the php-debug adapter (configure its command in debug.json)
        // listens for Xdebug's reverse connection, so turbo just attaches.
        spec.request = "attach";
        spec.host = "127.0.0.1";
        spec.port = 9003;
    }

    // 3. Project config (.turbo/debug.json) overrides the defaults, field by
    // field, so a project can supply an adapter command (required for php) or
    // change the request/port/program without restating the rest.
    if (const DebugAdapter *a = debugConfig.forLanguage(languageId))
    {
        if (!a->command.empty())
        {
            auto parts = splitArgs(a->command);
            if (!parts.empty())
            {
                spec.command = parts.front();
                spec.args.assign(parts.begin() + 1, parts.end());
            }
        }
        if (!a->request.empty()) spec.request = a->request;
        if (!a->program.empty()) spec.program = a->program;
        if (!a->cwd.empty())     spec.cwd = a->cwd;
        if (!a->host.empty())    spec.host = a->host;
        if (a->port)             spec.port = a->port;
        spec.stopOnEntry = a->stopOnEntry;
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
        if (pending.stopOnEntry)
            args["stopOnEntry"] = true;
    }
    else // launch
    {
        std::string prog = pending.program.empty() ? pendingProgram : pending.program;
        std::string wd = pending.cwd.empty() ? rootPath : pending.cwd;
        args = {
            {"program", prog},
            {"cwd", wd},
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

bool DapManager::toggleBreakpoint(const std::string &file, int line) noexcept
{
    auto &lines = breakpoints[file];
    bool nowSet;
    if (lines.erase(line))
        nowSet = false;
    else
    {
        lines.insert(line);
        nowSet = true;
    }
    if (lines.empty())
        breakpoints.erase(file);
    if (active)
        sendBreakpoints(file); // update the live session immediately
    return nowSet;
}

void DapManager::sendBreakpoints(const std::string &file) noexcept
{
    if (!client)
        return;
    Json bps = Json::array();
    auto it = breakpoints.find(file);
    if (it != breakpoints.end())
        for (int l : it->second)
            bps.push_back(Json{{"line", l + 1}}); // DAP lines are 1-based
    Json args = {
        {"source", {{"path", file}, {"name", baseName(file)}}},
        {"breakpoints", bps},
    };
    client->sendRequest("setBreakpoints", args);
}

void DapManager::sendAllBreakpoints() noexcept
{
    for (auto &kv : breakpoints)
        sendBreakpoints(kv.first);
}

void DapManager::fetchScopes(int frameId) noexcept
{
    if (!client)
        return;
    client->sendRequest("scopes", Json{{"frameId", frameId}},
        [this](bool ok, const Json &b, const std::string &)
        {
            std::vector<ScopeInfo> scopes;
            if (ok && b.contains("scopes") && b["scopes"].is_array())
                for (const Json &s : b["scopes"])
                {
                    ScopeInfo info;
                    info.name = s.value("name", std::string());
                    info.variablesReference = s.value("variablesReference", 0);
                    scopes.push_back(std::move(info));
                }
            if (onScopes)
                onScopes(scopes);
        });
}

void DapManager::fetchVariables(int variablesReference,
                                std::function<void(std::vector<VariableInfo>)> cb) noexcept
{
    if (!client || variablesReference <= 0)
    {
        if (cb)
            cb({});
        return;
    }
    client->sendRequest("variables", Json{{"variablesReference", variablesReference}},
        [cb = std::move(cb)](bool ok, const Json &b, const std::string &)
        {
            std::vector<VariableInfo> vars;
            if (ok && b.contains("variables") && b["variables"].is_array())
                for (const Json &v : b["variables"])
                {
                    VariableInfo info;
                    info.name = v.value("name", std::string());
                    info.value = v.value("value", std::string());
                    info.variablesReference = v.value("variablesReference", 0);
                    vars.push_back(std::move(info));
                }
            if (cb)
                cb(std::move(vars));
        });
}

void DapManager::onEvent(const std::string &event, const Json &body) noexcept
{
    if (event == "initialized")
    {
        // Adapter is ready for configuration: register breakpoints, then finish
        // configuration so the debuggee runs.
        sendAllBreakpoints();
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
        std::string reason = body.value("reason", std::string());
        // Resolve the stop location from the top stack frame, then surface it so
        // the UI can jump to and highlight the current line.
        if (client && currentThreadId)
        {
            Json args = {{"threadId", currentThreadId}, {"startFrame", 0}, {"levels", 20}};
            client->sendRequest("stackTrace", args,
                [this, reason](bool ok, const Json &b, const std::string &)
                {
                    std::vector<StackFrameInfo> frames;
                    if (ok && b.contains("stackFrames") && b["stackFrames"].is_array())
                        for (const Json &fr : b["stackFrames"])
                        {
                            StackFrameInfo info;
                            info.id = fr.value("id", 0);
                            info.name = fr.value("name", std::string());
                            info.line = fr.value("line", 0);
                            if (fr.contains("source") && fr["source"].is_object())
                            {
                                const Json &src = fr["source"];
                                if (src.contains("path") && src["path"].is_string())
                                    info.file = src["path"].get<std::string>();
                            }
                            frames.push_back(std::move(info));
                        }
                    if (onStopped)
                        onStopped(frames.empty() ? std::string() : frames[0].file,
                                  frames.empty() ? 0 : frames[0].line, reason);
                    if (onFrames)
                        onFrames(frames);
                    // Populate the Variables panel for the innermost frame.
                    if (!frames.empty())
                        fetchScopes(frames[0].id);
                });
        }
        else
        {
            if (onStopped)
                onStopped("", 0, reason);
            if (onFrames)
                onFrames({});
        }
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
