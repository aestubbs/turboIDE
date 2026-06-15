#ifndef TURBO_COMMANDPALETTE_H
#define TURBO_COMMANDPALETTE_H

#include <tvision/tv.h>

#include <string>
#include <vector>

// A dynamic palette entry the app supplies at call time (e.g. one per discovered
// Lua script). 'command' is dispatched via the normal evCommand path when chosen.
struct PaletteExtra
{
    std::string label;
    std::string detail;   // right-aligned hint (may be empty)
    unsigned short command;
};

// Show the Command Palette: a fuzzy launcher over the app's command surface
// (the commands wired into the menu bar) plus any 'extra' entries. 'hasEditor' is
// true when at least one editor is open; editor-only commands are dimmed and
// sorted last when false.
//
// Returns the chosen command id to dispatch (via the normal evCommand path), or
// 0 if the user cancelled.
ushort runCommandPalette(bool hasEditor,
                         const std::vector<PaletteExtra> &extra = {}) noexcept;

#endif // TURBO_COMMANDPALETTE_H
