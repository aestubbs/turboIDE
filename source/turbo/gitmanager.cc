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

void GitManager::requestStatus() noexcept
{
    if (root.empty())
        return;
    // Coalesce: if a status is already queued and not yet consumed, skip.
    if (statusQueued.exchange(true))
        return;
    enqueue([this] { runStatus(); });
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
    return s;
}

void GitManager::commit(const std::string &message, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, message, onDone] {
        std::string out;
        int code = GitClient::commit(root, message, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out}); }
        runStatus();
    });
}

void GitManager::stage(const std::vector<std::string> &paths, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, paths, onDone] {
        std::string out;
        int code = GitClient::stage(root, paths, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out}); }
        runStatus();
    });
}

void GitManager::unstage(const std::vector<std::string> &paths, OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, paths, onDone] {
        std::string out;
        int code = GitClient::unstage(root, paths, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out}); }
        runStatus();
    });
}

void GitManager::fetch(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::fetch(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out}); }
        runStatus();
    });
}

void GitManager::pull(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::pull(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out}); }
        runStatus();
    });
}

void GitManager::push(OpCallback onDone) noexcept
{
    if (root.empty()) return;
    enqueue([this, onDone] {
        std::string out;
        int code = GitClient::push(root, out);
        { std::lock_guard<std::mutex> lock(resMx); pendingOps.push_back({onDone, code, out}); }
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
    for (auto &op : ops)
        if (op.cb)
            op.cb(op.code, op.output);
    if (status && treeWindow && treeWindow->tree)
    {
        treeWindow->tree->applyGitStatus(status->files);
        treeWindow->setBranchInfo(status->isRepo ? branchInfo(*status) : std::string());
    }
}
