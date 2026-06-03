#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TStaticText
#define Uses_TListViewer
#define Uses_TScrollBar
#define Uses_TView
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TColorAttr
#include <tvision/tv.h>

#include "themedialog.h"
#include "colordialog.h"
#include "theme.h"
#include "cmds.h"

#include <turbo/styles.h>
#include <turbo/basicwindow.h>

#include <string>
#include <vector>

using namespace turbo;

namespace {

// Dialog-private commands.
const ushort cmThemeFg       = 0xF120;
const ushort cmThemeBg       = 0xF121;
const ushort cmThemeApplyBtn = 0xF122;
const ushort cmThemeResetBtn = 0xF123;
const ushort cmThemeRowChanged = 0xF124; // broadcast from the list on focus move

struct ThemeRow
{
    std::string label;
    bool isStyle;  // true: index into the syntax scheme; false: window chrome
    int index;     // TextStyle, or WindowPaletteItems slot
};

// ---------------------------------------------------------------------------
// The scrollable list of editable items.
// ---------------------------------------------------------------------------

struct ThemeListView : public TListViewer
{
    const std::vector<ThemeRow> *rows;

    ThemeListView(const TRect &bounds, TScrollBar *vsb, const std::vector<ThemeRow> *aRows) noexcept :
        TListViewer(bounds, 1, nullptr, vsb),
        rows(aRows)
    {
        setRange((short) aRows->size());
    }

    void getText(char *dest, short item, short maxLen) override
    {
        if (rows && item >= 0 && item < (short) rows->size())
        {
            strncpy(dest, (*rows)[item].label.c_str(), maxLen);
            dest[maxLen] = '\0';
        }
        else
            dest[0] = '\0';
    }

    void focusItem(short item) override
    {
        TListViewer::focusItem(item);
        if (owner)
            message(owner, evBroadcast, cmThemeRowChanged, this);
    }
};

// ---------------------------------------------------------------------------
// Right-pane preview: a sample line in the item's colours plus FG/BG/style text.
// ---------------------------------------------------------------------------

struct RowPreview : public TView
{
    TColorAttr sample {};
    std::string fgText, bgText, styleText;

    RowPreview(const TRect &bounds) noexcept : TView(bounds) {}

    void draw() override
    {
        const TColorAttr infoAttr {0xC8C8D8, 0x1E4D8C};
        for (int y = 0; y < size.y; ++y)
        {
            TDrawBuffer buf;
            if (y == 0)
            {
                buf.moveChar(0, ' ', sample, size.x);
                buf.moveStr(1, "Aa Bb Cc  123  // sample", sample);
            }
            else
            {
                buf.moveChar(0, ' ', infoAttr, size.x);
                if (y == 2)      buf.moveStr(1, ("Foreground: " + fgText).c_str(), infoAttr);
                else if (y == 3) buf.moveStr(1, ("Background: " + bgText).c_str(), infoAttr);
                else if (y == 4 && !styleText.empty())
                    buf.moveStr(1, ("Style: " + styleText).c_str(), infoAttr);
            }
            writeLine(0, y, size.x, 1, buf);
        }
    }
};

// ---------------------------------------------------------------------------
// ThemeDialog
// ---------------------------------------------------------------------------

struct ThemeDialog : public TDialog
{
    std::vector<ThemeRow> rows;
    // Working copies; only committed to the active schemes on Apply/OK.
    TColorAttr workScheme[TextStyleCount];
    TColorAttr workChrome[WindowPaletteItemCount];

    ThemeListView *list;
    RowPreview *preview;
    TCheckBoxes *flagBox;

