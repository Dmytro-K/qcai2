# Changelog

## [0.0.11] - 2026-03-29

### Added

### Changed

- Fixed Copilot completion no-timeout setting
- Run autocompation between iterations
- Fix read_file show in Actions Log

## [0.0.10] - 2026-03-28

### Added

- Added file links in Actions Log

### Changed

- Fixed class extract for autocomplete
- Fixed copy to clipboard in Actions Log

## [0.0.9] - 2026-03-25

### Added

- Added image/screenshot attachment support
- Added Unlimited value and reset button for settings Spin Boxes

### Changed

- Refactored Action Log to improve performance

## [0.0.8] - 2026-03-22

### Added

- Added an optional multi-turn inline diff refinement flow
- Added global system prompt
- Added per-project system prompt
- Added `web_search` tool with support of DuckDuckGo, Serper.dev, Perplexity API, Brave Search API
- Added interactive run steering (Soft in-flight steering)
- Added request queue (Ctrl+Enter by default)
- Added structured user decision requests
- Added Qt Creator debugger tools, including debugger start, status view, and managed breakpoint persistence

### Changed

- Fixed compaction
- Project/system prompt loading now also reads standard AI instruction files such as `AGENTS.md`, etc
- Fixed diff preview

## [0.0.7] - 2026-03-21

### Added

- Added `/compact` slash command to compress the relevant conversation context into a short structured summary for future steps
- Added progress status
- Added Anthropic API provider
- Added statistic
- Added detailed debug log
- Added GitHub Copilot Premium request usage show
- Added optional auto compact
- Added a conversation list
- Added a ClangCodeModel/clangd agent tools

### Changed

- Improved cache locality
- Fixed markdown streaming

## [0.0.6] - 2026-03-15

### Added

- Added persistent chat history with append-only messages, rolling summaries, stable workspace memory, stored artifacts, and lightweight context restoration for agent and completion requests.
- Added usage shown after completed model requests.
- Added and documented a repository coding style guide, then aligned the codebase with the explicit comparison and parenthesized-condition rules.
- Added MCP SSE transport

### Changed

- Fixed an MCP startup freeze where runtime HTTP MCP sessions could wait for the full 10-second startup timeout after the transport had already connected, delaying the initial `initialize` request.

## [0.0.5] - 2026-03-14

### Added

- Added a shared goal-editor special-token system with popup autocompletion, blue slash commands, `DECLARE_COMMAND(...)` auto-registration, and a sample `/hello` command.
- Added file references via linked files above the goal, drag-and-drop attachments, and `#` references in the goal editor.
- Added the standalone-friendly Qt MCP client library.
- Added MCP client.

### Changed

- The dock now auto-reloads project session state when session files are changed externally.
- Project session files now use stable `.qcai2/session.*` names.
- Moved `AI Agent` from a standalone dock widget into Qt Creator's navigation/sidebar area

## [0.0.4] - 2026-03-12

### Added

- Added per-project agent context selection with a project picker that lists currently open Qt Creator projects.
- Added `Ask` and `Agent` run modes in the dock widget.
- Added a version-and-build-aware migration system for global settings and project-local state.
- Added direct agent access to Qt Creator `Compile Output` and `Application Output` through new `show_compile_output` and `show_application_output` tools.
- Added repository-level static analysis configuration via `.clang-tidy` and `cmake/clang-static-analyzer-args.cmake`.

### Changed

- Plugin version metadata and migration revision constants are now sourced from the root `VERSION.txt` file via CMake parsing.
- Fixes for Agent widget UI
- Migrated `AgentDockWidget` and `SettingsPage` layout definition from hand-built C++ layout code to a Qt Designer `.ui`.
- The Actions Log and `Raw Markdown` view now omit raw `read_file` file contents, preventing markdown breakage from echoed source text.
- Inline diff review in the code editor now shows clickable AI diff annotations with visible accept/reject actions, and the dock diff preview stays synchronized as hunks are resolved inline.
- The qcai2 project selector now follows Qt Creator's active startup project on startup and when the startup project changes.

## [0.0.3] - 2026-03-09

### Added

- Added a `Raw Markdown` dock tab that shows the unrendered Actions Log markdown for debugging.

### Changed

- Rendered the `Actions Log` as markdown and reformatted tool-call entries so arguments and results are easier to read.
- Wrapped raw tool payloads such as `read_file` results in fenced code blocks before rendering, so C++ and other source snippets are displayed correctly.
- Split agent controls into separate `Reasoning` and `Thinking` settings in both the settings page and the dock widget panel.

## [0.0.2] - 2026-03-08

### Added

- Added `CHANGELOG.md` to track project releases and notable changes.

### Changed

- Linux GitHub Actions artifacts and release assets are now packaged as `.tar.xz` archives compressed with `xz -9` instead of `.zip`.

## [0.0.1] - 2026-03-08

### Added

- Initial release of the `qcai2` Qt Creator plugin.
- Autonomous agent execution with structured plan, tool-call, approval, and final-response stages.
- Built-in repository, patching, diagnostics, and git tools integrated into the Qt Creator workflow.
- Provider support for OpenAI-compatible APIs, GitHub Copilot through the bundled sidecar, custom local HTTP endpoints, and Ollama.
- Classic completion assist together with inline ghost-text suggestions in the editor.
- CI builds and packaged artifacts for Windows, Linux, and macOS.
