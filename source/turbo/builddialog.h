#ifndef TURBO_BUILDDIALOG_H
#define TURBO_BUILDDIALOG_H

#include <vector>

struct BuildConfig;
struct BuildCommand;

// Show the build-configuration dialog seeded from 'config'. On OK, writes the
// edited build/test/run/artifact/mode values back into 'config' and returns true
// (the caller persists it to .turbo/config.json); on Cancel, leaves 'config'
// untouched and returns false. The tool-process list is edited separately, via
// executeToolsDialog.
bool executeBuildDialog(BuildConfig &config) noexcept;

// Show the tool-processes dialog seeded from 'tools' (the configured long-running
// commands toggled from the Run menu). On OK, writes the edited list back into
// 'tools' and returns true; on Cancel, leaves it untouched and returns false.
bool executeToolsDialog(std::vector<BuildCommand> &tools) noexcept;

#endif // TURBO_BUILDDIALOG_H
