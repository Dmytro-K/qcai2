# GitHub Actions & Workflows

`build_cmake.yml` is the repository's current CI workflow for building, testing, installing, and packaging the plugin.

## When it runs

- On every `push`
- On every `pull_request`
- On version tags that match `v*`, which also trigger the release job

## Current build matrix

The workflow builds these artifacts:

- Windows x64
- Windows arm64
- Linux x64
- Linux arm64
- macOS universal package

The versions are pinned in the workflow `env:` block:

- `QT_VERSION: 6.10.2`
- `QT_CREATOR_VERSION: 18.0.2`
- `CMAKE_VERSION: 3.29.6`
- `NINJA_VERSION: 1.12.1`
- Node.js 22 for the Copilot sidecar packaging/install flow

## What the workflow does

For each matrix entry, the build job currently:

1. Checks out the repository
2. Installs CMake and Ninja with `lukka/get-cmake`
3. Installs extra Linux system libraries when needed
4. Installs Qt with `jurplel/install-qt-action@v4`
5. Downloads the Qt Creator development package with `qt-creator/install-dev-package@v2.1`
6. Sets up Node.js
7. Configures CMake with `-DWITH_TESTS=ON`
8. Builds the plugin with `cmake --build build --parallel`
9. Runs tests with `ctest --test-dir build --output-on-failure`
10. Installs into a temporary package root with `cmake --install`
11. Zips the installed payload and uploads it as a GitHub Actions artifact

## Release behavior

When the ref is a tag like `v1.2.3`, the `release` job:

- downloads all uploaded artifacts,
- flattens them into a `release/` directory,
- creates a GitHub release with `softprops/action-gh-release@v2`,
- attaches the zip files for all supported platforms.

## Notes for maintenance

- Linux Qt installs currently include the `icu` archive because the workflow needs those runtime pieces for Qt tools.
- Packaging is based on `cmake --install`, so sidecar files and any install-time scripts are included automatically.
- If the plugin gains new runtime files, ensure they are installed by CMake; the workflow packages whatever `cmake --install` produces.
