# qcai2 — AI Agent Plugin for Qt Creator

`qcai2` is a Qt Creator plugin built with Qt 6 and C++23. It embeds an autonomous coding agent, provider-backed chat/completion, diff review, and safety checks directly into the IDE.

## Features

- [x] Autonomous agent loop (plan → act → observe → verify) with Ask and Agent modes
- [x] Multiple LLM providers: OpenAI-compatible, GitHub Copilot, Ollama, custom local
- [x] MCP support (stdio, HTTP/OAuth, SSE) with per-project and global server configs
- [x] Diff review with per-line accept/reject and inline editor markers
- [x] AI code completion with inline ghost-text suggestions
- [x] Chat history, rolling summaries, and context budgeting
- [x] Per-project sessions under `.qcai2/` with migration and auto-reload
- [x] Safety: dry-run by default, approval gates, path sandboxing, command allowlisting
- [x] `#file` references and `/command` slash commands with auto-completion
- [ ] More tools: create_file, list_directory, run_command, find_symbol
- [ ] More slash commands: /clear, /diff, /explain, /refactor, /test
- [ ] Streaming markdown rendering
- [ ] Per-project custom instructions (`.qcai2/rules.md`)
- [ ] Semantic search over chat history

## Repository layout

- `ai/` — internal context and supporting materials for AI-assisted development.
- `cmake/` — helper CMake scripts for formatting, documentation, installation, and repository maintenance tasks.
- `lib/qtmcp/` — reusable Qt MCP client library (stdio + HTTP transports with OAuth).
- `src/` — the main plugin contents:
  - `docs/` — documentation templates and source material;
  - `resources/` — icons and other Qt resources;
  - `sidecar/` — the Node.js sidecar for GitHub Copilot integration;
  - `src/` — the C++ plugin code, organized by subsystem;
  - `tests/` — unit tests.

## Building

### Prerequisites

- CMake 3.28+
- Qt 6 with `Widgets`, `Network`, and (when tests are enabled) `Test`
- Qt Creator development package / SDK with the `Core`, `TextEditor`, and `ProjectExplorer` CMake packages
- Node.js + npm for the Copilot sidecar install/runtime flow
- Optional: `ripgrep` for faster repository searches inside the plugin

### Configure

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_TESTS=ON \
  -DCMAKE_PREFIX_PATH="<path-to-qt>;<path-to-qtcreator>"
```

If CMake cannot discover the package roots automatically, also pass `-DQt6_DIR=...` and/or `-DQtCreator_DIR=...`.

### Build

```bash
cmake --build build --parallel
```

### Tests

```bash
ctest --test-dir build --output-on-failure
```

### Format files

```bash
cmake --build build --target format-changed-files
```

By default this runs in dry-run mode: it does not modify files, prints the diffs
required to format them, and fails if formatting changes are needed.

To check all supported source files, use:

```bash
cmake --build build --target format-all-files
```

To apply formatting instead of showing diffs, use:

```bash
cmake --build build --target format-changed-files-apply
cmake --build build --target format-all-files-apply
```

To install a `pre-commit` hook that blocks commits with misformatted staged
files, run:

```bash
scripts/install-format-hook.sh
```

### Generate documentation

```bash
cmake --build build --target doc
```

The `doc` target runs Doxygen plus Sphinx and writes HTML output under `build/docs/html/` plus a PDF manual at `build/docs/latex/qcai2.pdf`. It expects `doxygen`, `sphinx-build`, `latexmk`, Python 3, and the Python packages required by the documentation source directory (`breathe` and `exhale`), along with a working PDF LaTeX toolchain.

### Run Qt Creator with the built plugin

```bash
cmake --build build --target RunQtCreator
```

## Installation

```bash
cmake --install build
```

The install step copies the plugin plus the sidecar directory and then runs:

```bash
npm install --no-audit --no-fund
```

inside the installed sidecar directory, so npm must be available at install time.

Current default install roots from the build configuration are:

- **Linux**: `$XDG_DATA_HOME/data/QtProject/qtcreator` or `~/.local/share/data/QtProject/qtcreator`
- **macOS**: `~/Library/Application Support/QtProject/Qt Creator`
- **Windows**: `%LOCALAPPDATA%/QtProject/qtcreator`

Override the destination with either:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/path/to/qtcreator-root
```

or:

```bash
cmake --install build --prefix /path/to/qtcreator-root
```

## Configuration

Open **Edit → Preferences → Qcai2** in Qt Creator.

### Providers tab

- **Active provider**: `openai`, `copilot`, `local`, or `ollama`
- **OpenAI-compatible defaults**:
  - Base URL: `https://api.openai.com`
  - Agent model: `gpt-5.4`
  - Thinking level: `medium`
  - Temperature: `0.2`
  - Max tokens: `4096`
- **GitHub Copilot defaults**:
  - Copilot model: `gpt-5.4`
  - Optional custom Node path
  - Optional custom sidecar path
- **Local HTTP defaults**:
  - Base URL: `http://localhost:8080`
  - Endpoint: `/v1/chat/completions`
  - Optional simplified prompt mode
  - Optional custom headers
- **Ollama defaults**:
  - Base URL: `http://localhost:11434`
  - Model: `llama4`

### Code Completion tab

- Enabled by default
- Trigger threshold: `3` characters
- Debounce delay: `500 ms`
- Optional completion-specific model override
- Completion thinking default: `off`
- Completion reasoning effort default: `off`

### Safety & Behavior tab

- Dry-run enabled by default
- Max iterations: `8`
- Max tool calls: `25`
- Max diff lines before approval: `300`
- Max changed files before approval: `10`
- Optional debug logging and raw agent JSON output

## Safety model

- `apply_patch` always requires approval.
- Large changes can also require approval based on changed file and line limits.
- File operations are restricted to paths under the active project/work directory.
- Auto-allowed external commands are currently: `cmake`, `ninja`, `make`, `ctest`, `rg`, `git`, `patch`.

## GitHub Copilot sidecar

The Copilot integration lives in the sidecar directory and communicates with the plugin over JSON Lines on stdin/stdout.

- Methods currently handled by the sidecar: `start`, `complete`, `list_models`, `cancel`, `stop`
- The sidecar uses `@github/copilot-sdk`
- Each completion request creates its own Copilot SDK session
- Errors guide the user to re-run `cmake --install` or `npm install` in the installed sidecar directory and authenticate with `copilot /login`

## CI

GitHub Actions are documented in the workflows directory. The current workflow builds, tests, installs, and packages the plugin on Windows x64/arm64, Linux x64/arm64, and macOS, and publishes tagged releases.

## License

BSD-2-Clause — (C) Dmytro Kosmii
