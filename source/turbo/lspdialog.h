#ifndef TURBO_LSPDIALOG_H
#define TURBO_LSPDIALOG_H

struct AppSettings;

// Shows the "Language Servers" settings dialog, editing 'settings' in place.
// Returns true if the user accepted changes (OK), false on Cancel.
bool executeLspDialog(AppSettings &settings) noexcept;

#endif // TURBO_LSPDIALOG_H
