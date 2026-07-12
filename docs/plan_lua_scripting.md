# Lua scripting

Turbo embeds a [Lua 5.4](https://www.lua.org/) interpreter so the editor can be
configured and extended in Lua: run scripts on demand from the menu, and hook
into editor events (commit, save, file open/close, …).

## Where scripts live

Two homes — the same project/global split as agent skills (`.claude/skills`,
`~/.claude/skills`):

| Home | Path | Purpose |
| --- | --- | --- |
| **Project Lua** | `<project>/turbo-scripts/` | scripts and hooks for this project, checked in with the repository |
| **Global Lua** | `~/.turbo/` | scripts and hooks of your own, across all projects |

In each home:

* `init.lua` — run at startup (project first, then global). Use it to register
  event hooks with `turbo.on(...)`. Re-run from **Lua → Reload Config**.
* the runnable scripts — listed in **Lua → Run Script...**, in the command
  palette, and in the file tree. They sit beside `init.lua` in the project home
  (`turbo-scripts/*.lua`) and under `scripts/` in the global one
  (`~/.turbo/scripts/*.lua`).

`<project>` is the directory Turbo was started in (`TurboApp::projectRoot`).
Both homes are injected into the file tree as always-shown synthetic groups
("Project Lua", "Global Lua"), so each is a clear place to add a script even
when empty; `turbo-scripts/` is therefore not also listed as a plain folder.

`<project>/.turbo/` holds no scripts: it is a disposable per-user cache (session,
MCP socket, `config.json`) that Turbo keeps out of git with its own `.gitignore`.

## The `turbo` API

A global `turbo` table is installed in every script and hook. Current surface:

| Call | Effect |
| --- | --- |
| `turbo.message(s)` / `turbo.log(s)` | show a message box |
| `turbo.version()` | version string, e.g. `Turbo (Lua 5.4.8)` |
| `turbo.on(event, fn)` | register an event handler (see below) |
| `turbo.register_command(name, [desc,] fn)` | add a command to the palette that runs `fn` |
| `turbo.active_file()` | path of the focused editor, or `""` |
| `turbo.file_text()` | full text of the focused editor |
| `turbo.insert_text(s)` | insert text at the cursor |
| `turbo.open_file(path)` | open (or focus) a file |
| `turbo.save()` | save the focused editor |
| `turbo.run_command(id)` | dispatch a Turbo command id (the `cmXxx` values) |
| `turbo.shell(cmd)` | run a shell command in the project dir, return stdout |
| `turbo.project_root()` | the project directory |

The full Lua standard library (`string`, `table`, `math`, `io`, `os`, …) is
available too.

### Events

`turbo.on(name, fn)` registers `fn` for an event. The handler is called with a
`params` table; for `before*` events, returning `false` cancels the action.

| Event | When | `params` |
| --- | --- | --- |
| `newFile` | a new scratch buffer is created | — |
| `openFile` | a file is opened into an editor | `path` |
| `beforeSave` | just before a save writes to disk | `path` |
| `afterSave` | after a save completes | `path` |
| `closeFile` | an editor is closed | `path` |
| `beforeCommit` | after Commit is confirmed, before git runs (cancellable) | `message` |
| `afterCommit` | after the commit finishes | `ok` (`"true"`/`"false"`), `output` |

Every handler also receives `params.event` (the event name).

```lua
-- ~/.turbo/init.lua
turbo.on("afterSave", function(p)
  turbo.message("saved " .. p.path)
end)

turbo.on("beforeCommit", function(p)
  if p.message == "" then return false end   -- veto empty messages
end)
```

### Registering commands

`turbo.register_command(name, [description,] fn)` adds an entry to the **Command
Palette** (`Ctrl+B`) that runs `fn` when chosen. This is how scripts extend the
editor's command surface. (A future step will let registered commands also be
placed on menus.)

```lua
-- ~/.turbo/init.lua
turbo.register_command("Insert date", "insert today's date", function()
  turbo.insert_text(turbo.shell("date +%Y-%m-%d"))
end)
```

The palette also lists every discovered script (`Lua Script: <name>`), so scripts
are launchable from it without registering anything. Reloading the init scripts
(Lua → Reload Config) replaces both the event handlers and the registered
commands rather than stacking duplicates.

> **Project rule:** every command added to the application must be reachable from
> Lua (dispatchable via `turbo.run_command`) **and** listed in the Command
> Palette (`commandpalette.cc`'s `kCommands`). New `cmXxx` commands are not done
> until both are true.

## Implementation

* **Build** — `deps/lua` is a git submodule pinned to `v5.4.8`. Its C sources are
  compiled into a `lua` OBJECT library (`CMakeLists.txt`, mirroring `vterm`) and
  folded into the `turbo` executable. `lua.c`/`onelua.c`/`ltests.c` are excluded
  (`main()` / amalgamation / test harness). `LUA_USE_POSIX` is set on Unix.
* **Engine** — `source/turbo/luamanager.{h,cc}` owns the `lua_State`, installs the
  `turbo` table, keeps an event-handler registry, runs scripts, and dispatches
  events. It is decoupled from `TurboApp` via a `LuaHost` callback struct, and is
  the only TU that includes the Lua headers (kept out of the unity batch).
* **Editing** — `.lua` already maps to `Language::Lua`; the Lexilla `lua` lexer is
  wired up in `styles.cc`/`editstates.cc`. Lua editor windows are a distinctive
  warm-brown surface — frame *and* text background — in two shades (a brighter
  active shade when focused, a dimmer passive shade when not). The text background
  is set in `EditorWindow::applyActiveStateTheme`; the frame/scrollbar chrome comes
  from a brown `WindowColorScheme` (`luaBrownScheme`) applied via `setScheme`.
* **Menu** — a top-level **Lua** menu (`makeMenuBar`) with Run Script, New Script,
  Reload Config and Show Scripts in Tree.
* **Events** — fired from `TurboApp` (`addEditor`, `editorWillSave`, `editorSaved`,
  `removeEditor`) and the commit dialog (`executeGitCommitDialog` gained optional
  `beforeCommit`/`afterCommit` callbacks).
* **Tree** — `DocumentTreeView::setLuaScripts` injects a synthetic, re-buildable
  "Lua Scripts" section (project + global) whose children carry the scripts' real
  paths, so they open and link to editors like any file.

## Limitations / future work

* Scripts run synchronously on the UI thread; a long script blocks the UI.
* `beforeCommit`/`afterCommit` were wired but not yet exercised under a live repo
  in automated testing (the rest of the events are runtime-verified).
* Only string event params are passed today.
* No sandboxing — scripts have full `io`/`os` access (they are the user's own).
* Reloading `init.lua` resets all registered handlers (so reload replaces, not
  stacks); handlers registered outside `init.lua` are dropped on reload.
