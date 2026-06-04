#ifndef TURBO_APP_H
#define TURBO_APP_H

#define Uses_TApplication
#define Uses_TFileDialog
#define Uses_TMenuBar
#define Uses_TMenu
#include <tvision/tv.h>

#include <memory>
#include <vector>
#include <fstream>
#include <functional>

#include <turbo/editstates.h>
#include <turbo/filewatcher.h>
#include "doctree.h"
#include "apputils.h"
#include "editwindow.h"
#include "settings.h"
#include "frecency.h"
#include "outputwindow.h"
#include "commandrunner.h"
#include "buildconfig.h"
#include "cmds.h"

struct EditorWindow;
class TClockView;
class LspManager;
class GitManager;
struct BranchView;
struct TerminalView;

// A background command started alongside Run (e.g. a queue runner). Its output
// is logged to .turbo/logs/<name>.log rather than shown in the output pane.
struct BackgroundJob
{
    std::string name;
    std::unique_ptr<CommandRunner> runner;
    std::ofstream log;
};

// TMenuView::menu (the root of the menu tree) is protected; expose it so the app
// can rewrite toggle-item labels to show check marks.
struct TurboMenuBar : public TMenuBar
{
    using TMenuBar::TMenuBar;
    TMenu *rootMenu() const noexcept { return menu; }
};

struct TurboApp : public TApplication, EditorWindowParent
{

    FileCounter fileCount;
    list_head<EditorWindow> MRUlist;
    TClockView *clock;
    DocumentTreeWindow *docTree;
    TCommandSet editorCmds;
    // Number of recent-window slots currently built into the Windows menu. The
    // menu bar is rebuilt when this needs to change, so there are never empty
    // placeholder rows (0 open editors = no slots).
    int menuRecentCount {0};
    bool argsParsed {false};
    int argc;
    const char **argv;
    turbo::SearchSettings searchSettings;
    std::string mostRecentDir;
    AppSettings settings;
    FrecencyStore frecency;
    std::unique_ptr<LspManager> lsp;
    std::unique_ptr<GitManager> git;
    std::unique_ptr<turbo::FileWatcher> watcher;

    // Branch indicator at the right of the menu bar (clickable; opens a popup of
    // the other branches). 'branchTextShown' is the last text written to it, so
    // the idle refresh only repaints/resizes on change.
    BranchView *branchView {nullptr};
    std::string branchTextShown;

    // Open terminal views, pumped each idle tick (see newTerminal()).
    std::vector<TerminalView *> terminals;

    // Build/Run: a bordered output pane docked at the bottom of the editor area,
    // and the command runner streaming the current build into it.
    OutputWindow *outputWin {nullptr};
    std::unique_ptr<CommandRunner> buildRunner;
    std::string projectRoot;      // cwd the app was opened from (build cwd)
    std::string lastBuildCommand; // remembered between Build invocations
    BuildConfig buildConfig;      // .turbo/config.json (build/test/run + extras)
    // Long-lived background commands started with Run (pumped each idle tick).
    std::vector<std::unique_ptr<BackgroundJob>> bgJobs;
    // Deferred action run on the next idle tick (used to start Run after a
    // build-first finishes, so we don't reassign buildRunner inside its pump).
    std::function<void()> pendingAfterBuild;

    TurboApp(int argc, const char **argv) noexcept;
    ~TurboApp();
    static TMenuBar* initMenuBar(TRect r);
    // Build the menu bar with 'recentCount' recent-window slots in the Windows
    // menu (initMenuBar builds with 0; rebuildMenuBar swaps in a new bar when the
    // count changes).
    static TMenuBar* makeMenuBar(TRect r, int recentCount);
    void rebuildMenuBar(int recentCount);
    static TStatusLine* initStatusLine(TRect r);
    // Dark, high-contrast palette for the menu bar and status line (indices 2..7
    // of the application palette), so disabled items stay readable.
    TPalette &getPalette() const override;
    // Custom desktop: a solid colour fill instead of Turbo Vision's shaded
    // (\xB0) pattern background.
    static TDeskTop* initDeskTop(TRect r);

    void shutDown() override;
    void idle() override;
    void getEvent(TEvent &event) override;
    void handleEvent(TEvent& event) override;
    void parseArgs();

