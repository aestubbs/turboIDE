#include "doctree.h"
#include "editwindow.h"
#include "app.h"
#include "gitclient.h"
#include <utility>
#include <vector>
#include <filesystem>
#include <turbo/tpath.h>

#define Uses_TProgram
#define Uses_TFrame
#define Uses_TDrawBuffer
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TMenuItem
#define Uses_TInputLine
#include <tvision/tv.h>

#include <turbo/basicwindow.h> // shared editor-window chrome scheme

#include <cctype>

using Node = DocumentTreeView::Node;

namespace {

// Minimum query length before filtering kicks in: filtering starts once the
// query is *longer than* three characters (i.e. 4+).
constexpr size_t kFilterMinLen = 3;

// Case-insensitive substring test: does 'haystack' contain 'needleLower'
// (which is assumed already lowercased)?
bool containsCI(const char *haystack, const std::string &needleLower) noexcept
{
    if (needleLower.empty())
        return true;
    if (!haystack)
        return false;
    std::string h;
    for (const char *p = haystack; *p; ++p)
        h += (char) std::tolower((unsigned char) *p);
    return h.find(needleLower) != std::string::npos;
}

// Single-line search box on the file-tree window. Live-filters as you type and
// clears (returning focus to the tree) on Esc.
struct TreeFilterInputLine : public TInputLine
{
    DocumentTreeWindow *win {nullptr};

    TreeFilterInputLine(const TRect &bounds, DocumentTreeWindow *aWin) noexcept :
        TInputLine(bounds, 256),
        win(aWin)
    {
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evKeyDown && ev.keyDown.keyCode == kbEsc)
        {
            if (win)
                win->clearFilterAndFocusTree();
            clearEvent(ev);
            return;
        }
        bool key = (ev.what == evKeyDown);
        TInputLine::handleEvent(ev);
        // After the base class has applied the keystroke, push the (possibly
        // changed) text into the tree filter. setFilter() no-ops if unchanged.
        if (key && win)
            win->applyFilterFromBox();
    }
};

} // namespace

namespace {

// Depth-first search over the WHOLE tree, including collapsed branches. The
// inherited TOutlineViewer::firstThat only visits expanded (on-screen) nodes,
// which is right for display-position work (focus/scroll) but wrong for looking
// a node up by path or editor: those must succeed regardless of what the user
// has expanded -- otherwise git badges and open-file links would vanish for any
// folder that happens to be collapsed.
template <class Pred>
Node *findNodeRec(TNode *list, Pred &&pred) noexcept
{
    for (TNode *n = list; n; n = n->next)
    {
        if (pred((Node *) n))
            return (Node *) n;
        if (n->childList)
            if (Node *r = findNodeRec(n->childList, pred))
                return r;
    }
    return nullptr;
}

} // namespace

DocumentTreeView::DocumentTreeView(const TRect &bounds, TScrollBar *hsb,
                                   TScrollBar *vsb, TNode *aRoot) noexcept :
    TOutline(bounds, hsb, vsb, aRoot)
{
    // The inner outline view is the one that swallows the first click of a
    // double-click (TView::handleEvent clears it when the view is selectable
    // but lacks ofFirstClick), so without this a double-click never reaches
    // selected() and a file won't open. The enclosing window sets ofFirstClick
    // too, but it is this view that does the swallowing.
    options |= ofFirstClick;
}

Node::Node(Node *parent, std::string_view p, bool isDir) noexcept :
    TNode(TPath::basename(p)),
    ptr(nullptr),
    parent(parent),
    path(p),
    isDir(isDir),
    editor(nullptr)
{
}

void Node::setEditor(EditorWindow *w) noexcept
{
    editor = w;
    refreshText();
}

void Node::refreshText() noexcept
{
    TStringView bn = TPath::basename(path);
    std::string label {bn.data(), bn.size()};
    // Mark files with unsaved changes.
    if (editor && !editor->getEditor().inSavePoint())
        label += " *";
    // Git status badge, to the right of the name (mirrors the " *" idiom).
    if (gitStatus && gitStatus != '.')
    {
        label += "  ";
        label += gitStatus;
    }
    else if (gitStatus == '.')
        label += "  \xC2\xB7"; // middle dot: directory contains changes
    delete[] text;
    text = newStr(label);
}

void Node::remove() noexcept
{
    if (next)
        ((Node *) next)->ptr = ptr;
    if (ptr)
        *ptr = next;
    next = nullptr;
    ptr = nullptr;
    parent = nullptr;
}

void Node::dispose() noexcept
{
    remove();
    delete this;
}

// Directories shall appear before files, both sorted alphabetically.
enum NodeType { ntDir, ntFile };
using NodeKey = std::pair<NodeType, std::string_view>;

static NodeKey getKey(Node *node) noexcept
{
    return {node->isDir ? ntDir : ntFile, node->text};
}

static void putNode(TNode **indirect, Node *node) noexcept
// Pre: node->parent is properly set.
{
    auto key = getKey(node);
    TNode *other;
    while ((other = *indirect))
    {
        if (key < getKey((Node *) other))
        {
            node->next = other;
            ((Node *) other)->ptr = &node->next;
            break;
        }
        indirect = &other->next;
    }
    *indirect = node;
    node->ptr = indirect;
}

