#ifndef TURBO_FUZZYMATCH_H
#define TURBO_FUZZYMATCH_H

#include <string_view>

// Pure, dependency-free fuzzy matcher shared by the command palette and the
// "Goto Anything" file/symbol picker. Both feed every candidate through
// fuzzyScore() and rank by the result, so the matching feel is identical
// everywhere. No Turbo Vision / Scintilla includes here on purpose: this is a
// plain string algorithm (and is unit-testable in isolation).
namespace fuzzy {

// Case-insensitive subsequence match of 'needle' against 'haystack'.
// Returns true if every character of 'needle' appears in 'haystack' in order,
// writing a relevance score to *outScore (higher = better). An empty needle
// always matches with score 0.
//
// Scoring rewards: matches at word boundaries (after a path separator, '_',
// '-', '.', space, or a camelCase hump), consecutive runs, and matches inside
// the basename (the part after the last '/'); it lightly penalises leading and
// interior gaps. An exact or prefix match of the basename gets a large bonus,
// so typing a full file/command name floats it to the top.
bool fuzzyScore(std::string_view needle, std::string_view haystack,
                int *outScore) noexcept;

} // namespace fuzzy

#endif // TURBO_FUZZYMATCH_H
