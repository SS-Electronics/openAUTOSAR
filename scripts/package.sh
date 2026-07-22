#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="qemu-x86_64-dev"
profile="generic-oem"
target="qemu-x86_64"
model="${repo_root}/model/examples/vehicle"
delivery_profile="generic-supplier"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
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
    --delivery-profile)
      delivery_profile="$2"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

"${repo_root}/scripts/validate-ci-contract.sh"
"${repo_root}/scripts/validate-compliance.sh"
"${repo_root}/scripts/validate-architecture-work-products.sh"
"${repo_root}/scripts/validate-architecture-decisions.sh"
"${repo_root}/scripts/validate-program-governance.sh"
"${repo_root}/scripts/validate-implementation-plan.sh"
"${repo_root}/scripts/validate-tool-confidence.sh"
"${repo_root}/scripts/validate-api-abi.sh"
"${repo_root}/scripts/validate-quality-metrics.sh"
"${repo_root}/scripts/validate-classic-integration.sh"
"${repo_root}/scripts/validate-delivery-workflow.sh" --source-only

"${repo_root}/scripts/build.sh" --preset "${preset}"
install_prefix="${repo_root}/out/package/${profile}"
cmake --install "${repo_root}/out/build/${preset}" --prefix "${install_prefix}"
runtime_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-runtime/files"
log_trace_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-log-trace/files"
local_ipc_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-local-ipc/files"
process_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-process-launcher/files"
state_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-state-management/files"
registry_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-registry/files"
vehicle_ucm_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-vehicle-ucm/files"
e2e_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-e2e-protection/files"
raw_stream_files="${repo_root}/meta-openautosar/recipes-platform"
raw_stream_files+="/openautosar-raw-data-stream/files"
api_gateway_files="${repo_root}/meta-openautosar/recipes-platform"
api_gateway_files+="/openautosar-automotive-api-gateway/files"
remote_persistency_files="${repo_root}/meta-openautosar/recipes-platform"
remote_persistency_files+="/openautosar-remote-persistency/files"
time_network_files="${repo_root}/meta-openautosar/recipes-platform/openautosar-time-network/files"
safe_hwa_files="${repo_root}/meta-openautosar/recipes-platform"
safe_hwa_files+="/openautosar-safe-hardware-acceleration/files"
dashboard_files="${repo_root}/meta-openautosar/recipes-ui/openautosar-dashboard/files"
security_files="${repo_root}/meta-openautosar/recipes-security/openautosar-security/files"
crypto_files="${repo_root}/meta-openautosar/recipes-security/openautosar-crypto/files"
firewall_files="${repo_root}/meta-openautosar/recipes-security/openautosar-firewall/files"
idsm_files="${repo_root}/meta-openautosar/recipes-security/openautosar-idsm/files"

rm -rf "${install_prefix}/share/openautosar/yocto" "${install_prefix}/share/openautosar/deployment"
install -d "${install_prefix}/share/openautosar/yocto"
cp -a "${repo_root}/meta-openautosar" "${install_prefix}/share/openautosar/yocto/"
rm -rf "${install_prefix}/share/openautosar/compliance"
install -d "${install_prefix}/share/openautosar/compliance"
cp -a "${repo_root}/compliance/." "${install_prefix}/share/openautosar/compliance/"
rm -rf "${install_prefix}/share/openautosar/work-products"
install -d "${install_prefix}/share/openautosar/work-products"
cp -a "${repo_root}/security" "${install_prefix}/share/openautosar/work-products/"
cp -a "${repo_root}/safety" "${install_prefix}/share/openautosar/work-products/"
install -d "${install_prefix}/share/openautosar/work-products/api"
cp -a "${repo_root}/docs/api/." "${install_prefix}/share/openautosar/work-products/api/"
install -d "${install_prefix}/share/openautosar/work-products/architecture-decisions"
cp -a "${repo_root}/docs/decisions/." \
  "${install_prefix}/share/openautosar/work-products/architecture-decisions/"
install -d "${install_prefix}/share/openautosar/work-products/governance"
cp -a "${repo_root}/docs/governance/." \
  "${install_prefix}/share/openautosar/work-products/governance/"
install -d "${install_prefix}/share/openautosar/work-products/platform-services"
cp -a "${repo_root}/docs/services/." \
  "${install_prefix}/share/openautosar/work-products/platform-services/"
install -d "${install_prefix}/share/openautosar/work-products/implementation"
cp -a "${repo_root}/docs/implementation/." \
  "${install_prefix}/share/openautosar/work-products/implementation/"
install -d "${install_prefix}/share/openautosar/work-products/backlog"
cp -a "${repo_root}/requirements/backlog/." \
  "${install_prefix}/share/openautosar/work-products/backlog/"
install -d "${install_prefix}/share/openautosar/work-products/planning"
cp -a "${repo_root}/requirements/planning/." \
  "${install_prefix}/share/openautosar/work-products/planning/"