static void scanInto(Node *parent, TNode **list, const std::string &dirPath,
                     int depth, bool showHidden) noexcept
{
    std::error_code ec;
    std::filesystem::directory_iterator it(dirPath, ec), end;
    if (ec)
        return;
    for (; it != end; it.increment(ec))
    {
        if (ec)
            break;
        const auto &entry = *it;
        std::string name = entry.path().filename().string();
        // Hidden entries (dotfiles and dot-directories such as .git) are skipped
        // unless the user has opted to show them. The .git directory and Turbo's
        // own .turbo project folder stay hidden either way (noise, not source).
        if (name.empty())
            continue;
        if (name[0] == '.' && (!showHidden || name == ".git" || name == ".turbo"))
            continue;
        std::error_code ec2;
        bool isDir = entry.is_directory(ec2);
        std::string full = entry.path().string();
        auto *node = new Node(parent, full, isDir);
        if (isDir)
            // Every directory starts collapsed (including the top level), so the
            // tree opens compact; the user expands what they want, like any
            // other folder. (depth is no longer used to force expansion.)
            node->expanded = False;
        putNode(list, node);
        if (isDir)
            scanInto(node, &node->childList, full, depth + 1, showHidden);
    }
}

void DocumentTreeView::scanDirectory(std::string_view aRootPath) noexcept
{
    rootPath = std::string {aRootPath};
    scanInto(nullptr, &root, rootPath, 0, showHidden);
    reinjectLuaNodes(); // no-op unless the Lua scripts section is enabled
    update();
    drawView();
}

// Dispose a synthetic Lua group: its leaf children first, then the group node
// itself. Only the group and its children are freed -- never its siblings.
static void disposeLuaGroup(DocumentTreeView::Node *&group) noexcept
{
    if (!group)
        return;
    while (group->childList)
        ((DocumentTreeView::Node *) group->childList)->dispose();
    group->dispose();
    group = nullptr;
}

void DocumentTreeView::reinjectLuaNodes() noexcept
{
    disposeLuaGroup(luaProjectGroup);
    disposeLuaGroup(luaHomeGroup);
    if (!showLuaScripts)
        return;
    auto build = [this] (const char *label,
                         const std::vector<std::string> &paths) -> Node * {
        if (paths.empty())
            return nullptr;
        // Synthetic directory node; its path is just the label (no real file).
        auto *group = new Node(nullptr, label, true);
        group->expanded = True; // open so the scripts are visible at a glance
        putNode(&root, group);
        for (const auto &p : paths)
            putNode(&group->childList, new Node(group, p, false));
        return group;
    };
    luaProjectGroup = build("Lua Scripts (project)", luaProjectScripts);
    luaHomeGroup = build("Lua Scripts (global)", luaHomeScripts);
    if (filtering())
        recomputeVisibility();
    update();
    drawView();
}

void DocumentTreeView::setLuaScripts(bool show, std::vector<std::string> projectScripts,
                                     std::vector<std::string> homeScripts) noexcept
{
    showLuaScripts = show;
    luaProjectScripts = std::move(projectScripts);
    luaHomeScripts = std::move(homeScripts);
    reinjectLuaNodes();
}

void DocumentTreeView::setShowHidden(bool show) noexcept
{
    if (show == showHidden)
        return;
    showHidden = show;
    if (rootPath.empty())
        return; // not scanned yet; the flag will take effect on the first scan
    // Remember which editors are open so we can re-link them after the rebuild
    // (disposeNode frees every node, dropping the old editor links).
    std::vector<EditorWindow *> openEditors;
    firstThat([&] (Node *node, int) {
        if (node->editor)
            openEditors.push_back(node->editor);
        return false; // visit all
    });
    // disposeNode(root) frees every node, including the synthetic Lua groups;
    // null the pointers first (don't double-free) and rebuild them afterwards.
    luaProjectGroup = nullptr;
    luaHomeGroup = nullptr;
    disposeNode(root);
    root = nullptr;
    foc = 0;
    scanInto(nullptr, &root, rootPath, 0, showHidden);
    for (auto *w : openEditors)
        linkEditor(w);
    reinjectLuaNodes(); // re-apply the Lua scripts section if it was enabled
    update();
    drawView();
}

DocumentTreeView::Node *DocumentTreeView::findDir(std::string_view path) noexcept
{
    return findNodeRec(root, [path] (Node *node) {
        return node->isDir && node->path == path;
    });
}

void DocumentTreeView::refreshNode(std::string_view path) noexcept
{
    if (auto *n = findByPath(path))
    {
        n->refreshText();
        update();
        drawView();
    }
}

void DocumentTreeView::removeNode(std::string_view path) noexcept
{
    Node *n = findByPath(path);
    if (!n)
        n = findDir(path);
    if (n)
    {
        n->dispose();
        if (filtering())
            recomputeVisibility(); // keep the filtered view in sync
        update();
        drawView();
    }
}

void DocumentTreeView::addNode(std::string_view path, bool isDir) noexcept
{
    // Already present, or a hidden entry we deliberately don't show.
    if (findByPath(path) || (isDir && findDir(path)))
        return;
    TStringView base = TPath::basename(path);
    if (base.empty())
        return;
    // Honour the show-hidden setting (mirrors scanInto); .git and .turbo stay hidden.
    if (base[0] == '.' && (!showHidden || base == ".git" || base == ".turbo"))
        return;
    // Locate the parent list: the root list for a top-level entry, else the
    // parent directory's child list (only if that directory is in the tree).
    TStringView dir = TPath::dirname(path);
    TNode **list;
    Node *parent = nullptr;
    if (dir == TStringView(rootPath))
        list = &root;
    else
    {
        parent = findDir(dir);
        if (!parent)
            return; // parent dir not in the tree (collapsed/unknown): skip
        list = &parent->childList;
    }
    auto *node = new Node(parent, std::string(path), isDir);
    putNode(list, node);
    if (isDir)
        scanInto(node, &node->childList, std::string(path), 1, showHidden);
    if (filtering())
        recomputeVisibility(); // a new node defaults to visible; reclassify it
    update();
    drawView();
}

