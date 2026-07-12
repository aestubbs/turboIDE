#ifndef TURBO_MCP_BRIDGE_H
#define TURBO_MCP_BRIDGE_H

namespace turbo {
namespace mcp {

// Entry point for `turboIDE mcp [--socket <path>]`: a thin stdio<->AF_UNIX
// bridge. The coding agent launches this as a normal stdio MCP server; it
// connects to the running turboIDE's unix socket and forwards bytes both ways
// (newline-delimited JSON, never parsed here). Diagnostics go to stderr only so
// stdout carries protocol bytes exclusively. Returns a process exit code.
int runBridge(int argc, const char **argv) noexcept;

} // namespace mcp
} // namespace turbo

#endif // TURBO_MCP_BRIDGE_H