install -d "${install_prefix}/share/openautosar/work-products/requirements/autosar"
cp -a "${repo_root}/requirements/autosar/." \
  "${install_prefix}/share/openautosar/work-products/requirements/autosar/"
install -d "${install_prefix}/share/openautosar/work-products/requirements/api"
cp -a "${repo_root}/requirements/api/." \
  "${install_prefix}/share/openautosar/work-products/requirements/api/"
install -d "${install_prefix}/share/openautosar/work-products/tool-confidence"
cp -a "${repo_root}/tools/qualification/." \
  "${install_prefix}/share/openautosar/work-products/tool-confidence/"
install -d "${install_prefix}/share/openautosar/work-products/quality"
cp -a "${repo_root}/requirements/quality/." \
  "${install_prefix}/share/openautosar/work-products/quality/"
install -d "${install_prefix}/share/openautosar/work-products/verification"
cp -a "${repo_root}/requirements/verification/." \
  "${install_prefix}/share/openautosar/work-products/verification/"
install -d "${install_prefix}/share/openautosar/work-products/performance"
cp -a "${repo_root}/requirements/performance/." \
  "${install_prefix}/share/openautosar/work-products/performance/"
install -d "${install_prefix}/share/openautosar/work-products/quality-policy"
cp -a "${repo_root}/docs/verification/." \
  "${install_prefix}/share/openautosar/work-products/quality-policy/"
rm -rf "${install_prefix}/share/openautosar/classic-integration"
install -d "${install_prefix}/share/openautosar/classic-integration"
cp -a "${repo_root}/integration/virtual-vehicle/classic-gateway/." \
  "${install_prefix}/share/openautosar/classic-integration/"
install -d "${install_prefix}/share/openautosar/classic-integration/signal-database"
cp -a "${repo_root}/integration/virtual-vehicle/classic-ecu-simulator/signal-database/." \
  "${install_prefix}/share/openautosar/classic-integration/signal-database/"
