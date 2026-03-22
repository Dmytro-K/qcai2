# qcai2 — AI Agent Plugin for Qt Creator

## Context for continuing development

This file should reflect the current state of the repository, not the local environment of any single machine.

---

## Overview

**qcai2** is a plugin for Qt Creator built with Qt 6 / C++23 and an autonomous agent loop:

`plan → act (tools) → observe → verify → iterate`

Current capabilities:

- multi-provider LLM layer: `openai`, `copilot`, `local`, `ollama`;
- tool-calling for files, search, builds, tests, git, and IDE navigation;
- dock widget with Plan / Actions Log / Diff Preview / Approvals / Debug Log tabs;
- AI completion via `CompletionAssistProvider`;
- ghost text via `GhostTextManager` and Qt Creator `TextSuggestion`;
- inline diff markers via `InlineDiffManager`;
- dry-run by default, approval workflow, sandboxed paths;
- debug logging and crash handler.

When changing code, follow the project coding style described in
`docs/CODING_STYLE.md`.

---

## Current project structure

```text
qcai2/
├── CMakeLists.txt
├── README.md
├── ai/
│   └── ai-context.md
├── cmake/
│   ├── build-docs.cmake
│   ├── format-files.cmake
│   └── install-sidecar-deps.cmake
├── scripts/
│   └── install-format-hook.sh
├── src/
│   ├── docs/
│   ├── qcai2.json.in
│   ├── qcai2constants.h
│   ├── qcai2tr.h
│   ├── resources/
│   ├── sidecar/
│   │   ├── copilot-sidecar.js
│   │   ├── package-lock.json
│   │   └── package.json
│   ├── src/
│   │   ├── ai_agent_plugin.h/.cpp
│   │   ├── agent_controller.h/.cpp
│   │   ├── ui/
│   │   │   └── agent_dock_widget.h/.cpp/.ui
│   │   ├── session/
│   │   │   └── agent_dock_session_controller.h/.cpp
│   │   ├── linked_files/
│   │   │   └── agent_dock_linked_files_controller.h/.cpp
│   │   ├── completion/
│   │   │   ├── ai_completion_provider.h/.cpp
│   │   │   ├── ai_completion_processor.h/.cpp
│   │   │   ├── ghost_text_manager.h/.cpp
│   │   ├── context/
│   │   │   └── editor_context.h/.cpp
│   │   ├── diff/
│   │   │   └── inline_diff_manager.h/.cpp
│   │   ├── models/
│   │   │   ├── agent_messages.h/.cpp
│   │   │   └── tool_call.h/.cpp
│   │   ├── providers/
│   │   │   ├── iai_provider.h
│   │   │   ├── open_ai_compatible_provider.h/.cpp
│   │   │   ├── copilot_provider.h/.cpp
│   │   │   ├── local_http_provider.h/.cpp
│   │   │   └── ollama_provider.h/.cpp
│   │   ├── safety/
│   │   │   └── safety_policy.h/.cpp
│   │   ├── settings/
│   │   │   ├── settings.h/.cpp
│   │   │   └── settings_page.h/.cpp
│   │   ├── tools/
│   │   │   ├── build_tools.h/.cpp
│   │   │   ├── file_tools.h/.cpp
│   │   │   ├── git_tools.h/.cpp
│   │   │   ├── ide_tools.h/.cpp
│   │   │   ├── i_tool.h
│   │   │   ├── search_tools.h/.cpp
│   │   │   └── tool_registry.h/.cpp
│   │   └── util/
│   │       ├── crash_handler.h/.cpp
│   │       ├── diff.h/.cpp
│   │       ├── json.h/.cpp
│   │       ├── logger.h/.cpp
│   │       └── process_runner.h/.cpp
│   └── tests/
│       ├── tst_json.cpp
│       └── tst_toolcall.cpp
```

---

## Providers

| ID | Class | Note |
|---|---|---|
| `openai` | `OpenAICompatibleProvider` | base OpenAI-compatible HTTP transport |
| `copilot` | `CopilotProvider` | via the Node.js sidecar and `@github/copilot-sdk` |
| `local` | `LocalHttpProvider` | local/custom HTTP endpoint |
| `ollama` | `OllamaProvider` | local Ollama server |

Current defaults from `Settings`:

- `provider = openai`
- `modelName = gpt-5.4`
- `thinkingLevel = medium`
- `temperature = 0.2`
- `maxTokens = 4096`
- `copilotModel = gpt-5.4`
- `localBaseUrl = http://localhost:8080`
- `localEndpointPath = /v1/chat/completions`
- `ollamaBaseUrl = http://localhost:11434`
- `ollamaModel = llama4`

---

## Tool registry

