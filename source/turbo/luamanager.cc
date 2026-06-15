// LuaManager — see luamanager.h. This is the only translation unit that pulls
// in the Lua C headers, so it is kept out of the unity batch (CMakeLists.txt).

#include "luamanager.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstdio>

namespace
{

// The LuaManager pointer lives in the lua_State's extra space (a void*-sized
// slot reserved per state). Every C API callback fetches it from there.
LuaManager *mgr(lua_State *L) noexcept
{
    return *static_cast<LuaManager **>(lua_getextraspace(L));
}

const LuaHost &host(lua_State *L) noexcept
{
    return mgr(L)->hostHooks();
}

void pushString(lua_State *L, const std::string &s) noexcept
{
    lua_pushlstring(L, s.data(), s.size());
}

// --- turbo.* API implementations ---------------------------------------------

int l_log(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    if (host(L).message)
        host(L).message(s);
    return 0;
}

int l_version(lua_State *L)
{
    lua_pushstring(L, "Turbo (" LUA_RELEASE ")");
    return 1;
}

// turbo.on(eventName, function) — append a handler to registry["turbo.handlers"][eventName].
int l_on(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, "turbo.handlers"); // [handlers]
    lua_getfield(L, -1, name);                            // [handlers, list?]
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);             // drop nil
        lua_newtable(L);           // [handlers, list]
        lua_pushvalue(L, -1);      // [handlers, list, list]
        lua_setfield(L, -3, name); // handlers[name] = list
    }
    lua_Integer n = luaL_len(L, -1);
    lua_pushvalue(L, 2);     // [handlers, list, fn]
    lua_seti(L, -2, n + 1);  // list[n+1] = fn
    lua_pop(L, 2);           // drop list + handlers
    return 0;
}

int l_active_file(lua_State *L)
{
    pushString(L, host(L).activeFilePath ? host(L).activeFilePath() : std::string());
    return 1;
}

int l_file_text(lua_State *L)
{
    pushString(L, host(L).activeFileText ? host(L).activeFileText() : std::string());
    return 1;
}

int l_insert_text(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    if (host(L).insertText)
        host(L).insertText(s);
    return 0;
}

int l_open_file(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    if (host(L).openFile)
        host(L).openFile(s);
    return 0;
}

int l_save(lua_State *L)
{
    if (host(L).saveFile)
        host(L).saveFile();
    return 0;
}

int l_run_command(lua_State *L)
{
    lua_Integer cmd = luaL_checkinteger(L, 1);
    if (host(L).runCommand)
        host(L).runCommand((int) cmd);
    return 0;
}

int l_shell(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    pushString(L, host(L).shell ? host(L).shell(s) : std::string());
    return 1;
}

int l_project_root(lua_State *L)
{
    pushString(L, host(L).projectRoot ? host(L).projectRoot() : std::string());
    return 1;
}

// turbo.register_command(name, fn) or turbo.register_command(name, description, fn)
// -- add a command that appears in the command palette and runs fn when chosen.
int l_register_command(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *desc = "";
    int fnIndex;
    if (lua_isfunction(L, 2)) // (name, fn)
        fnIndex = 2;
    else                      // (name, description, fn)
    {
        desc = luaL_checkstring(L, 2);
        luaL_checktype(L, 3, LUA_TFUNCTION);
        fnIndex = 3;
    }
    lua_pushvalue(L, fnIndex);                 // dup the function
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pops it, returns a registry ref
    mgr(L)->addRegisteredCommand(name, desc, ref);
    return 0;
}

const luaL_Reg kApi[] =
{
    {"log",          l_log},
    {"message",      l_log},
    {"version",      l_version},
    {"on",           l_on},
    {"active_file",  l_active_file},
    {"file_text",    l_file_text},
    {"insert_text",  l_insert_text},
    {"open_file",    l_open_file},
    {"save",         l_save},
    {"run_command",  l_run_command},
    {"shell",            l_shell},
    {"project_root",     l_project_root},
    {"register_command", l_register_command},
    {nullptr,            nullptr},
};

} // namespace

LuaManager::LuaManager(LuaHost host_) noexcept
    : host(std::move(host_))
{
    L = luaL_newstate();
    if (!L)
        return;
    *static_cast<LuaManager **>(lua_getextraspace(L)) = this;
    luaL_openlibs(L);
    openApi();
    // registry["turbo.handlers"] = {}  — event name -> list of handler functions.
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "turbo.handlers");
}

