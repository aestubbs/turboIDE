#define Uses_TApplication
#define Uses_MsgBox
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TSubMenu
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TFileDialog
#define Uses_TIndicator
#define Uses_TStaticText
#define Uses_TParamText
#define Uses_TScreen
#define Uses_TButton
#include <tvision/tv.h>

#include "app.h"
#include "help.h"
#include "apputils.h"
#include "editwindow.h"
#include "widgets.h"
#include "listviews.h"
#include "doctree.h"
#include "lspmanager.h"
#include "lspdialog.h"
#include "gitmanager.h"
#include "gitdialog.h"
#include "menucheck.h"
#include <turbo/fileeditor.h>
#include <turbo/tpath.h>
#include <filesystem>

using namespace Scintilla;

TurboApp::TurboApp(int argc, const char *argv[]) noexcept :
    TProgInit( &TurboApp::initStatusLine,
               &TurboApp::initMenuBar,
               &TApplication::initDeskTop
             ),
    argc(argc),
    argv(argv)
{
    loadSettings(settings);
    lsp = std::make_unique<LspManager>();
    configureLsp();
    git = std::make_unique<GitManager>();
    watcher = std::make_unique<turbo::FileWatcher>();

    TCommandSet ts;
    ts += cmSave;
    ts += cmSaveAs;
    ts += cmRename;
    ts += cmToggleWrap;
    ts += cmToggleLineNums;
    ts += cmFind;
    ts += cmReplace;
    ts += cmGoToLine;
    ts += cmSearchAgain;
    ts += cmSearchPrev;
    ts += cmToggleIndent;
    ts += cmCloseEditor;
    ts += cmSelUppercase;
    ts += cmSelLowercase;
    ts += cmSelCapitalize;
    ts += cmToggleComment;
    ts += cmCompletion;
    ts += cmSelectNextOccurrence;
    ts += cmSelectAllOccurrences;
    ts += cmToggleBookmark;
    ts += cmNextBookmark;
    ts += cmPrevBookmark;
    ts += cmToggleFolding;
    ts += cmFoldAtCursor;
    ts += cmFoldAll;
    ts += cmUnfoldAll;
    ts += cmToggleChangeHistory;
    ts += cmToggleEdge;
    ts += cmUndo;
    ts += cmRedo;
    ts += cmCut;
    ts += cmCopy;
    ts += cmPaste;
    disableCommands(ts);

    // Actions that only make sense when there is at least one editor.
    editorCmds += cmEditorNext;
    editorCmds += cmEditorPrev;
    editorCmds += cmTreeNext;
    editorCmds += cmTreePrev;
    editorCmds += cmCloseAll;
    editorCmds += cmCloseEditor;
    disableCommands(editorCmds);

    // Create the clock view.
    TRect r = getExtent();
    r.a.x = r.b.x - 9;
    r.b.y = r.a.y + 1;
    clock = new TClockView(r);
    clock->growMode = gfGrowLoX | gfGrowHiX;
    insert(clock);

    // Create the document tree view
    {
        TRect r = deskTop->getExtent();
        // Try to make it between 22 and 30 columns wide, and try to leave
        // at least 82 empty columns on screen (so that an editor view is
        // at least ~80 columns by default).
        if (r.b.x > 22)
            r.a.x = r.b.x - min(max(r.b.x - 82, 22), 30);
        docTree = new DocumentTreeWindow(r, &docTree);
        docTree->flags &= ~wfZoom;
        // The grow mode assumes it's placed on the right side of the screen.
        // Greater flexibility would require some trick or a dedicated class
        // for side views.
        docTree->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
        docTree->setState(sfShadow, False);
        deskTop->insert(docTree);
        // Show by default only on large terminals.
        if (deskTop->size.x - docTree->size.x < 82)
            docTree->hide();
    }
}

// Builds the windowListMax placeholder items for the recent-windows section of
// the Windows menu. They start disabled with a blank label; refreshMenuChecks()
// fills in the names of the most-recently-used editor windows at runtime.
static TMenuItem &recentWindowsItems()
{
    TMenuItem *head = nullptr;
    TMenuItem *tail = nullptr;
    for (int i = 0; i < windowListMax; ++i)
    {
        auto *item = new TMenuItem(" ", cmWindowBase + i, kbNoKey, hcNoContext);
        item->disabled = True;
        if (!head)
            head = tail = item;
        else { tail->append(item); tail = item; }
    }
    return *head;
}

