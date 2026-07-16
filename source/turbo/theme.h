#ifndef TURBO_THEME_H
#define TURBO_THEME_H

#define Uses_TColorAttr
#include <tvision/tv.h>

#include <string>

struct AppSettings;

// Editable window-chrome entries exposed by the theme dialog. 'id' is the stable
// key persisted in settings (under "theme.chrome.<id>"); 'index' is the
// WindowPaletteItems slot it maps to in the active window scheme.
struct ChromeItem
{
    const char *id;
    const char *label;
    int index;
};

extern const ChromeItem chromeItems[];
extern const int chromeItemCount;

// Format a colour for storage/display: "default" when unset (inherited), else a
// 6-digit hex string "RRGGBB". 'parseColor' is the inverse: an empty, "default"
// or malformed value yields a default (inherited) colour. 'parseHexColor'
// returns false on anything that isn't 6 hex digits (a leading '#' is allowed).
std::string formatColor(TColorDesired c) noexcept;
TColorDesired parseColor(const std::string &s) noexcept;
bool parseHexColor(const std::string &s, TColorRGB &out) noexcept;

// True when the low-colour (classic 16-colour BIOS) fallback should be used:
// colorMode "16" always, "full" never, "auto" only when the terminal reports no
// 256/true-colour support (e.g. the Windows legacy console).
bool useClassicColors(const AppSettings &settings) noexcept;

// Load persisted overrides (settings.theme) onto the built-in defaults, leaving
// the result in the active syntax + window schemes. Unspecified entries keep
// their factory defaults. In classic 16-colour mode the BIOS schemes are used
// instead and per-item RGB overrides are ignored.
void applyThemeFromSettings(const AppSettings &settings) noexcept;

// Serialize the active syntax + window schemes into settings.theme. Only entries
// that differ from the factory defaults are written, so the config stays small
// and future default changes still reach users who didn't override that entry.
void storeThemeToSettings(AppSettings &settings) noexcept;

#endif // TURBO_THEME_H
