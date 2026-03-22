# AGENTS.md

## Project

`qcai2` is a Qt Creator plugin written in Qt 6 / C++23. It provides an AI agent dock, tool-calling, approvals, inline diff review, code completion, ghost text, and persistent per-project chat context.

## Communication

- Communicate in Ukrainian.
- Keep changes surgical and aligned with the existing architecture.

## Architecture hotspots

- `src/src/agent_controller.*` and `src/src/agent_run_state_machine.*` own run orchestration, safe points, tool execution, approvals, soft steer, and compaction flow.
- `src/src/ui/agent_dock_widget.*` is the main dock UI facade. Request queue and steering UX belong here or in its supporting UI helpers.
- `src/src/session/` and `src/src/linked_files/` contain dock session state and linked-file context logic.
- `src/src/context/chat_context_*` owns persistent conversation state under `.qcai2/chat-context/`. Compaction persists summaries there, and future prompt reconstruction must use the latest summary plus only post-summary messages.
- `src/src/settings/` owns provider/behavior settings. New settings must implement the full settings path in the same patch: load/save, `settingsFromUi()`, snapshot capture/restore, and dirty-state wiring so `Apply` / `Cancel` work immediately.
- `src/tests/` contains focused regression tests. Behavior changes should come with tests.

## Coding rules

- Follow `docs/CODING_STYLE.md`.
- Use `snake_case` for fields, variables, functions, and methods.
- Use `snake_case_t` for classes, structs, enums, aliases, and other types.
- Do not use `m_` prefixes or trailing underscores for fields. Access members through `this->...`.
- Use `UPPER_CASE` with underscores for enum values and `#define` constants.
- Always use braces for control statements.
- Prefer `QStringLiteral` for fixed Qt strings where practical.
- Surface errors explicitly; avoid silent fallbacks and broad catch-all handling.
- Design modules, classes, interfaces, and functions to be easy to cover with focused unit tests.
- Prefer explicit dependencies, narrow responsibilities, and logic that can be exercised without unnecessary UI, filesystem, network, or IDE side effects.
- Keep function implementations in `.cpp` files unless header placement is genuinely required, such as templates or other necessary inline definitions.
- Document every file and every important declaration, including classes, structs, enums, functions, methods, templates, and comparable API surface.
- Document important architecture, behavior, and non-trivial code paths when that context helps future maintenance.
- Before implementing any non-trivial feature, ask clarifying questions about the desired UX, behavior, constraints, edge cases, and preferred implementation options, and explicitly present 2–4 viable implementation approaches with trade-offs before choosing one.
- After every patch run code autoformating: `cmake -DQCAI2_FORMAT_DRY_RUN=FALSE -P cmake/format-files.cmake` in project root directory.

## Change guidance

- Reuse existing helpers and shared services instead of duplicating logic.
- Keep docs and user-visible text in sync with behavior changes.
- Cover behavior changes and new logic with tests.

## Validation

- Use the existing CMake workflow.
- Default validation:
  - `cmake --build <build-dir> --parallel`
  - `ctest --test-dir <build-dir> --output-on-failure --parallel`
- Prefer focused tests for touched areas first, then broader validation when behavior changes.
