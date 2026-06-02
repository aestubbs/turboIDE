#ifndef TURBO_SURFACE_H
#define TURBO_SURFACE_H

#define Uses_TPoint
#define Uses_TRect
#define Uses_TScreenCell
#include <tvision/tv.h>

#include <turbo/scintilla/internals.h>

#include <map>
#include <vector>

class TDrawSurface;

namespace Scintilla::Internal {

// In the terminal port a Font carries no real font; it only conveys the TVision
// cell "style" byte (bold/italic/underline) that turbo stuffs into Scintilla's
// FontWeight field (see font.cc and getStyleColor()/setStyleColor()).
struct FontTV : public Font {
    ushort style {0};
};

inline ushort getStyle(const Font *font)
{
    return font ? static_cast<const FontTV *>(font)->style : ushort(0);
}

struct TPRect : public TRect {

    using TRect::TRect;
    TPRect(PRectangle rc);

};

inline TPRect::TPRect(PRectangle rc) :
    TRect({(int) rc.left, (int) rc.top, (int) rc.right, (int) rc.bottom})
{
}

// Colour token registry (defined in surface.cc).
//
// A TVision TColorDesired packs a colour-type tag (default/BIOS/RGB/XTerm) into
// its top byte; turbo's themes rely on it (e.g. the default scheme is all BIOS
// colours). Scintilla 5.x can only store an *opaque 24-bit RGB* per style
// (SCI_STYLESETFORE -> ColourRGBA::FromIpRGB masks to 24 bits and forces alpha),
// so that tag cannot survive a round trip through Scintilla. (The previous
// vendored Scintilla 4.x stored the raw 32-bit int, so this was a non-issue.)
//
// Instead we hand Scintilla a small integer *token* in place of the colour and
// resolve it back to the full TColorDesired when drawing.
int internTColor(TColorDesired c);
TColorDesired resolveTColor(int token);

// Encode: TVision colour -> a ColourRGBA whose low 24 bits hold the token.
inline ColourRGBA convertColor(TColorDesired c)
{
    return ColourRGBA(internTColor(c));
}

// Decode: ColourRGBA from Scintilla (token in the low 24 bits) -> TVision colour.
inline TColorDesired convertColor(ColourRGBA color)
{
    return resolveTColor(color.OpaqueRGB());
}

inline TColorAttr convertColorPair(ColourRGBA fore, ColourRGBA back)
{
    return {convertColor(fore), convertColor(back)};
}

struct TScintillaSurface : public Surface {

    TDrawSurface *surface {nullptr};
    TColorAttr defaultTextAttr {};
    TPRect clip {0, 0, 0, 0};
    std::vector<TPRect> clipStack {};
    SurfaceMode mode {};
    // Maps an indicator number to its fore/back colours. AlphaRectangle resolves
    // an indicator's colours through this table (see surface.cc / scintilla.cc).
    const std::map<int, TColorAttr> *indicatorColors {nullptr};

    TPRect clipRect(TPRect r);

    // Lifecycle and capabilities
    void Init(WindowID wid) override;
    void Init(SurfaceID sid, WindowID wid) override;
    std::unique_ptr<Surface> AllocatePixMap(int width, int height) override;
    void SetMode(SurfaceMode mode_) override;
    void Release() noexcept override;
    int SupportsFeature(Scintilla::Supports feature) noexcept override;
    bool Initialised() override;
    int LogPixelsY() override;
    int PixelDivisions() override;
    int DeviceHeightFont(int points) override;

    // Geometric primitives (mostly no-ops on a character cell grid)
    void LineDraw(Point start, Point end, Stroke stroke) override;
    void PolyLine(const Point *pts, size_t npts, Stroke stroke) override;
    void Polygon(const Point *pts, size_t npts, FillStroke fillStroke) override;
    void RectangleDraw(PRectangle rc, FillStroke fillStroke) override;
    void RectangleFrame(PRectangle rc, Stroke stroke) override;
    void FillRectangle(PRectangle rc, Fill fill) override;
    void FillRectangleAligned(PRectangle rc, Fill fill) override;
    void FillRectangle(PRectangle rc, Surface &surfacePattern) override;
    void RoundedRectangle(PRectangle rc, FillStroke fillStroke) override;
    void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) override;
    void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops, GradientOptions options) override;
    void DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *pixelsImage) override;
    void Ellipse(PRectangle rc, FillStroke fillStroke) override;
    void Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) override;
    void Copy(PRectangle rc, Point from, Surface &surfaceSource) override;

    std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *screenLine) override;

    // Text
    void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override;
    void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override;
    XYPOSITION WidthText(const Font *font_, std::string_view text) override;

    void DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override;
    void MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) override;
    XYPOSITION WidthTextUTF8(const Font *font_, std::string_view text) override;

    XYPOSITION Ascent(const Font *font_) override;
    XYPOSITION Descent(const Font *font_) override;
    XYPOSITION InternalLeading(const Font *font_) override;
    XYPOSITION Height(const Font *font_) override;
    XYPOSITION AverageCharWidth(const Font *font_) override;

    void SetClip(PRectangle rc) override;
    void PopClip() override;
    void FlushCachedState() override;
    void FlushDrawing() override;

};

inline TPRect TScintillaSurface::clipRect(TPRect r) {
    // The 'clip' member is already intersected with the view's extent.
    // See SetClip().
    r.intersect(clip);
    return r;
}

}

#endif
