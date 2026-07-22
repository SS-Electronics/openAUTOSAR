#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
action="up"
dry_run=0
evidence_dir="${repo_root}/out/evidence/network-lab/topology"
bridge="${OA_NETWORK_LAB_BRIDGE:-oa-br0}"
prefix="${OA_NETWORK_LAB_PREFIX:-10.42.0}"
netem_loss="${OA_NETWORK_LAB_LOSS:-0%}"
netem_delay="${OA_NETWORK_LAB_DELAY:-0ms}"
seed="${OA_NETWORK_LAB_SEED:-42}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    up|down|plan)
      action="$1"
      shift
      ;;
    --bridge)
      bridge="$2"
      shift 2
      ;;
    --prefix)
      prefix="$2"
      shift 2
      ;;
    --loss)
      netem_loss="$2"
      shift 2
      ;;
    --delay)
      netem_delay="$2"
      shift 2
      ;;
    --seed)
      seed="$2"
      shift 2
      ;;
    --evidence-dir)
      evidence_dir="$2"
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

namespaces=(oa-adaptive oa-classic oa-someip oa-dds oa-capture)
declare -A addresses=(
  [oa-adaptive]="${prefix}.10/24"
  [oa-classic]="${prefix}.20/24"
  [oa-someip]="${prefix}.30/24"
  [oa-dds]="${prefix}.40/24"
  [oa-capture]="${prefix}.50/24"
)

commands=()

append() {
  commands+=("$*")
}

build_up_commands() {
  for namespace in "${namespaces[@]}"; do
    append "ip netns delete ${namespace} 2>/dev/null || true"
  done
  append "ip link delete ${bridge} 2>/dev/null || true"
  append "modprobe vcan"
  append "modprobe vxcan || true"
  append "ip link add name ${bridge} type bridge"
  append "ip link set ${bridge} up"

  local index=0
  for namespace in "${namespaces[@]}"; do
    local host_veth="oa-veth-${index}"
    local ns_veth="eth0"
    append "ip netns add ${namespace}"
    append "ip link add ${host_veth} type veth peer name ${ns_veth}"
    append "ip link set ${ns_veth} netns ${namespace}"
    append "ip link set ${host_veth} master ${bridge}"
    append "ip link set ${host_veth} up"
    append "ip netns exec ${namespace} ip link set lo up"
    append "ip netns exec ${namespace} ip addr add ${addresses[${namespace}]} dev ${ns_veth}"
    append "ip netns exec ${namespace} ip link set ${ns_veth} up"
    append "tc qdisc replace dev ${host_veth} root netem loss ${netem_loss} delay ${netem_delay}"
    index=$((index + 1))
  done

  append "ip link add oa-vxcan-adaptive type vxcan peer name oa-vxcan-classic"
  append "ip link set oa-vxcan-adaptive netns oa-adaptive"
  append "ip link set oa-vxcan-classic netns oa-classic"
  append "ip netns exec oa-adaptive ip link set oa-vxcan-adaptive name can0"
  append "ip netns exec oa-classic ip link set oa-vxcan-classic name can0"
  append "ip netns exec oa-adaptive ip link set can0 up"
  append "ip netns exec oa-classic ip link set can0 up"
}

build_down_commands() {
  for namespace in "${namespaces[@]}"; do
    append "ip netns delete ${namespace} 2>/dev/null || true"
  done
  append "ip link delete ${bridge} 2>/dev/null || true"
}

write_evidence() {
  mkdir -p "${evidence_dir}"
  local timestamp
  timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

  {
    printf '{\n'
    printf '  "timestamp_utc": "%s",\n' "${timestamp}"
    printf '  "action": "%s",\n' "${action}"
    printf '  "dry_run": %s,\n' "$([[ "${dry_run}" -eq 1 || "${action}" == "plan" || "${EUID}" -ne 0 ]] && echo true || echo false)"
    printf '  "seed": "%s",\n' "${seed}"
    printf '  "bridge": "%s",\n' "${bridge}"
    printf '  "netem": {"loss": "%s", "delay": "%s"},\n' "${netem_loss}" "${netem_delay}"
    printf '  "namespaces": [\n'
    for index in "${!namespaces[@]}"; do
      local namespace="${namespaces[${index}]}"
      printf '    {"name": "%s", "address": "%s"}' "${namespace}" "${addresses[${namespace}]}"
      if [[ "${index}" -lt $((${#namespaces[@]} - 1)) ]]; then
        printf ','
      fi
      printf '\n'
    done
    printf '  ],\n'
    printf '  "can_tunnel": {"type": "vxcan", "adaptive": "oa-adaptive/can0", "classic": "oa-classic/can0"}\n'
    printf '}\n'
  } >"${evidence_dir}/topology.json"

  printf '%s\n' "${commands[@]}" >"${evidence_dir}/commands.txt"
  printf 'ip netns exec oa-capture tcpdump -i eth0 -w %s/packet-capture.pcap\n' \
    "${evidence_dir}" >"${evidence_dir}/packet-capture-command.txt"
  {
    printf '%s\n' 'Expected outcome:'
    printf '%s\n' '- namespaces for Adaptive, Classic, SOME/IP, DDS, and capture peers exist'
    printf '%s\n' "- veth peers attach to ${bridge} with deterministic addresses"
    printf '%s\n' '- vxcan links expose can0 in Adaptive and Classic namespaces'
    printf '%s\n' "- netem applies loss=${netem_loss} delay=${netem_delay} on host-side veth links"
    printf '%s\n' '- capture namespace can run the recorded tcpdump command for packet evidence'
  } >"${evidence_dir}/expected-outcome.txt"
}

run_commands() {
  if [[ "${action}" == "plan" || "${dry_run}" -eq 1 || "${EUID}" -ne 0 ]]; then
    printf '%s\n' "${commands[@]}"
    if [[ "${action}" != "plan" && "${dry_run}" -eq 0 && "${EUID}" -ne 0 ]]; then
      echo "run as root to apply the network lab topology" >&2
    fi
    return
  fi

  for command in "${commands[@]}"; do
    bash -c "${command}"
  done
}

case "${action}" in
  up|plan)
    build_up_commands
    ;;
  down)
    build_down_commands
    ;;
  *)
    echo "unsupported action: ${action}" >&2
    exit 2
    ;;
esac

write_evidence
run_commands
