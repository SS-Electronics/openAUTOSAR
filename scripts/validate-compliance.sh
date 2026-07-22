#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
evidence_dir="${repo_root}/out/evidence/compliance"
legal_dir="${repo_root}/compliance/legal"
release_profile="${repo_root}/requirements/autosar/release-profile.yaml"
deviations="${repo_root}/compliance/autosar/r22-11-deviations.yaml"

required_files=(
  "${legal_dir}/AUTOSAR-IP-ASSESSMENT.md"
  "${legal_dir}/BRANDING-GUIDELINES.md"
  "${legal_dir}/LICENSE-COMPATIBILITY.md"
  "${legal_dir}/THIRD_PARTY_NOTICES.md"
  "${legal_dir}/CONTRIBUTOR-IP-POLICY.md"
  "${legal_dir}/RELEASE-APPROVAL-CHECKLIST.md"
  "${release_profile}"
  "${deviations}"
  "${repo_root}/LICENSE"
  "${repo_root}/NOTICE"
  "${repo_root}/CONTRIBUTING.md"
)

mkdir -p "${evidence_dir}"

for path in "${required_files[@]}"; do
  if [[ ! -f "${path}" ]]; then
    echo "missing compliance artifact: ${path}" >&2
    exit 1
  fi
done

for path in "${legal_dir}"/*.md; do
  head -n 1 "${path}" | grep -q 'SPDX-License-Identifier: MIT' || {
    echo "missing SPDX marker: ${path}" >&2
    exit 1
  }
done

grep -q 'primary_release: R22-11' "${release_profile}"
grep -q 'behavior_contract: r22-11' "${release_profile}"
grep -q 'deviations_file: compliance/autosar/r22-11-deviations.yaml' "${release_profile}"
grep -q 'release: R22-11' "${deviations}"
grep -q 'do not commit AUTOSAR specifications' "${repo_root}/CONTRIBUTING.md"
grep -q 'Developer Certificate of Origin' "${repo_root}/CONTRIBUTING.md"

restricted_pattern='AUTOSAR certified|fully compliant|official AUTOSAR implementation|'
restricted_pattern+='Bosch-approved workflow|MIT-only'

if rg -n -S "${restricted_pattern}" \
  --glob '!out/**' \
  --glob '!.git/**' \
  --glob '!scripts/validate-compliance.sh' \
  --glob '!compliance/legal/**' \
  "${repo_root}" >"${evidence_dir}/restricted-claims.log"; then
  cat "${evidence_dir}/restricted-claims.log" >&2
  exit 1
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.compliance.validation.v1",\n'
  printf '  "legal_package": "present",\n'
  printf '  "release_profile": "R22-11",\n'
  printf '  "deviations": "present",\n'
  printf '  "restricted_claims": "absent",\n'
  printf '  "status": "valid"\n'
  printf '}\n'
} >"${evidence_dir}/validation.json"

echo "compliance metadata valid: ${evidence_dir}/validation.json"
