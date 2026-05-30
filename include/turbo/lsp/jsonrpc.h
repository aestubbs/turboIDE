#ifndef TURBO_LSP_JSONRPC_H
#define TURBO_LSP_JSONRPC_H

#ifdef TURBO_ENABLE_LSP

#include <string>
#include <cstddef>

namespace turbo {
namespace lsp {

// Wraps a JSON body in LSP's base-protocol framing:
//   Content-Length: <N>\r\n\r\n<body>
std::string frameMessage(const std::string &body);

// Incremental parser for the LSP base protocol. Feed it raw bytes as they
// arrive from the server's stdout; pull complete JSON message bodies back out.
class MessageReader
{
    std::string buf;
public:
    void feed(const char *data, size_t len);
    // Extracts the next complete message body into 'body'. Returns false when
    // no complete message is buffered yet (need to feed more bytes).
    bool next(std::string &body);
};

} // namespace lsp
} // namespace turbo

#endif // TURBO_ENABLE_LSP
#endif // TURBO_LSP_JSONRPC_H
