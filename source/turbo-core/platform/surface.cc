#define Uses_TText
#define Uses_TDrawSurface
#include <tvision/tv.h>

#include "surface.h"
#include <turbo/scintilla.h>

namespace Scintilla::Internal {

// Colour token registry. See the comment in surface.h. Theme setup (intern) and
// painting (resolve) both run on the main UI thread, so no locking is needed.
// Token 0 is reserved for the terminal-default colour, so a style Scintilla
// returns as 0 (unset) resolves to "default" rather than the first interned
// colour.

static TColorDesired defaultTColor() noexcept
{
    TColorDesired c;
    c.bitCast(0); // type ctDefault: terminal default colour
    return c;
}

static std::vector<TColorDesired> &colorPalette()
{
    static std::vector<TColorDesired> palette = { defaultTColor() };
    return palette;
}

static std::map<uint32_t, int> &colorIndex()
{
    static std::map<uint32_t, int> index = { { 0u, 0 } };
    return index;
}

int internTColor(TColorDesired c)
{
    auto &palette = colorPalette();
    auto &index = colorIndex();
    uint32_t key = c.bitCast();
    auto it = index.find(key);
    if (it != index.end())
        return it->second;
    int token = (int) palette.size();
    palette.push_back(c);
    index[key] = token;
    return token;
}

TColorDesired resolveTColor(int token)
{
    auto &palette = colorPalette();
    if (token < 0 || (size_t) token >= palette.size())
        return defaultTColor(); // unknown token (e.g. a raw colour) -> default
    return palette[token];
}

std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology)
{
    return std::make_unique<TScintillaSurface>();
}

void TScintillaSurface::Init(WindowID)
{
}

void TScintillaSurface::Init(SurfaceID, WindowID)
{
}

std::unique_ptr<Surface> TScintillaSurface::AllocatePixMap(int, int)
{
    // The terminal has no real pixmaps. Scintilla draws lines/markers into a
    // pixmap and then Copy()s them onto the destination; we make the "pixmap"
    // share the same TDrawSurface so that drawing lands directly on screen and
    // Copy() becomes a no-op. (turbo also disables buffered draw, so this path
    // is rarely exercised.)
    auto s = std::make_unique<TScintillaSurface>();
    s->surface = surface;
    s->defaultTextAttr = defaultTextAttr;
    s->mode = mode;
    s->clip = clip;
    s->indicatorColors = indicatorColors;
    return s;
}

void TScintillaSurface::SetMode(SurfaceMode mode_)
{
    mode = mode_;
}

void TScintillaSurface::Release() noexcept
{
    surface = nullptr;
    defaultTextAttr = {};
    clipStack.clear();
}

int TScintillaSurface::SupportsFeature(Scintilla::Supports) noexcept
{
    // The terminal Surface only fills cells and draws text; it supports none of
    // the optional rendering features (pixel divisions, fractional strokes...).
    return 0;
}

bool TScintillaSurface::Initialised()
{
    return surface;
}

int TScintillaSurface::LogPixelsY()
{
    return 1;
}

int TScintillaSurface::PixelDivisions()
{
    return 1;
}

int TScintillaSurface::DeviceHeightFont(int)
{
    return 1;
}

void TScintillaSurface::LineDraw(Point, Point, Stroke)
{
}

void TScintillaSurface::PolyLine(const Point *, size_t, Stroke)
{
}

void TScintillaSurface::Polygon(const Point *, size_t, FillStroke)
{
}

void TScintillaSurface::RectangleDraw(PRectangle, FillStroke)
{
}

void TScintillaSurface::RectangleFrame(PRectangle, Stroke)
{
}

