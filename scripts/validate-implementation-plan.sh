#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
program_plan="${repo_root}/docs/implementation/PROGRAM-PLAN.md"
workstreams="${repo_root}/requirements/planning/implementation-workstreams.yaml"
evidence="${repo_root}/out/evidence/implementation-plan/validation.json"
missing=()

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

require_count() {
  local path="$1"
  local pattern="$2"
  local expected="$3"
  local name="$4"
  local count="0"
  if [[ -f "${path}" ]]; then
    count="$(grep -c "${pattern}" "${path}" || true)"
  fi
  if [[ "${count}" != "${expected}" ]]; then
    missing+=("${name}:${count}/${expected}")
  fi
}

for path in "${program_plan}" "${workstreams}"; do
  require_file "${path}" "${path#${repo_root}/}"
  require_spdx "${path}" "${path#${repo_root}/}"
done

for heading in \
  "## Confirmed Scope" \
  "## Product Variants" \
  "## Target And Baseline" \
  "## Make Buy Reuse" \
  "## Requirement Matrix" \
  "## Architecture Decisions" \
  "## Team Ownership" \
  "## 30 Day Plan" \
  "## 60 Day Plan" \
  "## 90 Day Plan" \
  "## Milestones" \
  "## Risk Register" \
  "## Staffing And Budget" \
  "## OEM Bosch Integration" \
  "## Acceptance Criteria" \
  "## Release Roadmap"; do
  require_text "${program_plan}" "${heading}" "program-plan:${heading}"
done

require_text "${program_plan}" "QEMU-only R22-11 vertical slice" \
  "program-plan-qemu-only-scope"
require_text "${program_plan}" "AGL Ultimate Unagi 21.0.2" \
  "program-plan-agl-pin"
require_text "${program_plan}" "Yocto Scarthgap 5.0.18" \
  "program-plan-yocto-pin"
require_text "${program_plan}" "Physical boards, MCU scope" \
  "program-plan-no-hardware-scope"
require_text "${program_plan}" "zero missing optional evidence" \
  "program-plan-oem-acceptance"

require_text "${workstreams}" "schema: openautosar.implementation-workstreams.v1" \
  "workstreams-schema"
require_text "${workstreams}" "engineering_hours_per_week: 10" \
  "workstreams-capacity"
require_text "${workstreams}" "active_major_workstream_limit: 1" \
  "workstreams-active-limit"
require_text "${workstreams}" "broad_release_hours_min: 1060" \
  "workstreams-horizon-min"
require_text "${workstreams}" "broad_release_hours_max: 1835" \
  "workstreams-horizon-max"
require_count "${workstreams}" '^  - id: VS-' 12 "vertical-slice-steps"
require_count "${workstreams}" '^  - id: WS-' 9 "workstreams"
require_count "${workstreams}" '^  - id: MS-' 3 "milestones"
require_count "${workstreams}" '^  - id: PT-' 8 "parallelization-tracks"

for item in \
  "VS-0001" "VS-0002" "VS-0003" "VS-0004" "VS-0005" "VS-0006" \
  "VS-0007" "VS-0008" "VS-0009" "VS-0010" "VS-0011" "VS-0012" \
  "WS-00" "WS-01" "WS-02" "WS-03" "WS-04" "WS-05" "WS-06" \
  "WS-07" "WS-08" "MS-06M" "MS-12M" "MS-24M"; do
  require_text "${workstreams}" "id: ${item}" "workstream-item:${item}"
done

vertical_slice_count="$(grep -c '^  - id: VS-' "${workstreams}" || true)"
workstream_count="$(grep -c '^  - id: WS-' "${workstreams}" || true)"
milestone_count="$(grep -c '^  - id: MS-' "${workstreams}" || true)"
parallel_track_count="$(grep -c '^  - id: PT-' "${workstreams}" || true)"

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.implementation-plan.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "program_plan": "docs/implementation/PROGRAM-PLAN.md",\n'
  printf '  "workstream_register": '
  printf '"requirements/planning/implementation-workstreams.yaml",\n'
  printf '  "vertical_slice_step_count": %s,\n' "${vertical_slice_count}"
  printf '  "workstream_count": %s,\n' "${workstream_count}"
  printf '  "milestone_count": %s,\n' "${milestone_count}"
  printf '  "parallelization_track_count": %s,\n' "${parallel_track_count}"
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'implementation plan invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "implementation plan valid: ${evidence}"