void DocumentTreeView::openOrToggle(Node *node) noexcept
{
    if (!node)
        return;
    if (node->isDir)
    {
        adjust(node, Boolean(!isExpanded(node)));
        update();
        drawView();
    }
    else if (node->editor)
        node->editor->focus();
    else if (auto *app = (TurboApp *) TProgram::application)
        app->openFileFromTree(node->path.c_str());
}

void DocumentTreeView::selected(int i) noexcept
{
    // Triggered by Enter or a double-click (not by moving the highlight).
    openOrToggle((Node *) getNode(i));
}

void DocumentTreeView::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown)
    {
        TPoint local = makeLocal(ev.mouse.where);
        int row = delta.y + local.y;
        bool onRow = row >= 0 && row < limit.y;
        // Right-click: open the context menu for the clicked node. Handle it
        // ourselves so the base outline doesn't also treat it as a select/toggle.
        if (ev.mouse.buttons & mbRightButton)
        {
            if (onRow)
            {
                foc = row;
                update();
                drawView();
                showContextMenu(row, ev.mouse.where);
            }
            clearEvent(ev);
            return;
        }
        // Left double-click: open/toggle the clicked node directly. The base
        // class would do this on meDoubleClick too, but only reliably once the
        // tree already has focus; doing it here makes a plain double-click work
        // even when arriving from another window. clearEvent() stops the base
        // from acting on the same event (which would open the file twice).
        if ((ev.mouse.buttons & mbLeftButton) &&
            (ev.mouse.eventFlags & meDoubleClick) && onRow)
        {
            foc = row;
            openOrToggle((Node *) getNode(row));
            clearEvent(ev);
            return;
        }
    }
    // Arrow-key tree navigation. The base TOutline aliases Left->Up and
    // Right->Down (plain focus movement, no horizontal scroll), so we override
    // Left/Right entirely to give them the conventional expand/collapse/navigate
    // semantics. Only act while focused; Up/Down/Enter still go to the base.
    if (ev.what == evKeyDown && (state & sfFocused))
    {
        ushort key = ev.keyDown.keyCode;
        // Ctrl-F jumps the cursor into the search box; Esc clears any filter.
        // (Intercepted here so Ctrl-F doesn't also trigger the editor's Find.)
        if (key == kbCtrlF)
        {
            if (win)
                win->focusFilter();
            clearEvent(ev);
            return;
        }
        if (key == kbEsc && filtering())
        {
            if (win)
                win->clearFilter();
            clearEvent(ev);
            return;
        }
        if (key == kbRight || key == kbLeft)
        {
            auto *node = (Node *) getNode(foc);
            if (!node)
            {
                TOutline::handleEvent(ev);
                return;
            }
            if (key == kbRight)
            {
                // Right on a collapsed directory expands it. On an already
                // expanded directory (or a file) move down one row, which lands
                // on the first child of an expanded dir.
                if (node->isDir && !isExpanded(node))
                    openOrToggle(node);
                else
                {
                    foc++;
                    update();   // update() -> adjustFocus(foc): clamps + scrolls
                    drawView();
                }
            }
            else // kbLeft
            {
                // Left on an expanded directory collapses it. Otherwise (file or
                // collapsed dir) jump focus to the parent directory's row, if any.
                if (node->isDir && isExpanded(node))
                    openOrToggle(node);
                else if (node->parent)
                {
                    Node *parent = node->parent;
                    int parentPos = -1;
                    firstThat([parent, &parentPos] (Node *n, int pos) {
                        if (n == parent) { parentPos = pos; return true; }
                        return false;
                    });
                    if (parentPos >= 0)
                    {
                        foc = parentPos;
                        update();   // scroll the parent row into view
                        drawView();
                    }
                }
                // Top-level file/collapsed dir: no parent, so do nothing.
            }
            clearEvent(ev);
            return;
        }
    }
    TOutline::handleEvent(ev);
}

// --- Name filter -----------------------------------------------------------

namespace {
// Set 'visible' on a node and its entire subtree.
void markSubtree(Node *n, bool v) noexcept
{
    n->visible = v;
    for (Node *c = (Node *) n->childList; c; c = (Node *) c->next)
        markSubtree(c, v);
}
// Does a file node satisfy the git-status filter?
bool gitFilterMatch(const Node *n, int gf) noexcept
{
    if (gf == tgfAll)
        return true;
    switch (gf)
    {
        case tgfModified:   return n->gitStatus == 'M' || n->gitStatus == 'R' ||
                                   n->gitStatus == 'D' || n->gitStatus == 'A';
        case tgfStaged:     return n->gitStaged;
        case tgfUntracked:  return n->gitStatus == '?';
        case tgfConflicted: return n->gitStatus == 'U';
    }
    return true;
}
// Compute visibility for a node and its descendants; returns whether the node
// is visible. A file is visible iff its name matches the query AND its git state
// matches the git filter. A folder is visible iff its own name matches (revealing
// its whole subtree -- name filter only) or any descendant is visible.
bool computeVisible(Node *n, const std::string &q, int gf) noexcept
{
    bool nameMatch = q.empty() || containsCI(n->text, q);
    if (!n->isDir)
    {
        bool v = nameMatch && gitFilterMatch(n, gf);
        n->visible = v;
        return v;
    }
    // The "folder name matches -> reveal its whole subtree" shortcut applies only
    // to the pure name filter; a git filter must still match descendant files.
    if (gf == tgfAll && !q.empty() && containsCI(n->text, q))
    {
        markSubtree(n, true);
        return true;
    }
    bool any = false;
    for (Node *c = (Node *) n->childList; c; c = (Node *) c->next)
        any |= computeVisible(c, q, gf);
    n->visible = any;
    return any;
}
} // namespace

