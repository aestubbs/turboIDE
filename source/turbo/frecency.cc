#include "frecency.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

std::string storePath() noexcept
{
    const char *home = getenv("HOME");
    if (!home || !home[0])
        home = getenv("USERPROFILE"); // Windows fallback.
    if (!home || !home[0])
        return {};
    return std::string(home) + "/.turbo_frecency";
}

} // namespace

void FrecencyStore::load() noexcept
{
    auto path = storePath();
    if (path.empty())
        return;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[4096];
    while (fgets(line, sizeof line, f))
    {
        // "<unixtime>\t<count>\t<abspath>\n"
        long long ts = 0;
        int count = 0;
        const char *p = line;
        char *end = nullptr;
        ts = strtoll(p, &end, 10);
        if (end == p || *end != '\t')
            continue;
        p = end + 1;
        count = (int) strtol(p, &end, 10);
        if (end == p || *end != '\t')
            continue;
        p = end + 1;
        std::string path2 = p;
        while (!path2.empty() && (path2.back() == '\n' || path2.back() == '\r'))
            path2.pop_back();
        if (!path2.empty())
            entries[path2] = { (int64_t) ts, count };
    }
    fclose(f);
}

void FrecencyStore::save() const noexcept
{
    auto path = storePath();
    if (path.empty())
        return;
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return;
    for (auto &kv : entries)
        fprintf(f, "%lld\t%d\t%s\n",
                (long long) kv.second.last, kv.second.count, kv.first.c_str());
    fclose(f);
}

void FrecencyStore::record(const std::string &absPath) noexcept
{
    if (absPath.empty())
        return;
    Entry &e = entries[absPath];
    e.count += 1;
    e.last = (int64_t) ::time(nullptr);
    save();
}

int FrecencyStore::score(const std::string &absPath, int64_t now) const noexcept
{
    auto it = entries.find(absPath);
    if (it == entries.end())
        return 0;
    int64_t elapsed = now - it->second.last;
    double recency;
    if (elapsed < 3600)            recency = 4.0;   // < 1 hour
    else if (elapsed < 86400)      recency = 3.0;   // < 1 day
    else if (elapsed < 604800)     recency = 2.0;   // < 1 week
    else if (elapsed < 2592000)    recency = 1.5;   // < 30 days
    else                           recency = 1.0;
    int count = it->second.count;
    // Diminishing returns on raw count so a single very-frequent file can't
    // dominate the fuzzy score; recency multiplies it.
    return (int) (recency * 4.0 * std::log2(2.0 + count));
}
