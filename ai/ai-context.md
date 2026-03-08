# qcai2 — AI Agent Plugin for Qt Creator

## Контекст для продовження розробки

Цей файл має відображати поточний стан репозиторію, а не локальне середовище якоїсь однієї машини.

---

## Огляд

**qcai2** — плагін для Qt Creator на Qt 6 / C++23 з автономним агентським циклом:

`plan → act (tools) → observe → verify → iterate`

Поточні можливості:

- багатопровайдерний LLM-шар: `openai`, `copilot`, `local`, `ollama`;
- tool-calling для файлів, пошуку, збірки, тестів, git і IDE-навігації;
- dock widget із табами Plan / Actions Log / Diff Preview / Approvals / Debug Log;
- AI completion через `CompletionAssistProvider`;
- ghost text через `GhostTextManager` і Qt Creator `TextSuggestion`;
- inline diff markers через `InlineDiffManager`;
- dry-run за замовчуванням, approval workflow, sandboxed paths;
- debug logging і crash handler.

---

## Актуальна структура проєкту

```text
qcai2/
├── CMakeLists.txt
├── README.md
├── ai/
│   └── ai-context.md
├── cmake/
│   ├── build-docs.cmake
│   ├── cmake_install.cmake
│   ├── format-changed-files.cmake
│   └── install-sidecar-deps.cmake
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
│   │   ├── AiAgentPlugin.h/.cpp
│   │   ├── AgentController.h/.cpp
│   │   ├── AgentDockWidget.h/.cpp
│   │   ├── completion/
│   │   │   ├── AiCompletionProvider.h/.cpp
│   │   │   ├── AiCompletionProcessor.h/.cpp
│   │   │   ├── CompletionTrigger.h/.cpp     — є в дереві, але не входить у CMake target
│   │   │   ├── GhostTextManager.h/.cpp
│   │   │   └── GhostTextOverlay.h/.cpp      — є в дереві, але не входить у CMake target
│   │   ├── context/
│   │   │   └── EditorContext.h/.cpp
│   │   ├── diff/
│   │   │   └── InlineDiffManager.h/.cpp
│   │   ├── models/
│   │   │   ├── AgentMessages.h/.cpp
│   │   │   └── ToolCall.h/.cpp
│   │   ├── providers/
│   │   │   ├── IAIProvider.h
│   │   │   ├── OpenAICompatibleProvider.h/.cpp
│   │   │   ├── CopilotProvider.h/.cpp
│   │   │   ├── LocalHttpProvider.h/.cpp
│   │   │   └── OllamaProvider.h/.cpp
│   │   ├── safety/
│   │   │   └── SafetyPolicy.h/.cpp
│   │   ├── settings/
│   │   │   ├── Settings.h/.cpp
│   │   │   └── SettingsPage.h/.cpp
│   │   ├── tools/
│   │   │   ├── BuildTools.h/.cpp
│   │   │   ├── FileTools.h/.cpp
│   │   │   ├── GitTools.h/.cpp
│   │   │   ├── IdeTools.h/.cpp
│   │   │   ├── ITool.h
│   │   │   ├── SearchTools.h/.cpp
│   │   │   └── ToolRegistry.h/.cpp
│   │   └── util/
│   │       ├── CrashHandler.h/.cpp
│   │       ├── Diff.h/.cpp
│   │       ├── Json.h/.cpp
│   │       ├── Logger.h/.cpp
│   │       └── ProcessRunner.h/.cpp
│   └── tests/
│       ├── tst_json.cpp
│       └── tst_toolcall.cpp
```

---

## Провайдери

| ID | Клас | Нотатка |
|---|---|---|
| `openai` | `OpenAICompatibleProvider` | базовий OpenAI-compatible HTTP транспорт |
| `copilot` | `CopilotProvider` | через Node.js sidecar і `@github/copilot-sdk` |
| `local` | `LocalHttpProvider` | локальний/кастомний HTTP endpoint |
| `ollama` | `OllamaProvider` | локальний Ollama server |

Поточні дефолти з `Settings`:

