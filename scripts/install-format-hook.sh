#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
default_repo_root="$(cd "${script_dir}/.." && pwd -P)"

repo_root="${default_repo_root}"
force="false"

usage() {
    cat <<'EOF'
Usage: scripts/install-format-hook.sh [--repo <path>] [--force]

Installs a managed git pre-commit hook that rejects commits when staged
C/C++/ObjC files are not clang-formatted.

Options:
  --repo <path>  Repository root where the hook should be installed.
  --force        Overwrite an existing non-managed pre-commit hook.
  -h, --help     Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --repo" >&2
                exit 1
            fi
            repo_root="$2"
            shift 2
            ;;
        --force)
            force="true"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

repo_root="$(cd "${repo_root}" && pwd -P)"

if ! git -C "${repo_root}" rev-parse --show-toplevel >/dev/null 2>&1; then
    echo "Not a git repository: ${repo_root}" >&2
    exit 1
fi

git_dir="$(git -C "${repo_root}" rev-parse --absolute-git-dir)"
hook_path="${git_dir}/hooks/pre-commit"
mkdir -p "$(dirname "${hook_path}")"

managed_marker="# qcai2-format-pre-commit-hook"

if [[ -f "${hook_path}" ]] && ! grep -Fq "${managed_marker}" "${hook_path}"; then
    if [[ "${force}" != "true" ]]; then
        echo "Refusing to overwrite existing non-managed hook: ${hook_path}" >&2
        echo "Re-run with --force to replace it." >&2
        exit 1
    fi
fi

cat > "${hook_path}" <<'EOF'
#!/usr/bin/env bash
# qcai2-format-pre-commit-hook

set -euo pipefail

git_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
repo_root="$(git --git-dir="${git_dir}" rev-parse --show-toplevel)"
cd "${repo_root}"

find_clang_format() {
    local candidate
    if [[ -n "${CLANG_FORMAT:-}" ]] && command -v "${CLANG_FORMAT}" >/dev/null 2>&1; then
        command -v "${CLANG_FORMAT}"
        return 0
    fi
    for candidate in clang-format clang-format-18 clang-format-17; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done
    return 1
}

is_supported_file() {
    case "$1" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.ipp|*.inl|*.m|*.mm)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

clang_format="$(find_clang_format || true)"
if [[ -z "${clang_format}" ]]; then
    echo "clang-format was not found. Install it or set CLANG_FORMAT." >&2
    exit 1
fi

staged_files=()
while IFS= read -r -d '' path; do
    if is_supported_file "${path}"; then
        staged_files+=("${path}")
    fi
done < <(git --git-dir="${git_dir}" --work-tree="${repo_root}" diff --cached --name-only -z --diff-filter=ACMR --)

if [[ ${#staged_files[@]} -eq 0 ]]; then
    exit 0
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/qcai2-format-hook.XXXXXX")"
cleanup() {
    rm -rf "${tmpdir}"
}
trap cleanup EXIT

failed="false"
for relative_path in "${staged_files[@]}"; do
    staged_path="${tmpdir}/staged/${relative_path}"
    formatted_path="${tmpdir}/formatted/${relative_path}"
    mkdir -p "$(dirname "${staged_path}")" "$(dirname "${formatted_path}")"

    if ! git --git-dir="${git_dir}" --work-tree="${repo_root}" show ":${relative_path}" > "${staged_path}"; then
        echo "Failed to read staged file: ${relative_path}" >&2
        failed="true"
        continue
    fi

    if ! "${clang_format}" --style=file --fallback-style=none \
            --assume-filename="${repo_root}/${relative_path}" < "${staged_path}" > "${formatted_path}"; then
        echo "clang-format failed for staged file: ${relative_path}" >&2
        failed="true"
        continue
    fi

    if cmp -s "${staged_path}" "${formatted_path}"; then
        continue
    fi

    if [[ "${failed}" != "true" ]]; then
        echo "Formatting check failed for staged files:" >&2
    fi
    failed="true"
    diff -u --label "a/${relative_path}" --label "b/${relative_path}" \
        "${staged_path}" "${formatted_path}" >&2 || true
done

if [[ "${failed}" == "true" ]]; then
    cat >&2 <<'EOF_MSG'
Apply formatting, re-stage the files, and try again.

If you have a configured CMake build directory, you can use:
  cmake --build <build-dir> --target format-changed-files-apply
EOF_MSG
    exit 1
fi
EOF

chmod +x "${hook_path}"
echo "Installed qcai2 formatting pre-commit hook at ${hook_path}"
