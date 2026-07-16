#define Uses_TDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "debugdialog.h"
#include "debugconfig.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The languages presented, with their default request and a command hint.
struct KnownLang { const char *id; const char *label; const char *req; };

const KnownLang kLangs[] = {
    {"cpp",    "C/C++ (lldb-dap):", "launch"},
    {"python", "Python (debugpy):", "launch"},
    {"php",    "PHP (php-debug):",  "attach"},
};

constexpr int kNum = sizeof(kLangs) / sizeof(kLangs[0]);
constexpr int kCmdMax = 256;
constexpr int kReqMax = 12;

std::string trim(const char *s) noexcept
{
    std::string v = s ? s : "";
    size_t a = v.find_first_not_of(" \t");
    size_t b = v.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

} // namespace

bool executeDebugDialog(DebugConfig &config) noexcept
{
    const int firstY = 7;
    int rows = kNum;
    int height = firstY + rows + 4;
    auto *d = new TDialog(TRect(0, 0, 68, height), "Debuggers");
    d->options |= ofCentered;

    d->insert(new TStaticText(TRect(2, 2, 66, 6),
        "Debug adapter command + request (launch/attach) per language, saved to "
        ".turbo/debug.json. A blank command uses the built-in default (php has "
        "none -- give it the php-debug adapter). Adapters must be on PATH."));

    d->insert(new TLabel(TRect(24, firstY - 1, 32, firstY), "command", nullptr));
    d->insert(new TLabel(TRect(55, firstY - 1, 63, firstY), "request", nullptr));

    std::vector<TInputLine *> cmds, reqs;
    int y = firstY;
    for (int i = 0; i < kNum; ++i)
    {
        d->insert(new TLabel(TRect(2, y, 23, y + 1), kLangs[i].label, nullptr));
        auto *c = new TInputLine(TRect(23, y, 54, y + 1), kCmdMax);
        d->insert(c); cmds.push_back(c);
        auto *r = new TInputLine(TRect(55, y, 65, y + 1), kReqMax);
        d->insert(r); reqs.push_back(r);
        y += 1;
    }

    int by = firstY + rows + 1;
    d->insert(new TButton(TRect(44, by, 54, by + 2), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(55, by, 65, by + 2), "Cancel", cmCancel, bfNormal));

    // Seed from the current config.
    for (int i = 0; i < kNum; ++i)
    {
        const DebugAdapter *a = config.forLanguage(kLangs[i].id);
        std::string cmd = a ? a->command : std::string();
        std::string req = (a && !a->request.empty()) ? a->request : kLangs[i].req;
        // Guard the fixed data buffers (see lspdialog.cc: writing data[max] would
        // overflow the heap allocation and crash on open).
        strncpy(cmds[i]->data, cmd.c_str(), kCmdMax - 1); cmds[i]->data[kCmdMax - 1] = '\0';
        strncpy(reqs[i]->data, req.c_str(), kReqMax - 1); reqs[i]->data[kReqMax - 1] = '\0';
    }

    d->selectNext(False);

    bool accepted = (TProgram::deskTop->execView(d) == cmOK);
    if (accepted)
    {
        for (int i = 0; i < kNum; ++i)
        {
            std::string cmd = trim(cmds[i]->data);
            std::string req = trim(reqs[i]->data);
            // Preserve any other fields (port/program/cwd/...) from an existing
            // entry for this language.
            DebugAdapter merged;
            if (const DebugAdapter *ex = config.forLanguage(kLangs[i].id))
                merged = *ex;
            merged.language = kLangs[i].id;
            merged.command = cmd;
            merged.request = req;
            if (merged.request == "attach" && merged.port == 0)
                merged.port = 9003; // Xdebug's default
            // Replace the existing entry.
            config.adapters.erase(
                std::remove_if(config.adapters.begin(), config.adapters.end(),
                    [&](const DebugAdapter &a){ return a.language == kLangs[i].id; }),
                config.adapters.end());
            // Keep an entry only if it configures something beyond the default.
            if (!cmd.empty() || (!req.empty() && req != kLangs[i].req))
                config.adapters.push_back(std::move(merged));
        }
    }

    TObject::destroy(d);
    return accepted;
}