- `provider = openai`
- `modelName = gpt-5.2`
- `thinkingLevel = medium`
- `temperature = 0.2`
- `maxTokens = 4096`
- `copilotModel = gpt-4o`
- `localBaseUrl = http://localhost:8080`
- `localEndpointPath = /v1/chat/completions`
- `ollamaBaseUrl = http://localhost:11434`
- `ollamaModel = llama4`

---

## Tool registry

Зараз у `AiAgentPlugin::registerTools()` реєструються:

- `read_file`
- `apply_patch`
- `search_repo`
- `run_build`
- `run_tests`
- `show_diagnostics`
- `git_status`
- `git_diff`
- `open_file_at_location`

`SafetyPolicy` автоматично allowlist-ить команди:

- `cmake`
- `ninja`
- `make`
- `ctest`
- `rg`
- `git`
- `patch`

`apply_patch` завжди вимагає approval.

---

## GitHub Copilot sidecar

`src/sidecar/copilot-sidecar.js` — окремий Node.js процес.

**Протокол:** JSON Lines через stdin/stdout.

```json
{"id":1,"method":"complete","params":{...}}
```

Поточні методи:

- `start`
- `complete`
- `list_models`
- `cancel`
- `stop`

Поточна поведінка:

- sidecar стартує `CopilotClient` lazy-on-demand;
- список моделей запитується окремо через `list_models`;
- кожен completion створює окрему Copilot session;
- активні запити відстежуються через `activeRequests`;
- `cancel` може скасувати один конкретний запит або всі активні;
- у разі проблем користувачу підказується перевстановити sidecar через `cmake --install` або `npm install` в директорії sidecar і пройти `copilot /login`.

---

## Completion / ghost text

### Completion assist

Поточний шлях:

```text
keypress -> isActivationCharSequence() -> createProcessor()
         -> AiCompletionProcessor::perform() -> async provider call
```

Ключові факти:

- тригерні символи: `.`, `>`, `:`, `(`, newline;
- додатковий автотригер на слові, коли довжина ідентифікатора дорівнює `completionMinChars`;
- дефолт: `completionMinChars = 3`, `completionDelayMs = 500`;
- `completionModel` може бути порожнім — тоді використовується модель агента.

### Ghost text

`GhostTextManager`:

- слідкує за змінами документа;
- дебаунсить запити;
- бере контекст навколо курсора;
- вставляє suggestion через `TextEditor::TextSuggestion` і `insertSuggestion()`.

Тобто в актуальній версії ghost text базується на нативному suggestion API Qt Creator, а не на кастомному overlay.

---

## UI — Dock Widget

Поточна структура `AgentDockWidget`:

- верхня панель: статус + кнопки New Chat / Apply Patch / Revert Patch / Copy Plan;
- таби: **Plan**, **Actions Log**, **Diff Preview**, **Approvals**, **Debug Log**;
- нижній ряд: goal editor, editable model combo, thinking combo, Run / Stop, Dry-run;
- diff preview має список файлів, per-line approval та інтеграцію з `InlineDiffManager`.

Для `Ctrl+C` / `Ctrl+A` використовуються `QShortcut` з `Qt::WidgetShortcut` прямо на текстових віджетах, бо Qt Creator глобально перехоплює частину стандартних shortcut-ів.

---

## Settings

У `SettingsPage` є три таби:

1. **Providers**
   - активний провайдер;
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

---

## Збірка, тести, інсталяція

Актуальні команди з кореня репозиторію:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWITH_TESTS=ON -DCMAKE_PREFIX_PATH="<qt>;<qtcreator>"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Поточні unit tests:

- `qcai2_json`
- `qcai2_toolcall`

Інсталяція:

```bash
cmake --install build
```

Під час install CMake також копіює sidecar-файли й запускає `npm install --no-audit --no-fund` у встановленій sidecar-директорії.

---

## GitHub Actions

Поточний workflow `.github/workflows/build_cmake.yml`:

- запускається на `push` і `pull_request`;
- будує Windows x64 / Windows arm64 / Linux x64 / Linux arm64 / macOS;
- конфігурує CMake з `-DWITH_TESTS=ON`;
- запускає build + tests;
- пакує артефакт через `cmake --install`;
- на тегах `v*` публікує GitHub Release з zip-артефактами.

---

## Мова спілкування

Спілкуватися **українською**.