    ThemeDialog() noexcept :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 66, 23), "Colour Scheme")
    {
        options |= ofCentered;

        for (int i = 0; i < TextStyleCount; ++i)
            workScheme[i] = schemeActive[i];
        for (int i = 0; i < WindowPaletteItemCount; ++i)
            workChrome[i] = windowSchemeActive[i];

        for (int i = 0; i < TextStyleCount; ++i)
            rows.push_back({styleDisplayName((TextStyle) i), true, i});
        for (int i = 0; i < chromeItemCount; ++i)
            rows.push_back({chromeItems[i].label, false, chromeItems[i].index});

        auto *vsb = new TScrollBar(TRect(26, 2, 27, 18));
        insert(vsb);
        list = new ThemeListView(TRect(2, 2, 26, 18), vsb, &rows);
        insert(list);

        const int rx = 29;
        preview = new RowPreview(TRect(rx, 2, 64, 7));
        insert(preview);

        insert(new TButton(TRect(rx, 8, rx + 17, 10), "~F~oreground...", cmThemeFg, bfNormal));
        insert(new TButton(TRect(rx, 10, rx + 17, 12), "~B~ackground...", cmThemeBg, bfNormal));

        flagBox = new TCheckBoxes(TRect(rx, 13, 64, 16),
            new TSItem("Bo~l~d",
            new TSItem("~I~talic",
            new TSItem("~U~nderline", nullptr))));
        insert(flagBox);

        // Bottom row of dialog buttons.
        insert(new TButton(TRect(2, 19, 12, 21), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(13, 19, 25, 21), "~A~pply", cmThemeApplyBtn, bfNormal));
        insert(new TButton(TRect(26, 19, 37, 21), "~R~eset", cmThemeResetBtn, bfNormal));
        insert(new TButton(TRect(38, 19, 50, 21), "Cancel", cmCancel, bfNormal));

        selectNext(False);
    }

    const ThemeRow &currentRow() const noexcept
    {
        short f = list->focused;
        if (f < 0) f = 0;
        if (f >= (short) rows.size()) f = (short) rows.size() - 1;
        return rows[f];
    }

    TColorAttr *currentAttr() noexcept
    {
        const ThemeRow &r = currentRow();
        return r.isStyle ? &workScheme[r.index] : &workChrome[r.index];
    }

    // Read the flag checkboxes into the working copy for the current style row.
    void captureFlags() noexcept
    {
        const ThemeRow &r = currentRow();
        if (!r.isStyle)
            return;
        ushort v = 0;
        flagBox->getData(&v);
        TColorAttr &a = workScheme[r.index];
        ushort keep = ::getStyle(a) & ~(ushort)(slBold | slItalic | slUnderline);
        ::setStyle(a, keep | (v & (slBold | slItalic | slUnderline)));
    }

    void updatePreview() noexcept
    {
        TColorAttr *cur = currentAttr();
        const ThemeRow &r = currentRow();
        TColorAttr base = r.isStyle ? workScheme[sNormal] : TColorAttr {0xC8C8D8, 0x1E4D8C};
        preview->sample = coalesce(*cur, base);
        preview->fgText = formatColor(::getFore(*cur));
        preview->bgText = formatColor(::getBack(*cur));
        ushort st = ::getStyle(*cur);
        std::string s;
        if (st & slBold)      s += "bold ";
        if (st & slItalic)    s += "italic ";
        if (st & slUnderline) s += "underline ";
        preview->styleText = r.isStyle ? (s.empty() ? std::string("(none)") : s) : std::string();
        preview->drawView();
    }

    // Load the focused row into the right-pane controls.
    void loadRow() noexcept
    {
        const ThemeRow &r = currentRow();
        if (r.isStyle)
        {
            ushort v = (ushort) (::getStyle(workScheme[r.index]) & (slBold | slItalic | slUnderline));
            flagBox->setData(&v);
            flagBox->setState(sfDisabled, False);
        }
        else
        {
            ushort v = 0;
            flagBox->setData(&v);
            flagBox->setState(sfDisabled, True); // chrome has no text style
        }
        updatePreview();
    }

    void editColor(bool fg) noexcept
    {
        TColorAttr *cur = currentAttr();
        TColorDesired c = fg ? ::getFore(*cur) : ::getBack(*cur);
        std::string title = std::string(fg ? "Foreground: " : "Background: ") + currentRow().label;
        if (pickColor(title.c_str(), c, /*allowDefault=*/true))
        {
            if (fg) ::setFore(*cur, c);
            else    ::setBack(*cur, c);
            updatePreview();
        }
    }

    void resetWorking() noexcept
    {
        for (int i = 0; i < TextStyleCount; ++i)
            workScheme[i] = schemeDefault[i];
        for (int i = 0; i < WindowPaletteItemCount; ++i)
            workChrome[i] = windowSchemeDefault[i];
        loadRow();
    }

    void commit() noexcept
    {
        for (int i = 0; i < TextStyleCount; ++i)
            schemeActive[i] = workScheme[i];
        for (int i = 0; i < WindowPaletteItemCount; ++i)
            windowSchemeActive[i] = workChrome[i];
        // The application re-themes the open editors and persists the change.
        message(TProgram::application, evCommand, cmApplyTheme, nullptr);
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evBroadcast && ev.message.command == cmThemeRowChanged)
        {
            loadRow();
            clearEvent(ev);
            return;
        }
        if (ev.what == evCommand)
        {
            switch (ev.message.command)
            {
                case cmThemeFg:       editColor(true);  clearEvent(ev); return;
                case cmThemeBg:       editColor(false); clearEvent(ev); return;
                case cmThemeApplyBtn: commit();         clearEvent(ev); return;
                case cmThemeResetBtn: resetWorking();   clearEvent(ev); return;
                case cmOK:            commit();         break; // then close
            }
        }

        TDialog::handleEvent(ev);

        // Keep the working copy and preview synced with the flag checkboxes after
        // the base handler has applied the click/keystroke.
        if (ev.what == evKeyDown || ev.what == evMouseUp || ev.what == evMouseDown)
        {
            captureFlags();
            updatePreview();
        }
    }
};

} // namespace

bool executeThemeDialog() noexcept
{
    auto *d = new ThemeDialog;
    d->loadRow();
    bool ok = (TProgram::deskTop->execView(d) == cmOK);
    TObject::destroy(d);
    return ok;
}
