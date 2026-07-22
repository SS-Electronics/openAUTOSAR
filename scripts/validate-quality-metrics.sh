#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
metrics="${repo_root}/requirements/quality/metrics.yaml"
faults="${repo_root}/requirements/verification/fault-injection.yaml"
budgets="${repo_root}/requirements/performance/budgets.yaml"
policy="${repo_root}/docs/verification/QUALITY_GATE_POLICY.md"
evidence="${repo_root}/out/evidence/quality-metrics/validation.json"
missing=()

required_metrics=(
  "requirements coverage"
  "branch/MC/DC coverage where applicable"
  "escaped defects"
  "static-analysis warnings"
  "API compatibility"
  "startup time"
  "service discovery time"
  "IPC latency"
  "CPU/memory budget"
  "restart recovery time"
  "update/rollback success"
  "reproducibility match rate"
  "vulnerability age"
)

required_faults=(
  "process crash"
  "hang"
  "deadline violation"
  "memory exhaustion"
  "disk full"
  "corrupt manifest"
  "invalid signature"
  "network loss"
  "packet duplication"
  "clock loss"
  "service flapping"
  "persistency corruption"
  "interrupted update"
  "power loss during activation"
  "watchdog failure"
  "HSM unavailable"
)

required_budget_fields=(
  "cold_start_ms"
  "service_discovery_ms"
  "local_ipc_p99_us"
  "resident_memory_mb"
  "cpu_idle_percent"
  "restart_recovery_ms"
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
    return
  fi
  if ! head -n 5 "${path}" | grep -q "SPDX-License-Identifier: MIT"; then
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

require_file "${metrics}" "quality-metrics"
require_file "${faults}" "fault-injection"
require_file "${budgets}" "performance-budgets"
require_file "${policy}" "quality-gate-policy"

require_text "${metrics}" "schema: openautosar.quality.metrics.v1" "quality-metrics-schema"
require_text "${faults}" \
  "schema: openautosar.verification.fault-injection.v1" \
  "fault-injection-schema"
require_text "${budgets}" "schema: openautosar.performance.budgets.v1" "budgets-schema"
require_text "${budgets}" "target: qemu-x86_64" "budgets-target"

for metric in "${required_metrics[@]}"; do
  require_text "${metrics}" "name: ${metric}" "metric:${metric}"
done

for fault in "${required_faults[@]}"; do
  require_text "${faults}" "name: ${fault}" "fault:${fault}"
done

for field in "${required_budget_fields[@]}"; do
  require_text "${budgets}" "${field}:" "budget-field:${field}"
done

component_count=0
if [[ -f "${budgets}" ]]; then
  component_count="$(grep -c '^  - component:' "${budgets}")"
fi
if (( component_count < 3 )); then
  missing+=("budget-component-count")
fi

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.quality-metrics.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "metrics": "requirements/quality/metrics.yaml",\n'
  printf '  "fault_injection": "requirements/verification/fault-injection.yaml",\n'
  printf '  "performance_budgets": "requirements/performance/budgets.yaml",\n'
  printf '  "quality_gate_policy": "docs/verification/QUALITY_GATE_POLICY.md",\n'
  printf '  "required_metric_count": %s,\n' "${#required_metrics[@]}"
  printf '  "required_fault_count": %s,\n' "${#required_faults[@]}"
  printf '  "budget_component_count": %s,\n' "${component_count}"
  printf '  "required_budget_field_count": %s,\n' "${#required_budget_fields[@]}"
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'quality metrics invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "quality metrics valid: ${evidence}"
