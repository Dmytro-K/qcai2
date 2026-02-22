# Qcai2 — AI Agent Plugin for Qt Creator

## Контекст для продовження розробки

Цей файл містить повний контекст проєкту для AI-асистента (Copilot, ChatGPT тощо).
Прочитай його перед початком роботи.

---

## Огляд

**Qcai2** — повнофункціональний AI Agent плагін для Qt Creator (Qt 6, C++23).
Реалізує автономний агентський цикл: plan → act (tools) → observe → verify → iterate.

### Ключові можливості
- Multi-provider LLM підтримка (OpenAI, Local HTTP, Ollama, GitHub Copilot)
- Tool-calling система (файли, пошук, білд, git, IDE)
- Safety/approval workflow (dry-run за замовчуванням)
- Dock widget UI з табами (Plan, Actions Log, Diff Preview, Approvals, Debug Log)
- AI code autocompletion через Qt Creator's CompletionAssistProvider
- Completion model selection (окрема модель для completion)
- Debug logging в UI та stderr
- Crash handler

---

## Середовище розробки

- **ОС**: Arch Linux
- **Qt**: 6.9.1 (`<path-to-Qt>/gcc_64/`)
- **Qt Creator**: v18.0.2 (пакети: qtcreator 18.0.2-2, qtcreator-devel 18.0.2-2)
- **Qt Creator headers**: `/usr/include/qtcreator/src/`
- **Qt Creator source** (для дослідження): `<qt-creator-source>` (tag v18.0.2)
- **Build dir**: `<build-dir>`
- **Збірка**: `cd <build-dir> && cmake --build .`

---

## Архітектура

### Структура файлів

```
qcai2/
├── CMakeLists.txt              — збірка плагіна (add_qtc_plugin)
├── qcai2constants.h            — константи (menu/action ID)
├── qcai2tr.h                   — макрос перекладу
├── Qcai2.json.in               — метадані плагіна
├── ai-prompt.txt               — оригінальна специфікація
├── resources/
│   └── aiagentplugin.qrc       — ресурси (іконки)
├── sidecar/
│   ├── copilot-sidecar.js      — Node.js bridge для GitHub Copilot SDK
│   ├── package.json
│   └── node_modules/           — npm залежності (@github/copilot-sdk)
└── src/
    ├── AiAgentPlugin.h/.cpp    — точка входу плагіна (IPlugin)
    ├── AgentController.h/.cpp  — агентський цикл
    ├── AgentDockWidget.h/.cpp  — dock widget UI
    ├── completion/
    │   ├── AiCompletionProvider.h/.cpp  — CompletionAssistProvider
    │   ├── AiCompletionProcessor.h/.cpp — IAssistProcessor (async)
    │   ├── CompletionTrigger.h/.cpp     — НЕ ВИКОРИСТОВУЄТЬСЯ (видалено з CMake)
    ├── context/
    │   └── EditorContext.h/.cpp         — контекст редактора
    ├── models/
    │   ├── AgentMessages.h/.cpp         — ChatMessage, PlanStep, JSON
    │   └── ToolCall.h/.cpp              — ToolCall struct
    ├── providers/
    │   ├── IAIProvider.h                — інтерфейс провайдера
    │   ├── OpenAICompatibleProvider.h/.cpp
    │   ├── LocalHttpProvider.h/.cpp
    │   ├── OllamaProvider.h/.cpp
    │   └── CopilotProvider.h/.cpp       — GitHub Copilot через Node.js sidecar
    ├── safety/
    │   └── SafetyPolicy.h/.cpp          — allowlist, approvals
    ├── settings/
    │   ├── Settings.h/.cpp              — налаштування (QSettings)
    │   └── SettingsPage.h/.cpp          — UI сторінка налаштувань (3 таби)
    ├── tools/
    │   ├── ITool.h                      — інтерфейс інструменту
    │   ├── ToolRegistry.h/.cpp          — реєстр інструментів
    │   ├── FileTools.h/.cpp             — read_file, apply_patch
    │   ├── SearchTools.h/.cpp           — search_repo (ripgrep)
    │   ├── BuildTools.h/.cpp            — run_build, run_tests, diagnostics
    │   ├── GitTools.h/.cpp              — git status/diff
    │   └── IdeTools.h/.cpp              — open_file_at_location
    └── util/
        ├── Logger.h/.cpp                — QCAI_INFO/DEBUG/WARN/ERROR макроси
        ├── CrashHandler.h/.cpp          — SIGSEGV/SIGABRT handler
        ├── ProcessRunner.h/.cpp         — QProcess wrapper
        ├── Diff.h/.cpp                  — unified diff validation
        └── Json.h/.cpp                  — JSON helpers
```

### Провайдери

| ID | Клас | Транспорт |
|---|---|---|
| `openai` | OpenAICompatibleProvider | HTTPS (QNetworkAccessManager) |
| `local` | LocalHttpProvider | HTTP (налаштовуваний endpoint) |
| `ollama` | OllamaProvider | HTTP (localhost:11434) |
| `copilot` | CopilotProvider | JSON Lines через stdin/stdout до Node.js sidecar |

### GitHub Copilot Sidecar

`sidecar/copilot-sidecar.js` — Node.js процес, що використовує `@github/copilot-sdk`.

**Протокол**: JSON Lines over stdin/stdout.
```
Request:  {"id":N, "method":"start"|"complete"|"cancel"|"stop", "params":{...}}
Response: {"id":N, "result":{...}} або {"id":N, "error":"msg"}
Streaming: {"id":N, "stream_delta":"text"}
```

