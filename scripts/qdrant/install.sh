#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
default_repo_root="$(cd "${script_dir}/../.." && pwd -P)"

repo_root="${default_repo_root}"
enable_service="false"
start_service="false"

config_home="${XDG_CONFIG_HOME:-${HOME}/.config}"
data_home="${XDG_DATA_HOME:-${HOME}/.local/share}"

qdrant_config_dir="${config_home}/qdrant"
systemd_user_dir="${config_home}/systemd/user"
qdrant_data_dir="${data_home}/qdrant/storage"
qdrant_tls_dir=""
tls_dir_explicit="false"

usage() {
    cat <<'EOF'
Usage: scripts/install-qdrant.sh [options]

Create the required local directories for the bundled Qdrant config/service
files and install them into the user configuration.

Options:
  --repo <path>          Repository root that contains scripts/qdrant/.
  --config-dir <path>    Target config directory for Qdrant files.
  --data-dir <path>      Target Qdrant storage directory.
  --systemd-user-dir <path>
                         Target user systemd unit directory.
  --tls-dir <path>       Target TLS directory for generated certificates.
  --enable               Run systemctl --user enable qdrant.service after install.
  --start                Run systemctl --user restart qdrant.service after install.
  -h, --help             Show this help.
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
        --config-dir)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --config-dir" >&2
                exit 1
            fi
            qdrant_config_dir="$2"
            if [[ "${tls_dir_explicit}" != "true" ]]; then
                qdrant_tls_dir=""
            fi
            shift 2
            ;;
        --data-dir)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --data-dir" >&2
                exit 1
            fi
            qdrant_data_dir="$2"
            shift 2
            ;;
        --tls-dir)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --tls-dir" >&2
                exit 1
            fi
            qdrant_tls_dir="$2"
            tls_dir_explicit="true"
            shift 2
            ;;
        --systemd-user-dir)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --systemd-user-dir" >&2
                exit 1
            fi
            systemd_user_dir="$2"
            shift 2
            ;;
        --enable)
            enable_service="true"
            shift
            ;;
        --start)
            start_service="true"
            enable_service="true"
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

if [[ -z "${qdrant_tls_dir}" ]]; then
    qdrant_tls_dir="${qdrant_config_dir}/tls"
fi

config_source="${repo_root}/scripts/qdrant/config.yml"
service_source="${repo_root}/scripts/qdrant/qdrant.service"
cert_script="${repo_root}/scripts/qdrant/cert.sh"

if [[ ! -f "${config_source}" ]]; then
    echo "Missing Qdrant config template: ${config_source}" >&2
    exit 1
fi

if [[ ! -f "${service_source}" ]]; then
    echo "Missing Qdrant service template: ${service_source}" >&2
    exit 1
fi

if [[ ! -f "${cert_script}" ]]; then
    echo "Missing Qdrant certificate script: ${cert_script}" >&2
    exit 1
fi

config_target="${qdrant_config_dir}/config.yaml"
service_target="${systemd_user_dir}/qdrant.service"

ensure_pair_state() {
    local label="$1"
    local first_path="$2"
    local second_path="$3"

    if [[ -e "${first_path}" && -e "${second_path}" ]]; then
        return 0
    fi
    if [[ ! -e "${first_path}" && ! -e "${second_path}" ]]; then
        return 1
    fi

    echo "Incomplete ${label} files detected: ${first_path}, ${second_path}" >&2
    echo "Remove the partial files or complete the pair before rerunning install." >&2
    exit 1
}

mkdir -p \
    "${qdrant_config_dir}" \
    "${qdrant_tls_dir}" \
    "${systemd_user_dir}" \
    "${qdrant_data_dir}" \
    "${qdrant_data_dir}/snapshots" \
    "${qdrant_data_dir}/tmp"

install -m 0644 "${config_source}" "${config_target}"
install -m 0644 "${service_source}" "${service_target}"

pushd "${qdrant_tls_dir}" >/dev/null
if ensure_pair_state "CA" "ca.pem" "ca.key"; then
    echo "Reusing existing Qdrant CA files in ${qdrant_tls_dir}"
else
    bash "${cert_script}" --ca ca
fi

if ensure_pair_state "server certificate" "server.pem" "server.key"; then
    echo "Reusing existing Qdrant server certificate files in ${qdrant_tls_dir}"
else
    bash "${cert_script}" --node server
fi
popd >/dev/null

echo "Installed Qdrant config: ${config_target}"
echo "Installed Qdrant user service: ${service_target}"
echo "Prepared Qdrant TLS dir: ${qdrant_tls_dir}"
echo "Prepared Qdrant storage: ${qdrant_data_dir}"

if [[ "${enable_service}" == "true" || "${start_service}" == "true" ]]; then
    if ! command -v systemctl >/dev/null 2>&1; then
        echo "systemctl not found; cannot enable/start qdrant.service" >&2
        exit 1
    fi

    systemctl --user daemon-reload

    if [[ "${enable_service}" == "true" ]]; then
        systemctl --user enable qdrant.service
    fi

    if [[ "${start_service}" == "true" ]]; then
        systemctl --user restart qdrant.service
    fi
fi

cat <<EOF

Qdrant support files are installed.

Next steps:
  systemctl --user daemon-reload
  systemctl --user enable --now qdrant.service
EOF
