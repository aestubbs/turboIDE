#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string settingsPath() noexcept
{
    const char *home = getenv("HOME");
    if (!home || !home[0])
        home = getenv("USERPROFILE"); // Windows fallback.
    if (!home || !home[0])
        return {};
    return std::string(home) + "/.turborc";
}

std::string AppSettings::lspCommandFor(const std::string &language) const noexcept
{
    for (auto &s : lspServers)
        if (s.language == language)
            return s.command;
    return {};
}

void AppSettings::setLspCommand(const std::string &language, const std::string &command) noexcept
{
    for (auto it = lspServers.begin(); it != lspServers.end(); ++it)
        if (it->language == language)
        {
            if (command.empty())
                lspServers.erase(it);
            else
                it->command = command;
            return;
        }
    if (!command.empty())
        lspServers.push_back({language, command});
}

// Strips a trailing newline (and CR) in place.
static void chomp(char *s) noexcept
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void loadSettings(AppSettings &s) noexcept
{
    auto path = settingsPath();
    if (path.empty())
        return;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[1024];
    const char serverPrefix[] = "lsp.server.";
    while (fgets(line, sizeof line, f))
    {
        int v;
        if (sscanf(line, "autosave=%d", &v) == 1)
            s.autoSaveOnFocusLoss = (v != 0);
        else if (sscanf(line, "lsp.enabled=%d", &v) == 1)
            s.lspEnabled = (v != 0);
        else if (sscanf(line, "showhidden=%d", &v) == 1)
            s.showHidden = (v != 0);
        else if (strncmp(line, serverPrefix, sizeof serverPrefix - 1) == 0)
        {
            // lsp.server.<lang>=<command>
            char *rest = line + sizeof serverPrefix - 1;
            char *eq = strchr(rest, '=');
            if (eq)
            {
                *eq = '\0';
                char *cmd = eq + 1;
                chomp(cmd);
                if (rest[0] && cmd[0])
                    s.setLspCommand(rest, cmd);
            }
        }
    }
    fclose(f);
}

void saveSettings(const AppSettings &s) noexcept
{
    auto path = settingsPath();
    if (path.empty())
        return;
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return;
    fprintf(f, "autosave=%d\n", s.autoSaveOnFocusLoss ? 1 : 0);
    fprintf(f, "lsp.enabled=%d\n", s.lspEnabled ? 1 : 0);
    fprintf(f, "showhidden=%d\n", s.showHidden ? 1 : 0);
    for (auto &srv : s.lspServers)
        if (!srv.language.empty() && !srv.command.empty())
            fprintf(f, "lsp.server.%s=%s\n", srv.language.c_str(), srv.command.c_str());
    fclose(f);
}
