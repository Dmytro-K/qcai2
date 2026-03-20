#!/usr/bin/env bash

set -euo pipefail

mode=""
force="false"
common_name=""
days=""
name=""
ca_cert_path=""
ca_key_path=""
encrypt_key="false"

curve_name="prime256v1"

dns_names=("localhost")
ip_addresses=("127.0.0.1" "::1")
qdrant_cert_tmpdir=""

usage() {
    cat <<EOF
Usage:
  scripts/qdrant/cert.sh --ca [options] <name>
  scripts/qdrant/cert.sh --node [options] <name>

Generate TLS material in the current working directory.

Modes:
  --ca                  Generate `<name>.pem` and `<name>.key`.
  --node                Generate `<name>.pem` and `<name>.key`, signed by the CA.

Options:
  --cn <name>           Certificate common name.
  --days <count>        Certificate validity in days.
  --ca-cert <path>      CA certificate path for --node. Default: ./ca.pem
  --ca-key <path>       CA private key path for --node. Default: ./ca.key
  --pass                Encrypt private keys with AES-256 and let OpenSSL ask for the password.
  --dns <name>          Add a DNS SAN entry. Can be repeated.
  --ip <address>        Add an IP SAN entry. Can be repeated.
  --force               Overwrite existing output files.
  -h, --help            Show this help.

Defaults:
  Keys use EC ${curve_name}. Add `--pass` to encrypt them with AES-256.
  --ca:   CN=qdrant local CA, days=3650
  --node: CN=localhost, days=825, SANs=localhost,127.0.0.1,::1

Examples:
  scripts/qdrant/cert.sh --ca --pass ca
  scripts/qdrant/cert.sh --node server
  scripts/qdrant/cert.sh --node --ca-cert ca.pem --ca-key ca.key client
EOF
}

