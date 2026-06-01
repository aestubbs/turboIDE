#ifndef TURBO_GITDIALOG_H
#define TURBO_GITDIALOG_H

class GitManager;

// Shows the commit dialog: a list of changed files to stage and a message box.
// On OK, stages the chosen files and commits via 'git' (which queues a status
// refresh afterwards). Returns true if a commit was started.
bool executeGitCommitDialog(GitManager &git) noexcept;

// Asks how to handle uncommitted changes when switching to 'branch'. Returns
// cmYes (stash & switch), cmNo (force / discard) or cmCancel (stay put).
unsigned short executeBranchSwitchDialog(const char *branch) noexcept;

#endif // TURBO_GITDIALOG_H
