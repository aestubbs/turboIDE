#define Uses_TApplication
#define Uses_MsgBox
#define Uses_TBackground
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
#define Uses_TDrawBuffer
#define Uses_TMenu
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
#include "terminal.h"
#include "themedialog.h"
#include "theme.h"
#include "commandpalette.h"
#include "gotoanything.h"
#include "builddialog.h"
#include <turbo/fileeditor.h>
#include <turbo/tpath.h>
#include <cctype>
#include <filesystem>
#include <fstream>

using namespace Scintilla;

// Branch indicator that lives at the right of the menu bar. It draws the current
// branch (icon + name) like the clock used to, and a click opens a popup listing
// the other branches (built fresh each time, so there is no persistent item list
// to keep in sync). The list and the switch logic live in TurboApp.
struct BranchView : public TView
{
    std::string text {"\xE2\x8E\x87 \xE2\x80\xA6"}; // "⎇ …" until git status loads

    BranchView(const TRect &r) noexcept : TView(r)
    {
        eventMask |= evMouseDown;
        growMode = gfGrowLoX | gfGrowHiX; // hug the right edge on resize
    }

    void draw() override
    {
        TDrawBuffer buf;
        TColorAttr c = getColor(2); // same menu-row colour the clock used
        buf.moveChar(0, ' ', c, size.x);
        buf.moveStr(0, text.c_str(), c);
        writeLine(0, 0, size.x, 1, buf);
    }

    void setText(const char *t) noexcept
    {
        if (text != t)
        {
            text = t;
            drawView();
        }
    }

    void handleEvent(TEvent &ev) override
    {
        TView::handleEvent(ev);
        if (ev.what == evMouseDown)
        {
            // Anchor the popup at this view's top-left; popupMenu drops it one row
            // down and nudges it left to stay on screen.
            TPoint p = makeGlobal(TPoint {0, 0});
            if (auto *app = (TurboApp *) TProgram::application)
                app->showBranchMenu(p);
            clearEvent(ev);
        }
    }
};

// Desktop background colour shown behind all windows (a solid fill, replacing
// Turbo Vision's \xB0 shaded pattern).
static constexpr TColorDesired deskBackground = 0x0A0F1E;

// A plain solid-colour desktop background.
struct TurboBackground : public TBackground
{
    TurboBackground(const TRect &bounds) noexcept : TBackground(bounds, ' ') {}

    void draw() override
    {
        TDrawBuffer b;
        TColorAttr c {deskBackground, deskBackground};
        b.moveChar(0, ' ', c, size.x);
        writeLine(0, 0, size.x, size.y, b);
    }
};

// A desktop whose background is our solid-colour view.
struct TurboDeskTop : public TDeskTop
{
    TurboDeskTop(const TRect &bounds) noexcept :
        TDeskInit(&TurboDeskTop::initBackground),
        TDeskTop(bounds)
    {
    }

    static TBackground *initBackground(TRect bounds)
    {
        return new TurboBackground(bounds);
    }
};

TDeskTop *TurboApp::initDeskTop(TRect r)
{
    // Leave room for the menu bar (top row) and status line (bottom row), just
    // as TProgram::initDeskTop does -- otherwise the desktop covers the full
    // screen and hides them (and the windows' title/bottom bars).
    r.a.y++;
    r.b.y--;
    return new TurboDeskTop(r);
}

// Dark, high-contrast palette for the menu bar and the status line. Both resolve
// their colours through application-palette indices 2..7 (TMenuView's cpMenuView
// and TStatusLine's cpStatusLine), so we copy the default app palette and recolor
// just those six entries. The default grey-on-grey made disabled items (greyed
// menu commands, inactive status hints) almost invisible; this gives them a
// clearly readable mid-tone on a dark slate, matching the editor chrome.
TPalette &TurboApp::getPalette() const
{
    static TPalette pal = [this] {
        TPalette p = TProgram::getPalette(); // copy of the default app palette
        p[2] = TColorAttr(TColorRGB(0xD7DCE8), TColorRGB(0x222A3D)); // normal item
        p[3] = TColorAttr(TColorRGB(0x8A93A8), TColorRGB(0x222A3D)); // disabled item
        p[4] = TColorAttr(TColorRGB(0xF2A65A), TColorRGB(0x222A3D)); // hotkey letter
        p[5] = TColorAttr(TColorRGB(0xFFFFFF), TColorRGB(0x2E6FD6)); // selected item
        p[6] = TColorAttr(TColorRGB(0xC2C9DC), TColorRGB(0x2E6FD6)); // selected disabled
        p[7] = TColorAttr(TColorRGB(0xFFD27A), TColorRGB(0x2E6FD6)); // selected hotkey
        return p;
    }();
    return pal;
}