fail() {
    echo "error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

cleanup_tmpdir() {
    if [[ -n "${qdrant_cert_tmpdir}" ]]; then
        rm -rf "${qdrant_cert_tmpdir}"
        qdrant_cert_tmpdir=""
    fi
}

ensure_missing_or_forced() {
    local path
    for path in "$@"; do
        if [[ -e "${path}" && "${force}" != "true" ]]; then
            fail "refusing to overwrite existing file: ${path} (use --force)"
        fi
    done
}

make_subject() {
    printf '/CN=%s' "$1"
}

generate_encrypted_ec_key() {
    local output_key="$1"
    local tmp_key="$2"

    openssl ecparam -genkey -name "${curve_name}" -noout -out "${tmp_key}"
    if [[ "${encrypt_key}" == "true" ]]; then
        openssl ec -in "${tmp_key}" -out "${output_key}" -aes256
    else
        openssl ec -in "${tmp_key}" -out "${output_key}"
    fi
    rm -f "${tmp_key}"
}

write_ca_extfile() {
    local extfile="$1"
    {
        echo '[v3_ca]'
        echo 'basicConstraints = critical,CA:TRUE,pathlen:0'
        echo 'keyUsage = critical,keyCertSign,cRLSign'
        echo 'subjectKeyIdentifier = hash'
        echo 'authorityKeyIdentifier = keyid:always'
    } > "${extfile}"
}

write_node_extfile() {
    local extfile="$1"
    {
        echo '[v3_req]'
        echo 'basicConstraints = critical,CA:FALSE'
        echo 'keyUsage = critical,digitalSignature,keyAgreement'
        echo 'extendedKeyUsage = serverAuth,clientAuth'
        echo 'subjectAltName = @alt_names'
        echo
        echo '[alt_names]'

        local index=1
        local dns_name
        for dns_name in "${dns_names[@]}"; do
            [[ -n "${dns_name}" ]] || continue
            printf 'DNS.%d = %s\n' "${index}" "${dns_name}"
            index=$((index + 1))
        done

        index=1
        local ip_address
        for ip_address in "${ip_addresses[@]}"; do
            [[ -n "${ip_address}" ]] || continue
            printf 'IP.%d = %s\n' "${index}" "${ip_address}"
            index=$((index + 1))
        done
    } > "${extfile}"
}

generate_ca() {
    local output_cert="${name}.pem"
    local output_key="${name}.key"
    local csr_file="${name}.csr"
    local tmp_key="${name}-tmp.key"
    local ca_cn="${common_name:-qdrant local CA}"
    local valid_days="${days:-3650}"

    ensure_missing_or_forced "${output_cert}" "${output_key}" "${csr_file}"

    local tmpdir
    tmpdir="$(mktemp -d)"
    qdrant_cert_tmpdir="${tmpdir}"
    trap cleanup_tmpdir EXIT

    local extfile="${tmpdir}/ca-ext.cnf"
    write_ca_extfile "${extfile}"

    generate_encrypted_ec_key "${output_key}" "${tmp_key}"
    openssl req \
        -new \
        -key "${output_key}" \
        -sha256 \
        -out "${csr_file}" \
        -subj "$(make_subject "${ca_cn}")"
    openssl x509 \
        -req \
        -in "${csr_file}" \
        -signkey "${output_key}" \
        -out "${output_cert}" \
        -days "${valid_days}" \
        -sha256 \
        -extfile "${extfile}" \
        -extensions v3_ca

    chmod 0600 "${output_key}"
    rm -f "${csr_file}"
    trap - EXIT
    cleanup_tmpdir

    echo "Generated CA key:  ${PWD}/${output_key}"
    echo "Generated CA cert: ${PWD}/${output_cert}"
}

generate_node() {
    local output_cert="${name}.pem"
    local output_key="${name}.key"
    local csr_file="${name}.csr"
    local tmp_key="${name}-tmp.key"
    local node_cn="${common_name:-localhost}"
    local valid_days="${days:-825}"

    if [[ -z "${ca_cert_path}" ]]; then
        ca_cert_path="ca.pem"
    fi
    if [[ -z "${ca_key_path}" ]]; then
        ca_key_path="ca.key"
    fi

    [[ -f "${ca_cert_path}" ]] || fail "missing CA certificate: ${ca_cert_path}"
    [[ -f "${ca_key_path}" ]] || fail "missing CA private key: ${ca_key_path}"

    ensure_missing_or_forced "${output_cert}" "${output_key}" "${csr_file}"

    local tmpdir
    tmpdir="$(mktemp -d)"
    qdrant_cert_tmpdir="${tmpdir}"
    trap cleanup_tmpdir EXIT

    local extfile="${tmpdir}/node-ext.cnf"
    local serial_file="${tmpdir}/ca.srl"
    write_node_extfile "${extfile}"

    generate_encrypted_ec_key "${output_key}" "${tmp_key}"
    openssl req \
        -new \
        -key "${output_key}" \
        -out "${csr_file}" \
        -sha256 \
        -subj "$(make_subject "${node_cn}")"

    openssl x509 \
        -req \
        -in "${csr_file}" \
        -CA "${ca_cert_path}" \
        -CAkey "${ca_key_path}" \
        -CAserial "${serial_file}" \
        -CAcreateserial \
        -out "${output_cert}" \
        -days "${valid_days}" \
        -sha256 \
        -extfile "${extfile}" \
        -extensions v3_req

    chmod 0600 "${output_key}"
    rm -f "${csr_file}"
    trap - EXIT
    cleanup_tmpdir

    echo "Generated node key:  ${PWD}/${output_key}"
    echo "Generated node cert: ${PWD}/${output_cert}"
    echo "Used CA cert:        ${ca_cert_path}"
    echo "Used CA key:         ${ca_key_path}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ca)
            [[ -z "${mode}" ]] || fail "choose only one mode: --ca or --node"
            mode="ca"
            shift
            ;;
        --node)
            [[ -z "${mode}" ]] || fail "choose only one mode: --ca or --node"
            mode="node"
            shift
            ;;
        --cn)
            [[ $# -ge 2 ]] || fail "missing value for --cn"
            common_name="$2"
            shift 2
            ;;
        --days)
            [[ $# -ge 2 ]] || fail "missing value for --days"
            days="$2"
            shift 2
            ;;
        --ca-cert)
            [[ $# -ge 2 ]] || fail "missing value for --ca-cert"
            ca_cert_path="$2"
            shift 2
            ;;
        --ca-key)
            [[ $# -ge 2 ]] || fail "missing value for --ca-key"
            ca_key_path="$2"
            shift 2
            ;;
        --pass)
            encrypt_key="true"
            shift
            ;;
        --dns)
            [[ $# -ge 2 ]] || fail "missing value for --dns"
            dns_names+=("$2")
            shift 2
            ;;
        --ip)
            [[ $# -ge 2 ]] || fail "missing value for --ip"
            ip_addresses+=("$2")
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
            if [[ -z "${name}" ]]; then
                name="$1"
                shift
                continue
            fi
            fail "unexpected positional argument: $1"
            ;;
    esac
done

require_command openssl
[[ -n "${mode}" ]] || fail "missing mode: use --ca or --node"
[[ -n "${name}" ]] || fail "missing certificate name"

case "${mode}" in
    ca)
        generate_ca
        ;;
    node)
        generate_node
        ;;
esac
