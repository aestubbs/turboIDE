#ifndef TURBO_DEBUGPANELS_H
#define TURBO_DEBUGPANELS_H

#define Uses_TListViewer
#define Uses_TOutline
#define Uses_TOutlineViewer
#define Uses_TScrollBar
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// A frame shown in the Call Stack panel. 'file'/'line' (0-based) locate the
// frame's source for jump-to; 'line' < 0 means no resolvable location.
struct StackFrameItem
{
    std::string label; // e.g. "main    hello.py:3"
    std::string file;
    long line {-1};
    int frameId {0};   // DAP frame id, for re-scoping the Variables panel
};

// One row in the Variables tree: the display text ("name = value") and, when the
// value is expandable (a struct/object/array), its DAP variablesReference.
struct VarItem
{
    std::string text;
    int variablesReference {0};
};

// The Call Stack debugger panel: a scrolling, selectable list of stack frames,
// shown as a tab in the Output window (its view is swapped in/out with the
// tab). Activating a frame (Enter / double-click) fires onSelect(index) so the
// app can jump to the frame's source (and, from M3b, re-scope the Variables
// panel to that frame). Drawn with explicit colours matching the Output pane.
struct CallStackView : public TListViewer
{
    std::vector<StackFrameItem> frames;
    std::function<void(int index)> onSelect;

    CallStackView(const TRect &bounds) noexcept;

    void setFrames(std::vector<StackFrameItem> f) noexcept;
    void clearFrames() noexcept;

    void draw() override;
    void handleEvent(TEvent &ev) override;
};

// The Variables / Watches debugger panel: an expandable tree of scopes and
// variables, shown as a tab in the Output window. Scopes are the top-level
// nodes; expanding a scope or an expandable value (variablesReference > 0)
// lazily fetches its children -- onExpand fires, the app requests the children
// asynchronously, and fulfill() attaches them. An opaque token guards against a
// late response touching a tree that was rebuilt in the meantime.
struct VariablesView : public TOutline
{
    struct VarNode : public TNode
    {
        int variablesReference {0}; // > 0 = expandable (children fetched lazily)
        bool loaded {false};        // children have been fetched (or none exist)

        VarNode(TStringView aText, int varRef) noexcept :
            TNode(aText), variablesReference(varRef)
        {
            expanded = False;
            childList = nullptr;
            next = nullptr;
        }
    };

    // Fired when an unloaded expandable node is expanded: the app fetches the
    // children for 'variablesReference' and calls fulfill(token, children).
    std::function<void(int variablesReference, int token)> onExpand;

    VariablesView(const TRect &bounds, TScrollBar *vScrollBar) noexcept;

    // Rebuild the tree from a frame's scopes (each an expandable top-level node);
    // clearTree empties it. Both invalidate any in-flight expansions.
    void setScopes(const std::vector<VarItem> &scopes) noexcept;
    void clearTree() noexcept;
    // Attach the children fetched for a pending expansion. A no-op if 'token' is
    // stale (the tree was rebuilt), so it can never touch a freed node.
    void fulfill(int token, const std::vector<VarItem> &children) noexcept;

    void adjust(TNode *node, Boolean expand) override;

private:
    void setTreeRoot(VarNode *newRoot) noexcept;

    int nextToken {1};
    std::unordered_map<int, VarNode *> pending; // token -> node awaiting children
};

#endif // TURBO_DEBUGPANELS_H
