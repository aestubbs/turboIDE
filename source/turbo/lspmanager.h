#ifndef TURBO_LSPMANAGER_H
#define TURBO_LSPMANAGER_H

struct EditorWindow;

#ifdef TURBO_ENABLE_LSP

#include <turbo/lsp/client.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Owns the language-server clients and bridges Turbo's editors to them.
// One client per language id; clients are spawned lazily on the first didOpen
// for their language. All public methods run on the main thread; pump() is
// called from the application's idle loop to deliver server messages.
class LspManager
{
public:
    LspManager() noexcept;
    ~LspManager();

    // The workspace root (used as the LSP rootUri). Call before opening files.
    void setRootPath(const char *path) noexcept;

    // Editor document lifecycle. No-ops for files whose language has no server.
    void didOpen(EditorWindow &w) noexcept;
    void didChange(EditorWindow &w) noexcept;   // marks the document dirty
    void didSave(EditorWindow &w) noexcept;
    void didClose(EditorWindow &w) noexcept;

    // Flush debounced changes and deliver queued server messages. Cheap to call
    // every idle tick.
    void pump() noexcept;

    // Stop every server (joins reader/writer threads). Call on app shutdown.
    void shutdown() noexcept;

private:
    struct ServerConfig
    {
        std::string command;
        std::vector<std::string> args;
        bool valid() const noexcept { return !command.empty(); }
    };

    struct Document
    {
        std::string uri;
        std::string languageId;
        int version {1};
        turbo::lsp::Client *client {nullptr};
    };

    // Lazily returns (spawning if needed) the client for 'languageId', or null
    // if no server is configured/available. A null result is cached so we don't
    // retry spawning on every keystroke.
    turbo::lsp::Client *clientFor(const std::string &languageId) noexcept;
    ServerConfig serverFor(const std::string &languageId) noexcept;
    void onServerMessage(const std::string &languageId, const turbo::lsp::Json &msg) noexcept;
    void flushChange(EditorWindow &w) noexcept;

    std::string rootUri;
    std::unordered_map<std::string, std::unique_ptr<turbo::lsp::Client>> clients;
    std::unordered_set<std::string> deadLanguages; // no server available
    std::unordered_map<EditorWindow *, Document> docs;
    std::unordered_set<EditorWindow *> dirty;
};

#else // !TURBO_ENABLE_LSP

// Stub so the rest of the app compiles unchanged when LSP is disabled.
class LspManager
{
public:
    void setRootPath(const char *) noexcept {}
    void didOpen(EditorWindow &) noexcept {}
    void didChange(EditorWindow &) noexcept {}
    void didSave(EditorWindow &) noexcept {}
    void didClose(EditorWindow &) noexcept {}
    void pump() noexcept {}
    void shutdown() noexcept {}
};

#endif // TURBO_ENABLE_LSP

#endif // TURBO_LSPMANAGER_H
