#ifndef TURBO_CMDS_H
#define TURBO_CMDS_H

// Commands 0..255 support being disabled while the rest don't. However,
// command ranges 0..99 and 256..999 are reserved by Turbo Vision, so custom
// commands must be defined in the ranges 100..255 and 1000..65535.

enum : ushort
{
    // Commands that can be disabled.
    cmEditorNext = 100,
    cmEditorPrev,
    cmToggleWrap,
    cmToggleLineNums,
    cmSearchPrev,
    cmToggleIndent,
    cmTreeNext,
    cmTreePrev,
    cmCloseEditor,
    cmRename,
    cmSelUppercase,
    cmSelLowercase,
    cmSelCapitalize,
    cmToggleComment,
    cmGoToLine,
    cmReplaceOne,
    cmReplaceAll,
    cmCompletion,
    cmSelectNextOccurrence,
    cmSelectAllOccurrences,
    cmToggleBookmark,
    cmNextBookmark,
    cmPrevBookmark,
    cmToggleFolding,
    cmFoldAtCursor,
    cmFoldAll,
    cmUnfoldAll,
    cmToggleChangeHistory,
    cmToggleEdge,
    // Commands that cannot be disabled.
    cmToggleTree = 1000,
    cmStateChanged,
    cmFindFindBox,
    cmFindGoToLineBox,
    cmCloseView ,
    cmSearchIncr,
    cmFindReplaceBox,
    cmClearReplace,
    cmAbout,
    cmFindHelpWindow,
    cmToggleAutoSave,
    cmRevealInTree,
    cmLspSettings,
    cmShowCompletion,
    cmGitRefresh,
    cmGitCommit,
    cmGitFetch,
    cmGitPull,
    cmGitPush,
    cmToggleHidden,
    cmNewTerminal,
    // File-tree right-click context menu actions.
    cmTreeOpen,
    cmTreeRename,
    cmTreeNewFile,
    cmTreeGitAdd,
    cmTreeGitRevert,
    // Recent-windows list in the Windows menu: cmWindowBase + i selects the
    // i-th most-recently-used editor window (i in [0, windowListMax)).
    cmWindowBase = 1100,
    // Branch dropdown in the menu bar: cmBranchBase + i checks out the i-th
    // branch shown in the list (i in [0, branchListMax)).
    cmBranchBase = 1200,
};

enum { windowListMax = 10 };
enum { branchListMax = 100 };

#endif // TURBO_CMDS_H
