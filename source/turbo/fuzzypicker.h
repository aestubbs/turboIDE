#ifndef TURBO_FUZZYPICKER_H
#define TURBO_FUZZYPICKER_H

#define Uses_TWindow
#define Uses_TInputLine
#define Uses_TPalette
#include <tvision/tv.h>

#include <functional>
#include <string>
#include <vector>

// A reusable modal fuzzy picker: a single-line query box above a live-filtered,
// best-first result list, with an optional preview pane on the right. Used by
// both the Command Palette and "Goto Anything"; they differ only in the
// 'provider' that turns the current query into ranked rows (and whether a
// preview is shown). The picker owns all input/navigation/drawing; callers only
// supply the provider and react to the chosen payload.
//
// Colours: the picker overrides mapColor() to paint a dark, self-contained
// scheme for its frame, query box, list and preview, independent of the active
// editor theme (the same approach ListWindow uses).

struct FuzzyPickerInput;
struct FuzzyPickerList;
struct FuzzyPickerPreview;

class FuzzyPicker : public TWindow
{
public:

    struct Row
    {
        std::string text;   // primary text, fuzzy-matched and shown on the left
        std::string detail; // secondary text shown dimmed on the right (optional)
        int payload {-1};   // caller-defined id returned on selection
        bool dim {false};   // render dimmed (e.g. a command unavailable right now)
    };

    // Given the current query, return the rows to display, already ranked
    // best-first. Called on every keystroke.
    using Provider = std::function<std::vector<Row>(const std::string &)>;

    FuzzyPicker( const TRect &bounds, TStringView title,
                 Provider provider, bool withPreview ) noexcept;

    // Run modally. Returns the chosen row's payload, or -1 if cancelled.
    int run() noexcept;

    // Optional: invoked with the highlighted row's payload whenever the
    // selection changes, so callers can drive the preview (via setPreview).
    std::function<void(int payload)> onHighlight;

    // Point the preview pane at a file, centred on 'centreLine' (0-based; <0 =
    // top). No-op when the picker was created without a preview.
    void setPreview(const std::string &path, long centreLine) noexcept;

    // Re-run the provider with the current query (e.g. after async data such as
    // LSP symbols arrives and is folded into the provider's source).
    void reload() noexcept;

    TColorAttr mapColor(uchar index) override;
    void shutDown() override;

    // --- Internal hooks used by the child views (input/list/preview). ------
    void onQueryChanged() noexcept;     // re-run provider, rebuild rows
    void moveSelection(int delta) noexcept;
    void setSelection(int index) noexcept;
    void accept() noexcept;             // endModal(cmOK) with the current row
    void cancel() noexcept;             // endModal(cmCancel)
    int pageStep() const noexcept;

    std::vector<Row> rows;
    int selected {0};
    int top {0};                        // first visible row (scroll offset)

private:

    void emitHighlight() noexcept;

    Provider provider;
    int result {-1};
    std::string currentQuery;

    FuzzyPickerInput *input {nullptr};
    FuzzyPickerList *list {nullptr};
    FuzzyPickerPreview *preview {nullptr};
};

#endif // TURBO_FUZZYPICKER_H
