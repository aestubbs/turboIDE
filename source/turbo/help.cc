#include "help.h"
#include <tvision/help.h>

#define Uses_TButton
#define Uses_TDialog
#define Uses_TGroup
#define Uses_TStaticText
#include <tvision/tv.h>

#include "cmds.h"
#include <sstream>

static constexpr TStringView aboutDialogText =
    "\003turboIDE"
#ifdef TURBO_VERSION_STRING
    " (build " TURBO_VERSION_STRING ")"
#endif
    "\n\n"
    "\003A lightweight terminal IDE based on Scintilla and Turbo Vision\n\n"
    "\003https://github.com/aestubbs/turboIDE";

// Since we do not need cross-references, we can easily create a THelpFile
// on-the-fly and define topics manually instead of using TVHC.

static constexpr TStringView helpParagraphs[] =
{
    // Contents. (Leading-space paragraphs are shown verbatim; the others wrap.)
    "turboIDE's built-in help, in three sections. Scroll with the Down arrow, PgDn "
    "or the scrollbar to reach each one.\n\n",
    "   1.  Keyboard & shortcuts\n"
    "   2.  Features   (Git, multiple cursors, terminal, ...)\n"
    "   3.  Lua scripting\n",
    "  1 · Keyboard & shortcuts ▄\n"
    " ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀",
    "This table shows turboIDE's commands and their associated keyboard shortcuts."
    "\n\n",
    "Some commands can be triggered by more than one shortcut. Some keyboard "
    "shortcuts may not be supported by the console.\n\n",
    "Commands without a key combination are reached from the menu bar; their "
    "'Shortcuts' cell names the menu (e.g. \"Settings menu\") or how they are "
    "invoked (e.g. \"Title-bar click\").\n\n",
    " ┌─────────────┬────────────────────────┬────────────────────┐\n"
    " │  Category   │        Command         │     Shortcuts      │\n"
    " ╞═════════════╪════════════════════════╪════════════════════╡\n"
    " │ Application │ Focus menu             │ F12                │\n"
    " │ control     │ Exit                   │ ┬─ Ctrl+Q          │\n"
    " │             │                        │ └─ Alt+X           │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ File        │ New                    │ Ctrl+N             │\n"
    " │ management  │ Open                   │ Ctrl+O             │\n"
    " │             │ Save                   │ Ctrl+S             │\n"
    " │             │ Save as                │ File menu          │\n"
    " │             │ Close                  │ Ctrl+W             │\n"
    " │             │ Close all              │ File menu          │\n"
    " │             │ Rename                 │ F2                 │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Editing     │ Copy                   │ ┬─ Ctrl+C          │\n"
    " │             │                        │ └─ Ctrl+Ins        │\n"
    " │             │ Paste                  │ ┬─ Ctrl+V          │\n"
    " │             │                        │ └─ Shift+Ins       │\n"
    " │             │ Cut                    │ ┬─ Ctrl+X          │\n"
    " │             │                        │ └─ Shift+Del       │\n"
    " │             │ Undo                   │ Ctrl+Z             │\n"
    " │             │ Redo                   │ ┬─ Ctrl+Y          │\n"
    " │             │                        │ └─ Ctrl+Shift+Z    │\n"
    " │             │ Indent                 │ Tab                │\n"
    " │             │ Unindent               │ Shift+Tab          │\n"
    " │             │ Toggle comment         │ ┬─ Ctrl+E          │\n"
    " │             │                        │ ├─ Ctrl+/          │\n"
    " │             │                        │ └─ Ctrl+_          │\n"
    " │             │ Select all             │ Ctrl+A             │\n"
    " │             │ Delete current line    │ Ctrl+K             │\n"
    " │             │ Cut current line       │ Ctrl+L             │\n"
    " │             │ Word left              │ ┬─ Ctrl+Left       │\n"
    " │             │                        │ └─ Alt+Left        │\n"
    " │             │ Word right             │ ┬─ Ctrl+Right      │\n"
    " │             │                        │ └─ Alt+Right       │\n"
    " │             │ Erase word left        │ ┬─ Ctrl+Back       │\n"
    " │             │                        │ └─ Alt+Back        │\n"
    " │             │ Erase word right       │ ┬─ Ctrl+Del        │\n"
    " │             │                        │ └─ Alt+Del         │\n"
    " │             │ Move lines up          │ Alt+Shift+Up       │\n"
    " │             │ Move lines down        │ Alt+Shift+Down     │\n"
    " │             │ Scroll up one line     │ Ctrl+Up            │\n"
    " │             │ Scroll down one line   │ Ctrl+Down          │\n"
    " │             │ Uppercase selection    │ Selection menu     │\n"
    " │             │ Lowercase selection    │ Selection menu     │\n"
    " │             │ Capitalize selection   │ Selection menu     │\n"
    " │             │ Trigger completion     │ Ctrl+Space         │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Search      │ 'Find' panel           │ Ctrl+F             │\n"
    " │             │ Find next              │ F3                 │\n"
    " │             │ Find previous          │ Shift+F3           │\n"
    " │             │ 'Replace' panel        │ Ctrl+R             │\n"
    " │             │ 'Go To Line' panel     │ Ctrl+G             │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Navigation  │ Goto Anything          │ Ctrl+P             │\n"
    " │             │ Command palette        │ Ctrl+B             │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Multiple    │ Select next occurrence │ Ctrl+D             │\n"
    " │ cursors     │ Select all occurrences │ Selection menu     │\n"
    " │             │ Skip occurrence        │ Selection menu     │\n"
    " │             │ Undo last selection    │ Ctrl+U             │\n"
    " │             │ Add caret above        │ Ctrl+Alt+Up        │\n"
    " │             │ Add caret below        │ Ctrl+Alt+Down      │\n"
    " │             │ Column selection       │ Ctrl+Shift+Arrows  │\n"
    " │             │ Split into lines       │ Selection menu     │\n"
    " │             │ Collapse to one caret  │ Esc                │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Code        │ Toggle fold margin     │ Code menu          │\n"
    " │ folding     │ Toggle fold at cursor  │ Code menu          │\n"
    " │             │ Fold all               │ Code menu          │\n"
    " │             │ Unfold all             │ Code menu          │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Bookmarks   │ Toggle bookmark        │ Code menu          │\n"
    " │             │ Next bookmark          │ Code menu          │\n"
    " │             │ Previous bookmark      │ Code menu          │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ View &      │ Toggle line numbers    │ F8                 │\n"
    " │ settings    │ Toggle line wrapping   │ F9                 │\n"
    " │             │ Toggle auto indent     │ Settings menu      │\n"
    " │             │ Toggle file tree       │ Settings menu      │\n"
    " │             │ Show hidden files      │ Settings menu      │\n"
    " │             │ Toggle auto-save       │ Settings menu      │\n"
    " │             │ Toggle change history  │ Settings menu      │\n"
    " │             │ Toggle long-line guide │ Settings menu      │\n"
    " │             │ Language servers...    │ Settings menu      │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Window      │ Next editor (MRU)      │ ┬─ F6              │\n"
    " │ management  │                        │ ├─ Ctrl+Tab        │\n"
    " │             │                        │ └─ Alt+Tab         │\n"
    " │             │ Previous editor (MRU)  │ ┬─ Shift+F6        │\n"
    " │             │                        │ ├─ Ctrl+Shift+Tab  │\n"
    " │             │                        │ └─ Alt+Shift+Tab   │\n"
    " │             │ Next editor (tree)     │ Alt+Down           │\n"
    " │             │ Previous editor (tree) │ Alt+Up             │\n"
    " │             │ Select window 1..9     │ Alt+1 .. Alt+9     │\n"
    " │             │ Recent windows         │ Windows menu       │\n"
    " │             │ Zoom                   │ F5                 │\n"
    " │             │ Resize/move            │ Ctrl+F5            │\n"
    " │             │ Close window           │ Alt+F3             │\n"
    " │             │ Reveal in tree         │ Title-bar click    │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Git         │ Commit...              │ Git menu           │\n"
    " │             │ Fetch                  │ Git menu           │\n"
    " │             │ Pull                   │ Git menu           │\n"
    " │             │ Push                   │ Git menu           │\n"
    " │             │ Refresh status         │ Git menu           │\n"
    " │             │ Switch branch          │ Menu bar           │\n"
    " │             │ New branch...          │ Git menu/menu bar  │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Lua         │ Run script             │ Lua menu / palette │\n"
    " │ scripting   │ New script             │ Lua menu           │\n"
    " │             │ Reload config          │ Lua menu           │\n"
    " │             │ Show scripts in tree   │ Lua menu           │\n"
    " ├─────────────┼────────────────────────┼────────────────────┤\n"
    " │ Tools       │ New terminal           │ File menu          │\n"
    " └─────────────┴────────────────────────┴────────────────────┘\n",
    // These trailing prose paragraphs are kept separate from the table above so
    // they start with a non-space character and therefore wrap to the window
    // width (the table paragraph must not wrap, hence its leading spaces).
    "\nGoto Anything (Ctrl+P) is a fuzzy finder for the whole project. Start "
    "typing part of a file name and press Enter to open the highlighted file; a "
    "preview of it is shown on the right. Prefix the query with ':' to jump to a "
    "line in the current file (for example ':120'), or with '@' to jump to a "
    "symbol in it (for example '@parse'); a file query may also end with ':N' to "
    "open that file at line N. Recently and frequently opened files rank higher."
    "\n\n",
    "The Command Palette (Ctrl+B, or the File menu) lists every command by name "
    "so you can run it without hunting through the menus -- type to filter, then "
    "press Enter. Commands that need an open editor are shown dimmed when there "
    "is none. It also lists your Lua scripts (\"Lua Script: <name>\") and any "
    "commands a script has registered (see section 3), so those are launchable "
    "the same way.\n\n",
    "  2 · Features ▄\n"
    " ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀",
    "Multiple cursors: press Ctrl+D to select the word at the caret, and again "
    "to add the next occurrence -- then type to edit every selection at once. "
    "Ctrl+Alt+Up and Ctrl+Alt+Down add a caret on the line above or below (also "
    "on the Selection menu, the fallback where the terminal does not send those "
    "combinations). Ctrl+Shift+Arrows make a rectangular (column) selection. "
    "'Split into Lines' on the Selection menu turns a multi-line selection into "
    "one caret per line, and Esc collapses back to a single caret.\n\n",
    "\nThe Git menu's commit/fetch/pull/push and the branch shown in the menu "
    "bar (click it to switch or create branches) operate on the workspace "
    "repository. "
    "The file tree shows per-file Git status badges; right-click a file for "
    "Open, Rename, New File and Git Add / Git Revert.\n\n",
    "File > New Terminal opens a shell in a window. Set 'terminal.shell' in "
    "~/.turborc to choose which shell (the default is $SHELL). While a terminal "
    "is focused, keystrokes go to the shell -- including Ctrl+C, Ctrl+D, Ctrl+R "
    "and Ctrl+Z -- except the menu accelerators (F1, F12, Ctrl+Q and "
    "Ctrl+N/O/S), which still drive the editor.\n\n",

    "  3 · Lua scripting ▄\n"
    " ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀",
    "turboIDE embeds a Lua 5.4 interpreter, so you can configure and extend the "
    "editor in Lua: run scripts on demand, and hook into editor events such as "
    "save and commit. The whole Lua standard library (string, table, math, io, "
    "os, ...) is available.\n\n",
    "Where scripts live: in two homes, the same split as agent skills -- a "
    "project one, checked in with the repository, and a global one in your home "
    "directory:\n\n",
    "   <project>/turbo-scripts/   project Lua -- only this project,\n"
    "                              committed, shared with the team\n"
    "   ~/.turbo/                  global Lua  -- yours, every project\n"
    "\n"
    "   In each: init.lua runs at startup (register hooks here).\n"
    "   Runnable scripts sit beside it in turbo-scripts/, and under\n"
    "   scripts/ in the global home (~/.turbo/scripts/*.lua).\n"
    "\n"
    "   (<project>/.turbo/ is a per-user cache -- session, sockets,\n"
    "   config -- and holds no scripts.)\n"
    "\n",
    "Running scripts: use the Lua menu (Run Script...) or the Command Palette "
    "(Ctrl+B) -- each script appears as \"Lua Script: <name>\". Lua > New "
    "Script... creates one under the project's turbo-scripts. Script windows "
    "have a brown frame so they stand out.\n\n",
    "A script is just Lua. For example turbo-scripts/hello.lua:\n\n",
    "   -- hello.lua\n"
    "   if turbo.active_file() ~= \"\" then\n"
    "     turbo.message(\"editing \" .. turbo.active_file())\n"
    "   else\n"
    "     turbo.message(\"no file open\")\n"
    "   end\n"
    "\n",
    "Events: in an init.lua, call turbo.on(event, handler) to react to what you "
    "do in the editor. turboIDE calls the handler with a table of parameters; for "
    "\"before\" events, returning false cancels the action.\n\n",
    "   -- ~/.turbo/init.lua   (global hooks)\n"
    "   turbo.on(\"afterSave\", function(p)\n"
    "     turbo.message(\"saved \" .. p.path)\n"
    "   end)\n"
    "\n"
    "   turbo.on(\"beforeCommit\", function(p)\n"
    "     if p.message == \"\" then return false end  -- veto empty msg\n"
    "   end)\n"
    "\n",
    "   Events you can hook (turbo.on):\n"
    "\n"
    "     newFile        a new empty buffer was created      (none)\n"
    "     openFile       a file was opened in an editor      path\n"
    "     beforeSave*    about to write the file to disk     path\n"
    "     afterSave      the file was saved                  path\n"
    "     closeFile      an editor was closed                path\n"
    "     beforeCommit*  commit confirmed, before git runs   message\n"
    "     afterCommit    the commit finished                 ok, output\n"
    "\n"
    "   (*) returning false cancels the action. Handlers also get\n"
    "   params.event (the event name).\n"
    "\n",
    "The turbo API -- the global 'turbo' table available to every script and "
    "hook:\n\n",
    "     turbo.message(s)            show a message box (alias: log)\n"
    "     turbo.version()             turboIDE / Lua version string\n"
    "     turbo.on(event, fn)         register an event handler\n"
    "     turbo.register_command(     add a palette command that runs\n"
    "        name, [desc,] fn)        fn when chosen\n"
    "     turbo.active_file()         path of the focused editor, or \"\"\n"
    "     turbo.file_text()           full text of the focused editor\n"
    "     turbo.insert_text(s)        insert text at the cursor\n"
    "     turbo.open_file(path)       open (or focus) a file\n"
    "     turbo.save()                save the focused editor\n"
    "     turbo.run_command(id)       dispatch a turboIDE command id\n"
    "     turbo.shell(cmd)            run a shell command, return stdout\n"
    "     turbo.project_root()        the project directory\n"
    "\n",
    "register_command adds your own entry to the Command Palette. For example, "
    "in ~/.turbo/init.lua:\n\n",
    "   turbo.register_command(\"Insert date\", \"today's date\",\n"
    "     function()\n"
    "       turbo.insert_text(turbo.shell(\"date +%Y-%m-%d\"))\n"
    "     end)\n"
    "\n"
    "   Registered commands are searchable in the palette by name.",
};

