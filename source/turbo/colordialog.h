#ifndef TURBO_COLORDIALOG_H
#define TURBO_COLORDIALOG_H

#define Uses_TColorAttr
#include <tvision/tv.h>

// Modal colour picker. Combines a 256-colour swatch grid (quick picks) with a
// 6-digit hex field for any 24-bit value, plus a live preview. When
// 'allowDefault' is true, a checkbox lets the user choose "default" (inherit).
//
// On OK it writes the chosen colour into 'color' and returns true; on Cancel it
// returns false and leaves 'color' untouched.
bool pickColor(const char *title, TColorDesired &color, bool allowDefault) noexcept;

#endif // TURBO_COLORDIALOG_H
