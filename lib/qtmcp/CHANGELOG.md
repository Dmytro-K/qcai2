# Changelog

## 0.9.1

- Initial standalone-friendly Qt MCP client library scaffolding.
- Added transport abstraction for future stdio, HTTP, and OAuth-backed transports.
- Implemented the first stdio transport using `QProcess`.
- Added a transport-agnostic JSON-RPC client core for future MCP lifecycle support.
- Added richer client and stdio transport debug logs for MCP lifecycle, JSON-RPC traffic, state changes, and parse/process failures.
