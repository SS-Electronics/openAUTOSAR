#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
decision_dir="${repo_root}/docs/decisions"
register="${decision_dir}/open-decision-register.yaml"
evidence="${repo_root}/out/evidence/architecture-decisions/validation.json"
missing=()

accepted_adrs=(
  "ADR-0001-monorepo-structure.md:Monorepo Structure"
  "ADR-0002-cpp20-r22-11-abi-policy.md:C++20 and R22-11-Oriented ABI Policy"
  "ADR-0003-agl-unagi-scarthgap-pin.md:AGL Unagi and Yocto Scarthgap Pin"
  "ADR-0004-qemu-x86-64-current-target.md:QEMU x86-64 Current Target"
  "ADR-0005-r22-11-arxml-first-typed-ir.md:R22-11 ARXML-First Typed IR"
  "ADR-0006-original-someip-udp-first.md:Original SOME/IP UDP-First Implementation"
  "ADR-0007-original-dds-rtps-subset.md:Original DDS/RTPS Subset"
  "ADR-0008-jenkins-authoritative-ci.md:Jenkins as Authoritative CI"
  "ADR-0009-uds-socketcan-vcan-diagnostics.md:UDS over SocketCAN vcan Diagnostics"
  "ADR-0010-ucm-rauc-ab-backend.md:UCM over Replaceable RAUC A/B Backend"
  "ADR-0011-qt6-qml-dashboard.md:Qt 6 QML Engineering Dashboard"
  "ADR-0012-mit-license-original-code.md:MIT License for Original Code"
  "ADR-0013-ultrasonic-fail-silent-degraded-policy.md:Ultrasonic Fail-Silent"
  "ADR-0014-complete-oem-artifact-export.md:Complete OEM Artifact Export"
  "ADR-0015-openautosar-working-name-risk.md:openAUTOSAR Working-Name Risk"
)

open_decisions=(
  "OD-0001:exception and RTTI policy"
  "OD-0002:dynamic allocation and bounded-memory policy"
  "OD-0003:local IPC transport"
  "OD-0004:XML library beneath the original AUTOSAR semantic parser"
  "OD-0005:cgroup namespace capability and seccomp profile"
  "OD-0006:signing algorithm provider and key-management model"
  "OD-0007:persistency engine"
  "OD-0008:logging backend and binary or text trace format"
  "OD-0009:exact UDS service session and security subset"
  "OD-0010:external SOME/IP and DDS interoperability peers"
  "OD-0011:Jenkins topology artifact storage and untrusted-PR policy"
  "OD-0012:Qt dashboard placement and LGPL compliance approach"
  "OD-0013:ultrasonic rate range resolution and timeout contract"
  "OD-0014:future safety qualification intent and evidence depth"
  "OD-0015:Bosch and ETAS integration boundary under lawful contract"
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

for entry in "${accepted_adrs[@]}"; do
  file="${entry%%:*}"
  title="${entry#*:}"
  path="${decision_dir}/${file}"
  adr_id="${file:0:8}"
  require_file "${path}" "${file}"
  require_spdx "${path}" "${file}"
  require_text "${path}" "# ${adr_id}: ${title}" "${file}:title"
  require_text "${path}" "Accepted" "${file}:accepted-status"
  require_text "${path}" "## Decision" "${file}:decision-section"
  require_text "${path}" "## Consequences" "${file}:consequences-section"
  require_text "${path}" "## Evidence" "${file}:evidence-section"
done

require_file "${register}" "open-decision-register"
require_spdx "${register}" "open-decision-register"
require_text "${register}" "schema: openautosar.open-decision-register.v1" \
  "open-register-schema"
require_text "${register}" "round: 4-required-before-adr-approval" \
  "open-register-round"

for entry in "${open_decisions[@]}"; do
  id="${entry%%:*}"
  topic="${entry#*:}"
  require_text "${register}" "id: ${id}" "open:${id}"
  require_text "${register}" "topic: ${topic}" "open:${id}:topic"
  require_text "${register}" "status: open-after-round-4" "open:${id}:status"
done

accepted_count=0
if [[ -d "${decision_dir}" ]]; then
  accepted_count="$(grep -R -l '^Accepted$' "${decision_dir}"/ADR-*.md 2>/dev/null | wc -l)"
  accepted_count="${accepted_count//[[:space:]]/}"
fi

open_count=0
if [[ -f "${register}" ]]; then
  open_count="$(grep -c '^  - id: OD-' "${register}")"
fi

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.architecture-decisions.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "decision_directory": "docs/decisions",\n'
  printf '  "accepted_adr_count": %s,\n' "${accepted_count}"
  printf '  "required_accepted_adr_count": %s,\n' "${#accepted_adrs[@]}"
  printf '  "open_decision_count": %s,\n' "${open_count}"
  printf '  "required_open_decision_count": %s,\n' "${#open_decisions[@]}"
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'architecture decisions invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "architecture decisions valid: ${evidence}"
