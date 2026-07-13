#include "lspmanager.h"

#ifdef TURBO_ENABLE_LSP

// These Uses_ macros must precede the first include of <tvision/tv.h>
// (pulled in transitively by editwindow.h) so the modal completion popup can
// use TProgram/TDeskTop/execView and post events.
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "editwindow.h"
#include "editor.h"
#include "listviews.h"
#include "cmds.h"

#include <turbo/scintilla.h>
#include <turbo/styles.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

using turbo::lsp::Client;
using turbo::lsp::Json;

// Diagnostics are decorated with Scintilla annotations: a line drawn *below* the
// offending code showing a '~~~~' run aligned under the span plus the message.
// (Real squiggly underlines are not possible here -- the terminal Surface leaves
// the line-drawing primitives every Scintilla squiggle style relies on as empty
// stubs, and a terminal cell has no sub-character row to draw a wave in.)
//
// Annotation text is coloured via two dedicated styles, placed high above the
// lexer style range through SCI_ANNOTATIONSETSTYLEOFFSET so they never clash
// with a language's syntax styles.
namespace {
constexpr int kAnnoStyleOffset = 200;
constexpr int kAnnoError = 0;   // -> real style kAnnoStyleOffset + 0
constexpr int kAnnoWarning = 1; // -> real style kAnnoStyleOffset + 1

// Configure annotation visibility and the error/warning colours for one editor.
// Idempotent; safe to call repeatedly.
void setupAnnotations(turbo::Editor &ed) noexcept
{
    ed.callScintilla(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_STANDARD, 0U);
    ed.callScintilla(SCI_ANNOTATIONSETSTYLEOFFSET, kAnnoStyleOffset, 0U);
    // Colour the diagnostic annotations from the active scheme so they honour the
    // user's theme. Errors reuse the scheme's 'sError' (white-on-red by default);
    // warnings have no dedicated scheme entry, so they keep the editor background
    // with a gold foreground borrowed from the escape-sequence accent.
    const turbo::ColorScheme &scheme = ed.scheme ? *ed.scheme : turbo::schemeActive;
    TColorAttr err = turbo::normalize(scheme, turbo::sError);
    TColorAttr warn = turbo::getStyleColor(ed.scintilla, STYLE_DEFAULT);
    ::setFore(warn, ::getFore(turbo::normalize(scheme, turbo::sEscapeSequence)));
    turbo::setStyleColor(ed.scintilla, kAnnoStyleOffset + kAnnoError, err);
    turbo::setStyleColor(ed.scintilla, kAnnoStyleOffset + kAnnoWarning, warn);
}
} // namespace

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
    if (lang == &Language::PHP)         return "php";
    if (lang == &Language::Elixir)      return "elixir";
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

// Converts a Scintilla byte offset to an LSP (line, character) position,
// honouring the negotiated position encoding.
void bytePosToLsp(turbo::Editor &ed, long pos, turbo::lsp::PositionEncoding enc,
                  int &line, int &character) noexcept
{
    line = (int) ed.callScintilla(SCI_LINEFROMPOSITION, pos, 0U);
    long lineStart = ed.callScintilla(SCI_POSITIONFROMLINE, line, 0U);
    if (enc == turbo::lsp::PositionEncoding::UTF8)
    {
        character = (int) (pos - lineStart);
        return;
    }
    // UTF-16: count code units between the line start and pos.
    TStringView s = turbo::getRangePointer(ed.scintilla, lineStart, pos);
    int utf16 = 0;
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = (unsigned char) s[i];
        int len;
        if (c < 0x80)             len = 1;
        else if ((c >> 5) == 0x6) len = 2;
        else if ((c >> 4) == 0xE) len = 3;
        else if ((c >> 3) == 0x1E) len = 4;
        else                      len = 1;
        utf16 += (len == 4) ? 2 : 1;
        i += len;
    }
    character = utf16;
}