void TurboHelp::executeAboutDialog(TGroup &owner) noexcept
{
    TDialog *aboutBox = new TDialog(TRect(0, 0, 46, 13), "About");

    aboutBox->insert(
        new TStaticText(TRect(2, 2, 44, 9), aboutDialogText)
    );

    aboutBox->insert(
        new TButton(TRect(17, 10, 29, 12), "OK", cmOK, bfDefault)
    );

    aboutBox->options |= ofCentered;

    owner.execView(aboutBox);

    TObject::destroy(aboutBox);
}

// Use a stringbuf to store the help file contents. We use inheritance to
// ensure that the stringbuf's lifetime exceeds that of the THelpFile.
class InMemoryHelpFile : private std::stringbuf, public THelpFile
{
public:

    InMemoryHelpFile() noexcept;
};

InMemoryHelpFile::InMemoryHelpFile() noexcept :
    THelpFile(*new iopstream(this))
{
}

// Inherit THelpWindow to be able to handle the cmFindHelpWindow command.
class TurboHelpWindow : public THelpWindow
{
public:

    TurboHelpWindow(THelpFile &helpFile) noexcept;

    void handleEvent(TEvent &ev) override;
};

TurboHelpWindow::TurboHelpWindow(THelpFile &helpFile) noexcept :
    TWindowInit(&initFrame),
    THelpWindow(&helpFile, hcNoContext)
{
    state &= ~sfShadow;
}

