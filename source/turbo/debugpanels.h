#ifndef TURBO_DEBUGPANELS_H
#define TURBO_DEBUGPANELS_H

#define Uses_TListViewer
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include <functional>
#include <string>
#include <vector>

// A frame shown in the Call Stack panel. 'file'/'line' (0-based) locate the
// frame's source for jump-to; 'line' < 0 means no resolvable location.
struct StackFrameItem
{
    std::string label; // e.g. "main    hello.py:3"
    std::string file;
    long line {-1};
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

#endif // TURBO_DEBUGPANELS_H
