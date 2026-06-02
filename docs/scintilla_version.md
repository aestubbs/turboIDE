# Scintilla version & upgrade

## Status: migrated to Scintilla 5.x + Lexilla (via submodules)

This fork previously vendored a copy of **Scintilla ~4.4.x** directly in the tree
(`source/scintilla/`, `include/turbo/scintilla/{include,src,lexlib}`). That copy
has been removed and replaced with the current Scintilla 5.x line and the
separate **Lexilla** lexer library, both pulled in as git submodules so they can
be updated with `git submodule update` instead of re-vendoring.

### What is used now

- **`deps/scintilla`** — `https://github.com/mirror/scintilla`, pinned at
  `rel-5-5-2`. (The absolute latest at time of writing is 5.6.2, but no git
  mirror carried it yet; bump the submodule when a mirror does.)
- **`deps/lexilla`** — `https://github.com/ScintillaOrg/lexilla`, pinned at
  `rel-5-4-9` (official org, latest tag).
- Clone with `--recursive`, or run `git submodule update --init` after pulling.

### How it builds (CMakeLists.txt)

- Target **`scintilla`** compiles `deps/scintilla/src/*.cxx` (platform-independent
  core only) with a Unity build.
- Target **`lexilla`** compiles `deps/lexilla/{lexlib,lexers}/*.cxx` +
  `deps/lexilla/src/Lexilla.cxx` with a PCH (lexers are not Unity-safe).
- Both link into `turbo-core` as object libraries, exactly as the old
  `scintilla`/`scilexers` targets did.
- The Scintilla/Lexilla **public** include dirs (`deps/scintilla/include`,
  `deps/lexilla/include`) are PUBLIC on `turbo-core` because `<turbo/scintilla.h>`
  exposes the Scintilla/Lexilla C API; `deps/scintilla/src` (internal headers) is
  PRIVATE.

### The TVision platform port (the load-bearing part)

Upstream Scintilla has no terminal backend, so turbo implements the Scintilla
platform abstractions itself. These were rewritten against the 5.x ABI:

- `source/turbo-core/platform/{surface,font,window,menu,listbox,platform}.cc`
  now implement the `Scintilla::Internal::{Surface,Font,Window,Menu,ListBox}`
  interfaces (`ColourRGBA` instead of `ColourDesired`, `Fill`/`Stroke`/
  `FillStroke`, `Font` as `std::shared_ptr` carrying the TVision cell style,
  `SurfaceMode`, `PopClip`, the UTF-8 text variants, etc.).
  `library.cc` was deleted (Scintilla 5.x has no `DynamicLibrary`/external
  lexers).
- `source/turbo-core/tscintilla.{h,cc}` — `TScintilla` moved into
  `Scintilla::Internal` and its overrides updated to the 5.x signatures
  (`NotificationData`, scoped `Message`/`Keys`/`KeyMod`, `CaseMapping`,
  `std::unique_ptr<CaseFolder>`, the new `UTF8FromEncoded`/`EncodedFromUTF8`
  pure virtuals). `WndProc` now takes a scoped `Scintilla::Message`, so the
  integer `SCI_*` macros are cast at the call sites.
- `source/turbo-core/scintilla.cc` — the C-API glue (`call`, `paint`,
  key/mouse handling, colour setters) updated for the scoped enums and
  `ColourRGBA`.

### Lexers (Lexilla split)

`source/turbo-core/editstates.cc` `applyTheming()` no longer uses the removed
`SCI_SETLEXER(SCLEX_*)`. It maps the table's `SCLEX_*` id to a Lexilla lexer name
(`lexerNameForId`) and installs the lexer via `Lexilla::CreateLexer(name)` +
`SCI_SETILEXER`. The `builtInLexers` table in `styles.cc` still keys off
`SCLEX_*` ids (those constants still exist in Lexilla's `SciLexer.h`); only the
application path changed. Note HTML maps to the `"hypertext"` lexer.

### Colours (the subtle one)

A TVision `TColorDesired` packs a colour-**type** tag (default / BIOS / RGB /
XTerm) into its top byte, and turbo's themes rely on it — the default scheme is
entirely BIOS colours (`'\x1'`, `'\xE'`, …). The old vendored Scintilla 4.x
stored the raw 32-bit colour int per style, so that tag round-tripped through
Scintilla untouched. Scintilla 5.x does **not**: `SCI_STYLESETFORE` runs the
value through `ColourRGBA::FromIpRGB` (mask to 24-bit RGB, force alpha) and
`SCI_STYLEGETFORE` returns `OpaqueRGB()`, so the type tag is destroyed and every
colour collapses to "terminal default" — i.e. no syntax colours at all.

Fix (`source/turbo-core/platform/surface.{h,cc}`): instead of sending the colour
bits to Scintilla, `convertColor` interns each `TColorDesired` into a small
integer **token** (`internTColor`) and hands Scintilla the token; when Scintilla
gives the colour back while drawing, `convertColor` resolves the token to the
full `TColorDesired` (`resolveTColor`). All existing call sites
(`setStyleColor`/`setSelectionColor`/`setWhitespaceColor` and the `DrawText`
path) go through `convertColor`, so they were unaffected. Token 0 is reserved for
the terminal default; unknown tokens (e.g. the raw RGB literals used for the
bookmark/change/edge colours, whose top byte is already 0/default) resolve to
default, matching the previous behaviour.

### Visible whitespace

`TScintilla`'s ctor sets `vs.whitespaceSize = 0`. Scintilla draws the
visible-space marker as a sub-cell dot rectangle (`whitespaceSize` pixels), but
in the terminal each "pixel" is a whole cell, so the default size of 1 fills the
entire space cell with the whitespace colour (a magenta block on every space).
Size 0 keeps spaces blank; tabs are still shown via the `"\t"` → `"»"`
representation.

### Indicators

The terminal `Surface` can't draw translucent indicators, so it recolours cells.
Scintilla 5.x's FULLBOX indicator passes a single `fore` colour to
`AlphaRectangle`, which can't carry both a fore and a back colour the way the old
`OUTLINEALPHA` smuggling did. Instead, `setIndicatorColor` smuggles the
*indicator number* through `INDICSETFORE` and stores the real fore/back in a
`std::map<int, TColorAttr>` on `TScintilla` (`indicatorColors`); `AlphaRectangle`
resolves the colours from that table at paint time.

## Follow-ups (optional)

- Switch the change-history margin to the native `SCI_SETCHANGEHISTORY` /
  `SC_MARKNUM_HISTORY_*` (added in 5.3) and drop the manual marker tracking in
  `editor.cc`. The migration kept the existing manual implementation.
- Bump `deps/scintilla` to 5.6.x once a git mirror carries it.
- Interactively verify the IDE features render correctly in a real terminal:
  folding, multi-cursor, bookmarks, change-history gutter, edge guide, and the
  LSP diagnostic/replace-highlight indicators (the reworked indicator path).
