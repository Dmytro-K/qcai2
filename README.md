# qcai2 — AI Agent Plugin for Qt Creator

An autonomous AI coding agent plugin for Qt Creator (Qt 6, C++17/20).
It implements a full plan → act → observe → verify → iterate loop with tool-calling capabilities,
multi-provider LLM support, and safety-first design.

## Features

- **Autonomous Agent Loop** — iterative controller that plans, executes tools, observes results, verifies via build/tests, and repeats until the goal is achieved or limits are hit.
- **Tool-calling** — read_file, search_repo (ripgrep), apply_patch (unified diff), run_build (cmake), run_tests (ctest), git_status, git_diff, open_file_at_location, show_diagnostics.
- **Multi-provider LLM** — pluggable provider system with built-in support for OpenAI-compatible REST APIs, custom local HTTP endpoints, and Ollama.
- **Safety by default** — dry-run mode, approval workflow for risky operations, command allowlisting, sandbox path enforcement, configurable iteration/diff/file limits.
- **Dock Widget UI** — goal input, run/stop, plan view, streaming action log, diff preview with apply/revert, approval queue.
- **Settings Page** — Qt Creator Options page for provider config, model selection, API keys, safety limits.

## Architecture

```
src/
├── AiAgentPlugin.h/.cpp        — Qt Creator plugin entry point (ExtensionSystem::IPlugin)
├── AgentController.h/.cpp      — Autonomous agent loop (plan → act → observe → verify)
├── AgentDockWidget.h/.cpp      — Dock widget UI
├── context/
│   └── EditorContext.h/.cpp    — Captures active editor state, open files, project/build dirs
├── tools/
│   ├── ITool.h                 — Abstract tool interface
│   ├── ToolRegistry.h/.cpp     — Central tool registry
│   ├── FileTools.h/.cpp        — read_file, apply_patch
│   ├── SearchTools.h/.cpp      — search_repo (ripgrep + fallback)
│   ├── BuildTools.h/.cpp       — run_build, run_tests, show_diagnostics
│   ├── GitTools.h/.cpp         — git_status, git_diff (read-only)
│   └── IdeTools.h/.cpp         — open_file_at_location
├── providers/
│   ├── IAIProvider.h           — Abstract provider interface
│   ├── OpenAICompatibleProvider.h/.cpp — OpenAI / vLLM / LMStudio / etc.
│   ├── LocalHttpProvider.h/.cpp       — Custom local HTTP endpoints
│   └── OllamaProvider.h/.cpp          — Ollama adapter
├── models/
│   ├── AgentMessages.h/.cpp    — Chat messages, agent response parsing
│   └── ToolCall.h/.cpp         — Tool call data structure
├── safety/
│   └── SafetyPolicy.h/.cpp     — Allowlist, approval rules, limits
├── util/
│   ├── Json.h/.cpp             — JSON traversal helpers
│   ├── ProcessRunner.h/.cpp    — QProcess wrapper with timeouts
│   └── Diff.h/.cpp             — Unified diff validation, apply, revert
└── settings/
    ├── Settings.h/.cpp         — Persistent settings (QSettings)
    └── SettingsPage.h/.cpp     — Qt Creator Options page
```

## How to Build

### Prerequisites

- Qt 6.x
- Qt Creator source or SDK (with plugin development headers)
- CMake 3.28+

### Build Steps

```bash
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=<path_to_qtcreator> \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      <path_to_plugin_source>
cmake --build .
```

Where `<path_to_qtcreator>` is the path to a Qt Creator build/install directory.

### Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWITH_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

This currently builds and runs the QtTest-based unit tests under `tests/`.

### Format Changed Files

```bash
cmake -P format-changed-files.cmake
```

If you already have a configured build directory, the equivalent CMake target is:

```bash
cmake --build build --target format-changed-files
```

This formats changed C/C++/Objective-C source files with the repository `.clang-format`
and skips common build directories.

### Install

```bash
cmake --install build
```

By default this installs into the current user's Qt Creator plugin area:

- Linux: `~/.local/share/data/QtProject/qtcreator/plugins/18.0.2`

The install step deploys the plugin and Node.js sidecar there and runs `npm install`
in the installed sidecar directory so `@github/copilot-sdk` is available.

To use a different destination, override the install prefix:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/path/to/qtcreator-user-root
# or:
cmake --install build --prefix /path/to/qtcreator-user-root
```

The custom prefix should be the Qt Creator root that contains the versioned `plugins/<qtcreator-version>/` directory.

### Run

```bash
cmake --build . --target RunQtCreator
```

This starts Qt Creator with `-pluginpath` pointing to the built plugin.
Add `-temporarycleansettings` to avoid affecting your global settings.

## Configuration

Open **Tools → Options → AI Agent** in Qt Creator:

| Setting | Description | Default |
|---------|-------------|---------|
| Provider | openai, local, ollama | openai |
| Base URL | LLM endpoint base URL | https://api.openai.com |
| API Key | Your API key (stored in Qt Creator settings, never logged) | — |
| Model | Model name | gpt-4o |
| Temperature | Sampling temperature | 0.2 |
| Max Tokens | Max response tokens | 4096 |
| Dry-run | Default to dry-run mode (plan only, no apply) | Yes |
| Max iterations | Agent loop iteration limit | 8 |
| Max tool calls | Total tool calls per run | 25 |
| Max diff lines | Max ± lines in a single patch | 300 |
| Max changed files | Max files in a single patch | 10 |

### Using a Custom Local Model

Set **Provider** to "Local HTTP" or "OpenAI-Compatible", then set:
- **Base URL**: e.g. `http://localhost:8080` or `http://localhost:1234`
- **Model**: your model name
- For Ollama: set Provider to "Ollama" and Base URL to `http://localhost:11434`

The plugin supports any endpoint that speaks the OpenAI `/v1/chat/completions` protocol.

## How to Add a New Provider

1. Create a class inheriting `IAIProvider` (see `src/providers/IAIProvider.h`).
2. Implement `id()`, `displayName()`, `complete()`, `cancel()`, `setBaseUrl()`, `setApiKey()`.
3. Register it in `AiAgentPlugin::setupProviders()`.
4. Add a combo box entry in `SettingsPage`.

## How to Add a New Tool

1. Create a class inheriting `ITool` (see `src/tools/ITool.h`).
2. Implement `name()`, `description()`, `argsSchema()`, `execute()`.
3. Optionally override `requiresApproval()` if the tool modifies files.
4. Register it in `AiAgentPlugin::registerTools()`.

## Safety Model

- **Dry-run by default**: agent produces plans and diffs but doesn't apply them.
- **Approval workflow**: risky actions (apply_patch, file deletion, commands not in allowlist) require explicit user acceptance via a dialog.
- **Sandbox enforcement**: file operations are restricted to the project directory.
- **Command allowlist**: only cmake, ninja, make, ctest, rg, git, patch are auto-allowed.
- **Limits**: configurable caps on iterations, tool calls, diff size, and files changed.

## License

BSD-2-Clause — (C) Dmytro Kosmii
