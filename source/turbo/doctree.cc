#include "doctree.h"
#include "editwindow.h"
#include "app.h"
#include "gitclient.h"
#include <utility>
#include <filesystem>
#include <turbo/tpath.h>

#define Uses_TProgram
#define Uses_TDrawBuffer
#define Uses_TKeys
#include <tvision/tv.h>

using Node = DocumentTreeView::Node;

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

static void scanInto(Node *parent, TNode **list, const std::string &dirPath, int depth) noexcept
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
        // Skip hidden entries (dotfiles and dot-directories such as .git).
        if (name.empty() || name[0] == '.')
            continue;
        std::error_code ec2;
        bool isDir = entry.is_directory(ec2);
        std::string full = entry.path().string();
        auto *node = new Node(parent, full, isDir);
        if (isDir)
            // Expand only the top level by default to keep the tree manageable.
            node->expanded = (depth == 0) ? True : False;
        putNode(list, node);
        if (isDir)
            scanInto(node, &node->childList, full, depth + 1);
    }
}

void DocumentTreeView::scanDirectory(std::string_view rootPath) noexcept
{
    scanInto(nullptr, &root, std::string {rootPath}, 0);
    update();
    drawView();
}

void DocumentTreeView::selected(int i) noexcept
{
    // Triggered by Enter or a double-click (not by moving the highlight).
    auto *node = (Node *) getNode(i);
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

namespace {

struct DrawCtx { TDrawBuffer *b; int last; };

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
            color = v->getColor(0x0202);
        else if (v->isSelected(position))
            color = v->getColor(0x0303);
        else
            color = v->getColor(0x0401);
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
                char fg;
                switch (gs)
                {
                    case 'M': case 'R': fg = '\x0E'; break; // yellow
                    case 'A': case '?': fg = '\x0A'; break; // green
                    case 'D':           fg = '\x0C'; break; // red
                    case 'U':           fg = '\x0D'; break; // magenta
                    case '.':           fg = '\x06'; break; // dim (dir)
                    default:            fg = '\x07'; break;
                }
                setFore(c, TColorDesired(fg));
            }
            dBuf.moveStr(max(0, x), text, c, (ushort) -1U, max(0, -x));
        }
        v->writeLine(0, position - v->delta.y, v->size.x, 1, dBuf);
        ctx->last = position;
    }
    return False;
}

} // namespace

void DocumentTreeView::draw()
{
    TDrawBuffer dBuf;
    DrawCtx ctx {&dBuf, -1};
    TOutlineViewer::firstThat(drawNode, &ctx);
    TAttrPair nrmColor = getColor(0x0401);
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
    if (auto *node = findByEditor(w))
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
    return firstThat(
    [path] (Node *node, int)
    {
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
