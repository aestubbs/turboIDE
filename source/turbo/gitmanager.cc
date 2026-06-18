#include "gitmanager.h"
#include "doctree.h"

#include <cstdio>

GitManager::GitManager() noexcept
{
    worker = std::thread(&GitManager::workerLoop, this);
}

GitManager::~GitManager()
{
    shutdown();
}

void GitManager::shutdown() noexcept
{
    {
        std::lock_guard<std::mutex> lock(taskMx);
        if (stopping)
            return;
        stopping = true;
    }
    taskCv.notify_all();
    if (worker.joinable())
        worker.join();
}

void GitManager::enqueue(std::function<void()> task) noexcept
{
    {
        std::lock_guard<std::mutex> lock(taskMx);
        tasks.push(std::move(task));
    }
    taskCv.notify_one();
}

void GitManager::workerLoop() noexcept
{
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(taskMx);
            taskCv.wait(lock, [this] { return stopping || !tasks.empty(); });
            if (stopping && tasks.empty())
                return;
            task = std::move(tasks.front());
            tasks.pop();
        }
        task();
    }
}

void GitManager::setWorkspace(const char *dir) noexcept
{
    if (!dir || !dir[0])
        return;
    std::string d = dir;
    enqueue([this, d] {
        std::string r = GitClient::findRepoRoot(d);
        if (!r.empty())
        {
            root = r; // worker-only until first status; safe (single worker)
            runStatus();
        }
    });
}

void GitManager::clearWorkspace() noexcept
{
    // Clear the root on the worker thread (the only writer of 'root', mirroring
    // setWorkspace) and queue an empty status for pump() to apply: that wipes the
    // tree's git badges and resets the branch indicator to "no repo".
    enqueue([this] {
        root.clear();
        std::lock_guard<std::mutex> lk(resMx);
        pendingStatus = GitRepoStatus {}; // isRepo == false
    });
}

void GitManager::requestStatus() noexcept
{
    if (root.empty())
        return;
    // Coalesce: if a status is already queued and not yet consumed, skip.
    if (statusQueued.exchange(true))
        return;
    enqueue([this] { runStatus(); });
}

void GitManager::statusToOutput() noexcept
{
    if (root.empty())
        return;
    enqueue([this] {
        std::string out;
        int code = GitClient::statusText(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({{}, code, out, "git status"}); }
        runStatus(); // also refresh the file badges / branch info from porcelain
    });
}

void GitManager::runStatus() noexcept
{
    statusQueued.store(false);
    GitRepoStatus st;
    GitClient::status(root, st);
    std::lock_guard<std::mutex> lock(resMx);
    pendingStatus = std::move(st);
}

std::string GitManager::branchInfo(const GitRepoStatus &st) const
{
    std::string s = st.branch.empty() ? std::string("(no branch)") : st.branch;
    if (st.ahead || st.behind)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, " +%d-%d", st.ahead, st.behind);
        s += buf;
    }
    if (st.merging)
        s += " | MERGING";
    return s;
}

void GitManager::revert(const std::vector<std::string> &paths, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, paths, onDone] {
        std::string out;
        int code = GitClient::revert(root, paths, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git restore"}); }
        runStatus();
    });
}

void GitManager::switchBranch(const std::string &branch, SwitchMode mode,
                              OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, branch, mode, onDone] {
        std::string out;
        int code;
        if (mode == SwitchMode::Stash)
        {
            // Set local changes aside, switch, then re-apply them on the new
            // branch (the usual "move my WIP" flow). If the checkout fails, the
            // pop restores the changes onto the branch we never left.
            int stashCode = GitClient::stashPush(root, out);
            if (stashCode != 0)
                code = stashCode; // nothing stashed / stash failed: report it
            else
            {
                code = GitClient::checkout(root, branch, false, out);
                std::string popOut;
                GitClient::stashPop(root, popOut);
                out += popOut;
            }
        }
        else
            code = GitClient::checkout(root, branch,
                                       mode == SwitchMode::Force, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git checkout " + branch}); }
        runStatus(); // refreshes file badges, current branch and branch list
    });
}

void GitManager::createBranch(const std::string &branch, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, branch, onDone] {
        std::string out;
        int code = GitClient::createBranch(root, branch, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git checkout -b " + branch}); }
        runStatus(); // refreshes the current branch and branch list
    });
}

void GitManager::commit(const std::string &message, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, message, onDone] {
        std::string out;
        int code = GitClient::commit(root, message, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git commit"}); }
        runStatus();
    });
}

void GitManager::stage(const std::vector<std::string> &paths, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, paths, onDone] {
        std::string out;
        int code = GitClient::stage(root, paths, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git add"}); }
        runStatus();
    });
}

void GitManager::unstage(const std::vector<std::string> &paths, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, paths, onDone] {
        std::string out;
        int code = GitClient::unstage(root, paths, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git restore --staged"}); }
        runStatus();
    });
}

void GitManager::merge(const std::string &branch, MergeFavor favor,
                       OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, branch, favor, onDone] {
        std::string out;
        int fav = favor == MergeFavor::Ours ? 1 : favor == MergeFavor::Theirs ? 2 : 0;
        int code = GitClient::merge(root, branch, fav, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git merge " + branch}); }
        runStatus();
    });
}

void GitManager::mergeAbort(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::mergeAbort(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git merge --abort"}); }
        runStatus();
    });
}

void GitManager::mergeContinue(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::mergeContinue(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git merge --continue"}); }
        runStatus();
    });
}

void GitManager::fetch(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::fetch(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git fetch"}); }
        runStatus();
    });
}

void GitManager::pull(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::pull(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git pull"}); }
        runStatus();
    });
}

void GitManager::push(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::push(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out, "git push"}); }
        runStatus();
    });
}

void GitManager::pump(DocumentTreeWindow *treeWindow) noexcept
{
    std::optional<GitRepoStatus> status;
    std::vector<OpResult> ops;
    {
        std::lock_guard<std::mutex> lock(resMx);
        status.swap(pendingStatus);
        ops.swap(pendingOps);
    }
    // Deliver op results first (they may show messages), then refresh the view.
    // Route each labelled op's output to the sink (the output pane's GIT tab) so
    // git's normally-swallowed output is visible, then run any per-call callback.
    for (auto &op : ops)
    {
        if (outputSink && !op.label.empty())
            outputSink(op.label, op.code, op.output);
        if (op.cb)
            op.cb(op.code, op.output);
    }
    if (status)
    {
        // Update the main-thread cache BEFORE touching the tree, and regardless
        // of whether a tree window exists: currentStatus() feeds the commit
        // dialog, which otherwise always saw an empty file list ("nothing to
        // commit") because lastStatus was never assigned here.
        lastStatus = std::move(*status);
        if (treeWindow && treeWindow->tree)
        {
            treeWindow->tree->applyGitStatus(lastStatus.files);
            treeWindow->setBranchInfo(
                lastStatus.isRepo ? branchInfo(lastStatus) : std::string());
        }
    }
}
