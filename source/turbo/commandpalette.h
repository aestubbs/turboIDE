#ifndef TURBO_COMMANDPALETTE_H
#define TURBO_COMMANDPALETTE_H

#include <tvision/tv.h>

// Show the Command Palette: a fuzzy launcher over the app's command surface
// (the commands wired into the menu bar). 'hasEditor' is true when at least one
// editor is open; editor-only commands are dimmed and sorted last when false.
//
// Returns the chosen command id to dispatch (via the normal evCommand path), or
// 0 if the user cancelled.
ushort runCommandPalette(bool hasEditor) noexcept;

#endif // TURBO_COMMANDPALETTE_H
