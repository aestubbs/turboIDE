# Plan: Git integration

## Context

Turbo is becoming a terminal IDE. Git integration is the next major feature:
developers expect to see at a glance which files have changed, commit without
leaving the editor, and have the file tree stay truthful as files change on disk
(from saves, from git operations like checkout, and from other tools running in
parallel). This plan covers how we add that.

**Confirmed decisions:**
- **Backend:** shell out to the `git` CLI (no libgit2). Reuses the existing
  cross-platform process runner; matches user expectations; avoids another
  vendored dependency to version (the maintenance burden we deliberately avoided
  with Scintilla — see `docs/scintilla_version.md`).
- **Scope:** full, including push/pull/fetch.
- **Change detection:** real filesystem watching (FSEvents / inotify /
  ReadDirectoryChangesW), not just polling.
- **Credentials:** rely on the user's configured credential helper / SSH agent.
  We never prompt interactively inside the TUI; network ops run non-interactively
  and fail gracefully. We assume git works on the command line.

## What we build on (already in the tree)

- **`lsp::Process`** (`include/turbo/lsp/process.h`, `source/turbo-core/lsp/process.cc`):
  cross-platform child process with redirected stdio (`posix_spawn`/`CreateProcess`),
  blocking `readStdout`/`writeStdin`, `terminate`, `running`. Exactly the runner
  git needs — but currently gated behind `#ifdef TURBO_ENABLE_LSP` and in the
  `lsp` namespace.
- **Idle-pump threading model** (LSP): worker threads never touch the UI; results
  are posted and drained on the main thread from `TurboApp::idle()` (runs ~50×/s,
  `eventTimeoutMs = 20`). Git status and network ops follow the same model.
- **The file tree** (`source/turbo/doctree.{h,cc}`): `DocumentTreeView` over
  `TOutline`. Nodes hold `path` / `isDir` / `editor`. Built **once** by
  `scanDirectory` at startup; `draw()`/`refreshText()` already render per-node
  text with colour/bold; helpers `findByPath` / `getDirNode` exist. Two gaps to
  close: no per-node git status field, and no incremental add/remove/refresh
  (today it is a one-shot full scan).
- **Settings + menu conventions** (`app.cc`, `settings.{h,cc}`): how to add menu
  items, commands (`cmds.h`), and persisted `~/.turborc` options.

## Architecture

Three new subsystems, all off-main-thread, drained in `idle()` — mirroring LSP.

```
 worker threads                main thread (TV event loop, idle pump)
 ───────────────               ──────────────────────────────────────
 GitClient    ── status/branch ─► applied to tree nodes + status bar
 (git CLI)    ── op results ────►
                                 │
 FS watcher   ── changed paths ─► incremental tree add/remove/refresh
 (per-OS)                         + trigger GitClient re-status
```

### 0. Refactor: promote `Process` to shared `turbo::Process`

Move the process runner out of the `lsp` namespace and the `TURBO_ENABLE_LSP`
guard into an always-built `turbo::Process` in turbo-core
(`include/turbo/process.h`, `source/turbo-core/process.cc`). Have `lsp::Client`
use it. No behaviour change; this is the enabler for everything below and keeps
one process abstraction.

### 1. `GitClient` (turbo-core) — run git, parse porcelain

- Discover the repo root (`git rev-parse --show-toplevel`); if the workspace is
  not a git repo, the whole feature stays dormant (no indicators, menu items
  disabled).
- **Status in one pass:**
  `git status --porcelain=v2 --branch -z --untracked-files=all`
  → parse into `{ path → GitFileStatus }` (modified, added, deleted, untracked,
  staged, renamed, conflicted; track worktree vs. index state separately) plus
  `{ branch, upstream, ahead, behind }`. NUL-delimited (`-z`) so filenames with
  spaces/newlines are safe; `=v2` is the stable machine format.
- **Mutations:** stage (`git add -- <paths>`), unstage (`git restore --staged`),
  commit (`git commit -m <msg>`, or staged-only), and remote ops
  (`fetch`/`pull`/`push`).
- Runs each invocation on a worker thread; the parsed result is posted to the
  main thread and applied there. Status on a large repo isn't instant and
  network ops certainly aren't, so nothing blocks the UI.

### 2. Filesystem watcher (turbo-core)

One interface, three backends behind `#ifdef` (the `process.cc` convention):
- macOS: **FSEvents**; Linux: **inotify** (recursive — add watches per dir);
  Windows: **ReadDirectoryChangesW**.
- Watcher thread coalesces/debounces (~200 ms) and posts a batch of changed
  paths to the main loop.
- **Two trigger classes, handled differently:**
  - Paths under the worktree → tree node add/remove/refresh **and** re-run git
    status (a new/deleted file changes both the tree and its status).
  - `.git/HEAD`, `.git/index`, `.git/refs/**` → branch/status changed (a commit,
    a checkout, or external `git` use). **Ignore `.git/objects/**`** — high churn,
    no UI meaning.