Currently, `AiAgentPlugin::registerTools()` registers:

- `read_file`
- `apply_patch`
- `search_repo`
- `run_build`
- `run_tests`
- `show_diagnostics`
- `git_status`
- `git_diff`
- `open_file_at_location`

`SafetyPolicy` automatically allowlists these commands:

- `cmake`
- `ninja`
- `make`
- `ctest`
- `rg`
- `git`
- `patch`

`apply_patch` always requires approval.

---

## GitHub Copilot sidecar

`src/sidecar/copilot-sidecar.js` is a separate Node.js process.

**Protocol:** JSON Lines over stdin/stdout.

```json
{"id":1,"method":"complete","params":{...}}
```

Current methods:

- `start`
- `complete`
- `list_models`
- `cancel`
- `stop`

Current behavior:

- the sidecar starts `CopilotClient` lazily, on demand;
- the model list is requested separately via `list_models`;
- each completion creates a separate Copilot session;
- active requests are tracked through `activeRequests`;
- `cancel` can cancel one specific request or all active ones;
- if something goes wrong, the user is advised to reinstall the sidecar via `cmake --install` or `npm install` in the sidecar directory and complete `copilot /login`.

---

## Completion / ghost text

### Completion assist

Current path:

```text
keypress -> isActivationCharSequence() -> createProcessor()
         -> AiCompletionProcessor::perform() -> async provider call
```

Key facts:

- trigger characters: `.`, `>`, `:`, `(`, newline;
- an additional auto-trigger on a word when the identifier length equals `completionMinChars`;
- defaults: `completionMinChars = 3`, `completionDelayMs = 500`;
- `completionModel` may be empty — then the agent model is used.

### Ghost text

`GhostTextManager`:

- watches document changes;
- debounces requests;
- takes context around the cursor;
- inserts the suggestion via `TextEditor::TextSuggestion` and `insertSuggestion()`.

So, in the current version, ghost text is based on Qt Creator's native suggestion API rather than a custom overlay.

---

## UI — Dock Widget

Current `AgentDockWidget` structure:

- top bar: status + New Chat / Apply Patch / Revert Patch / Copy Plan buttons;
- tabs: **Plan**, **Actions Log**, **Diff Preview**, **Approvals**, **Debug Log**;
- bottom row: goal editor, editable model combo, thinking combo, Run / Stop, Dry-run;
- diff preview includes a file list, per-line approval, and integration with `InlineDiffManager`.
- source layout: UI facade lives in `src/src/ui/AgentDockWidget.*`, with linked-files and project-session logic split into `src/src/linked_files/` and `src/src/session/`.

For `Ctrl+C` / `Ctrl+A`, `QShortcut` with `Qt::WidgetShortcut` is used directly on text widgets because Qt Creator globally intercepts some standard shortcuts.

---

## Settings

`SettingsPage` has three tabs:

1. **Providers**
   - active provider;
   - OpenAI-compatible URL / API key / model / thinking / temperature / max tokens;
   - Copilot model / Node path / sidecar path;
   - Local HTTP base URL / endpoint / simple mode / headers;
   - Ollama base URL / model.
2. **Code Completion**
   - enable checkbox;
   - completion model;
   - completion thinking;
   - completion reasoning effort;
   - min chars;
   - delay.
3. **Safety & Behavior**
   - max iterations;
   - max tool calls;
   - max diff lines;
   - max changed files;
   - dry-run default;
   - debug logging;
   - agent debug.

When adding a new setting, always implement the full dirty-state path in the same patch. That includes `settings_t` load/save, `settingsFromUi()`, `snapshot_t`, `captureSnapshot()`, `restoreSnapshot()`, and the settings-page dirty wiring via `installCheckSettingsDirtyTrigger(...)` and/or explicit `checkSettingsDirty` signal connections, so `Apply` and `Cancel` behave correctly immediately.

---

## Build, tests, installation

Current commands from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWITH_TESTS=ON -DCMAKE_PREFIX_PATH="<qt>;<qtcreator>"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Current unit tests:

- `qcai2_json`
- `qcai2_toolcall`

Installation:

```bash
cmake --install build
```

During install, CMake also copies the sidecar files and runs `npm install --no-audit --no-fund` in the installed sidecar directory.

---

## GitHub Actions

Current workflow `.github/workflows/build_cmake.yml`:

- runs on `push` and `pull_request`;
- builds Windows x64 / Windows arm64 / Linux x64 / Linux arm64 / macOS;
- configures CMake with `-DWITH_TESTS=ON`;
- runs build + tests;
- packages the artifact via `cmake --install`;
- on `v*` tags, publishes a GitHub Release with zip artifacts.

---

## Communication language

Communicate **in Ukrainian**.
