#ifndef TURBO_APP_H
#define TURBO_APP_H

#define Uses_TApplication
#define Uses_TFileDialog
#define Uses_TMenuBar
#define Uses_TMenu
#include <tvision/tv.h>

#include <memory>

#include <turbo/editstates.h>
#include <turbo/filewatcher.h>
#include "doctree.h"
#include "apputils.h"
#include "editwindow.h"
#include "settings.h"
#include "cmds.h"

struct EditorWindow;
class TClockView;
class LspManager;
class GitManager;

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
    bool argsParsed {false};
    int argc;
    const char **argv;
    turbo::SearchSettings searchSettings;
    std::string mostRecentDir;
    AppSettings settings;
    std::unique_ptr<LspManager> lsp;
    std::unique_ptr<GitManager> git;
    std::unique_ptr<turbo::FileWatcher> watcher;

    TurboApp(int argc, const char **argv) noexcept;
    ~TurboApp();
    static TMenuBar* initMenuBar(TRect r);
    static TStatusLine* initStatusLine(TRect r);

    void shutDown() override;
    void idle() override;
    void getEvent(TEvent &event) override;
    void handleEvent(TEvent& event) override;
    void parseArgs();

    void fileNew();
    void fileOpen();
    void fileOpenOrNew(const char *path);
    void openFileFromTree(const char *absPath);
    void scanWorkspace();
    void toggleAutoSave();
    void toggleHiddenFiles();
    // Rewrite the toggle menu items' labels so enabled ones show a check mark.
    // Reflects the active editor for per-editor toggles. Call when state that a
    // check depends on changes (toggles, editor focus, tree show/hide).
    void refreshMenuChecks() noexcept;
    void onFilesChanged();
    void gitRefresh();
    void gitCommitDialog();
    void gitRemote(int which); // 0=fetch 1=pull 2=push
    void configureLsp();
    void editLspSettings();
    void closeAll();
    TRect newEditorBounds() const;
    turbo::TScintilla &createScintilla() noexcept;
    void addEditor(turbo::TScintilla &, const char *path);
    void showEditorList(TEvent *ev);
    void toggleTreeView();

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
