#include "gitclient.h"

#include <turbo/process.h>

#include <cstring>

namespace {

// The environment that makes git safe to run under a TUI: never prompt on the
// terminal (which would fight our event loop), and don't page output.
std::vector<std::string> nonInteractiveEnv()
{
    return {
        "GIT_TERMINAL_PROMPT=0", // never ask for credentials on the tty
        "GIT_PAGER=cat",
        "GIT_OPTIONAL_LOCKS=0",  // status must not take the index.lock
    };
}

// Map a porcelain v2 XY pair to a single display state + staged flag.
// X = staged/index status, Y = worktree status. '.' means unchanged.
GitFileStatus deriveStatus(char x, char y) noexcept
{
    GitFileStatus s;
    s.staged = (x != '.' && x != '?' && x != ' ');
    char eff = (y != '.' && y != ' ') ? y : x; // prefer the worktree change
    switch (eff)
    {
        case 'M': case 'T': s.state = GitFileState::Modified; break;
        case 'A':           s.state = GitFileState::Added; break;
        case 'D':           s.state = GitFileState::Deleted; break;
        case 'R': case 'C': s.state = GitFileState::Renamed; break;
        default:            s.state = GitFileState::Modified; break;
    }
    return s;
}

// Split a NUL-delimited buffer into records.
std::vector<std::string> splitNul(const std::string &buf)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < buf.size(); ++i)
        if (buf[i] == '\0')
        {
            out.emplace_back(buf, start, i - start);
            start = i + 1;
        }
    if (start < buf.size())
        out.emplace_back(buf, start, buf.size() - start);
    return out;
}

std::string trimNewline(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

} // namespace

