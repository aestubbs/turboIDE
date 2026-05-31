#define Uses_TScrollBar
#define Uses_TFrame
#define Uses_TDrawBuffer
#define Uses_TProgram
#define Uses_MsgBox
#define Uses_TKeys
#define __INC_EDITORS_H
#include <tvision/tv.h>

#include <turbo/tpath.h>
#include <turbo/util.h>
#include "editwindow.h"
#include "app.h"
#include "apputils.h"
#include "search.h"
#include "gotoline.h"
#include <iostream>
#include <sstream>
using std::ios;

EditorFrame::EditorFrame(const TRect &bounds) noexcept :
    turbo::BasicEditorFrame(bounds)
{
}

void EditorFrame::draw()
{
    turbo::BasicEditorFrame::draw();
    // Draw the "[>]" reveal button left of the zoom icon (which sits at
    // size.x-5..size.x-3). Only meaningful while the frame is active.
    if ((state & sfActive) && size.x > 12)
    {
        TDrawBuffer b;
        TAttrPair cFrame = getColor(0x0503);
        b.moveCStr(0, "[\xE2\x96\xB6]", cFrame); // [►]
        writeLine(size.x - 9, 0, 3, 1, b);
    }
}

void EditorFrame::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown && (state & sfActive))
    {
        TPoint m = makeLocal(ev.mouse.where);
        if (m.y == 0 && m.x >= size.x - 9 && m.x <= size.x - 7)
        {
            message(TProgram::application, evCommand, cmRevealInTree, owner);
            clearEvent(ev);
            return;
        }
    }
    turbo::BasicEditorFrame::handleEvent(ev);
}

TFrame *EditorWindow::initFrame(TRect bounds)
{
    return new EditorFrame(bounds);
}

EditorWindow::EditorWindow( const TRect &bounds, TurboEditor &aEditor,
                            active_counter &fileCounter,
                            turbo::SearchSettings &searchSettings,
                            EditorWindowParent &aParent ) noexcept :
    TWindowInit(&EditorWindow::initFrame),
    super(bounds, aEditor),
    listHead(this),
    fileNumber(fileCounter),
    parent(aParent),
    searchState {searchSettings}
{
    // Commands that always get enabled when focusing the editor.
    enabledCmds += cmSave;
    enabledCmds += cmSaveAs;
    enabledCmds += cmToggleWrap;
    enabledCmds += cmToggleLineNums;
    enabledCmds += cmFind;
    enabledCmds += cmReplace;
    enabledCmds += cmGoToLine;
    enabledCmds += cmSearchAgain;
    enabledCmds += cmSearchPrev;
    enabledCmds += cmToggleIndent;
    enabledCmds += cmCloseEditor;
    enabledCmds += cmSelUppercase;
    enabledCmds += cmSelLowercase;
    enabledCmds += cmSelCapitalize;
    enabledCmds += cmToggleComment;
    enabledCmds += cmReplaceOne;
    enabledCmds += cmReplaceAll;
    enabledCmds += cmCompletion;
    enabledCmds += cmSelectNextOccurrence;
    enabledCmds += cmSelectAllOccurrences;
    enabledCmds += cmToggleBookmark;
    enabledCmds += cmNextBookmark;
    enabledCmds += cmPrevBookmark;
    enabledCmds += cmToggleFolding;
    enabledCmds += cmFoldAtCursor;
    enabledCmds += cmFoldAll;
    enabledCmds += cmUnfoldAll;
    enabledCmds += cmToggleChangeHistory;
    enabledCmds += cmToggleEdge;

    // Commands that always get disabled when unfocusing the editor.
    disabledCmds += enabledCmds;
    disabledCmds += cmRename;
}

void EditorWindow::shutDown()
{
    parent.removeEditor(*this);
    bottomView = nullptr;
    super::shutDown();
}

