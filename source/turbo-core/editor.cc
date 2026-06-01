#define Uses_TGroup
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <turbo/editor.h>
#include <turbo/util.h>
#include "utils.h"
#include <iostream>

namespace turbo {

Editor::Editor(TScintilla &aScintilla) noexcept :
    scintilla(aScintilla)
{
    // Editor should send notifications to this object.
    setParent(scintilla, this);
    // Set color defaults.
    applyTheming(nullptr, nullptr, scintilla);

    // Dynamic horizontal scroll.
    call(scintilla, SCI_SETSCROLLWIDTHTRACKING, true, 0U);
    call(scintilla, SCI_SETSCROLLWIDTH, 1, 0U);
    call(scintilla, SCI_SETXCARETPOLICY, CARET_EVEN, 0);
    // Trick so that the scroll width gets computed.
    call(scintilla, SCI_SETFIRSTVISIBLELINE, 1, 0U);
    call(scintilla, SCI_SETFIRSTVISIBLELINE, 0, 0U);

    // Enable wrapping markers
    call(scintilla, SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_END, 0U);

    // Indentation
    call(scintilla, SCI_SETUSETABS, false, 0U);
    call(scintilla, SCI_SETINDENT, 4, 0U);
    call(scintilla, SCI_SETTABWIDTH, 4, 0U);
    call(scintilla, SCI_SETTABINDENTS, true, 0U);
    call(scintilla, SCI_SETBACKSPACEUNINDENTS, true, 0U);

    // Margins: 0 = line numbers, 1 = bookmarks, 2 = change history, 3 = fold.
    // The extra margins start at width 0, so the default appearance is unchanged
    // until a feature is enabled.
    call(scintilla, SCI_SETMARGINS, 4, 0U);
    call(scintilla, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    setUpExtraMargins();
    updateMarginWidth();

    // Multiple selections / multi-cursor (typing replicates to every caret).
    call(scintilla, SCI_SETMULTIPLESELECTION, true, 0U);
    call(scintilla, SCI_SETADDITIONALSELECTIONTYPING, true, 0U);
    call(scintilla, SCI_SETADDITIONALCARETSVISIBLE, true, 0U);

    // Savepoint and undo buffer.
    call(scintilla, SCI_EMPTYUNDOBUFFER, 0U, 0U);
    call(scintilla, SCI_SETSAVEPOINT, 0U, 0U);
}

Editor::~Editor()
{
    destroyScintilla(scintilla);
}

void Editor::associate( EditorParent *aParent,
                        EditorView *aView, LeftMarginView *aLeftMargin,
                        TScrollBar *aHScrollBar, TScrollBar *aVScrollBar ) noexcept
{
    disassociate();
    parent = aParent;
    if (aView)
    {
        if (aView->editor)
            aView->editor->disassociate();
        aView->editor = this;
        aView->state |= sfCursorVis;
    }
    view = aView;
    if (aView && aLeftMargin)
    {
        // Place the margin to the left of the view, as if it was hidden.
        // If necessary, it will be made visible during redraw().
        TRect r = aView->getBounds();
        r.b.x = r.a.x;
        aLeftMargin->setBounds(r);
        aLeftMargin->editor = this; // so it can route fold-margin clicks
    }
    leftMargin = aLeftMargin;
    hScrollBar = aHScrollBar;
    vScrollBar = aVScrollBar;
}

void Editor::disassociate() noexcept
// Pre: if view != nullptr, view->editor == this.
// Post: if view != nullptr && leftMargin != nullptr, they are sized as if
//       the line numbers were hidden.
{
    parent = nullptr;
    if (view)
    {
        if (leftMargin)
        {
            TRect r = view->getBounds();
            r.a.x = leftMargin->getBounds().a.x;
            view->setBounds(r);
            leftMargin->size.x = 0;
        }
        view->editor = nullptr;
        view->state &= ~sfCursorVis;
    }
    if (leftMargin)
        leftMargin->editor = nullptr;
    view = nullptr;
    leftMargin = nullptr;
    hScrollBar = nullptr;
    vScrollBar = nullptr;
}

TPoint Editor::getEditorSize() noexcept
{
    if (view)
        return {
            view->size.x + (leftMargin ? leftMargin->size.x : 0),
            view->size.y,
        };
    return {0, 0};
}

void Editor::scrollBarEvent(TEvent &ev)
{
    // TScrollBar::handleEvent leads to a cmScrollBarChanged being messaged,
    // which EditorView handles with a call to redraw(). Hold the draw lock
    // to prevent such redraw from happening.
    bool lastDrawLock = drawLock;
    drawLock = true;
    if (hScrollBar)
        hScrollBar->handleEvent(ev);
    if (vScrollBar)
        vScrollBar->handleEvent(ev);
    drawLock = lastDrawLock;
}

void Editor::scrollTo(TPoint delta) noexcept
{
    // TScrollBar::setValue leads to a cmScrollBarChanged being messaged,
    // which EditorView handles with a call to redraw(). Hold the draw lock
    // to prevent such redraw from happening.
    bool lastDrawLock = drawLock;
    drawLock = true;
    if (hScrollBar)
        hScrollBar->setValue(delta.x);
    if (vScrollBar)
        vScrollBar->setValue(delta.y);
    drawLock = lastDrawLock;
}


void Editor::redraw() noexcept
{
    auto size = getEditorSize();
    if (redraw({0, 0, size.x, size.y}))
        invalidatedArea.clear();
}

void Editor::partialRedraw() noexcept
{
    if (redraw(invalidatedArea))
        invalidatedArea.clear();
}

void Editor::invalidate(TRect area) noexcept
{
    if (invalidatedArea.empty())
        invalidatedArea = area;
    else
        invalidatedArea.Union(area);
}

bool Editor::redraw(const TRect &area) noexcept
{
    if ( !drawLock && 0 <= area.a.x && area.a.x < area.b.x
                   && 0 <= area.a.y && area.a.y < area.b.y )
    {
        drawLock = true;
        updateMarginWidth();
        idleWork(scintilla);
        if (!reflowLock)
        {
            changeSize(scintilla);
            updateBraces(scheme, scintilla);
        }
        auto size = getEditorSize();
        TRect paintArea;
        if (surface.size != size)
        {
            surface.resize(size);
            paintArea = {{0, 0}, size};
        }
        else
            paintArea = area; // Read 'area' here since it may have mutated.
        paint(scintilla, surface, paintArea); // Emits SCN_PAINTED.
        if (changeHistoryEnabled)
            tintChangedLines();
        forEachNotNull([&] (TView &p) {
            p.drawView();
        }, vScrollBar, hScrollBar);
        forEachNotNull([&] (TSurfaceView &p) {
            drawWithSurface(p, &surface);
        }, leftMargin, view);
        drawLock = false;
        return true;
    }
    return false;
}

void Editor::updateMarginWidth() noexcept
{
    int lnWidth = lineNumbers.update(scintilla); // width of margin 0.
    // The single LeftMarginView shows columns [0, total) of the Scintilla
    // surface and the EditorView shows [total, ...). 'total' must therefore be
    // the combined width of every Scintilla margin: line numbers (margin 0)
    // plus the visible symbol margins (bookmarks, fold). Change history is
    // tracked via a width-0 margin and rendered as a line-number background
    // tint instead of a glyph, so it adds no width.
    int total = lnWidth;
    int extraWidth = 0;
    int marginCount = (int) call(scintilla, SCI_GETMARGINS, 0U, 0U);
    for (int i = 1; i < marginCount; ++i)
        extraWidth += (int) call(scintilla, SCI_GETMARGINWIDTHN, i, 0U);
    total += extraWidth;
    if (leftMargin)
    {
        TRect mr = leftMargin->getBounds();
        mr.b.x = mr.a.x + total;
        leftMargin->setBounds(mr);
        if (view)
        {
            // Keep a full separator column between the margin and the text: the
            // framed LeftMarginView draws its border there. Symbol margins (e.g.
            // fold) add their own real columns to the left of that border rather
            // than overwriting it, so the border and the markers never collide.
            int sep = leftMargin->distanceFromView * (lnWidth != 0);
            TRect vr = view->getBounds();
            vr.a.x = mr.b.x + sep;
            view->setBounds(vr);
            view->delta = {total, 0};
        }
    }
}

bool Editor::handleScrollBarChanged(TScrollBar *s)
{
    if (s == hScrollBar)
    {
        call(scintilla, SCI_SETXOFFSET, s->value, 0U);
        return true;
    }
    else if (s == vScrollBar)
    {
        call(scintilla, SCI_SETFIRSTVISIBLELINE, s->value, 0U);
        return true;
    }
    return false;
}

void Editor::handleNotification(const SCNotification &scn)
{
    switch (scn.nmhdr.code)
    {
        case SCN_CHARADDED:
            if (scn.ch == '\n')
                autoIndent.applyToCurrentLine(scintilla);
            break;
        case SCN_MODIFIED:
            if (scn.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
                markLineChanged(scn.position, scn.linesAdded);
            break;
        case SCN_SAVEPOINTREACHED:
            clearChangeHistory();
            break;
    }
    if (parent)
        parent->handleNotification(scn, *this);
}

void Editor::setHorizontalScrollPos(int delta, int limit) noexcept
{
    if (view && hScrollBar)
    {
        auto size = view->size.x;
        hScrollBar->setParams(delta, 0, limit - size, size - 1, 1);
    }
}

void Editor::setVerticalScrollPos(int delta, int limit) noexcept
{
    if (view && vScrollBar)
    {
        auto size = view->size.y;
        vScrollBar->setParams(delta, 0, limit - size, size - 1, 1);
    }
}

bool Editor::inSavePoint()
{
    return call(scintilla, SCI_GETMODIFY, 0U, 0U) == 0;
}

// --- IDE features ----------------------------------------------------------

// Marker numbers. Bookmarks use a low, free number; change-history and fold use
// Scintilla's reserved ranges. The terminal Surface can only draw fills and
// text, so every marker is defined as SC_MARK_CHARACTER (drawn via DrawText) or
// SC_MARK_EMPTY for the fold connector lines (which would need line drawing).
static constexpr int markBookmark = 1;
static constexpr int markChanged = 2;

void Editor::setUpExtraMargins() noexcept
{
    // Margin 1: bookmarks.
    call(scintilla, SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    call(scintilla, SCI_SETMARGINMASKN, 1, 1 << markBookmark);
    call(scintilla, SCI_SETMARGINWIDTHN, 1, 0);
    call(scintilla, SCI_MARKERDEFINE, markBookmark, SC_MARK_CHARACTER + '#');
    call(scintilla, SCI_MARKERSETFORE, markBookmark, 0x00FFFF); // cyan-ish (BGR)

    // Margin 2: change history (lines modified since the last save). This
    // vendored Scintilla predates SCI_SETCHANGEHISTORY, so we track it manually:
    // SCN_MODIFIED adds the markChanged marker on edited lines (see
    // handleNotification) and it is cleared on save.
    call(scintilla, SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    call(scintilla, SCI_SETMARGINMASKN, 2, 1 << markChanged);
    call(scintilla, SCI_SETMARGINWIDTHN, 2, 0);
    call(scintilla, SCI_MARKERDEFINE, markChanged, SC_MARK_CHARACTER + '|');
    call(scintilla, SCI_MARKERSETFORE, markChanged, 0x00A5FF); // orange (BGR)

    // Margin 3: fold. Use character glyphs for the open/closed markers and
    // empty markers for the connector lines (which can't be drawn in a cell).
    call(scintilla, SCI_SETMARGINTYPEN, 3, SC_MARGIN_SYMBOL);
    call(scintilla, SCI_SETMARGINMASKN, 3, SC_MASK_FOLDERS);
    call(scintilla, SCI_SETMARGINSENSITIVEN, 3, 1);
    call(scintilla, SCI_SETMARGINWIDTHN, 3, 0);
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_CHARACTER + '+');
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_CHARACTER + '-');
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_CHARACTER + '+');
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_CHARACTER + '-');
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY);
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY);
    call(scintilla, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY);
    // Let Scintilla keep folds consistent across edits and expand on actions.
    call(scintilla, SCI_SETAUTOMATICFOLD,
         SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CHANGE, 0U);
}

