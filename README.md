# qcai2 — AI Agent Plugin for Qt Creator

`qcai2` is a Qt Creator plugin built with Qt 6 and C++23. It embeds an autonomous coding agent, provider-backed chat/completion, diff review, and safety checks directly into the IDE.

## Features

- [x] Autonomous agent loop (plan → act → observe → verify) with Ask and Agent modes
- [x] Multiple LLM providers: OpenAI-compatible, Anthropic API, GitHub Copilot, Ollama, custom local
- [x] MCP support (stdio, HTTP/OAuth, SSE) with per-project and global server configs
- [x] Diff review with per-line accept/reject and inline editor markers
- [x] AI code completion with inline ghost-text suggestions
- [x] Chat history, rolling summaries, and context budgeting
- [x] Per-project sessions under `.qcai2/` with migration, auto-reload, and per-conversation state files
- [x] Safety: dry-run by default, approval gates, path sandboxing, command allowlisting
- [x] `#file` references and slash commands with auto-completion
- [x] Built-in tools for file creation, directory listing, command execution, symbol lookup, optional clangd-backed code-model queries, diagnostics, git, and IDE output access
- [x] Slash commands including `/compact`, `/explain`, `/refactor`, and `/test`
- [x] Live provider progress status, detailed per-request Markdown logs, and per-project usage statistics
- [x] GitHub Copilot Premium request usage display
- [x] Optional auto-compact based on prior context token usage
- [x] Conversation list UI
- [x] Optional Qdrant-backed vector search
- [x] Per-project custom instructions (`.qcai2/rules.md`)
- [x] Soft in-flight steering
- [x] Multi-turn inline edit
- [x] Debugger integration
- [x] Web search tool
- [x] Image/screenshot attachment
- [ ] Skills
- [ ] Semantic search over chat history
- [ ] Semantic search over code
- [ ] Multi-agent orchestration with delegated subtasks and parallel execution
- [ ] Voice input
- [ ] Voice output

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
- `ClangCodeModel` is also required when building with `-DFEATURE_CLANGD_ENABLE=ON` (default)
- Node.js + npm for the Copilot sidecar install/runtime flow
- Optional: `ripgrep` for faster repository searches inside the plugin
- Optional: `gcovr` for C/C++ coverage reports when building tests with `-DQCAI2_ENABLE_COVERAGE=ON`

### Configure

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_TESTS=ON \
  -DCMAKE_PREFIX_PATH="<path-to-qt>;<path-to-qtcreator>"
```

If CMake cannot discover the package roots automatically, also pass `-DQt6_DIR=...` and/or `-DQtCreator_DIR=...`.

Optional feature flags:

- `-DFEATURE_CLANGD_ENABLE=OFF` builds without the Qt Creator `ClangCodeModel` dependency.
- `-DFEATURE_QDRANT_ENABLE=OFF` builds without the Qdrant-backed vector search backend.

When `FEATURE_QDRANT_ENABLE=ON`, the plugin also needs `zlib`, because the Qdrant
integration uses the bundled `lib/qdrant_client` HTTP layer.

### Build

```bash
cmake --build build --parallel
```

### Tests

```bash
ctest --test-dir build --output-on-failure
```

### Coverage report

Configure with coverage instrumentation enabled:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_TESTS=ON \
  -DQCAI2_ENABLE_COVERAGE=ON \
  -DCMAKE_PREFIX_PATH="<path-to-qt>;<path-to-qtcreator>"
```

Then build and generate the `gcovr` reports:

```bash
cmake --build build --target coverage --parallel
```

This runs `ctest` and writes the reports to `build/coverage/`:

- `coverage.txt`
- `coverage.xml`
- `coverage.html`

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

- **Active provider**: `openai`, `anthropic`, `copilot`, `local`, or `ollama`
- **Remote API (OpenAI-compatible / Anthropic)**:
  - Shared settings: base URL, API key, model, reasoning effort, thinking, temperature, and max tokens
  - Default base URL: `https://api.openai.com`
  - Default agent model: `gpt-5.4`
  - Default reasoning effort: `medium`
  - Default thinking: `medium`
  - Default temperature: `0.2`
  - Default max tokens: `4096`
  - Editable presets include OpenAI, Anthropic, Gemini, xAI, DeepSeek, Mistral, Groq, OpenRouter, Together, Fireworks, and local URLs
- **GitHub Copilot (via sidecar)**:
  - Model selector with runtime-populated presets
  - Default model: `gpt-5.4`
  - Optional custom Node path
  - Optional custom sidecar path
  - Completion timeout: `300 s` by default; `0` disables the timeout
- **Local HTTP**:
  - Default base URL: `http://localhost:8080`
  - Default endpoint: `/v1/chat/completions`
  - Optional custom headers
  - Optional simplified `"{prompt}"` mode
- **Ollama**:
  - Default base URL: `http://localhost:11434`
  - Default model: `llama4`
  - Editable model list with common local presets

### MCP Servers tab

- Configure global MCP server definitions used by the plugin.
- Supports stdio servers plus HTTP/SSE transports with the project MCP client stack.

### Code Completion tab

- Enabled by default
- Completion model is optional; leave it empty to use the same model as the agent
- Trigger threshold: `3` characters
- Trigger delay: `500 ms`
- Default completion thinking: `off`
- Default completion reasoning effort: `off`

### Safety & Behavior tab

- **Agent Limits**:
  - Max iterations: `8`
  - Max tool calls: `25`
  - Max diff lines before approval: `300`
  - Max changed files before approval: `10`
- **Behavior**:
  - Dry-run mode enabled by default
  - Optional debug logging to the `Debug Log` tab
  - Optional detailed per-request Markdown logging under the project `.qcai2/logs/` directory
  - Optional `Agent Debug` mode that shows raw JSON payloads in chat
  - Conversation history is split into a project session file plus per-conversation state files under `.qcai2/conversations/`
- **Auto-compact**:
  - Disabled by default
  - When enabled, automatically runs `/compact` before the next request after the previous request exceeds the input-token threshold
  - Default threshold: `8000` input tokens

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
