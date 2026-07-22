#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="${repo_root}/tools/qualification/tool-confidence.yaml"
evidence="${repo_root}/out/evidence/tool-confidence/validation.json"
missing=()

required_classes=(
  "development-only"
  "verification"
  "code-generator"
  "manifest-generator"
  "release-signing"
  "evidence-generator"
)

generator_tools=(
  "oa-model-generator"
  "oa-manifest-summary-generator"
  "oa-oem-evidence-exporter"
  "oa-debug-bundle-generator"
  "oa-supplier-delivery-exporter"
  "oa-classic-integration-validator"
  "oa-program-governance-validator"
  "oa-implementation-plan-validator"
)

generator_fields=(
  "inputs"
  "outputs"
  "possible_tool_failures"
  "independent_validation"
  "test_suite"
  "version"
  "reproducibility"
  "approval_status"
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

record_has_field() {
  local tool_id="$1"
  local field="$2"
  awk -v id="  - id: ${tool_id}" -v field="    ${field}:" '
    $0 == id { in_record = 1; next }
    in_record && /^  - id: / { exit }
    in_record && index($0, field) == 1 { found = 1; exit }
    END { exit(found ? 0 : 1) }
  ' "${manifest}"
}

if [[ ! -f "${manifest}" ]]; then
  missing+=("tool-confidence-manifest")
else
  if ! head -n 5 "${manifest}" | grep -q "SPDX-License-Identifier: MIT"; then
    missing+=("tool-confidence-manifest:spdx")
  fi
  if ! grep -Fq "schema: openautosar.tool-confidence.v1" "${manifest}"; then
    missing+=("tool-confidence-schema")
  fi
fi

for class in "${required_classes[@]}"; do
  if [[ ! -f "${manifest}" ]] || ! grep -Fq "  - ${class}" "${manifest}"; then
    missing+=("tool-class:${class}")
  fi
done

for tool_id in "${generator_tools[@]}"; do
  if [[ ! -f "${manifest}" ]] || ! grep -Fq "  - id: ${tool_id}" "${manifest}"; then
    missing+=("generator:${tool_id}")
    continue
  fi
  for field in "${generator_fields[@]}"; do
    if ! record_has_field "${tool_id}" "${field}"; then
      missing+=("generator:${tool_id}:${field}")
    fi
  done
done

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.tool-confidence.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "manifest": "tools/qualification/tool-confidence.yaml",\n'
  printf '  "required_class_count": %s,\n' "${#required_classes[@]}"
  printf '  "generator_record_count": %s,\n' "${#generator_tools[@]}"
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'tool confidence invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "tool confidence valid: ${evidence}"
