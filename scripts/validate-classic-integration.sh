#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
config="${repo_root}/integration/virtual-vehicle/classic-gateway/gateway-config.json"
generated="${repo_root}/out/generated/classic-integration"
evidence_dir="${repo_root}/out/evidence/classic-integration"
output=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --config)
      config="$2"
      shift 2
      ;;
    --generated)
      generated="$2"
      shift 2
      ;;
    --evidence-dir)
      evidence_dir="$2"
      shift 2
      ;;
    --output)
      output="$2"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

args=(
  validate-classic-integration
  --repo-root "${repo_root}"
  --config "${config}"
  --generated "${generated}"
  --evidence-dir "${evidence_dir}"
)

if [[ -n "${output}" ]]; then
  args+=(--output "${output}")
else
  args+=(--output "${evidence_dir}/validation.json")
fi

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli "${args[@]}"
echo "classic integration valid: ${evidence_dir}/validation.json"
