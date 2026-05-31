#include <turbo/filewatcher.h>

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

namespace turbo {

// Shared state common to every backend: a mutex-guarded set of changed paths
// that the watcher thread fills and poll() drains.
struct FileWatcher::Impl
{
    std::mutex mx;
    std::set<std::string> pending;
    std::atomic<bool> running {false};
    std::thread thread;
    std::string root;

    void record(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mx);
        pending.insert(path);
    }

    // Backend-specific fields/teardown are defined in the platform sections.
    void platformStart();
    void platformStop();
#if defined(__APPLE__)
    void *stream {nullptr};       // FSEventStreamRef
    void *runLoop {nullptr};      // CFRunLoopRef of the watcher thread
#endif
};

FileWatcher::FileWatcher() noexcept : impl(std::make_unique<Impl>()) {}
FileWatcher::~FileWatcher() { stop(); }

void FileWatcher::start(const std::string &root) noexcept
{
    if (impl->running.exchange(true))
        return;
    impl->root = root;
    impl->platformStart();
}

void FileWatcher::stop() noexcept
{
    if (!impl->running.exchange(false))
        return;
    impl->platformStop();
}

bool FileWatcher::poll(std::vector<std::string> &out) noexcept
{
    std::lock_guard<std::mutex> lock(impl->mx);
    if (impl->pending.empty())
        return false;
    out.assign(impl->pending.begin(), impl->pending.end());
    impl->pending.clear();
    return true;
}

} // namespace turbo

// ===========================================================================
// macOS: FSEvents
// ===========================================================================
#if defined(__APPLE__)

#include <CoreServices/CoreServices.h>

namespace turbo {

static void fsCallback(ConstFSEventStreamRef, void *info, size_t numEvents,
                       void *eventPaths, const FSEventStreamEventFlags[],
                       const FSEventStreamEventId[])
{
    auto *impl = (FileWatcher::Impl *) info;
    char **paths = (char **) eventPaths;
    for (size_t i = 0; i < numEvents; ++i)
        impl->record(paths[i]);
}

void FileWatcher::Impl::platformStart()
{
    thread = std::thread([this] {
        runLoop = CFRunLoopGetCurrent();
        CFStringRef path = CFStringCreateWithCString(
            nullptr, root.c_str(), kCFStringEncodingUTF8);
        CFArrayRef paths = CFArrayCreate(
            nullptr, (const void **) &path, 1, &kCFTypeArrayCallBacks);
        FSEventStreamContext ctx {0, this, nullptr, nullptr, nullptr};
        FSEventStreamRef s = FSEventStreamCreate(
            nullptr, &fsCallback, &ctx, paths, kFSEventStreamEventIdSinceNow,
            0.2 /* latency s */,
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        stream = s;
        CFRelease(paths);
        CFRelease(path);
        if (s)
        {
            FSEventStreamScheduleWithRunLoop(s, (CFRunLoopRef) runLoop,
                                             kCFRunLoopDefaultMode);
            FSEventStreamStart(s);
            CFRunLoopRun(); // blocks until CFRunLoopStop()
            FSEventStreamStop(s);
            FSEventStreamInvalidate(s);
            FSEventStreamRelease(s);
        }
    });
}

void FileWatcher::Impl::platformStop()
{
    if (runLoop)
        CFRunLoopStop((CFRunLoopRef) runLoop);
    if (thread.joinable())
        thread.join();
    stream = nullptr;
    runLoop = nullptr;
}

} // namespace turbo

// ===========================================================================
// Linux: inotify (recursive; adds watches for new directories)
// ===========================================================================
#elif defined(__linux__)

#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#include <dirent.h>
#include <climits>
#include <cstring>
#include <unordered_map>

namespace turbo {

namespace {
int g_inotifyFd = -1;
std::unordered_map<int, std::string> g_wd2path;

void addWatchRecursive(int fd, const std::string &dir)
{
    int wd = inotify_add_watch(fd, dir.c_str(),
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd >= 0)
        g_wd2path[wd] = dir;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        // Skip the high-churn objects dir; still watch the rest of .git so
        // HEAD/index/refs changes are seen.
        std::string child = dir + "/" + e->d_name;
        bool isDir = false;
        if (e->d_type == DT_DIR) isDir = true;
        else if (e->d_type == DT_UNKNOWN)
        {
            DIR *t = opendir(child.c_str());
            if (t) { isDir = true; closedir(t); }
        }
        if (isDir)
        {
            if (std::string(e->d_name) == "objects" &&
                dir.size() >= 4 && dir.compare(dir.size() - 4, 4, ".git") == 0)
                continue;
            addWatchRecursive(fd, child);
        }
    }
    closedir(d);
}
} // namespace

void FileWatcher::Impl::platformStart()
{
    thread = std::thread([this] {
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) return;
        g_inotifyFd = fd;
        addWatchRecursive(fd, root);
        char buf[64 * 1024];
        while (running.load())
        {
            struct pollfd pfd {fd, POLLIN, 0};
            int pr = ::poll(&pfd, 1, 200);
            if (pr <= 0) continue;
            ssize_t len = ::read(fd, buf, sizeof buf);
            if (len <= 0) continue;
            for (char *p = buf; p < buf + len; )
            {
                auto *ev = (struct inotify_event *) p;
                auto it = g_wd2path.find(ev->wd);
                if (it != g_wd2path.end())
                {
                    std::string path = it->second;
                    if (ev->len > 0) { path += "/"; path += ev->name; }
                    record(path);
                    // New directory: start watching it too.
                    if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR))
                        addWatchRecursive(fd, path);
                }
                p += sizeof(struct inotify_event) + ev->len;
            }
        }
        close(fd);
        g_inotifyFd = -1;
        g_wd2path.clear();
    });
}

void FileWatcher::Impl::platformStop()
{
    if (thread.joinable())
        thread.join();
}

} // namespace turbo

// ===========================================================================
// Fallback (Windows and others): recursive mtime polling
// ===========================================================================
#else

#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace turbo {

void FileWatcher::Impl::platformStart()
{
    thread = std::thread([this] {
        namespace fs = std::filesystem;
        std::unordered_map<std::string, long long> snapshot;
        bool first = true;
        while (running.load())
        {
            std::unordered_map<std::string, long long> current;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(
                     root, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                const auto &p = it->path();
                auto name = p.filename().string();
                if (!name.empty() && name[0] == '.' && it->is_directory(ec))
                {
                    if (name != ".git") { it.disable_recursion_pending(); continue; }
                }
                auto t = fs::last_write_time(p, ec);
                long long ticks = ec ? 0 : (long long) t.time_since_epoch().count();
                current[p.string()] = ticks;
            }
            if (!first)
            {
                for (auto &kv : current)
                {
                    auto old = snapshot.find(kv.first);
                    if (old == snapshot.end() || old->second != kv.second)
                        record(kv.first);
                }
                for (auto &kv : snapshot)
                    if (current.find(kv.first) == current.end())
                        record(kv.first); // deleted
            }
            snapshot.swap(current);
            first = false;
            for (int i = 0; i < 10 && running.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void FileWatcher::Impl::platformStop()
{
    if (thread.joinable())
        thread.join();
}

} // namespace turbo

#endif
