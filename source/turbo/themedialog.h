#ifndef TURBO_THEMEDIALOG_H
#define TURBO_THEMEDIALOG_H

// Shows the colour-scheme dialog. It edits a working copy of the active syntax
// and window-chrome schemes; on Apply/OK it copies the working values into the
// active schemes and posts cmApplyTheme to the application (which re-themes the
// open editors and persists the result). Returns true if accepted (OK).
bool executeThemeDialog() noexcept;

#endif // TURBO_THEMEDIALOG_H
