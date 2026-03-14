# Qt MCP client library

`lib/mcp` is a small, reusable Qt-based MCP client library project that is intended to
stay decoupled from the qcai2 plugin code so it can later be moved into its own
repository.

## Goals

- Qt-native API and event model
- transport-oriented architecture
- no dependency on qcai2-specific types
- incremental delivery of MCP support without painting ourselves into a corner

## Roadmap

- v1: stdio transport only
- v2: HTTP transport with bearer token / API key support
- v3: full OAuth 2.1 flow

## Current scope

This first version intentionally implements only the reusable foundation:

- generic JSON-RPC client core
- pluggable transport abstraction
- stdio transport backed by `QProcess`

It does **not** yet implement the full MCP lifecycle, capability negotiation, tool or
resource helpers, or OAuth-specific flows. Those can be layered on top of the current
client and transport abstractions.

## Layout

- `include/qtmcp/Transport.h` - abstract transport interface
- `include/qtmcp/StdioTransport.h` - subprocess-backed stdio transport
- `include/qtmcp/Client.h` - transport-agnostic JSON-RPC client core
- `src/` - implementations

## Example

```cpp
#include <qtmcp/Client.h>
#include <qtmcp/StdioTransport.h>

auto client = new qtmcp::Client(this);

qtmcp::StdioTransportConfig config;
config.program = QStringLiteral("my-mcp-server");
config.arguments = {QStringLiteral("--stdio")};

client->setTransport(std::make_unique<qtmcp::StdioTransport>(config));
client->start();
client->sendRequest(QStringLiteral("initialize"));
```

## Build

The library can be built as part of qcai2 via the root `CMakeLists.txt`, and it also has
its own `lib/mcp/CMakeLists.txt` so it can be extracted into a standalone project later
with minimal structural changes.
