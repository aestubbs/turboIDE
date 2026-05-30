#include "settings.h"

#include <cstdio>
#include <cstdlib>
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

void loadSettings(AppSettings &s) noexcept
{
    auto path = settingsPath();
    if (path.empty())
        return;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        int v;
        if (sscanf(line, "autosave=%d", &v) == 1)
            s.autoSaveOnFocusLoss = (v != 0);
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
    fclose(f);
}
