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
    cmGitNewBranch,   // prompt for a name, then create + switch (checkout -b)
    cmToggleHidden,
    cmNewTerminal,
    // Coding-agent window (Alt-0 opens or focuses it).
    cmToggleAgent,
    cmSelectAgent,   // choose the per-project agent (Claude Code / Codex / ...)
    cmRestartAgent,  // close and reopen the agent window
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
    cmToolsConfig,    // open the tool-processes configuration dialog
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
    cmTreeNewLuaScript, // create a new .lua script in a Lua-home group's dir
    cmTreeNewSkill,     // create a new skill (dir + SKILL.md) in a Skills-home dir
    // Lua scripting menu.
    cmLuaRunScript,   // pop up the list of discovered scripts and run one
    cmLuaNewScript,   // create + open a new script in the project's turbo-scripts
    cmLuaReload,      // re-run init.lua from the project + global Lua homes
    cmLuaShowScripts, // retired: Lua homes are now always shown (kept: enum slot)
    // Project (workspace) open/close. The IDE holds at most one project at a
    // time; opening another replaces it. With no project open the file tree is
    // empty except for the user's global Lua scripts.
    cmOpenDir,        // pick a directory and open it as the project
    cmCloseProject,   // close the current project (empties the tree/workspace)
    // Recent-windows list in the Windows menu: cmWindowBase + i selects the
    // i-th most-recently-used editor window (i in [0, windowListMax)).
    cmWindowBase = 1100,
    // Lua "Run Script" popup / palette: cmLuaScriptBase + i runs the i-th
    // discovered script (i in [0, luaScriptListMax)).
    cmLuaScriptBase = 1400,
    // Lua-registered commands (turbo.register_command): cmLuaCommandBase + i
    // invokes the i-th registered Lua function (i in [0, luaCommandListMax)).
    cmLuaCommandBase = 1600,
    // Run-menu tool toggles: cmToolBase + i starts/stops the i-th configured
    // tool process (i in [0, toolListMax)).
    cmToolBase = 1500,
    // Branch dropdown in the menu bar: cmBranchBase + i checks out the i-th
    // branch shown in the list (i in [0, branchListMax)).
    cmBranchBase = 1200,
    // File-tree git-status filter dropdown: cmTreeGitFilterBase + i selects the
    // i-th TreeGitFilter (i in [0, tgfCount)).
    cmTreeGitFilterBase = 1300,
};

enum { windowListMax = 10 };
enum { toolListMax = 32 };
enum { branchListMax = 100 };
enum { luaScriptListMax = 100 };
enum { luaCommandListMax = 100 };

#endif // TURBO_CMDS_H
