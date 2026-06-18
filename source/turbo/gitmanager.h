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
    // Forget the current repo: clears the root and pushes an empty (non-repo)
    // status so pump() clears the tree badges and branch indicator. Used when a
    // project is closed.
    void clearWorkspace() noexcept;
    bool isRepo() const noexcept { return !root.empty(); }
    const std::string &repoRoot() const noexcept { return root; }

    // Queue a status refresh (cheap to call repeatedly; coalesced). Silent:
    // used for automatic/background refreshes (file changes, after mutations).
    void requestStatus() noexcept;

    // Like requestStatus(), but also echoes a human-readable `git status` to the
    // output sink. For the explicit "Refresh Status" menu command, so it gives
    // visible feedback like the other Git-menu commands.
    void statusToOutput() noexcept;

    // A sink for the combined output of every git command (commit, fetch, pull,
    // push, stage, unstage, revert, branch switch), invoked on the main thread
    // from pump() with a short command label (e.g. "git push"), the exit code,
    // and the captured stdout+stderr. The app wires this to the output pane's GIT
    // tab -- a continuous log -- so git's normally-swallowed output is visible
    // and every action is acknowledged. Status refreshes are NOT routed here
    // (they are internal and parsed for badges, not user-issued commands).
    using OutputSink = std::function<void(const std::string &label, int code,
                                          const std::string &output)>;
    void setOutputSink(OutputSink sink) noexcept { outputSink = std::move(sink); }

    // The most recent status applied on the main thread (updated by pump()).
    const GitRepoStatus &currentStatus() const noexcept { return lastStatus; }

    // Mutations / remote ops. 'onDone' (optional) runs on the main thread during
    // pump() with the git exit code and captured output; a status refresh is
    // always queued afterwards so the tree updates.
    using OpCallback = std::function<void(int code, const std::string &output)>;
    void commit(const std::string &message, OpCallback onDone = {}) noexcept;
    void stage(const std::vector<std::string> &paths, OpCallback onDone = {}) noexcept;
    void unstage(const std::vector<std::string> &paths, OpCallback onDone = {}) noexcept;
    void revert(const std::vector<std::string> &paths, OpCallback onDone = {}) noexcept;

    // How to deal with uncommitted changes when switching branch.
    enum class SwitchMode {
        Plain,  // plain `git checkout` (fails if it would clobber local changes)
        Stash,  // stash, checkout, then pop the stash onto the new branch
        Force,  // `git checkout --force` (discards local changes)
    };
    // Switch to 'branch'. 'onDone' reports the checkout's exit code/output.
    void switchBranch(const std::string &branch, SwitchMode mode,
                      OpCallback onDone = {}) noexcept;
    // Create 'branch' at HEAD and switch to it (uncommitted changes come along).
    void createBranch(const std::string &branch, OpCallback onDone = {}) noexcept;
    void fetch(OpCallback onDone = {}) noexcept;
    void pull(OpCallback onDone = {}) noexcept;
    void push(OpCallback onDone = {}) noexcept;

    // Merge 'branch' into the current branch. 'favor' picks the conflict
    // strategy (None = manual, Ours/Theirs = -X ours/theirs auto-resolve).
    enum class MergeFavor { None, Ours, Theirs };
    void merge(const std::string &branch, MergeFavor favor,
               OpCallback onDone = {}) noexcept;
    void mergeAbort(OpCallback onDone = {}) noexcept;     // restore pre-merge state
    void mergeContinue(OpCallback onDone = {}) noexcept;  // commit the resolved merge

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

    OutputSink outputSink; // main-thread only (set once at startup, read in pump())

    // Worker -> main results.
    std::mutex resMx;
    std::optional<GitRepoStatus> pendingStatus;
    // 'label' is the echoed header for the output sink (e.g. "git push"). An
    // empty label means the op is not routed to the sink.
    struct OpResult { OpCallback cb; int code; std::string output; std::string label; };
    std::vector<OpResult> pendingOps;
};

#endif // TURBO_GITMANAGER_H
