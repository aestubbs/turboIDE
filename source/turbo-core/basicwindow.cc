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

// 24-bit window-chrome palette, coherent with the navy editor background and the
// Turbo-blue frames. The frame and scrollbar entries are what's normally visible
// on an editor window (and what the theme dialog exposes); the remaining
// button/label/cluster entries only show when dialog-like views are embedded in
// a window. Every entry -- including the input-line, history and list-viewer
// ones (e.g. the file-tree name filter and its list) -- is given an explicit RGB
// value here so nothing falls back to Turbo Vision's bright-blue BIOS dialog
// palette, which clashed with the dark theme.
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
    /* wndInputLineNormal          */ {wcFrameFgAct, wcFramePsv},   \
    /* wndInputLineSelected        */ {wcFrameFgAct, wcFrameAct},   \
    /* wndInputLineArrows          */ {wcBarArrows,  wcFramePsv},   \
    /* wndHistoryArrow             */ {wcFrameFgAct, wcBtnBg},      \
    /* wndHistorySides             */ {wcIcon,       wcNavy},       \
    /* wndHistWinScrollBarPageArea */ {wcBarThumb,   wcBarTrough},  \
    /* wndHistWinScrollBarControls */ {wcBarArrows,  wcBarTrough},  \
    /* wndListViewerNormal         */ {wcTextFg,     wcFrameAct},   \
    /* wndListViewerFocused        */ {wcFrameFgAct, wcBtnSelBg},   \
    /* wndListViewerSelected       */ {wcFrameFgAct, wcSelBg},      \
    /* wndListViewerDivider        */ {wcBarThumb,   wcNavy},       \
    /* wndInfoPane                 */ {wcTextFg,     wcNavy},       \
    /* wndClusterDisabled          */ {wcDim,        wcNavy},

extern constexpr WindowColorScheme windowSchemeDefault = { WINDOW_SCHEME_BODY };

// Runtime-editable copy; filled from the factory default at static-init and on
// reset. (Editors read it only at runtime, so the init order is safe.)
WindowColorScheme windowSchemeActive = { WINDOW_SCHEME_BODY };

#undef WINDOW_SCHEME_BODY

void resetWindowSchemeToDefault() noexcept
{
    for (int i = 0; i < WindowPaletteItemCount; ++i)
        windowSchemeActive[i] = windowSchemeDefault[i];
}

// Classic 16-colour (BIOS) window chrome. uchar(n) picks TColorDesired's
// constexpr BIOS constructor (a bare int would select the RGB one). Blue window
// interior with white/gray frames, cyan scrollbars and green buttons -- the
// authentic Turbo Vision look, distinct on any terminal's 16-colour palette.
namespace {
constexpr TColorDesired
    kwBlack  {uchar(0)},  kwBlue    {uchar(1)},  kwGreen  {uchar(2)},  kwCyan  {uchar(3)},
    kwGray   {uchar(7)},  kwDkGray  {uchar(8)},  kwLtGrn  {uchar(10)}, kwYellow{uchar(14)},
    kwWhite  {uchar(15)};
} // namespace

#define WINDOW_SCHEME_CLASSIC_BODY \
    /* wndFramePassive             */ {kwGray,   kwBlue},  \
    /* wndFrameActive              */ {kwWhite,  kwBlue},  \
    /* wndFrameIcon                */ {kwLtGrn,  kwBlue},  \
    /* wndScrollBarPageArea        */ {kwCyan,   kwBlue},  \
    /* wndScrollBarControls        */ {kwWhite,  kwBlue},  \
    /* wndStaticText               */ {kwGray,   kwBlue},  \
    /* wndLabelNormal              */ {kwGray,   kwBlue},  \
    /* wndLabelSelected            */ {kwWhite,  kwGreen}, \
    /* wndLabelShortcut            */ {kwYellow, kwBlue},  \
    /* wndButtonNormal             */ {kwBlack,  kwGreen}, \
    /* wndButtonDefault            */ {kwWhite,  kwGreen}, \
    /* wndButtonSelected           */ {kwWhite,  kwCyan},  \
    /* wndButtonDisabled           */ {kwDkGray, kwGreen}, \
    /* wndButtonShortcut           */ {kwYellow, kwGreen}, \
    /* wndButtonShadow             */ {kwBlack,  {}},      \
    /* wndClusterNormal            */ {kwGray,   kwBlue},  \
    /* wndClusterSelected          */ {kwWhite,  kwGreen}, \
    /* wndClusterShortcut          */ {kwYellow, kwBlue},  \
    /* wndInputLineNormal          */ {kwBlack,  kwCyan},  \
    /* wndInputLineSelected        */ {kwBlack,  kwCyan},  \
    /* wndInputLineArrows          */ {kwBlue,   kwCyan},  \
    /* wndHistoryArrow             */ {kwBlack,  kwCyan},  \
    /* wndHistorySides             */ {kwLtGrn,  kwBlue},  \
    /* wndHistWinScrollBarPageArea */ {kwCyan,   kwBlue},  \
    /* wndHistWinScrollBarControls */ {kwWhite,  kwBlue},  \
    /* wndListViewerNormal         */ {kwGray,   kwBlue},  \
    /* wndListViewerFocused        */ {kwWhite,  kwGreen}, \
    /* wndListViewerSelected       */ {kwBlack,  kwCyan},  \
    /* wndListViewerDivider        */ {kwCyan,   kwBlue},  \
    /* wndInfoPane                 */ {kwGray,   kwBlue},  \
    /* wndClusterDisabled          */ {kwDkGray, kwBlue},

extern const WindowColorScheme windowSchemeClassic = { WINDOW_SCHEME_CLASSIC_BODY };

#undef WINDOW_SCHEME_CLASSIC_BODY

void resetWindowSchemeToClassic() noexcept
{
    for (int i = 0; i < WindowPaletteItemCount; ++i)
        windowSchemeActive[i] = windowSchemeClassic[i];
}

} // namespace turbo
