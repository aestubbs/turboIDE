#ifndef TURBO_MCP_NDJSON_H
#define TURBO_MCP_NDJSON_H

#include <cstddef>
#include <string>

namespace turbo {
namespace mcp {

// Incremental splitter for MCP's stdio framing: newline-delimited JSON (one
// message per line, no embedded newlines). Feed it raw bytes; pull complete
// lines back out. A trailing '\r' (CRLF) is tolerated and stripped.
class NdjsonReader
{
    std::string buf;
public:
    void feed(const char *data, size_t len) { buf.append(data, len); }

    // Extract the next complete line (without its terminator) into 'line'.
    // Returns false when no complete line is buffered yet.
    bool next(std::string &line)
    {
        size_t pos = buf.find('\n');
        if (pos == std::string::npos)
            return false;
        line.assign(buf, 0, pos);
        buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return true;
    }
};

} // namespace mcp
} // namespace turbo

#endif // TURBO_MCP_NDJSON_H
