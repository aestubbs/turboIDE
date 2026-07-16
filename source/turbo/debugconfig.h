#ifndef TURBO_DEBUGCONFIG_H
#define TURBO_DEBUGCONFIG_H

#include <string>
#include <vector>

// A per-language debug-adapter entry. 'command' is the adapter command line
// (executable + args, whitespace-split); empty falls back to the built-in
// default for the language. 'request' is "launch" or "attach" (empty = the
// language default). launch uses 'program' (empty = the current file) and 'cwd'
// (empty = project root); attach uses 'host'/'port' (e.g. Xdebug on 9003).
struct DebugAdapter
{
    std::string language;   // "cpp", "python", "php", ...
    std::string command;    // adapter command line ("" = built-in default)
    std::string request;    // "launch" | "attach" | ""
    std::string program;    // launch: program to debug ("" = current file)
    std::string cwd;        // launch: working dir ("" = project root)
    std::string host;       // attach: host ("" = 127.0.0.1)
    int port {0};           // attach: port
    bool stopOnEntry {false};
};

// Per-project debugger configuration, stored as <root>/.turbo/debug.json.
struct DebugConfig
{
    std::vector<DebugAdapter> adapters;

    // The configured adapter for 'language', or nullptr if none.
    const DebugAdapter *forLanguage(const std::string &language) const noexcept;

    // Load from <root>/.turbo/debug.json. Returns false if absent/unreadable
    // (leaving the current contents in place). Never throws.
    bool load(const std::string &root) noexcept;
    // Write to <root>/.turbo/debug.json, creating the .turbo directory.
    void save(const std::string &root) const noexcept;
};

#endif // TURBO_DEBUGCONFIG_H
