#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TStaticText
#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "lspdialog.h"
#include "settings.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

// The languages presented in the dialog, with their built-in default commands
// (shown as a hint when nothing is configured).
struct KnownLang { const char *id; const char *label; const char *def; };

const KnownLang kKnownLangs[] = {
    {"cpp",        "C/C++ (clangd):",        "clangd"},
    {"python",     "Python (pyright):",      "pyright-langserver --stdio"},
    {"rust",       "Rust (rust-analyzer):",  "rust-analyzer"},
    {"go",         "Go (gopls):",            "gopls"},
    {"javascript", "JavaScript (tsserver):", "typescript-language-server --stdio"},
};

constexpr int kNumLangs = sizeof(kKnownLangs) / sizeof(kKnownLangs[0]);
constexpr int kInputMax = 256;

} // namespace

bool executeLspDialog(AppSettings &settings) noexcept
{
    int rows = kNumLangs;
    // Layout: title note, enable checkbox, one labelled input line per language.
    int height = 6 + rows * 3 + 2;
    auto *d = new TDialog(TRect(0, 0, 64, height), "Language Servers");
    d->options |= ofCentered;

    d->insert(new TStaticText(TRect(2, 2, 62, 4),
        "Command run per language. Leave blank to use the built-in "
        "default. Servers must be installed on PATH."));

    auto *enableBox = new TCheckBoxes(
        TRect(3, 4, 40, 5),
        new TSItem("~E~nable language servers", nullptr));
    d->insert(enableBox);

    std::vector<TInputLine *> lines;
    lines.reserve(kNumLangs);
    int y = 6;
    for (int i = 0; i < kNumLangs; ++i)
    {
        d->insert(new TLabel(TRect(3, y, 61, y + 1), kKnownLangs[i].label, nullptr));
        auto *line = new TInputLine(TRect(3, y + 1, 61, y + 2), kInputMax);
        d->insert(line);
        lines.push_back(line);
        y += 3;
    }

    int by = height - 3;
    d->insert(new TButton(TRect(40, by, 50, by + 2), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(51, by, 61, by + 2), "Cancel", cmCancel, bfNormal));

    // Seed the controls from current settings.
    if (settings.lspEnabled)
        enableBox->press(0);
    for (int i = 0; i < kNumLangs; ++i)
    {
        std::string cmd = settings.lspCommandFor(kKnownLangs[i].id);
        strncpy(lines[i]->data, cmd.c_str(), kInputMax);
        lines[i]->data[kInputMax] = '\0';
    }

    d->selectNext(False);

    bool accepted = (TProgram::deskTop->execView(d) == cmOK);
    if (accepted)
    {
        settings.lspEnabled = enableBox->mark(0);
        for (int i = 0; i < kNumLangs; ++i)
        {
            // Trim leading/trailing spaces.
            std::string cmd = lines[i]->data;
            size_t a = cmd.find_first_not_of(" \t");
            size_t b = cmd.find_last_not_of(" \t");
            cmd = (a == std::string::npos) ? std::string() : cmd.substr(a, b - a + 1);
            settings.setLspCommand(kKnownLangs[i].id, cmd);
        }
    }

    TObject::destroy(d);
    return accepted;
}
