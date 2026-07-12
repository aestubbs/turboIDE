#ifndef TURBO_OUTPUTWINDOW_H
#define TURBO_OUTPUTWINDOW_H

#define Uses_TWindow
#define Uses_TListViewer
#define Uses_TPalette
#include <tvision/tv.h>

#include <functional>
#include <string>
#include <vector>

// A single line in the output pane. 'file'/'line' are filled in (Phase 2) when
// the line parses as a compiler error/warning referencing a source location, so
// the line becomes clickable.
enum OutputKind { okNormal, okError, okWarning, okInfo };

struct OutputLine
{
    std::string text;
    OutputKind kind {okNormal};
    std::string file;   // absolute path for click-through ("" = none)
    long line {-1};     // 0-based target line (-1 = none)
};

// The output pane keeps one buffer per tab, identified by a stable id. BUILD and
// GIT are the two built-in tabs (BUILD is wiped at the start of each build/run;
// GIT is a continuous log of every git command). The app adds one further tab
// per configured tool process; those get ids allocated above otGit. The tab bar
// at the foot of the window switches between whatever tabs currently exist.
enum OutputTab { otBuild = 0, otGit = 1 };

struct OutputTabBar;

// Scrolling list of output lines, backed by one buffer per tab (only the active
// tab is drawn). Auto-follows the tail of its buffer unless the user scrolls up.
// Colours lines by kind; error lines with a resolved file:line are clickable.
struct OutputView : public TListViewer
{
    struct Buffer
    {
        int id {0};            // stable tab id (otBuild / otGit / a tool tab id)
        std::string title;     // tab label shown in the bar (no brackets)
        std::vector<OutputLine> lines;
        bool followTail {true};
        int savedTop {0};      // topItem to restore when this tab is reactivated
        int savedFocused {0};  // focused item to restore
    };
    std::vector<Buffer> buffers; // [0]=BUILD, [1]=GIT, then one per tool tab
    int activeId {otBuild};
    OutputTabBar *tabBar {nullptr}; // redrawn on tab change (set by OutputWindow)

    // Called when a line with a resolved (file, line) is activated (Enter or
    // double-click). The app wires this to openOrFocus, so error lines jump to
    // the source. Kept as a callback to avoid coupling this view to TurboApp.
    std::function<void(const std::string &file, long line)> onActivate;

    OutputView(const TRect &bounds, TScrollBar *vScrollBar) noexcept;

    // Tab lookup / management, all keyed by stable id.
    int indexOfId(int id) const noexcept;               // -1 if absent
    Buffer *bufferById(int id) noexcept;                // nullptr if absent
    void ensureTab(int id, const std::string &title) noexcept; // add or retitle
    void removeTab(int id) noexcept;                    // drop a tool tab
    std::vector<OutputLine> &active() noexcept
    {
        int i = indexOfId(activeId);
        return buffers[i < 0 ? 0 : i].lines;
    }

    void addLine(int id, OutputLine ln) noexcept;  // append to a tab's buffer
    void clear(int id) noexcept;                   // wipe a tab's buffer
    void setActiveTab(int id) noexcept;            // switch tab, preserve scroll
    void showTab(int id) noexcept;                 // switch tab + follow its tail
    void activate(int idx) noexcept; // jump to active()[idx]'s file:line if any

    void draw() override;
    void handleEvent(TEvent &ev) override;
};

// One-row tab bar docked at the foot of the output window: draws each tab's
// bracketed label (the active one highlighted) and switches OutputView's tab on
// a click. Hit-test ranges are recomputed each draw to match the current tabs.
struct OutputTabBar : public TView
{
    OutputView *view {nullptr};
    std::vector<int> tabX0;    // per-tab hit-test ranges (parallel to view->buffers)
    std::vector<int> tabX1;

    OutputTabBar(const TRect &bounds, OutputView *view) noexcept;

    void draw() override;
    void handleEvent(TEvent &ev) override;
};

// Classify a raw build-output line: detect compiler/error formats
// (gcc/clang 'file:line:col: error:', MSVC 'file(line): error', generic
// 'file:line:', Python 'File "f", line N'), set its colour kind, and -- when the
// referenced file exists under 'root' -- resolve an absolute file + 0-based line
// so the line becomes clickable. Falls back to plain text.
OutputLine parseBuildLine(const std::string &raw, const std::string &root) noexcept;

// A bordered, pre-sized window docked across the bottom of the editor area --
// laid out and themed exactly like the file-tree window (DocumentTreeWindow):
// it resolves its chrome colours through the shared window scheme so the frame
// and scrollbar match the editors.
struct OutputWindow : public TWindow
{
    OutputView *view {nullptr};
    OutputTabBar *tabBar {nullptr};
    OutputWindow **ptr;
    // Invoked while the user drags the pane's top border, with the dragged
    // border's screen row. The app turns that into a new pane height and
    // re-lays out the editors above. Lets the docked pane stay anchored.
    std::function<void(int borderScreenY)> onResizeTo;

    OutputWindow(const TRect &bounds, OutputWindow **ptr) noexcept;

    TColorAttr mapColor(uchar index) noexcept override;
    void sizeLimits(TPoint &min, TPoint &max) noexcept override;
    void setState(ushort aState, Boolean enable) override; // repaint on activate
    void handleEvent(TEvent &ev) override;
    void close() override;
    void shutDown() override;
};

#endif // TURBO_OUTPUTWINDOW_H
