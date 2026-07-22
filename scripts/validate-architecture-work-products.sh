#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
evidence="${repo_root}/out/evidence/architecture-work-products/validation.json"
missing=()

required_files=(
  "security/threat-model/assets.md"
  "security/threat-model/trust-boundaries.md"
  "security/threat-model/attack-surfaces.md"
  "security/threat-model/threats.yaml"
  "security/architecture/identities.md"
  "security/architecture/key-management.md"
  "security/architecture/secure-boot.md"
  "security/architecture/package-trust.md"
  "security/architecture/network-security.md"
  "security/architecture/logging-and-ids.md"
  "security/hardening/linux-hardening.md"
  "security/hardening/systemd-sandboxing.md"
  "security/hardening/seccomp/openautosar-default.md"
  "security/hardening/capabilities.yaml"
  "security/evidence/README.md"
  "safety/concepts/safety-context.md"
  "safety/concepts/assumptions-of-use.md"
  "safety/concepts/safety-mechanisms.md"
  "safety/analyses/fmea/README.md"
  "safety/analyses/fta/README.md"
  "safety/analyses/dependent-failure/README.md"
  "safety/analyses/interference/README.md"
  "safety/mechanisms/health-supervision.md"
  "safety/mechanisms/e2e.md"
  "safety/mechanisms/resource-partitioning.md"
  "safety/mechanisms/watchdog.md"
  "safety/mechanisms/degraded-modes.md"
  "safety/evidence/README.md"
)

required_dirs=(
  "security/threat-model"
  "security/architecture"
  "security/hardening/seccomp"
  "security/evidence"
  "safety/concepts"
  "safety/analyses/fmea"
  "safety/analyses/fta"
  "safety/analyses/dependent-failure"
  "safety/analyses/interference"
  "safety/mechanisms"
  "safety/evidence"
)

required_surfaces=(
  "ARXML and model import"
  "generated code and templates"
  "execution, service, and machine manifests"
  "local IPC"
  "SOME/IP"
  "service discovery"
  "DDS"
  "diagnostics"
  "update transfer"
  "package activation"
  "persistency"
  "log collection"
  "external backend"
  "development CI"
  "signing system"
)

json_array() {
  local first="true"
  printf '['
  for item in "$@"; do
    if [[ "${first}" == "true" ]]; then
      first="false"
    else
      printf ', '
    fi
    printf '"%s"' "${item}"
  done
  printf ']'
}

require_text() {
  local relative="$1"
  local pattern="$2"
  local name="$3"
  local path="${repo_root}/${relative}"
  if [[ ! -f "${path}" ]] || ! grep -Fq "${pattern}" "${path}"; then
    missing+=("${name}")
  fi
}

for relative in "${required_dirs[@]}"; do
  if [[ ! -d "${repo_root}/${relative}" ]]; then
    missing+=("${relative}")
  fi
done

for relative in "${required_files[@]}"; do
  path="${repo_root}/${relative}"
  if [[ ! -f "${path}" ]]; then
    missing+=("${relative}")
    continue
  fi
  if ! head -n 5 "${path}" | grep -q "SPDX-License-Identifier: MIT"; then
    missing+=("${relative}:spdx")
  fi
done

for surface in "${required_surfaces[@]}"; do
  require_text "security/threat-model/threats.yaml" "${surface}" "threat-surface:${surface}"
done

require_text "safety/concepts/safety-context.md" \
  "not itself qualified" \
  "safety-scope-decision"
require_text "security/hardening/capabilities.yaml" \
  "CAP_SYS_ADMIN" \
  "forbidden-capabilities"

mkdir -p "$(dirname "${evidence}")"
status="valid"
if [[ "${#missing[@]}" -gt 0 ]]; then
  status="invalid"
fi

{
  printf '{\n'
  printf '  "schema": "openautosar.architecture-work-products.validation.v1",\n'
  printf '  "status": "%s",\n' "${status}"
  printf '  "required_file_count": %s,\n' "${#required_files[@]}"
  printf '  "required_directory_count": %s,\n' "${#required_dirs[@]}"
  printf '  "required_threat_surface_count": %s,\n' "${#required_surfaces[@]}"
  printf '  "security_root": "security",\n'
  printf '  "safety_root": "safety",\n'
  printf '  "missing": '
  json_array "${missing[@]}"
  printf '\n}\n'
} >"${evidence}"

if [[ "${status}" != "valid" ]]; then
  printf 'architecture work products invalid: %s\n' "${missing[*]}" >&2
  exit 1
fi

echo "architecture work products valid: ${evidence}"