void Editor::selectNextOccurrence() noexcept
{
    // If the main selection is empty, Scintilla first selects the word at the
    // caret; otherwise it adds the next occurrence as an additional selection.
    call(scintilla, SCI_MULTIPLESELECTADDNEXT, 0U, 0U);
}

void Editor::selectAllOccurrences() noexcept
{
    if (call(scintilla, SCI_GETSELECTIONEMPTY, 0U, 0U))
        call(scintilla, SCI_MULTIPLESELECTADDNEXT, 0U, 0U); // seed with the word
    call(scintilla, SCI_MULTIPLESELECTADDEACH, 0U, 0U);
}

void Editor::toggleFolding() noexcept
{
    foldingEnabled = !foldingEnabled;
    call(scintilla, SCI_SETMARGINWIDTHN, 3, foldingEnabled ? 1 : 0);
    if (!foldingEnabled)
        call(scintilla, SCI_FOLDALL, SC_FOLDACTION_EXPAND, 0U); // reveal everything
}

void Editor::toggleFoldAtCursor() noexcept
{
    if (!foldingEnabled)
        toggleFolding();
    long pos = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    long line = call(scintilla, SCI_LINEFROMPOSITION, pos, 0U);
    call(scintilla, SCI_TOGGLEFOLD, line, 0U);
}

