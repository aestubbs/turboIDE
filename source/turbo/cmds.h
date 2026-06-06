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
    // Multi-cursor / multiple-selection editing (disable-able: editor-only).
    cmAddCaretUp,
    cmAddCaretDown,
    cmSkipOccurrence,
    cmUndoSelection,
    cmSplitSelectionLines,
    cmCollapseSelection,
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
    cmGitMerge,
    cmGitMergeAbort,
    cmGitMergeContinue,
    cmGitResolveFile, // mark the conflict-bar's editor file resolved (save + git add)
    cmToggleHidden,
    cmNewTerminal,
    // Guided "New File...": prompt for a name+location up front (via the Save As
    // dialog) so the buffer is created on disk with the right lexer from the
    // start. Distinct from cmNew, which opens an unnamed scratch buffer.
    cmNewNamedFile,
    // Fuzzy navigation overlays.
    cmGotoAnything,
    cmCommandPalette,
    // Build / Run system.
    cmBuild,
    cmRun,
    cmTest,
    cmStop,
    cmBuildConfig,
    cmToggleOutput,
    // Colour-scheme dialog (Settings menu) and the "apply the active scheme"
    // notification the dialog posts to the application on Apply/OK.
    cmThemeSettings,
    cmApplyTheme,
    // File-tree right-click context menu actions.
    cmTreeOpen,
    cmTreeRename,
    cmTreeNewFile,
    cmTreeNewFolder,
    cmTreeGitAdd,
    cmTreeGitRevert,
    // Recent-windows list in the Windows menu: cmWindowBase + i selects the
    // i-th most-recently-used editor window (i in [0, windowListMax)).
    cmWindowBase = 1100,
    // Branch dropdown in the menu bar: cmBranchBase + i checks out the i-th
    // branch shown in the list (i in [0, branchListMax)).
    cmBranchBase = 1200,
    // File-tree git-status filter dropdown: cmTreeGitFilterBase + i selects the
    // i-th TreeGitFilter (i in [0, tgfCount)).
    cmTreeGitFilterBase = 1300,
};

enum { windowListMax = 10 };
enum { branchListMax = 100 };

#endif // TURBO_CMDS_H
