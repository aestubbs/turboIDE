#define Uses_TApplication
#define Uses_TDialog
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <turbo/basicwindow.h>
#include <turbo/basicframe.h>
#include <iostream>

namespace turbo {

TFrame* BasicEditorWindow::initFrame(TRect bounds)
{
    return new BasicEditorFrame(bounds);
}

BasicEditorWindow::BasicEditorWindow(const TRect &bounds, Editor &aEditor) :
    TWindowInit(&initFrame),
    TWindow(bounds, nullptr, wnNoNumber),
    editor(aEditor)
{
    options |= ofTileable | ofFirstClick;
    setState(sfShadow, False);

    auto *editorView = new EditorView(TRect(1, 1, size.x - 1, size.y - 1));
    insert(editorView);

    auto *leftMargin = new LeftMarginView(leftMarginSep);
    leftMargin->options |= ofFramed;
    insert(leftMargin);

    auto *hScrollBar = new TScrollBar(TRect(18, size.y - 1, size.x - 2, size.y));
    hScrollBar->hide();
    insert(hScrollBar);

    auto *vScrollBar = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
    vScrollBar->hide();
    insert(vScrollBar);

    editor.associate(this, editorView, leftMargin, hScrollBar, vScrollBar);
}

BasicEditorWindow::~BasicEditorWindow()
{
    delete &editor;
}

void BasicEditorWindow::shutDown()
{
    editor.disassociate();
    TWindow::shutDown();
}

void BasicEditorWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    if (aState == sfActive && editor.parent == this)
    {
        editor.hScrollBar->setState(sfVisible, enable);
        editor.vScrollBar->setState(sfVisible, enable);
    }
}

void BasicEditorWindow::dragView(TEvent& event, uchar mode, TRect& limits, TPoint minSize, TPoint maxSize)
{
    auto lastSize = size;
    editor.lockReflow([&] {
        TWindow::dragView(event, mode, limits, minSize, maxSize);
    });
    if (lastSize != size)
        editor.redraw(); // Redraw without reflow lock.
}

void BasicEditorWindow::sizeLimits(TPoint &min, TPoint &max)
{
    TView::sizeLimits(min, max);
    min = minSize;
}

TColorAttr BasicEditorWindow::mapColor(uchar index) noexcept
{
    if (0 < index && index - 1 < WindowPaletteItemCount)
        return getScheme()[index - 1];
    return errorAttr;
}

void BasicEditorWindow::handleNotification(const SCNotification &scn, Editor &editor)
{
    switch (scn.nmhdr.code)
    {
        case SCN_PAINTED:
            if (!(state & sfDragging) && frame) // It already gets drawn when resizing.
                frame->drawView(); // The frame is sensible to the cursor position and the save point state.
            break;
    }
}

#define dialogColor(i) cpAppColor[(uchar) (cpDialog[i] - 1)]

// 24-bit window-chrome palette, coherent with the navy editor background and the
// Turbo-blue frames. The frame and scrollbar entries are what's normally visible
// on an editor window (and what the theme dialog exposes); the remaining
// button/label/cluster entries only show when dialog-like views are embedded in
// a window, but are given matching RGB values for consistency. The input-line,
// history and list-viewer entries are inherited from the application dialog
// palette via 'dialogColor' (left as-is; they degrade gracefully).
// TColorDesired (not TColorRGB): its int constructor is constexpr, which the
// constexpr scheme below requires.
namespace {
constexpr TColorDesired
    wcNavy       = 0x10182E, // window background (matches editor bg)
    wcFramePsv   = 0x16335E, // passive frame background
    wcFrameAct   = 0x1E4D8C, // active frame background (Turbo blue)
    wcFrameFgPsv = 0xC8D4F0, // passive frame text
    wcFrameFgAct = 0xFFFFFF, // active frame text
    wcIcon       = 0x7FE0B0, // frame icons (close/zoom) -- green-teal
    wcBarTrough  = 0x1A2E52, // scrollbar trough background
    wcBarThumb   = 0x3A5C92, // scrollbar slider
    wcBarArrows  = 0xD0DEFF, // scrollbar arrow controls
    wcTextFg     = 0xE0E6F8, // static text
    wcLabelFg    = 0xB8C2E0, // label text
    wcShortcut   = 0xE8C07D, // hotkey letters (gold)
    wcSelBg      = 0x1E4D8C, // selected label/cluster background
    wcBtnBg      = 0x2A5896, // button background
    wcBtnDefBg   = 0x2F6FB0, // default button background
    wcBtnSelBg   = 0x3A7FD0, // focused button background
    wcDim        = 0x6A7390, // disabled foreground
    wcDimBg      = 0x1A2540, // disabled button background
    wcShadow     = 0x0A1020; // button shadow
} // namespace

