#include <turbo/scintilla/internals.h>

namespace Scintilla::Internal {

// turbo does not use Scintilla's autocompletion/calltip list box (it provides
// its own UI), so this is an inert implementation of the 5.x ListBox ABI.
class ListBoxTV : public ListBox
{
    void SetFont(const Font *) override
    {
    }

    void Create(Window &, int, Point, int, bool, Scintilla::Technology) override
    {
    }

    void SetAverageCharWidth(int) override
    {
    }

    void SetVisibleRows(int) override
    {
    }

    int GetVisibleRows() const override
    {
        return 0;
    }

    PRectangle GetDesiredRect() override
    {
        return PRectangle();
    }

    int CaretFromEdge() override
    {
        return 0;
    }

    void Clear() noexcept override
    {
    }

    void Append(char *, int) override
    {
    }

    int Length() override
    {
        return 0;
    }

    void Select(int) override
    {
    }

    int GetSelection() override
    {
        return 0;
    }

    int Find(const char *) override
    {
        return 0;
    }

    std::string GetValue(int) override
    {
        return std::string();
    }

    void RegisterImage(int, const char *) override
    {
    }

    void RegisterRGBAImage(int, int, int, const unsigned char *) override
    {
    }

    void ClearRegisteredImages() override
    {
    }

    void SetDelegate(IListBoxDelegate *) override
    {
    }

    void SetList(const char *, char, char) override
    {
    }

    void SetOptions(ListOptions) override
    {
    }
};

ListBox::ListBox() noexcept
{
}

ListBox::~ListBox() noexcept
{
}

std::unique_ptr<ListBox> ListBox::Allocate()
{
    return std::make_unique<ListBoxTV>();
}

} // namespace Scintilla::Internal
