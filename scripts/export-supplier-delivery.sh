#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile="generic-oem"
delivery_profile="generic-supplier"
target="qemu-x86_64"
model="${repo_root}/model/examples/vehicle"
package_root=""
generated=""
evidence_dir="${repo_root}/out/evidence"
test_results_dir="${repo_root}/out/test-results"
output=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      profile="$2"
      shift 2
      ;;
    --delivery-profile)
      delivery_profile="$2"
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
if [[ -z "${output}" ]]; then
  output="${repo_root}/out/supplier-delivery/${profile}"
fi

if [[ ! -f "${package_root}/provenance.json" ]]; then
  "${repo_root}/scripts/package.sh" \
    --profile "${profile}" \
    --target "${target}" \
    --model "${model}" \
    --delivery-profile "${delivery_profile}"
fi

"${repo_root}/scripts/validate-delivery-workflow.sh" --source-only

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli export-delivery \
  --repo-root "${repo_root}" \
  --package-root "${package_root}" \
  --output "${output}" \
  --model "${model}" \
  --generated "${generated}" \
  --evidence-dir "${evidence_dir}" \
  --test-results-dir "${test_results_dir}" \
  --oem-export "${package_root}/share/openautosar/oem-export" \
  --profile "${delivery_profile}" \
  --target "${target}"

"${repo_root}/scripts/validate-delivery-workflow.sh" --delivery-root "${output}"

package_delivery="${package_root}/share/openautosar/supplier-delivery"
if [[ -d "${package_root}" ]]; then
  install -D -m 0644 "${output}/manifest.json" "${package_delivery}/manifest.json"
  install -D -m 0644 \
    "${output}/validation-summary.json" \
    "${package_delivery}/validation-summary.json"
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

echo "supplier delivery bundle: ${output}"