void TScintillaSurface::FillRectangle(PRectangle rc, Fill fill)
{
    auto r = clipRect(rc);
    if ( surface && 0 <= r.a.x && r.a.x < r.b.x
                 && 0 <= r.a.y && r.a.y < r.b.y )
    {
        // Used to draw text selections and areas without text. The foreground color
        // also needs to be set or else the cursor will have the wrong color when
        // placed on this area.
        auto attr = defaultTextAttr;
        ::setBack(attr, convertColor(fill.colour));
        auto *cells = &surface->at(r.a.y, r.a.x);
        size_t count = r.b.x - r.a.x;
        for (int y = r.a.y; y < r.b.y; ++y)
        {
            TText::drawChar({cells, count}, ' ', attr);
            cells += surface->size.x;
        }
    }
}

void TScintillaSurface::FillRectangleAligned(PRectangle rc, Fill fill)
{
    FillRectangle(rc, fill);
}

void TScintillaSurface::FillRectangle(PRectangle rc, Surface &)
{
    FillRectangle(rc, Fill(ColourRGBA()));
}

void TScintillaSurface::RoundedRectangle(PRectangle, FillStroke)
{
}

void TScintillaSurface::AlphaRectangle(PRectangle rc, XYPOSITION, FillStroke fillStroke)
{
    auto r = clipRect(rc);
    if ( surface && 0 <= r.a.x && r.a.x < r.b.x
                 && 0 <= r.a.y && r.a.y < r.b.y )
    {
        // AlphaRectangle is used to draw FULLBOX indicators. Both fill and stroke
        // colours carry the same RGB, which turbo sets to the indicator number
        // (see turbo::setIndicatorColor); resolve the real fore/back through the
        // indicator table.
        TColorDesired fg, bg;
        bool haveFg = false, haveBg = false;
        if (indicatorColors)
        {
            int ind = fillStroke.fill.colour.OpaqueRGB();
            auto it = indicatorColors->find(ind);
            if (it != indicatorColors->end())
            {
                auto fore = ::getFore(it->second),
                     back = ::getBack(it->second);
                if (!fore.isDefault()) { fg = fore; haveFg = true; }
                if (!back.isDefault()) { bg = back; haveBg = true; }
            }
        }
        auto *cells = &surface->at(r.a.y, r.a.x);
        size_t count = r.b.x - r.a.x;
        for (int y = r.a.y; y < r.b.y; ++y)
        {
            for (size_t x = 0; x < count; ++x)
            {
                if (haveFg)
                    ::setFore(cells[x].attr, fg);
                if (haveBg)
                    ::setBack(cells[x].attr, bg);
            }
            cells += surface->size.x;
        }
    }
}

void TScintillaSurface::GradientRectangle(PRectangle, const std::vector<ColourStop> &, GradientOptions)
{
}

void TScintillaSurface::DrawRGBAImage(PRectangle, int, int, const unsigned char *)
{
}

void TScintillaSurface::Ellipse(PRectangle, FillStroke)
{
}

void TScintillaSurface::Stadium(PRectangle, FillStroke, Ends)
{
}

void TScintillaSurface::Copy(PRectangle, Point, Surface &)
{
    // No-op: AllocatePixMap shares the underlying TDrawSurface, so the source
    // content has already been drawn at its final position.
}

std::unique_ptr<IScreenLineLayout> TScintillaSurface::Layout(const IScreenLine *)
{
    return nullptr;
}

void TScintillaSurface::DrawTextNoClip( PRectangle rc, const Font *font_,
                                        XYPOSITION ybase, std::string_view text,
                                        ColourRGBA fore, ColourRGBA back )
{
    if (surface)
    {
        auto lastClip = clip;
        clip = {0, 0, surface->size.x, surface->size.y};
        DrawTextClipped(rc, font_, ybase, text, fore, back);
        clip = lastClip;
    }
}

