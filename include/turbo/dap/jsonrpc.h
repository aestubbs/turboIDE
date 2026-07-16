#ifndef TURBO_DAP_JSONRPC_H
#define TURBO_DAP_JSONRPC_H

#ifdef TURBO_ENABLE_DAP

#include <string>
#include <cstddef>

namespace turbo {
namespace dap {

// The Debug Adapter Protocol shares LSP's base-protocol framing:
//   Content-Length: <N>\r\n\r\n<body>
// This is deliberately a small, independent copy of the LSP framing so DAP can
// be built without TURBO_ENABLE_LSP (which gates turbo::lsp::frameMessage).

// Wraps a JSON body in the Content-Length base-protocol framing.
std::string frameMessage(const std::string &body);

// Incremental parser for the base protocol. Feed it raw bytes as they arrive
// from the adapter's stdout; pull complete JSON message bodies back out.
class MessageReader
{
    std::string buf;
public:
    void feed(const char *data, size_t len);
    // Extracts the next complete message body into 'body'. Returns false when
    // no complete message is buffered yet (need to feed more bytes).
    bool next(std::string &body);
};

} // namespace dap
} // namespace turbo

#endif // TURBO_ENABLE_DAP
#endif // TURBO_DAP_JSONRPC_H
