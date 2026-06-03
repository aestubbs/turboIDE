#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TStaticText
#define Uses_TView
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TColorAttr
#include <tvision/tv.h>

#include "colordialog.h"
#include "theme.h"

#include <cstdint>
#include <cstdio>

namespace {

// Broadcast within the picker: the swatch grid changed its selection.
const ushort cmSwatchPicked = 0xF110;

const int kGridCols = 16;
const int kGridRows = 16;
const int kCellW = 2; // each swatch is two cells wide

// Standard 16 ANSI colours (xterm indices 0..15); 16..255 come from the
// xterm-256 cube/greyscale via TVision's converter.
const uint32_t kAnsi16[16] =
{
    0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
    0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
};

TColorRGB swatchRGB(int idx) noexcept
{
    if (idx < 16)
        return TColorRGB((uint32_t) kAnsi16[idx]);
    return XTerm256toRGB((uint8_t) idx);
}

bool isLightColor(TColorRGB c) noexcept
{
    return (c.r * 299 + c.g * 587 + c.b * 114) / 1000 >= 128;
}

// ---------------------------------------------------------------------------
// SwatchGrid: a 16x16 grid of clickable colour cells.
// ---------------------------------------------------------------------------

struct SwatchGrid : public TView
{
    int selected {0};

    SwatchGrid(const TRect &bounds) noexcept : TView(bounds)
    {
        options |= ofSelectable;
        eventMask |= evMouseDown | evMouseMove | evMouseAuto;
    }

    TColorRGB color() const noexcept { return swatchRGB(selected); }

    // Move the selection to the swatch closest to 'target' (no notification).
    void selectNearest(TColorRGB target) noexcept
    {
        long best = -1;
        int bestIdx = 0;
        for (int i = 0; i < kGridCols * kGridRows; ++i)
        {
            TColorRGB c = swatchRGB(i);
            long dr = (long) c.r - target.r, dg = (long) c.g - target.g, db = (long) c.b - target.b;
            long d = dr * dr + dg * dg + db * db;
            if (best < 0 || d < best) { best = d; bestIdx = i; }
        }
        selected = bestIdx;
    }

    void setSelected(int idx, bool notify) noexcept
    {
        if (idx < 0) idx = 0;
        if (idx >= kGridCols * kGridRows) idx = kGridCols * kGridRows - 1;
        if (idx != selected)
        {
            selected = idx;
            drawView();
        }
        if (notify && owner)
            message(owner, evBroadcast, cmSwatchPicked, this);
    }

    void draw() override
    {
        for (int row = 0; row < kGridRows; ++row)
        {
            TDrawBuffer buf;
            for (int col = 0; col < kGridCols; ++col)
            {
                int idx = row * kGridCols + col;
                TColorRGB rgb = swatchRGB(idx);
                if (idx == selected)
                {
                    TColorRGB mark = isLightColor(rgb) ? TColorRGB(0u) : TColorRGB(0xFFFFFFu);
                    buf.moveStr(col * kCellW, "><", TColorAttr(mark, rgb));
                }
                else
                    buf.moveChar(col * kCellW, ' ', TColorAttr(rgb, rgb), kCellW);
            }
            writeLine(0, row, size.x, 1, buf);
        }
    }

    void selectAt(TPoint local, bool notify) noexcept
    {
        int col = local.x / kCellW, row = local.y;
        if (col < 0) col = 0; else if (col >= kGridCols) col = kGridCols - 1;
        if (row < 0) row = 0; else if (row >= kGridRows) row = kGridRows - 1;
        setSelected(row * kGridCols + col, notify);
    }

    void handleEvent(TEvent &ev) override
    {
        TView::handleEvent(ev);
        if (ev.what == evMouseDown)
        {
            do {
                selectAt(makeLocal(ev.mouse.where), true);
            } while (mouseEvent(ev, evMouseMove | evMouseAuto));
            clearEvent(ev);
        }
        else if (ev.what == evKeyDown)
        {
            int col = selected % kGridCols, row = selected / kGridCols;
            switch (ev.keyDown.keyCode)
            {
                case kbLeft:  if (col > 0) --col; break;
                case kbRight: if (col < kGridCols - 1) ++col; break;
                case kbUp:    if (row > 0) --row; break;
                case kbDown:  if (row < kGridRows - 1) ++row; break;
                default: return; // leave Tab/Enter/etc. for the dialog
            }
            setSelected(row * kGridCols + col, true);
            clearEvent(ev);
        }
    }
};

// ---------------------------------------------------------------------------
// PreviewView: shows the candidate colour as sample text and a solid bar.
// ---------------------------------------------------------------------------

struct PreviewView : public TView
{
    TColorDesired color {};
    bool isDefault {true};

    PreviewView(const TRect &bounds) noexcept : TView(bounds) {}

    void setColor(TColorDesired c, bool def) noexcept
    {
        color = c;
        isDefault = def;
        drawView();
    }

