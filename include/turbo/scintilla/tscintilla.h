#ifndef TURBO_TSCINTILLA_H
#define TURBO_TSCINTILLA_H

#define Uses_TPoint
#include <tvision/tv.h>

#include <turbo/scintilla/internals.h>

namespace turbo {
class TScintillaParent;
} // namespace turbo

namespace Scintilla::Internal {

class TScintilla : public ScintillaBase
{
    using super = ScintillaBase;

    static void drawWrapMarker(Surface *, PRectangle, bool, ColourRGBA);

protected:

    void SetVerticalScrollPos() override;
    void SetHorizontalScrollPos() override;
    bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;
    void Copy() override;
    void Paste() override;
    void ClaimSelection() override;
    void NotifyChange() override;
    void NotifyParent(Scintilla::NotificationData scn) override;
    void CopyToClipboard(const SelectionText &selectedText) override;
    bool FineTickerRunning(TickReason reason) override;
    void FineTickerStart(TickReason reason, int millis, int tolerance) override;
    void FineTickerCancel(TickReason reason) override;
    void SetMouseCapture(bool on) override;
    bool HaveMouseCapture() override;
    Scintilla::sptr_t DefWndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) override;
    void CreateCallTipWindow(PRectangle rc) override;
    void AddToPopUp(const char *label, int cmd=0, bool enabled=true) override;

    std::unique_ptr<CaseFolder> CaseFolderForEncoding() override;
    std::string CaseMapString(const std::string &s, CaseMapping caseMapping) override;
    int KeyDefault(Scintilla::Keys key, Scintilla::KeyMod modifiers) override;
    std::string UTF8FromEncoded(std::string_view encoded) const override;
    std::string EncodedFromUTF8(std::string_view utf8) const override;

public:

    TScintilla();

    void setParent(turbo::TScintillaParent *aParent);
    turbo::TScintillaParent *getParent() const;
    using super::ChangeSize;
    using super::ClearBeforeTentativeStart;
    using super::InsertPasteShape;
    using super::PasteShape;
    // Insert 'text' as a stream paste. Wraps InsertPasteShape so callers need not
    // name the PasteShape enum: it is protected in Scintilla's Editor, and a
    // public using-declaration re-exposes the type name but not the enumerators'
    // access -- MSVC rejects `TScintilla::PasteShape::stream` from a non-member,
    // while Clang/GCC accept it. Naming the enum inside this member sidesteps that.
    void InsertPasteStream(const char *text, Sci::Position len)
    {
        InsertPasteShape(text, len, PasteShape::stream);
    }
    using super::InsertCharacter;
    using super::IdleWork;
    using super::PointMainCaret;
    using super::KeyDownWithModifiers;
    using super::ButtonDownWithModifiers;
    using super::ButtonUpWithModifiers;
    using super::ButtonMoveWithModifiers;
    using super::Paint;
    using super::ChangeCaseOfSelection;
    using super::CaseMapping;

    // Maps an indicator number to its fore/back colours, used by the terminal
    // Surface to colour FULLBOX indicators (see source/turbo-core/scintilla.cc
    // setIndicatorColor() and platform/surface.cc AlphaRectangle()).
    std::map<int, TColorAttr> indicatorColors;

};

inline void TScintilla::setParent(turbo::TScintillaParent *aParent)
{
    wMain = aParent;
}

inline turbo::TScintillaParent *TScintilla::getParent() const
{
    return (turbo::TScintillaParent *) wMain.GetID();
}

} // namespace Scintilla::Internal

#endif