LuaManager::~LuaManager()
{
    if (L)
        lua_close(L);
    L = nullptr;
}

void LuaManager::openApi() noexcept
{
    luaL_newlib(L, kApi);     // [turbo]
    lua_setglobal(L, "turbo"); // _G.turbo = table
}

bool LuaManager::reportIfError(int status) noexcept
{
    if (status == LUA_OK)
        return false;
    const char *msg = lua_tostring(L, -1);
    errorMessage = msg ? msg : "unknown Lua error";
    lua_pop(L, 1);
    if (host.message)
        host.message("Lua error: " + errorMessage);
    return true;
}

bool LuaManager::runString(const std::string &code, const char *chunkName) noexcept
{
    if (!L)
        return false;
    int status = luaL_loadbuffer(L, code.data(), code.size(), chunkName);
    if (status == LUA_OK)
        status = lua_pcall(L, 0, 0, 0);
    return !reportIfError(status);
}

bool LuaManager::runFile(const std::string &path) noexcept
{
    if (!L)
        return false;
    int status = luaL_loadfilex(L, path.c_str(), nullptr);
    if (status == LUA_OK)
        status = lua_pcall(L, 0, 0, 0);
    return !reportIfError(status);
}

int LuaManager::loadInitScripts(const std::string &projectTurboDir,
                                const std::string &homeTurboDir) noexcept
{
    if (!L)
        return 0;
    // Reset the handler registry and registered commands first so re-running init
    // scripts (a reload) replaces them rather than stacking duplicates.
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "turbo.handlers");
    clearRegisteredCommands();

    int ran = 0;
    for (const std::string &dir : {projectTurboDir, homeTurboDir})
    {
        if (dir.empty())
            continue;
        std::string init = dir + "/init.lua";
        if (FILE *f = std::fopen(init.c_str(), "r"))
        {
            std::fclose(f);
            if (runFile(init))
                ++ran;
        }
    }
    return ran;
}

void LuaManager::addRegisteredCommand(std::string name, std::string description,
                                      int ref) noexcept
{
    commands.push_back({std::move(name), std::move(description), ref});
}

void LuaManager::clearRegisteredCommands() noexcept
{
    if (L)
        for (auto &c : commands)
            luaL_unref(L, LUA_REGISTRYINDEX, c.ref);
    commands.clear();
}

void LuaManager::runRegisteredCommand(int i) noexcept
{
    if (!L || i < 0 || i >= (int) commands.size())
        return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, commands[i].ref); // push the handler function
    reportIfError(lua_pcall(L, 0, 0, 0));
}

bool LuaManager::hasHandlers(const std::string &event) noexcept
{
    if (!L)
        return false;
    lua_getfield(L, LUA_REGISTRYINDEX, "turbo.handlers");
    lua_getfield(L, -1, event.c_str());
    bool any = lua_istable(L, -1) && luaL_len(L, -1) > 0;
    lua_pop(L, 2);
    return any;
}

bool LuaManager::fireEvent(const std::string &event) noexcept
{
    return fireEvent(event, {});
}

bool LuaManager::fireEvent(const std::string &event, const LuaEventParams &params) noexcept
{
    if (!L)
        return true;
    lua_getfield(L, LUA_REGISTRYINDEX, "turbo.handlers"); // [handlers]
    lua_getfield(L, -1, event.c_str());                  // [handlers, list?]
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 2);
        return true;
    }
    bool proceed = true;
    lua_Integer n = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= n; ++i)
    {
        lua_geti(L, -1, i); // [handlers, list, fn]
        // Build the params table the handler receives.
        lua_createtable(L, 0, (int) params.size() + 1);
        for (const auto &kv : params)
        {
            pushString(L, kv.second);
            lua_setfield(L, -2, kv.first.c_str());
        }
        lua_pushstring(L, event.c_str());
        lua_setfield(L, -2, "event");
        // fn(params) -> optional boolean
        int status = lua_pcall(L, 1, 1, 0); // [handlers, list, result|err]
        if (status != LUA_OK)
            reportIfError(status); // pops the error object
        else
        {
            if (lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1))
                proceed = false;
            lua_pop(L, 1); // drop result
        }
    }
    lua_pop(L, 2); // drop list + handlers
    return proceed;
}