void DocumentTreeView::recomputeVisibility() noexcept
{
    if (!filtering())
        return; // the overrides ignore 'visible' when no filter is active
    for (Node *n = (Node *) root; n; n = (Node *) n->next)
        computeVisible(n, filter, gitFilter);
}

void DocumentTreeView::setFilter(std::string_view query) noexcept
{
    std::string q;
    for (char c : query)
        q += (char) std::tolower((unsigned char) c);
    if (q.size() <= kFilterMinLen)
        q.clear(); // 3 chars or fewer: no filtering
    if (q == filter)
        return;    // nothing changed
    // Keep the same node highlighted across the change (so clearing the filter
    // leaves the active node where it was).
    Node *focusedNode = (Node *) getNode(foc);
    filter = std::move(q);
    recomputeVisibility();
    int pos = -1;
    if (focusedNode)
        firstThat([focusedNode, &pos] (Node *n, int p) {
            if (n == focusedNode) { pos = p; return true; }
            return false;
        });
    foc = (pos >= 0) ? pos : 0; // first visible row if the old node is now hidden
    update();    // recompute range, clamp foc, scroll it into view
    drawView();
}

void DocumentTreeView::setGitFilter(int gf) noexcept
{
    if (gf == gitFilter)
        return;
    Node *focusedNode = (Node *) getNode(foc); // keep the same node highlighted
    gitFilter = gf;
    recomputeVisibility();
    int pos = -1;
    if (focusedNode)
        firstThat([focusedNode, &pos] (Node *n, int p) {
            if (n == focusedNode) { pos = p; return true; }
            return false;
        });
    foc = (pos >= 0) ? pos : 0;
    update();
    drawView();
}

// Outline iteration overrides -- skip non-visible nodes while filtering so the
// base viewer lays out, scrolls and draws only the matching subset.

TNode *DocumentTreeView::getRoot()
{
    TNode *r = root;
    if (filtering())
        while (r && !((Node *) r)->visible)
            r = r->next;
    return r;
}

TNode *DocumentTreeView::getNext(TNode *node)
{
    TNode *n = node->next;
    if (filtering())
        while (n && !((Node *) n)->visible)
            n = n->next;
    return n;
}

TNode *DocumentTreeView::getChild(TNode *node, int i)
{
    TNode *c = node->childList;
    if (filtering())
        while (c && !((Node *) c)->visible)
            c = c->next;
    while (i-- > 0 && c)
        c = getNext(c);
    return c;
}

int DocumentTreeView::getNumChildren(TNode *node)
{
    if (!filtering())
        return TOutline::getNumChildren(node);
    int n = 0;
    for (TNode *c = getChild(node, 0); c; c = getNext(c))
        ++n;
    return n;
}

Boolean DocumentTreeView::hasChildren(TNode *node)
{
    if (!filtering())
        return TOutline::hasChildren(node);
    return Boolean(getChild(node, 0) != 0);
}

Boolean DocumentTreeView::isExpanded(TNode *node)
{
    if (filtering())
        return hasChildren(node); // show every kept folder expanded
    return TOutline::isExpanded(node);
}

void DocumentTreeView::showContextMenu(int row, TPoint where) noexcept
{
    auto *node = (Node *) getNode(row);
    if (!node)
        return;
    // Capture identity by value: the menu is modal, and while it is up the idle
    // loop can still run the filesystem watcher, which may rebuild the tree and
    // free 'node'. So below we act on these copies and re-resolve by path, never
    // touching 'node' again.
    std::string path = node->path;
    bool isDir = node->isDir;
    char gs = node->gitStatus;

    // Build the item chain by hand so the optional Revert entry is easy to add.
    auto *items  = new TMenuItem("~O~pen", cmTreeOpen, kbNoKey, hcNoContext);
    auto *rename = new TMenuItem("~R~ename...", cmTreeRename, kbNoKey, hcNoContext);
    auto *newf   = new TMenuItem("~N~ew File...", cmTreeNewFile, kbNoKey, hcNoContext);
    // Accelerator 'F' (not 'o') so it doesn't collide with "Open" in this menu.
    auto *newd   = new TMenuItem("New ~F~older...", cmTreeNewFolder, kbNoKey, hcNoContext);
    auto *sep    = &newLine();
    auto *add    = new TMenuItem("Git ~A~dd", cmTreeGitAdd, kbNoKey, hcNoContext);
    items->append(rename);
    rename->append(newf);
    newf->append(newd);
    newd->append(sep);
    sep->append(add);
    TMenuItem *tail = add;
    // Offer Revert only for a tracked file with committed content to restore
    // from: modified ('M'), deleted ('D') or conflicted ('U'). Not for new/
    // untracked files (nothing at HEAD), renames, or directories.
    if (!isDir && (gs == 'M' || gs == 'D' || gs == 'U'))
    {
        auto *revert = new TMenuItem("Git Re~v~ert", cmTreeGitRevert, kbNoKey, hcNoContext);
        tail->append(revert);
        tail = revert;
    }

    ushort cmd = popupMenu(where, *items, nullptr);

    auto *app = (TurboApp *) TProgram::application;
    switch (cmd)
    {
        case cmTreeOpen:
            // Re-resolve by path (see note above) rather than reuse 'node'.
            if (isDir)
            {
                if (auto *n = findDir(path))
                    openOrToggle(n);
            }
            else if (auto *n = findByPath(path))
                openOrToggle(n);                 // focus existing editor, else open
            else if (app)
                app->openFileFromTree(path.c_str());
            break;
        case cmTreeRename:
            if (app)
                app->treeRenamePath(path, isDir);
            break;
        case cmTreeNewFile:
        case cmTreeNewFolder:
        {
            // Create inside a folder; for a file, create alongside it.
            std::string dir = path;
            if (!isDir)
            {
                TStringView d = TPath::dirname(path);
                dir.assign(d.data(), d.size());
            }
            if (app)
            {
                if (cmd == cmTreeNewFile)
                    app->treeCreateFile(dir);
                else
                    app->treeCreateFolder(dir);
            }
            break;
        }
        case cmTreeGitAdd:
            if (app)
                app->treeStagePath(path);
            break;
        case cmTreeGitRevert:
            if (app)
                app->treeRevertPath(path);
            break;
    }
}