rm -rf "${install_prefix}/share/openautosar/profiles"
install -d "${install_prefix}/share/openautosar/profiles"
cp -a "${repo_root}/profiles/." "${install_prefix}/share/openautosar/profiles/"
install -d "${install_prefix}/share/openautosar/tools"
rm -rf "${install_prefix}/share/openautosar/tools/oa_cli"
install -d "${install_prefix}/share/openautosar/tools/oa_cli"
for tool_file in "${repo_root}"/tools/oa_cli/*.py; do
  install -m 0644 "${tool_file}" "${install_prefix}/share/openautosar/tools/oa_cli/"
done
install -d "${install_prefix}/bin"
{
  printf '#!/usr/bin/env python3\n'
  printf 'import pathlib\n'
  printf 'import sys\n'
  printf 'root = pathlib.Path(__file__).resolve().parents[1]\n'
  printf 'sys.path.insert(0, str(root / "share/openautosar/tools"))\n'
  printf 'from oa_cli.__main__ import main\n'
  printf 'raise SystemExit(main())\n'
} >"${install_prefix}/bin/oa"
chmod 0755 "${install_prefix}/bin/oa"
install -d "${install_prefix}/share/openautosar/deployment/machine"
cp -a "${repo_root}/deployment/machine/." "${install_prefix}/share/openautosar/deployment/machine/"
install -D -m 0644 \
  "${runtime_files}/openautosar-bootstrap.service" \
  "${install_prefix}/share/openautosar/systemd/openautosar-bootstrap.service"
install -D -m 0644 \
  "${runtime_files}/openautosar-bootstrap.env" \
  "${install_prefix}/share/openautosar/systemd/openautosar-bootstrap.env"
install -D -m 0644 \
  "${dashboard_files}/openautosar-dashboard.service" \
  "${install_prefix}/share/openautosar/systemd/openautosar-dashboard.service"
install -D -m 0644 \
  "${dashboard_files}/openautosar-dashboard.env" \
  "${install_prefix}/share/openautosar/systemd/openautosar-dashboard.env"
install -D -m 0755 \
  "${dashboard_files}/openautosar-dashboard-launch.sh" \
  "${install_prefix}/share/openautosar/systemd/openautosar-dashboard-launch.sh"
install -D -m 0755 \
  "${dashboard_files}/openautosar-dashboard-launch.sh" \
  "${install_prefix}/bin/openautosar-dashboard-launch"
install -D -m 0644 \
  "${security_files}/openautosar-security-policy.yaml" \
  "${install_prefix}/share/openautosar/security/openautosar-security-policy.yaml"
install -D -m 0644 \
  "${crypto_files}/openautosar-crypto-policy.yaml" \
  "${install_prefix}/share/openautosar/security/openautosar-crypto-policy.yaml"
install -D -m 0644 \
  "${firewall_files}/openautosar-firewall-policy.yaml" \
  "${install_prefix}/share/openautosar/security/openautosar-firewall-policy.yaml"
install -D -m 0644 \
  "${idsm_files}/openautosar-idsm-policy.yaml" \
  "${install_prefix}/share/openautosar/security/openautosar-idsm-policy.yaml"
install -D -m 0644 \
  "${log_trace_files}/openautosar-log-trace-policy.yaml" \
  "${install_prefix}/share/openautosar/observability/openautosar-log-trace-policy.yaml"
install -D -m 0644 \
  "${local_ipc_files}/openautosar-local-ipc-policy.yaml" \
  "${install_prefix}/share/openautosar/communication/openautosar-local-ipc-policy.yaml"
install -D -m 0644 \
  "${process_files}/openautosar-process-launcher-policy.yaml" \
  "${install_prefix}/share/openautosar/execution/openautosar-process-launcher-policy.yaml"
install -D -m 0644 \
  "${state_files}/openautosar-state-management-policy.yaml" \
  "${install_prefix}/share/openautosar/state/openautosar-state-management-policy.yaml"
install -D -m 0644 \
  "${registry_files}/openautosar-registry-policy.yaml" \
  "${install_prefix}/share/openautosar/registry/openautosar-registry-policy.yaml"
install -D -m 0644 \
  "${vehicle_ucm_files}/openautosar-vehicle-ucm-policy.yaml" \
  "${install_prefix}/share/openautosar/update/openautosar-vehicle-ucm-policy.yaml"
install -D -m 0644 \
  "${e2e_files}/openautosar-e2e-protection-policy.yaml" \
  "${install_prefix}/share/openautosar/communication/openautosar-e2e-protection-policy.yaml"
install -D -m 0644 \
  "${raw_stream_files}/openautosar-raw-data-stream-policy.yaml" \
  "${install_prefix}/share/openautosar/communication/openautosar-raw-data-stream-policy.yaml"
install -D -m 0644 \
  "${api_gateway_files}/openautosar-automotive-api-gateway-policy.yaml" \
  "${install_prefix}/share/openautosar/communication/openautosar-api-gateway-policy.yaml"
install -D -m 0644 \
  "${remote_persistency_files}/openautosar-remote-persistency-policy.yaml" \
  "${install_prefix}/share/openautosar/persistency/openautosar-remote-persistency-policy.yaml"
install -D -m 0644 \
  "${time_network_files}/openautosar-time-network-policy.yaml" \
  "${install_prefix}/share/openautosar/network/openautosar-time-network-policy.yaml"
install -D -m 0644 \
  "${safe_hwa_files}/openautosar-safe-hardware-acceleration-policy.yaml" \
  "${install_prefix}/share/openautosar/safety/openautosar-safe-hwa-policy.yaml"
PYTHONPATH="${repo_root}/tools" python3 -m oa_cli generate \
  --model "${model}" \
  --output "${install_prefix}/share/openautosar/generated"
rm -rf "${install_prefix}/share/openautosar/generated/classic-integration"
install -d "${install_prefix}/share/openautosar/generated/classic-integration"
cp -a "${repo_root}/out/generated/classic-integration/." \
  "${install_prefix}/share/openautosar/generated/classic-integration/"

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli provenance \
  --target "${target}" \
  --profile "${profile}" \
  --output "${install_prefix}/provenance.json"

oem_export_args=(
  export-evidence
  --repo-root "${repo_root}"
  --package-root "${install_prefix}"
  --output "${install_prefix}/share/openautosar/oem-export"
  --model "${model}"
  --generated "${install_prefix}/share/openautosar/generated"
  --evidence-dir "${repo_root}/out/evidence"
  --test-results-dir "${repo_root}/out/test-results"
  --profile "${profile}"
  --target "${target}"
)
PYTHONPATH="${repo_root}/tools" python3 -m oa_cli "${oem_export_args[@]}"

debug_bundle="${repo_root}/out/evidence/debug-bundle/manifest.json"
PYTHONPATH="${repo_root}/tools" python3 -m oa_cli debug bundle \
  --repo-root "${repo_root}" \
  --package-root "${install_prefix}" \
  --generated "${install_prefix}/share/openautosar/generated" \
  --evidence-dir "${repo_root}/out/evidence" \
  --test-results-dir "${repo_root}/out/test-results" \
  --output "${debug_bundle}"
install -D -m 0644 \
  "${debug_bundle}" \
  "${install_prefix}/share/openautosar/debug-bundle/manifest.json"

PYTHONPATH="${repo_root}/tools" python3 -m oa_cli "${oem_export_args[@]}"

supplier_delivery="${repo_root}/out/supplier-delivery/${profile}"
"${repo_root}/scripts/export-supplier-delivery.sh" \
  --profile "${profile}" \
  --delivery-profile "${delivery_profile}" \
  --target "${target}" \
  --model "${model}" \
  --package-root "${install_prefix}" \
  --generated "${install_prefix}/share/openautosar/generated" \
  --evidence-dir "${repo_root}/out/evidence" \
  --test-results-dir "${repo_root}/out/test-results" \
  --output "${supplier_delivery}"

echo "package staged: ${install_prefix}"