**Оптимізації (поточна версія)**:
- Черга запитів (request queue): всі запити серіалізуються через `enqueue()` → `drainQueue()`, щоб уникнути race conditions в async readline handlers
- Кожен запит створює свіжу сесію (Copilot SDK сесії не можна перевикористовувати — "Timeout waiting for session.idle")
- Cancel обробляється одразу (поза чергою) — встановлює `activeRequestId = null`, щоб stale відповіді відкидалися
- Client створюється один раз при `start`, живе до `stop`

---

## AI Code Completion

### Архітектура

```
Keystroke → Qt Creator's process() → isActivationCharSequence() → createProcessor()
         → AiCompletionProcessor::perform() → async complete() → setAsyncProposalAvailable()
```

### Ключові рішення

1. **isActivationCharSequence замість invokeAssist**: Прямий виклик `invokeAssist()` викликав segfault через re-entrancy в `cancelCurrentRequest()` Qt Creator. Використання `isActivationCharSequence` працює через безпечний шлях `process()`.

2. **`wordLen == completionMinChars` (не `>=`)**: Тригериться рівно один раз при досягненні N символів слова. `>=` скасовував попередній запит при кожному новому символі.

3. **cancel() не викликає provider->cancel()**: Щоб уникнути re-entrancy crash. Замість цього `m_alive` shared_ptr guard захищає від use-after-free в async callback.

4. **Окрема модель для completion**: `settings().completionModel` (порожній = та сама модель, що й для агента).

5. **Qt Creator setting**: Completion trigger в Settings → Text Editor → Completion повинен бути "When Triggered" або "Always" (не "Manually"), щоб `isActivationCharSequence` викликалась.

### Відомі обмеження

- Completion через Copilot SDK займає ~10-50 сек (мережева затримка + створення сесії)
- `isContinuationChar` за замовчуванням повертає true для букв/цифр/_ — попередній запит не скасовується при продовженні набору
- `handleCancel` в sidecar реалізований, плагін відправляє cancel через `CopilotProvider::cancel()`
- CompletionTrigger.h/.cpp існують на диску, але видалені з CMakeLists.txt

---

## UI — Dock Widget

### Структура
- **Верхній тулбар**: Run, Stop, Dry-run checkbox, статус
- **Таби**: Plan | Actions Log | Diff Preview | Approvals | Debug Log
- **Нижня панель**: Apply Patch, Revert Patch, Copy Plan
- **Введення**: QTextEdit для goal внизу (chat-like layout)

### Ctrl+C/A

Qt Creator перехоплює Ctrl+C глобально через ActionManager. Рішення: `QShortcut` з `Qt::WidgetShortcut` контекстом прямо на кожному `QPlainTextEdit` (logView, diffView, debugLogView).

---

## Settings

Три таби:
1. **Providers** — вибір провайдера, URL, API key, модель (editable combo), temperature, max tokens
2. **Code Completion** — enabled checkbox, min chars, delay, completion model (окремий combo)
3. **Safety & Behavior** — max iterations, max tool calls, max diff lines, dry-run, debug logging

### Моделі в combo boxes

**Main model combo** (Copilot): gpt-5.3-codex, gpt-5.2-codex, gpt-5.2, gpt-5.1-codex, gpt-5.1, gpt-5, gpt-4.1, o3, o4-mini, claude-opus-4-6, claude-sonnet-4-6, claude-sonnet-4-5, claude-haiku-4-5, gemini-3.1-pro, gemini-3-pro, gemini-3-flash, gemini-2.5-pro, grok-code-fast-1, та ін.

**Completion model combo**: "(Same as agent model)" (перший елемент, зберігає порожній рядок) + швидкі моделі: gpt-5-mini, gpt-4.1, claude-haiku-4-5, gemini-3-flash, grok-code-fast-1, та ін.

---

## Відомі проблеми та TODO

### Працює
- [x] Збірка плагіна
- [x] Dock widget з UI
- [x] Підключення до Copilot SDK через sidecar
- [x] AI completion (async, через isActivationCharSequence)
- [x] Completion model selection
- [x] Debug logging (UI + sidecar stderr)
- [x] Crash handler
- [x] Черга запитів в sidecar (уникнення створення зайвих сесій)
- [x] Cancel команда від плагіна до sidecar

### Потребує перевірки
- [ ] Ctrl+C копіювання в dock widget (QShortcut з WidgetShortcut)
- [ ] Completion popup з'являється після AI відповіді

### Можливі покращення
- [ ] Completion provider selector (окремий від agent provider)
- [ ] Streaming для completion (швидший перший токен)
- [ ] Live settings update (зараз деякі налаштування потребують перезапуску)
- [ ] Покращити FIM prompt (fill-in-the-middle)
- [ ] AbortController в sidecar для реального скасування HTTP запитів

---

## Критичні деталі Qt Creator (v18.0.2)

### CodeAssistant re-entrancy crash

**Шлях**: `cancelCurrentRequest()` → `m_processor->cancel()` → sync callback → `invalidateCurrentRequestData()` → `m_processor = nullptr` → `m_processor->running()` = CRASH

**Два шляхи до completion**:
- `invoke()` (через `invokeAssist`) → `requestProposal` → `cancelCurrentRequest` (UNSAFE)
- `process()` (через keypress) → `requestActivationCharProposal` → перевіряє `isWaitingForProposal()` перед cancel (SAFE)

**Наш підхід**: використовуємо тільки безпечний шлях через `isActivationCharSequence`.

### SOFT ASSERT `!running()`

```
SOFT ASSERT: "!running()" in languageclientcompletionassist.cpp:417
```

Це від Language Client (clangd/LSP), НЕ від нашого плагіна. Безпечне попередження — Qt Creator сам скасовує попередній запит.

---

## Мова спілкування

Спілкуватися **українською**.
