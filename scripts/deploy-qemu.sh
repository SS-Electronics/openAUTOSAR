#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${OA_QEMU_IMAGE:-}"
tap="${OA_QEMU_TAP:-tap-openautosar0}"
memory="${OA_QEMU_MEMORY:-2048}"
smp="${OA_QEMU_SMP:-2}"
dry_run=0
no_kvm=0
evidence_dir="${repo_root}/out/evidence/qemu"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image)
      image="$2"
      shift 2
      ;;
    --tap)
      tap="$2"
      shift 2
      ;;
    --memory)
      memory="$2"
      shift 2
      ;;
    --smp)
      smp="$2"
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
    --no-kvm)
      no_kvm=1
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "${image}" ]]; then
  echo "QEMU deployment requires --image or OA_QEMU_IMAGE for a qemux86-64 AGL image." >&2
  exit 2
fi

qemu_bin="${OA_QEMU_BIN:-qemu-system-x86_64}"
if [[ "${dry_run}" -eq 0 ]] && ! command -v "${qemu_bin}" >/dev/null 2>&1; then
  echo "QEMU executable not found: ${qemu_bin}" >&2
  exit 2
fi

if [[ "${dry_run}" -eq 0 && ! -r "${image}" ]]; then
  echo "QEMU image is not readable: ${image}" >&2
  exit 2
fi

accel="tcg"
cpu="max"
if [[ "${no_kvm}" -eq 0 && -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
  accel="kvm"
  cpu="host"
fi

mkdir -p "${evidence_dir}"
serial_log="${evidence_dir}/serial.log"
timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
image_sha256="unavailable"
image_exists=false
if [[ -r "${image}" ]]; then
  image_exists=true
  image_sha256="$(sha256sum "${image}" | awk '{print $1}')"
fi

qemu_command=(
  "${qemu_bin}"
  -machine "q35,accel=${accel}"
  -cpu "${cpu}"
  -smp "${smp}"
  -m "${memory}"
  -drive "file=${image},if=virtio,format=raw"
  -netdev "tap,id=net0,ifname=${tap},script=no,downscript=no"
  -device "virtio-net-pci,netdev=net0"
  -serial "file:${serial_log}"
  -display none
  -no-reboot
)

printf '%q ' "${qemu_command[@]}" >"${evidence_dir}/qemu-command.sh"
printf '\n' >>"${evidence_dir}/qemu-command.sh"
chmod +x "${evidence_dir}/qemu-command.sh"

{
  printf '{\n'
  printf '  "timestamp_utc": "%s",\n' "${timestamp}"
  printf '  "target": "qemux86-64",\n'
  printf '  "dry_run": %s,\n' "$([[ "${dry_run}" -eq 1 ]] && echo true || echo false)"
  printf '  "qemu_binary": "%s",\n' "${qemu_bin}"
  printf '  "image": "%s",\n' "${image}"
  printf '  "image_exists": %s,\n' "${image_exists}"
  printf '  "image_sha256": "%s",\n' "${image_sha256}"
  printf '  "tap": "%s",\n' "${tap}"
  printf '  "memory_mib": %s,\n' "${memory}"
  printf '  "smp": %s,\n' "${smp}"
  printf '  "acceleration": "%s",\n' "${accel}"
  printf '  "serial_log": "%s",\n' "${serial_log}"
  printf '  "expected_outcome": "AGL qemux86-64 image boots with deterministic serial log and TAP networking"\n'
  printf '}\n'
} >"${evidence_dir}/qemu-command.json"

if [[ "${dry_run}" -eq 1 ]]; then
  cat "${evidence_dir}/qemu-command.sh"
  echo "qemu dry-run evidence: ${evidence_dir}"
  exit 0
fi

exec "${qemu_command[@]}"
