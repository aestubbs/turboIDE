#include <turbo/dap/jsonrpc.h>

#ifdef TURBO_ENABLE_DAP

#include <cctype>
#include <cstdlib>

namespace turbo {
namespace dap {

std::string frameMessage(const std::string &body)
{
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

void MessageReader::feed(const char *data, size_t len)
{
    buf.append(data, len);
}

bool MessageReader::next(std::string &body)
{
    // Locate the blank line that separates headers from the body.
    auto headerEnd = buf.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    // Parse the Content-Length header (case-insensitive key).
    size_t contentLength = 0;
    bool haveLength = false;
    size_t pos = 0;
    while (pos < headerEnd)
    {
        auto lineEnd = buf.find("\r\n", pos);
        if (lineEnd == std::string::npos || lineEnd > headerEnd)
            lineEnd = headerEnd;
        std::string line = buf.substr(pos, lineEnd - pos);
        auto colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string key = line.substr(0, colon);
            for (auto &c : key) c = (char) std::tolower((unsigned char) c);
            if (key == "content-length")
            {
                contentLength = (size_t) std::strtoul(line.c_str() + colon + 1, nullptr, 10);
                haveLength = true;
            }
        }
        pos = lineEnd + 2;
    }

    if (!haveLength)
    {
        // Malformed header block; drop it and resync past the separator.
        buf.erase(0, headerEnd + 4);
        return false;
    }

    size_t bodyStart = headerEnd + 4;
    if (buf.size() < bodyStart + contentLength)
        return false; // Body not fully received yet.

    body.assign(buf, bodyStart, contentLength);
    buf.erase(0, bodyStart + contentLength);
    return true;
}

} // namespace dap
} // namespace turbo

#endif // TURBO_ENABLE_DAP