void EditorWindow::handleEvent(TEvent &ev)
{
    using namespace turbo;
    auto &editor = getEditor();
    TurboFileDialogs dlgs {parent};
    bool handled = true;
    switch (ev.what)
    {
        case evKeyDown:
            // Ctrl+Space requests completion. We match on the raw key + control
            // modifier rather than a fixed keyCode, since terminals encode it
            // inconsistently (and macOS Option+Space rarely arrives as Alt).
            if ( (ev.keyDown.controlKeyState & kbCtrlShift) &&
                 ev.keyDown.charScan.charCode == ' ' )
            {
                parent.editorRequestCompletion(*this);
                break;
            }
            switch (ev.keyDown.keyCode)
            {
                case kbEsc:
                    if ((handled = bottomView))
                        closeBottomView();
                    break;
                default:
                    handled = false;
            }
            break;
        case evCommand:
            switch (ev.message.command)
            {
                case cmSave:
                    editor.save(dlgs);
                    break;
                case cmSaveAs:
                    editor.saveAs(dlgs);
                    break;
                case cmRename:
                    editor.rename(dlgs);
                    break;
                case cmToggleWrap:
                    editor.wrapping.toggle(editor.scintilla);
                    editor.redraw();
                    break;
                case cmToggleLineNums:
                    editor.lineNumbers.toggle();
                    editor.redraw();
                    break;
                case cmToggleIndent:
                    editor.autoIndent.toggle();
                    break;
                case cmCloseEditor:
                    ev.message.command = cmClose;
                    handled = false;
                    break;
                case cmSelUppercase:
                    editor.uppercase();
                    editor.partialRedraw();
                    break;
                case cmSelLowercase:
                    editor.lowercase();
                    editor.partialRedraw();
                    break;
                case cmSelCapitalize:
                    editor.capitalize();
                    editor.partialRedraw();
                    break;
                case cmToggleComment:
                    editor.toggleComment();
                    editor.partialRedraw();
                    break;
                case cmFind:
                    openBottomView<FindBox>(searchState);
                    break;
                case cmReplace:
                    openBottomView<ReplaceBox>(searchState);
                    break;
                case cmSearchAgain:
                    editor.search(searchState.findText, sdForward, searchState.settingsPreset.get());
                    editor.partialRedraw();
                    break;
                case cmSearchPrev:
                    editor.search(searchState.findText, sdBackwards, searchState.settingsPreset.get());
                    editor.partialRedraw();
                    break;
                case cmSearchIncr:
                    editor.search(searchState.findText, sdForwardIncremental, searchState.settingsPreset.get());
                    editor.partialRedraw();
                    break;
                case cmReplaceOne:
                    editor.replace(searchState.findText, searchState.replaceText, rmReplaceOne, searchState.settingsPreset.get());
                    editor.partialRedraw();
                    break;
                case cmReplaceAll:
                    editor.replace(searchState.findText, searchState.replaceText, rmReplaceAll, searchState.settingsPreset.get());
                    editor.partialRedraw();
                    break;
                case cmClearReplace:
                    editor.clearReplaceIndicator();
                    editor.partialRedraw();
                    break;
                case cmSelectNextOccurrence:
                    editor.selectNextOccurrence();
                    editor.redraw();
                    break;
                case cmSelectAllOccurrences:
                    editor.selectAllOccurrences();
                    editor.redraw();
                    break;
                case cmToggleBookmark:
                    editor.toggleBookmark();
                    editor.redraw();
                    break;
                case cmNextBookmark:
                    editor.nextBookmark();
                    editor.redraw();
                    break;
                case cmPrevBookmark:
                    editor.prevBookmark();
                    editor.redraw();
                    break;
                case cmToggleFolding:
                    editor.toggleFolding();
                    editor.redraw();
                    break;
                case cmFoldAtCursor:
                    editor.toggleFoldAtCursor();
                    editor.redraw();
                    break;
                case cmFoldAll:
                    editor.foldAll(true);
                    editor.redraw();
                    break;
                case cmUnfoldAll:
                    editor.foldAll(false);
                    editor.redraw();
                    break;
                case cmToggleChangeHistory:
                    editor.toggleChangeHistory();
                    editor.redraw();
                    break;
                case cmToggleEdge:
                    editor.toggleEdge();
                    editor.redraw();
                    break;
                case cmGoToLine:
                    openBottomView<GoToLineBox>(editor);
                    break;
                case cmCompletion:
                    parent.editorRequestCompletion(*this);
                    break;
                case cmCloseView:
                    if ((handled = bottomView && ev.message.infoPtr == bottomView))
                        closeBottomView();
                    break;
                default:
                    handled = false;
            }
            break;
        default:
            handled = false;
    }
    if (handled)
        clearEvent(ev);
    else
        super::handleEvent(ev);
}

