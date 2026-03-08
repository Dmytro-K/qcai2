# Changelog

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