void TScintillaSurface::DrawTextClipped( PRectangle rc, const Font *font_,
                                         XYPOSITION, std::string_view text,
                                         ColourRGBA fore, ColourRGBA back )
{
    // Scintilla's LineMarker::Draw insets SC_MARK_CHARACTER markers (fold +/-,
    // change-history |) by one pixel top and bottom. With a one-row line height
    // that collapses the rect to zero/negative height, so the marker would not
    // be drawn at all. Restore it to the single cell row it was meant to cover.
    if (rc.bottom <= rc.top)
    {
        if (rc.bottom < rc.top)
            rc.top = rc.bottom;
        rc.bottom = rc.top + 1;
    }
    auto r = clipRect(rc);
    if ( surface && 0 <= r.a.x && r.a.x < r.b.x
                 && 0 <= r.a.y && r.a.y < r.b.y )
    {
        auto attr = convertColorPair(fore, back);
        ::setStyle(attr, getStyle(font_));
        auto *cells = &surface->at(r.a.y, r.a.x);
        size_t count = r.b.x - r.a.x;
        int indent = clip.a.x - (int) rc.left;
        for (int y = r.a.y; y < r.b.y; ++y)
        {
            TText::drawStr({cells, count}, 0, text, indent, attr);
            cells += surface->size.x;
        }
    }
}

void TScintillaSurface::DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION, std::string_view text, ColourRGBA fore)
{
    auto r = clipRect(rc);
    if ( surface && 0 <= r.a.x && r.a.x < r.b.x
                 && 0 <= r.a.y && r.a.y < r.b.y )
    {
        auto fg = convertColor(fore);
        auto style = getStyle(font_);
        TScreenCell *cells = &surface->at(r.a.y, r.a.x);
        size_t count = r.b.x - r.a.x;
        int indent = clip.a.x - (int) rc.left;
        for (int y = r.a.y; y < r.b.y; ++y)
        {
            TText::drawStrEx({cells, count}, 0, text, indent, [&] (auto &attr) {
                ::setFore(attr, fg);
                ::setStyle(attr, style);
            });
            cells += surface->size.x;
        }
    }
}

void TScintillaSurface::MeasureWidths(const Font *, std::string_view text, XYPOSITION *positions)
{
    size_t i = 0, j = 1;
    while (i < text.size()) {
        size_t width = 0, k = i;
        TText::next(text, i, width);
        // I don't know why. It just works.
        j += width - 1;
        while (k < i)
            positions[k++] = (int) j;
        ++j;
    }
}

XYPOSITION TScintillaSurface::WidthText(const Font *, std::string_view text)
{
    return strwidth(text);
}

void TScintillaSurface::DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back)
{
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
}

void TScintillaSurface::DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back)
{
    DrawTextClipped(rc, font_, ybase, text, fore, back);
}

void TScintillaSurface::DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore)
{
    DrawTextTransparent(rc, font_, ybase, text, fore);
}

void TScintillaSurface::MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions)
{
    MeasureWidths(font_, text, positions);
}

XYPOSITION TScintillaSurface::WidthTextUTF8(const Font *font_, std::string_view text)
{
    return WidthText(font_, text);
}

XYPOSITION TScintillaSurface::Ascent(const Font *)
{
    return 0;
}

XYPOSITION TScintillaSurface::Descent(const Font *)
{
    return 0;
}

XYPOSITION TScintillaSurface::InternalLeading(const Font *)
{
    return 0;
}

XYPOSITION TScintillaSurface::Height(const Font *)
{
    return 1;
}

XYPOSITION TScintillaSurface::AverageCharWidth(const Font *)
{
    return 1;
}

void TScintillaSurface::SetClip(PRectangle rc)
{
    clipStack.push_back(clip);
    clip = rc;
    if (surface)
        clip.intersect({0, 0, surface->size.x, surface->size.y});
}

void TScintillaSurface::PopClip()
{
    if (!clipStack.empty())
    {
        clip = clipStack.back();
        clipStack.pop_back();
    }
}

void TScintillaSurface::FlushCachedState()
{
}

void TScintillaSurface::FlushDrawing()
{
}

} // namespace Scintilla::Internal
