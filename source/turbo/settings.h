#ifndef TURBO_SETTINGS_H
#define TURBO_SETTINGS_H

// Application-wide settings, persisted to a config file in the user's home
// directory (~/.turborc) between runs.
struct AppSettings
{
    bool autoSaveOnFocusLoss {true};
};

// Read settings from the config file into 's'. Missing file/keys leave the
// corresponding defaults untouched.
void loadSettings(AppSettings &s) noexcept;

// Write 's' to the config file.
void saveSettings(const AppSettings &s) noexcept;

#endif // TURBO_SETTINGS_H