// Converts an LSP (line, character) position to a Scintilla byte offset.
// 'character' counts UTF-16 code units or UTF-8 bytes depending on the encoding
// negotiated with the server.
long lspToBytePos(turbo::Editor &ed, int line, int character,
                  turbo::lsp::PositionEncoding enc) noexcept
{
    long lineCount = ed.callScintilla(SCI_GETLINECOUNT, 0U, 0U);
    if (line < 0)
        line = 0;
    if (line >= lineCount)
        return ed.callScintilla(SCI_GETLENGTH, 0U, 0U);
    long lineStart = ed.callScintilla(SCI_POSITIONFROMLINE, line, 0U);
    long lineEnd = ed.callScintilla(SCI_GETLINEENDPOSITION, line, 0U);
    if (character <= 0)
        return lineStart;
    if (enc == turbo::lsp::PositionEncoding::UTF8)
    {
        long p = lineStart + character;
        return p < lineEnd ? p : lineEnd;
    }
    // UTF-16: walk the line's bytes, counting UTF-16 code units.
    TStringView s = turbo::getRangePointer(ed.scintilla, lineStart, lineEnd);
    long bytePos = lineStart;
    int utf16 = 0;
    size_t i = 0;
    while (i < s.size() && utf16 < character)
    {
        unsigned char c = (unsigned char) s[i];
        int len;
        if (c < 0x80)            len = 1;
        else if ((c >> 5) == 0x6) len = 2;
        else if ((c >> 4) == 0xE) len = 3;
        else if ((c >> 3) == 0x1E) len = 4;
        else                      len = 1;
        utf16 += (len == 4) ? 2 : 1; // astral planes take two UTF-16 units
        i += len;
        bytePos += len;
    }
    return bytePos > lineEnd ? lineEnd : bytePos;
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

void LspManager::configure(bool aEnabled,
                           std::vector<std::pair<std::string, std::string>> servers) noexcept
{
    enabled = aEnabled;
    configuredServers.clear();
    for (auto &s : servers)
        if (!s.first.empty() && !s.second.empty())
            configuredServers[s.first] = s.second;
    // A language that previously had no server may now be configured; clear the
    // negative cache so it gets another chance on the next didOpen.
    deadLanguages.clear();
}

LspManager::ServerConfig LspManager::serverFor(const std::string &languageId) noexcept
{
    auto fromCommandLine = [] (const std::string &cmdline) -> ServerConfig {
        ServerConfig cfg;
        auto parts = splitArgs(cmdline);
        if (!parts.empty())
        {
            cfg.command = parts[0];
            cfg.args.assign(parts.begin() + 1, parts.end());
        }
        return cfg;
    };

    // An environment override (TURBO_LSP_SERVER_<LANG>) takes precedence; this
    // makes the transport testable against a mock server independent of config.
    std::string envKey = "TURBO_LSP_SERVER_" + languageId;
    for (auto &c : envKey) c = (char) std::toupper((unsigned char) c);
    if (const char *ov = getenv(envKey.c_str()); ov && ov[0])
        return fromCommandLine(ov);

    // User-configured command (from ~/.turborc / the settings dialog).
    if (auto it = configuredServers.find(languageId); it != configuredServers.end())
        return fromCommandLine(it->second);

    // Built-in defaults.
    static const struct { const char *lang, *cmd, *args; } defaults[] = {
        {"cpp",        "clangd",                     ""},
        {"python",     "pyright-langserver",         "--stdio"},
        {"rust",       "rust-analyzer",              ""},
        {"go",         "gopls",                      ""},
        {"javascript", "typescript-language-server", "--stdio"},
        {"php",        "intelephense",               "--stdio"},
        {"elixir",     "expert",                     ""},
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

Json LspManager::initOptionsFor(const std::string &languageId) noexcept
{
    // intelephense will not index or publish diagnostics unless it is given a
    // writable 'storagePath'. Point it at a per-user cache directory (created if
    // needed). Other servers receive no special options.
    if (languageId == "php")
    {
        const char *home = getenv("HOME");
        if (!home || !home[0])
            home = getenv("USERPROFILE");
        std::string base = (home && home[0]) ? std::string(home) + "/.cache/turbo-lsp"
                                              : std::string("/tmp/turbo-lsp");
        std::string dir = base + "/intelephense";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return Json{
            {"storagePath", dir},
            {"globalStoragePath", dir},
        };
    }
    return Json(); // null -> omitted
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
    client->initializationOptions = initOptionsFor(languageId);
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
    if (!enabled)
        return;
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

    // Set up diagnostic annotation styles for this editor (idempotent).
    auto &ed = w.getEditor();
    setupAnnotations(ed);
    // Enable hover: SCN_DWELLSTART fires after the mouse rests this long (ms).
    ed.callScintilla(SCI_SETMOUSEDWELLTIME, 500, 0U);

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

EditorWindow *LspManager::findByUri(const std::string &uri) noexcept
{
    for (auto &kv : docs)
        if (kv.second.uri == uri)
            return kv.first;
    return nullptr;
}

void LspManager::applyDiagnostics(EditorWindow &w, const Json &diagnostics,
                                  turbo::lsp::PositionEncoding enc) noexcept
{
    auto &ed = w.getEditor();
    setupAnnotations(ed); // (re)assert visibility/colours in case theming changed

    auto &doc = docs[&w];
    doc.diagnostics.clear();

    for (auto &d : diagnostics)
    {
        if (!d.contains("range"))
            continue;
        const Json &r = d["range"];
        long start = lspToBytePos(ed, r["start"].value("line", 0),
                                  r["start"].value("character", 0), enc);
        long end = lspToBytePos(ed, r["end"].value("line", 0),
                                r["end"].value("character", 0), enc);
        if (end < start)
            std::swap(start, end);
        int severity = d.value("severity", 1);
        doc.diagnostics.push_back({start, end, severity, d.value("message", std::string())});
    }

    renderAnnotations(w);

    ed.redraw();
    logLine("applied " + std::to_string(doc.diagnostics.size()) +
            " diagnostic(s) to " + doc.uri);
}

void LspManager::renderAnnotations(EditorWindow &w) noexcept
{
    auto &ed = w.getEditor();
    Document *doc = docFor(w);
    if (!doc)
        return;

    ed.callScintilla(SCI_ANNOTATIONCLEARALL, 0U, 0U);

    // Group diagnostics by the (display) line their span starts on, preserving
    // document order so earlier diagnostics list first.
    std::map<long, std::vector<const Diagnostic *>> byLine;
    for (auto &d : doc->diagnostics)
    {
        long line = ed.callScintilla(SCI_LINEFROMPOSITION, d.start, 0U);
        byLine[line].push_back(&d);
    }

    for (auto &entry : byLine)
    {
        long line = entry.first;
        const auto &diags = entry.second;
        std::string text;
        int worst = 4; // 1=Error is "worst"; default to hint
        for (size_t i = 0; i < diags.size(); ++i)
        {
            const Diagnostic *d = diags[i];
            worst = std::min(worst, d->severity);
            // Column of the span start, and how many columns it covers on this
            // line (so the '~~~~' sits directly under the offending text).
            int startCol = (int) ed.callScintilla(SCI_GETCOLUMN, d->start, 0U);
            long endLine = ed.callScintilla(SCI_LINEFROMPOSITION, d->end, 0U);
            int endCol;
            if (endLine == line)
                endCol = (int) ed.callScintilla(SCI_GETCOLUMN, d->end, 0U);
            else
            {
                long lineEnd = ed.callScintilla(SCI_GETLINEENDPOSITION, line, 0U);
                endCol = (int) ed.callScintilla(SCI_GETCOLUMN, lineEnd, 0U);
            }
            int width = endCol - startCol;
            if (width < 1) width = 1;

            // One condensed message line (annotations don't wrap); collapse any
            // embedded newlines so multi-line server messages stay on one row.
            std::string msg = d->message;
            for (char &c : msg)
                if (c == '\n' || c == '\r' || c == '\t') c = ' ';

            if (i > 0)
                text += '\n';
            text.append((size_t) startCol, ' ');
            text.append((size_t) width, '~');
            text += ' ';
            text += msg;
        }
        ed.callScintilla(SCI_ANNOTATIONSETTEXT, line, (sptr_t) text.c_str());
        ed.callScintilla(SCI_ANNOTATIONSETSTYLE, line,
                         (worst == 1) ? kAnnoError : kAnnoWarning);
    }
}

void LspManager::onServerMessage(const std::string &languageId, const Json &msg) noexcept
{
    std::string method = msg.value("method", std::string());
    logLine("server[" + languageId + "] " + method);

    if (method == "textDocument/publishDiagnostics" && msg.contains("params"))
    {
        const Json &params = msg["params"];
        std::string uri = params.value("uri", std::string());
        EditorWindow *w = findByUri(uri);
        if (w)
        {
            auto it = docs.find(w);
            auto enc = it->second.client->positionEncoding();
            applyDiagnostics(*w, params.value("diagnostics", Json::array()), enc);
        }
    }
}

LspManager::Document *LspManager::docFor(EditorWindow &w) noexcept
{
    auto it = docs.find(&w);
    return it == docs.end() ? nullptr : &it->second;
}

Json LspManager::positionParams(EditorWindow &w, long pos) noexcept
{
    Document *doc = docFor(w);
    auto &ed = w.getEditor();
    int line = 0, character = 0;
    bytePosToLsp(ed, pos, doc->client->positionEncoding(), line, character);
    return Json{
        {"textDocument", {{"uri", doc->uri}}},
        {"position", {{"line", line}, {"character", character}}},
    };
}

void LspManager::sendCompletion(EditorWindow &w) noexcept
{
    Document *doc = docFor(w);
    if (!doc)
        return;
    // Flush any pending change so the server completes against current text.
    if (dirty.erase(&w))
        flushChange(w);
    auto &ed = w.getEditor();
    long pos = ed.callScintilla(SCI_GETCURRENTPOS, 0U, 0U);
    Json params = positionParams(w, pos);
    EditorWindow *wp = &w;
    doc->client->request("textDocument/completion", params,
        [this, wp](const Json &result, const Json *error) {
            if (error || result.is_null())
                return;
            // Result is either CompletionItem[] or {items: CompletionItem[]}.
            const Json *items = nullptr;
            if (result.is_array())
                items = &result;
            else if (result.contains("items") && result["items"].is_array())
                items = &result["items"];
            if (!items || items->empty())
                return;
            Document *doc = docFor(*wp);
            if (!doc) // editor closed while the request was in flight
                return;
            std::vector<std::string> labels;
            labels.reserve(items->size());
            for (auto &it : *items)
            {
                std::string label = it.value("label", std::string());
                // Some servers pad labels with a leading space; trim it.
                auto a = label.find_first_not_of(' ');
                if (a != std::string::npos)
                    label = label.substr(a);
                if (!label.empty())
                    labels.push_back(label);
            }
            if (labels.empty())
                return;
            std::sort(labels.begin(), labels.end());
            labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
            doc->pendingCompletions = std::move(labels);
            // Display happens on the main event loop, NOT here: this callback
            // runs inside idle()'s pump(), and opening a modal view from there
            // is unsafe. Post a command the app turns into showCompletion().
            if (TProgram::application)
            {
                TEvent ev {};
                ev.what = evCommand;
                ev.message.command = cmShowCompletion;
                ev.message.infoPtr = wp;
                TProgram::application->putEvent(ev);
            }
        });
}

void LspManager::showCompletion(EditorWindow &w) noexcept
{
    Document *doc = docFor(w);
    if (!doc || doc->pendingCompletions.empty())
        return;
    std::vector<std::string> labels = std::move(doc->pendingCompletions);
    doc->pendingCompletions.clear();

    // Build a list model over the labels. 'entries' references 'labels'; both
    // stay alive for the duration of the modal execView below.
    std::vector<SpanListModelEntry<int>> entries;
    entries.reserve(labels.size());
    for (int i = 0; i < (int) labels.size(); ++i)
        entries.push_back({ i, TStringView(labels[i]) });
    SpanListModel<int> model(
        TSpan<const SpanListModelEntry<int>>(entries.data(), entries.size()));

    TDeskTop *dt = TProgram::deskTop;
    if (!dt)
        return;
    TRect r {0, 0, 0, 0};
    r.b.x = min(max((int) ListModel::maxItemCStrLen(model) + 4, 20), dt->size.x - 6);
    r.b.y = min(max((int) model.size() + 2, 5), dt->size.y - 4);
    r.move((dt->size.x - r.b.x) / 2, (dt->size.y - r.b.y) / 3);

    auto &lw = ListWindow::create<EditorListView>(r, "Completions", model, lvScrollBars);
    int chosen = -1;
    if (dt->execView(&lw) == cmOK)
        if (auto *e = (SpanListModelEntry<int> *) lw.getCurrent())
            chosen = e->data;
    TObject::destroy(&lw);

    // The window may have closed during the modal loop.
    if (!docFor(w))
        return;
    if (chosen >= 0 && chosen < (int) labels.size())
    {
        auto &ed = w.getEditor();
        // Replace the partial word at the caret with the chosen completion.
        long cur = ed.callScintilla(SCI_GETCURRENTPOS, 0U, 0U);
        long wordStart = ed.callScintilla(SCI_WORDSTARTPOSITION, cur, true);
        ed.callScintilla(SCI_SETSEL, wordStart, cur);
        ed.callScintilla(SCI_REPLACESEL, 0U, (sptr_t) labels[chosen].c_str());
        ed.redraw();
    }
    w.focus();
}

void LspManager::charAdded(EditorWindow &w, int ch) noexcept
{
    // Auto-trigger completion only on member-access sequences ('.', '->', '::'),
    // mirroring how IDEs behave for e.g. "$this->". We deliberately do NOT
    // trigger on ordinary identifier characters: completions are shown in a
    // modal list, and popping that on every keystroke would be disruptive.
    // (Completions are no longer routed through Scintilla's SCI_AUTOCSHOW, which
    // activated the stub list box and froze the editor.)
    Document *doc = docFor(w);
    if (!doc)
        return;
    auto &ed = w.getEditor();
    bool trigger = (ch == '.');
    if (!trigger && (ch == '>' || ch == ':'))
    {
        long pos = ed.callScintilla(SCI_GETCURRENTPOS, 0U, 0U);
        if (pos >= 2)
        {
            char prev = (char) ed.callScintilla(SCI_GETCHARAT, pos - 2, 0U);
            trigger = (ch == '>' && prev == '-') || (ch == ':' && prev == ':');
        }
    }
    if (trigger)
        sendCompletion(w);
}

void LspManager::requestCompletion(EditorWindow &w) noexcept
{
    if (docFor(w))
        sendCompletion(w);
}

void LspManager::hover(EditorWindow &w, long pos) noexcept
{
    Document *doc = docFor(w);
    if (!doc)
        return;
    Json params = positionParams(w, pos);
    EditorWindow *wp = &w;
    doc->client->request("textDocument/hover", params,
        [this, wp, pos](const Json &result, const Json *error) {
            if (error || result.is_null() || !result.contains("contents"))
                return;
            // contents may be a string, a {kind,value} MarkupContent, or an
            // array of MarkedString.
            const Json &c = result["contents"];
            std::string text;
            if (c.is_string())
                text = c.get<std::string>();
            else if (c.is_object())
                text = c.value("value", std::string());
            else if (c.is_array() && !c.empty())
            {
                const Json &first = c[0];
                text = first.is_string() ? first.get<std::string>()
                                         : first.value("value", std::string());
            }
            if (text.empty())
                return;
            // Single line for the call tip; trim to a sane length.
            auto nl = text.find('\n');
            if (nl != std::string::npos)
                text = text.substr(0, nl);
            if (text.size() > 120)
                text = text.substr(0, 120);
            auto &ed = wp->getEditor();
            ed.callScintilla(SCI_CALLTIPSHOW, pos, (sptr_t) text.c_str());
        });
}

void LspManager::hoverEnd(EditorWindow &w) noexcept
{
    if (docFor(w))
        w.getEditor().callScintilla(SCI_CALLTIPCANCEL, 0U, 0U);
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
