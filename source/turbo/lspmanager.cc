#include "lspmanager.h"

#ifdef TURBO_ENABLE_LSP

#include "editwindow.h"
#include "editor.h"

#include <turbo/scintilla.h>
#include <turbo/styles.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#endif

using turbo::lsp::Client;
using turbo::lsp::Json;

// --- small helpers ---------------------------------------------------------

namespace {

FILE *lspLog() noexcept
{
    // Optional debug log; enabled by setting TURBO_LSP_LOG=<path>.
    static FILE *f = [] () -> FILE * {
        const char *p = getenv("TURBO_LSP_LOG");
        return (p && p[0]) ? fopen(p, "w") : nullptr;
    }();
    return f;
}

void logLine(const std::string &s) noexcept
{
    if (FILE *f = lspLog())
    {
        fputs(s.c_str(), f);
        fputc('\n', f);
        fflush(f);
    }
}

// Maps Turbo's detected Language to an LSP language id. Returns "" when there
// is no sensible LSP id (so no server is started).
std::string languageIdFor(const turbo::Language *lang) noexcept
{
    using turbo::Language;
    if (!lang) return {};
    if (lang == &Language::CPP)         return "cpp";
    if (lang == &Language::Python)      return "python";
    if (lang == &Language::Rust)        return "rust";
    if (lang == &Language::Go)          return "go";
    if (lang == &Language::JavaScript)  return "javascript";
    return {};
}

std::string uriFromPath(const std::string &path) noexcept
{
    // Minimal file:// URI with percent-encoding. POSIX-oriented; Windows drive
    // paths would need extra handling, deferred until needed.
    std::string uri = "file://";
    char buf[4];
    for (unsigned char c : path)
    {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' ||
            c == '.' || c == '~')
            uri += (char) c;
        else
        {
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            uri += buf;
        }
    }
    return uri;
}

std::string readEditorText(EditorWindow &w) noexcept
{
    auto &ed = w.getEditor();
    auto len = ed.callScintilla(SCI_GETLENGTH, 0U, 0U);
    if (len <= 0)
        return {};
    TStringView text = turbo::getRangePointer(ed.scintilla, 0, len);
    return std::string(text.data(), text.size());
}

std::vector<std::string> splitArgs(const std::string &s) noexcept
{
    // Naive whitespace split (good enough for config/env command lines).
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == ' ' || c == '\t')
        {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
        else
            cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool existsOnPath(const std::string &cmd) noexcept
{
#ifdef _WIN32
    return true; // Let CreateProcess attempt the lookup.
#else
    if (cmd.find('/') != std::string::npos)
        return access(cmd.c_str(), X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path)
        return false;
    std::string dirs = path, dir;
    auto tryDir = [&] (const std::string &d) {
        std::string full = d + "/" + cmd;
        return access(full.c_str(), X_OK) == 0;
    };
    for (char c : dirs)
    {
        if (c == ':')
        {
            if (!dir.empty() && tryDir(dir)) return true;
            dir.clear();
        }
        else
            dir += c;
    }
    return !dir.empty() && tryDir(dir);
#endif
}

} // namespace

// --- LspManager ------------------------------------------------------------

LspManager::LspManager() noexcept = default;

LspManager::~LspManager()
{
    shutdown();
}

void LspManager::setRootPath(const char *path) noexcept
{
    if (path && path[0])
        rootUri = uriFromPath(path);
}

LspManager::ServerConfig LspManager::serverFor(const std::string &languageId) noexcept
{
    // An environment override (TURBO_LSP_SERVER_<LANG>) takes precedence; this
    // also makes the transport testable against a mock server. Settings-based
    // configuration arrives in a later stage.
    std::string envKey = "TURBO_LSP_SERVER_" + languageId;
    for (auto &c : envKey) c = (char) std::toupper((unsigned char) c);
    if (const char *ov = getenv(envKey.c_str()); ov && ov[0])
    {
        auto parts = splitArgs(ov);
        if (!parts.empty())
        {
            ServerConfig cfg;
            cfg.command = parts[0];
            cfg.args.assign(parts.begin() + 1, parts.end());
            return cfg;
        }
    }

    // Built-in defaults.
    static const struct { const char *lang, *cmd, *args; } defaults[] = {
        {"cpp",        "clangd",                     ""},
        {"python",     "pyright-langserver",         "--stdio"},
        {"rust",       "rust-analyzer",              ""},
        {"go",         "gopls",                      ""},
        {"javascript", "typescript-language-server", "--stdio"},
    };
    for (auto &d : defaults)
        if (languageId == d.lang)
        {
            ServerConfig cfg;
            cfg.command = d.cmd;
            cfg.args = splitArgs(d.args);
            return cfg;
        }
    return {};
}