void Editor::foldAll(bool contract) noexcept
{
    if (!foldingEnabled)
        toggleFolding();
    call(scintilla, SCI_FOLDALL,
         contract ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND, 0U);
}

bool Editor::marginClick(int localX, int localY) noexcept
{
    // The terminal margin isn't a real Scintilla margin (it's our own surface
    // composite), so route the click ourselves. Only the fold column is
    // interactive: it is the last column of the left margin when folding is on.
    if (!foldingEnabled)
        return false;
    int foldWidth = (int) call(scintilla, SCI_GETMARGINWIDTHN, 3, 0U);
    if (foldWidth <= 0)
        return false;
    int marginWidth = 0;
    int marginCount = (int) call(scintilla, SCI_GETMARGINS, 0U, 0U);
    for (int i = 0; i < marginCount; ++i)
        marginWidth += (int) call(scintilla, SCI_GETMARGINWIDTHN, i, 0U);
    // Fold column occupies [marginWidth - foldWidth, marginWidth).
    if (localX < marginWidth - foldWidth || localX >= marginWidth)
        return false;
    long firstVisible = call(scintilla, SCI_GETFIRSTVISIBLELINE, 0U, 0U);
    long docLine = call(scintilla, SCI_DOCLINEFROMVISIBLE, firstVisible + localY, 0U);
    long level = call(scintilla, SCI_GETFOLDLEVEL, docLine, 0U);
    if (!(level & SC_FOLDLEVELHEADERFLAG))
        return false; // not a foldable line
    call(scintilla, SCI_TOGGLEFOLD, docLine, 0U);
    redraw();
    return true;
}

