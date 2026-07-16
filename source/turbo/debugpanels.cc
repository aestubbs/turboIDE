#define Uses_TListViewer
#define Uses_TOutline
#define Uses_TOutlineViewer
#define Uses_TScrollBar
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "debugpanels.h"

#include <turbo/basicwindow.h> // shared window-chrome scheme

#include <cstdint>

namespace {

// The panel background, tracking the window's active state -- the same scheme
// the Output list and the frame resolve their colours through.
TColorDesired panelBg(TView *owner, bool active) noexcept
{
    if (!owner)
        return TColorRGB(0x10182E);
    return getBack(owner->mapColor((active ? turbo::wndFrameActive
                                           : turbo::wndFramePassive) + 1));
}

TColorRGB lighten(TColorDesired c, int pct) noexcept
{
    uint32_t v = (uint32_t) c.asRGB();
    int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    auto m = [&] (int x) { return (uint8_t) (x + (255 - x) * pct / 100); };
    return TColorRGB(m(r), m(g), m(b));
}

} // namespace

CallStackView::CallStackView(const TRect &bounds) noexcept :
    TListViewer(bounds, 1, nullptr, nullptr)
{
    options |= ofFirstClick;
    setRange(0);
}

void CallStackView::setFrames(std::vector<StackFrameItem> f) noexcept
{
    frames = std::move(f);
    setRange((short) frames.size());
    focused = 0;
    topItem = 0;
    drawView();
}

void CallStackView::clearFrames() noexcept
{
    frames.clear();
    setRange(0);
    focused = 0;
    topItem = 0;
    drawView();
}

void CallStackView::draw()
{
    bool winActive = owner && (owner->state & sfActive);
    TColorDesired bg = panelBg(owner, winActive);
    TColorAttr cNormal {TColorRGB(0xCBD6F2), bg};
    TColorAttr cTop    {TColorRGB(0xFFFFFF), bg, slBold}; // the innermost frame
    TColorRGB focusBg = lighten(bg, 26);
    bool focusedView = winActive && (state & sfSelected) != 0;

    int n = (int) frames.size();
    for (int y = 0; y < size.y; ++y)
    {
        int idx = topItem + y;
        TDrawBuffer b;
        b.moveChar(0, ' ', cNormal, size.x);
        if (idx >= 0 && idx < n)
        {
            TColorAttr c = (idx == 0) ? cTop : cNormal; // top (current) frame stands out
            if (focusedView && idx == focused)
                ::setBack(c, focusBg);
            b.moveChar(0, ' ', c, size.x);
            std::string t = frames[idx].label;
            if ((int) t.size() > size.x)
                t = t.substr(0, size.x);
            b.moveStr(0, t.c_str(), c);
        }
        writeLine(0, y, size.x, 1, b);
    }
}

void CallStackView::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown)
    {
        TPoint m = makeLocal(ev.mouse.where);
        int idx = topItem + m.y;
        if (idx >= 0 && idx < (int) frames.size())
        {
            focused = (short) idx;
            drawView();
            if ((ev.mouse.eventFlags & meDoubleClick) && onSelect)
                onSelect(idx);
        }
        clearEvent(ev);
        return;
    }
    if (ev.what == evKeyDown && ev.keyDown.keyCode == kbEnter)
    {
        if (focused >= 0 && focused < (int) frames.size() && onSelect)
            onSelect(focused);
        clearEvent(ev);
        return;
    }
    TListViewer::handleEvent(ev);
}

// ---------------------------------------------------------------------------
// VariablesView -- an expandable scopes/variables tree (lazy async children)

namespace {

VariablesView::VarNode *buildVarChildList(const std::vector<VarItem> &items) noexcept
{
    VariablesView::VarNode *first = nullptr, *last = nullptr;
    for (const VarItem &it : items)
    {
        auto *n = new VariablesView::VarNode(it.text, it.variablesReference);
        // Give an expandable value a placeholder child so the node always has a
        // real child list (base hasChildren/getNumChildren stay consistent, and
        // the outline never expands into an empty node). It is replaced with the
        // real children on the first expansion (fulfill).
        if (it.variablesReference > 0)
            n->childList = new VariablesView::VarNode("...", 0);
        if (!first) first = n; else last->next = n;
        last = n;
    }
    return first;
}

// The tree always has one synthetic, always-expanded "Variables" root; the
// scopes hang beneath it.
VariablesView::VarNode *buildVarRoot(const std::vector<VarItem> &scopes) noexcept
{
    auto *r = new VariablesView::VarNode("Variables", 0);
    r->expanded = True;
    r->childList = buildVarChildList(scopes);
    return r;
}

} // namespace

VariablesView::VariablesView(const TRect &bounds, TScrollBar *vScrollBar) noexcept :
    TOutline(bounds, nullptr, vScrollBar, buildVarRoot({}))
{
}

void VariablesView::setTreeRoot(VarNode *newRoot) noexcept
{
    pending.clear(); // invalidate in-flight expansions; their nodes are freed next
    if (root)
        disposeNode(root);
    root = newRoot;
    foc = 0;
    update();
    drawView();
}

void VariablesView::setScopes(const std::vector<VarItem> &scopes) noexcept
{
    setTreeRoot(buildVarRoot(scopes));
}

void VariablesView::clearTree() noexcept
{
    setTreeRoot(buildVarRoot({}));
}

void VariablesView::fulfill(int token, const std::vector<VarItem> &children) noexcept
{
    auto it = pending.find(token);
    if (it == pending.end())
        return; // stale: the tree was rebuilt, the node may be freed -- do nothing
    VarNode *node = it->second;
    pending.erase(it);
    if (node->childList)
        disposeNode(node->childList); // replace the placeholder / prior children
    node->childList = buildVarChildList(children);
    update();
    drawView();
}

void VariablesView::adjust(TNode *node, Boolean expand)
{
    auto *vn = (VarNode *) node;
    if (expand && vn->variablesReference > 0 && !vn->loaded && onExpand)
    {
        vn->loaded = true; // fetch children only once
        int token = nextToken++;
        pending[token] = vn;
        onExpand(vn->variablesReference, token);
    }
    TOutline::adjust(node, expand); // sets node->expanded
}