namespace GitClient {

std::string findRepoRoot(const std::string &dir) noexcept
{
    std::string out;
    int rc = turbo::Process::runToEnd(
        "git", {"rev-parse", "--show-toplevel"}, out, dir, nonInteractiveEnv());
    if (rc != 0)
        return {};
    return trimNewline(std::move(out));
}

bool status(const std::string &root, GitRepoStatus &out) noexcept
{
    out = GitRepoStatus {};
    if (root.empty())
        return false;
    out.root = root;

    std::string buf;
    int rc = turbo::Process::runToEnd(
        "git",
        {"status", "--porcelain=v2", "--branch", "-z", "--untracked-files=all"},
        buf, root, nonInteractiveEnv());
    if (rc != 0)
        return false;
    out.isRepo = true;

    auto records = splitNul(buf);
    for (size_t i = 0; i < records.size(); ++i)
    {
        const std::string &r = records[i];
        if (r.empty())
            continue;
        char kind = r[0];
        if (kind == '#')
        {
            // "# branch.head <name>" etc.
            if (r.rfind("# branch.head ", 0) == 0)
            {
                out.branch = r.substr(14);
                out.detached = (out.branch == "(detached)");
            }
            else if (r.rfind("# branch.upstream ", 0) == 0)
                out.upstream = r.substr(18);
            else if (r.rfind("# branch.ab ", 0) == 0)
            {
                // "# branch.ab +N -M"
                int a = 0, b = 0;
                std::sscanf(r.c_str() + 12, "%d %d", &a, &b);
                out.ahead = a;
                out.behind = (b < 0) ? -b : b;
            }
        }
        else if (kind == '1' || kind == '2')
        {
            // "1 XY sub mH mI mW hH hI path"
            // "2 XY sub mH mI mW hH hI Xscore path"   (rename; origPath in next record)
            if (r.size() < 4)
                continue;
            char x = r[2], y = r[3];
            // Find the path: skip the fixed fields (8 for '1', 9 for '2').
            int fieldsToSkip = (kind == '1') ? 8 : 9;
            size_t pos = 0;
            for (int f = 0; f < fieldsToSkip && pos != std::string::npos; ++f)
                pos = r.find(' ', pos == 0 ? 0 : pos + 1);
            if (pos == std::string::npos)
                continue;
            std::string rel = r.substr(pos + 1);
            GitFileStatus st = deriveStatus(x, y);
            out.files[root + "/" + rel] = st;
            if (kind == '2' && i + 1 < records.size())
                ++i; // consume the original-path record for renames
        }
        else if (kind == 'u')
        {
            // Unmerged: "u XY sub m1 m2 m3 mW h1 h2 h3 path" (10 fields).
            size_t pos = 0;
            for (int f = 0; f < 10 && pos != std::string::npos; ++f)
                pos = r.find(' ', pos == 0 ? 0 : pos + 1);
            if (pos == std::string::npos)
                continue;
            std::string rel = r.substr(pos + 1);
            out.files[root + "/" + rel] = {GitFileState::Conflicted, false};
        }
        else if (kind == '?')
        {
            // "? path"
            std::string rel = r.substr(2);
            out.files[root + "/" + rel] = {GitFileState::Untracked, false};
        }
        // '!' (ignored) is not requested, so it won't appear.
    }

    // Local branch names (cheap local op), for the menu-bar branch dropdown.
    std::string brBuf;
    if (turbo::Process::runToEnd(
            "git", {"for-each-ref", "--format=%(refname:short)", "refs/heads"},
            brBuf, root, nonInteractiveEnv()) == 0)
    {
        size_t start = 0;
        for (size_t i = 0; i <= brBuf.size(); ++i)
            if (i == brBuf.size() || brBuf[i] == '\n')
            {
                if (i > start)
                    out.branches.emplace_back(brBuf, start, i - start);
                start = i + 1;
            }
    }
    return true;
}

// --- Mutations -------------------------------------------------------------

int stage(const std::string &root, const std::vector<std::string> &paths,
          std::string &output) noexcept
{
    std::vector<std::string> args = {"add", "--"};
    for (auto &p : paths) args.push_back(p);
    return turbo::Process::runToEnd("git", args, output, root, nonInteractiveEnv());
}

int unstage(const std::string &root, const std::vector<std::string> &paths,
            std::string &output) noexcept
{
    std::vector<std::string> args = {"restore", "--staged", "--"};
    for (auto &p : paths) args.push_back(p);
    return turbo::Process::runToEnd("git", args, output, root, nonInteractiveEnv());
}

int revert(const std::string &root, const std::vector<std::string> &paths,
           std::string &output) noexcept
{
    // `checkout HEAD -- <paths>` rewrites both the index and the working tree
    // from the last commit, so it discards staged AND unstaged changes in one
    // step (and restores a file deleted in the work tree).
    std::vector<std::string> args = {"checkout", "HEAD", "--"};
    for (auto &p : paths) args.push_back(p);
    return turbo::Process::runToEnd("git", args, output, root, nonInteractiveEnv());
}

int checkout(const std::string &root, const std::string &branch, bool force,
             std::string &output) noexcept
{
    std::vector<std::string> args = {"checkout"};
    if (force)
        args.push_back("--force");
    args.push_back(branch);
    return turbo::Process::runToEnd("git", args, output, root, nonInteractiveEnv());
}

int stashPush(const std::string &root, std::string &output) noexcept
{
    return turbo::Process::runToEnd(
        "git", {"stash", "push"}, output, root, nonInteractiveEnv());
}

int stashPop(const std::string &root, std::string &output) noexcept
{
    return turbo::Process::runToEnd(
        "git", {"stash", "pop"}, output, root, nonInteractiveEnv());
}

int commit(const std::string &root, const std::string &message,
           std::string &output) noexcept
{
    return turbo::Process::runToEnd(
        "git", {"commit", "-m", message}, output, root, nonInteractiveEnv());
}

int fetch(const std::string &root, std::string &output) noexcept
{
    return turbo::Process::runToEnd("git", {"fetch"}, output, root, nonInteractiveEnv());
}

int pull(const std::string &root, std::string &output) noexcept
{
    return turbo::Process::runToEnd(
        "git", {"pull", "--ff-only"}, output, root, nonInteractiveEnv());
}

int push(const std::string &root, std::string &output) noexcept
{
    return turbo::Process::runToEnd("git", {"push"}, output, root, nonInteractiveEnv());
}

} // namespace GitClient
