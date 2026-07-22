#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generic="${repo_root}/profiles/generic-supplier"
bosch="${repo_root}/profiles/bosch-supplier"
delivery_root="${repo_root}/out/supplier-delivery/generic-oem"
evidence="${repo_root}/out/evidence/delivery-workflow/validation.json"
source_only="false"
missing=()
generated_missing=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --delivery-root)
      delivery_root="$2"
      shift 2
      ;;
    --source-only)
      source_only="true"
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

required_inputs=(
  "system and service ARXML"
  "service interface definitions"
  "network design"
  "diagnostics specification"
  "machine resource budgets"
  "cybersecurity requirements"
  "safety requirements"
  "update strategy"
  "software-cluster allocation"
  "coding/static-analysis rules"
  "release naming"
  "accepted tool versions"
  "integration test specifications"
  "supplier quality gates"
)

import_controls=(
  "identify release/schema"
  "validate XML/schema"
  "normalize namespaces"
  "resolve references"
  "detect unsupported model elements"
  "compare with previous delivery"
  "generate a change report"
  "preserve source provenance"
  "create mapping decisions"
  "reject ambiguous destructive changes"
)

freeze_items=(
  "service interfaces"
  "data types"
  "versions"
  "service instance identifiers"
  "network endpoints"
  "process names"
  "Function Groups"
  "diagnostics IDs"
  "package dependencies"
  "machine resource allocation"
)

profile_files=(
  "${generic}/profile.yaml"
  "${generic}/input-contract.yaml"
  "${generic}/delivery-layout.yaml"
  "${generic}/quality-gates.yaml"
  "${generic}/README.md"
  "${bosch}/profile.yaml"
  "${bosch}/naming-rules.yaml"
  "${bosch}/input-contract.schema.json"
  "${bosch}/arxml-mapping.yaml"
  "${bosch}/delivery-layout.yaml"
  "${bosch}/quality-gates.yaml"
  "${bosch}/reports/README.md"
  "${bosch}/exporters/README.md"
  "${bosch}/README.md"
)

delivery_dirs=(
  "00-cover"
  "10-model/arxml"
  "10-model/validation-report"
  "10-model/change-report"
  "20-software/software-clusters"
  "20-software/binaries"
  "20-software/libraries"
  "20-software/debug-symbols-controlled"
  "30-manifests/execution"
  "30-manifests/service-instance"
  "30-manifests/machine"
  "40-build"
  "50-quality/test-reports"
  "50-quality/coverage"
  "50-quality/static-analysis"
  "50-quality/performance"
  "50-quality/traceability"
  "60-safety-security/safety-evidence"
  "60-safety-security/threat-analysis"
  "60-safety-security/vulnerability-report"
  "60-safety-security/signing-certificate-chain"
  "70-compliance/sbom"
  "70-compliance/license-report"
  "70-compliance/deviations"
  "70-compliance/approvals"
  "80-integration/acceptance-tests"
)

