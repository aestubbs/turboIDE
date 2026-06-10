#ifndef TURBO_GITCLIENT_H
#define TURBO_GITCLIENT_H

#include <string>
#include <unordered_map>
#include <vector>

// Thin wrapper around the `git` command line. Pure logic, no UI: it shells out
// via turbo::Process and parses output. All calls are blocking and meant to be
// run from a worker thread (see GitManager); results are applied on the main
// thread.

// Per-file status, distilled from `git status --porcelain=v2` into a single
// display state plus a "staged" flag.
enum class GitFileState : char
{
    Clean = 0,
    Modified,   // changed in the work tree or index
    Added,      // newly added / staged add
    Deleted,
    Renamed,
    Untracked,
    Conflicted,
};

struct GitFileStatus
{
    GitFileState state {GitFileState::Clean};
    bool staged {false};
};

struct GitRepoStatus
{
    bool isRepo {false};
    std::string root;        // absolute worktree root
    std::string branch;      // current branch (or detached description)
    std::string upstream;    // upstream ref, if any
    int ahead {0};
    int behind {0};
    bool detached {false};
    bool merging {false};    // a merge is in progress (MERGE_HEAD exists)
    // Absolute path -> status, for every non-clean file.
    std::unordered_map<std::string, GitFileStatus> files;
    // Local branch short-names (refs/heads), sorted; includes the current one.
    std::vector<std::string> branches;
};

namespace GitClient
{
    // Returns the worktree root containing 'dir', or "" if not in a repo.
    std::string findRepoRoot(const std::string &dir) noexcept;

    // Runs `git status` in 'root' and fills 'out'. Returns false on failure
    // (out.isRepo will be false).
    bool status(const std::string &root, GitRepoStatus &out) noexcept;

    // Human-readable `git status` (long form) for display in the output pane,
    // captured into 'output' (combined stdout+stderr). Returns the exit code.
    // Distinct from status() above, which parses porcelain v2 for file badges.
    int statusText(const std::string &root, std::string &output) noexcept;

    // Mutations (later phases). Each returns the git exit code (0 = success) and
    // captures combined output into 'output'. Paths are absolute or repo-relative.
    int stage(const std::string &root, const std::vector<std::string> &paths,
              std::string &output) noexcept;
    int unstage(const std::string &root, const std::vector<std::string> &paths,
                std::string &output) noexcept;
    // Discard local changes: restore 'paths' to their committed (HEAD) content,
    // dropping both staged and working-tree modifications.
    int revert(const std::string &root, const std::vector<std::string> &paths,
               std::string &output) noexcept;
    int commit(const std::string &root, const std::string &message,
               std::string &output) noexcept;

    // Merge 'branch' into the current branch. 'favor': 0 = default (stop on
    // conflicts for manual resolution), 1 = -X ours, 2 = -X theirs (auto-resolve
    // text conflicts toward that side). Returns the git exit code (non-zero on
    // conflict or failure); output captures git's messages.
    int merge(const std::string &root, const std::string &branch, int favor,
              std::string &output) noexcept;
    // Abort an in-progress merge (restore the pre-merge state).
    int mergeAbort(const std::string &root, std::string &output) noexcept;
    // Conclude an in-progress merge once conflicts are resolved/staged.
    int mergeContinue(const std::string &root, std::string &output) noexcept;

    // Branch switching and stash, for the menu-bar branch dropdown.
    int checkout(const std::string &root, const std::string &branch, bool force,
                 std::string &output) noexcept;
    // Create 'branch' at HEAD and switch to it (`checkout -b`). Uncommitted
    // changes are carried over to the new branch.
    int createBranch(const std::string &root, const std::string &branch,
                     std::string &output) noexcept;
    int stashPush(const std::string &root, std::string &output) noexcept;
    int stashPop(const std::string &root, std::string &output) noexcept;

    // Remote operations. Run non-interactively (no credential prompts); rely on
    // the user's credential helper / SSH agent.
    int fetch(const std::string &root, std::string &output) noexcept;
    int pull(const std::string &root, std::string &output) noexcept;
    int push(const std::string &root, std::string &output) noexcept;
}

#endif // TURBO_GITCLIENT_H
