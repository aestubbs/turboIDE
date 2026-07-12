#ifndef TURBO_SETTINGS_H
#define TURBO_SETTINGS_H

#include <map>
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
    bool showHidden {false}; // include dotfiles/dot-dirs in the file tree
    // Command line for the terminal window's shell (e.g. "bash", "zsh -l").
    // Empty = use $SHELL, falling back to /bin/sh.
    std::string terminalShell;
    // Global default coding agent for the agent window, as a preset name
    // ("claude", "codex", "opencode") or a raw command line. A project may
    // override this in its .turbo/config.json. Empty = built-in default.
    std::string defaultAgent;
    // Language-server command overrides. Empty by default; the LSP manager
    // falls back to built-in defaults for languages not listed here.
    std::vector<LspServerConfig> lspServers;
    // Colour-theme overrides, stored as "<item>.<fg|bg|style>" -> value (e.g.
    // "sKeyword1.fg" -> "569CD6", "sComment.style" -> "italic"). Persisted under
    // the "theme." prefix in ~/.turborc. The mapping to/from the active colour
    // schemes lives in theme.cc; empty means "use the built-in defaults".
    std::map<std::string, std::string> theme;

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