void Editor::toggleBookmark() noexcept
{
    bookmarksUsed = true;
    call(scintilla, SCI_SETMARGINWIDTHN, 1, 1);
    long pos = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    long line = call(scintilla, SCI_LINEFROMPOSITION, pos, 0U);
    if (call(scintilla, SCI_MARKERGET, line, 0U) & (1 << markBookmark))
        call(scintilla, SCI_MARKERDELETE, line, markBookmark);
    else
        call(scintilla, SCI_MARKERADD, line, markBookmark);
}

void Editor::nextBookmark() noexcept
{
    long pos = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    long line = call(scintilla, SCI_LINEFROMPOSITION, pos, 0U);
    long found = call(scintilla, SCI_MARKERNEXT, line + 1, 1 << markBookmark);
    if (found < 0) // wrap around to the top
        found = call(scintilla, SCI_MARKERNEXT, 0, 1 << markBookmark);
    if (found >= 0)
        call(scintilla, SCI_GOTOLINE, found, 0U);
}

void Editor::prevBookmark() noexcept
{
    long pos = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    long line = call(scintilla, SCI_LINEFROMPOSITION, pos, 0U);
    long found = call(scintilla, SCI_MARKERPREVIOUS, line - 1, 1 << markBookmark);
    if (found < 0) // wrap around to the bottom
    {
        long last = call(scintilla, SCI_GETLINECOUNT, 0U, 0U) - 1;
        found = call(scintilla, SCI_MARKERPREVIOUS, last, 1 << markBookmark);
    }
    if (found >= 0)
        call(scintilla, SCI_GOTOLINE, found, 0U);
}

