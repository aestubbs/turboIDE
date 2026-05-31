#ifndef TURBO_FILEWATCHER_H
#define TURBO_FILEWATCHER_H

#include <memory>
#include <string>
#include <vector>

namespace turbo {

// Watches a directory tree for changes and exposes a coalesced set of changed
// absolute paths. Detection runs on a background thread; the consumer drains
// events on its own thread via poll() (e.g. from the app's idle loop), so no
// callback ever touches the UI directly.
//
// Backends: FSEvents (macOS), inotify (Linux), and a portable mtime-polling
// fallback (Windows and anything else). All present the same interface.
class FileWatcher
{
public:
    FileWatcher() noexcept;
    ~FileWatcher();
    FileWatcher(const FileWatcher &) = delete;
    FileWatcher &operator=(const FileWatcher &) = delete;

    // Begin watching 'root' (recursively). Safe to call once.
    void start(const std::string &root) noexcept;
    // Stop watching and join the worker thread.
    void stop() noexcept;

    // Drain the changed paths accumulated since the last poll. Returns true if
    // 'out' was filled with at least one path. Cheap; call every idle tick.
    bool poll(std::vector<std::string> &out) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace turbo

#endif // TURBO_FILEWATCHER_H