    void fileNew();
    void fileOpen();
    void fileOpenOrNew(const char *path);
    void openFileFromTree(const char *absPath);
    // Focus the editor already showing 'absPath', or open it; then jump to
    // 'line' (0-based; <0 = no jump) and record the open for frecency ranking.
    void openOrFocus(const std::string &absPath, long line = -1) noexcept;
    // Fuzzy navigation overlays (Ctrl-P / Ctrl-Shift-P).
    void gotoAnything();
    void commandPalette();
    void scanWorkspace();
    void toggleAutoSave();
    void toggleHiddenFiles();
    // Rewrite the toggle menu items' labels so enabled ones show a check mark.
    // Reflects the active editor for per-editor toggles. Call when state that a
    // check depends on changes (toggles, editor focus, tree show/hide).
    void refreshMenuChecks() noexcept;
    // Refresh the recent-windows list in the Windows menu from the MRU order.
    void refreshWindowList() noexcept;
    // Focus the i-th most-recently-used editor window (Windows menu list).
    void focusRecentWindow(int index) noexcept;
    // Lowest window number in [1, 9] not currently used by an open editor, or
    // wnNoNumber if all nine are taken. Drives the built-in Alt-1..9 selection.
    short lowestFreeWindowNumber() noexcept;
    void onFilesChanged();
    // File-tree context-menu actions. Paths are absolute.
    void treeCreateFile(const std::string &dirPath);     // prompt + create + open
    void treeRenamePath(const std::string &path, bool isDir); // prompt + rename
    void treeStagePath(const std::string &path);         // git add
    void treeRevertPath(const std::string &path);        // confirm + git checkout HEAD
    // Reload an open editor's buffer from the file on disk (used after a revert,
    // so the editor doesn't keep -- and re-save -- the discarded changes). No-op
    // if 'path' isn't open in any editor.
    void reloadEditorFromDisk(const std::string &path) noexcept;
    // Reload every open editor that has no unsaved changes from disk (used after
    // a branch switch). Dirty buffers and files absent on the new branch are
    // left untouched, so no in-progress work is lost.
    void reloadCleanEditorsFromDisk() noexcept;

    // Menu-bar branch indicator (top-right view + on-demand popup).
    void refreshBranchView() noexcept;        // idle: update the indicator's text
    void showBranchMenu(TPoint where) noexcept; // build & run the branch popup
    void switchToBranch(const std::string &branch) noexcept; // confirm + checkout
    void gitRefresh();
    void gitCommitDialog();
    void gitRemote(int which); // 0=fetch 1=pull 2=push
    void configureLsp();
    void editLspSettings();
    // Colour-scheme dialog. 'editThemeSettings' runs the dialog (which posts
    // cmApplyTheme on Apply/OK); 'applyActiveTheme' re-themes every open editor
    // from the now-current active schemes, repaints the chrome, and persists.
    void editThemeSettings();
    void applyActiveTheme() noexcept;
    void closeAll();
    TRect newEditorBounds() const;
    turbo::TScintilla &createScintilla() noexcept;
    void addEditor(turbo::TScintilla &, const char *path);
    void showEditorList(TEvent *ev);
    void toggleTreeView();

    // Build/Run output pane.
    TRect outputBounds() const;   // target bounds of the docked pane
    void toggleOutputView();      // show/hide it (resizes editors on the Y axis)
    void showOutput();            // ensure it is visible
    void runBuild();              // run the configured build (or prompt for one)
    void runTest();               // run the configured test command
    void runRun();                // build-if-needed, then run + background cmds
    void stopAll();               // stop the build/run and all background cmds
    void editBuildConfig();       // open the build-configuration dialog
    // Stream 'command' (a shell command) into the output pane, with 'label'
    // shown as the echoed header line. 'onDone' (if set) runs on the main thread
    // with the exit code once the command finishes.
    void runInOutput(const std::string &label, const std::string &command,
                     std::function<void(int)> onDone = {});
    // Run the configured run command + start the configured background commands.
    void startRun();
    void startBackgroundCommands();
    void stopBackgroundCommands();
    // True if Run should build first (per run mode / artifact staleness).
    bool needsBuildBeforeRun() const;
    // True if the configured artifact is missing or older than a project source.
    bool isArtifactStale() const;

    // Terminal windows. newTerminal() opens one running the configured shell.
    // Each TerminalView registers itself so idle() can pump its PTY output.
    void newTerminal();
    void registerTerminal(TerminalView *t) noexcept;
    void unregisterTerminal(TerminalView *t) noexcept;

    void handleFocus(EditorWindow &w) noexcept override;
    void handleTitleChange(EditorWindow &w) noexcept override;
    void removeEditor(EditorWindow &w) noexcept override;
    const char *getFileDialogDir() noexcept override;
    bool autoSaveOnFocusLoss() noexcept override;
    void editorTextChanged(EditorWindow &w) noexcept override;
    void editorSaved(EditorWindow &w) noexcept override;
    void editorCharAdded(EditorWindow &w, int ch) noexcept override;
    void editorRequestCompletion(EditorWindow &w) noexcept override;
    void editorHoverStart(EditorWindow &w, long pos) noexcept override;
    void editorHoverEnd(EditorWindow &w) noexcept override;
};

#endif
