#ifndef TURBO_MCPSERVER_H
#define TURBO_MCPSERVER_H

#include <turbo/mcp/transport.h>

#include <cstdint>
#include <functional>
#include <string>

class LuaManager;

// The MCP socket path for a project: <projectRoot>/.turbo/mcp.sock, or a short
// $TMPDIR fallback when that would exceed the AF_UNIX sun_path length limit.
std::string mcpSocketPath(const std::string &projectRoot) noexcept;

// Merge-write <projectRoot>/.mcp.json so a Claude-Code-style agent discovers the
// turboIDE MCP server (a "turboide" stdio server launching `<this binary> mcp
// --socket <socketPath>`), preserving any other configured servers.
void writeAgentMcpConfig(const std::string &projectRoot,
                         const std::string &socketPath) noexcept;

// Register the "turboide" server with the Claude Code CLI at LOCAL scope
// (`claude mcp add-json ... -s local`), run in the project dir. Local-scope
// servers are auto-approved (no trust prompt) and shadow any project .mcp.json
// entry of the same name. Runs on a detached background thread (the CLI takes
// ~1s) and is a best-effort no-op when the `claude` CLI is absent.
void registerClaudeMcpServerAsync(const std::string &projectRoot,
                                  const std::string &socketPath) noexcept;

// Exposes turboIDE to a coding agent as an MCP server, over a unix socket
// (turbo::mcp::SocketServer) reached through the `turboIDE mcp` bridge. Built-in
// editor tools (open_file, insert_text, run_command, ...) call the LuaHost
// hooks; each Lua command registered with turbo.register_command becomes a
// `lua_<name>` tool. All tool dispatch happens on the main thread from pump().
class McpServer
{
public:
    // 'lua' supplies both the LuaHost hooks (built-in tools) and the registered
    // command list (Lua tools). 'message' is an optional NON-modal notice sink
    // (never a message box) for lifecycle notes.
    McpServer(LuaManager &lua,
              std::function<void(const std::string &)> message = {}) noexcept;
    ~McpServer();

    // Start listening on 'socketPath'. Returns false if it could not bind (e.g.
    // another turboIDE already owns this project's MCP endpoint).
    bool start(const std::string &socketPath) noexcept;
    // Drain and dispatch pending requests. Call from the app's idle loop.
    void pump() noexcept;
    void stop() noexcept;
    bool running() const noexcept { return transport.running(); }

    // Wire the transport's wake hook (e.g. to TEventQueue::wakeUp) so a request
    // arriving on the socket thread nudges the UI loop to pump() promptly.
    void setWake(std::function<void()> wake) noexcept { transport.onWake = std::move(wake); }

private:
    void handleMessage(uint64_t connId, const std::string &msg) noexcept;

    LuaManager &lua;
    std::function<void(const std::string &)> message;
    turbo::mcp::SocketServer transport;
    bool inPump {false}; // reentrancy guard: a tool may pump modal UI
};

#endif // TURBO_MCPSERVER_H