namespace {

struct DrawCtx { TDrawBuffer *b; int last; };

enum class TreeRole { Normal, Focused, Selected };

// Row colours for the file tree, derived from the shared window-chrome scheme so
// the tree matches the editor windows (and follows any theme edits). Returned as
// a TAttrPair: the low attr paints folder/expanded rows and the row fill, while
// the high attr (color >> 8) paints file/leaf text.
TAttrPair treeColors(TreeRole role, bool active) noexcept
{
    using namespace turbo;
    // Normal rows track the window's active state (the unified blue when active,
    // a dimmer shade when not) -- matching the frame and the other windows.
    TColorDesired bg = ::getBack(windowSchemeActive[active ? wndFrameActive
                                                           : wndFramePassive]);
    switch (role)
    {
        case TreeRole::Focused:
            return TAttrPair(TColorAttr(0xFFFFFF, 0x3A7FD0), TColorAttr(0xFFFFFF, 0x3A7FD0));
        case TreeRole::Selected:
            return TAttrPair(TColorAttr(0xFFFFFF, 0x2F6FB0), TColorAttr(0xFFFFFF, 0x2F6FB0));
        default: // Normal: folders bright, files slightly dimmer, on the window blue.
            return TAttrPair(TColorAttr(0xEAEFFF, bg), TColorAttr(0xCBD6F2, bg));
    }
}

// Mirrors TOutlineViewer's internal drawTree(), with one addition: files that
// are open in an editor (node->editor != null) are drawn in bold.
Boolean drawNode( TOutlineViewer *v, TNode *cur, int level, int position,
                  long lines, ushort flags, void *arg )
{
    auto *ctx = (DrawCtx *) arg;
    TDrawBuffer &dBuf = *ctx->b;
    if (position >= v->delta.y)
    {
        if (position >= v->delta.y + v->size.y)
            return True;
        bool winActive = v->owner && (v->owner->state & sfActive);
        TAttrPair color;
        if (position == v->foc && (v->state & sfFocused))
            color = treeColors(TreeRole::Focused, winActive);
        else if (v->isSelected(position))
            color = treeColors(TreeRole::Selected, winActive);
        else
            color = treeColors(TreeRole::Normal, winActive);
        dBuf.moveChar(0, ' ', color, v->size.x);
        int x;
        {
            // The root level draws no connector for files, so its vertical line
            // runs only between folders: column 0 (the root ancestor's line) is
            // kept only while the root folder has another folder after it. A root
            // folder followed only by files is therefore the "last folder" -- it
            // ends as a corner, and its whole subtree drops column 0.
            long glines = lines;
            ushort gflags = flags;
            {
                Node *root = (Node *) cur;
                while (root->parent) root = root->parent;
                bool rootNextFolder = root->next && ((Node *) root->next)->isDir;
                if (rootNextFolder) glines |= 1L; else glines &= ~1L;
                if (level == 0 && ((Node *) cur)->isDir && !rootNextFolder)
                    gflags |= ovLast; // last folder among the root entries -> corner
            }
            TStringView graph = v->getGraph(level, glines, gflags);
            x = strwidth(graph) - v->delta.x;
            if (x > 0)
                dBuf.moveStr(0, graph, color, (ushort) -1U, v->delta.x);
            delete[] (char *) graph.data();
        }
        {
            TStringView text = v->getText(cur);
            TColorAttr c = (flags & ovExpanded) ? color : (color >> 8);
            if (((Node *) cur)->editor)
                setStyle(c, getStyle(c) | slBold);
            // Tint rows by git status (only when not the focused/selected row,
            // which keep their highlight colours for legibility).
            char gs = ((Node *) cur)->gitStatus;
            if (gs && !(position == v->foc && (v->state & sfFocused))
                   && !v->isSelected(position))
            {
                TColorDesired fg {};
                switch (gs)
                {
                    case 'M': case 'R': fg = 0xE6C98C; break; // gold (modified)
                    case 'A': case '?': fg = 0x9CDC8C; break; // green (added/new)
                    case 'D':           fg = 0xE06C75; break; // red (deleted)
                    case 'U':           fg = 0xD79CD2; break; // magenta (conflict)
                    case '.':           fg = 0x9AA6CE; break; // dim (dir w/ changes)
                    default:            fg = 0xCBD6F2; break;
                }
                setFore(c, fg);
            }
            dBuf.moveStr(max(0, x), text, c, (ushort) -1U, max(0, -x));
        }
        v->writeLine(0, position - v->delta.y, v->size.x, 1, dBuf);
        ctx->last = position;
    }
    return False;
}

} // namespace

