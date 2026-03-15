# Coding Style

This project follows a consistent, readable Qt/C++ style with formatting driven by
the repository `.clang-format`.

## General Principles

- Prefer simple, explicit code over clever shortcuts.
- Keep changes surgical and local to the problem being solved.
- Match existing naming, structure, and Qt idioms in nearby code.
- Avoid silent fallbacks and hidden control flow.

## Formatting

- indentation: 4 spaces.
- Keep include order and spacing compatible with `.clang-format`.
- Use brace placement consistent with the repository formatter.
- Do not write one-line `if`, `for`, or function bodies.
- Keep comments short and useful; explain intent, not obvious syntax.

## Control Statements

All `if`, `for`, `while`, and `do-while` statements must always use curly braces,
even for a single statement body.

Preferred:

```cpp
if (ready)
{
    run();
}
```

Avoid:

```cpp
if (ready)
    run();
```

## C++ and Qt Conventions

- Use `QStringLiteral` for fixed string literals in Qt code.
- Prefer Qt containers and Qt types where the surrounding code already uses them.
- Use `nullptr` instead of `0` or `NULL`.
- Write zero and non-zero checks with explicit comparisons such as `== 0` or `!= 0`.
- For `bool` values and `bool`-returning expressions, use explicit comparisons to `true` or `false`.
- Do not rely on operator precedence when readability can suffer; use parentheses to make evaluation order explicit.
- Keep pointer alignment consistent with the formatter (`Type *name`).
- Prefer scoped, strongly typed helpers over loose global utilities.

## Error Handling

- Surface errors explicitly; do not swallow failures silently.
- Reuse existing logging and error-reporting patterns.
- Do not add broad catch-all handling unless there is a clear repository pattern.

## Tests and Maintenance

- Add or update tests when changing behavior or parsing logic.
- Prefer targeted unit tests for non-UI logic.
- Keep documentation and configuration in sync with behavior changes.

## Final Check

Before finishing, run formatting/build/test commands that already exist in the
project and make sure your changes still match surrounding code.
