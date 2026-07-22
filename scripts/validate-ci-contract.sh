#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
jenkinsfile="${repo_root}/Jenkinsfile"
library="${repo_root}/ci/jenkins/shared/vars/openAutosarPipeline.groovy"
evidence="${repo_root}/out/evidence/ci-contract/validation.json"
missing=()

require_file() {
  local path="$1"
  local name="$2"
  if [[ ! -f "${path}" ]]; then
    missing+=("${name}")
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

require_file "${jenkinsfile}" "jenkinsfile"
require_file "${library}" "shared-library"
require_text "${jenkinsfile}" "@Library('openautosar-ci')" "library-annotation"
require_text "${jenkinsfile}" "openAutosarPipeline()" "library-entrypoint"

for stage in \
  "Bootstrap" \
  "Configure" \
  "Generate" \
  "Yocto Layer Smoke" \
  "Build" \
  "Test" \
  "Network Lab Smoke" \
  "License Scan" \
  "Compliance" \
  "Architecture Work Products" \
  "Architecture Decisions" \
  "Program Governance" \
  "Implementation Plan" \
  "Tool Confidence" \
  "API ABI" \
  "Quality Metrics" \
  "Classic Integration" \
  "Delivery Workflow" \
  "Package" \
  "Debug Bundle" \
  "Supplier Delivery"; do
  require_text "${library}" "stage('${stage}')" "stage-${stage}"
done

for command in \
  "./scripts/bootstrap.sh" \
  "./scripts/configure.sh" \
  "./scripts/generate.sh" \
  "./scripts/validate-yocto-layer.sh" \
  "./scripts/build.sh" \
  "./scripts/test.sh" \
  "./scripts/run-network-lab.sh" \
  "./scripts/deploy-qemu.sh" \
  "./ci/checks/license-scan.sh" \
  "./scripts/validate-compliance.sh" \
  "./scripts/validate-architecture-work-products.sh" \
  "./scripts/validate-architecture-decisions.sh" \
  "./scripts/validate-program-governance.sh" \
  "./scripts/validate-implementation-plan.sh" \
  "./scripts/validate-tool-confidence.sh" \
  "./scripts/validate-api-abi.sh" \
  "./scripts/validate-quality-metrics.sh" \
  "./scripts/validate-classic-integration.sh" \
  "./scripts/validate-delivery-workflow.sh" \
  "./scripts/package.sh" \
  "./scripts/export-debug-bundle.sh" \
  "./scripts/export-supplier-delivery.sh"; do
  require_text "${library}" "${command}" "command-${command}"
done

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.ci-contract.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "jenkinsfile": "Jenkinsfile",\n'
  printf '  "shared_library": "ci/jenkins/shared/vars/openAutosarPipeline.groovy",\n'
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'CI contract invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "CI contract valid: ${evidence}"