char *DocumentTreeView::getGraph(int level, long lines, ushort flags)
{
    if (level == 0)
    {
        // One column, aligned across root entries, then a space before the name:
        //   '+' collapsed, '┬' expanded (a folder follows), '└' expanded (last
        //   folder), ' ' file. The down-leg '┬' is only used when the root line
        //   continues to another folder below; the last folder closes with a
        //   corner so its leg doesn't dangle into the trailing root files.
        char *g = new char[3];
        if (!(flags & ovExpanded))
            g[0] = '+';                 // has children, currently collapsed
        else if ((flags & ovChildren) && !(flags & ovLast))
            g[0] = '\xC2';              // ┬ : expanded, another folder follows
        else if (flags & ovChildren)
            g[0] = '\xC0';              // └ : expanded last folder, line ends here
        else
            g[0] = ' ';                 // leaf
        g[1] = ' ';                     // gap between the marker and the name
        g[2] = '\0';
        return g;
    }
    // Deeper levels, built as tight as possible: one column per ancestor level
    // (a vertical 'lines' bar or a gap) followed immediately by this item's
    // connector and then the name -- no filler and no trailing marker column.
    // The connector sits directly under the first letter of the parent name.
    // A collapsed folder shows '+' in the connector slot (the classic
    // expandable-node marker), so the affordance survives without any extra
    // width; files and expanded folders show the usual branch glyph.
    char *g = new char[level + 3]; // ancestor cols + connector + space + NUL
    char *p = g;
    long l = lines;
    for (int i = 0; i < level; ++i, l >>= 1)
        *p++ = (l & 1) ? '\xB3' : ' ';          // │ (continues) or gap
    if (!(flags & ovExpanded))
        *p++ = '+';                             // collapsed folder
    else if ((flags & ovChildren) && !(flags & ovLast))
        // ┼ : an expanded folder with siblings below. The sibling line runs
        // through it (up + down) so it stays connected to its parent and the
        // siblings around it, while the horizontal bar leads into its own name
        // and (one column right) its children -- a plain '┬' would break the
        // line by lacking the upward leg.
        *p++ = '\xC5';
    else
        // └ for any last child (file or expanded folder -- its line ends, so no
        // leg to dangle), ├ for a non-last file.
        *p++ = (flags & ovLast) ? '\xC0' : '\xC3';
    *p++ = ' ';                                 // gap between the connector and the name
    *p = '\0';
    return g;
}

void DocumentTreeView::draw()
{
    TDrawBuffer dBuf;
    DrawCtx ctx {&dBuf, -1};
    TOutlineViewer::firstThat(drawNode, &ctx);
    TAttrPair nrmColor = treeColors(TreeRole::Normal, owner && (owner->state & sfActive));
    dBuf.moveChar(0, ' ', nrmColor, size.x);
    writeLine(0, ctx.last + 1, size.x, size.y - (ctx.last - delta.y), dBuf);
}

void DocumentTreeView::linkEditor(EditorWindow *w) noexcept
{
    if (w->filePath().empty())
        return;
    if (auto *node = findByPath(w->filePath()))
    {
        node->setEditor(w);
        update();
        drawView();
    }
}

void DocumentTreeView::unlinkEditor(EditorWindow *w) noexcept
{
    // Full-tree search (not findByEditor, which is visible-only): the link must
    // be cleared even if the file's folder is collapsed, or a closed editor's
    // dangling pointer would linger on a hidden node and crash when next drawn.
    if (auto *node = findNodeRec(root, [w] (Node *n) { return n->editor == w; }))
    {
        node->setEditor(nullptr);
        update();
        drawView();
    }
}

void DocumentTreeView::focusEditor(EditorWindow *w) noexcept
{
    // Move the highlight onto the active editor's node and scroll it into view.
    int i;
    if (findByEditor(w, &i))
    {
        foc = i;
        update();   // adjustFocus(foc) scrolls the node into view
        drawView();
    }
}

void DocumentTreeView::focusNext() noexcept
{
    // Cycle forward to the next file currently open in an editor.
    firstThat([this] (Node *node, int pos) {
        if (node->editor && pos > foc) {
            selected(pos);
            return true;
        }
        return false;
    });
}

void DocumentTreeView::focusPrev() noexcept
{
    // Cycle backward to the previous file currently open in an editor.
    int prevPos = -1;
    firstThat([this, &prevPos] (Node *node, int pos) {
        if (node->editor) {
            if (pos < foc)
                prevPos = pos;
            else if (prevPos >= 0) {
                selected(prevPos);
                return true;
            }
        }
        return false;
    });
}

static Node *findEditorNode(Node *list, EditorWindow *w) noexcept
{
    for (Node *n = list; n; n = (Node *) n->next)
    {
        if (n->editor == w)
            return n;
        if (n->childList)
            if (Node *r = findEditorNode((Node *) n->childList, w))
                return r;
    }
    return nullptr;
}

void DocumentTreeView::revealEditor(EditorWindow *w) noexcept
{
    // Search the whole tree (including collapsed branches) for the editor's
    // node, expand its ancestors so it becomes visible, then highlight it.
    Node *node = findEditorNode((Node *) root, w);
    if (!node)
        return;
    // A reveal must win over any active filter that might be hiding the node.
    if (filtering() && win)
        win->clearFilter();
    for (Node *p = node->parent; p; p = p->parent)
        p->expanded = True;
    update();
    focusEditor(w);
}

