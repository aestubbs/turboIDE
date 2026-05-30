# Plan: Language Server Protocol (LSP) support for Turbo

## Context

Turbo is a Turbo Vision + Scintilla IDE. Scintilla already provides the UI
primitives an LSP client needs — indicators (squiggles), autocompletion lists,
call tips, annotations, and dwell/hover notifications — but the app has **no LSP
client, no subprocess management, and no JSON parser**. Syntax highlighting today
is purely lexer-based (Lexilla); there is no semantic understanding of code.

This plan adds an LSP client so Turbo can show diagnostics, offer completions,
and display hover info from real language servers (clangd, pyright, rust-analyzer,
gopls, …), configurable per file type. Per the Scintilla maintainers' own
guidance, LSP belongs in a **companion layer that drives Scintilla**, not inside
Scintilla itself — which is exactly the shape proposed here.

**Decisions (confirmed):**
- **MVP features:** publishDiagnostics, completion, hover. **But** the transport,
  document-sync, and request/response core must be **general** so signatureHelp,
  goto-definition, find-references, rename, and formatting can be added later as
  thin handlers without touching the plumbing.
- **JSON:** vendor `nlohmann/json` as a **git submodule** (`deps/json`).
- **Configuration:** a **Settings dialog** (TDialog) backed by an extended
  `~/.turborc`, with sensible built-in defaults.

## Goals / non-goals

- **Goal:** a robust, non-blocking JSON-RPC transport to one server process per
  language; correct document lifecycle (didOpen/didChange/didSave/didClose);
  diagnostics, completion, hover wired to Scintilla; an extensible request layer.
- **Non-goal (this phase):** multi-root workspaces, workspace symbol search,
  semantic-tokens highlighting, code actions/quick-fixes, multiple servers per
  language. The design leaves room for all of these.

## Architecture overview

```
                 main thread (TV event loop)                 │ reader thread(s)
                                                             │
 EditorWindow ──notif──► LspManager ──requests──► LspClient ─┼─► server stdin
   (didOpen/Change/                 ◄──responses── (per lang)│   (clangd, …)
    Save/Close, hover,                  ▲                     │
    completion triggers)                │  drain in idle()    │   server stdout
                                  ThreadSafeQueue ◄───────────┴── reader parses
                                  (parsed JSON msgs)              Content-Length
                                                                  framed messages
```

- **`LspClient`** (one per running server): owns the child process, writes
  framed JSON-RPC to its stdin, and runs a **background reader thread** that
  parses `Content-Length`-framed messages off stdout into a thread-safe queue.
  Tracks pending request ids → callbacks.
- **`LspManager`** (one, owned by `TurboApp`): maps language → `LspClient`
  (lazily spawned on first didOpen for that language), routes editor lifecycle
  events to the right client, dispatches server messages to the right editor,
  and applies results to Scintilla.
- **Delivery to UI:** the reader thread never touches Scintilla. It only enqueues
  parsed messages. `TurboApp::idle()` (runs ~50×/s, see `eventTimeoutMs = 20` in
  `deps/tvision/source/tvision/tprogram.cpp:38`) calls `LspManager::pumpMessages()`
  to drain the queue and apply results on the main thread. This sidesteps all
  cross-thread UI hazards and the single-slot `TProgram::pending` limitation.
  *(Optional later optimization: use tvision's `WakeUpEventSource`/`wakeUp()` to
  cut the ≤20 ms latency; not needed for MVP.)*

### Why idle()-drain over thread event injection

`TProgram::pending` holds only one event; `TEventQueue::wakeUp()` is a no-op
except under `__FLAT__`. Polling a queue in `idle()` is simple, race-free, and
the 20 ms worst-case latency is imperceptible for diagnostics/hover/completion.
Heavy continuous typing can starve `idle()`, but LSP results during a burst of
typing are stale anyway (we debounce didChange — see below).

## New components (files)

