# Changelog

## [0.0.4] - 2026-03-11

### Added

- Added per-project agent context selection with a project picker that lists currently open Qt Creator projects.
- Added project-local context storage under each project's `.qcai2/` directory for chat state and dock UI state.
- Added `Ask` and `Agent` run modes in the dock widget.
- Added mouse-resizable vertical splitting between the tabs area and the input panel.

### Changed

- Persisted the last selected `Mode`, `Model`, `Reasoning`, `Thinking`, and `Dry-run` state per project instead of sharing one global dock state.
- Reworked the dock layout so the goal editor grows to fill the available height, `Mode` and `Model` sit under the goal editor, and the execution controls stay compact and bottom-aligned.
- Migrated `AgentDockWidget` layout definition from hand-built C++ layout code to a Qt Designer `.ui` file while keeping the custom diff preview widget wired in from C++.
- Migrated `SettingsPage` layout definition from hand-built C++ layout code to a Qt Designer `.ui` file.

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
