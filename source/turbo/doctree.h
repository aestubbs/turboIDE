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

    using TOutline::TOutline;

    // Opening a file happens here (Enter / double-click), not on mere
    // highlight movement, so the arrow keys only move the selection.
    void selected(int i) noexcept override;
    // Custom draw so that files open in an editor are shown in bold.
    void draw() override;

    // Build the tree by recursively scanning 'rootPath'.
    void scanDirectory(std::string_view rootPath) noexcept;

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