All new `.cc` under `source/turbo-core/lsp/` are auto-collected by the existing
`GLOB_RECURSE` (CMakeLists.txt:95). Core, reusable transport lives in
**turbo-core**; app glue lives in **source/turbo/**.

**turbo-core (library) — transport & protocol, UI-agnostic:**
- `include/turbo/lsp/jsonrpc.h`, `source/turbo-core/lsp/jsonrpc.cc`
  Framing (read/write `Content-Length: N\r\n\r\n{json}`), message structs.
- `include/turbo/lsp/process.h`, `source/turbo-core/lsp/process.cc`
  Cross-platform child process with redirected stdio.
  - POSIX: `posix_spawn` + `pipe2`/`pipe`; non-blocking read loop.
  - Windows: `CreateProcessW` + anonymous pipes.
  - `#ifdef _WIN32` split, following `main.cc`'s existing convention.
- `include/turbo/lsp/client.h`, `source/turbo-core/lsp/client.cc`
  `LspClient`: spawn, `initialize`/`initialized` handshake, request/notify API,
  id→callback table, reader thread, `ThreadSafeQueue<json>` of inbound messages,
  `shutdown`/`exit` on teardown. **Knows nothing about Scintilla or TV.**

**turbo app — glue, Scintilla/UI, config:**
- `source/turbo/lspmanager.h`, `source/turbo/lspmanager.cc`
  `LspManager`: language→client map, lifecycle routing, `pumpMessages()`,
  and the result→Scintilla appliers (diagnostics/completion/hover). This is the
  one place that grows when new LSP features are added.
- `source/turbo/lspconfig.h`, `source/turbo/lspconfig.cc`
  `LspServerConfig { langId, command, args }`; built-in defaults + parse/serialize
  to `~/.turborc`.
- `source/turbo/lspdialog.h`, `source/turbo/lspdialog.cc`
  The Settings → "Language Servers…" dialog (TDialog + list + add/edit/remove).

**Dependency:**
- `deps/json` — nlohmann/json as a git submodule; `.gitmodules` entry; wired in
  `deps/CMakeLists.txt` (interface include dir only, header-only).

## Build / dependency wiring

- `.gitmodules`: add
  ```
  [submodule "deps/json"]
      path = deps/json
      url = https://github.com/nlohmann/json
  ```
- CMake: add `nlohmann_json::nlohmann_json` (or a plain include dir) to
  `turbo-core`. Gate the whole feature behind an option, mirroring the optional
  `MAGIC` pattern (CMakeLists.txt:134-138):
  ```cmake
  option(TURBO_ENABLE_LSP "Enable Language Server Protocol support" ON)
  if (TURBO_ENABLE_LSP)
      find_package(Threads REQUIRED)
      target_link_libraries(turbo-core PUBLIC Threads::Threads)
      target_include_directories(turbo-core PRIVATE deps/json/include)
      target_compile_definitions(turbo-core PUBLIC TURBO_ENABLE_LSP)
  endif()
  ```
  All new LSP code is wrapped in `#ifdef TURBO_ENABLE_LSP` so the app still builds
  with the feature off.
- **Unity-build caution:** turbo-core uses unity builds + PCH. Put helpers in a
  named `namespace turbo::lsp`, avoid anonymous-namespace/`static` symbol names
  that could collide across the concatenated TU (the codebase already prefers
  named namespaces — see `source/turbo-core/editstates.cc`).

## Integration points (existing code to hook)

These were confirmed during exploration; line numbers approximate.

| LSP event | Where to hook | Mechanism |
|-----------|---------------|-----------|
| **didOpen** | `TurboApp::addEditor` (`app.cc:~368`) after the editor exists and has a path | `LspManager::didOpen(editor)` |
| **didChange** | `EditorWindow::handleNotification` (`editwindow.cc:~263`) on `SCN_MODIFIED` with `SC_MOD_INSERTTEXT`/`SC_MOD_DELETETEXT` | debounced `LspManager::didChange` |
| **didSave** | same notification handler on `SCN_SAVEPOINTREACHED` (already handled there) | `LspManager::didSave` |
| **didClose** | `EditorWindow::shutDown` (`editwindow.cc:~95`) / `TurboApp::removeEditor` (`app.cc:~474`) | `LspManager::didClose` |
| **hover** | `EditorWindow::handleNotification` on `SCN_DWELLSTART` (enable via `SCI_SETMOUSEDWELLTIME`) | request `textDocument/hover` |
| **completion** | `EditorWindow::handleEvent` on `SCN_CHARADDED` (trigger chars) and/or a `cmAutoComplete` command | request `textDocument/completion` |
| **pump results** | `TurboApp::idle` (`app.cc:~200`, beside the clock update) | `LspManager::pumpMessages()` |

The `EditorParent`/`EditorWindowParent` interfaces
(`source/turbo/editwindow.h`) get one new accessor so editors can reach the
manager, e.g. `virtual LspManager *lspManager() noexcept;` implemented by
`TurboApp` (returns null when LSP disabled) — same pattern as the recently added
`autoSaveOnFocusLoss()`.

### Scintilla APIs used to apply results (companion-drives-Scintilla)

All via `editor.callScintilla(...)` (`include/turbo/editor.h`) /
`turbo::call(scintilla, …)` (`include/turbo/scintilla.h`):
- **Document text & positions:** `SCI_GETLENGTH`, `getRangePointer` (zero-copy,
  `source/turbo-core/scintilla.cc:238`), `SCI_LINEFROMPOSITION`,
  `SCI_POSITIONFROMLINE`, `SCI_GETCURRENTPOS`. LSP uses **UTF-16 line/character**
  positions → add `lsp::toUtf16`/`fromUtf16` helpers that convert against the
  document's UTF-8 bytes (clangd also supports `positionEncoding: "utf-8"`, which
  we request in client capabilities to avoid most conversion).
- **Diagnostics:** dedicate indicator ids (e.g. `idtrDiagError`,
  `idtrDiagWarn`, after `INDICATOR_CONTAINER`); `SCI_INDICSETSTYLE`=`INDIC_SQUIGGLE`,
  `SCI_SETINDICATORCURRENT`, `SCI_INDICATORCLEARRANGE`, `SCI_INDICATORFILLRANGE`.
  Store the diagnostic list per editor for hover/quick-fix reuse.
- **Completion:** build a space-separated list → `SCI_AUTOCSHOW`; on
  `SCN_AUTOCSELECTION` apply the textEdit.
- **Hover:** `SCI_CALLTIPSHOW` at the dwell position; `SCI_CALLTIPCANCEL` on
  `SCN_DWELLEND`.

## Document synchronization details

- Assign each editor a `file://` URI from `filePath()`; skip untitled buffers.
- Track a per-document **version** integer, incremented on each change.
- **didChange:** send **full-text** sync for the MVP (simplest, correct), with a
  TODO to switch to incremental ranges from `SCN_MODIFIED` fields once stable.
  Debounce ~250 ms in `idle()` so a burst of keystrokes yields one didChange.
- Guard against sending notifications for a language with no configured server
  (no-op) and for files outside any known root.

## Configuration

**`~/.turborc` (extends the current `key=value` format in `settings.cc`):**
```
autosave=1
lsp.enabled=1
lsp.server.cpp=clangd
lsp.server.python=pyright-langserver --stdio
lsp.server.rust=rust-analyzer
```
- `AppSettings` (`source/turbo/settings.h`) gains
  `bool lspEnabled` and `std::vector<LspServerConfig>`; `loadSettings`/
  `saveSettings` parse/emit the `lsp.*` lines. Language key = the existing
  `Language` id from `detectFileLanguage` (`source/turbo-core/styles.cc`), so
  server selection reuses Turbo's extension→language map.
- **Built-in defaults** seeded when no `lsp.server.*` present (clangd, pyright,
  rust-analyzer, gopls, typescript-language-server) — only ever spawned if the
  binary is found on `PATH`.

**Settings dialog** (Settings menu → "Language Servers…", new `cmLspSettings`):
- A `TDialog` listing configured `language → command` rows, with Add / Edit /
  Remove and an "Enable LSP" checkbox; OK writes back through `saveSettings`.
- Reuses the project's list/dialog idioms (`source/turbo/listviews.h`,
  `combobox.*`, `checkbox.*`). Menu wiring mirrors `cmToggleAutoSave`
  (`app.cc:154`, `app.cc:~327`).

## Lifecycle & robustness

- **Lazy spawn:** a server starts on the first didOpen of its language; the
  initialize/initialized handshake completes before queued notifications flush.
- **Crash handling:** reader-thread EOF / process exit marks the client dead,
  clears its diagnostics, and (bounded) auto-restarts on next activity.
- **Shutdown:** on app exit / last file of a language closing, send
  `shutdown`+`exit`, join the reader thread, close pipes. Hook in
  `TurboApp::shutDown` and `LspManager`'s destructor.
- **Capabilities:** advertise only what we implement; read server capabilities
  from the initialize result and skip unsupported requests.

## Phasing

1. **Transport core** — submodule + CMake; `jsonrpc`, `process`, `LspClient`
   with initialize handshake. Verify against a real server with a logging stub.
2. **Document sync** — `LspManager`, URIs/versions, didOpen/Change/Save/Close,
   `idle()` pump. No UI yet; log inbound messages.
3. **Diagnostics** — publishDiagnostics → squiggle indicators + stored list.
4. **Completion & hover** — SCN_CHARADDED/trigger → completion; dwell → hover.
5. **Config + dialog** — `~/.turborc` keys, defaults, Settings dialog.
6. **Extensible handlers (later)** — signatureHelp, goto-definition,
   find-references, rename, formatting: each is a new request + a result applier
   in `LspManager`, no transport changes. A small `cmGoToDefinition` etc. set of
   commands/menu items.

The split (generic transport in turbo-core, feature appliers isolated in
`LspManager`) is what makes phase 6 additive rather than invasive.

## Risks & mitigations

- **Blocking the UI:** all blocking I/O is on reader threads; main thread only
  drains a queue. Writes to stdin are small and buffered; if a write would block,
  hand it to the writer side of the client (queue + thread) too.
- **Position-encoding bugs:** request UTF-8 encoding; otherwise convert carefully
  with unit-tested helpers (see Verification).
- **Unity-build symbol clashes:** named namespaces, no loose statics.
- **Server not installed:** detect on `PATH`; if absent, silently disable that
  language and surface a one-line status message (no modal nagging).
- **Windows pipe/async differences:** isolate in `process.cc` behind the same
  interface; CI/build both paths.

## Verification

1. **Build both ways:** `cmake -DTURBO_ENABLE_LSP=ON .` and `=OFF`; `make turbo`.
   With OFF, no submodule/threads needed and the app is unchanged.
2. **Unit tests** (the repo has a gtest target, `TURBO_BUILD_TESTS`): framing
   round-trip in `jsonrpc`; UTF-8↔UTF-16 position conversion on multibyte text.
3. **End-to-end with clangd** (install via `brew install llvm`):
   - Open a `.cpp` with a deliberate error → red squiggle appears; fixing it and
     pausing clears it.
   - Type `obj.` → completion list from clangd; selecting inserts.
   - Hover an identifier → call tip with its type/declaration.
   - Save/close → server receives didSave/didClose (confirm via a `--log=verbose`
     clangd trace or the client's debug log).
4. **Resilience:** `kill` clangd mid-session → diagnostics clear, no crash, server
   respawns on next edit. Open a file type with no server → app behaves exactly
   as today.
5. **Config:** Settings → Language Servers… add a mapping, OK, reopen app →
   mapping persisted in `~/.turborc`.

## Key references

- Editor/Scintilla API: `include/turbo/editor.h`, `include/turbo/scintilla.h`,
  `source/turbo-core/scintilla.cc` (`getRangePointer`), notification dispatch in
  `source/turbo-core/editor.cc`.
- Event loop / idle cadence: `deps/tvision/source/tvision/tprogram.cpp:38,101,143`;
  cross-thread wake: `deps/tvision/include/tvision/internal/events.h`
  (`WakeUpEventSource`), `TEventQueue::wakeUp` (`tevent.cpp:461`).
- Lifecycle hooks: `source/turbo/app.cc` (`addEditor`, `idle`, `removeEditor`),
  `source/turbo/editwindow.cc` (`handleNotification`, `shutDown`).
- Settings to extend: `source/turbo/settings.{h,cc}`, menu in `app.cc`.
- Optional-dependency template: `CMakeLists.txt:134-138` (`MAGIC`); glob at `:95,143`.
- LSP spec 3.17: <https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/>
- Minimal C++ client references: bare-lsp <https://github.com/hzeller/bare-lsp>,
  lsp-framework <https://github.com/leon-bckl/lsp-framework>.
- Scintilla maintainer guidance (companion library, indicators for diagnostics):
  <https://sourceforge.net/p/scintilla/feature-requests/1330/>.