void Editor::toggleChangeHistory() noexcept
{
    // Only toggles *display*: changes are tracked continuously (see
    // markLineChanged) so that enabling the feature immediately shows every
    // line edited since the last save, not just edits made afterwards. The
    // markers are rendered as a green tint on the line-number area
    // (tintChangedLines), not a glyph that would collide with the border.
    changeHistoryEnabled = !changeHistoryEnabled;
}

void Editor::markLineChanged(long pos, long linesAdded) noexcept
{
    // Track unconditionally, even when display is off, so toggling the feature
    // on reveals pre-existing unsaved edits.
    long line = call(scintilla, SCI_LINEFROMPOSITION, pos, 0U);
    long last = line + (linesAdded > 0 ? linesAdded : 0);
    for (long l = line; l <= last; ++l)
        call(scintilla, SCI_MARKERADD, l, markChanged);
}

void Editor::clearChangeHistory() noexcept
{
    // Called on save: the file on disk now matches, so nothing is "modified".
    call(scintilla, SCI_MARKERDELETEALL, markChanged, 0U);
}

void Editor::tintChangedLines() noexcept
{
    // Paint a green background across the line-number area (and the separator
    // column, i.e. the whole left margin width) of every visible line that has
    // an unsaved change. This replaces the old change-history glyph, which
    // collided with the framed margin border. Done directly on the painted
    // surface so it needs no extra margin column and no Scintilla changes.
    if (surface.size.y <= 0)
        return;
    // Tint the whole left-margin width (line numbers + any symbol columns such
    // as fold). This is the part of the surface shown by the LeftMarginView;
    // the framed border sits just past it and is drawn separately. We stop at
    // the margin edge so the first code character is never tinted.
    int marginWidth = 0;
    int marginCount = (int) call(scintilla, SCI_GETMARGINS, 0U, 0U);
    for (int i = 0; i < marginCount; ++i)
        marginWidth += (int) call(scintilla, SCI_GETMARGINWIDTHN, i, 0U);
    if (marginWidth <= 0)
        return;
    int tintWidth = min(marginWidth, surface.size.x);
    long firstVisible = call(scintilla, SCI_GETFIRSTVISIBLELINE, 0U, 0U);
    TColorDesired green = TColorRGB(0x107010); // dark green
    for (int y = 0; y < surface.size.y; ++y)
    {
        long visibleLine = firstVisible + y;
        long docLine = call(scintilla, SCI_DOCLINEFROMVISIBLE, visibleLine, 0U);
        if (!(call(scintilla, SCI_MARKERGET, docLine, 0U) & (1 << markChanged)))
            continue;
        // Only tint the first visual row of the line (the one with the line
        // number), not wrap/annotation continuation rows of the same doc line.
        long prevDoc = visibleLine > 0
            ? call(scintilla, SCI_DOCLINEFROMVISIBLE, visibleLine - 1, 0U) : -1;
        if (prevDoc == docLine)
            continue;
        for (int x = 0; x < tintWidth; ++x)
            ::setBack(surface.at(y, x).attr, green);
    }
}

void Editor::toggleEdge() noexcept
{
    edgeEnabled = !edgeEnabled;
    if (edgeEnabled)
    {
        call(scintilla, SCI_SETEDGECOLUMN, edgeColumn, 0U);
        call(scintilla, SCI_SETEDGECOLOUR, 0x303030, 0U); // dim grey tint (BGR)
        call(scintilla, SCI_SETEDGEMODE, EDGE_BACKGROUND, 0U);
    }
    else
        call(scintilla, SCI_SETEDGEMODE, EDGE_NONE, 0U);
}

} // namespace turbo
