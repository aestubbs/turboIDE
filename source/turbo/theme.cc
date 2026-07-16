#define Uses_TColorAttr
#define Uses_TScreen
#include <tvision/tv.h>

#include "theme.h"
#include "settings.h"

#include <turbo/styles.h>
#include <turbo/basicwindow.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace turbo;

// The window-chrome entries the dialog lets the user edit. Kept to the parts
// that are actually visible on an editor window; the rest of the window scheme
// keeps its built-in values.
const ChromeItem chromeItems[] =
{
    {"frameActive",  "Window frame (active)",   wndFrameActive},
    {"framePassive", "Window frame (inactive)", wndFramePassive},
    {"frameIcon",    "Frame icons",             wndFrameIcon},
    {"scrollbarBar", "Scrollbar",               wndScrollBarPageArea},
    {"scrollbarCtl", "Scrollbar arrows",        wndScrollBarControls},
};
const int chromeItemCount = sizeof(chromeItems) / sizeof(chromeItems[0]);

bool parseHexColor(const std::string &str, TColorRGB &out) noexcept
{
    // Trim surrounding whitespace and an optional leading '#'.
    size_t a = str.find_first_not_of(" \t");
    if (a == std::string::npos)
        return false;
    size_t b = str.find_last_not_of(" \t");
    std::string s = str.substr(a, b - a + 1);
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6)
        return false;
    for (char ch : s)
        if (!std::isxdigit((unsigned char) ch))
            return false;
    unsigned long v = std::strtoul(s.c_str(), nullptr, 16);
    out = TColorRGB((uint32_t) v);
    return true;
}

std::string formatColor(TColorDesired c) noexcept
{
    if (c.isDefault())
        return "default";
    if (c.isRGB())
    {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%06X", (unsigned) ((uint32_t) c.asRGB() & 0xFFFFFF));
        return buf;
    }
    // Editable items are always RGB or default; anything else (a BIOS/XTerm
    // colour) has no stable hex form here, so treat it as "inherit".
    return "default";
}

TColorDesired parseColor(const std::string &s) noexcept
{
    if (s.empty() || s == "default")
        return {};
    TColorRGB rgb;
    if (parseHexColor(s, rgb))
        return TColorDesired(rgb);
    return {};
}

// --- Style flag (bold/italic/underline) text encoding ----------------------

static ushort parseStyleFlags(const std::string &s) noexcept
{
    ushort f = 0;
    if (s.find("bold") != std::string::npos)      f |= slBold;
    if (s.find("italic") != std::string::npos)    f |= slItalic;
    if (s.find("underline") != std::string::npos) f |= slUnderline;
    return f;
}

static std::string formatStyleFlags(ushort f) noexcept
{
    std::string r;
    if (f & slBold)      r += "bold,";
    if (f & slItalic)    r += "italic,";
    if (f & slUnderline) r += "underline,";
    if (!r.empty())
        r.pop_back(); // drop trailing comma
    return r;
}

// --- Settings <-> active scheme --------------------------------------------

bool useClassicColors(const AppSettings &settings) noexcept
{
    if (settings.colorMode == "16")
        return true;
    if (settings.colorMode == "full")
        return false;
    // "auto":
#ifdef _WIN32
    // The Windows console reports truecolor whenever VT processing is on, even
    // though cmd.exe/PowerShell render it poorly, so screenMode can't tell a
    // good terminal from a bad one. Default to the classic fallback unless the
    // terminal explicitly advertises truecolor via COLORTERM (Windows Terminal,
    // VS Code, ...), which renders it well. Kept in lock-step with the start-up
    // cap in applyColorDepthPreference().
    return !terminalAdvertisesTrueColor();
#else
    // Elsewhere, classic only when the terminal genuinely lacks 256/true colour.
    return !(TScreen::screenMode & (TScreen::smColor256 | TScreen::smColorHigh));
#endif
}

void applyThemeFromSettings(const AppSettings &settings) noexcept
{
    if (useClassicColors(settings))
    {
        // Classic 16-colour fallback: use the hand-authored BIOS schemes and
        // ignore any saved RGB per-item overrides, which would re-introduce
        // 24-bit colour and defeat the fallback. (The colour dialog remains a
        // full-colour feature.)
        resetSchemeToClassic();
        resetWindowSchemeToClassic();
        return;
    }

    // Start from the factory defaults, then layer any saved overrides on top so
    // unspecified attributes keep their built-in values.
    resetSchemeToDefault();
    resetWindowSchemeToDefault();

    auto get = [&] (const std::string &key) -> const std::string * {
        auto it = settings.theme.find(key);
        return it == settings.theme.end() ? nullptr : &it->second;
    };

    for (int i = 0; i < TextStyleCount; ++i)
    {
        std::string id = styleName((TextStyle) i);
        TColorAttr &a = schemeActive[i];
        if (auto *v = get(id + ".fg"))    ::setFore(a, parseColor(*v));
        if (auto *v = get(id + ".bg"))    ::setBack(a, parseColor(*v));
        if (auto *v = get(id + ".style")) ::setStyle(a, parseStyleFlags(*v));
    }

    for (int i = 0; i < chromeItemCount; ++i)
    {
        std::string id = std::string("chrome.") + chromeItems[i].id;
        TColorAttr &a = windowSchemeActive[chromeItems[i].index];
        if (auto *v = get(id + ".fg")) ::setFore(a, parseColor(*v));
        if (auto *v = get(id + ".bg")) ::setBack(a, parseColor(*v));
    }
}

void storeThemeToSettings(AppSettings &settings) noexcept
{
    settings.theme.clear();

    for (int i = 0; i < TextStyleCount; ++i)
    {
        std::string id = styleName((TextStyle) i);
        const TColorAttr &a = schemeActive[i];
        const TColorAttr &d = schemeDefault[i];
        if (::getFore(a) != ::getFore(d))   settings.theme[id + ".fg"]    = formatColor(::getFore(a));
        if (::getBack(a) != ::getBack(d))   settings.theme[id + ".bg"]    = formatColor(::getBack(a));
        if (::getStyle(a) != ::getStyle(d)) settings.theme[id + ".style"] = formatStyleFlags(::getStyle(a));
    }

    for (int i = 0; i < chromeItemCount; ++i)
    {
        std::string id = std::string("chrome.") + chromeItems[i].id;
        const TColorAttr &a = windowSchemeActive[chromeItems[i].index];
        const TColorAttr &d = windowSchemeDefault[chromeItems[i].index];
        if (::getFore(a) != ::getFore(d)) settings.theme[id + ".fg"] = formatColor(::getFore(a));
        if (::getBack(a) != ::getBack(d)) settings.theme[id + ".bg"] = formatColor(::getBack(a));
    }
}
