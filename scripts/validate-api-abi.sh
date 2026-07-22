#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
policy="${repo_root}/docs/api/API_ABI_POLICY.md"
baseline="${repo_root}/requirements/api/public-api-baseline.yaml"
evidence_dir="${repo_root}/out/evidence/api-abi"
validation="${evidence_dir}/validation.json"
header_baseline="${evidence_dir}/public-header-baseline.json"
missing=()

mapfile -t headers < <(
  find "${repo_root}/bsw" "${repo_root}/platform" "${repo_root}/integration" "${repo_root}/apps" \
    -path '*/include/openautosar/*' \
    -name '*.h' \
    -type f \
    -print | sort
)

json_array() {
  local first="true"
  printf '['
  for item in "$@"; do
    if [[ "${first}" == "true" ]]; then
      first="false"
    else
      printf ', '
    fi
    printf '"%s"' "${item}"
  done
  printf ']'
}

require_text() {
  local path="$1"
  local pattern="$2"
  local name="$3"
  if [[ ! -f "${path}" ]] || ! grep -Fq "${pattern}" "${path}"; then
    missing+=("${name}")
  fi
}

minimum_headers=0
if [[ -f "${baseline}" ]]; then
  minimum_headers="$(awk '/^minimum_public_headers:/ { print $2 }' "${baseline}")"
fi

if [[ ! -f "${policy}" ]]; then
  missing+=("api-abi-policy")
elif ! head -n 5 "${policy}" | grep -q "SPDX-License-Identifier: MIT"; then
  missing+=("api-abi-policy:spdx")
fi

if [[ ! -f "${baseline}" ]]; then
  missing+=("public-api-baseline")
elif ! head -n 5 "${baseline}" | grep -q "SPDX-License-Identifier: MIT"; then
  missing+=("public-api-baseline:spdx")
fi

require_text "${repo_root}/CMakeLists.txt" "VERSION 0.1.0" "cmake-project-version"
require_text "${repo_root}/CMakeLists.txt" \
  "CMAKE_CXX_VISIBILITY_PRESET hidden" \
  "symbol-visibility-preset"
require_text "${repo_root}/CMakeLists.txt" \
  "CMAKE_VISIBILITY_INLINES_HIDDEN YES" \
  "inline-visibility-hidden"
require_text "${repo_root}/CMakeLists.txt" \
  "OpenAutosarConfigVersion.cmake" \
  "package-version-metadata"
require_text "${repo_root}/CMakeLists.txt" \
  "NAMESPACE openautosar::" \
  "cmake-package-namespace"

for term in \
  "semantic versioning" \
  "symbol visibility" \
  "ABI baseline" \
  "deprecations" \
  "stable error-domain" \
  "internal implementation types"; do
  require_text "${policy}" "${term}" "policy:${term}"
done

for term in \
  "extension_namespace: openautosar::extension" \
  "semantic-versioning" \
  "symbol-visibility-control" \
  "abi-baseline-check" \
  "explicit-deprecation-policy" \
  "generated-api-compatibility-tests" \
  "stable-error-domains" \
  "no-internal-type-leakage"; do
  require_text "${baseline}" "${term}" "baseline:${term}"
done

if (( ${#headers[@]} < minimum_headers )); then
  missing+=("public-header-count")
fi

for header in "${headers[@]}"; do
  if ! head -n 5 "${header}" | grep -q "SPDX-License-Identifier: MIT"; then
    missing+=("${header#${repo_root}/}:spdx")
  fi
done

if (( ${#headers[@]} > 0 )) && rg -n "namespace[[:space:]]+ara\\b" "${headers[@]}" >/dev/null; then
  missing+=("non-standard-ara-namespace")
fi

mkdir -p "${evidence_dir}"
{
  printf '{\n'
  printf '  "schema": "openautosar.api-abi.public-header-baseline.v1",\n'
  printf '  "project_version": "0.1.0",\n'
  printf '  "namespace": "openautosar",\n'
  printf '  "public_header_count": %s,\n' "${#headers[@]}"
  printf '  "headers": [\n'
  first="true"
  for header in "${headers[@]}"; do
    relative="${header#${repo_root}/}"
    digest="$(sha256sum "${header}" | awk '{ print $1 }')"
    if [[ "${first}" == "true" ]]; then
      first="false"
    else
      printf ',\n'
    fi
    printf '    {"path": "%s", "sha256": "%s"}' "${relative}" "${digest}"
  done
  printf '\n  ]\n'
  printf '}\n'
} >"${header_baseline}"

status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.api-abi.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "policy": "docs/api/API_ABI_POLICY.md",\n'
  printf '  "baseline": "requirements/api/public-api-baseline.yaml",\n'
  printf '  "public_header_count": %s,\n' "${#headers[@]}"
  printf '  "minimum_public_headers": %s,\n' "${minimum_headers}"
  printf '  "public_header_baseline": "out/evidence/api-abi/public-header-baseline.json",\n'
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${validation}"

if [[ "${status}" != "valid" ]]; then
  printf 'API/ABI validation invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "API/ABI validation valid: ${validation}"
