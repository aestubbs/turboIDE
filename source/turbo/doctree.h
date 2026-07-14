#ifndef TURBO_DOCTREE_H
#define TURBO_DOCTREE_H

#define Uses_TWindow
#define Uses_TFrame
#define Uses_TOutline
#define Uses_TInputLine
#include <tvision/tv.h>

#include "treeicons.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct EditorWindow;
struct GitFileStatus;
struct DocumentTreeWindow;

// Git-status filter shown in the tree's filter area (a dropdown). 'All' clears
// it; the rest restrict the tree to files in that state.
enum TreeGitFilter { tgfAll = 0, tgfModified, tgfStaged, tgfUntracked,
                     tgfConflicted, tgfCount };

struct DocumentTreeView : public TOutline {

    struct Node : public TNode {

        TNode **ptr;
        Node *parent;
        std::string path;       // Absolute path of this file or directory.
        // Whether this node is a container (it has a child list and expands).
        // True for Project, Dir, Skill and both home kinds.
        bool isDir;
        EditorWindow *editor;   // Non-null iff this file is open in an editor.
        NodeKind kind {NodeKind::File};
        // Git status badge char: 0 = clean, else 'M' 'A' 'D' 'R' '?' 'U' for a
        // file, or '.' for a directory that contains changes.
        char gitStatus {0};
        bool gitStaged {false}; // the file's change is staged (for the Staged filter)
        // Whether this node is shown under the current filter (see setFilter).
        // Only consulted while a filter is active; ignored otherwise.
        bool visible {true};
        // The real path behind a node whose own 'path' is not the thing it acts on:
        //   LuaHome / SkillsHome -> the real directory the home stands for (its own
        //                           'path' is just a display label)
        //   Skill                -> the SKILL.md inside the skill folder
        // Empty for every other kind.
        std::string primaryPath;

        Node(Node *parent, std::string_view path, bool isDir) noexcept;
        void setEditor(EditorWindow *w) noexcept;
        void refreshText() noexcept;
        void remove() noexcept;
        void dispose() noexcept;

        // The file this node opens on Enter/double-click, or "" when there is
        // nothing to open (a pure container, which toggles instead). A skill is
        // both: a folder that expands, and a node that opens its SKILL.md.
        std::string openPath() const noexcept
        {
            if (kind == NodeKind::Skill) return primaryPath;
            if (kind == NodeKind::File)  return path;
            return {};
        }
        // The directory that "New File..." / "New Folder..." create into.
        std::string targetDir() const noexcept;

    };

    // A synthetic top-level "Lua scripts" home: a friendly label, the real
    // directory it represents, and the .lua files to list beneath it.
    struct LuaSection {
        std::string label;
        std::string dir;
        std::vector<std::string> scripts;
    };

    // A synthetic top-level "Skills" home: a friendly label and the real skills
    // directory it stands for. Unlike a Lua home, its contents are not listed
    // here: they are scanned from 'dir' like any other folder, so a skill's
    // SKILL.md, references/ and scripts/ are all reachable in the tree.
    struct SkillSection {
        std::string label;
        std::string dir;
    };

    DocumentTreeView(const TRect &bounds, TScrollBar *hsb, TScrollBar *vsb,
                     TNode *root) noexcept;

    // Opening a file happens here (Enter / double-click), not on mere
    // highlight movement, so the arrow keys only move the selection.
    void selected(int i) noexcept override;
    // Intercept right-click (context menu) and left double-click (open/toggle)
    // before the base outline handling; everything else falls through.
    void handleEvent(TEvent &ev) override;
    // Custom draw: the connector graph (dim), then the node's icon in its own
    // colour, then the name. Files open in an editor are shown in bold.
    void draw() override;
    // The base viewer only ever MEASURES this string -- for the horizontal-scroll
    // limit and for the click-to-toggle hit test. All drawing goes through
    // drawNode(), which has the node in hand and so can pick glyphs this cannot
    // know about (an empty folder still being a folder, the tree's first row).
    // Both share treeGraph(), so the width they agree on is exact.
    char *getGraph(int level, long lines, ushort flags) override;