#define WINDOW_SCHEME_BODY \
    /* wndFramePassive             */ {wcFrameFgPsv, wcFramePsv},   \
    /* wndFrameActive              */ {wcFrameFgAct, wcFrameAct},   \
    /* wndFrameIcon                */ {wcIcon,       wcFrameAct},   \
    /* wndScrollBarPageArea        */ {wcBarThumb,   wcBarTrough},  \
    /* wndScrollBarControls        */ {wcBarArrows,  wcBarTrough},  \
    /* wndStaticText               */ {wcTextFg,     wcNavy},       \
    /* wndLabelNormal              */ {wcLabelFg,    wcNavy},       \
    /* wndLabelSelected            */ {wcFrameFgAct, wcSelBg},      \
    /* wndLabelShortcut            */ {wcShortcut,   wcNavy},       \
    /* wndButtonNormal             */ {wcFrameFgAct, wcBtnBg},      \
    /* wndButtonDefault            */ {0xF0F4FF,     wcBtnDefBg},   \
    /* wndButtonSelected           */ {wcFrameFgAct, wcBtnSelBg},   \
    /* wndButtonDisabled           */ {wcDim,        wcDimBg},      \
    /* wndButtonShortcut           */ {wcShortcut,   wcBtnBg},      \
    /* wndButtonShadow             */ {wcShadow,     {}},           \
    /* wndClusterNormal            */ {wcLabelFg,    wcNavy},       \
    /* wndClusterSelected          */ {wcFrameFgAct, wcSelBg},      \
    /* wndClusterShortcut          */ {wcShortcut,   wcNavy},       \
    dialogColor(wndInputLineNormal         ),                      \
    dialogColor(wndInputLineSelected       ),                      \
    dialogColor(wndInputLineArrows         ),                      \
    /* wndHistoryArrow             */ {wcFrameFgAct, wcBtnBg},      \
    /* wndHistorySides             */ {wcIcon,       wcNavy},       \
    dialogColor(wndHistWinScrollBarPageArea),                      \
    dialogColor(wndHistWinScrollBarControls),                      \
    dialogColor(wndListViewerNormal        ),                      \
    dialogColor(wndListViewerFocused       ),                      \
    dialogColor(wndListViewerSelected      ),                      \
    dialogColor(wndListViewerDivider       ),                      \
    dialogColor(wndInfoPane                ),                      \
    /* wndClusterDisabled          */ {wcDim,        wcNavy},

extern constexpr WindowColorScheme windowSchemeDefault = { WINDOW_SCHEME_BODY };

// Runtime-editable copy; filled from the factory default at static-init and on
// reset. (Editors read it only at runtime, so the init order is safe.)
WindowColorScheme windowSchemeActive = { WINDOW_SCHEME_BODY };

#undef WINDOW_SCHEME_BODY
#undef dialogColor

void resetWindowSchemeToDefault() noexcept
{
    for (int i = 0; i < WindowPaletteItemCount; ++i)
        windowSchemeActive[i] = windowSchemeDefault[i];
}

} // namespace turbo
