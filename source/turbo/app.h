#ifndef TURBO_APP_H
#define TURBO_APP_H

#define Uses_TApplication
#define Uses_TFileDialog
#include <tvision/tv.h>

#include <memory>

#include <turbo/editstates.h>
#include "doctree.h"
#include "apputils.h"
#include "editwindow.h"
#include "settings.h"
#include "cmds.h"

struct EditorWindow;
class TClockView;
class LspManager;

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
