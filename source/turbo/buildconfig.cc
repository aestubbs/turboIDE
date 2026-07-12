#include "buildconfig.h"

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
    return configDir(root) + "/config.json";
}

} // namespace

bool BuildConfig::load(const std::string &root) noexcept
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
        build    = j.value("build", std::string());
        test     = j.value("test", std::string());
        run      = j.value("run", std::string());
        artifact = j.value("artifact", std::string());
        runMode  = j.value("runMode", std::string("auto"));
        agent    = j.value("agent", std::string());
        extra.clear();
        if (j.contains("extra") && j["extra"].is_array())
            for (auto &e : j["extra"])
                extra.push_back({ e.value("name", std::string()),
                                  e.value("command", std::string()) });
    }
    catch (...)
    {
        return false; // malformed config: keep defaults rather than crash
    }
    return true;
}

void BuildConfig::save(const std::string &root) const noexcept
{
    if (root.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(configDir(root), ec);
    try
    {
        Json j;
        j["build"]    = build;
        j["test"]     = test;
        j["run"]      = run;
        j["artifact"] = artifact;
        j["runMode"]  = runMode;
        j["agent"]    = agent;
        Json arr = Json::array();
        for (auto &c : extra)
            arr.push_back({ {"name", c.name}, {"command", c.command} });
        j["extra"] = arr;
        std::ofstream f(configPath(root));
        if (f)
            f << j.dump(2) << '\n';
    }
    catch (...)
    {
    }
}