Node* DocumentTreeView::findByEditor(const EditorWindow *w, int *pos) noexcept
{
    return firstThat(
    [w, pos] (Node *node, int position)
    {
        if (node->editor && node->editor == w) {
            if (pos)
                *pos = position;
            return true;
        }
        return false;
    });
}

Node* DocumentTreeView::findByPath(std::string_view path) noexcept
{
    return findNodeRec(root, [path] (Node *node) {
        return !node->isDir && node->path == path;
    });
}

void DocumentTreeView::collectFilePaths(std::vector<std::string> &out) noexcept
{
    findNodeRec(root, [&out] (Node *node) {
        if (!node->isDir)
            out.push_back(node->path);
        return false; // visit every node
    });
}

static char badgeFor(const GitFileStatus &s) noexcept
{
    switch (s.state)
    {
        case GitFileState::Modified:   return 'M';
        case GitFileState::Added:      return 'A';
        case GitFileState::Deleted:    return 'D';
        case GitFileState::Renamed:    return 'R';
        case GitFileState::Untracked:  return '?';
        case GitFileState::Conflicted: return 'U';
        default:                       return 0;
    }
}

static void clearStatusRecursive(Node *list) noexcept
{
    for (Node *n = list; n; n = (Node *) n->next)
    {
        bool had = n->gitStatus != 0;
        n->gitStatus = 0;
        n->gitStaged = false;
        if (had)
            n->refreshText();
        if (n->childList)
            clearStatusRecursive((Node *) n->childList);
    }
}

void DocumentTreeView::applyGitStatus(
    const std::unordered_map<std::string, GitFileStatus> &files) noexcept
{
    clearStatusRecursive((Node *) root);
    for (auto &kv : files)
    {
        Node *node = findByPath(kv.first);
        if (!node)
            continue; // file outside the scanned tree (e.g. ignored dir)
        node->gitStatus = badgeFor(kv.second);
        node->gitStaged = kv.second.staged;
        node->refreshText();
        // Roll the "contains changes" marker up to ancestor directories.
        for (Node *p = node->parent; p; p = p->parent)
            if (p->gitStatus == 0)
            {
                p->gitStatus = '.';
                p->refreshText();
            }
    }
    // Keep an active git-status filter live as badges change (e.g. a file that
    // just became conflicted should appear under the Conflicted filter).
    if (filtering())
        recomputeVisibility();
    update();
    drawView();
}

namespace {

const char *gitFilterLabel(int gf) noexcept
{
    switch (gf)
    {
        case tgfModified:   return "Modified";
        case tgfStaged:     return "Staged";
        case tgfUntracked:  return "Untracked";
        case tgfConflicted: return "Conflicted";
        default:            return "All";
    }
}

// The git-status filter dropdown, a one-row control under the name filter.
// Clicking it pops a menu of states; choosing one filters the tree.
struct TreeGitFilterBar : public TView
{
    DocumentTreeWindow *win {nullptr};

    TreeGitFilterBar(const TRect &b, DocumentTreeWindow *w) noexcept :
        TView(b), win(w) { eventMask |= evMouseDown; }

    void draw() override
    {
        int gf = (win && win->tree) ? win->tree->gitFilter : tgfAll;
        TColorAttr c = win ? win->mapColor(turbo::wndInputLineNormal + 1) : TColorAttr {};
        TDrawBuffer b;
        b.moveChar(0, ' ', c, size.x);
        std::string s = "Git: " + std::string(gitFilterLabel(gf));
        b.moveStr(1, s.c_str(), c);
        if (size.x >= 3)
            b.moveStr(size.x - 2, "v", c); // dropdown affordance
        writeLine(0, 0, size.x, 1, b);
    }

    void handleEvent(TEvent &ev) override
    {
        if (ev.what == evMouseDown && win && win->tree)
        {
            static const char *labels[tgfCount] =
                {"All", "Modified", "Staged", "Untracked", "Conflicted"};
            TMenuItem *head = nullptr, *tail = nullptr;
            for (int i = 0; i < tgfCount; ++i)
            {
                auto *it = new TMenuItem(labels[i], cmTreeGitFilterBase + i,
                                         kbNoKey, hcNoContext);
                if (!head) head = tail = it;
                else { tail->append(it); tail = it; }
            }
            ushort cmd = popupMenu(ev.mouse.where, *head, nullptr);
            int sel = (int) cmd - cmTreeGitFilterBase;
            if (sel >= 0 && sel < tgfCount)
            {
                win->tree->setGitFilter(sel);
                drawView();
            }
            clearEvent(ev);
            return;
        }
        TView::handleEvent(ev);
    }
};

// The tree window's frame, customised to rule one interior row (the filter-area
// divider) with a line that joins the side borders via the proper tee
// connectors -- so it reads as part of the window chrome rather than a detached
// rule floating inside it. Drawn by the frame (not a separate view) so it stays
// correct across the frame's own redraws (activation, title change).
struct TreeFrame : public TFrame
{
    int dividerRow {-1}; // window-local row to rule, or -1 for none

    TreeFrame(const TRect &b) noexcept : TFrame(b) {}

