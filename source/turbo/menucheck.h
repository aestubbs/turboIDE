#ifndef TURBO_MENUCHECK_H
#define TURBO_MENUCHECK_H

class TMenu;

// Turbo Vision menu items have no native "checked" state (TMenuItem only carries
// name/command/disabled/key/param), so a check mark is shown by rewriting the
// item's label. setMenuItemCheck() finds the item with the given command in the
// menu tree and gives it a "<check> " prefix when checked, or two leading spaces
// when not, so labels stay aligned either way. It is a no-op if the command is
// not found. Safe to call repeatedly; only touches the label when it changes.
void setMenuItemCheck(TMenu *menu, unsigned short command, bool checked) noexcept;

// Set the label and enabled state of the menu item with the given command,
// used for the dynamic recent-windows list. Replaces the owned name string only
// when it actually changes. No-op if the command is not found.
void setMenuItemLabel(TMenu *menu, unsigned short command,
                      const char *label, bool enabled) noexcept;

#endif // TURBO_MENUCHECK_H
