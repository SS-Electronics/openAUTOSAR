#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
model="${repo_root}/model/examples/vehicle"
output="${repo_root}/out/generated"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --model)
      model="$2"
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

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli validate-model --model "${model}"
PYTHONPATH="${repo_root}/tools" python3 -m oa_cli generate --model "${model}" --output "${output}"
echo "generated metadata: ${output}/manifest-summary.json"
