#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${OA_CMAKE_PRESET:-qemu-x86_64-dev}"
gateway="${repo_root}/out/build/${preset}/integration/virtual-vehicle/gateway-peer/oa-ultrasonic-gateway-smoke"
classic_ecu="${repo_root}/out/build/${preset}/integration/virtual-vehicle/classic-ecu-simulator/oa-classic-ecu-sim"
evidence_dir="${repo_root}/out/evidence/network-lab/last-run"
samples="${OA_NETWORK_LAB_SAMPLES:-6}"
fault="${OA_NETWORK_LAB_FAULT:-crc_error}"
fault_at="${OA_NETWORK_LAB_FAULT_AT:-3}"
seed="${OA_NETWORK_LAB_SEED:-42}"

if [[ ! -x "${gateway}" || ! -x "${classic_ecu}" ]]; then
  "${repo_root}/scripts/build.sh" --preset "${preset}"
fi

mkdir -p "${evidence_dir}"
timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

"${repo_root}/scripts/setup-network-lab.sh" plan \
  --seed "${seed}" \
  --evidence-dir "${evidence_dir}/topology" \
  >"${evidence_dir}/topology-plan.txt"

{
  printf '{\n'
  printf '  "timestamp_utc": "%s",\n' "${timestamp}"
  printf '  "preset": "%s",\n' "${preset}"
  printf '  "seed": "%s",\n' "${seed}"
  printf '  "samples": %s,\n' "${samples}"
  printf '  "fault": "%s",\n' "${fault}"
  printf '  "fault_at": %s,\n' "${fault_at}"
  printf '  "expected_outcome": "fault is detected without publishing invalid distance; repeated faults request degraded state"\n'
  printf '}\n'
} >"${evidence_dir}/run.json"

echo "classic-ecu-simulator:"
"${classic_ecu}" --dry-run --samples "${samples}" --fault "${fault}" --fault-at "${fault_at}" \
  | tee "${evidence_dir}/classic-ecu-simulator.log"

echo "gateway-peer:"
"${gateway}" --samples "${samples}" --fault "${fault}" --fault-at "${fault_at}" \
  | tee "${evidence_dir}/gateway-peer.log"

printf 'network lab evidence: %s\n' "${evidence_dir}"