delivery_files=(
  "manifest.json"
  "validation-summary.json"
  "00-cover/delivery-note.pdf"
  "00-cover/release-notes.md"
  "00-cover/known-issues.md"
  "10-model/validation-report/model-validation.json"
  "10-model/change-report/change-report.json"
  "20-software/software-clusters/manifest.json"
  "30-manifests/service-instance/manifest-summary.json"
  "40-build/tool-versions.json"
  "40-build/build-config.json"
  "40-build/hashes.txt"
  "40-build/provenance.json"
  "50-quality/traceability/traceability.json"
  "70-compliance/sbom/sbom.spdx.json"
  "80-integration/install-guide.md"
  "80-integration/rollback-guide.md"
  "80-integration/support-matrix.md"
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

require_file() {
  local path="$1"
  local name="$2"
  if [[ ! -f "${path}" ]]; then
    missing+=("${name}")
  fi
}

require_dir() {
  local path="$1"
  local name="$2"
  if [[ ! -d "${path}" ]]; then
    generated_missing+=("${name}")
  fi
}

require_generated_file() {
  local path="$1"
  local name="$2"
  if [[ ! -f "${path}" ]]; then
    generated_missing+=("${name}")
  fi
}

require_spdx() {
  local path="$1"
  local name="$2"
  if [[ ! -f "${path}" ]] || ! head -n 5 "${path}" | grep -q "SPDX-License-Identifier: MIT"; then
    missing+=("${name}:spdx")
  fi
}

require_text() {
  local path="$1"
  local pattern="$2"
  local name="$3"
  if [[ ! -f "${path}" ]] || ! grep -Fq "${pattern}" "${path}"; then
    missing+=("${name}")
  fi
}

for path in "${profile_files[@]}"; do
  relative="${path#${repo_root}/}"
  require_file "${path}" "${relative}"
  require_spdx "${path}" "${relative}"
done

require_text "${generic}/profile.yaml" "schema: openautosar.supplier-delivery.profile.v1" \
  "generic-profile-schema"
require_text "${generic}/profile.yaml" "confidential_workflow: false" \
  "generic-confidential-policy"
require_text "${generic}/profile.yaml" "post_freeze_change_policy:" \
  "post-freeze-change-policy"
require_text "${bosch}/profile.yaml" "status: template-only" "bosch-template-only"
require_text "${bosch}/profile.yaml" "confidential_workflow: false" "bosch-confidential-policy"
require_text "${bosch}/profile.yaml" "requires_authorized_project_contract: true" \
  "bosch-authorized-contract"
require_text "${bosch}/README.md" "does not represent confidential Bosch workflow" \
  "bosch-readme-confidential-disclaimer"

for item in "${required_inputs[@]}"; do
  require_text "${generic}/input-contract.yaml" "${item}" "input:${item}"
done

for control in "${import_controls[@]}"; do
  require_text "${generic}/profile.yaml" "${control}" "import:${control}"
done

for item in "${freeze_items[@]}"; do
  require_text "${generic}/profile.yaml" "${item}" "freeze:${item}"
done

for directory in "${delivery_dirs[@]}"; do
  require_text "${generic}/delivery-layout.yaml" "${directory}" "layout:${directory}"
done

generated_status="not-present"
if [[ "${source_only}" == "false" && -d "${delivery_root}" ]]; then
  generated_status="valid"
  for directory in "${delivery_dirs[@]}"; do
    require_dir "${delivery_root}/${directory}" "delivery-dir:${directory}"
  done
  for file in "${delivery_files[@]}"; do
    require_generated_file "${delivery_root}/${file}" "delivery-file:${file}"
  done
  if [[ -f "${delivery_root}/manifest.json" ]] \
    && ! grep -Fq '"status": "valid"' "${delivery_root}/manifest.json"; then
    generated_missing+=("delivery-manifest-status")
  fi
  if [[ -f "${delivery_root}/validation-summary.json" ]] \
    && ! grep -Fq '"status": "valid"' "${delivery_root}/validation-summary.json"; then
    generated_missing+=("delivery-validation-status")
  fi
  if [[ -f "${delivery_root}/00-cover/delivery-note.pdf" ]] \
    && ! head -c 8 "${delivery_root}/00-cover/delivery-note.pdf" | grep -q '%PDF-1.'; then
    generated_missing+=("delivery-note-pdf")
  fi
  if [[ "${#generated_missing[@]}" -gt 0 ]]; then
    generated_status="invalid"
  fi
fi

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 || "${#generated_missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.delivery-workflow.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "generic_profile": "profiles/generic-supplier",\n'
  printf '  "bosch_profile": "profiles/bosch-supplier",\n'
  printf '  "delivery_root": "%s",\n' "${delivery_root#${repo_root}/}"
  printf '  "required_input_count": %s,\n' "${#required_inputs[@]}"
  printf '  "import_control_count": %s,\n' "${#import_controls[@]}"
  printf '  "freeze_item_count": %s,\n' "${#freeze_items[@]}"
  printf '  "delivery_section_count": 9,\n'
  printf '  "generated_delivery_status": "%s",\n' "${generated_status}"
  printf '  "missing": '
  json_array "${missing[@]}"
  printf ',\n'
  printf '  "generated_missing": '
  json_array "${generated_missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'delivery workflow invalid: %s %s\n' "${missing[*]}" "${generated_missing[*]}" >&2
  exit 1
fi

echo "delivery workflow valid: ${evidence}"
