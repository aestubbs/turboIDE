#include "debugconfig.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using Json = nlohmann::json;

namespace {

std::string configDir(const std::string &root)
{
    return root + "/.turbo";
}

std::string configPath(const std::string &root)
{
    return configDir(root) + "/debug.json";
}

} // namespace

const DebugAdapter *DebugConfig::forLanguage(const std::string &language) const noexcept
{
    for (const DebugAdapter &a : adapters)
        if (a.language == language)
            return &a;
    return nullptr;
}

bool DebugConfig::load(const std::string &root) noexcept
{
    if (root.empty())
        return false;
    std::ifstream f(configPath(root));
    if (!f)
        return false;
    try
    {
        Json j;
        f >> j;
        adapters.clear();
        if (j.contains("adapters") && j["adapters"].is_array())
            for (auto &e : j["adapters"])
            {
                DebugAdapter a;
                a.language    = e.value("language", std::string());
                a.command     = e.value("command", std::string());
                a.request     = e.value("request", std::string());
                a.program     = e.value("program", std::string());
                a.cwd         = e.value("cwd", std::string());
                a.host        = e.value("host", std::string());
                a.port        = e.value("port", 0);
                a.stopOnEntry = e.value("stopOnEntry", false);
                if (!a.language.empty())
                    adapters.push_back(std::move(a));
            }
    }
    catch (...)
    {
        return false; // malformed config: keep whatever we had rather than crash
    }
    return true;
}

void DebugConfig::save(const std::string &root) const noexcept
{
    if (root.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(configDir(root), ec);
    try
    {
        Json arr = Json::array();
        for (const DebugAdapter &a : adapters)
        {
            Json e;
            e["language"] = a.language;
            e["command"]  = a.command;
            e["request"]  = a.request;
            if (!a.program.empty()) e["program"] = a.program;
            if (!a.cwd.empty())     e["cwd"]     = a.cwd;
            if (!a.host.empty())    e["host"]    = a.host;
            if (a.port)             e["port"]    = a.port;
            if (a.stopOnEntry)      e["stopOnEntry"] = a.stopOnEntry;
            arr.push_back(std::move(e));
        }
        Json j;
        j["adapters"] = arr;
        std::ofstream f(configPath(root));
        if (f)
            f << j.dump(2) << '\n';
    }
    catch (...)
    {
    }
}
