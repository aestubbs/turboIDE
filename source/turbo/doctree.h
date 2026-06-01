#ifndef TURBO_DOCTREE_H
#define TURBO_DOCTREE_H

#define Uses_TWindow
#define Uses_TOutline
#include <tvision/tv.h>

#include <string>
#include <string_view>
#include <unordered_map>

struct EditorWindow;
struct GitFileStatus;

struct DocumentTreeView : public TOutline {

    struct Node : public TNode {

        TNode **ptr;
        Node *parent;
        std::string path;       // Absolute path of this file or directory.
        bool isDir;
        EditorWindow *editor;   // Non-null iff this file is open in an editor.
        // Git status badge char: 0 = clean, else 'M' 'A' 'D' 'R' '?' 'U' for a
        // file, or '.' for a directory that contains changes.
        char gitStatus {0};

        Node(Node *parent, std::string_view path, bool isDir) noexcept;
        void setEditor(EditorWindow *w) noexcept;
        void refreshText() noexcept;
        void remove() noexcept;
        void dispose() noexcept;

    };

    DocumentTreeView(const TRect &bounds, TScrollBar *hsb, TScrollBar *vsb,
                     TNode *root) noexcept;

    // Opening a file happens here (Enter / double-click), not on mere
    // highlight movement, so the arrow keys only move the selection.
    void selected(int i) noexcept override;
    // Intercept right-click (context menu) and left double-click (open/toggle)
    // before the base outline handling; everything else falls through.
    void handleEvent(TEvent &ev) override;
    // Custom draw so that files open in an editor are shown in bold.
    void draw() override;
    // Root entries (level 0) get a compact 1-char marker instead of the 3-char
    // connector graph: the tree lines convey no hierarchy at the top level and
    // only waste columns in the narrow pane. Deeper levels keep the full graph.
    char *getGraph(int level, long lines, ushort flags) override;

    // Open a file in the editor (or focus it if already open), or toggle a
    // directory's expanded state. Shared by Enter, double-click and the menu.
    void openOrToggle(Node *node) noexcept;
    // Pop up the file/folder context menu for the node on display row 'row',
    // anchored at the absolute screen position 'where', and run the chosen action.
    void showContextMenu(int row, TPoint where) noexcept;

    // Build the tree by recursively scanning 'rootPath'.
    void scanDirectory(std::string_view rootPath) noexcept;

    // Whether to include hidden entries (dotfiles/dot-dirs) when scanning.
    // Changing it rebuilds the tree from the current root, preserving the links
    // to any open editors. No-op if unchanged or before scanDirectory().
    void setShowHidden(bool show) noexcept;
    bool showsHidden() const noexcept { return showHidden; }

    // Associate/dissociate an open editor window with its file node (by path).
    void linkEditor(EditorWindow *w) noexcept;
    void unlinkEditor(EditorWindow *w) noexcept;
    void focusEditor(EditorWindow *w) noexcept;
    void focusNext() noexcept;
    void focusPrev() noexcept;
    // Expand ancestors of the editor's node and scroll it into view.
    void revealEditor(EditorWindow *w) noexcept;

    // Apply git per-file status: clear all badges, set the given files, roll
    // changed state up to ancestor directories, then redraw.
    void applyGitStatus(const std::unordered_map<std::string, GitFileStatus> &files) noexcept;

    // Incremental structural updates (used by the filesystem watcher) so the
    // tree stays live without a full rescan, which would lose expand/scroll
    // state. No-ops when the change is outside the scanned tree or hidden.
    void addNode(std::string_view path, bool isDir) noexcept;
    void removeNode(std::string_view path) noexcept;
    void refreshNode(std::string_view path) noexcept;
    Node *findDir(std::string_view path) noexcept;

    std::string rootPath;   // absolute path scanned by scanDirectory()
    bool showHidden {false}; // include dotfiles/dot-dirs when scanning

    Node *findByEditor(const EditorWindow *w, int *pos=nullptr) noexcept;
    Node *findByPath(std::string_view path) noexcept;
    template <class Func>
    Node *firstThat(Func &&func) noexcept;

};

template <class Func>
inline DocumentTreeView::Node *DocumentTreeView::firstThat(Func &&func) noexcept
{
    auto applyCallback =
    [] ( TOutlineViewer *, TNode *node, int, int position,
         long, ushort, void *arg )
    {
        return (*(Func *) arg)((Node *) node, position);
    };

    return (Node *) TOutlineViewer::firstThat(applyCallback, &func);
}

struct DocumentTreeWindow : public TWindow {

    DocumentTreeView *tree;
    DocumentTreeWindow **ptr;
    std::string baseTitle {"Files"};
    std::string titleBuf;   // backing store for getTitle()

    DocumentTreeWindow(const TRect &bounds, DocumentTreeWindow **ptr) noexcept;
    ~DocumentTreeWindow();

    // Set the branch/ahead-behind shown in the window title (empty = none).
    void setBranchInfo(std::string_view info) noexcept;
    const char *getTitle(short) override;

    void close() override;

};

template <class Func>
inline TNode* findInList(TNode **list, Func &&test)
{
    auto *node = *list;
    while (node) {
        auto *next = node->next;
        if (test((DocumentTreeView::Node *) node))
            return node;
        node = next;
    }
    return nullptr;
}

#endif
