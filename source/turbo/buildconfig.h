#ifndef TURBO_BUILDCONFIG_H
#define TURBO_BUILDCONFIG_H

#include <string>
#include <vector>

// One of the extra commands started alongside Run (e.g. a queue runner).
struct BuildCommand
{
    std::string name;
    std::string command;
};

// Per-project Build/Run configuration, stored as <root>/.turbo/config.json.
struct BuildConfig
{
    std::string build;            // build command
    std::string test;             // test command
    std::string run;              // run command
    std::string artifact;         // file Run depends on (optional, for staleness)
    std::string runMode {"auto"}; // "auto" (build if stale) | "build" | "run"
    std::vector<BuildCommand> extra; // started in the background with Run
    std::string agent;            // coding-agent override: preset name or command

    bool empty() const noexcept
    {
        return build.empty() && test.empty() && run.empty() && extra.empty();
    }

    // Load from <root>/.turbo/config.json. Returns false if absent/unreadable
    // (leaving the defaults in place). Never throws.
    bool load(const std::string &root) noexcept;
    // Write to <root>/.turbo/config.json, creating the .turbo directory.
    void save(const std::string &root) const noexcept;
};

#endif // TURBO_BUILDCONFIG_H
