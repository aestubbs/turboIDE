#include "doctree.h"
#include "editwindow.h"
#include "app.h"
#include "gitclient.h"
#include <utility>
#include <vector>
#include <filesystem>
#include <turbo/tpath.h>

#define Uses_TProgram
#define Uses_TDrawBuffer
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TMenuItem
#include <tvision/tv.h>

#include <turbo/basicwindow.h> // shared editor-window chrome scheme

using Node = DocumentTreeView::Node;

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
        // unless the user has opted to show them. The .git directory in
        // particular stays hidden either way (it is noise, not source).
        if (name.empty())
            continue;
        if (name[0] == '.' && (!showHidden || name == ".git"))
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
    update();
    drawView();
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
    disposeNode(root);
    root = nullptr;
    foc = 0;
    scanInto(nullptr, &root, rootPath, 0, showHidden);
    for (auto *w : openEditors)
        linkEditor(w);
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
    // Honour the show-hidden setting (mirrors scanInto); .git stays hidden.
    if (base[0] == '.' && (!showHidden || base == ".git"))
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
    auto *sep    = &newLine();
    auto *add    = new TMenuItem("Git ~A~dd", cmTreeGitAdd, kbNoKey, hcNoContext);
    items->append(rename);
    rename->append(newf);
    newf->append(sep);
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
        {
            // Create inside a folder; for a file, create alongside it.
            std::string dir = path;
            if (!isDir)
            {
                TStringView d = TPath::dirname(path);
                dir.assign(d.data(), d.size());
            }
            if (app)
                app->treeCreateFile(dir);
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
TAttrPair treeColors(TreeRole role) noexcept
{
    using namespace turbo;
    TColorDesired bg = ::getBack(windowSchemeActive[wndFrameActive]); // unified window blue
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
        TAttrPair color;
        if (position == v->foc && (v->state & sfFocused))
            color = treeColors(TreeRole::Focused);
        else if (v->isSelected(position))
            color = treeColors(TreeRole::Selected);
        else
            color = treeColors(TreeRole::Normal);
        dBuf.moveChar(0, ' ', color, v->size.x);
        int x;
        {
            TStringView graph = v->getGraph(level, lines, flags);
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
        // One ASCII column, aligned across root entries:
        //   '+' collapsed folder, '-' expanded folder, ' ' file/empty dir.
        char *g = new char[2];
        if (!(flags & ovExpanded))
            g[0] = '+';                 // has children, currently collapsed
        else if (flags & ovChildren)
            g[0] = '-';                 // has children, currently expanded
        else
            g[0] = ' ';                 // leaf
        g[1] = '\0';
        return g;
    }
    // Deeper levels, built as tight as possible: one column per ancestor level
    // (a vertical 'lines' bar or a gap) followed immediately by this item's
    // connector and then the name -- no filler and no trailing marker column.
    // The connector sits directly under the first letter of the parent name.
    // A collapsed folder shows '+' in the connector slot (the classic
    // expandable-node marker), so the affordance survives without any extra
    // width; files and expanded folders show the usual branch glyph.
    char *g = new char[level + 2]; // 'level' ancestor cols + connector + NUL
    char *p = g;
    long l = lines;
    for (int i = 0; i < level; ++i, l >>= 1)
        *p++ = (l & 1) ? '\xB3' : ' ';          // │ (continues) or gap
    if (!(flags & ovExpanded))
        *p++ = '+';                             // collapsed folder
    else
        *p++ = (flags & ovLast) ? '\xC0' : '\xC3'; // └ (last) or ├
    *p = '\0';
    return g;
}

void DocumentTreeView::draw()
{
    TDrawBuffer dBuf;
    DrawCtx ctx {&dBuf, -1};
    TOutlineViewer::firstThat(drawNode, &ctx);
    TAttrPair nrmColor = treeColors(TreeRole::Normal);
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
        node->refreshText();
        // Roll the "contains changes" marker up to ancestor directories.
        for (Node *p = node->parent; p; p = p->parent)
            if (p->gitStatus == 0)
            {
                p->gitStatus = '.';
                p->refreshText();
            }
    }
    update();
    drawView();
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
    tree = new DocumentTreeView(getExtent().grow(-1, -1), hsb, vsb, nullptr);
    tree->growMode = gfGrowHiX | gfGrowHiY;
    insert(tree);
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

void DocumentTreeWindow::close()
{
    message(TProgram::application, evCommand, cmToggleTree, 0);
}
