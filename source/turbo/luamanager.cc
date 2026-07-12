// LuaManager — see luamanager.h. This is the only translation unit that pulls
// in the Lua C headers, so it is kept out of the unity batch (CMakeLists.txt).

#include "luamanager.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <nlohmann/json.hpp>

#include <cstdio>

namespace
{

using Json = nlohmann::json;

// Push a JSON value onto the Lua stack (objects -> tables keyed by name, arrays
// -> 1-based tables). Used to hand MCP tool arguments to a Lua command handler.
void pushJson(lua_State *L, const Json &j) noexcept
{
    switch (j.type())
    {
        case Json::value_t::boolean: lua_pushboolean(L, j.get<bool>()); break;
        case Json::value_t::number_integer:
        case Json::value_t::number_unsigned:
            lua_pushinteger(L, (lua_Integer) j.get<long long>()); break;
        case Json::value_t::number_float:
            lua_pushnumber(L, j.get<double>()); break;
        case Json::value_t::string:
        {
            const auto &s = j.get_ref<const std::string &>();
            lua_pushlstring(L, s.data(), s.size());
            break;
        }
        case Json::value_t::array:
        {
            lua_createtable(L, (int) j.size(), 0);
            int idx = 1;
            for (const auto &el : j) { pushJson(L, el); lua_rawseti(L, -2, idx++); }
            break;
        }
        case Json::value_t::object:
        {
            lua_createtable(L, 0, (int) j.size());
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                pushJson(L, it.value());
                lua_setfield(L, -2, it.key().c_str());
            }
            break;
        }
        default: lua_pushnil(L); break; // null / discarded
    }
}

// Convert the Lua value at 'idx' to JSON. A table with a non-empty 1..n integer
// run is treated as an array, otherwise as an object over its string keys.
Json luaToJson(lua_State *L, int idx) noexcept
{
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx))
    {
        case LUA_TBOOLEAN: return (bool) lua_toboolean(L, idx);
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) return (long long) lua_tointeger(L, idx);
            return (double) lua_tonumber(L, idx);
        case LUA_TSTRING:
        {
            size_t n = 0; const char *s = lua_tolstring(L, idx, &n);
            return std::string(s, n);
        }
        case LUA_TTABLE:
        {
            lua_Integer len = luaL_len(L, idx);
            if (len > 0)
            {
                Json arr = Json::array();
                for (lua_Integer i = 1; i <= len; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    arr.push_back(luaToJson(L, -1));
                    lua_pop(L, 1);
                }
                return arr;
            }
            Json obj = Json::object();
            lua_pushnil(L);
            while (lua_next(L, idx) != 0) // key at -2, value at -1
            {
                if (lua_type(L, -2) == LUA_TSTRING)
                    obj[lua_tostring(L, -2)] = luaToJson(L, -1);
                lua_pop(L, 1); // pop value, keep key for next()
            }
            return obj;
        }
        default: return nullptr; // nil / function / etc.
    }
}

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
    lua_pushstring(L, "turboIDE (" LUA_RELEASE ")");
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
// or the table form:
//   turbo.register_command{ name=, description=, params={ {name=,type=,
//     description=, required=}, ... }, run=function(args) ... return result end }
// -- add a command that appears in the command palette and, via the MCP server,
// is exposed to the agent as a tool.
int l_register_command(lua_State *L)
{
    if (lua_istable(L, 1)) // table form: declares typed params and returns a value
    {
        lua_getfield(L, 1, "name");
        std::string name = luaL_checkstring(L, -1);
        lua_pop(L, 1);

        std::string desc;
        lua_getfield(L, 1, "description");
        if (lua_isstring(L, -1))
            desc = lua_tostring(L, -1);
        lua_pop(L, 1);

        std::vector<LuaParam> params;
        lua_getfield(L, 1, "params");
        if (lua_istable(L, -1))
        {
            int pt = lua_absindex(L, -1);
            lua_Integer n = luaL_len(L, pt);
            for (lua_Integer k = 1; k <= n; ++k)
            {
                lua_rawgeti(L, pt, k); // the k-th param descriptor
                if (lua_istable(L, -1))
                {
                    LuaParam p;
                    lua_getfield(L, -1, "name");
                    if (lua_isstring(L, -1)) p.name = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "type");
                    if (lua_isstring(L, -1)) p.type = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "description");
                    if (lua_isstring(L, -1)) p.description = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "required");
                    p.required = lua_toboolean(L, -1);
                    lua_pop(L, 1);
                    if (!p.name.empty())
                        params.push_back(std::move(p));
                }
                lua_pop(L, 1); // pop the descriptor
            }
        }
        lua_pop(L, 1); // pop params

        lua_getfield(L, 1, "run");
        if (!lua_isfunction(L, -1))
            return luaL_error(L, "register_command: 'run' must be a function");
        int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops the function
        mgr(L)->addRegisteredCommand(std::move(name), std::move(desc), ref,
                                     std::move(params));
        return 0;
    }

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
                                      int ref, std::vector<LuaParam> params) noexcept
{
    commands.push_back({std::move(name), std::move(description), ref, std::move(params)});
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
    // Interactive (palette/menu) path: no arguments, discard the return, and
    // surface any error via the host message (a modal box) as before.
    std::string out;
    if (!runRegisteredCommandJson(i, "{}", out) && host.message && !errorMessage.empty())
        host.message(errorMessage);
}

bool LuaManager::runRegisteredCommandJson(int i, const std::string &argsJson,
                                          std::string &out) noexcept
{
    out.clear();
    if (!L || i < 0 || i >= (int) commands.size())
    {
        errorMessage = "no such command";
        return false;
    }
    Json args;
    if (!argsJson.empty())
    {
        try { args = Json::parse(argsJson); }
        catch (const std::exception &e)
        {
            errorMessage = std::string("invalid arguments: ") + e.what();
            return false;
        }
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, commands[i].ref); // push the handler
    if (args.is_object() || args.is_array())
        pushJson(L, args);                              // push the args table
    else
        lua_newtable(L);                                // no/scalar args -> {}
    int status = lua_pcall(L, 1, 1, 0);
    if (status != LUA_OK)
    {
        const char *msg = lua_tostring(L, -1);
        errorMessage = msg ? msg : "error";
        lua_pop(L, 1);
        return false;                                   // NOTE: no host.message here
    }
    if (lua_isnoneornil(L, -1))
        out.clear();
    else if (lua_type(L, -1) == LUA_TSTRING)
    {
        size_t n = 0; const char *s = lua_tolstring(L, -1, &n);
        out.assign(s, n);                               // pass strings through raw
    }
    else
    {
        try { out = luaToJson(L, -1).dump(); }
        catch (...) { out.clear(); }
    }
    lua_pop(L, 1);
    return true;
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
