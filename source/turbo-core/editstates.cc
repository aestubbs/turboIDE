#define Uses_MsgBox
#include <tvision/tv.h>

#include <turbo/editstates.h>
#include <turbo/scintilla.h>
#include <turbo/scintilla/internals.h>
#include <Lexilla.h> // Lexilla::CreateLexer (lexers are external in Scintilla 5.x)

namespace turbo {

/////////////////////////////////////////////////////////////////////////
// LineNumbersWidth

int LineNumbersWidth::update(TScintilla &scintilla)
{
    int newWidth = enabled ? calcWidth(scintilla) : 0;
    call(scintilla, SCI_SETMARGINWIDTHN, 0, newWidth); // Does nothing if width hasn't changed.
    return newWidth;
}

int LineNumbersWidth::calcWidth(TScintilla &scintilla)
{
    // Count the digits needed for the last line number, then add one cell of
    // left padding so the number isn't flush against the frame. Sized to the
    // file's line count, which is stable, so the margin (and the border after
    // it) doesn't shift around as you scroll.
    int digits = 1;
    size_t lines = call(scintilla, SCI_GETLINECOUNT, 0U, 0U);
    while (lines /= 10)
        ++digits;
    int width = digits + 1; // one space of left padding
    if (width < minWidth)
        width = minWidth;
    return width;
}

/////////////////////////////////////////////////////////////////////////
// WrapState

void WrapState::setState(bool enable, TScintilla &scintilla, TFuncView<bool(int)> confirmWrap)
{
    if (!enable)
    {
        auto line = call(scintilla, SCI_GETFIRSTVISIBLELINE, 0U, 0U);
        call(scintilla, SCI_SETWRAPMODE, SC_WRAP_NONE, 0U);
        call(scintilla, SCI_SETFIRSTVISIBLELINE, line, 0U);
        enabled = false;
    }
    else
    {
        bool proceed = true;
        int size = call(scintilla, SCI_GETLENGTH, 0U, 0U);
        bool documentBig = size >= (1 << 19);
        if (documentBig && !confirmedOnce)
        {
            int width = call(scintilla, SCI_GETSCROLLWIDTH, 0U, 0U);
            proceed = confirmedOnce = confirmWrap(width);
        }
        if (proceed)
        {
            call(scintilla, SCI_SETWRAPMODE, SC_WRAP_WORD, 0U);
            enabled = true;
        }
    }
}

bool WrapState::defConfirmWrap(int width)
{
    return cmYes == messageBox( mfInformation | mfYesButton | mfNoButton,
                                "This document is quite large and the longest of its lines is at least %d characters long.\nAre you sure you want to enable line wrapping?", width );
}

/////////////////////////////////////////////////////////////////////////
// AutoIndent

void AutoIndent::applyToCurrentLine(TScintilla &scintilla)
{
    if (enabled)
    {
        auto pos = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
        auto line = call(scintilla, SCI_LINEFROMPOSITION, pos, 0U);
        if (line > 0)
        {
            auto indentation = call(scintilla, SCI_GETLINEINDENTATION, line - 1, 0U);
            if (indentation > 0)
            {
                call(scintilla, SCI_SETLINEINDENTATION, line, indentation);
                call(scintilla, SCI_VCHOME, 0U, 0U);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////
// Search

static void initSearchFlags(TScintilla &scintilla, SearchSettings settings)
{
    int searchFlags =
          (-(settings.mode == smWholeWords) & SCFIND_WHOLEWORD)
        | (-(settings.mode == smRegularExpression) & (SCFIND_REGEXP | SCFIND_CXX11REGEX))
        | (-!!(settings.flags & sfCaseSensitive) & SCFIND_MATCHCASE)
        ;
    call(scintilla, SCI_SETSEARCHFLAGS, searchFlags, 0U);
}

static void initSearchTarget(TScintilla &scintilla, SearchDirection direction)
{
    Sci::Position selStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
    Sci::Position selEnd = call(scintilla, SCI_GETSELECTIONEND, 0U, 0U);
    Sci::Position targetStart, targetEnd;
    if (direction == sdForward)
        targetStart = selEnd;
    else
        targetStart = selStart;
    if (direction == sdBackwards)
        targetEnd = 0;
    else
        targetEnd = call(scintilla, SCI_GETTEXTLENGTH, 0U, 0U);
    call(scintilla, SCI_SETTARGETRANGE, targetStart, targetEnd);
}

static void wrapSearchTarget(TScintilla &scintilla)
{
    sptr_t docEnd = call(scintilla, SCI_GETTEXTLENGTH, 0U, 0U);
    sptr_t targetStart = call(scintilla, SCI_GETTARGETSTART, 0U, 0U);
    sptr_t targetEnd = call(scintilla, SCI_GETTARGETEND, 0U, 0U);
    call(scintilla, SCI_SETTARGETRANGE, docEnd - targetEnd, targetStart);
}

static bool searchInTarget(TScintilla &scintilla, TStringView s, SearchDirection direction, bool select = true)
{
    sptr_t result = call(scintilla, SCI_SEARCHINTARGET, s.size(), (sptr_t) s.data());
    if (result != -1)
    {
        sptr_t resultEnd = call(scintilla, SCI_GETTARGETEND, 0U, 0U);
        if (select)
            call(scintilla, SCI_SETSEL, result, resultEnd);
        return true;
    }
    else if (direction == sdForwardIncremental)
    {
        sptr_t cur = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
        if (select)
            call(scintilla, SCI_SETEMPTYSELECTION, cur, 0U);
    }
    return false;
}

static void searchInTargetOrWrap(TScintilla &scintilla, TStringView s, SearchDirection direction)
{
    if (!searchInTarget(scintilla, s, direction) && direction != sdForwardIncremental)
    {
        wrapSearchTarget(scintilla);
        searchInTarget(scintilla, s, direction);
    }
}

void search(TScintilla &scintilla, TStringView text, SearchDirection direction, SearchSettings settings)
{
    if (!text.empty())
    {
        initSearchFlags(scintilla, settings);
        initSearchTarget(scintilla, direction);
        searchInTargetOrWrap(scintilla, text, direction);
    }
}

static bool selectionMatches(TScintilla &scintilla, TStringView text)
{
    Sci::Position selStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
    Sci::Position selEnd = call(scintilla, SCI_GETSELECTIONEND, 0U, 0U);
    call(scintilla, SCI_TARGETFROMSELECTION, 0U, 0U);
    Sci::Position targetStart = call(scintilla, SCI_SEARCHINTARGET, text.size(), (sptr_t) text.data());
    return selStart == targetStart &&
           selEnd == call(scintilla, SCI_GETTARGETEND, 0U, 0U);
}

void clearIndicator(TScintilla &scintilla, Indicator indicator)
{
    call(scintilla, SCI_SETINDICATORCURRENT, indicator, 0U);
    call(scintilla, SCI_INDICATORCLEARRANGE, 0, call(scintilla, SCI_GETTEXTLENGTH, 0U, 0U));
}

static void fillTargetWithIndicator(TScintilla &scintilla, Indicator indicator)
{
    Sci::Position targetStart = call(scintilla, SCI_GETTARGETSTART, 0U, 0U);
    Sci::Position targetEnd = call(scintilla, SCI_GETTARGETEND, 0U, 0U);
    call(scintilla, SCI_SETINDICATORCURRENT, indicator, 0U);
    call(scintilla, SCI_INDICATORFILLRANGE, targetStart, targetEnd - targetStart);
}

static void replaceSelectionAndMoveCaret(TScintilla &scintilla, TStringView withText)
{
    call(scintilla, SCI_TARGETFROMSELECTION, 0U, 0U);
    call(scintilla, SCI_REPLACETARGET, withText.size(), (sptr_t) withText.data());
    Sci::Position resultEnd = call(scintilla, SCI_GETTARGETEND, 0U, 0U);
    call(scintilla, SCI_GOTOPOS, resultEnd, 0U);
}

static void initReplaceOneSearchTarget(TScintilla &scintilla)
{
    Sci::Position targetStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
    Sci::Position targetEnd = call(scintilla, SCI_GETTEXTLENGTH, 0U, 0U);
    call(scintilla, SCI_SETTARGETRANGE, targetStart, targetEnd);
}

static void targetWholeDocument(TScintilla &scintilla)
{
    call(scintilla, SCI_TARGETWHOLEDOCUMENT, 0U, 0U);
}

static void targetUntilEnd(TScintilla &scintilla)
{
    Sci::Position targetStart = call(scintilla, SCI_GETTARGETEND, 0U, 0U);
    Sci::Position targetEnd = call(scintilla, SCI_GETTEXTLENGTH, 0U, 0U);
    call(scintilla, SCI_SETTARGETRANGE, targetStart, targetEnd);
}

static void replaceTarget(TScintilla &scintilla, TStringView withText)
{
    call(scintilla, SCI_REPLACETARGET, withText.size(), (sptr_t) withText.data());
}

void replace(TScintilla &scintilla, TStringView text, TStringView withText, ReplaceMethod method, SearchSettings settings)
{
    if (!text.empty())
    {
        call(scintilla, SCI_BEGINUNDOACTION, 0U, 0U);
        clearIndicator(scintilla, idtrReplaceHighlight);
        initSearchFlags(scintilla, settings);
        if (method == rmReplaceOne)
        {
            if (selectionMatches(scintilla, text))
            {
                replaceSelectionAndMoveCaret(scintilla, withText);
                fillTargetWithIndicator(scintilla, idtrReplaceHighlight);
            }
            initReplaceOneSearchTarget(scintilla);
            searchInTargetOrWrap(scintilla, text, sdForward);
        }
        else if (method == rmReplaceAll)
        {
            targetWholeDocument(scintilla);
            while (searchInTarget(scintilla, text, sdForward, false))
            {
                replaceTarget(scintilla, withText);
                fillTargetWithIndicator(scintilla, idtrReplaceHighlight);
                targetUntilEnd(scintilla);
            }
        }
        call(scintilla, SCI_ENDUNDOACTION, 0U, 0U);
    }
}

/////////////////////////////////////////////////////////////////////////
// Comment toggling

static bool removeComment(TScintilla &, const Language &);
static bool removeBlockComment(TScintilla &, const Language &);
static Sci::Position getSelectionEndSkippingEmptyLastLine(TScintilla &, Sci::Position);
static void getLineStartAndEnd(TScintilla &, Sci::Position &, Sci::Position &);
static size_t findCommentAtStart(TStringView, TStringView);
static size_t findCommentAtEnd(TStringView, TStringView);
static bool removeLineComments(TScintilla &, const Language &);
static bool noLinesBeginWithoutLineComment(TScintilla &, const Language &, Sci::Line, Sci::Line);
static void removeLineCommentFromLine(TScintilla &, const Language &, Sci::Line);
static void insertComment(TScintilla &, const Language &);
static bool thereIsTextBeforeOrAfterSelection(TScintilla &);
static void insertBlockComment(TScintilla &, const Language &);
static void restoreSelection(TScintilla &, Sci::Position, Sci::Position, Sci::Position, size_t, size_t);
static void insertLineComments(TScintilla &, const Language &);
static size_t minIndentationInLines(TScintilla &, Sci::Line, Sci::Line);
static size_t insertLineCommentIntoLine(TScintilla &, const Language &, Sci::Line, size_t);

void toggleComment(TScintilla &scintilla, const Language *language)
{
    if (language && (language->hasLineComments() || language->hasBlockComments()))
    {
        if (!removeComment(scintilla, *language))
            insertComment(scintilla, *language);
        call(scintilla, SCI_SCROLLCARET, 0U, 0U);
    }
}

static bool removeComment(TScintilla &scintilla, const Language &language)
{
    return removeBlockComment(scintilla, language)
        || removeLineComments(scintilla, language);
}

static bool removeBlockComment(TScintilla &scintilla, const Language &language)
{
    if (language.hasBlockComments())
    {
        Sci::Position posStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
        Sci::Position posEnd = getSelectionEndSkippingEmptyLastLine(scintilla, posStart);
        if (posStart == posEnd)
            getLineStartAndEnd(scintilla, posStart, posEnd);
        TStringView text = getRangePointer(scintilla, posStart, posEnd);

        size_t openStart = findCommentAtStart(text, language.blockCommentOpen);
        if (openStart < text.size())
        {
            size_t closeStart = findCommentAtEnd(text, language.blockCommentClose);
            if (closeStart < text.size())
            {
                size_t openSize = language.blockCommentOpen.size();
                size_t closeSize = language.blockCommentClose.size();
                call(scintilla, SCI_BEGINUNDOACTION, 0U, 0U);
                call(scintilla, SCI_DELETERANGE, posStart + openStart, openSize);
                call(scintilla, SCI_DELETERANGE, posStart + closeStart - openSize, closeSize);
                call(scintilla, SCI_ENDUNDOACTION, 0U, 0U);
                return true;
            }
        }
    }
    return false;
}

static Sci::Position getSelectionEndSkippingEmptyLastLine(TScintilla &scintilla, Sci::Position selStart)
{
    Sci::Position selEnd = call(scintilla, SCI_GETSELECTIONEND, 0U, 0U);
    if (selStart < selEnd)
    {
        Sci::Line line = call(scintilla, SCI_LINEFROMPOSITION, selEnd, 0U);
        Sci::Line prevPosLine = call(scintilla, SCI_LINEFROMPOSITION, selEnd - 1, 0U);
        if (prevPosLine < line)
            return call(scintilla, SCI_GETLINEENDPOSITION, prevPosLine, 0U);
    }
    return selEnd;
}

static void getLineStartAndEnd(TScintilla &scintilla, Sci::Position &posStart, Sci::Position &posEnd)
{
    Sci::Line line = call(scintilla, SCI_LINEFROMPOSITION, posStart, 0U);
    posStart = call(scintilla, SCI_POSITIONFROMLINE, line, 0U);
    posEnd = call(scintilla, SCI_GETLINEENDPOSITION, line, 0U);
}

static size_t findCommentAtStart(TStringView text, TStringView comment)
{
    size_t i = 0;
    while (i < text.size() && Scintilla::Internal::IsSpaceOrTab(text[i]))
        ++i;
    size_t j = 0;
    while (j < comment.size())
        if (!(i < text.size() && text[i++] == comment[j++]))
            return text.size();
    return i - comment.size();
}

static size_t findCommentAtEnd(TStringView text, TStringView comment)
{
    size_t i = text.size();
    while (i > 0 && Scintilla::Internal::IsSpaceOrTab(text[i - 1]))
        --i;
    size_t j = comment.size();
    while (j > 0)
        if (!(i > 0 && text[--i] == comment[--j]))
            return text.size();
    return i;
}

static bool removeLineComments(TScintilla &scintilla, const Language &language)
{
    if (language.hasLineComments())
    {
        Sci::Position selStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
        Sci::Position selEnd = getSelectionEndSkippingEmptyLastLine(scintilla, selStart);
        Sci::Line firstLine = call(scintilla, SCI_LINEFROMPOSITION, selStart, 0U);
        Sci::Line lastLine = call(scintilla, SCI_LINEFROMPOSITION, selEnd, 0U);

        if (noLinesBeginWithoutLineComment(scintilla, language, firstLine, lastLine))
        {
            call(scintilla, SCI_BEGINUNDOACTION, 0U, 0U);
            for (Sci::Line line = firstLine; line <= lastLine; ++line)
                removeLineCommentFromLine(scintilla, language, line);
            call(scintilla, SCI_ENDUNDOACTION, 0U, 0U);
            return true;
        }
    }
    return false;
}

static bool noLinesBeginWithoutLineComment(TScintilla &scintilla, const Language &language, Sci::Line firstLine, Sci::Line lastLine)
{
    bool atLeastOneIsNotEmpty = false;
    for (Sci::Line line = firstLine; line <= lastLine; ++line)
    {
        Sci::Position lineStart = call(scintilla, SCI_POSITIONFROMLINE, line, 0U);
        Sci::Position lineEnd = call(scintilla, SCI_GETLINEENDPOSITION, line, 0U);
        TStringView text = getRangePointer(scintilla, lineStart, lineEnd);
        if (!text.empty() && text.size() == findCommentAtStart(text, language.lineComment))
            return false;
        else if (!text.empty())
            atLeastOneIsNotEmpty = true;
    }
    return atLeastOneIsNotEmpty;
}

static void removeLineCommentFromLine(TScintilla &scintilla, const Language &language, Sci::Line line)
// Pre: 'language.lineComment' is not empty.
{
    Sci::Position lineStart = call(scintilla, SCI_POSITIONFROMLINE, line, 0U);
    Sci::Position lineEnd = call(scintilla, SCI_GETLINEENDPOSITION, line, 0U);
    TStringView text = getRangePointer(scintilla, lineStart, lineEnd);
    TStringView comment = language.lineComment;
    size_t commentStart = findCommentAtStart(text, comment);
    if (commentStart < text.size())
    {
        size_t commentEnd = commentStart + comment.size();
        if (comment.back() != ' ' && commentEnd < text.size() && text[commentEnd] == ' ')
            commentEnd += 1;
        call(scintilla, SCI_DELETERANGE, lineStart + commentStart, commentEnd - commentStart);
    }
}

static void insertComment(TScintilla &scintilla, const Language &language)
// Pre: language supports at least one kind of comment.
{
    if ( !language.hasLineComments()
         || (language.hasBlockComments() && thereIsTextBeforeOrAfterSelection(scintilla)) )
        insertBlockComment(scintilla, language);
    else
        insertLineComments(scintilla, language);
}

bool thereIsTextBeforeOrAfterSelection(TScintilla &scintilla)
{
    Sci::Position selStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
    Sci::Position selEnd = getSelectionEndSkippingEmptyLastLine(scintilla, selStart);
    if (selStart < selEnd)
    {
        Sci::Line firstLine = call(scintilla, SCI_LINEFROMPOSITION, selStart, 0U);
        Sci::Position firstLineStart = call(scintilla, SCI_POSITIONFROMLINE, firstLine, 0U);
        TStringView textBefore = getRangePointer(scintilla, firstLineStart, selStart);
        for (char c : textBefore)
            if (!Scintilla::Internal::IsSpaceOrTab(c))
                return true;
        Sci::Line lastLine = call(scintilla, SCI_LINEFROMPOSITION, selEnd, 0U);
        Sci::Position lastLineEnd = call(scintilla, SCI_GETLINEENDPOSITION, lastLine, 0U);
        TStringView textAfter = getRangePointer(scintilla, selEnd, lastLineEnd);
        for (char c : textAfter)
            if (!Scintilla::Internal::IsSpaceOrTab(c))
                return true;
    }
    return false;
}

static void insertBlockComment(TScintilla &scintilla, const Language &language)
// Pre: language.hasBlockComments()
{
    Sci::Position caret = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    Sci::Position anchor = call(scintilla, SCI_GETANCHOR, 0U, 0U);
    Sci::Position posStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
    Sci::Position posEnd = getSelectionEndSkippingEmptyLastLine(scintilla, posStart);
    if (posStart == posEnd)
        getLineStartAndEnd(scintilla, posStart, posEnd);

    call(scintilla, SCI_BEGINUNDOACTION, 0U, 0U);
    call(scintilla, SCI_INSERTTEXT, posEnd, (sptr_t) std::string(language.blockCommentClose).c_str());
    call(scintilla, SCI_INSERTTEXT, posStart, (sptr_t) std::string(language.blockCommentOpen).c_str());
    restoreSelection(scintilla, caret, anchor, posStart, language.blockCommentOpen.size(), language.blockCommentClose.size());
    call(scintilla, SCI_ENDUNDOACTION, 0U, 0U);
}

static void restoreSelection(TScintilla &scintilla, Sci::Position caret, Sci::Position anchor, Sci::Position prefixInsertPos, size_t prefixLength, size_t suffixLength)
{
    Sci::Position &selStart = caret < anchor ? caret : anchor;
    Sci::Position &selEnd = caret < anchor ? anchor : caret;
    if (caret == anchor)
    {
        selStart += prefixLength;
        selEnd = selStart;
    }
    else
    {
        if (prefixInsertPos < selStart)
            selStart += prefixLength;
        selEnd += prefixLength + suffixLength;
    }
    call(scintilla, SCI_SETSEL, anchor, caret);
}

static void insertLineComments(TScintilla &scintilla, const Language &language)
// Pre: language.hasLineComments()
{
    Sci::Position caret = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    Sci::Position anchor = call(scintilla, SCI_GETANCHOR, 0U, 0U);
    Sci::Position posStart = call(scintilla, SCI_GETSELECTIONSTART, 0U, 0U);
    Sci::Position posEnd = getSelectionEndSkippingEmptyLastLine(scintilla, posStart);
    Sci::Line firstLine = call(scintilla, SCI_LINEFROMPOSITION, posStart, 0U);
    Sci::Line lastLine = call(scintilla, SCI_LINEFROMPOSITION, posEnd, 0U);
    Sci::Position firstLineStart = call(scintilla, SCI_POSITIONFROMLINE, firstLine, 0U);

    size_t indentation = minIndentationInLines(scintilla, firstLine, lastLine);
    call(scintilla, SCI_BEGINUNDOACTION, 0U, 0U);
    size_t insertLength = 0;
    for (Sci::Line line = firstLine; line <= lastLine; ++line)
        insertLength += insertLineCommentIntoLine(scintilla, language, line, indentation);
    restoreSelection(scintilla, caret, anchor, firstLineStart + indentation, insertLength, 0);
    call(scintilla, SCI_ENDUNDOACTION, 0U, 0U);
}

static size_t minIndentationInLines(TScintilla &scintilla, Sci::Line firstLine, Sci::Line lastLine)
{
    size_t result = SIZE_MAX;
    for (Sci::Line line = firstLine; line <= lastLine; ++line)
    {
        Sci::Position lineStart = call(scintilla, SCI_POSITIONFROMLINE, line, 0U);
        Sci::Position lineEnd = call(scintilla, SCI_GETLINEENDPOSITION, line, 0U);
        TStringView text = getRangePointer(scintilla, lineStart, lineEnd);
        if (!text.empty())
        {
            size_t i = 0;
            while (i < text.size() && Scintilla::Internal::IsSpaceOrTab(text[i]))
                ++i;
            if (i != text.size())
                result = min(i, result);
        }
    }
    return result == SIZE_MAX ? 0 : result;
}

static size_t insertLineCommentIntoLine(TScintilla &scintilla, const Language &language, Sci::Line line, size_t indentation)
{
    std::string comment {language.lineComment};
    if (comment.back() != ' ')
        comment.push_back(' ');
    size_t insertLength = 0;
    Sci::Position lineStart = call(scintilla, SCI_POSITIONFROMLINE, line, 0U);
    Sci::Position lineEnd = call(scintilla, SCI_GETLINEENDPOSITION, line, 0U);
    if (lineStart == lineEnd && indentation > 0)
    {
        call(scintilla, SCI_INSERTTEXT, lineStart, (sptr_t) std::string(indentation, ' ').c_str());
        insertLength += indentation;
    }
    call(scintilla, SCI_INSERTTEXT, lineStart + indentation, (sptr_t) comment.c_str());
    insertLength += comment.size();
    return insertLength;
}

/////////////////////////////////////////////////////////////////////////

// Maps the legacy SCLEX_* lexer id (still used by the builtInLexers table) to
// the Lexilla lexer name needed by Lexilla::CreateLexer. Scintilla 5.x no longer
// bundles lexers, so they are created by name instead of selected by id.
static const char *lexerNameForId(int id)
{
    switch (id)
    {
        case SCLEX_CPP:        return "cpp";
        case SCLEX_LUA:        return "lua";
        case SCLEX_MAKEFILE:   return "makefile";
        case SCLEX_ASM:        return "asm";
        case SCLEX_RUST:       return "rust";
        case SCLEX_PYTHON:     return "python";
        case SCLEX_BASH:       return "bash";
        case SCLEX_RUBY:       return "ruby";
        case SCLEX_JSON:       return "json";
        case SCLEX_YAML:       return "yaml";
        case SCLEX_HTML:       return "hypertext";
        case SCLEX_PROPERTIES: return "props";
        case SCLEX_VB:         return "vb";
        case SCLEX_PASCAL:     return "pascal";
        case SCLEX_LATEX:      return "latex";
        case SCLEX_SQL:        return "sql";
        case SCLEX_MARKDOWN:   return "markdown";
        default:               return nullptr;
    }
}

void applyTheming(const LexerSettings *lexer, const ColorScheme *aScheme, TScintilla &scintilla)
{
    auto &scheme = aScheme ? *aScheme : schemeActive;
    setStyleColor(scintilla, STYLE_DEFAULT, scheme[sNormal]);
    call(scintilla, SCI_STYLECLEARALL, 0U, 0U); // Must be done before setting other colors.
    setSelectionColor(scintilla, scheme[sSelection]);
    setWhitespaceColor(scintilla, scheme[sWhitespace]);
    setStyleColor(scintilla, STYLE_CONTROLCHAR, normalize(scheme, sCtrlChar));
    setStyleColor(scintilla, STYLE_LINENUMBER, normalize(scheme, sLineNums));
    // Fold markers live in the same gutter as the line numbers; paint their
    // +/- glyphs in the same colours so they don't sit in Scintilla's default
    // grey marker box (which ignores the scheme).
    {
        TColorAttr foldAttr = normalize(scheme, sLineNums);
        for (int m : {SC_MARKNUM_FOLDER, SC_MARKNUM_FOLDEROPEN, SC_MARKNUM_FOLDEROPENMID,
                      SC_MARKNUM_FOLDEREND, SC_MARKNUM_FOLDERSUB, SC_MARKNUM_FOLDERTAIL,
                      SC_MARKNUM_FOLDERMIDTAIL})
            setMarkerColor(scintilla, m, foldAttr);
    }
    setIndicatorColor(scintilla, idtrReplaceHighlight, scheme[sReplaceHighlight]);
    // The bookmark and change-history markers sit on hidden (width-0) margins,
    // so Scintilla treats them as in-line background markers and paints the
    // whole marked line with the marker's background (see markBookmark /
    // markChanged in editstates.h). Pin that background to the normal text
    // background -- which here already carries the active/passive frame shade --
    // so a modified or bookmarked line is not drawn in the terminal-default
    // colour. Only the background is set; the margin glyph colours come from
    // Editor::setUpExtraMargins.
    setMarkerBackColor(scintilla, markBookmark, scheme[sNormal]);
    setMarkerBackColor(scintilla, markChanged, scheme[sNormal]);
    if (lexer)
    {
        // Create the lexer through Lexilla (lexers are no longer part of
        // Scintilla core) and install it with SCI_SETILEXER.
        Scintilla::ILexer5 *ilexer = nullptr;
        if (const char *name = lexerNameForId(lexer->id))
            ilexer = CreateLexer(name);
        call(scintilla, SCI_SETILEXER, 0, (sptr_t) ilexer);
        for (const auto &s : lexer->styles)
        {
            TColorAttr attr = normalize(scheme, s.style);
            if (s.styleAdd)
                attr = {::getFore(attr), ::getBack(attr),
                        uchar(::getStyle(attr) | s.styleAdd)};
            setStyleColor(scintilla, s.id, attr);
        }
        for (const auto &k : lexer->keywords)
            call(scintilla, SCI_SETKEYWORDS, k.id, (sptr_t) k.keywords);
        for (const auto &p : lexer->properties)
            call(scintilla, SCI_SETPROPERTY, (sptr_t) p.name, (sptr_t) p.value);
        // Enable fold-point computation for every lexer, so the fold margin
        // works when the user turns folding on. Overrides any per-lexer
        // "fold"="0" default. The HTML lexer (also used for PHP) additionally
        // gates folding behind "fold.html", so enable that too -- without it
        // PHP brace folding never computes (LexHTML: fold = foldHTML && fold).
        call(scintilla, SCI_SETPROPERTY, (sptr_t) "fold", (sptr_t) "1");
        call(scintilla, SCI_SETPROPERTY, (sptr_t) "fold.html", (sptr_t) "1");
    }
    else
        // No lexer: clear any installed ILexer (the old SCLEX_CONTAINER state).
        call(scintilla, SCI_SETILEXER, 0, (sptr_t) nullptr);
    call(scintilla, SCI_COLOURISE, 0, -1);
}

static bool isBrace(char ch)
{
    TStringView braces = "[](){}";
    return memchr(braces.data(), ch, braces.size()) != nullptr;
}

void updateBraces(const ColorScheme *aScheme, TScintilla &scintilla)
{
    auto pos = call(scintilla, SCI_GETCURRENTPOS, 0U, 0U);
    auto ch = call(scintilla, SCI_GETCHARAT, pos, 0U);
    bool braceFound = false;
    if (isBrace(ch))
    {
        // Scintilla already makes sure that both braces have the same style.
        auto matchPos = call(scintilla, SCI_BRACEMATCH, pos, 0U);
        if (matchPos != -1)
        {
            auto &scheme = aScheme ? *aScheme : schemeActive;
            auto style = call(scintilla, SCI_GETSTYLEAT, pos, 0U);
            auto curAttr = getStyleColor(scintilla, style);
            auto braceAttr = coalesce(scheme[sBraceMatch], curAttr);
            setStyleColor(scintilla, STYLE_BRACELIGHT, braceAttr);
            call(scintilla, SCI_BRACEHIGHLIGHT, pos, matchPos);
            braceFound = true;
        }
    }
    if (!braceFound)
        call(scintilla, SCI_BRACEHIGHLIGHT, -1, -1);
}

static void keepSpecialTrailingSpaces( const Language *language,
                                       Sci::Position &whitespaceStart,
                                       Sci::Position lineEnd )
{
    // In Markdown, two trailing whitespaces behave as a line break, so keep them.
    if (language == &Language::Markdown)
    {
        if (whitespaceStart + 2 <= lineEnd)
            whitespaceStart += 2;
    }
}

void stripTrailingSpaces(TScintilla &scintilla, const Language *language)
{
    Sci::Line lineCount = call(scintilla, SCI_GETLINECOUNT, 0U, 0U);
    for (Sci::Line line = 0; line < lineCount; ++line) {
        Sci::Position lineStart = call(scintilla, SCI_POSITIONFROMLINE, line, 0U);
        Sci::Position lineEnd = call(scintilla, SCI_GETLINEENDPOSITION, line, 0U);
        Sci::Position whitespaceStart = lineEnd;
        while (whitespaceStart > lineStart)
        {
            char ch = call(scintilla, SCI_GETCHARAT, whitespaceStart - 1, 0U);
            if (ch != ' ' && ch != '\t')
                break;
            --whitespaceStart;
        }
        if (whitespaceStart < lineEnd)
        {
            keepSpecialTrailingSpaces(language, whitespaceStart, lineEnd);
            call(scintilla, SCI_SETTARGETRANGE, whitespaceStart, lineEnd);
            call(scintilla, SCI_REPLACETARGET, 0, (sptr_t) "");
        }
    }
}

void ensureNewlineAtEnd(TScintilla &scintilla)
{
    int EOLType = call(scintilla, SCI_GETEOLMODE, 0U, 0U);
    Sci::Line lineCount = call(scintilla, SCI_GETLINECOUNT, 0U, 0U);
    Sci::Position docEnd = call(scintilla, SCI_POSITIONFROMLINE, lineCount, 0U);
    if ( lineCount == 1 || (lineCount > 1 &&
         docEnd > call(scintilla, SCI_POSITIONFROMLINE, lineCount - 1, 0U)) )
    {
        std::string_view EOL = (EOLType == SC_EOL_CRLF) ? "\r\n" :
                               (EOLType == SC_EOL_CR)   ? "\r"   :
                                                          "\n";
        call(scintilla, SCI_APPENDTEXT, EOL.size(), (sptr_t) EOL.data());
    }
}

} // namespace turbo
