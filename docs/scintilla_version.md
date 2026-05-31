# Scintilla version & upgrade plan

## Context

This fork adds IDE features on top of the editor, several of which lean on
Scintilla capabilities. While adding the change-history margin we discovered the
vendored Scintilla **predates `SCI_SETCHANGEHISTORY`**, which prompted the
question of whether to update to a current Scintilla to stay maintained. This
note records what we found and the decision.

## What is vendored today

- **Copied source, not a submodule.** Scintilla lives directly in the tree at
  `source/scintilla/` (`src/`, `lexlib/`, 114 `lexers/Lex*.cxx`) and
  `include/turbo/scintilla/`. `.gitmodules` only tracks `deps/tvision` and
  `deps/json` — there is no Scintilla submodule to bump.
- **Version: ~Scintilla 4.4.x (mid-2020).** Fingerprints:
  - Has both `SCI_SETLEXER` (old) **and** `SCI_SETILEXER` + `ILexer5` — the
    transitional state late in the 4.4 series.
  - Lexers are still **bundled in core** with `Catalogue.cxx`/`LexerModule`,
    i.e. before the **Lexilla** split that happened at **5.0**.
  - No `SCI_SETCHANGEHISTORY` (that landed in **5.3**, 2022). Current upstream
    is **5.5.x**, so we are ~3 feature-eras behind.
- **Custom platform layer.** turbo-core implements Scintilla's `Surface`,
  `Window`, `ListBox`, `Font` abstractions itself in
  `source/turbo-core/platform/*.cc` and `source/turbo-core/tscintilla.cc`
  (subclassing `ScintillaBase`). This is the load-bearing coupling and the part
  that makes an upgrade a re-port rather than a version bump.

## Why an upgrade is a re-port (the cost)

Updating to Scintilla 5.x is a breaking migration, not a refresh:

1. **Lexilla split (5.0).** Scintilla 5 removed all lexers from core. We would
   add Lexilla as a new dependency and rewrite lexer setup from
   `SCI_SETLEXER(SCLEX_CPP)` to `Lexilla::CreateLexer("cpp")` + `SCI_SETILEXER`.
   The `source/turbo-core/styles.cc` lexer tables key off integer `SCLEX_*` IDs
   that no longer exist; they must be re-keyed to lexer **names**.
2. **Platform ABI changes (4 → 5).** The abstract `Surface`/`Window` base
   classes changed signatures (`ColourDesired` → `ColourRGBA`, scoped enums in
   the new `ScintillaTypes.h`, `std::string_view` text APIs, etc.). Effectively
   every file under `source/turbo-core/platform/` plus `tscintilla.cc` must be
   rewritten — the same delicate work as the original terminal port.
3. **Divergence risk** if upstream were active (see decision below).

## Decision

**Defer now; plan to take on the full Scintilla 5.x + Lexilla migration
(option 4) ourselves when we revisit.**

Rationale: upstream [`magiblot/turbo`](https://github.com/magiblot/turbo) does
not appear to be seriously maintained, so we should not expect it to perform the
Scintilla 5 re-port for us. That removes the main argument for staying close to
upstream — which means if we want to remain current, **owning the migration is
the only realistic option**. We accept the resulting divergence from upstream as
a deliberate consequence of that.

For the immediate term we stay on the vendored ~4.4.x and keep the manual
change-history implementation (markers added on `SCN_MODIFIED`, cleared on
`SCN_SAVEPOINTREACHED` — see `source/turbo-core/editor.cc`), which delivers the
modified-lines gutter without the new API.

## When we revisit (scope checklist for the migration)

- Add Scintilla 5.x + **Lexilla** (decide: vendored copy vs. submodule, matching
  the json/tvision convention).
- Rewrite the platform layer (`source/turbo-core/platform/*.cc`,
  `tscintilla.cc`) against the 5.x `Surface`/`Window`/`ListBox` ABI
  (`ColourRGBA`, `ScintillaTypes.h` scoped enums, string_view APIs).
- Re-key `styles.cc` lexer setup from `SCLEX_*` IDs to Lexilla lexer **names**
  via `CreateLexer` + `SCI_SETILEXER`.
- Switch the change-history margin to the native `SCI_SETCHANGEHISTORY` /
  `SC_MARKNUM_HISTORY_*` and drop the manual marker tracking.
- Re-verify all five IDE features (folding, multi-cursor, bookmarks, change
  history, edge guide) and the LSP decorations still render in the terminal
  Surface, which only implements cell fills + text (no line/pixmap primitives).
- Do this as its own planning cycle with a written migration plan first; it is
  multi-day and touches the most fragile part of the codebase.