    void draw() override
    {
        const TColorRGB backdrop = 0x1E4D8C; // editor-like background
        const TColorRGB faint = 0x9098B0;
        for (int y = 0; y < size.y; ++y)
        {
            TDrawBuffer buf;
            if (isDefault)
            {
                buf.moveChar(0, ' ', TColorAttr(faint, backdrop), size.x);
                if (y == size.y / 2)
                    buf.moveStr(1, "(default / inherit)", TColorAttr(faint, backdrop));
            }
            else if (y < size.y - 1)
            {
                // Text rendered in the chosen colour over the editor backdrop.
                buf.moveChar(0, ' ', TColorAttr(color, backdrop), size.x);
                buf.moveStr(1, "Sample text  Aa Bb 0123", TColorAttr(color, backdrop));
            }
            else
            {
                // A solid bar filled with the chosen colour as background.
                buf.moveChar(0, ' ', TColorAttr(backdrop, color), size.x);
            }
            writeLine(0, y, size.x, 1, buf);
        }
    }
};

// ---------------------------------------------------------------------------
// ColorPickerDialog
// ---------------------------------------------------------------------------

struct ColorPickerDialog : public TDialog
{
    SwatchGrid *grid;
    TInputLine *hexLine;
    TCheckBoxes *defBox; // null when default isn't allowed
    PreviewView *preview;
    bool allowDefault;

    ColorPickerDialog(const char *title, TColorDesired initial, bool aAllowDefault) noexcept :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(0, 0, 62, 21), title),
        defBox(nullptr),
        allowDefault(aAllowDefault)
    {
        options |= ofCentered;

        // Swatch grid on the left.
        grid = new SwatchGrid(TRect(2, 2, 2 + kGridCols * kCellW, 2 + kGridRows));
        insert(grid);

        const int rx = 2 + kGridCols * kCellW + 3; // right column origin

        insert(new TLabel(TRect(rx, 2, rx + 18, 3), "Hex (~R~RGGBB):", nullptr));
        hexLine = new TInputLine(TRect(rx, 3, rx + 9, 4), 8);
        insert(hexLine);

        if (allowDefault)
        {
            defBox = new TCheckBoxes(TRect(rx, 5, rx + 18, 6),
                new TSItem("~D~efault", nullptr));
            insert(defBox);
        }

        insert(new TStaticText(TRect(rx, 7, rx + 24, 8), "Preview:"));
        preview = new PreviewView(TRect(rx, 8, rx + 24, 12));
        insert(preview);

        insert(new TButton(TRect(rx, 14, rx + 11, 16), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(rx, 16, rx + 11, 18), "Cancel", cmCancel, bfNormal));

        // Seed the controls from the initial colour.
        bool def = initial.isDefault();
        TColorRGB rgb = initial.isRGB() ? initial.asRGB() : TColorRGB(0xD4D4E4u);
        if (def && allowDefault)
            defBox->press(0);
        grid->selectNearest(rgb);
        writeHex(rgb);

        selectNext(False);
    }

    void writeHex(TColorRGB rgb) noexcept
    {
        std::snprintf(hexLine->data, 8, "%06X", (unsigned) ((uint32_t) rgb & 0xFFFFFF));
        hexLine->drawView();
    }

    bool defaultChecked() const noexcept
    {
        return defBox && (defBox->mark(0));
    }

    // The colour currently described by the controls.
    TColorDesired currentColor() const noexcept
    {
        if (defaultChecked())
            return {};
        TColorRGB rgb;
        if (parseHexColor(hexLine->data, rgb))
            return TColorDesired(rgb);
        return TColorDesired(grid->color());
    }

    void refresh() noexcept
    {
        bool def = defaultChecked();
        preview->setColor(def ? TColorDesired{} : currentColor(), def);
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evBroadcast && ev.message.command == cmSwatchPicked)
        {
            // A swatch was chosen: mirror it into the hex field, clear "default".
            if (defBox && defBox->mark(0))
            {
                defBox->press(0); // toggle the (single) box off
                defBox->drawView();
            }
            writeHex(grid->color());
            refresh();
            clearEvent(ev);
            return;
        }

        TDialog::handleEvent(ev);

        // Keep the preview (and grid) in sync with the controls after the base
        // handler has applied keystrokes / checkbox clicks.
        if (ev.what == evKeyDown || ev.what == evMouseUp || ev.what == evMouseDown)
        {
            TColorRGB rgb;
            if (!defaultChecked() && parseHexColor(hexLine->data, rgb))
                grid->selectNearest(rgb), grid->drawView();
            refresh();
        }
    }
};

} // namespace

bool pickColor(const char *title, TColorDesired &color, bool allowDefault) noexcept
{
    auto *d = new ColorPickerDialog(title, color, allowDefault);
    d->refresh();
    bool ok = (TProgram::deskTop->execView(d) == cmOK);
    if (ok)
        color = d->currentColor();
    TObject::destroy(d);
    return ok;
}