void EditorWindow::setState(ushort aState, Boolean enable)
{
    super::setState(aState, enable);
    if (aState == sfActive)
    {
        updateCommands();
        if (enable)
            parent.handleFocus(*this);
        else if ( parent.autoSaveOnFocusLoss() && !filePath().empty() &&
                  !getEditor().inSavePoint() )
        {
            // Auto-save the file when the editor loses focus, if enabled.
            // A non-empty path means save() writes directly without prompting.
            TurboFileDialogs dlgs {parent};
            getEditor().save(dlgs);
        }
    }
}

Boolean EditorWindow::valid(ushort command)
{
    auto &editor = getEditor();
    if (super::valid(command))
    {
        if (command != cmValid)
        {
            TurboFileDialogs dlgs {parent};
            return editor.close(dlgs);
        }
        return true;
    }
    return false;
}

const char* EditorWindow::getTitle(short)
{
    return formatTitle();
}

void EditorWindow::updateCommands() noexcept
{
    if (!filePath().empty())
        enabledCmds += cmRename;
    if (state & sfActive)
        enableCommands(enabledCmds);
    else
        disableCommands(disabledCmds);
}

void EditorWindow::handleNotification(const SCNotification &scn, turbo::Editor &editor)
{
    using namespace turbo;
    super::handleNotification(scn, editor);
    switch (scn.nmhdr.code)
    {
        case SCN_SAVEPOINTREACHED:
            updateCommands();
            parent.handleTitleChange(*this);
            editor.redraw();
            parent.editorSaved(*this);
            break;
        case SCN_MODIFIED:
            if (scn.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
                parent.editorTextChanged(*this);
            break;
        case SCN_CHARADDED:
            parent.editorCharAdded(*this, scn.ch);
            break;
        case SCN_DWELLSTART:
            parent.editorHoverStart(*this, scn.position);
            break;
        case SCN_DWELLEND:
            parent.editorHoverEnd(*this);
            break;
    }
}

static void growEditor(turbo::Editor &editor, int dy)
{
    turbo::forEachNotNull([&] (TView &v) {
        TRect r = v.getBounds();
        r.b.y += dy;
        v.setBounds(r);
    }, editor.view, editor.leftMargin);
}

void EditorWindow::closeBottomView()
// Pre: 'bottomView' is not null.
{
    int dy = bottomView->size.y + !!(bottomView->options & ofFramed);
    growEditor(editor, dy);
    destroy(bottomView);
    bottomView = nullptr;
    editor.redraw();
}

void EditorWindow::setBottomView(TView *view)
{
    if (bottomView)
        closeBottomView();
    insert(view);
    bottomView = view;
    int dy = -(bottomView->size.y + !!(bottomView->options & ofFramed));
    growEditor(editor, dy);
}

template <class T, class ...Args>
void EditorWindow::openBottomView(Args&& ...args)
{
    auto *view = (T *) message(bottomView, evCommand, T::findCommand, nullptr);
    if (!view)
    {
        TRect r = getExtent().grow(-1, -1);
        r.a.y = r.b.y - T::height;
        view = new T(r, static_cast<Args &&>(args)...);
        view->growMode = gfGrowAll & ~gfGrowLoX;
        view->setState(sfActive, state & sfActive);
        setBottomView(view);
        editor.redraw();
    }
    view->select();
    view->resetCurrent();
}

const char* EditorWindow::formatTitle(ushort flags) noexcept
{
    bool inSavePoint = (flags & tfNoSavePoint) || editor.inSavePoint();
    TitleState titleState {fileNumber.counter, fileNumber.number, inSavePoint};
    if (lastTitleState != titleState)
    {
        lastTitleState = titleState;
        TStringView name = filePath().empty() ? "Untitled" : TPath::basename(filePath());
        std::ostringstream os;
        os << name;
        if (fileNumber.number > 1)
            os << " (" << fileNumber.number << ')';
        if (!inSavePoint)
            os << '*';
        title = os.str();
    }
    return title.c_str();
}

void EditorWindow::sizeLimits(TPoint &min, TPoint &max)
{
    super::sizeLimits(min, max);
    if (bottomView)
        min.y = ::max(min.y, bottomView->size.y + !!(bottomView->options & ofFramed) + 3);
}
