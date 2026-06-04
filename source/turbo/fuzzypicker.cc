#define Uses_TWindow
#define Uses_TInputLine
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include "fuzzypicker.h"

#include <algorithm>
#include <fstream>

namespace {

inline int clampi(int v, int lo, int hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Truncate a UTF-8-ish string to at most 'cols' display columns. Byte-based,
// which is fine for the ASCII-dominant paths/commands shown here.
std::string clip(const std::string &s, int cols) noexcept
{
    if (cols <= 0)
        return {};
    if ((int) s.size() <= cols)
        return s;
    return s.substr(0, cols);
}

// Expand tabs to 4-column stops for display (the editor uses tab width 4).
std::string expandTabs(const std::string &s) noexcept
{
    std::string out;
    int col = 0;
    for (char c : s)
    {
        if (c == '\t')
        {
            int n = 4 - (col % 4);
            out.append(n, ' ');
            col += n;
        }
        else if (c == '\r' || c == '\n')
            ; // skip stray line endings
        else
        {
            out += c;
            ++col;
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Query box: forwards navigation/accept/cancel keys to the picker and re-runs
// the provider whenever the typed text changes.

struct FuzzyPickerInput : public TInputLine
{
    FuzzyPicker *picker;

    FuzzyPickerInput(const TRect &bounds, FuzzyPicker *aPicker) noexcept :
        TInputLine(bounds, 512),
        picker(aPicker)
    {
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evKeyDown)
        {
            switch (ev.keyDown.keyCode)
            {
                case kbDown:  picker->moveSelection(1);   clearEvent(ev); return;
                case kbUp:    picker->moveSelection(-1);  clearEvent(ev); return;
                case kbPgDn:  picker->moveSelection(picker->pageStep());  clearEvent(ev); return;
                case kbPgUp:  picker->moveSelection(-picker->pageStep()); clearEvent(ev); return;
                case kbEnter: picker->accept(); clearEvent(ev); return;
                case kbEsc:   picker->cancel(); clearEvent(ev); return;
                default: break;
            }
        }
        std::string before = data ? data : "";
        TInputLine::handleEvent(ev);
        if ((data ? std::string(data) : std::string()) != before)
            picker->onQueryChanged();
    }
};

// ---------------------------------------------------------------------------
// Result list: draws the picker's rows with the highlighted one always shown in
// the selection colour (it stays highlighted even though the query box, not the
// list, holds the keyboard focus).

struct FuzzyPickerList : public TView
{
    FuzzyPicker *picker;

    FuzzyPickerList(const TRect &bounds, FuzzyPicker *aPicker) noexcept :
        TView(bounds),
        picker(aPicker)
    {
        growMode = gfGrowHiX | gfGrowHiY;
    }

    TPalette &getPalette() const override
    {
        static TPalette palette("\x06\x07\x08\x09", 4);
        return palette;
    }

    void draw() override
    {
        TColorAttr cNormal = mapColor(1);
        TColorAttr cSel    = mapColor(2);
        TColorAttr cDim    = mapColor(3);
        TColorAttr cDetail = mapColor(4);
        int n = (int) picker->rows.size();
        for (int i = 0; i < size.y; ++i)
        {
            int idx = picker->top + i;
            bool sel = (idx == picker->selected);
            TColorAttr c = cNormal;
            if (idx < n && picker->rows[idx].dim)
                c = cDim;
            if (sel)
                c = cSel;

            TDrawBuffer b;
            b.moveChar(0, ' ', c, size.x);
            if (idx < n)
            {
                const auto &row = picker->rows[idx];
                std::string text = clip(row.text, size.x - 2);
                b.moveStr(1, text.c_str(), c);
                if (!row.detail.empty())
                {
                    std::string det = clip(row.detail, size.x / 2);
                    int dx = size.x - 1 - (int) det.size();
                    if (dx >= (int) text.size() + 2)
                        b.moveStr((ushort) dx, det.c_str(), sel ? c : cDetail);
                }
            }
            writeLine(0, i, size.x, 1, b);
        }
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evMouseDown)
        {
            TPoint m = makeLocal(ev.mouse.where);
            int idx = picker->top + m.y;
            if (idx >= 0 && idx < (int) picker->rows.size())
            {
                picker->setSelection(idx);
                if (ev.mouse.eventFlags & meDoubleClick)
                    picker->accept();
            }
            clearEvent(ev);
            return;
        }
        TView::handleEvent(ev);
    }
};

// ---------------------------------------------------------------------------
// Preview pane: shows the highlighted file, centred on the target line. Reads
// straight from disk (cheap, capped) and caches the last-loaded path.

struct FuzzyPickerPreview : public TView
{
    std::vector<std::string> lines;
    std::string loadedPath;
    long centreLine {-1};

    FuzzyPickerPreview(const TRect &bounds) noexcept :
        TView(bounds)
    {
        growMode = gfGrowHiX | gfGrowHiY;
    }

    TPalette &getPalette() const override
    {
        static TPalette palette("\x0A\x0B", 2);
        return palette;
    }

    void setTarget(const std::string &path, long line) noexcept
    {
        if (path != loadedPath)
        {
            lines.clear();
            std::ifstream f(path);
            std::string ln;
            int count = 0;
            while (count < 2000 && std::getline(f, ln))
            {
                lines.push_back(std::move(ln));
                ++count;
            }
            loadedPath = path;
        }
        centreLine = line;
        drawView();
    }

    void draw() override
    {
        TColorAttr cN = mapColor(1);
        TColorAttr cH = mapColor(2);
        int h = size.y;
        long total = (long) lines.size();
        long start = 0;
        if (centreLine >= 0)
            start = std::max(0L, centreLine - h / 2);
        if (start + h > total)
            start = std::max(0L, total - h);
        for (int i = 0; i < h; ++i)
        {
            long ln = start + i;
            TColorAttr c = (ln == centreLine) ? cH : cN;
            TDrawBuffer b;
            b.moveChar(0, ' ', c, size.x);
            b.moveStr(0, "\xE2\x94\x82", cN); // left divider │
            if (ln >= 0 && ln < total)
            {
                std::string s = clip(expandTabs(lines[ln]), size.x - 2);
                b.moveStr(2, s.c_str(), c);
            }
            writeLine(0, i, size.x, 1, b);
        }
    }
};

// ---------------------------------------------------------------------------
// FuzzyPicker

FuzzyPicker::FuzzyPicker( const TRect &bounds, TStringView title,
                          Provider aProvider, bool withPreview ) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, title, wnNoNumber),
    provider(std::move(aProvider))
{
    flags = wfClose | wfMove;
    growMode = 0;
    palette = wpBlueWindow;

    TRect intr = getExtent().grow(-1, -1);
    TRect rInput(intr.a.x, intr.a.y, intr.b.x, intr.a.y + 1);
    int contentTop = intr.a.y + 1;

    if (withPreview)
    {
        int interiorW = intr.b.x - intr.a.x;
        int listW = std::max(20, interiorW * 42 / 100);
        int split = intr.a.x + listW;
        TRect rList(intr.a.x, contentTop, split, intr.b.y);
        TRect rPrev(split, contentTop, intr.b.x, intr.b.y);
        list = new FuzzyPickerList(rList, this);
        preview = new FuzzyPickerPreview(rPrev);
        insert(preview);
        insert(list);
    }
    else
    {
        TRect rList(intr.a.x, contentTop, intr.b.x, intr.b.y);
        list = new FuzzyPickerList(rList, this);
        insert(list);
    }

    input = new FuzzyPickerInput(rInput, this);
    insert(input);
    input->select();
}

void FuzzyPicker::shutDown()
{
    input = nullptr;
    list = nullptr;
    preview = nullptr;
    TWindow::shutDown();
}

int FuzzyPicker::run() noexcept
{
    currentQuery.clear();
    onQueryChanged();
    int code = TProgram::deskTop->execView(this);
    return code == cmOK ? result : -1;
}

void FuzzyPicker::onQueryChanged() noexcept
{
    std::string q = (input && input->data) ? input->data : "";
    currentQuery = q;
    rows = provider ? provider(q) : std::vector<Row>{};
    selected = 0;
    top = 0;
    if (list)
        list->drawView();
    emitHighlight();
}

void FuzzyPicker::reload() noexcept
{
    int keep = selected;
    rows = provider ? provider(currentQuery) : std::vector<Row>{};
    selected = clampi(keep, 0, std::max(0, (int) rows.size() - 1));
    if (list)
        list->drawView();
    emitHighlight();
}

void FuzzyPicker::moveSelection(int delta) noexcept
{
    if (rows.empty())
        return;
    int n = (int) rows.size();
    selected = clampi(selected + delta, 0, n - 1);
    int h = list ? list->size.y : 0;
    if (selected < top)
        top = selected;
    else if (h > 0 && selected >= top + h)
        top = selected - h + 1;
    if (list)
        list->drawView();
    emitHighlight();
}

void FuzzyPicker::setSelection(int index) noexcept
{
    if (index < 0 || index >= (int) rows.size())
        return;
    selected = index;
    int h = list ? list->size.y : 0;
    if (selected < top)
        top = selected;
    else if (h > 0 && selected >= top + h)
        top = selected - h + 1;
    if (list)
        list->drawView();
    emitHighlight();
}

void FuzzyPicker::accept() noexcept
{
    if (selected >= 0 && selected < (int) rows.size())
    {
        result = rows[selected].payload;
        endModal(cmOK);
    }
    else
        cancel();
}

void FuzzyPicker::cancel() noexcept
{
    result = -1;
    endModal(cmCancel);
}

int FuzzyPicker::pageStep() const noexcept
{
    return list ? std::max(1, list->size.y - 1) : 1;
}

void FuzzyPicker::emitHighlight() noexcept
{
    if (onHighlight && selected >= 0 && selected < (int) rows.size())
        onHighlight(rows[selected].payload);
}

void FuzzyPicker::setPreview(const std::string &path, long centreLine) noexcept
{
    if (preview)
        preview->setTarget(path, centreLine);
}

TColorAttr FuzzyPicker::mapColor(uchar index)
{
    switch (index)
    {
        // Frame (TFrame requests window indices 1..3).
        case 1: return {TColorRGB(0x6A7286), TColorRGB(0x14182B)}; // passive border
        case 2: return {TColorRGB(0xC8D0E0), TColorRGB(0x14182B)}; // active border/title
        case 3: return {TColorRGB(0x7AA2F7), TColorRGB(0x14182B)}; // close icon
        // Result list (FuzzyPickerList getPalette -> 6..9).
        case 6: return {TColorRGB(0xC8CCD8), TColorRGB(0x14182B)}; // normal item
        case 7: return {TColorRGB(0x0B0E1A), TColorRGB(0x7AA2F7)}; // selected item
        case 8: return {TColorRGB(0x5A6072), TColorRGB(0x14182B)}; // dimmed item
        case 9: return {TColorRGB(0x7E869B), TColorRGB(0x14182B)}; // detail column
        // Preview (FuzzyPickerPreview getPalette -> 10..11).
        case 10: return {TColorRGB(0x9AA3B5), TColorRGB(0x10131F)}; // preview text
        case 11: return {TColorRGB(0xF0F0F0), TColorRGB(0x2A2F4A)}; // target line
        // Query box (TInputLine requests window indices 0x13..0x15).
        case 0x13: return {TColorRGB(0xE6E6E6), TColorRGB(0x232842)}; // text
        case 0x14: return {TColorRGB(0x101010), TColorRGB(0x7AA2F7)}; // selected text
        case 0x15: return {TColorRGB(0x9AA0B5), TColorRGB(0x232842)}; // arrows
        default:   return {TColorRGB(0xC8CCD8), TColorRGB(0x14182B)};
    }
}
