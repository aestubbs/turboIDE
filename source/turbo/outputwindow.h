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

// Scrolling list of output lines. Auto-follows the tail unless the user scrolls
// up. Colours lines by kind. (Click-through on error lines is wired in Phase 2.)
struct OutputView : public TListViewer
{
    std::vector<OutputLine> lines;
    bool followTail {true};
    // Called when a line with a resolved (file, line) is activated (Enter or
    // double-click). The app wires this to openOrFocus, so error lines jump to
    // the source. Kept as a callback to avoid coupling this view to TurboApp.
    std::function<void(const std::string &file, long line)> onActivate;

    OutputView(const TRect &bounds, TScrollBar *vScrollBar) noexcept;

    void addLine(OutputLine ln) noexcept;
    void clear() noexcept;
    void activate(int idx) noexcept; // jump to lines[idx]'s file:line if it has one

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
    OutputWindow **ptr;

    OutputWindow(const TRect &bounds, OutputWindow **ptr) noexcept;

    TColorAttr mapColor(uchar index) noexcept override;
    void close() override;
    void shutDown() override;
};

#endif // TURBO_OUTPUTWINDOW_H