    // Outline iteration overrides. While a filter is active these present only
    // the matching subset of nodes to the base viewer (which drives all layout,
    // scrolling and drawing); with no filter they defer to the base behaviour.
    TNode *getRoot() override;
    TNode *getChild(TNode *node, int i) override;
    TNode *getNext(TNode *node) override;
    int getNumChildren(TNode *node) override;
    Boolean hasChildren(TNode *node) override;
    Boolean isExpanded(TNode *node) override;

    // Open a file in the editor (or focus it if already open), or -- for a node
    // with nothing to open -- toggle it. Shared by Enter, double-click and the
    // menu. A skill opens its SKILL.md rather than expanding, so the arrow keys
    // must use toggleExpand() instead of this.
    void openOrToggle(Node *node) noexcept;
    // Expand/collapse a container, without ever opening anything.
    void toggleExpand(Node *node) noexcept;
    // Pop up the file/folder context menu for the node on display row 'row',
    // anchored at the absolute screen position 'where', and run the chosen action.
    void showContextMenu(int row, TPoint where) noexcept;

    // Build the tree by recursively scanning 'rootPath'.
    void scanDirectory(std::string_view rootPath) noexcept;

    // Empty the tree: dispose every scanned file/directory node and forget the
    // root path, leaving only the synthetic Lua scripts section (re-applied per
    // the current showLuaScripts setting). Used when a project is closed, so the
    // tree shows nothing but the user's global scripts. Open editors keep their
    // windows; they simply lose their (now-gone) tree links until a project that
    // contains them is opened again.
    void clear() noexcept;

    // --- Lua scripts homes ------------------------------------------------
    // Show (or refresh) the synthetic top-level Lua-script "homes" (one group per
    // home: project / global). Their scripts live outside the project or in a dir
    // excluded from the normal scan, so they are injected as group nodes whose
    // children carry the scripts' real paths (so they open and link to editors like
    // any file). Each group is shown even when empty, so it acts as a clear home to
    // drop scripts into. 'show' == false removes them. The sections are stored and
    // re-applied after a tree rebuild (e.g. toggling hidden files).
    void setLuaScripts(bool show, std::vector<LuaSection> sections) noexcept;

    // Inject synthetic top-level "Skills" homes, laid out exactly like the Lua
    // homes (project vs global as separate labelled groups). Stored and
    // re-applied after a tree rebuild.
    void setSkills(bool show, std::vector<SkillSection> sections) noexcept;

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

    // --- Name filter ------------------------------------------------------
    // Filter the displayed tree to nodes whose name matches 'query' (a
    // case-insensitive partial/substring match). A query of three characters or
    // fewer clears the filter and shows the whole tree. While filtering, folders
    // are kept if they contain a match (so the path stays visible) and are shown
    // expanded; a folder whose own name matches reveals its entire contents. The
    // underlying node tree is never modified -- only what the outline iterates.
    void setFilter(std::string_view query) noexcept;
    bool filtering() const noexcept { return !filter.empty() || gitFilter != tgfAll; }

    // --- Git-status filter ------------------------------------------------
    // Restrict the tree to files in a given git state (combined with the name
    // filter). tgfAll clears it. Folders stay visible iff they contain a match.
    int gitFilter {tgfAll};
    void setGitFilter(int gf) noexcept;

    // The owning window wires this up so the tree can drive the search box
    // (Ctrl-F to focus it, Esc to clear it).
    DocumentTreeWindow *win {nullptr};

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
    // The node that owns 'dirPath' as its child list: a real directory node, or --
    // for the real directory behind a synthetic Lua/Skills home -- the home node
    // itself, whose own 'path' is a label and so is invisible to findDir().
    Node *findContainer(std::string_view dirPath) noexcept;

