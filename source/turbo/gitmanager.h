#ifndef TURBO_GITMANAGER_H
#define TURBO_GITMANAGER_H

#include "gitclient.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct DocumentTreeWindow;

// Owns a single worker thread that runs git commands off the main thread, and
// applies their results to the UI from pump() (called in the app's idle loop) --
// the same pattern as LspManager. git is never run on the main thread, so a slow
// status on a big repo or a blocking network op cannot freeze the editor.
class GitManager
{
public:
    GitManager() noexcept;
    ~GitManager();

    // Discover the repo root containing 'dir' (off-thread). If found, kicks off
    // an initial status. No-op if 'dir' is not in a git repo.
    void setWorkspace(const char *dir) noexcept;
    bool isRepo() const noexcept { return !root.empty(); }
    const std::string &repoRoot() const noexcept { return root; }

    // Queue a status refresh (cheap to call repeatedly; coalesced).
    void requestStatus() noexcept;

    // The most recent status applied on the main thread (updated by pump()).
    const GitRepoStatus &currentStatus() const noexcept { return lastStatus; }

    // Mutations / remote ops. 'onDone' (optional) runs on the main thread during
    // pump() with the git exit code and captured output; a status refresh is
    // always queued afterwards so the tree updates.
    using OpCallback = std::function<void(int code, const std::string &output)>;
    void commit(const std::string &message, OpCallback onDone = {}) noexcept;
    void stage(const std::vector<std::string> &paths, OpCallback onDone = {}) noexcept;
    void unstage(const std::vector<std::string> &paths, OpCallback onDone = {}) noexcept;
    void fetch(OpCallback onDone = {}) noexcept;
    void pull(OpCallback onDone = {}) noexcept;
    void push(OpCallback onDone = {}) noexcept;

    // Apply any ready results to the tree window. Cheap; call every idle tick.
    void pump(DocumentTreeWindow *treeWindow) noexcept;

    // Join the worker thread. Call on shutdown.
    void shutdown() noexcept;

private:
    void enqueue(std::function<void()> task) noexcept;
    void workerLoop() noexcept;
    void runStatus() noexcept;            // worker side
    std::string branchInfo(const GitRepoStatus &) const; // "main +1-2"

    std::string root;
    GitRepoStatus lastStatus; // main-thread cache, updated in pump()

    std::thread worker;
    std::mutex taskMx;
    std::condition_variable taskCv;
    std::queue<std::function<void()>> tasks;
    bool stopping {false};
    std::atomic<bool> statusQueued {false};

    // Worker -> main results.
    std::mutex resMx;
    std::optional<GitRepoStatus> pendingStatus;
    struct OpResult { OpCallback cb; int code; std::string output; };
    std::vector<OpResult> pendingOps;
};

#endif // TURBO_GITMANAGER_H
