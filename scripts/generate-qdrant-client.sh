#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

tag="${1:-${QDRANT_OPENAPI_TAG:-}}"
output_dir="${2:-${QDRANT_CLIENT_OUTPUT_DIR:-${repo_root}/lib/qdrant_client/generated}}"
package_name="${QDRANT_CLIENT_PACKAGE_NAME:-qcai2_qdrant_client}"
cpp_namespace="${QDRANT_CLIENT_CPP_NAMESPACE:-qcai2::qdrant_client}"
model_prefix="${QDRANT_CLIENT_MODEL_PREFIX:-qdrant_}"

spec_ref="refs/heads/master"
if [[ -n "${tag}" ]]; then
    spec_ref="refs/tags/${tag}"
fi

default_spec_url="https://raw.githubusercontent.com/qdrant/qdrant/${spec_ref}/docs/redoc/master/openapi.json"
input_spec="${QDRANT_OPENAPI_SPEC:-${default_spec_url}}"

if [[ -z "${input_spec}" ]]; then
    echo "error: missing Qdrant OpenAPI input. Set QDRANT_OPENAPI_SPEC if you want to override the default URL." >&2
    exit 1
fi

if command -v openapi-generator-cli >/dev/null 2>&1; then
    generator_cmd=(openapi-generator-cli)
elif command -v npx >/dev/null 2>&1; then
    generator_cmd=(npx --yes @openapitools/openapi-generator-cli)
else
    echo "error: openapi-generator-cli not found. Install it or ensure npx is available." >&2
    exit 1
fi

mkdir -p "${output_dir}"

additional_properties=(
    "packageName=${package_name}"
    "cppNamespace=${cpp_namespace}"
    "modelNamePrefix=${model_prefix}"
    "optionalProjectFile=false"
    "makeOperationsVirtual=true"
    "addDownloadProgress=true"
    "contentCompression=true"
    "enumUnknownDefaultCase=true"
)

echo "Generating Qdrant Qt client from: ${input_spec}"
echo "Output directory: ${output_dir}"

"${generator_cmd[@]}" generate \
    -g cpp-qt-client \
    -i "${input_spec}" \
    -o "${output_dir}" \
    --additional-properties "$(IFS=,; echo "${additional_properties[*]}")"
