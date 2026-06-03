#define Uses_TKeys
#define Uses_TEvent
#define Uses_TDrawSurface
#include <tvision/tv.h>

#include <turbo/scintilla/tscintilla.h>
#include <turbo/scintilla.h>
#include "platform/surface.h"
#include <chrono>

namespace turbo {

TScintilla &createScintilla() noexcept
{
    return *new TScintilla;
}

void destroyScintilla(TScintilla &self) noexcept
{
    delete &self;
}

sptr_t call(TScintilla &self, uint iMessage, uptr_t wParam, sptr_t lParam)
{
    return self.WndProc((Scintilla::Message) iMessage, wParam, lParam);
}

void setParent(TScintilla &self, TScintillaParent *aParent)
{
    self.setParent(aParent);
}

void changeSize(TScintilla &self)
{
    self.ChangeSize();
}

void clearBeforeTentativeStart(TScintilla &self)
{
    self.ClearBeforeTentativeStart();
}

void insertPasteStream(TScintilla &self, TStringView text)
{
    self.InsertPasteShape(text.data(), text.size(), TScintilla::PasteShape::stream);
}

void insertCharacter(TScintilla &self, TStringView text)
{
    self.InsertCharacter(text, Scintilla::CharacterSource::DirectInput);
}

void idleWork(TScintilla &self)
{
    self.IdleWork();
}

TPoint pointMainCaret(TScintilla &self)
{
    auto p = self.PointMainCaret();
    return {(int) p.x, (int) p.y};
}

static int convertModifiers(ulong controlKeyState)
{
    static constexpr struct { ushort tv; int scmod; } modifiersTable[] =
    {
        {kbShift,       SCMOD_SHIFT},
        {kbCtrlShift,   SCMOD_CTRL},
        {kbAltShift,    SCMOD_ALT}
    };

    int modifiers = 0;
    for (const auto &m : modifiersTable)
        if (controlKeyState & m.tv)
            modifiers |= m.scmod;
    return modifiers;
}

bool handleKeyDown(TScintilla &self, const KeyDownEvent &keyDown)
{
    static constexpr struct { ushort tv; int sck; } keysTable[] =
    {
        {kbDown,        SCK_DOWN},
        {kbUp,          SCK_UP},
        {kbLeft,        SCK_LEFT},
        {kbRight,       SCK_RIGHT},
        {kbHome,        SCK_HOME},
        {kbEnd,         SCK_END},
        {kbPgUp,        SCK_PRIOR},
        {kbPgDn,        SCK_NEXT},
        {kbDel,         SCK_DELETE},
        {kbIns,         SCK_INSERT},
        {kbTab,         SCK_TAB},
        {kbEnter,       SCK_RETURN},
        {kbBack,        SCK_BACK},
        {kbShiftDel,    SCK_DELETE},
        {kbShiftIns,    SCK_INSERT},
        {kbShiftTab,    SCK_TAB},
        {kbCtrlDown,    SCK_DOWN},
        {kbCtrlUp,      SCK_UP},
        {kbCtrlLeft,    SCK_LEFT},
        {kbCtrlRight,   SCK_RIGHT},
        {kbCtrlHome,    SCK_HOME},
        {kbCtrlEnd,     SCK_END},
        {kbCtrlPgUp,    SCK_PRIOR},
        {kbCtrlPgDn,    SCK_NEXT},
        {kbCtrlDel,     SCK_DELETE},
        {kbCtrlIns,     SCK_INSERT},
        {kbCtrlEnter,   SCK_RETURN},
        {kbCtrlBack,    SCK_BACK},
        {kbAltDown,     SCK_DOWN},
        {kbAltUp,       SCK_UP},
        {kbAltLeft,     SCK_LEFT},
        {kbAltRight,    SCK_RIGHT},
        {kbAltHome,     SCK_HOME},
        {kbAltEnd,      SCK_END},
        {kbAltPgUp,     SCK_PRIOR},
        {kbAltPgDn,     SCK_NEXT},
        {kbAltDel,      SCK_DELETE},
        {kbAltIns,      SCK_INSERT},
        {kbAltBack,     SCK_BACK},
    };

    int modifiers = convertModifiers(keyDown.controlKeyState);
    bool specialKey = modifiers && !keyDown.textLength;

    int key;
    if (keyDown.keyCode <= kbCtrlZ)
        key = keyDown.keyCode + 'A' - 1;
    else
    {
        key = keyDown.charScan.charCode;
        for (const auto [tv, sck] : keysTable)
            if (keyDown.keyCode == tv)
            {
                key = sck;
                specialKey = true;
                break;
            }
    }

    if (specialKey)
    {
        bool consumed = false;
        self.KeyDownWithModifiers((Scintilla::Keys) key, (Scintilla::KeyMod) modifiers, &consumed);
        return consumed;
    }
    else
    {
        self.InsertCharacter({keyDown.text, keyDown.textLength}, Scintilla::CharacterSource::DirectInput);
        return true;
    }
}

bool handleMouse(TScintilla &self, ushort what, const MouseEventType &mouse)
{
    using namespace Scintilla::Internal;
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    auto pt = Point::FromInts(mouse.where.x, mouse.where.y);
    uint time = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    auto modifiers = (Scintilla::KeyMod) convertModifiers(mouse.controlKeyState); // Very few environments do support this.
    if (mouse.buttons & mbLeftButton)
    {
        // Scintilla actually assumes these functions are invoked only for the
        // left button mouse.
        switch (what)
        {
            case evMouseDown:
                self.ButtonDownWithModifiers(pt, time, modifiers);
                break;
            case evMouseUp:
                self.ButtonUpWithModifiers(pt, time, modifiers);
                break;
            case evMouseMove:
            case evMouseAuto:
                self.ButtonMoveWithModifiers(pt, time, modifiers);
                break;
        }
        return true;
    }
    return false;
}

TColorAttr getStyleColor(TScintilla &self, int style)
{
    using namespace Scintilla::Internal;
    ColourRGBA fore {(int) call(self, SCI_STYLEGETFORE, style, 0U)};
    ColourRGBA back {(int) call(self, SCI_STYLEGETBACK, style, 0U)};
    auto styleWeight = call(self, SCI_STYLEGETWEIGHT, style, 0U);
    return {
        convertColor(fore),
        convertColor(back),
        (ushort) styleWeight,
    };
}

void paint(TScintilla &self, TDrawSurface &d, TRect area)
{
    using namespace Scintilla::Internal;
    TScintillaSurface s;
    s.surface = &d;
    s.defaultTextAttr = getStyleColor(self, STYLE_DEFAULT);
    s.indicatorColors = &self.indicatorColors;
    self.Paint(
        &s,
        PRectangle::FromInts(area.a.x, area.a.y, area.b.x, area.b.y)
    );
}

void setStyleColor(TScintilla &self, int style, TColorAttr attr)
{
    using namespace Scintilla::Internal;
    call(self, SCI_STYLESETFORE, style, convertColor(::getFore(attr)).OpaqueRGB());
    call(self, SCI_STYLESETBACK, style, convertColor(::getBack(attr)).OpaqueRGB());
    call(self, SCI_STYLESETWEIGHT, style, ::getStyle(attr));
}

void setSelectionColor(TScintilla &self, TColorAttr attr)
{
    using namespace Scintilla::Internal;
    auto fg = ::getFore(attr),
         bg = ::getBack(attr);
    // Active (primary) selection: the legacy messages set the SELECTION_TEXT/BACK
    // elements opaquely on the Base layer, which the surface resolves correctly.
    call(self, SCI_SETSELFORE, !fg.isDefault(), convertColor(fg).OpaqueRGB());
    call(self, SCI_SETSELBACK, !bg.isDefault(), convertColor(bg).OpaqueRGB());
    // The inactive (unfocused-editor) and additional (multi-cursor) selection
    // elements default to *translucent* greys. The terminal surface can't draw
    // translucency and can't resolve a raw RGB through the colour-token registry,
    // so those selections otherwise revert to the editor background. Mirror the
    // active colours onto them as opaque colour tokens, so a selection looks the
    // same whether or not the editor has focus and for every cursor.
    auto setElem = [&] (int element, TColorDesired c) {
        if (c.isDefault())
            call(self, SCI_RESETELEMENTCOLOUR, element, 0);
        else
            call(self, SCI_SETELEMENTCOLOUR, element,
                 (sptr_t) ((uint32_t) convertColor(c).OpaqueRGB() | 0xFF000000u));
    };
    setElem(SC_ELEMENT_SELECTION_INACTIVE_BACK, bg);
    setElem(SC_ELEMENT_SELECTION_ADDITIONAL_BACK, bg);
    setElem(SC_ELEMENT_SELECTION_INACTIVE_TEXT, fg);
    setElem(SC_ELEMENT_SELECTION_ADDITIONAL_TEXT, fg);
}

void setWhitespaceColor(TScintilla &self, TColorAttr attr)
{
    using namespace Scintilla::Internal;
    auto fg = ::getFore(attr),
         bg = ::getBack(attr);
    call(self, SCI_SETWHITESPACEFORE, !fg.isDefault(), convertColor(fg).OpaqueRGB());
    call(self, SCI_SETWHITESPACEBACK, !bg.isDefault(), convertColor(bg).OpaqueRGB());
}

void setMarkerColor(TScintilla &self, int markerNum, TColorAttr attr)
{
    using namespace Scintilla::Internal;
    // Marker colours must go through the colour-token registry like every other
    // colour (a raw RGB resolves to the default). Scintilla's default marker
    // background is a grey that ignores the scheme, so symbol markers (e.g. the
    // fold +/- glyphs) otherwise sit in a grey box; set both ends explicitly.
    call(self, SCI_MARKERSETFORE, markerNum, convertColor(::getFore(attr)).OpaqueRGB());
    call(self, SCI_MARKERSETBACK, markerNum, convertColor(::getBack(attr)).OpaqueRGB());
}

TStringView getRangePointer(TScintilla &self, Sci_Position start, Sci_Position end)
{
    auto length = end - start;
    if (length <= 0)
        return TStringView();
    return TStringView {
        (const char *) self.WndProc((Scintilla::Message) SCI_GETRANGEPOINTER, (uptr_t) start, (sptr_t) length),
        size_t(length),
    };
}

void changeCaseOfSelection(TScintilla &self, CaseConversion cnv)
{
    self.ChangeCaseOfSelection((TScintilla::CaseMapping) (int) cnv);
}

void setIndicatorColor(TScintilla &self, Indicator indicator, TColorAttr attr)
{
    // The terminal Surface cannot draw real translucent indicators; instead it
    // recolours the underlying cells. AlphaRectangle only receives the indicator
    // 'fore' colour, so smuggle the indicator number through it and keep the real
    // fore/back colours in a side table that the Surface resolves at paint time
    // (see platform/surface.cc AlphaRectangle()).
    self.indicatorColors[(int) indicator] = attr;
    call(self, SCI_INDICSETSTYLE, indicator, INDIC_FULLBOX);
    call(self, SCI_INDICSETFORE, indicator, (int) indicator);
    call(self, SCI_INDICSETALPHA, indicator, 255);
    call(self, SCI_INDICSETOUTLINEALPHA, indicator, 255);
}

} // namespace turbo
