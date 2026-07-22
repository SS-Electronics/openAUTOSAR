#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile="generic-oem"
target="qemu-x86_64"
model="${repo_root}/model/examples/vehicle"
output=""
strict="false"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      profile="$2"
      shift 2
      ;;
    --target)
      target="$2"
      shift 2
      ;;
    --model)
      model="$2"
      shift 2
      ;;
    --output)
      output="$2"
      shift 2
      ;;
    --strict)
      strict="true"
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "${output}" ]]; then
  output="${repo_root}/out/oem-export/${profile}"
fi

"${repo_root}/scripts/package.sh" \
  --profile "${profile}" \
  --target "${target}" \
  --model "${model}"

package_root="${repo_root}/out/package/${profile}"
generated="${package_root}/share/openautosar/generated"
args=(
  export-evidence
  --repo-root "${repo_root}"
  --package-root "${package_root}"
  --output "${output}"
  --model "${model}"
  --generated "${generated}"
  --evidence-dir "${repo_root}/out/evidence"
  --test-results-dir "${repo_root}/out/test-results"
  --profile "${profile}"
  --target "${target}"
)

if [[ "${strict}" == "true" ]]; then
  args+=(--strict)
fi

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli "${args[@]}"
echo "OEM evidence bundle: ${output}"
