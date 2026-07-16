#ifndef TURBO_DEBUGDIALOG_H
#define TURBO_DEBUGDIALOG_H

struct DebugConfig;

// Per-project debug-adapter settings dialog. Presents the adapter command and
// request (launch/attach) per language, seeded from 'config' and -- on OK --
// written back into it. Returns true if the user accepted; the caller persists
// (.turbo/debug.json) and re-applies the result.
bool executeDebugDialog(DebugConfig &config) noexcept;

#endif // TURBO_DEBUGDIALOG_H
