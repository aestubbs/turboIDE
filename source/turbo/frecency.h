#ifndef TURBO_FRECENCY_H
#define TURBO_FRECENCY_H

#include <cstdint>
#include <string>
#include <unordered_map>

// Tracks how often and how recently files are opened, to bias "Goto Anything"
// results toward the things you actually use. Persisted to ~/.turbo_frecency
// (kept separate from ~/.turborc) as lines "<unixtime>\t<count>\t<abspath>" --
// the path comes last so it may contain anything except a tab. Scoring is
// Mozilla-style: a recent open is worth more than an old one.
class FrecencyStore
{
public:
    // Load the store file (call once at startup). Missing file is fine.
    void load() noexcept;

    // Record an open of 'absPath' (bumps count + timestamp) and persist.
    void record(const std::string &absPath) noexcept;

    // Ranking bonus for 'absPath' (>= 0; 0 if never opened). 'now' is unix
    // seconds; pass the same value across one ranking pass for stable ordering.
    int score(const std::string &absPath, int64_t now) const noexcept;

private:
    struct Entry { int64_t last {0}; int count {0}; };
    std::unordered_map<std::string, Entry> entries;

    void save() const noexcept;
};

#endif // TURBO_FRECENCY_H
