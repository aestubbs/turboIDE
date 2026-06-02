#define Uses_TText
#define Uses_TClipboard
#include <tvision/tv.h>

#include <turbo/scintilla.h>
#include <turbo/scintilla/tscintilla.h>

#include "platform/surface.h"

namespace Scintilla::Internal {

TScintilla::TScintilla()
{
    // Scintilla 5.x's WndProc takes a scoped Scintilla::Message; turbo drives it
    // with the legacy integer SCI_* macros, so cast through this helper.
    auto Msg = [this](unsigned int m, Scintilla::uptr_t w, Scintilla::sptr_t l) {
        return WndProc((Scintilla::Message) m, w, l);
    };

    // Block caret for both Insertion and Overwrite mode.
    Msg(SCI_SETCARETSTYLE, CARETSTYLE_BLOCK | CARETSTYLE_OVERSTRIKE_BLOCK, 0U);
    // Disable margin on line numbers.
    vs.marginNumberPadding = 0;
    // Disable margin pixels
    Msg(SCI_SETMARGINLEFT, 0U, 0);
    Msg(SCI_SETMARGINRIGHT, 0U, 0);
    // Disable buffered draw
    Msg(SCI_SETBUFFEREDDRAW, 0, 0U);
    // Disable space between lines
    Msg(SCI_SETEXTRADESCENT, -1, 0U);
    vs.maxAscent = 0;
    vs.maxDescent = 0;
    // Set our custom representations.
    reprs->Clear();
    {
        constexpr int ranges[][2] = {{0, ' '}, {0x7F, 0x100}};
        for (auto &[beg, end] : ranges) {
            for (int i = beg; i < end; ++i) {
                char c[2] = {(char) i};
                char r[8] = {};
                sprintf(r, "\\x%02X", i);
                reprs->SetRepresentation(c, r);
            }
        }
        reprs->SetRepresentation("\t", "»");
    }
    // Do not use padding for control characters.
    vs.ctrlCharPadding = 0;
    view.tabWidthMinimumPixels = 0; // Otherwise, tabs will be more than 8 columns wide.
    // Always draw tabulators (shown via the "\t" -> "»" representation above).
    Msg(SCI_SETVIEWWS, SCWS_VISIBLEALWAYS, 0U);
    // Scintilla draws the visible-space marker as a sub-cell dot rectangle. In a
    // character cell each "pixel" is a whole cell, so a 1px dot fills the entire
    // space cell with the whitespace colour. Set the dot size to 0 so spaces stay
    // blank (tabs are still shown as "»").
    vs.whitespaceSize = 0;
    // Process mouse down events:
    Msg(SCI_SETMOUSEDOWNCAPTURES, true, 0U);
    // Double clicks only in the same cell.
    doubleClickCloseThreshold = Point(0, 0);
    // Set our custom function to draw wrap markers.
    view.customDrawWrapMarker = drawWrapMarker;

    // Extra key shortcuts.

    // Some Ctrl+key combinations are not supported by many terminals,
    // so allow using Alt instead.
    Msg(SCI_ASSIGNCMDKEY, SCK_LEFT | (SCMOD_ALT << 16), SCI_WORDLEFT);
    Msg(SCI_ASSIGNCMDKEY, SCK_LEFT | ((SCMOD_SHIFT | SCMOD_ALT) << 16), SCI_WORDLEFTEXTEND);
    Msg(SCI_ASSIGNCMDKEY, SCK_RIGHT | (SCMOD_ALT << 16), SCI_WORDRIGHT);
    Msg(SCI_ASSIGNCMDKEY, SCK_RIGHT | ((SCMOD_SHIFT | SCMOD_ALT) << 16), SCI_WORDRIGHTEXTEND);
    Msg(SCI_ASSIGNCMDKEY, SCK_UP | ((SCMOD_SHIFT | SCMOD_CTRL) << 16), SCI_MOVESELECTEDLINESUP);
    Msg(SCI_ASSIGNCMDKEY, SCK_UP | ((SCMOD_SHIFT | SCMOD_ALT) << 16), SCI_MOVESELECTEDLINESUP);
    Msg(SCI_ASSIGNCMDKEY, SCK_DOWN | ((SCMOD_SHIFT | SCMOD_CTRL) << 16), SCI_MOVESELECTEDLINESDOWN);
    Msg(SCI_ASSIGNCMDKEY, SCK_DOWN | ((SCMOD_SHIFT | SCMOD_ALT) << 16), SCI_MOVESELECTEDLINESDOWN);
    Msg(SCI_ASSIGNCMDKEY, SCK_BACK | ((SCMOD_ALT) << 16), SCI_DELWORDLEFT);
    Msg(SCI_ASSIGNCMDKEY, SCK_DELETE | ((SCMOD_ALT) << 16), SCI_DELWORDRIGHT);

    // Home/End keys should respect line wrapping.
    Msg(SCI_ASSIGNCMDKEY, SCK_HOME | (SCMOD_NORM << 16), SCI_VCHOMEWRAP);
    Msg(SCI_ASSIGNCMDKEY, SCK_HOME | (SCMOD_SHIFT << 16), SCI_VCHOMEWRAPEXTEND);
    Msg(SCI_ASSIGNCMDKEY, SCK_END | (SCMOD_NORM << 16), SCI_LINEENDWRAP);
    Msg(SCI_ASSIGNCMDKEY, SCK_END | (SCMOD_SHIFT << 16), SCI_LINEENDWRAPEXTEND);

    // Reassign 'delete line' from Ctrl+Shift+L (which will most likely not work)
    // into Ctrl+K.
    Msg(SCI_ASSIGNCMDKEY, 'L' | ((SCMOD_SHIFT | SCMOD_CTRL) << 16), SCI_LINEDELETE);
    Msg(SCI_ASSIGNCMDKEY, 'K' | (SCMOD_CTRL << 16), SCI_LINEDELETE);
}

void TScintilla::SetVerticalScrollPos()
{
    auto *parent = getParent();
    if (parent) {
        auto limit = LinesOnScreen() + MaxScrollPos();
        parent->setVerticalScrollPos(topLine, limit);
    }
}

void TScintilla::SetHorizontalScrollPos()
{
    auto *parent = getParent();
    if (parent) {
        bool noWrap = WndProc((Scintilla::Message) SCI_GETWRAPMODE, 0, 0) == SC_WRAP_NONE;
        parent->setHorizontalScrollPos(xOffset, noWrap ? scrollWidth : 1);
    }
}

bool TScintilla::ModifyScrollBars(Sci::Line, Sci::Line)
{
    SetVerticalScrollPos();
    SetHorizontalScrollPos();
    return false;
}

void TScintilla::Copy()
{
    if (!sel.Empty())
    {
        SelectionText selText;
        CopySelectionRange(&selText);
        TClipboard::setText({selText.Data(), selText.Length()});
    }
}

void TScintilla::Paste()
{
    TClipboard::requestText();
}

void TScintilla::ClaimSelection()
{
}

void TScintilla::NotifyChange()
{
}

void TScintilla::NotifyParent(Scintilla::NotificationData scn)
{
    auto *parent = getParent();
    if (parent)
        // NotificationData and the legacy SCNotification C struct share an
        // identical layout (both generated from Scintilla.iface).
        parent->handleNotification(reinterpret_cast<const SCNotification &>(scn));
}

void TScintilla::CopyToClipboard(const SelectionText &)
{
}

bool TScintilla::FineTickerRunning(TickReason)
{
    return false;
}

void TScintilla::FineTickerStart(TickReason, int, int)
{
}

void TScintilla::FineTickerCancel(TickReason)
{
}

void TScintilla::SetMouseCapture(bool)
{
}

bool TScintilla::HaveMouseCapture()
{
    return true;
}

Scintilla::sptr_t TScintilla::DefWndProc(Scintilla::Message, Scintilla::uptr_t, Scintilla::sptr_t)
{
    return 0;
}

void TScintilla::CreateCallTipWindow(PRectangle)
{
}

void TScintilla::AddToPopUp(const char *, int, bool)
{
}

std::unique_ptr<CaseFolder> TScintilla::CaseFolderForEncoding()
{
    if (IsUnicodeMode())
        return std::make_unique<CaseFolderUnicode>();
    return super::CaseFolderForEncoding();
}

template <size_t (& next)(TStringView) noexcept>
static std::string capitalize(TStringView s, const Document &doc)
{
    std::string result;
    size_t i = 0;
    while (i < s.size())
    {
        size_t spaceBegin = i;
        while (i < s.size() && doc.WordCharacterClass(s[i]) != CharacterClass::word)
            ++i;
        auto space = s.substr(spaceBegin, i - spaceBegin);
        result.append(space.data(), space.size());

        size_t firstBegin = i;
        i += next(s.substr(i));
        auto first = s.substr(firstBegin, i - firstBegin);
        result.append(CaseConvertString(first, CaseConversion::upper));

        size_t tailBegin = i;
        while (i < s.size() && doc.WordCharacterClass(s[i]) == CharacterClass::word)
            ++i;
        auto tail = s.substr(tailBegin, i - tailBegin);
        result.append(CaseConvertString(tail, CaseConversion::lower));
    }
    return result;
}

static size_t nextUnicode(TStringView s) noexcept
{
    return TText::next(s);
}

static size_t nextAscii(TStringView s) noexcept
{
    return min<size_t>(s.size(), 1);
}

std::string TScintilla::CaseMapString(const std::string &s, CaseMapping caseMapping)
{
    auto mapping = turbo::CaseConversion((int) caseMapping);
    if (IsUnicodeMode())
        switch (mapping)
        {
            case turbo::caseConvNone:
                return s;
            case turbo::caseConvUpper:
                return CaseConvertString(s, CaseConversion::upper);
            case turbo::caseConvLower:
                return CaseConvertString(s, CaseConversion::lower);
            case turbo::caseConvCapitalize:
                return capitalize<nextUnicode>(s, *pdoc);
        }
    switch (mapping)
    {
        case turbo::caseConvCapitalize:
            return capitalize<nextAscii>(s, *pdoc);
        default:
            return super::CaseMapString(s, caseMapping);
    }
}

int TScintilla::KeyDefault(Scintilla::Keys key, Scintilla::KeyMod modifiers) {
    if (modifiers == Scintilla::KeyMod::Norm)
    {
        super::AddChar((char) (int) key);
        return 1;
    }
    return 0;
}

std::string TScintilla::UTF8FromEncoded(std::string_view encoded) const
{
    // turbo operates the document in UTF-8 / Unicode mode, so the encoded form
    // is already UTF-8.
    return std::string(encoded);
}

std::string TScintilla::EncodedFromUTF8(std::string_view utf8) const
{
    return std::string(utf8);
}

void TScintilla::drawWrapMarker(Surface *surface, PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour)
{
    auto *s = (TScintillaSurface *) surface;
    if (isEndMarker)
        // Imitate the Tilde text editor.
        s->DrawTextTransparent(rcPlace, nullptr, rcPlace.bottom, "↵", wrapColour);
}

} // namespace Scintilla::Internal