- Respect `.gitignore` for tree display where practical (git status already
  excludes ignored files; the watcher should not spam refreshes for ignored
  build output — filter via the status result rather than watching everything).

### 3. Tree: status field + incremental updates

- Add `GitFileStatus gitStatus` to `Node`. `draw()`/`refreshText()` render a
  one-char badge + colour: `M` amber (modified), `A`/`?` green (added/untracked),
  `D` red (deleted), `U`/`!` (conflict), staged shown distinctly (e.g. reverse or
  a second column). Directories get a **rollup** marker (a dot) when anything
  inside has changed, so status is visible while collapsed.
- Replace full rescans with incremental ops: `addNode(path)`, `removeNode(path)`,
  `refreshNode(path)` using `findByPath`/`getDirNode`. This preserves expand /
  collapse / scroll / selection state (a full `scanDirectory` would lose it and
  is O(repo)). This is the core of "the tree auto-updates as files change and as
  other subsystems add files."
- Keep `scanDirectory` for the initial build and a manual "hard refresh".

### 4. UI surface

- **Status bar:** `⎇ <branch> ↑<ahead>↓<behind>` (plus a `*` when the worktree is
  dirty). Reuses the existing status-line update path.
- **Commit dialog** (`TDialog`): a checklist of changed files (pre-checked =
  staged), a multi-line message input, and Commit / Cancel. OK stages the
  checked set and runs `git commit`.
- **Git menu** (new submenu) + commands in `cmds.h`: Stage, Unstage, Commit…,
  Push, Pull, Fetch, Refresh. Disabled when the workspace isn't a repo.
- **Config** (`~/.turborc`): `git.enabled` (default on), and an option to
  auto-fetch on open (default off).

### Credentials & network safety (the load-bearing constraint)

Network ops must never hang the UI on a prompt:
- Run with `GIT_TERMINAL_PROMPT=0` and a no-op `GIT_ASKPASS`/`SSH_ASKPASS`, so
  git cannot block reading from the terminal.
- Authentication is provided entirely by the user's credential helper / SSH
  agent (assumed working, since git works on their command line).
- On auth/other failure, surface a clear status-bar/dialog error
  ("authentication required — configure a credential helper") rather than
  hanging. Interactive in-TUI credential entry is explicitly out of scope.

## Phasing (each independently shippable; commit per phase)

1. **Refactor** — promote `Process` to `turbo::Process`; LSP reuses it. No
   behaviour change.
2. **Status (read-only)** — `GitClient` status parse → tree indicators +
   status-bar branch/ahead-behind. Refresh on save / focus-regain / manual key.
3. **FS watcher** — real watching + incremental tree updates (worktree + `.git`
   refs/index). Removes the need for manual refresh.
4. **Commit** — staging dialog + commit.
5. **Remote** — push / pull / fetch with the non-interactive credential policy
   above.

## Risks & mitigations

- **UI hang on credential prompt** → non-interactive env + graceful failure
  (above). The single most important safety property.
- **Large-repo status latency** → always off-thread; debounce; coalesce bursts.
- **Tree state loss** → incremental updates, never full rescans on change.
- **inotify recursive-watch limits / FSEvents coalescing differences** → cap
  watch depth, rely on git status as the source of truth and treat watcher
  events only as "something changed, re-query" hints (not authoritative diffs).
- **Watcher feedback loops** (our own writes trigger events) → debounce and
  re-query rather than acting on individual events.
- **Unmaintained-upstream divergence** — this is all additive app code in
  `source/turbo/` + turbo-core; it does not touch Scintilla, so it does not
  worsen the Scintilla-upgrade story.

## Verification

- **Unit:** porcelain=v2 parser against captured fixtures (renames, spaces in
  names, conflicts, staged+unstaged same file).
- **Headless:** drive `GitClient` against a throwaway repo created in `/tmp`
  (init, add, commit, branch, modify) and assert the parsed status/branch.
- **FS watcher:** create/modify/delete files in a temp dir; assert the watcher
  posts the expected paths; assert the tree adds/removes nodes without losing
  expand state.
- **End-to-end (pty):** open the app in a temp repo, edit a file → status badge
  appears; commit via dialog → badge clears, status bar updates; `git checkout`
  in another shell → branch in status bar updates without a keypress.
- **Remote:** push/pull against a local bare repo over `file://` (no creds); then
  confirm graceful failure when pointed at an auth-required remote with no helper.
- Build with the feature flag on and off.

## Key references

- Process runner to promote: `include/turbo/lsp/process.h`,
  `source/turbo-core/lsp/process.cc`.
- Idle pump / threading precedent: `source/turbo/lspmanager.cc`,
  `TurboApp::idle()` in `source/turbo/app.cc`.
- Tree to extend: `source/turbo/doctree.{h,cc}` (`Node`, `scanDirectory`,
  `findByPath`, `getDirNode`, `draw`/`refreshText`).
- Menu / command / settings conventions: `source/turbo/app.cc`,
  `source/turbo/cmds.h`, `source/turbo/settings.{h,cc}`.
