# Changelog

## 0.9.1

- Initial standalone-friendly Qt MCP client library scaffolding.
- Added transport abstraction for future stdio, HTTP, and OAuth-backed transports.
- Implemented the first stdio transport using `QProcess`.
- Added a transport-agnostic JSON-RPC client core for future MCP lifecycle support.
- Added richer client and stdio transport debug logs for MCP lifecycle, JSON-RPC traffic, state changes, and parse/process failures.
- Added a streamable HTTP transport with custom headers, MCP session ID handling, JSON and SSE response support, and request-level debug logging.
- Added OAuth 2.1 support for HTTP transports, including metadata discovery, PKCE browser auth, dynamic client registration, refresh-token reuse, and in-process token caching.
- Fixed remote OAuth bootstrap for HTTP transports by honoring `WWW-Authenticate` protected-resource metadata hints, propagating OAuth `resource` parameters, and surfacing richer 401/auth diagnostics.
- Added a public `HttpTransport::authorize()` entry point so host applications can trigger the existing OAuth flow explicitly from UI actions.
- Added persistent OAuth cache storage on disk so HTTP transport tokens survive application restarts and can be reused for non-interactive authorization attempts.
