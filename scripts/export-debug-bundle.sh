#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile="generic-oem"
target="qemu-x86_64"
model="${repo_root}/model/examples/vehicle"
package_root=""
generated=""
evidence_dir="${repo_root}/out/evidence"
test_results_dir="${repo_root}/out/test-results"
output="${repo_root}/out/evidence/debug-bundle/manifest.json"

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
    --package-root)
      package_root="$2"
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
    --test-results-dir)
      test_results_dir="$2"
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

if [[ -z "${package_root}" ]]; then
  package_root="${repo_root}/out/package/${profile}"
fi
if [[ -z "${generated}" ]]; then
  generated="${package_root}/share/openautosar/generated"
fi

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli debug bundle \
  --repo-root "${repo_root}" \
  --package-root "${package_root}" \
  --generated "${generated}" \
  --evidence-dir "${evidence_dir}" \
  --test-results-dir "${test_results_dir}" \
  --output "${output}"

package_debug="${package_root}/share/openautosar/debug-bundle/manifest.json"
if [[ -d "${package_root}" && "${output}" != "${package_debug}" ]]; then
  install -D -m 0644 "${output}" "${package_debug}"
fi

if [[ -f "${package_root}/provenance.json" \
  && -f "${package_root}/lib/cmake/openautosar/OpenAutosarConfig.cmake" \
  && -f "${generated}/manifest-summary.json" ]]; then
  PYTHONPATH="${repo_root}/tools" python3 -m oa_cli export-evidence \
    --repo-root "${repo_root}" \
    --package-root "${package_root}" \
    --output "${package_root}/share/openautosar/oem-export" \
    --model "${model}" \
    --generated "${generated}" \
    --evidence-dir "${evidence_dir}" \
    --test-results-dir "${test_results_dir}" \
    --profile "${profile}" \
    --target "${target}"
fi

echo "debug bundle: ${output}"
