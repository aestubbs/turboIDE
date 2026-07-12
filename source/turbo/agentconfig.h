#ifndef TURBO_AGENTCONFIG_H
#define TURBO_AGENTCONFIG_H

#include <string>
#include <vector>

// A selectable coding-agent preset shown in the "Select Agent" picker.
struct AgentPreset
{
    std::string name;    // stored value / preset id, e.g. "claude"
    std::string command; // command line to launch, e.g. "claude"
    std::string label;   // human-facing label, e.g. "Claude Code"
};

// The built-in agent presets, in display order.
const std::vector<AgentPreset> &agentPresets() noexcept;

// Map a stored agent value (a preset name or a raw command line) to the command
// line to run: a value matching a preset name yields that preset's command;
// anything else is treated as a literal command. Empty yields "".
std::string agentCommandFor(const std::string &value) noexcept;

// Resolve the agent command to launch: the project override if set, else the
// global default if set, else the built-in default ("claude"). Returns a
// launchable command line.
std::string resolveAgentCommand(const std::string &projectOverride,
                                const std::string &globalDefault) noexcept;

#endif // TURBO_AGENTCONFIG_H
