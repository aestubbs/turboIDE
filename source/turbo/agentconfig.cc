#include "agentconfig.h"

const std::vector<AgentPreset> &agentPresets() noexcept
{
    static const std::vector<AgentPreset> presets = {
        { "claude",   "claude",   "Claude Code" },
        { "codex",    "codex",    "Codex" },
        { "opencode", "opencode", "opencode" },
    };
    return presets;
}

std::string agentCommandFor(const std::string &value) noexcept
{
    if (value.empty())
        return {};
    for (auto &p : agentPresets())
        if (p.name == value)
            return p.command;
    return value; // not a known preset: treat as a literal command line
}

std::string resolveAgentCommand(const std::string &projectOverride,
                                const std::string &globalDefault) noexcept
{
    if (!projectOverride.empty())
        return agentCommandFor(projectOverride);
    if (!globalDefault.empty())
        return agentCommandFor(globalDefault);
    return agentCommandFor("claude");
}
