#ifndef TURBO_GITDIALOG_H
#define TURBO_GITDIALOG_H

#include <functional>
#include <string>

class GitManager;

// Shows the commit dialog: a list of changed files to stage and a message box.
// On OK, stages the chosen files and commits via 'git' (which queues a status
// refresh afterwards). Returns true if a commit was started.
//
// 'beforeCommit' (if set) runs after the user confirms, with the commit message,
// just before staging+committing; returning false aborts the commit (so a Lua
// beforeCommit handler can veto it). 'afterCommit' (if set) runs on the main
// thread once the commit completes, with success and git's output.
bool executeGitCommitDialog(
    GitManager &git,
    std::function<bool(const std::string &message)> beforeCommit = {},
    std::function<void(bool ok, const std::string &output)> afterCommit = {}) noexcept;

// Asks how to handle uncommitted changes when switching to 'branch'. Returns
// cmYes (stash & switch), cmNo (force / discard) or cmCancel (stay put).
unsigned short executeBranchSwitchDialog(const char *branch) noexcept;

// New-branch dialog: ask for a branch name. On OK fills 'name' (trimmed,
// non-empty) and returns true; returns false if cancelled or the name is empty.
bool executeNewBranchDialog(std::string &name) noexcept;

// Merge dialog: pick a branch to merge into the current one and a conflict
// strategy. On OK fills 'branch' and 'favor' (0 = default, 1 = favor ours,
// 2 = favor theirs) and returns true; returns false if cancelled / no branches.
bool executeMergeDialog(GitManager &git, std::string &branch, int &favor) noexcept;

#endif // TURBO_GITDIALOG_H
