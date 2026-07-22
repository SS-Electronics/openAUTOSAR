#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

iface="${OA_VCAN_IFACE:-vcan0}"
dry_run=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --iface)
      iface="$2"
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

commands=(
  "modprobe vcan"
  "ip link add dev ${iface} type vcan"
  "ip link set up ${iface}"
)

if [[ "${dry_run}" -eq 1 || "${EUID}" -ne 0 ]]; then
  printf '%s\n' "${commands[@]}"
  if [[ "${dry_run}" -eq 0 ]]; then
    echo "run as root to create ${iface}" >&2
  fi
  exit 0
fi

modprobe vcan
if ! ip link show "${iface}" >/dev/null 2>&1; then
  ip link add dev "${iface}" type vcan
fi
ip link set up "${iface}"
ip -details link show "${iface}"