TurboApp::TurboApp(int argc, const char *argv[]) noexcept :
    TProgInit( &TurboApp::initStatusLine,
               &TurboApp::initMenuBar,
               &TurboApp::initDeskTop
             ),
    argc(argc),
    argv(argv)
{
    loadSettings(settings);
    frecency.load();
    // Fold any saved colour overrides onto the built-in 24-bit defaults before
    // the first editor is created, so new editors theme from the right scheme.
    applyThemeFromSettings(settings);
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
    ts += cmAddCaretUp;
    ts += cmAddCaretDown;
    ts += cmSkipOccurrence;
    ts += cmUndoSelection;
    ts += cmSplitSelectionLines;
    ts += cmCollapseSelection;
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

    // Clock: bottom-right of the status line (out of the way; the menu-bar right
    // is now used by the branch indicator).
    TRect ext = getExtent();
    {
        TRect r {ext.b.x - 9, ext.b.y - 1, ext.b.x, ext.b.y};
        clock = new TClockView(r);
        clock->growMode = gfGrowLoX | gfGrowHiX | gfGrowLoY | gfGrowHiY;
        insert(clock);
    }
    // Branch indicator: top-right of the menu bar. refreshBranchView() sizes it
    // to the current branch name; this is just an initial placeholder slot.
    {
        TRect r {ext.b.x - 12, ext.a.y, ext.b.x, ext.a.y + 1};
        branchView = new BranchView(r);
        insert(branchView);
    }

    // Create the document tree view
    {
        TRect r = deskTop->getExtent();
        // Try to make it between 22 and 30 columns wide, and try to leave
        // at least 82 empty columns on screen (so that an editor view is
        // at least ~80 columns by default).
        if (r.b.x > 22)
            r.a.x = r.b.x - min(max(r.b.x - 82, 22), 30);
        docTree = new DocumentTreeWindow(r, &docTree);
        // Docked side pane: no move/grow/zoom (so no bottom-right resize handle);
        // its left border is the resize handle and the close box toggles it off.
        docTree->flags = wfClose;
        // The grow mode assumes it's placed on the right side of the screen.
        // Greater flexibility would require some trick or a dedicated class
        // for side views.
        docTree->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
        docTree->setState(sfShadow, False);
        // Drag the left border to set the width; the editors beside it re-layout.
        docTree->onResizeTo = [this] (int borderScreenX) {
            setTreeWidth(deskTop->getExtent().b.x - borderScreenX);
        };
        deskTop->insert(docTree);
        // Show by default only on large terminals.
        if (deskTop->size.x - docTree->size.x < 82)
            docTree->hide();
    }

    // Output pane (Build/Run): a bordered window docked across the bottom of the
    // editor area, created hidden and shown on demand. Docked the same way the
    // file tree is, just on the Y axis.
    {
        outputWin = new OutputWindow(outputBounds(), &outputWin);
        outputWin->growMode = gfGrowLoY | gfGrowHiY | gfGrowHiX;
        outputWin->setState(sfShadow, False);
        // Clicking / Entering an error line jumps to the source.
        outputWin->view->onActivate = [this] (const std::string &file, long line) {
            openOrFocus(file, line);
        };
        // Dragging the pane's top border resizes it. The bottom is anchored to
        // the desktop bottom, so the height is (desktop bottom - dragged row).
        outputWin->onResizeTo = [this] (int borderScreenY) {
            setOutputPaneHeight(deskTop->getBounds().b.y - borderScreenY);
        };
        deskTop->insert(outputWin);
        outputWin->hide();
    }
}

// Builds 'count' placeholder items for the recent-windows section of the Windows
// menu. They start disabled with a blank label; refreshWindowList() fills in the
// names of the most-recently-used editor windows at runtime. 'count' tracks the
// number of open editors, so there are never trailing empty rows.
static TMenuItem &recentWindowsItems(int count)
{
    TMenuItem *head = nullptr;
    TMenuItem *tail = nullptr;
    for (int i = 0; i < count; ++i)
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
    return makeMenuBar(r, 0); // no editors open yet, so no recent-window slots
}

