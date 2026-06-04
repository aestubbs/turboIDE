#include "fuzzymatch.h"

#include <cctype>

namespace fuzzy {

namespace {

inline char lower(char c) noexcept
{
    return (char) std::tolower((unsigned char) c);
}

inline bool isSep(char c) noexcept
{
    return c == '/' || c == '\\' || c == '_' || c == '-' ||
           c == '.' || c == ' ' || c == ':';
}

// A boundary is the very start, the char after a separator, or the lower->upper
// transition of a camelCase hump (the position of the upper-case letter).
inline bool isBoundary(std::string_view s, size_t i) noexcept
{
    if (i == 0)
        return true;
    if (isSep(s[i - 1]))
        return true;
    unsigned char prev = (unsigned char) s[i - 1], cur = (unsigned char) s[i];
    return std::islower(prev) && std::isupper(cur);
}

// Offset of the basename (one past the last path separator).
inline size_t basenameStart(std::string_view s) noexcept
{
    size_t slash = s.find_last_of("/\\");
    return slash == std::string_view::npos ? 0 : slash + 1;
}

} // namespace

bool fuzzyScore(std::string_view needle, std::string_view haystack,
                int *outScore) noexcept
{
    if (needle.empty())
    {
        if (outScore) *outScore = 0;
        return true;
    }
    if (haystack.empty())
        return false;

    size_t baseStart = basenameStart(haystack);

    int total = 0;
    int consec = 0;
    size_t ni = 0;
    size_t firstMatch = std::string_view::npos;
    size_t prevMatch = std::string_view::npos;
    int gaps = 0;

    for (size_t hi = 0; hi < haystack.size() && ni < needle.size(); ++hi)
    {
        if (lower(haystack[hi]) != lower(needle[ni]))
        {
            consec = 0;
            continue;
        }
        if (firstMatch == std::string_view::npos)
            firstMatch = hi;
        if (prevMatch != std::string_view::npos && hi > prevMatch + 1)
            gaps += (int) (hi - prevMatch - 1);
        prevMatch = hi;

        int s = 1;                      // base reward per matched character
        if (isBoundary(haystack, hi))
            s += 9;
        if (hi >= baseStart)            // matches in the basename matter most
            s += 3;
        s += consec * 4;                // reward consecutive runs
        ++consec;
        total += s;
        ++ni;
    }

    if (ni < needle.size())
        return false;                   // not all needle chars consumed

    // Light penalties: a late first match and large interior gaps both signal a
    // looser match. Capped so deep paths aren't unfairly buried.
    if (firstMatch != std::string_view::npos)
        total -= (int) (firstMatch > 10 ? 10 : firstMatch);
    total -= gaps > 20 ? 20 : gaps;

    // Strong bonuses for matching the basename exactly or as a prefix, so typing
    // a complete name wins over an incidental subsequence elsewhere.
    std::string_view base = haystack.substr(baseStart);
    if (base.size() >= needle.size())
    {
        bool prefix = true, exact = base.size() == needle.size();
        for (size_t i = 0; i < needle.size(); ++i)
            if (lower(base[i]) != lower(needle[i]))
            {
                prefix = false;
                break;
            }
        if (prefix)
            total += exact ? 100 : 50;
    }

    if (outScore) *outScore = total;
    return true;
}

} // namespace fuzzy
