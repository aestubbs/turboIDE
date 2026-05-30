#ifndef TURBO_SETTINGS_H
#define TURBO_SETTINGS_H

#include <string>
#include <vector>

// A language-server command configured for a given language id (e.g.
// {"cpp", "clangd"} or {"python", "pyright-langserver --stdio"}).
struct LspServerConfig
{
    std::string language;
    std::string command;
};

// Application-wide settings, persisted to a config file in the user's home
// directory (~/.turborc) between runs.
struct AppSettings
{
    bool autoSaveOnFocusLoss {true};
    bool lspEnabled {true};
    // Language-server command overrides. Empty by default; the LSP manager
    // falls back to built-in defaults for languages not listed here.
    std::vector<LspServerConfig> lspServers;

    // Returns the configured command for 'language', or "" if none.
    std::string lspCommandFor(const std::string &language) const noexcept;
    // Sets/replaces the command for 'language' (empty command removes it).
    void setLspCommand(const std::string &language, const std::string &command) noexcept;
};

// Read settings from the config file into 's'. Missing file/keys leave the
// corresponding defaults untouched.
void loadSettings(AppSettings &s) noexcept;

// Write 's' to the config file.
void saveSettings(const AppSettings &s) noexcept;

#endif // TURBO_SETTINGS_H
