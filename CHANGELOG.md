# Changelog

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