TMenuBar *TurboApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y+1;
    return new TurboMenuBar( r,
        *new TSubMenu( "~F~ile", kbAltF, hcNoContext ) +
            *new TMenuItem( "~N~ew", cmNew, kbCtrlN, hcNoContext, "Ctrl-N" ) +
            *new TMenuItem( "~O~pen", cmOpen, kbCtrlO, hcNoContext, "Ctrl-O" ) +
            newLine() +
            *new TMenuItem( "~S~ave", cmSave, kbCtrlS, hcNoContext, "Ctrl-S" ) +
            *new TMenuItem( "S~a~ve As...", cmSaveAs, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~R~ename...", cmRename, kbF2, hcNoContext, "F2" ) +
            newLine() +
            *new TMenuItem( "~C~lose", cmCloseEditor, kbCtrlW, hcNoContext, "Ctrl-W" ) +
            *new TMenuItem( "Close All", cmCloseAll, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "S~u~spend", cmDosShell, kbNoKey, hcNoContext ) +
            *new TMenuItem( "E~x~it", cmQuit, kbCtrlQ, hcNoContext, "Ctrl-Q" ) +
        *new TSubMenu( "~E~dit", kbAltE ) +
            *new TMenuItem( "~U~ndo", cmUndo, kbCtrlZ, hcNoContext, "Ctrl-Z" ) +
            *new TMenuItem( "Re~d~o", cmRedo, kbCtrlY, hcNoContext, "Ctrl-Y" ) +
            newLine() +
            *new TMenuItem( "Cu~t~", cmCut, kbCtrlX, hcNoContext, "Ctrl-X" ) +
            *new TMenuItem( "~C~opy", cmCopy, kbCtrlC, hcNoContext, "Ctrl-C" ) +
            *new TMenuItem( "~P~aste", cmPaste, kbCtrlV, hcNoContext, "Ctrl-V" ) +
            newLine() +
            *new TMenuItem( "~F~ind...", cmFind, kbCtrlF, hcNoContext, "Ctrl-F" ) +
            *new TMenuItem( "~R~eplace...",cmReplace, kbCtrlR, hcNoContext, "Ctrl-R" ) +
            *new TMenuItem( "~G~o to Line...",cmGoToLine, kbCtrlG, hcNoContext, "Ctrl-G" ) +
            *new TMenuItem( "Find ~N~ext", cmSearchAgain, kbF3, hcNoContext, "F3" ) +
            *new TMenuItem( "Find ~P~revious", cmSearchPrev, kbShiftF3, hcNoContext, "Shift-F3" ) +
            newLine() +
            *new TMenuItem( "C~o~mplete", cmCompletion, kbNoKey, hcNoContext, "Ctrl-Space" ) +
        *new TSubMenu( "Se~l~ection", kbAltL ) +
            *new TMenuItem( "~T~oggle Comment", cmToggleComment, kbCtrlE, hcNoContext, "Ctrl-E" ) +
            newLine() +
            *new TMenuItem( "~U~ppercase", cmSelUppercase, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~L~owercase", cmSelLowercase, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~C~apitalize", cmSelCapitalize, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "Select ~N~ext Occurrence", cmSelectNextOccurrence, kbCtrlD, hcNoContext, "Ctrl-D" ) +
            *new TMenuItem( "Select ~A~ll Occurrences", cmSelectAllOccurrences, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~C~ode", kbAltC ) +
            *new TMenuItem( "Code ~F~olding", cmToggleFolding, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~T~oggle Fold at Cursor", cmFoldAtCursor, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Fold ~A~ll", cmFoldAll, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~U~nfold All", cmUnfoldAll, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "Toggle ~B~ookmark", cmToggleBookmark, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~N~ext Bookmark", cmNextBookmark, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~P~revious Bookmark", cmPrevBookmark, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~S~ettings", kbAltS ) +
            *new TMenuItem( "Toggle Line ~N~umbers", cmToggleLineNums, kbF8, hcNoContext, "F8" ) +
            *new TMenuItem( "Toggle Line ~W~rapping", cmToggleWrap, kbF9, hcNoContext, "F9" ) +
            *new TMenuItem( "Toggle Auto ~I~ndent", cmToggleIndent, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Toggle File ~T~ree View", cmToggleTree, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Show ~H~idden Files", cmToggleHidden, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Toggle ~A~uto-save on Focus Loss", cmToggleAutoSave, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Toggle Chan~g~e History", cmToggleChangeHistory, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Toggle Long Line G~u~ide", cmToggleEdge, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~L~anguage Servers...", cmLspSettings, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~G~it", kbAltG ) +
            *new TMenuItem( "~C~ommit...", cmGitCommit, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~F~etch", cmGitFetch, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Pu~l~l", cmGitPull, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~P~ush", cmGitPush, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~R~efresh Status", cmGitRefresh, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~W~indows", kbAltW ) +
            *new TMenuItem( "~Z~oom", cmZoom, kbF5, hcNoContext, "F5" ) +
            *new TMenuItem( "~R~esize/move",cmResize, kbCtrlF5, hcNoContext, "Ctrl-F5" ) +
            *new TMenuItem( "~N~ext", cmEditorNext, kbF6, hcNoContext, "F6" ) +
            *new TMenuItem( "~P~revious", cmEditorPrev, kbShiftF6, hcNoContext, "Shift-F6" ) +
            *new TMenuItem( "~C~lose", cmClose, kbAltF3, hcNoContext, "Alt-F3" ) +
            *new TMenuItem( "Previous (in tree)", cmTreePrev, kbAltUp, hcNoContext, "Alt-Up" ) +
            *new TMenuItem( "Next (in tree)", cmTreeNext, kbAltDown, hcNoContext, "Alt-Down" ) +
            newLine() +
            recentWindowsItems() +
        *new TSubMenu( "~H~elp", kbAltH ) +
            *new TMenuItem( "~K~eyboard shortcurs", cmHelp, kbF1, hcNoContext, "F1" ) +
            newLine() +
            *new TMenuItem( "~A~bout...", cmAbout, kbNoKey, hcNoContext )
            );

}

TStatusLine *TurboApp::initStatusLine( TRect r )
{
    r.a.y = r.b.y-1;
    return new TStatusLine( r,
        *new TStatusDef( 0, 0xFFFF ) +
            *new TStatusItem( 0, kbAltX, cmQuit ) +
            *new TStatusItem( "~Ctrl-N~ New", kbNoKey, cmNew ) +
            *new TStatusItem( "~Ctrl-O~ Open", kbNoKey, cmOpen ) +
            *new TStatusItem( "~Ctrl-S~ Save", kbNoKey, cmSave ) +
            *new TStatusItem( "~F6~ Next", kbF6, cmEditorNext ) +
            *new TStatusItem( "~F12~ Menu", kbF12, cmMenu ) +
            *new TStatusItem( 0, TKey(kbCtrlZ, kbShift), cmRedo ) +
            *new TStatusItem( 0, kbCtrlX, cmCut ) +
            *new TStatusItem( 0, kbCtrlC, cmCopy ) +
            *new TStatusItem( 0, kbCtrlV, cmPaste ) +
            *new TStatusItem( 0, kbShiftDel, cmCut ) +
            *new TStatusItem( 0, kbCtrlIns, cmCopy ) +
            *new TStatusItem( 0, kbShiftIns, cmPaste ) +
            *new TStatusItem( 0, kbCtrlTab, cmEditorNext ) +
            *new TStatusItem( 0, kbAltTab, cmEditorNext ) +
            *new TStatusItem( 0, kbShiftF6, cmEditorPrev ) +
            *new TStatusItem( 0, TKey(kbCtrlTab, kbShift), cmEditorPrev ) +
            *new TStatusItem( 0, TKey(kbAltTab, kbShift), cmEditorPrev ) +
            *new TStatusItem( 0, TKey('/', kbCtrlShift), cmToggleComment ) +
            *new TStatusItem( 0, TKey('_', kbCtrlShift), cmToggleComment ) +
            *new TStatusItem( 0, kbF5, cmZoom ) +
            *new TStatusItem( 0, kbCtrlF5, cmResize )
            );
}

TurboApp::~TurboApp() = default;

void TurboApp::shutDown()
{
    if (lsp)
        lsp->shutdown();
    if (git)
        git->shutdown();
    if (watcher)
        watcher->stop();
    docTree = nullptr;
    clock = nullptr;
    TApplication::shutDown();
}

void TurboApp::idle()
{
    TApplication::idle();
    if (clock)
        clock->update();
    if (lsp)
        lsp->pump();
    if (watcher)
        onFilesChanged();
    if (git)
        git->pump(docTree);
    // Keep menu check marks in sync with per-editor toggle state and the active
    // editor. Cheap: only rewrites a label when its checked state changes.
    refreshMenuChecks();
    refreshWindowList();
}

void TurboApp::getEvent(TEvent &event)
{
    if (!argsParsed) {
        argsParsed = true;
        scanWorkspace();
        parseArgs();
    }
    TApplication::getEvent(event);
}

void TurboApp::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);
    bool handled = false;
    if (event.what == evCommand) {
        handled = true;
        switch (event.message.command) {
            case cmNew: fileNew(); break;
            case cmOpen: fileOpen(); break;
            case cmEditorNext:
            case cmEditorPrev:
                showEditorList(&event);
                break;
            case cmCloseAll: closeAll(); break;
            case cmToggleTree: toggleTreeView(); break;
            case cmToggleHidden: toggleHiddenFiles(); break;
            case cmToggleAutoSave: toggleAutoSave(); break;
            case cmLspSettings: editLspSettings(); break;
            case cmGitRefresh: gitRefresh(); break;
            case cmGitCommit: gitCommitDialog(); break;
            case cmGitFetch: gitRemote(0); break;
            case cmGitPull: gitRemote(1); break;
            case cmGitPush: gitRemote(2); break;
            case cmShowCompletion:
                if (lsp)
                    if (auto *w = (EditorWindow *) event.message.infoPtr)
                        lsp->showCompletion(*w);
                break;
            case cmRevealInTree:
                if (docTree) {
                    if (!(docTree->state & sfVisible))
                        toggleTreeView();
                    if (auto *w = (EditorWindow *) event.message.infoPtr)
                        docTree->tree->revealEditor(w);
                }
                break;
            case cmTreeNext:
                if (docTree)
                    docTree->tree->focusNext();
                break;
            case cmTreePrev:
                if (docTree)
                    docTree->tree->focusPrev();
                break;
            case cmAbout:
                TurboHelp::executeAboutDialog(*deskTop);
                break;
            case cmHelp:
                TurboHelp::showOrFocusHelpWindow(*deskTop);
                break;
            default:
                if (event.message.command >= cmWindowBase &&
                    event.message.command < cmWindowBase + windowListMax)
                    focusRecentWindow(event.message.command - cmWindowBase);
                else
                    handled = false;
                break;
        }
    }
    if (handled)
        clearEvent(event);
}

void TurboApp::parseArgs()
{
    if (argc && argv) {
        auto *w = new TWindow(TRect(15, 8, 65, 19), "Please Wait...", wnNoNumber);
        w->flags = 0;
        w->options |= ofCentered;
        w->palette = wpGrayWindow;
        w->insert( new TStaticText(TRect(2, 2, 48, 3), "Opening file:"));
        auto *current = new TParamText(TRect(2, 3, 48, 9));
        w->insert(current);
        insert(w);
        for (int i = 1; i < argc; ++i) {
            current->setText("%s", argv[i]);
            TScreen::flushScreen();
            fileOpenOrNew(argv[i]);
        }
        remove(w);
        TObject::destroy(w);
    }
}

void TurboApp::fileNew()
{
    addEditor(createScintilla(), "");
}

void TurboApp::fileOpen()
{
    TurboFileDialogs dlgs {*this};
    turbo::openFile([&] () -> auto& {
        return createScintilla();
    }, [&] (auto &scintilla, auto *path) {
        addEditor(scintilla, path);
    }, dlgs);
}

void TurboApp::fileOpenOrNew(const char *path)
{
    char abspath[MAXPATH];
    strnzcpy(abspath, path, MAXPATH);
    fexpand(abspath);
    auto &scintilla = createScintilla();
    if (turbo::readFile(scintilla, abspath, turbo::acceptMissingFilesOnOpen))
        addEditor(scintilla, abspath);
}

void TurboApp::openFileFromTree(const char *absPath)
{
    fileOpenOrNew(absPath);
}

void TurboApp::scanWorkspace()
{
    if (!docTree)
        return;
    char *cwd = ::getcwd(nullptr, 0);
    if (cwd)
    {
        docTree->tree->setShowHidden(settings.showHidden); // before the first scan
        docTree->tree->scanDirectory(cwd);
        if (lsp)
            lsp->setRootPath(cwd);
        if (git)
            git->setWorkspace(cwd);
        if (watcher)
            watcher->start(cwd);
        ::free(cwd);
    }
}

void TurboApp::toggleAutoSave()
{
    settings.autoSaveOnFocusLoss ^= true;
    saveSettings(settings);
    refreshMenuChecks();
}

void TurboApp::toggleHiddenFiles()
{
    settings.showHidden ^= true;
    saveSettings(settings);
    if (docTree)
        docTree->tree->setShowHidden(settings.showHidden);
    // The rescan rebuilt every node, dropping git badges; re-fetch so they
    // reappear on the new nodes.
    if (git)
        git->requestStatus();
    refreshMenuChecks();
}

void TurboApp::refreshMenuChecks() noexcept
{
    auto *bar = (TurboMenuBar *) menuBar;
    if (!bar)
        return;
    TMenu *m = bar->rootMenu();

    // Global toggles.
    setMenuItemCheck(m, cmToggleTree, docTree && (docTree->state & sfVisible));
    setMenuItemCheck(m, cmToggleHidden, settings.showHidden);
    setMenuItemCheck(m, cmToggleAutoSave, settings.autoSaveOnFocusLoss);

    // Per-editor toggles reflect the active (most-recently-focused) editor.
    // With no editor open they all show unchecked.
    EditorWindow *w = MRUlist.empty() ? nullptr : MRUlist.next->self;
    bool lineNums = false, wrap = false, indent = false, chHist = false, edge = false;
    if (w)
    {
        auto &ed = w->getEditor();
        lineNums = ed.lineNumbers.isEnabled();
        wrap     = ed.wrapping.isEnabled();
        indent   = ed.autoIndent.isEnabled();
        chHist   = ed.changeHistoryEnabled;
        edge     = ed.edgeEnabled;
    }
    setMenuItemCheck(m, cmToggleLineNums,      lineNums);
    setMenuItemCheck(m, cmToggleWrap,          wrap);
    setMenuItemCheck(m, cmToggleIndent,        indent);
    setMenuItemCheck(m, cmToggleChangeHistory, chHist);
    setMenuItemCheck(m, cmToggleFolding,       w && w->getEditor().foldingEnabled);
    setMenuItemCheck(m, cmToggleEdge,          edge);
}

void TurboApp::refreshWindowList() noexcept
{
    auto *bar = (TurboMenuBar *) menuBar;
    if (!bar)
        return;
    TMenu *m = bar->rootMenu();
    int i = 0;
    MRUlist.forEach([&] (EditorWindow *w) {
        if (i >= windowListMax || !w)
            return;
        // "N filename" -- a 1-based index plus the window's title. Escape '~'
        // (TVision's hotkey marker) so titles containing it render literally.
        std::string label = std::to_string(i + 1) + " ";
        for (char c : w->title)
        {
            if (c == '~') label += '~';
            label += c;
        }
        setMenuItemLabel(m, cmWindowBase + i, label.c_str(), true);
        ++i;
    });
    // Blank and disable the unused slots.
    for (; i < windowListMax; ++i)
        setMenuItemLabel(m, cmWindowBase + i, " ", false);
}

void TurboApp::focusRecentWindow(int index) noexcept
{
    int i = 0;
    EditorWindow *target = nullptr;
    MRUlist.forEach([&] (EditorWindow *w) {
        if (i == index)
            target = w;
        ++i;
    });
    if (target)
        target->focus();
}

void TurboApp::configureLsp()
{
    if (!lsp)
        return;
    std::vector<std::pair<std::string, std::string>> servers;
    servers.reserve(settings.lspServers.size());
    for (auto &s : settings.lspServers)
        servers.emplace_back(s.language, s.command);
    lsp->configure(settings.lspEnabled, std::move(servers));
}

void TurboApp::editLspSettings()
{
    if (executeLspDialog(settings))
    {
        saveSettings(settings);
        configureLsp();
    }
}

void TurboApp::gitRefresh()
{
    if (git)
        git->requestStatus();
}

void TurboApp::onFilesChanged()
{
    std::vector<std::string> changed;
    if (!watcher->poll(changed))
        return;
    bool gitTouched = false;
    for (auto &p : changed)
    {
        gitTouched = true; // any change under the worktree may affect git status
        // Skip git's own internals for tree structure (they are hidden anyway);
        // they only matter for the status refresh below.
        if (p.find("/.git/") != std::string::npos)
            continue;
        if (!docTree)
            continue;
        std::error_code ec;
        if (std::filesystem::exists(p, ec))
        {
            bool isDir = std::filesystem::is_directory(p, ec);
            // No-op if already present or hidden; adds a node for new files/dirs.
            docTree->tree->addNode(p, isDir);
        }
        else
            docTree->tree->removeNode(p); // no-op if not in the tree
    }
    if (gitTouched && git)
        git->requestStatus();
}

void TurboApp::gitCommitDialog()
{
    if (!git || !git->isRepo())
    {
        messageBox("This folder is not a git repository.", mfInformation | mfOKButton);
        return;
    }
    if (executeGitCommitDialog(*git))
        ; // GitManager queues a status refresh after committing.
}

void TurboApp::gitRemote(int which)
{
    if (!git || !git->isRepo())
    {
        messageBox("This folder is not a git repository.", mfInformation | mfOKButton);
        return;
    }
    auto report = [] (const char *what) {
        return [what] (int code, const std::string &output) {
            if (code != 0)
            {
                std::string msg = std::string("git ") + what + " failed:\n" +
                    (output.empty() ? "(see terminal)" : output.substr(0, 400));
                messageBox(msg.c_str(), mfError | mfOKButton);
            }
        };
    };
    switch (which)
    {
        case 0: git->fetch(report("fetch")); break;
        case 1: git->pull(report("pull")); break;
        case 2: git->push(report("push")); break;
    }
}

void TurboApp::closeAll()
{
    while (!MRUlist.empty()) {
        auto *head = MRUlist.next;
        message((EditorWindow *) head->self, evCommand, cmClose, 0);
        TScreen::flushScreen();
        if (MRUlist.next == head) // Not removed
            break;
    }
}

TRect TurboApp::newEditorBounds() const
{
    if (!MRUlist.empty())
        return MRUlist.next->self->getBounds();
    else {
        TRect r = deskTop->getExtent();
        if (docTree && docTree->state & sfVisible) {
            TRect t = docTree->getBounds();
            // Align left.
            if (t.a.x > r.b.x - t.b.x)
                r.b.x = max(t.a.x, EditorWindow::minSize.x);
            // Align right.
            else
                r.a.x = min(t.b.x, r.b.x - EditorWindow::minSize.x);
        }
        return r;
    }
}

turbo::TScintilla &TurboApp::createScintilla() noexcept
{
    return turbo::createScintilla();
}

void TurboApp::addEditor(turbo::TScintilla &scintilla, const char *path)
// Pre: 'path' is an absolute path.
{
    TRect r = newEditorBounds();
    auto &counter = fileCount[TPath::basename(path)];
    auto &editor = *new TurboEditor(scintilla, path);
    EditorWindow &w = *new EditorWindow(r, editor, counter, searchSettings, *this);
    if (docTree)
        docTree->tree->linkEditor(&w);
    w.listHead.insert_after(&MRUlist);
    deskTop->insert(&w);
    enableCommands(editorCmds);
    if (lsp)
        lsp->didOpen(w);
}

void TurboApp::showEditorList(TEvent *ev)
{
    EditorListModel model {MRUlist};
    TRect r {0, 0, 0, 0};
    r.b.x = min(max(ListModel::maxItemCStrLen(model) + 4, 40), deskTop->size.x - 10);
    r.b.y = min(max(model.size() + 2, 6), deskTop->size.y - 4);
    r.move((deskTop->size.x - r.b.x) / 2,
           (deskTop->size.y - r.b.y) / 4);
    ListWindow *lw = &ListWindow::create<EditorListView>(r, "Buffer List", model, lvScrollBars);
    if (ev)
        lw->putEvent(*ev);
    if (deskTop->execView(lw) == cmOK)
        if (auto *wnd = (EditorWindow *) lw->getCurrent())
            wnd->focus();

    destroy(lw);
}

void TurboApp::toggleTreeView()
{
    if (!docTree)
        return;
    // Prevent editors from doing draw() on each changeBounds(). We'll draw all
    // the views at the end.
    MRUlist.forEach([&] (auto *win) {
        win->setState(sfExposed, False);
    });
    TRect dr = docTree->getBounds();
    if (docTree->state & sfVisible) {
        docTree->hide();
        MRUlist.forEach([this, dr] (auto *win) {
            TRect r = win->getBounds();
            if (r.a.x >= dr.b.x)
                r.a.x -= docTree->size.x;
            else if (r.b.x <= dr.a.x)
                r.b.x += docTree->size.x;
            win->locate(r);
        });
    } else {
        MRUlist.forEach([this, dr] (auto *win) {
            TRect r = win->getBounds();
            if (r.a.x + docTree->size.x >= dr.b.x)
                r.a.x += docTree->size.x;
            else if (r.b.x - docTree->size.x <= dr.a.x)
                r.b.x -= docTree->size.x;
            win->locate(r);
        });
        docTree->show();
    }
    MRUlist.forEach([&] (auto *win) {
        win->setState(sfExposed, True);
    });
    deskTop->redraw();
}

void TurboApp::handleFocus(EditorWindow &w) noexcept
{
    // w has been focused, so it becomes the first of our MRU list.
    w.listHead.insert_after(&MRUlist);
    if (docTree)
        docTree->tree->focusEditor(&w);
    // We keep track of the most recent directory for file dialogs.
    if (!w.filePath().empty())
        mostRecentDir = TPath::dirname(w.filePath());
}

void TurboApp::handleTitleChange(EditorWindow &w) noexcept
{
    auto &counter = fileCount[TPath::basename(w.filePath())];
    bool renamed = (&counter != w.fileNumber.counter);
    if (renamed)
        w.fileNumber = {counter};
    if (docTree)
    {
        if (renamed)
        {
            // The file path may have changed (rename): relink to the new node.
            docTree->tree->unlinkEditor(&w);
            docTree->tree->linkEditor(&w);
            docTree->tree->focusEditor(&w);
        }
        else if (auto *node = docTree->tree->findByEditor(&w))
        {
            // Save point reached/lost: refresh the unsaved-changes marker.
            node->refreshText();
            docTree->tree->drawView();
        }
    }
    if (!w.filePath().empty() && w.state & sfActive)
        mostRecentDir = TPath::dirname(w.filePath());
}

void TurboApp::removeEditor(EditorWindow &w) noexcept
{
    if (lsp)
        lsp->didClose(w);
    w.listHead.remove();
    if (MRUlist.empty())
        disableCommands(editorCmds);
    if (docTree)
    {
        docTree->tree->unlinkEditor(&w);
        // Removing the editor causes the focus to stay on the same position
        // but maybe not on the right element.
        if (!MRUlist.empty())
            docTree->tree->focusEditor(MRUlist.next->self);
    }
}

const char *TurboApp::getFileDialogDir() noexcept
{
    return mostRecentDir.c_str();
}

bool TurboApp::autoSaveOnFocusLoss() noexcept
{
    return settings.autoSaveOnFocusLoss;
}

void TurboApp::editorTextChanged(EditorWindow &w) noexcept
{
    if (lsp)
        lsp->didChange(w);
}

void TurboApp::editorSaved(EditorWindow &w) noexcept
{
    if (lsp)
        lsp->didSave(w);
    if (git)
        git->requestStatus(); // a save may change the file's git status
}

void TurboApp::editorCharAdded(EditorWindow &w, int ch) noexcept
{
    if (lsp)
        lsp->charAdded(w, ch);
}

void TurboApp::editorRequestCompletion(EditorWindow &w) noexcept
{
    if (lsp)
        lsp->requestCompletion(w);
}

void TurboApp::editorHoverStart(EditorWindow &w, long pos) noexcept
{
    if (lsp)
        lsp->hover(w, pos);
}

void TurboApp::editorHoverEnd(EditorWindow &w) noexcept
{
    if (lsp)
        lsp->hoverEnd(w);
}
