#ifndef TURBO_GOTOANYTHING_H
#define TURBO_GOTOANYTHING_H

struct TurboApp;

// Show the "Goto Anything" overlay (Ctrl-P): fuzzy file open across the project
// with frecency ranking and a live preview. A leading ':' switches to line-jump
// in the active editor; a leading '@' lists symbols in the active file; a
// trailing ':N' on a file query opens that file at line N.
void runGotoAnything(TurboApp &app) noexcept;

#endif // TURBO_GOTOANYTHING_H
