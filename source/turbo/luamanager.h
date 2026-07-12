#ifndef TURBO_LUAMANAGER_H
#define TURBO_LUAMANAGER_H

// LuaManager embeds a Lua 5.4 interpreter so the editor can be configured and
// scripted in Lua. It owns a single lua_State, installs a `turbo` API table,
// loads the user's init scripts (which register event hooks via turbo.on), runs
// named scripts on demand, and dispatches editor lifecycle events
// (beforeCommit, afterCommit, newFile, ...) to the registered Lua handlers.
//
// The manager is deliberately decoupled from TurboApp: instead of calling back
// into the app directly it talks to a LuaHost, a bundle of std::functions the
// app wires to its own methods. This keeps the Lua C headers (which it includes
// in luamanager.cc only) out of the rest of the app and avoids include cycles.

#include <functional>
#include <string>
#include <utility>
#include <vector>

struct lua_State;

// Key/value parameters passed alongside an event to its Lua handlers. Values
// land in the `params` table the handler receives (e.g. params.path, params.message).
using LuaEventParams = std::vector<std::pair<std::string, std::string>>;

// One declared input parameter of a Lua-registered command (the table form of
// turbo.register_command). Used to build the tool's JSON Schema for the MCP
// server. 'type' is a JSON-schema type name ("string", "number", "integer",
// "boolean"); it defaults to "string" when unspecified.
struct LuaParam
{
    std::string name;
    std::string type {"string"};
    std::string description;
    bool required {false};
};

// Host hooks the Lua API calls into. Every field is optional; a script calling
// an API whose host hook is unset gets a graceful no-op / empty result.
struct LuaHost
{
    // Show a line of text to the user (status line / message box).
    std::function<void(const std::string &)> message;
    // Absolute path of the focused editor, or "" when none.
    std::function<std::string()> activeFilePath;
    // Full text of the focused editor, or "" when none.
    std::function<std::string()> activeFileText;
    // Insert text at the focused editor's cursor.
    std::function<void(const std::string &)> insertText;
    // Open (or focus, if already open) a file by path.
    std::function<void(const std::string &)> openFile;
    // Save the focused editor.
    std::function<void()> saveFile;
    // Dispatch a Turbo command id (the cmXxx constants) to the application.
    std::function<void(int)> runCommand;
    // Run a shell command in the project directory; returns its stdout.
    std::function<std::string(const std::string &)> shell;
    // Absolute path of the project root (the directory Turbo was opened in).
    std::function<std::string()> projectRoot;
};

class LuaManager
{
public:
    explicit LuaManager(LuaHost host) noexcept;
    ~LuaManager();

    LuaManager(const LuaManager &) = delete;
    LuaManager &operator=(const LuaManager &) = delete;

    // True once the interpreter is up (it always should be).
    bool ok() const noexcept { return L != nullptr; }

    // Load init.lua from the project Lua home (<projectRoot>/turbo-scripts) and the
    // global one (~/.turbo), in that order. These typically register event hooks.
    // Missing files are not an error (an empty dir is skipped). Returns the number
    // of init scripts that ran successfully.
    int loadInitScripts(const std::string &projectLuaHome,
                        const std::string &globalLuaHome) noexcept;

    // Run a Lua chunk. On failure the error is reported via host.message and
    // false is returned (lastError() holds the message).
    bool runFile(const std::string &path) noexcept;
    bool runString(const std::string &code, const char *chunkName = "=(turbo)") noexcept;

    // Fire an editor event to every handler registered for it via turbo.on.
    // For "before*" events a handler may cancel the action by returning false;
    // fireEvent then returns false (meaning "do not proceed"). All other events
    // ignore the return value and fireEvent returns true.
    bool fireEvent(const std::string &event) noexcept;
    bool fireEvent(const std::string &event, const LuaEventParams &params) noexcept;

    // Number of handlers registered for an event (lets call sites skip building
    // params when nothing is listening).
    bool hasHandlers(const std::string &event) noexcept;

    // --- Lua-registered commands -----------------------------------------
    // Scripts call turbo.register_command(name, description, fn) to add a command
    // that shows up in the command palette and runs 'fn' when chosen. These query
    // the registered set (for the palette) and invoke one by index.
    int commandCount() const noexcept { return (int) commands.size(); }
    const std::string &commandName(int i) const noexcept { return commands[i].name; }
    const std::string &commandDescription(int i) const noexcept { return commands[i].description; }
    // Declared input parameters of the i-th command (empty for legacy no-arg
    // commands). Used by the MCP server to build the tool's input schema.
    const std::vector<LuaParam> &commandParams(int i) const noexcept { return commands[i].params; }
    void runRegisteredCommand(int i) noexcept; // call the i-th command's function
    // Invoke the i-th command with JSON arguments (an object string keyed by
    // param name; "" or "{}" = none) and capture its return value into 'out':
    // a string return is passed through verbatim, any other value is JSON-
    // encoded, nil/no-return yields "". Returns false on a Lua error or bad
    // arguments (message in lastError()); unlike runRegisteredCommand it does
    // NOT pop a modal message box, so it is safe to call from the MCP server.
    bool runRegisteredCommandJson(int i, const std::string &argsJson,
                                  std::string &out) noexcept;
    // Append a command (called by the turbo.register_command API callback). 'ref'
    // is a luaL_ref into the registry holding the handler function; 'params' are
    // the command's declared input parameters (empty for the legacy no-arg form).
    void addRegisteredCommand(std::string name, std::string description, int ref,
                              std::vector<LuaParam> params = {}) noexcept;

    const std::string &lastError() const noexcept { return errorMessage; }
    const LuaHost &hostHooks() const noexcept { return host; }

private:
    void openApi() noexcept;     // installs the global `turbo` table
    bool reportIfError(int status) noexcept; // turns a lua_pcall status into a message

    // A command registered from Lua: a label, a description, and a reference into
    // the Lua registry holding the handler function.
    struct LuaCommand
    {
        std::string name;
        std::string description;
        int ref; // luaL_ref into LUA_REGISTRYINDEX
        std::vector<LuaParam> params; // declared input schema (empty = legacy no-arg)
    };
    // Drops registered commands (and frees their refs) when init scripts reload.
    void clearRegisteredCommands() noexcept;

    lua_State *L = nullptr;
    LuaHost host;
    std::string errorMessage;
    std::vector<LuaCommand> commands;
};

#endif // TURBO_LUAMANAGER_H
