#define Uses_TMenu
#define Uses_TMenuItem
#include <tvision/tv.h>

#include "menucheck.h"

#include <cstring>

namespace {

// UTF-8 check glyph (U+221A SQUARE ROOT, the classic TUI tick) + a space, and
// the matching two-space padding for the unchecked state. cstrlen() measures
// display width, so the glyph counts as one column and menu boxes size right.
const char checkPrefix[] = "\xE2\x88\x9A ";
const char blankPrefix[] = "  ";

// True if 'name' already starts with our check prefix.
bool hasCheck(const char *name) noexcept
{
    return name && std::strncmp(name, checkPrefix, sizeof checkPrefix - 1) == 0;
}

// True if 'name' starts with the two-space blank prefix (an item we manage that
// is currently unchecked).
bool hasBlank(const char *name) noexcept
{
    return name && name[0] == ' ' && name[1] == ' ';
}

// The label text without any prefix we may have added before.
const char *bareLabel(const char *name) noexcept
{
    if (hasCheck(name))
        return name + (sizeof checkPrefix - 1);
    if (hasBlank(name))
        return name + (sizeof blankPrefix - 1);
    return name;
}

void rewrite(TMenuItem *item, bool checked) noexcept
{
    const char *bare = bareLabel(item->name);
    const char *prefix = checked ? checkPrefix : blankPrefix;
    // Skip the allocation if the label is already in the desired state.
    if (checked ? hasCheck(item->name) : (hasBlank(item->name) && !hasCheck(item->name)))
        return;
    char *buf = new char[std::strlen(prefix) + std::strlen(bare) + 1];
    std::strcpy(buf, prefix);
    std::strcat(buf, bare);
    delete[] (char *) item->name; // TMenuItem owns its name (newStr/delete[]).
    item->name = buf;
}

} // namespace

void setMenuItemCheck(TMenu *menu, unsigned short command, bool checked) noexcept
{
    if (!menu)
        return;
    for (TMenuItem *p = menu->items; p; p = p->next)
    {
        if (p->name == nullptr) // separator line
            continue;
        if (p->command == 0 && p->subMenu) // submenu: recurse
            setMenuItemCheck(p->subMenu, command, checked);
        else if (p->command == command)
        {
            rewrite(p, checked);
            return;
        }
    }
}