void TurboHelpWindow::handleEvent(TEvent &ev)
{
    THelpWindow::handleEvent(ev);

    if (ev.what == evBroadcast && ev.message.command == cmFindHelpWindow)
        clearEvent(ev);
}

static THelpFile &createInMemoryHelpFile(TSpan<const TStringView> paragraphs) noexcept
{
    auto &helpTopic = *new THelpTopic;
    for (TStringView paragraphText : paragraphs)
    {
        paragraphText = paragraphText.substr(0, USHRT_MAX);
        auto &paragraph = *new TParagraph;
        paragraph.text = newStr(paragraphText);
        paragraph.size = (ushort) paragraphText.size();
        paragraph.wrap = (paragraphText.size() > 0 && paragraphText[0] != ' ');
        paragraph.next = nullptr;
        helpTopic.addParagraph(&paragraph);
    }

    auto &helpFile = *new InMemoryHelpFile;
    helpFile.recordPositionInIndex(hcNoContext);
    helpFile.putTopic(&helpTopic);

    return helpFile;
}

void TurboHelp::showOrFocusHelpWindow(TGroup &owner) noexcept
{
    auto *helpWindow =
        (TurboHelpWindow *) message(&owner, evBroadcast, cmFindHelpWindow, nullptr);

    if (helpWindow == 0)
    {
        THelpFile &helpFile = createInMemoryHelpFile(helpParagraphs);
        helpWindow = new TurboHelpWindow(helpFile);

        // Resize the Help Window so that:
        // - It fits into the owner view.
        // - It is wide enough for the topic contents to fit, if possible.
        THelpViewer *helpViewer = (THelpViewer *) helpWindow->first();
        THelpTopic *topic = helpViewer->topic;
        int topicWidth = max(helpViewer->size.x, topic->longestLineWidth());
        int marginWidth = helpWindow->size.x - helpViewer->size.x;
        int windowWidth = topicWidth + marginWidth;
        TRect bounds = owner.getExtent();
        bounds.b.x = min(bounds.b.x, windowWidth);
        // THelpWindow has 'ofCentered' set, so it will be centered automatically.
        helpWindow->changeBounds(bounds);

        owner.insert(helpWindow);
    }

    helpWindow->focus();
}