    void draw() override
    {
        TFrame::draw();
        if (dividerRow <= 0 || dividerRow >= size.y - 1)
            return;
        // Match the border colour, then lay a single-line rule with double-line
        // tee junctions (CP437: 0xC7 = ╟, 0xC4 = ─, 0xB6 = ╢) -- the same bytes
        // TFrame uses, so they track the active/passive frame palette.
        TColorAttr c = getColor((state & sfActive) ? 0x0503 : 0x0101);
        TDrawBuffer b;
        for (int x = 0; x < size.x; ++x)
        {
            b.putChar(x, x == 0 ? '\xC7' : (x == size.x - 1 ? '\xB6' : '\xC4'));
            b.putAttribute(x, c);
        }
        writeLine(0, dividerRow, size.x, 1, b);
    }
};

} // namespace

TFrame *DocumentTreeWindow::initFrame(TRect bounds)
{
    return new TreeFrame(bounds);
}

DocumentTreeWindow::DocumentTreeWindow(const TRect &bounds, DocumentTreeWindow **ptr) noexcept :
    TWindowInit(&DocumentTreeWindow::initFrame),
    TWindow(bounds, "Files", wnNoNumber),
    ptr(ptr)
{
    // Without ofFirstClick, the first click of a double-click is swallowed just
    // to activate the window (when focus was in an editor), so the outline never
    // sees a double-click and a file won't open. Forward that first click.
    options |= ofFirstClick;
    auto *hsb = standardScrollBar(sbHorizontal),
         *vsb = standardScrollBar(sbVertical);
    // Delineated filter area at the top: name filter (row 0), git-status filter
    // dropdown (row 1), a divider (row 2, ruled by the frame); the tree below.
    TRect inner = getExtent();
    inner.grow(-1, -1);
    TRect nameR = inner; nameR.b.y = nameR.a.y + 1;
    TRect gitR  = inner; gitR.a.y = nameR.b.y;  gitR.b.y = gitR.a.y + 1;
    TRect divR  = inner; divR.a.y = gitR.b.y;   divR.b.y = divR.a.y + 1;
    TRect treeR = inner; treeR.a.y = divR.b.y;

    auto *box = new TreeFilterInputLine(nameR, this);
    box->growMode = gfGrowHiX; // full width, pinned to the top
    insert(box);
    filterBox = box;

    auto *gbar = new TreeGitFilterBar(gitR, this);
    gbar->growMode = gfGrowHiX;
    insert(gbar);

    // The divider row is ruled by the frame so the line joins the side borders.
    if (frame)
        ((TreeFrame *) frame)->dividerRow = divR.a.y;

    // The vertical scrollbar belongs to the tree only: start it below the filter
    // area so it doesn't run up alongside the filter rows.
    {
        TRect sr = vsb->getBounds();
        sr.a.y = treeR.a.y;
        vsb->setBounds(sr);
        vsb->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    }

    tree = new DocumentTreeView(treeR, hsb, vsb, nullptr);
    tree->growMode = gfGrowHiX | gfGrowHiY;
    tree->win = this;
    insert(tree);
    setCurrent(tree, normalSelect); // the tree is the default focus, not the box
}

void DocumentTreeWindow::focusFilter() noexcept
{
    if (filterBox)
        filterBox->select(); // make it the current view; cursor lands in it
}

void DocumentTreeWindow::applyFilterFromBox() noexcept
{
    if (filterBox && tree)
        tree->setFilter(filterBox->data);
}

void DocumentTreeWindow::clearFilter() noexcept
{
    if (filterBox)
    {
        char empty[256] = {};
        filterBox->setData(empty); // clears the text and resets the cursor
        filterBox->drawView();
    }
    if (tree)
        tree->setFilter("");
}

void DocumentTreeWindow::clearFilterAndFocusTree() noexcept
{
    clearFilter();
    if (tree)
        tree->select();
}

DocumentTreeWindow::~DocumentTreeWindow()
{
    if (ptr)
        *ptr = nullptr;
}

TColorAttr DocumentTreeWindow::mapColor(uchar index) noexcept
{
    // Same resolution as BasicEditorWindow: map the window palette indices onto
    // the shared chrome scheme so the frame and scrollbars match the editors.
    if (index > 0 && index - 1 < turbo::WindowPaletteItemCount)
        return turbo::windowSchemeActive[index - 1];
    return errorAttr;
}

void DocumentTreeWindow::setBranchInfo(std::string_view info) noexcept
{
    if (info.empty())
        titleBuf = baseTitle;
    else
        titleBuf = baseTitle + " - " + std::string(info);
    frame->drawView();
}

const char *DocumentTreeWindow::getTitle(short)
{
    return titleBuf.empty() ? baseTitle.c_str() : titleBuf.c_str();
}

void DocumentTreeWindow::handleEvent(TEvent &ev)
{
    if (ev.what == evMouseDown)
    {
        TPoint m = makeLocal(ev.mouse.where);
        // The left border (between the corners) is a horizontal resize handle:
        // drag it left/right to widen/narrow the docked tree. onResizeTo re-lays
        // out the editors beside it on each step. The close box sits on the top
        // border (row 0), so starting at row >= 1 never steals it.
        if (m.x == 0 && m.y >= 1 && m.y < size.y - 1 && onResizeTo)
        {
            do {
                onResizeTo(ev.mouse.where.x);
            } while (mouseEvent(ev, evMouseMove | evMouseAuto));
            clearEvent(ev);
            return;
        }
    }
    TWindow::handleEvent(ev);
}

void DocumentTreeWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    // The tree rows dim when the window is inactive; repaint them on the change
    // (the frame already repaints itself).
    if ((aState & sfActive) && tree)
        tree->drawView();
}

void DocumentTreeWindow::close()
{
    message(TProgram::application, evCommand, cmToggleTree, 0);
}