TMenuBar *TurboApp::makeMenuBar(TRect r, int recentCount)
{
    r.b.y = r.a.y+1;

    // Build the Windows submenu separately so the recent-windows list can be
    // sized to the number of open editors. The file-tree toggle lives here,
    // beside the in-tree navigation.
    TSubMenu &windows = *new TSubMenu( "~W~indows", kbAltW ) +
        *new TMenuItem( "~Z~oom", cmZoom, kbF5, hcNoContext, "F5" ) +
        *new TMenuItem( "~R~esize/move",cmResize, kbCtrlF5, hcNoContext, "Ctrl-F5" ) +
        *new TMenuItem( "~N~ext", cmEditorNext, kbF6, hcNoContext, "F6" ) +
        *new TMenuItem( "~P~revious", cmEditorPrev, kbShiftF6, hcNoContext, "Shift-F6" ) +
        *new TMenuItem( "~C~lose", cmClose, kbAltF3, hcNoContext, "Alt-F3" ) +
        newLine() +
        *new TMenuItem( "File ~T~ree View", cmToggleTree, kbNoKey, hcNoContext ) +
        *new TMenuItem( "Previous (in tree)", cmTreePrev, kbAltUp, hcNoContext, "Alt-Up" ) +
        *new TMenuItem( "Next (in tree)", cmTreeNext, kbAltDown, hcNoContext, "Alt-Down" );
    if (recentCount > 0)
        windows + newLine() + recentWindowsItems(recentCount);

    return new TurboMenuBar( r,
        *new TSubMenu( "~F~ile", kbAltF, hcNoContext ) +
            *new TMenuItem( "~N~ew", cmNew, kbCtrlN, hcNoContext, "Ctrl-N" ) +
            *new TMenuItem( "~O~pen", cmOpen, kbCtrlO, hcNoContext, "Ctrl-O" ) +
            *new TMenuItem( "Go to ~A~nything...", cmGotoAnything, kbNoKey, hcNoContext, "Ctrl-P" ) +
            *new TMenuItem( "Command Pa~l~ette...", cmCommandPalette, kbNoKey, hcNoContext, "Ctrl-B" ) +
            newLine() +
            *new TMenuItem( "~S~ave", cmSave, kbCtrlS, hcNoContext, "Ctrl-S" ) +
            *new TMenuItem( "S~a~ve As...", cmSaveAs, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~R~ename...", cmRename, kbF2, hcNoContext, "F2" ) +
            newLine() +
            *new TMenuItem( "~C~lose", cmCloseEditor, kbCtrlW, hcNoContext, "Ctrl-W" ) +
            *new TMenuItem( "Close All", cmCloseAll, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "New ~T~erminal", cmNewTerminal, kbNoKey, hcNoContext ) +
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
            *new TMenuItem( "S~k~ip Occurrence", cmSkipOccurrence, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Undo Last Selecti~o~n", cmUndoSelection, kbNoKey, hcNoContext, "Ctrl-U" ) +
            newLine() +
            *new TMenuItem( "Add Caret ~U~p", cmAddCaretUp, kbNoKey, hcNoContext, "Ctrl-Alt-Up" ) +
            *new TMenuItem( "Add Caret ~D~own", cmAddCaretDown, kbNoKey, hcNoContext, "Ctrl-Alt-Down" ) +
            *new TMenuItem( "Spl~i~t into Lines", cmSplitSelectionLines, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~C~ode", kbAltC ) +
            *new TMenuItem( "Code ~F~olding", cmToggleFolding, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Fold a~t~ Cursor", cmFoldAtCursor, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Fold ~A~ll", cmFoldAll, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~U~nfold All", cmUnfoldAll, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "Toggle ~B~ookmark", cmToggleBookmark, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~N~ext Bookmark", cmNextBookmark, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~P~revious Bookmark", cmPrevBookmark, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~S~ettings", kbAltS ) +
            *new TMenuItem( "Line ~N~umbers", cmToggleLineNums, kbF8, hcNoContext, "F8" ) +
            *new TMenuItem( "Line ~W~rapping", cmToggleWrap, kbF9, hcNoContext, "F9" ) +
            *new TMenuItem( "Auto ~I~ndent", cmToggleIndent, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~H~idden Files", cmToggleHidden, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~A~uto-save on Focus Loss", cmToggleAutoSave, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Chan~g~e History", cmToggleChangeHistory, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Long Line G~u~ide", cmToggleEdge, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~C~olour Scheme...", cmThemeSettings, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~L~anguage Servers...", cmLspSettings, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~G~it", kbAltG ) +
            *new TMenuItem( "~C~ommit...", cmGitCommit, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~F~etch", cmGitFetch, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Pu~l~l", cmGitPull, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~P~ush", cmGitPush, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~R~efresh Status", cmGitRefresh, kbNoKey, hcNoContext ) +
        *new TSubMenu( "~B~uild", kbAltB ) +
            *new TMenuItem( "~B~uild", cmBuild, kbF7, hcNoContext, "F7" ) +
            *new TMenuItem( "~R~un", cmRun, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~T~est", cmTest, kbNoKey, hcNoContext ) +
            *new TMenuItem( "~S~top", cmStop, kbNoKey, hcNoContext ) +
            newLine() +
            *new TMenuItem( "~C~onfigure...", cmBuildConfig, kbNoKey, hcNoContext ) +
            *new TMenuItem( "Show/Hide ~O~utput", cmToggleOutput, kbNoKey, hcNoContext ) +
        windows +
        *new TSubMenu( "~H~elp", kbAltH ) +
            *new TMenuItem( "~K~eyboard shortcuts", cmHelp, kbF1, hcNoContext, "F1" ) +
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
            *new TStatusItem( 0, kbCtrlF5, cmResize ) +
            // Fuzzy navigation. Bound on the status line so they convert to a
            // command inside getEvent before the focused editor (Scintilla) sees
            // the key. Ctrl-P (Goto Anything) and Ctrl-B (Command Palette) are
            // both free single-Ctrl combos every terminal can send -- Ctrl-Shift-P
            // is avoided because terminals can't distinguish it from Ctrl-P.
            *new TStatusItem( 0, kbCtrlP, cmGotoAnything ) +
            *new TStatusItem( 0, kbCtrlB, cmCommandPalette ) +
            // Undo-selection on Ctrl+U (only fires with an editor focused, as the
            // command is disabled otherwise; converted before the editor sees the
            // key, like the navigation overlays above). Split-into-lines is NOT
            // bound to a key: in a terminal Ctrl+Shift+L is indistinguishable from
            // Ctrl+L (Cut current line), so binding it would shadow that. Reach
            // Split-into-lines from the Selection menu or the Command Palette.
            *new TStatusItem( 0, kbCtrlU, cmUndoSelection )
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
    pendingAfterBuild = nullptr;
    if (buildRunner)
        buildRunner->stop();
    stopBackgroundCommands();
    docTree = nullptr;
    clock = nullptr;
    branchView = nullptr;
    outputWin = nullptr;
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
    // Drain any output the terminal child processes produced since the last tick
    // and repaint the affected views (the reader threads wake the loop for us).
    for (auto *t : terminals)
        t->pump();
    // Stream build-command output into the output pane (same reader-thread +
    // wakeUp pattern as the terminals).
    if (buildRunner)
        buildRunner->pump();
    for (auto &j : bgJobs)
        if (j && j->runner)
            j->runner->pump();
    // Run any action deferred from a runner's onExit (e.g. start Run after a
    // build-first), now that we're safely outside that runner's pump.
    if (pendingAfterBuild)
    {
        auto action = std::move(pendingAfterBuild);
        pendingAfterBuild = nullptr;
        action();
    }
    // Keep menu check marks in sync with per-editor toggle state and the active
    // editor. Cheap: only rewrites a label when its checked state changes.
    refreshMenuChecks();
    refreshWindowList();
    refreshBranchView();
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
            case cmGotoAnything: gotoAnything(); break;
            case cmCommandPalette: commandPalette(); break;
            case cmBuild: runBuild(); break;
            case cmRun: runRun(); break;
            case cmTest: runTest(); break;
            case cmStop: stopAll(); break;
            case cmBuildConfig: editBuildConfig(); break;
            case cmToggleOutput: toggleOutputView(); break;
            case cmEditorNext:
            case cmEditorPrev:
                showEditorList(&event);
                break;
            case cmCloseAll: closeAll(); break;
            case cmToggleTree: toggleTreeView(); break;
            case cmNewTerminal: newTerminal(); break;
            case cmToggleHidden: toggleHiddenFiles(); break;
            case cmToggleAutoSave: toggleAutoSave(); break;
            case cmLspSettings: editLspSettings(); break;
            case cmThemeSettings: editThemeSettings(); break;
            case cmApplyTheme: applyActiveTheme(); break;
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

void TurboApp::openOrFocus(const std::string &absPath, long line) noexcept
{
    if (absPath.empty())
        return;
    EditorWindow *found = nullptr;
    MRUlist.forEach([&] (EditorWindow *w) {
        if (w && w->filePath() == absPath)
            found = w;
    });
    if (found)
        found->focus();
    else
    {
        fileOpenOrNew(absPath.c_str());
        found = MRUlist.empty() ? nullptr : MRUlist.next->self;
    }
    if (found && line >= 0)
    {
        auto &ed = found->getEditor();
        ed.callScintilla(SCI_GOTOLINE, line, 0U);
        ed.callScintilla(SCI_SCROLLCARET, 0U, 0U);
        ed.redraw();
    }
    frecency.record(absPath);
}

void TurboApp::gotoAnything()
{
    runGotoAnything(*this);
}

void TurboApp::commandPalette()
{
    ushort cmd = runCommandPalette(!MRUlist.empty());
    if (cmd)
    {
        // Dispatch through the normal event path so the command reaches the
        // focused editor/app exactly like a menu pick.
        TEvent ev;
        ev.what = evCommand;
        ev.message.command = cmd;
        ev.message.infoPtr = nullptr;
        putEvent(ev);
    }
}

void TurboApp::scanWorkspace()
{
    if (!docTree)
        return;
    char *cwd = ::getcwd(nullptr, 0);
    if (cwd)
    {
        projectRoot = cwd; // build/run commands run from here
        buildConfig.load(projectRoot); // .turbo/config.json (no-op if absent)
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

void TurboApp::rebuildMenuBar(int recentCount)
{
    if (menuBar)
    {
        remove(menuBar);
        TObject::destroy(menuBar);
    }
    TRect r = getExtent();
    menuBar = makeMenuBar(r, recentCount);
    insert(menuBar);
    // The branch indicator sits on top of the full-width menu bar's right end;
    // re-insert it so the freshly inserted menu bar doesn't cover it.
    if (branchView)
    {
        remove(branchView);
        insert(branchView);
    }
    menuRecentCount = recentCount;
    refreshMenuChecks(); // the new bar starts without check marks
}

void TurboApp::refreshWindowList() noexcept
{
    // Size the recent-window section to the number of open editors, rebuilding
    // the menu bar when that count changes so there are never empty rows.
    int count = 0;
    MRUlist.forEach([&] (EditorWindow *w) { if (w) ++count; });
    if (count > windowListMax)
        count = windowListMax;
    if (count != menuRecentCount)
        rebuildMenuBar(count);

    auto *bar = (TurboMenuBar *) menuBar;
    if (!bar)
        return;
    TMenu *m = bar->rootMenu();
    int i = 0;
    MRUlist.forEach([&] (EditorWindow *w) {
        if (i >= count || !w)
            return;
        // Entries are MRU-ordered, but each is prefixed with the window's stable
        // 1..9 number -- the same one Alt-1..9 selects -- so the menu and the
        // keyboard shortcut always agree. Windows past nine show no number.
        std::string label = (w->number >= 1 && w->number <= 9)
            ? std::to_string(w->number) + " " : "  ";
        for (char c : w->title) // escape '~' (TVision's hotkey marker)
        {
            if (c == '~') label += '~';
            label += c;
        }
        setMenuItemLabel(m, cmWindowBase + i, label.c_str(), true);
        ++i;
    });
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

void TurboApp::editThemeSettings()
{
    // The dialog mutates the active schemes and posts cmApplyTheme itself on
    // Apply/OK (handled synchronously by applyActiveTheme), so there's nothing
    // to do here on return. Cancel leaves the active schemes untouched.
    executeThemeDialog();
}

void TurboApp::applyActiveTheme() noexcept
{
    // Persist only the diffs from the built-in defaults.
    storeThemeToSettings(settings);
    saveSettings(settings);
    // Re-theme every open editor from the (now updated) active scheme. Editors
    // carry no per-editor scheme, so applyTheming falls back to schemeActive.
    MRUlist.forEach([] (EditorWindow *w) {
        if (!w)
            return;
        auto &ed = w->getEditor();
        turbo::applyTheming(ed.lexer, ed.scheme, ed.scintilla);
        ed.redraw();
    });
    // Repaint the window chrome (frames/scrollbars read windowSchemeActive).
    if (deskTop)
        deskTop->redraw();
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

static std::string trimmed(const char *s)
{
    std::string v = s;
    auto a = v.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    auto b = v.find_last_not_of(" \t");
    return v.substr(a, b - a + 1);
}

void TurboApp::treeCreateFile(const std::string &dirPath)
{
    char name[256] = "";
    if (inputBox("New File", "File ~n~ame:", name, sizeof(name) - 1) != cmOK)
        return;
    std::string n = trimmed(name);
    if (n.empty())
        return;
    std::string full = dirPath + "/" + n;
    std::error_code ec;
    if (std::filesystem::exists(full, ec))
    {
        messageBox(mfError | mfOKButton, "'%s' already exists.", n.c_str());
        return;
    }
    // Allow a relative path in the name (e.g. "sub/new.txt"): make the parents.
    auto parent = std::filesystem::path(full).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    {
        std::ofstream f(full, std::ios::out);
        if (!f)
        {
            messageBox(mfError | mfOKButton, "Unable to create '%s'.", full.c_str());
            return;
        }
    }
    // Show the new node immediately (the watcher would also pick it up), then
    // open it for editing.
    if (docTree)
        docTree->tree->addNode(full, false);
    fileOpenOrNew(full.c_str());
    if (git)
        git->requestStatus();
}

void TurboApp::treeRenamePath(const std::string &path, bool isDir)
{
    TStringView baseSv = TPath::basename(path);
    std::string base {baseSv.data(), baseSv.size()};
    char name[256] = "";
    strnzcpy(name, base.c_str(), sizeof name);
    if (inputBox(isDir ? "Rename Folder" : "Rename File",
                 "New ~n~ame:", name, sizeof(name) - 1) != cmOK)
        return;
    std::string n = trimmed(name);
    if (n.empty() || n == base)
        return;
    TStringView dirSv = TPath::dirname(path);
    std::string newPath {dirSv.data(), dirSv.size()};
    newPath += "/";
    newPath += n;
    std::error_code ec;
    if (std::filesystem::exists(newPath, ec))
    {
        messageBox(mfError | mfOKButton, "'%s' already exists.", n.c_str());
        return;
    }
    std::filesystem::rename(path, newPath, ec);
    if (ec)
    {
        messageBox(mfError | mfOKButton, "Unable to rename: %s.", ec.message().c_str());
        return;
    }
    // Re-point any open editor whose file is the renamed file, or lives inside
    // the renamed directory, at its new location.
    std::vector<EditorWindow *> affected;
    std::string oldPrefix = path + "/";
    std::string newPrefix = newPath + "/";
    MRUlist.forEach([&] (EditorWindow *w) {
        if (!w)
            return;
        const std::string &fp = w->filePath();
        std::string np;
        if (!isDir && fp == path)
            np = newPath;
        else if (isDir && fp.size() > oldPrefix.size() &&
                 fp.compare(0, oldPrefix.size(), oldPrefix) == 0)
            np = newPrefix + fp.substr(oldPrefix.size());
        else
            return;
        w->getEditor().filePath = np;
        w->getEditor().onFilePathSet(); // re-detect language for the new name
        affected.push_back(w);
    });
    // Rebuild the affected part of the tree (the watcher would reconcile this
    // eventually, but do it now so the rename is immediate and links are kept).
    if (docTree)
    {
        docTree->tree->removeNode(path);
        docTree->tree->addNode(newPath, isDir);
        for (auto *w : affected)
        {
            docTree->tree->linkEditor(w);   // attach to the freshly created node
            handleTitleChange(*w);          // renumber/retitle a renamed file
        }
    }
    if (git)
        git->requestStatus();
}

void TurboApp::treeStagePath(const std::string &path)
{
    if (!git || !git->isRepo())
    {
        messageBox("This folder is not a git repository.", mfInformation | mfOKButton);
        return;
    }
    git->stage({path}, [] (int code, const std::string &output) {
        if (code != 0)
        {
            std::string m = "git add failed:\n" +
                (output.empty() ? std::string("(see terminal)") : output.substr(0, 400));
            messageBox(m.c_str(), mfError | mfOKButton);
        }
    });
    // GitManager queues a status refresh after staging, so the badge updates.
}

void TurboApp::treeRevertPath(const std::string &path)
{
    if (!git || !git->isRepo())
    {
        messageBox("This folder is not a git repository.", mfInformation | mfOKButton);
        return;
    }
    TStringView baseSv = TPath::basename(path);
    std::string base {baseSv.data(), baseSv.size()};
    // Discarding local changes is irreversible, so confirm first.
    if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
                   "Discard all local changes to '%s'?\nThis cannot be undone.",
                   base.c_str()) != cmYes)
        return;
    std::string p = path;
    git->revert({p}, [this, p] (int code, const std::string &output) {
        if (code != 0)
        {
            std::string m = "git revert failed:\n" +
                (output.empty() ? std::string("(see terminal)") : output.substr(0, 400));
            messageBox(m.c_str(), mfError | mfOKButton);
            return;
        }
        // The file on disk is now the committed version; pull it into any open
        // editor so the buffer matches (and a later save doesn't undo the revert).
        reloadEditorFromDisk(p);
    });
    // GitManager queues a status refresh after the checkout, clearing the badge.
}

void TurboApp::reloadEditorFromDisk(const std::string &path) noexcept
{
    EditorWindow *target = nullptr;
    MRUlist.forEach([&] (EditorWindow *w) {
        if (w && w->filePath() == path)
            target = w;
    });
    if (!target)
        return;
    auto &ed = target->getEditor();
    ed.callScintilla(SCI_CLEARALL, 0U, 0U);
    turbo::readFile(ed.scintilla, path.c_str(), turbo::showNoDialogs);
    ed.callScintilla(SCI_EMPTYUNDOBUFFER, 0U, 0U);
    ed.callScintilla(SCI_SETSAVEPOINT, 0U, 0U); // mark the buffer clean (= on disk)
    ed.redraw();
}

void TurboApp::reloadCleanEditorsFromDisk() noexcept
{
    MRUlist.forEach([&] (EditorWindow *w) {
        if (!w || w->filePath().empty())
            return;
        auto &ed = w->getEditor();
        if (!ed.inSavePoint())
            return; // unsaved buffer: leave the user's work alone
        std::error_code ec;
        if (!std::filesystem::exists(w->filePath(), ec))
            return; // file absent on the new branch: keep what's in the editor
        ed.callScintilla(SCI_CLEARALL, 0U, 0U);
        turbo::readFile(ed.scintilla, w->filePath().c_str(), turbo::showNoDialogs);
        ed.callScintilla(SCI_EMPTYUNDOBUFFER, 0U, 0U);
        ed.callScintilla(SCI_SETSAVEPOINT, 0U, 0U);
        ed.redraw();
    });
}

void TurboApp::refreshBranchView() noexcept
{
    if (!branchView)
        return;
    // Text: branch icon (U+2387) + current branch (or a placeholder).
    std::string text = "\xE2\x8E\x87 ";
    bool repo = git && git->isRepo();
    if (!repo)
        text += "no repo";
    else
    {
        const std::string &cur = git->currentStatus().branch;
        text += cur.empty() ? "\xE2\x80\xA6" : cur; // ellipsis: not loaded yet
    }
    if (text == branchTextShown)
        return;
    branchTextShown = text;
    branchView->setText(text.c_str());
    // Size the indicator to its text and keep it pinned to the right edge.
    int w = strwidth(text.c_str());
    TRect ext = getExtent();
    TRect r {ext.b.x - w, ext.a.y, ext.b.x, ext.a.y + 1};
    branchView->locate(r);
}

void TurboApp::showBranchMenu(TPoint where) noexcept
{
    if (!git || !git->isRepo())
    {
        messageBox("This folder is not a git repository.", mfInformation | mfOKButton);
        return;
    }
    // Build the list of the OTHER branches fresh each time (no persistent items).
    const GitRepoStatus &st = git->currentStatus();
    std::vector<std::string> others;
    for (auto &b : st.branches)
        if (b != st.branch)
            others.push_back(b);

    TMenuItem *head = nullptr, *tail = nullptr;
    auto append = [&] (TMenuItem *it) {
        if (!head) head = tail = it;
        else { tail->next = it; tail = it; }
    };
    int n = (int) others.size();
    if (n > branchListMax)
        n = branchListMax;
    for (int i = 0; i < n; ++i)
        append(new TMenuItem(others[i].c_str(), cmBranchBase + i, kbNoKey, hcNoContext));
    if (!head)
    {
        auto *it = new TMenuItem("(no other branches)", cmBranchBase, kbNoKey, hcNoContext);
        it->disabled = True;
        append(it);
    }

    // popupMenu takes ownership of the chain (wraps it in a TMenu it deletes).
    unsigned short cmd = popupMenu(where, *head, nullptr);
    int idx = (int) cmd - cmBranchBase;
    if (idx >= 0 && idx < n)
        switchToBranch(others[idx]);
}

void TurboApp::switchToBranch(const std::string &branch) noexcept
{
    if (!git || !git->isRepo())
        return;

    // "Dirty" for checkout purposes = any change to a tracked file. Untracked
    // files don't block a normal checkout, so they don't trigger the prompt.
    bool dirty = false;
    for (auto &kv : git->currentStatus().files)
        if (kv.second.state != GitFileState::Untracked)
        {
            dirty = true;
            break;
        }

    auto onDone = [this, branch] (int code, const std::string &output) {
        if (code != 0)
        {
            std::string m = "Could not switch to '" + branch + "':\n" +
                (output.empty() ? std::string("(see terminal)") : output.substr(0, 400));
            messageBox(m.c_str(), mfError | mfOKButton);
            return;
        }
        // Working tree now matches the new branch; refresh editors to suit.
        reloadCleanEditorsFromDisk();
    };

    if (!dirty)
    {
        git->switchBranch(branch, GitManager::SwitchMode::Plain, onDone);
        return;
    }
    unsigned short choice = executeBranchSwitchDialog(branch.c_str());
    if (choice == cmYes)
        git->switchBranch(branch, GitManager::SwitchMode::Stash, onDone);
    else if (choice == cmNo)
        git->switchBranch(branch, GitManager::SwitchMode::Force, onDone);
    // cmCancel (or closing the dialog): stay on the current branch.
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
        // Leave room for the docked output pane at the bottom.
        if (outputWin && (outputWin->state & sfVisible))
            r.b.y = min(r.b.y, outputBounds().a.y);
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
    // Give it a stable 1..9 number for Turbo Vision's built-in Alt-1..9 window
    // selection. Assigned before inserting into the MRU list so the new window
    // isn't counted as already using a number; it stays fixed for the window's
    // lifetime and is freed for reuse when the window closes.
    w.number = lowestFreeWindowNumber();
    if (docTree)
        docTree->tree->linkEditor(&w);
    w.listHead.insert_after(&MRUlist);
    deskTop->insert(&w);
    enableCommands(editorCmds);
    if (lsp)
        lsp->didOpen(w);
}

short TurboApp::lowestFreeWindowNumber() noexcept
{
    bool used[10] = {}; // index 1..9
    MRUlist.forEach([&] (EditorWindow *win) {
        if (win && win->number >= 1 && win->number <= 9)
            used[win->number] = true;
    });
    for (short n = 1; n <= 9; ++n)
        if (!used[n])
            return n;
    return wnNoNumber; // more than 9 windows: extras are unnumbered
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
    // The output pane spans the editor area, so its width follows the tree.
    if (outputWin && (outputWin->state & sfVisible))
    {
        TRect ob = outputBounds();
        outputWin->locate(ob);
    }
    MRUlist.forEach([&] (auto *win) {
        win->setState(sfExposed, True);
    });
    deskTop->redraw();
}

void TurboApp::setTreeWidth(int w)
{
    if (!docTree || !(docTree->state & sfVisible))
        return;
    TRect ext = deskTop->getExtent();
    int totalW = ext.b.x - ext.a.x;
    // Keep the tree at least a window's minimum width (minWinSize.x = 16, so
    // locate() never clamps it wider than asked) and leave an editor's minimum
    // width beside it.
    w = max(16, min(w, totalW - EditorWindow::minSize.x));
    int oldLeft = docTree->getBounds().a.x;
    int newLeft = ext.b.x - w;
    if (newLeft == oldLeft)
        return;
    // Suppress per-editor redraws while we relocate; draw once at the end.
    MRUlist.forEach([&] (auto *win) { win->setState(sfExposed, False); });
    // Editors whose right edge sat on the tree's left edge follow it (narrowing
    // the tree grows them, widening shrinks them).
    MRUlist.forEach([&] (auto *win) {
        TRect r = win->getBounds();
        if (r.b.x >= oldLeft)
        {
            r.b.x = newLeft;
            win->locate(r);
        }
    });
    TRect tb = docTree->getBounds();
    tb.a.x = newLeft;
    docTree->locate(tb);
    // The output pane spans the editor area, so its width follows the tree too.
    if (outputWin && (outputWin->state & sfVisible))
    {
        TRect ob = outputBounds();
        outputWin->locate(ob);
    }
    MRUlist.forEach([&] (auto *win) { win->setState(sfExposed, True); });
    deskTop->redraw();
}

TRect TurboApp::outputBounds() const
{
    TRect ext = deskTop->getExtent();
    int totalH = ext.b.y - ext.a.y;
    // User-set height once dragged, else ~1/5 of the editor area. Keep the pane
    // at least 3 rows and leave at least an editor's minimum height (minWinSize.y
    // = 6) above, so the editor can actually shrink that far -- otherwise locate()
    // clamps it to its minimum and it overlaps (hides) the pane's top border.
    int h = (outputPaneHeight > 0) ? outputPaneHeight : max(4, totalH / 5);
    h = max(3, min(h, totalH - 6));
    TRect r = ext;
    r.a.y = ext.b.y - h;           // bottom slice
    // Span only the editor area: stop at the file tree's edge when it is shown.
    if (docTree && (docTree->state & sfVisible))
    {
        TRect t = docTree->getBounds();
        if (t.b.x >= ext.b.x)      // tree docked on the right
            r.b.x = t.a.x;
        else if (t.a.x <= ext.a.x) // tree docked on the left
            r.a.x = t.b.x;
    }
    return r;
}

void TurboApp::showOutput()
{
    if (outputWin && !(outputWin->state & sfVisible))
        toggleOutputView();
}

void TurboApp::toggleOutputView()
{
    if (!outputWin)
        return;
    // Suppress per-editor redraws while we relocate them; draw once at the end.
    MRUlist.forEach([&] (auto *win) { win->setState(sfExposed, False); });
    if (outputWin->state & sfVisible)
    {
        TRect orc = outputWin->getBounds();
        int paneH = orc.b.y - orc.a.y;
        outputWin->hide();
        MRUlist.forEach([&] (auto *win) {
            TRect r = win->getBounds();
            if (r.b.y <= orc.a.y)  // editor sat above the pane: grow back down
                r.b.y += paneH;
            win->locate(r);
        });
    }
    else
    {
        TRect orc = outputBounds();
        outputWin->locate(orc);
        MRUlist.forEach([&] (auto *win) {
            TRect r = win->getBounds();
            if (r.b.y > orc.a.y)   // shrink editors to sit above the pane
                r.b.y = orc.a.y;
            win->locate(r);
        });
        outputWin->show();
    }
    MRUlist.forEach([&] (auto *win) { win->setState(sfExposed, True); });
    deskTop->redraw();
}

void TurboApp::setOutputPaneHeight(int h)
{
    TRect ext = deskTop->getExtent();
    int totalH = ext.b.y - ext.a.y;
    h = max(3, min(h, totalH - 6)); // leave an editor's min height (6) above
    if (h == outputPaneHeight)
        return;
    if (!outputWin || !(outputWin->state & sfVisible))
    {
        outputPaneHeight = h; // remembered for the next time it is shown
        return;
    }
    int oldTop = outputWin->getBounds().a.y;
    outputPaneHeight = h;
    TRect nb = outputBounds();
    int newTop = nb.a.y;
    if (newTop == oldTop)
        return;
    // Move each editor's bottom that was sitting on the pane to the new pane top
    // (shrinking the pane grows them, growing the pane shrinks them).
    MRUlist.forEach([&] (auto *win) { win->setState(sfExposed, False); });
    MRUlist.forEach([&] (auto *win) {
        TRect r = win->getBounds();
        if (r.b.y >= oldTop)
        {
            r.b.y = newTop;
            win->locate(r);
        }
    });
    outputWin->locate(nb);
    MRUlist.forEach([&] (auto *win) { win->setState(sfExposed, True); });
    deskTop->redraw();
}

void TurboApp::runInOutput(const std::string &label, const std::string &command,
                           std::function<void(int)> onDone)
{
    showOutput();
    if (!outputWin)
        return;
    outputWin->view->clear();
    outputWin->view->addLine({"$ " + label, okInfo, "", -1});

    if (buildRunner)
        buildRunner->stop();
    buildRunner = std::make_unique<CommandRunner>();
    buildRunner->onLine = [this] (std::string_view line) {
        if (outputWin)
            outputWin->view->addLine(parseBuildLine(std::string(line), projectRoot));
    };
    buildRunner->onExit = [this, onDone] (int code) {
        if (outputWin)
            outputWin->view->addLine(
                { code == 0 ? std::string("[finished]")
                            : ("[exited with code " + std::to_string(code) + "]"),
                  okInfo, "", -1 });
        if (onDone)
            onDone(code);
    };
    std::string cwd = projectRoot.empty() ? std::string(".") : projectRoot;
    if (!buildRunner->start(command, cwd))
        outputWin->view->addLine({"Failed to start command.", okError, "", -1});
}

void TurboApp::runBuild()
{
    std::string command = buildConfig.build;
    if (command.empty())
    {
        // No build command configured yet: prompt for a one-off (configure a
        // default in Build > Configure to skip this).
        char cmd[256] = ""; // inputBox's length limit is a uchar (<= 255)
        strnzcpy(cmd, lastBuildCommand.c_str(), sizeof cmd);
        if (inputBox("Build", "Build ~c~ommand:", cmd, sizeof(cmd) - 1) != cmOK)
            return;
        command = trimmed(cmd);
        if (command.empty())
            return;
        lastBuildCommand = command;
    }
    runInOutput(command, command);
}

void TurboApp::editBuildConfig()
{
    if (executeBuildDialog(buildConfig))
        buildConfig.save(projectRoot);
}

void TurboApp::runTest()
{
    if (buildConfig.test.empty())
    {
        messageBox("No test command configured.\nSet one in Build > Configure.",
                   mfInformation | mfOKButton);
        return;
    }
    runInOutput(buildConfig.test, buildConfig.test);
}

bool TurboApp::isArtifactStale() const
{
    namespace fs = std::filesystem;
    if (buildConfig.artifact.empty())
        return true; // no artifact configured -> can't tell, treat as stale
    std::error_code ec;
    fs::path art = fs::path(projectRoot) / buildConfig.artifact;
    if (!fs::exists(art, ec))
        return true; // not built yet
    auto artTime = fs::last_write_time(art, ec);
    if (ec)
        return true;
    // Stale if any project source is newer than the artifact. Always skip the
    // artifact file itself, and -- when the artifact lives in a subdirectory
    // (a build/ dir) -- skip that whole directory so the build's own outputs
    // don't count as "sources". If the artifact sits in the project root, only
    // the artifact file is skipped.
    std::string root = fs::path(projectRoot).lexically_normal().string();
    std::string artFile = art.lexically_normal().string();
    std::string artDir = art.parent_path().lexically_normal().string();
    std::string dirPrefix;
    if (artDir.size() > root.size())
        dirPrefix = artDir + "/";
    std::vector<std::string> files;
    if (docTree && docTree->tree)
        docTree->tree->collectFilePaths(files);
    for (auto &f : files)
    {
        std::string fn = fs::path(f).lexically_normal().string();
        if (fn == artFile)
            continue;
        if (!dirPrefix.empty() && fn.rfind(dirPrefix, 0) == 0)
            continue;
        auto t = fs::last_write_time(fs::path(f), ec);
        if (!ec && t > artTime)
            return true;
    }
    return false;
}

bool TurboApp::needsBuildBeforeRun() const
{
    if (buildConfig.runMode == "run")
        return false;
    if (buildConfig.runMode == "build")
        return true;
    return isArtifactStale(); // "auto": build only when the artifact is stale
}

void TurboApp::runRun()
{
    if (buildConfig.run.empty())
    {
        messageBox("No run command configured.\nSet one in Build > Configure.",
                   mfInformation | mfOKButton);
        return;
    }
    if (needsBuildBeforeRun() && !buildConfig.build.empty())
    {
        // Build first; on success, start Run on the next idle tick (so we don't
        // tear down buildRunner from inside its own onExit/pump).
        runInOutput(buildConfig.build, buildConfig.build, [this] (int code) {
            if (code == 0)
                pendingAfterBuild = [this] { startRun(); };
            else if (outputWin)
                outputWin->view->addLine({"[build failed -- not running]", okError, "", -1});
        });
    }
    else
        startRun();
}

void TurboApp::startRun()
{
    runInOutput(buildConfig.run, buildConfig.run); // clears + streams in the pane
    startBackgroundCommands();                     // then note the background cmds
}

static std::string sanitizeName(const std::string &name) noexcept
{
    std::string s;
    for (char c : name)
        s += (std::isalnum((unsigned char) c) || c == '-' || c == '_') ? c : '_';
    return s.empty() ? std::string("job") : s;
}

void TurboApp::startBackgroundCommands()
{
    stopBackgroundCommands();
    if (projectRoot.empty() || buildConfig.extra.empty())
        return;
    std::string logDir = projectRoot + "/.turbo/logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    for (auto &cmd : buildConfig.extra)
    {
        if (cmd.command.empty())
            continue;
        auto job = std::make_unique<BackgroundJob>();
        job->name = cmd.name.empty() ? cmd.command : cmd.name;
        job->log.open(logDir + "/" + sanitizeName(job->name) + ".log", std::ios::trunc);
        BackgroundJob *jp = job.get();
        job->runner = std::make_unique<CommandRunner>();
        job->runner->onLine = [jp] (std::string_view line) {
            if (jp->log) { jp->log << line << '\n'; jp->log.flush(); }
        };
        job->runner->start(cmd.command, projectRoot);
        bgJobs.push_back(std::move(job));
    }
    if (outputWin && !bgJobs.empty())
        outputWin->view->addLine(
            { "[started " + std::to_string(bgJobs.size()) +
              " background command(s); output in .turbo/logs]", okInfo, "", -1 });
}

void TurboApp::stopBackgroundCommands()
{
    for (auto &j : bgJobs)
        if (j && j->runner)
            j->runner->stop();
    bgJobs.clear();
}

void TurboApp::stopAll()
{
    bool had = (buildRunner && buildRunner->running()) || !bgJobs.empty();
    pendingAfterBuild = nullptr;
    if (buildRunner)
        buildRunner->stop();
    stopBackgroundCommands();
    if (outputWin && had)
        outputWin->view->addLine({"[stopped]", okInfo, "", -1});
}

void TurboApp::newTerminal()
{
    // Open over the editor area (to the right of the file tree if it is shown),
    // like a new editor window; the user can then zoom/move/resize it as usual.
    TRect r = deskTop->getExtent();
    if (docTree && (docTree->state & sfVisible))
    {
        TRect t = docTree->getBounds();
        if (t.a.x > r.b.x - t.b.x)
            r.b.x = max(t.a.x, 20);
        else
            r.a.x = min(t.b.x, r.b.x - 20);
    }
    auto *win = new TerminalWindow(r);
    deskTop->insert(win);
}

void TurboApp::registerTerminal(TerminalView *t) noexcept
{
    terminals.push_back(t);
}

void TurboApp::unregisterTerminal(TerminalView *t) noexcept
{
    for (auto it = terminals.begin(); it != terminals.end(); ++it)
        if (*it == t)
        {
            terminals.erase(it);
            break;
        }
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
