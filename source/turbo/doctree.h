#ifndef TURBO_DOCTREE_H
#define TURBO_DOCTREE_H

#define Uses_TWindow
#define Uses_TOutline
#include <tvision/tv.h>

#include <string>
#include <string_view>

struct EditorWindow;

struct DocumentTreeView : public TOutline {

    struct Node : public TNode {

        TNode **ptr;
        Node *parent;
        std::string path;       // Absolute path of this file or directory.
        bool isDir;
        EditorWindow *editor;   // Non-null iff this file is open in an editor.

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

    DocumentTreeWindow(const TRect &bounds, DocumentTreeWindow **ptr) noexcept;
    ~DocumentTreeWindow();

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
