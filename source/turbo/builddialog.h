#ifndef TURBO_BUILDDIALOG_H
#define TURBO_BUILDDIALOG_H

struct BuildConfig;

// Show the build-configuration dialog seeded from 'config'. On OK, writes the
// edited values back into 'config' and returns true (the caller persists it to
// .turbo/config.json); on Cancel, leaves 'config' untouched and returns false.
bool executeBuildDialog(BuildConfig &config) noexcept;

#endif // TURBO_BUILDDIALOG_H