Client *LspManager::clientFor(const std::string &languageId) noexcept
{
    if (auto it = clients.find(languageId); it != clients.end())
        return it->second.get();
    if (deadLanguages.count(languageId))
        return nullptr;

    ServerConfig cfg = serverFor(languageId);
    if (!cfg.valid() || !existsOnPath(cfg.command))
    {
        deadLanguages.insert(languageId);
        logLine("no server available for language: " + languageId);
        return nullptr;
    }

    auto client = std::make_unique<Client>(languageId, cfg.command, cfg.args);
    if (!client->start(rootUri))
    {
        deadLanguages.insert(languageId);
        logLine("failed to start server for language: " + languageId);
        return nullptr;
    }
    logLine("started server '" + cfg.command + "' for language: " + languageId);
    Client *raw = client.get();
    clients.emplace(languageId, std::move(client));
    return raw;
}

void LspManager::didOpen(EditorWindow &w) noexcept
{
    if (docs.count(&w))
        return;
    auto &path = w.filePath();
    if (path.empty())
        return;
    std::string langId = languageIdFor(w.getEditor().language);
    if (langId.empty())
        return;
    Client *client = clientFor(langId);
    if (!client)
        return;

    Document doc;
    doc.uri = uriFromPath(path);
    doc.languageId = langId;
    doc.version = 1;
    doc.client = client;

    Json params = {
        {"textDocument", {
            {"uri", doc.uri},
            {"languageId", langId},
            {"version", doc.version},
            {"text", readEditorText(w)},
        }},
    };
    client->notify("textDocument/didOpen", params);
    logLine("didOpen " + doc.uri);
    docs.emplace(&w, std::move(doc));
}

void LspManager::didChange(EditorWindow &w) noexcept
{
    if (docs.count(&w))
        dirty.insert(&w); // flushed (debounced) in pump()
}

void LspManager::flushChange(EditorWindow &w) noexcept
{
    auto it = docs.find(&w);
    if (it == docs.end())
        return;
    Document &doc = it->second;
    doc.version += 1;
    // Full-document sync (textDocumentSync.change = Full).
    Json params = {
        {"textDocument", {{"uri", doc.uri}, {"version", doc.version}}},
        {"contentChanges", Json::array({ Json{{"text", readEditorText(w)}} })},
    };
    doc.client->notify("textDocument/didChange", params);
    logLine("didChange v" + std::to_string(doc.version) + " " + doc.uri);
}

void LspManager::didSave(EditorWindow &w) noexcept
{
    auto it = docs.find(&w);
    if (it == docs.end())
        return;
    // Flush any pending change first so the server's view matches disk.
    if (dirty.erase(&w))
        flushChange(w);
    Document &doc = it->second;
    Json params = {
        {"textDocument", {{"uri", doc.uri}}},
        {"text", readEditorText(w)},
    };
    doc.client->notify("textDocument/didSave", params);
    logLine("didSave " + doc.uri);
}

void LspManager::didClose(EditorWindow &w) noexcept
{
    auto it = docs.find(&w);
    if (it == docs.end())
        return;
    dirty.erase(&w);
    Document &doc = it->second;
    Json params = {{"textDocument", {{"uri", doc.uri}}}};
    doc.client->notify("textDocument/didClose", params);
    logLine("didClose " + doc.uri);
    docs.erase(it);
}

void LspManager::onServerMessage(const std::string &languageId, const Json &msg) noexcept
{
    // Stage 2 just logs server-initiated traffic; later stages act on
    // publishDiagnostics, etc.
    std::string method = msg.value("method", std::string());
    logLine("server[" + languageId + "] " + method);
}

void LspManager::pump() noexcept
{
    // Flush debounced changes (idle only fires once typing pauses).
    if (!dirty.empty())
    {
        std::vector<EditorWindow *> pending(dirty.begin(), dirty.end());
        dirty.clear();
        for (auto *w : pending)
            flushChange(*w);
    }

    // Deliver inbound server messages/responses on the main thread.
    for (auto &kv : clients)
    {
        const std::string &lang = kv.first;
        Client *client = kv.second.get();
        client->pump([this, &lang](const Json &msg) {
            onServerMessage(lang, msg);
        });
    }
}

void LspManager::shutdown() noexcept
{
    docs.clear();
    dirty.clear();
    for (auto &kv : clients)
        if (kv.second)
            kv.second->stop();
    clients.clear();
}

#endif // TURBO_ENABLE_LSP
