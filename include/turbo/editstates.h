#ifndef TURBO_EDITSTATES_H
#define TURBO_EDITSTATES_H

#include <tvision/tv.h>
#include <turbo/styles.h>
#include <turbo/funcview.h>
#include <turbo/scintilla.h>

namespace turbo {

class LineNumbersWidth
{
    int minWidth;
    bool enabled {false};

    int calcWidth(TScintilla &scintilla);

public:

    LineNumbersWidth(int min) :
        minWidth(min)
    {
    }

    inline void setState(bool enable);
    inline void toggle();
    bool isEnabled() const { return enabled; }

    int update(TScintilla &scintilla);
};

inline void LineNumbersWidth::setState(bool enable)
{
    enabled = enable;
}

inline void LineNumbersWidth::toggle()
{
    enabled ^= true;
}

class WrapState
{
    bool enabled {false};
    bool confirmedOnce {false};

public:

    static bool defConfirmWrap(int width);

    // * 'confirmWrap' shall return whether line wrapping should be activated
    //   even if the document is quite large (>= 512 KiB).
    void setState( bool enable, TScintilla &scintilla,
                   TFuncView<bool(int width)> confirmWrap = defConfirmWrap );
    inline void toggle( TScintilla &scintilla,
                        TFuncView<bool(int width)> confirmWrap = defConfirmWrap );
    bool isEnabled() const { return enabled; }
};

inline void WrapState::toggle(TScintilla &scintilla, TFuncView<bool(int width)> confirmWrap)
{
    setState(!enabled, scintilla, confirmWrap);
}

class AutoIndent
{
    bool enabled {true};

public:

    inline void setState(bool enable);
    inline void toggle();
    bool isEnabled() const { return enabled; }

    void applyToCurrentLine(TScintilla &scintilla);
};

inline void AutoIndent::setState(bool enable)
{
    enabled = enable;
}

inline void AutoIndent::toggle()
{
    enabled ^= true;
}

enum SearchDirection : uint8_t
{
    sdForward,
    sdForwardIncremental,
    sdBackwards,
};

enum SearchMode : uint8_t
{
    smPlainText,
    smWholeWords,
    smRegularExpression,
};

enum SearchFlags : uint8_t
{
    sfCaseSensitive = 0x01,
};

struct SearchSettings
{
    SearchMode mode {smPlainText};
    uint8_t flags {0};
};

void search(TScintilla &scintilla, TStringView text, SearchDirection direction, SearchSettings settings);

enum ReplaceMethod : uint8_t
{
    rmReplaceOne,
    rmReplaceAll,
};

void replace(TScintilla &scintilla, TStringView text, TStringView withText, ReplaceMethod method, SearchSettings settings);
void clearIndicator(TScintilla &scintilla, Indicator indicator);

// Symbol-marker numbers for the bookmark (margin 1) and change-history
// (margin 2) markers, set up in Editor::setUpExtraMargins. Both margins are
// hidden (width 0) by default, and Scintilla only excludes a margin's markers
// from the in-line background mask when that margin is visible (see
// ViewStyle::CalculateMarginWidthAndMask). With the margin hidden the marker
// behaves as an in-line *background* marker, so its background paints the whole
// marked line. applyTheming therefore pins these markers' backgrounds to the
// normal text background, keeping a marked (modified/bookmarked) line from
// rendering in the terminal-default colour. Shared here so applyTheming and the
// margin setup agree on the numbers.
constexpr int markBookmark = 1;
constexpr int markChanged = 2;

// Updates 'scintilla' so that it makes use of the current state of
// 'lexer' and 'scheme'. If 'scheme' is null, 'schemeDefault' is used instead.
void applyTheming(const LexerSettings *lexer, const ColorScheme *scheme, TScintilla &scintilla);

// Highlights matching braces if there are any.
void updateBraces(const ColorScheme *scheme, TScintilla &scintilla);

// Toggles comment in selected text.
void toggleComment(TScintilla &scintilla, const Language *language);

void stripTrailingSpaces(TScintilla &scintilla, const Language *language);
void ensureNewlineAtEnd(TScintilla &scintilla);

} // namespace turbo

#endif