    // Append the absolute path of every file (not directory) in the whole tree,
    // regardless of expand/collapse state. Used by the "Goto Anything" picker as
    // its file source. Order follows the tree's scan order.
    void collectFilePaths(std::vector<std::string> &out) noexcept;

    std::string rootPath;   // absolute path scanned by scanDirectory()
    bool showHidden {false}; // include dotfiles/dot-dirs when scanning

    // The project's files are nested under this wrapper node (named for the
    // project folder), so the tree has a clear top-level parent for the project,
    // sitting beside the Lua-script homes. Null when no project is open.
    Node *projectNode {nullptr};
    // Link the top-level nodes into 'root' in fixed display order: the project
    // folder first, then the Lua-script homes (in luaSections order). Each may
    // be absent. Called after the members are (re)built.
    void assembleRoot() noexcept;
    // (Re)build the project wrapper node, scanning the project's files into it.
    void rebuildProjectNode() noexcept;
    void disposeProjectNode() noexcept; // free the project subtree + the wrapper

    // Synthetic Lua-script homes (see setLuaScripts). The group nodes are owned
    // by the tree's node lists; they are freed and rebuilt from luaSections on
    // any tree rebuild (which frees all nodes).
    bool showLuaScripts {true};
    std::vector<LuaSection> luaSections;
    std::vector<Node *> luaGroups; // live group nodes, parallel to luaSections
    void reinjectLuaNodes() noexcept; // rebuild the groups from luaSections

    // Synthetic Skills homes (see setSkills): same machinery as the Lua homes,
    // freed and rebuilt from skillSections on any tree rebuild.
    bool showSkills {true};
    std::vector<SkillSection> skillSections;
    std::vector<Node *> skillGroups; // live group nodes, parallel to skillSections
    void reinjectSkillNodes() noexcept; // rebuild the groups from skillSections
    // Free a synthetic home and its ENTIRE subtree (a skills home now holds real
    // folders, so freeing only its direct children would leak them).
    void disposeGroup(Node *&group) noexcept;
    std::string filter;     // active filter query (lowercased; "" = no filter)
    // Recompute Node::visible for the whole tree from 'filter'. A file is
    // visible iff its name matches; a folder is visible iff its name matches
    // (in which case its whole subtree is revealed) or any descendant matches.
    void recomputeVisibility() noexcept;

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
    TInputLine *filterBox {nullptr}; // single-line name filter at the top
    DocumentTreeWindow **ptr;
    std::string baseTitle {"Files"};
    std::string titleBuf;   // backing store for getTitle()
    // Invoked while the user drags the tree's (editor-facing) left border, with
    // the dragged border's screen column. The app turns that into a new tree
    // width and re-lays out the editors beside it. Keeps the tree docked.
    std::function<void(int borderScreenX)> onResizeTo;

    DocumentTreeWindow(const TRect &bounds, DocumentTreeWindow **ptr) noexcept;
    ~DocumentTreeWindow();

    // Custom frame (draws the filter-area divider so it joins the side borders).
    static TFrame *initFrame(TRect bounds);

    // Search-box coordination (called from the box and the tree).
    void focusFilter() noexcept;          // put the cursor in the search box
    void applyFilterFromBox() noexcept;   // push the box text into the tree filter
    void clearFilter() noexcept;          // empty the box and the filter
    void clearFilterAndFocusTree() noexcept;

    // Set the branch/ahead-behind shown in the window title (empty = none).
    void setBranchInfo(std::string_view info) noexcept;
    const char *getTitle(short) override;

    // Resolve all colours (frame, scrollbars) through the shared editor-window
    // chrome scheme, so the file tree matches the editor windows instead of
    // using Turbo Vision's default bright-blue window palette.
    TColorAttr mapColor(uchar index) noexcept override;

    void handleEvent(TEvent &ev) override;
    void setState(ushort aState, Boolean enable) override; // repaint tree on activate
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
