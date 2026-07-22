#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
layer="${repo_root}/meta-openautosar"
evidence_dir="${repo_root}/out/evidence/yocto-layer"
log_trace_recipe="${layer}/recipes-platform/openautosar-log-trace"
log_trace_policy="${log_trace_recipe}/files/openautosar-log-trace-policy.yaml"
local_ipc_recipe="${layer}/recipes-platform/openautosar-local-ipc"
local_ipc_policy="${local_ipc_recipe}/files/openautosar-local-ipc-policy.yaml"
process_recipe="${layer}/recipes-platform/openautosar-process-launcher"
process_policy="${process_recipe}/files/openautosar-process-launcher-policy.yaml"
state_recipe="${layer}/recipes-platform/openautosar-state-management"
state_policy="${state_recipe}/files/openautosar-state-management-policy.yaml"
registry_recipe="${layer}/recipes-platform/openautosar-registry"
registry_policy="${registry_recipe}/files/openautosar-registry-policy.yaml"
vehicle_ucm_recipe="${layer}/recipes-platform/openautosar-vehicle-ucm"
vehicle_ucm_policy="${vehicle_ucm_recipe}/files/openautosar-vehicle-ucm-policy.yaml"
e2e_recipe="${layer}/recipes-platform/openautosar-e2e-protection"
e2e_policy="${e2e_recipe}/files/openautosar-e2e-protection-policy.yaml"
raw_stream_recipe="${layer}/recipes-platform/openautosar-raw-data-stream"
raw_stream_policy="${raw_stream_recipe}/files/openautosar-raw-data-stream-policy.yaml"
api_gateway_recipe="${layer}/recipes-platform/openautosar-automotive-api-gateway"
api_gateway_policy="${api_gateway_recipe}/files"
api_gateway_policy+="/openautosar-automotive-api-gateway-policy.yaml"
remote_persistency_recipe="${layer}/recipes-platform/openautosar-remote-persistency"
remote_persistency_policy="${remote_persistency_recipe}/files"
remote_persistency_policy+="/openautosar-remote-persistency-policy.yaml"
safe_hwa_recipe="${layer}/recipes-platform/openautosar-safe-hardware-acceleration"
safe_hwa_policy="${safe_hwa_recipe}/files"
safe_hwa_policy+="/openautosar-safe-hardware-acceleration-policy.yaml"
firewall_recipe="${layer}/recipes-security/openautosar-firewall"
firewall_policy="${firewall_recipe}/files/openautosar-firewall-policy.yaml"
idsm_recipe="${layer}/recipes-security/openautosar-idsm"
idsm_policy="${idsm_recipe}/files/openautosar-idsm-policy.yaml"

required_files=(
  "${layer}/conf/layer.conf"
  "${layer}/conf/distro/include/openautosar-agl-pin.inc"
  "${layer}/classes/openautosar-app.bbclass"
  "${layer}/classes/openautosar-cluster.bbclass"
  "${layer}/recipes-platform/openautosar-runtime/openautosar-runtime_0.1.0.bb"
  "${layer}/recipes-platform/openautosar-runtime/files/openautosar-bootstrap.service"
  "${layer}/recipes-platform/openautosar-runtime/files/openautosar-bootstrap.env"
  "${log_trace_recipe}/openautosar-log-trace_0.1.0.bb"
  "${log_trace_policy}"
  "${local_ipc_recipe}/openautosar-local-ipc_0.1.0.bb"
  "${local_ipc_policy}"
  "${process_recipe}/openautosar-process-launcher_0.1.0.bb"
  "${process_policy}"
  "${state_recipe}/openautosar-state-management_0.1.0.bb"
  "${state_policy}"
  "${registry_recipe}/openautosar-registry_0.1.0.bb"
  "${registry_policy}"
  "${vehicle_ucm_recipe}/openautosar-vehicle-ucm_0.1.0.bb"
  "${vehicle_ucm_policy}"
  "${e2e_recipe}/openautosar-e2e-protection_0.1.0.bb"
  "${e2e_policy}"
  "${raw_stream_recipe}/openautosar-raw-data-stream_0.1.0.bb"
  "${raw_stream_policy}"
  "${api_gateway_recipe}/openautosar-automotive-api-gateway_0.1.0.bb"
  "${api_gateway_policy}"
  "${remote_persistency_recipe}/openautosar-remote-persistency_0.1.0.bb"
  "${remote_persistency_policy}"
  "${safe_hwa_recipe}/openautosar-safe-hardware-acceleration_0.1.0.bb"
  "${safe_hwa_policy}"
  "${layer}/recipes-platform/openautosar-time-network/openautosar-time-network_0.1.0.bb"
  "${layer}/recipes-platform/openautosar-time-network/files/openautosar-time-network-policy.yaml"
  "${layer}/recipes-security/openautosar-crypto/openautosar-crypto_0.1.0.bb"
  "${layer}/recipes-security/openautosar-crypto/files/openautosar-crypto-policy.yaml"
  "${layer}/recipes-security/openautosar-security/openautosar-security_0.1.0.bb"
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
  "${firewall_recipe}/openautosar-firewall_0.1.0.bb"
  "${firewall_policy}"
  "${idsm_recipe}/openautosar-idsm_0.1.0.bb"
  "${idsm_policy}"
  "${layer}/recipes-ui/openautosar-dashboard/openautosar-dashboard_0.1.0.bb"
  "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard.service"
  "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard.env"
  "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard-launch.sh"
  "${layer}/recipes-core/images/openautosar-image-dev.bb"
  "${layer}/recipes-core/images/openautosar-image-ci.bb"
  "${layer}/recipes-core/images/openautosar-image-production.bb"
  "${layer}/recipes-core/images/openautosar-image-safety.bb"
  "${layer}/wic/openautosar-reference-ab.wks"
  "${repo_root}/deployment/machine/qemux86-64-agl-unagi.yaml"
)

mkdir -p "${evidence_dir}"

for path in "${required_files[@]}"; do
  if [[ ! -f "${path}" ]]; then
    echo "missing required Yocto/AGL metadata: ${path}" >&2
    exit 1
  fi
done

grep -q 'BBFILE_COLLECTIONS.*openautosar' "${layer}/conf/layer.conf"
grep -q 'LAYERSERIES_COMPAT_openautosar = "scarthgap"' "${layer}/conf/layer.conf"
grep -q 'OPENAUTOSAR_AGL_RELEASE_VERSION = "21.0.2"' "${layer}/conf/layer.conf"
grep -q 'OPENAUTOSAR_YOCTO_RELEASE = "scarthgap-5.0.18"' "${layer}/conf/layer.conf"
grep -q 'inherit openautosar-cluster openautosar-app useradd' \
  "${layer}/recipes-platform/openautosar-runtime/openautosar-runtime_0.1.0.bb"
grep -q 'SYSTEMD_SERVICE:${PN} = "openautosar-bootstrap.service"' \
  "${layer}/recipes-platform/openautosar-runtime/openautosar-runtime_0.1.0.bb"
grep -q 'DEPENDS += "openssl"' \
  "${layer}/recipes-platform/openautosar-runtime/openautosar-runtime_0.1.0.bb"
grep -q 'ExecStart=/usr/bin/oa-bootstrap' \
  "${layer}/recipes-platform/openautosar-runtime/files/openautosar-bootstrap.service"
grep -q 'NoNewPrivileges=true' \
  "${layer}/recipes-platform/openautosar-runtime/files/openautosar-bootstrap.service"
grep -q 'ProtectSystem=strict' \
  "${layer}/recipes-platform/openautosar-runtime/files/openautosar-bootstrap.service"
grep -q 'schema: openautosar.log-trace.policy.v1' "${log_trace_policy}"
grep -q 'backend_independent: true' "${log_trace_policy}"
grep -q 'overflow_policy: drop-oldest' "${log_trace_policy}"
grep -q 'count_sink_failures: true' "${log_trace_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime"' \
  "${log_trace_recipe}/openautosar-log-trace_0.1.0.bb"
grep -q 'openautosar-log-trace' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-log-trace' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-log-trace' "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-log-trace' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.local-ipc.policy.v1' "${local_ipc_policy}"
grep -q 'transport: unix-domain-socket' "${local_ipc_policy}"
grep -q 'overflow_policy: reject-newest' "${local_ipc_policy}"
grep -q 'verify_security_label: true' "${local_ipc_policy}"
grep -q 'stale_endpoint_cleanup: true' "${local_ipc_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime"' \
  "${local_ipc_recipe}/openautosar-local-ipc_0.1.0.bb"
grep -q 'openautosar-local-ipc' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-local-ipc' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-local-ipc' "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-local-ipc' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.process-launcher.policy.v1' "${process_policy}"
grep -q 'launcher: systemd-scope' "${process_policy}"
grep -q 'record_cgroup_operations: true' "${process_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime systemd"' \
  "${process_recipe}/openautosar-process-launcher_0.1.0.bb"
grep -q 'openautosar-process-launcher' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-process-launcher' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-process-launcher' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-process-launcher' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.state-management.policy.v1' "${state_policy}"
grep -q 'Function Group state model and request policy' \
  "${state_recipe}/openautosar-state-management_0.1.0.bb"
grep -q 'required_priority: critical' "${state_policy}"
grep -q 'failure_state: Degraded' "${state_policy}"
grep -q 'record_transition_causes: true' "${state_policy}"
grep -q 'openautosar-state-management' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-state-management' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-state-management' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-state-management' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.registry.policy.v1' "${registry_policy}"
grep -q 'mode: basic-runtime-registry' "${registry_policy}"
grep -q 'require_source_model: true' "${registry_policy}"
grep -q 'reject_duplicate_active_records: true' "${registry_policy}"
grep -q 'deterministic_ordering: true' "${registry_policy}"
grep -q 'count_rejected_updates: true' "${registry_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime"' \
  "${registry_recipe}/openautosar-registry_0.1.0.bb"
grep -q 'openautosar-registry' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-registry' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-registry' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-registry' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.vehicle-ucm.policy.v1' "${vehicle_ucm_policy}"
grep -q 'mode: coordinated-campaign-simulator' "${vehicle_ucm_policy}"
grep -q 'require_all_targets_online: true' "${vehicle_ucm_policy}"
grep -q 'require_rollback_capable: true' "${vehicle_ucm_policy}"
grep -q 'activation_state: UpdateAllowed' "${vehicle_ucm_policy}"
grep -q 'coordinated_rollback_on_health_failure: true' "${vehicle_ucm_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openautosar-security"' \
  "${vehicle_ucm_recipe}/openautosar-vehicle-ucm_0.1.0.bb"
grep -q 'openautosar-vehicle-ucm' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-vehicle-ucm' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-vehicle-ucm' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-vehicle-ucm' \
  "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.e2e-protection.policy.v1' "${e2e_policy}"
grep -q 'crc32-iso-hdlc' "${e2e_policy}"
grep -q 'transport_independent: true' "${e2e_policy}"
grep -q 'source_model_pointer' "${e2e_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime"' \
  "${e2e_recipe}/openautosar-e2e-protection_0.1.0.bb"
grep -q 'openautosar-e2e-protection' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-e2e-protection' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-e2e-protection' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-e2e-protection' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.raw-data-stream.policy.v1' "${raw_stream_policy}"
grep -q 'mode: partial-bounded-stream' "${raw_stream_policy}"
grep -q 'fragment_reassembly: enabled' "${raw_stream_policy}"
grep -q 'reject_stale_frames: true' "${raw_stream_policy}"
grep -q 'deterministic_receipts: true' "${raw_stream_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime"' \
  "${raw_stream_recipe}/openautosar-raw-data-stream_0.1.0.bb"
grep -q 'openautosar-raw-data-stream' \
  "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-raw-data-stream' \
  "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-raw-data-stream' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-raw-data-stream' \
  "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.automotive-api-gateway.policy.v1' \
  "${api_gateway_policy}"
grep -q 'mode: bounded-ingress-mediator' "${api_gateway_policy}"
grep -q 'namespace: openautosar::gateway' "${api_gateway_policy}"
grep -q 'ara_extension: false' "${api_gateway_policy}"
grep -q 'backend: iam' "${api_gateway_policy}"
grep -q 'reject_unauthenticated_clients: true' "${api_gateway_policy}"
grep -q 'reject_remote_clients_by_default: true' "${api_gateway_policy}"
grep -q 'service_discovery_forwarding: ara-com' "${api_gateway_policy}"
grep -q 'event_publish_forwarding: ara-com' "${api_gateway_policy}"
grep -q 'deterministic_receipts: true' "${api_gateway_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openautosar-security"' \
  "${api_gateway_recipe}/openautosar-automotive-api-gateway_0.1.0.bb"
grep -q 'openautosar-automotive-api-gateway' \
  "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-automotive-api-gateway' \
  "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-automotive-api-gateway' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-automotive-api-gateway' \
  "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.remote-persistency.policy.v1' \
  "${remote_persistency_policy}"
grep -q 'mode: represented-not-production-backend' "${remote_persistency_policy}"
grep -q 'reject_weakened_targets: true' "${remote_persistency_policy}"
grep -q 'confidentiality_required: true' "${remote_persistency_policy}"
grep -q 'integrity_required: true' "${remote_persistency_policy}"
grep -q 'rollback_protection_required: true' "${remote_persistency_policy}"
grep -q 'required_data_residency_region: us-east-1' "${remote_persistency_policy}"
grep -q 'record_blocked_receipts: true' "${remote_persistency_policy}"
grep -q 'network_backend: deferred' "${remote_persistency_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openautosar-security"' \
  "${remote_persistency_recipe}/openautosar-remote-persistency_0.1.0.bb"
grep -q 'openautosar-remote-persistency' \
  "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-remote-persistency' \
  "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-remote-persistency' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-remote-persistency' \
  "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.safe-hardware-acceleration.policy.v1' \
  "${safe_hwa_policy}"
grep -q 'mode: represented-not-production-backend' "${safe_hwa_policy}"
grep -q 'provider_model: safety-aware-accelerator-service' "${safe_hwa_policy}"
grep -q 'production_hardware_backend: deferred' "${safe_hwa_policy}"
grep -q 'isolation: iommu-or-stronger' "${safe_hwa_policy}"
grep -q 'memory_ownership: service-owned-copy' "${safe_hwa_policy}"
grep -q 'job_timeout: deterministic' "${safe_hwa_policy}"
grep -q 'result_plausibility: required' "${safe_hwa_policy}"
grep -q 'data_corruption_detection: required' "${safe_hwa_policy}"
grep -q 'record_provider_gaps: true' "${safe_hwa_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openautosar-security"' \
  "${safe_hwa_recipe}/openautosar-safe-hardware-acceleration_0.1.0.bb"
grep -q 'openautosar-safe-hardware-acceleration' \
  "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-safe-hardware-acceleration' \
  "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-safe-hardware-acceleration' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-safe-hardware-acceleration' \
  "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.time-network.policy.v1' \
  "${layer}/recipes-platform/openautosar-time-network/files/openautosar-time-network-policy.yaml"
grep -q 'jump_policy: degrade' \
  "${layer}/recipes-platform/openautosar-time-network/files/openautosar-time-network-policy.yaml"
grep -q 'tap-openautosar0' \
  "${layer}/recipes-platform/openautosar-time-network/files/openautosar-time-network-policy.yaml"
grep -q 'allow_in_update_mode: false' \
  "${layer}/recipes-platform/openautosar-time-network/files/openautosar-time-network-policy.yaml"
grep -q 'openautosar-time-network' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-time-network' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-time-network' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-time-network' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.crypto.policy.v1' \
  "${layer}/recipes-security/openautosar-crypto/files/openautosar-crypto-policy.yaml"
grep -q 'provider: openssl' \
  "${layer}/recipes-security/openautosar-crypto/files/openautosar-crypto-policy.yaml"
grep -q 'material_source: provisioned' \
  "${layer}/recipes-security/openautosar-crypto/files/openautosar-crypto-policy.yaml"
grep -q 'exportable: false' \
  "${layer}/recipes-security/openautosar-crypto/files/openautosar-crypto-policy.yaml"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openssl"' \
  "${layer}/recipes-security/openautosar-crypto/openautosar-crypto_0.1.0.bb"
grep -q 'openautosar-crypto' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-crypto' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-crypto' "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-crypto' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'production_mode: true' \
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
grep -q 'remote_diagnostics_allowed: false' \
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
grep -q 'network-policy-admin' \
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
grep -q 'install-firewall-rule' \
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
grep -q 'api-gateway-client' \
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
grep -q 'invoke-gateway-route' \
  "${layer}/recipes-security/openautosar-security/files/openautosar-security-policy.yaml"
grep -q 'openautosar-security' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-security' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-security' "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-security' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.firewall.policy.v1' "${firewall_policy}"
grep -q 'mode: staged-command-generation' "${firewall_policy}"
grep -q 'backend: nftables' "${firewall_policy}"
grep -q 'default_ingress: deny' "${firewall_policy}"
grep -q 'require_default_deny: true' "${firewall_policy}"
grep -q 'authorization: iam' "${firewall_policy}"
grep -q 'tap-openautosar0' "${firewall_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openautosar-security"' \
  "${firewall_recipe}/openautosar-firewall_0.1.0.bb"
grep -q 'openautosar-firewall' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-firewall' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-firewall' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-firewall' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'schema: openautosar.idsm.policy.v1' "${idsm_policy}"
grep -q 'mode: bounded-normalizer' "${idsm_policy}"
grep -q 'privacy_mode: redact-principal' "${idsm_policy}"
grep -q 'per_key_count: 10' "${idsm_policy}"
grep -q 'forwarding_threshold: critical' "${idsm_policy}"
grep -q 'firewall' "${idsm_policy}"
grep -q 'RDEPENDS:${PN} += "openautosar-runtime openautosar-security"' \
  "${idsm_recipe}/openautosar-idsm_0.1.0.bb"
grep -q 'openautosar-idsm' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-idsm' "${layer}/recipes-core/images/openautosar-image-ci.bb"
grep -q 'openautosar-idsm' \
  "${layer}/recipes-core/images/openautosar-image-production.bb"
grep -q 'openautosar-idsm' "${layer}/recipes-core/images/openautosar-image-safety.bb"
grep -q 'SYSTEMD_SERVICE:${PN} = "openautosar-dashboard.service"' \
  "${layer}/recipes-ui/openautosar-dashboard/openautosar-dashboard_0.1.0.bb"
grep -q 'qtdeclarative-qmlplugins' \
  "${layer}/recipes-ui/openautosar-dashboard/openautosar-dashboard_0.1.0.bb"
grep -q 'ExecStart=/usr/bin/openautosar-dashboard-launch' \
  "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard.service"
grep -q '/usr/bin/qml' \
  "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard-launch.sh"
grep -q 'oa-dashboard-snapshot' \
  "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard-launch.sh"
grep -q 'openautosar-dashboard' "${layer}/recipes-core/images/openautosar-image-dev.bb"
grep -q 'openautosar-dashboard' "${layer}/recipes-core/images/openautosar-image-ci.bb"

if command -v systemd-analyze >/dev/null 2>&1; then
  verify_root="${evidence_dir}/systemd-root"
  rm -rf "${verify_root}"
  mkdir -p "${verify_root}/etc/systemd/system" "${verify_root}/usr/bin"
  cp "${layer}/recipes-platform/openautosar-runtime/files/openautosar-bootstrap.service" \
    "${verify_root}/etc/systemd/system/openautosar-bootstrap.service"
  cp "${layer}/recipes-ui/openautosar-dashboard/files/openautosar-dashboard.service" \
    "${verify_root}/etc/systemd/system/openautosar-dashboard.service"
  systemd_targets=(
    sysinit.target
    basic.target
    multi-user.target
    network-online.target
    local-fs.target
  )
  for target in "${systemd_targets[@]}"; do
    {
      printf '[Unit]\n'
      printf 'Description=validation stub %s\n' "${target}"
    } >"${verify_root}/etc/systemd/system/${target}"
  done
  printf '#!/usr/bin/env sh\nexit 0\n' >"${verify_root}/usr/bin/oa-bootstrap"
  printf '#!/usr/bin/env sh\nexit 0\n' >"${verify_root}/usr/bin/oa-dashboard-snapshot"
  printf '#!/usr/bin/env sh\nexit 0\n' >"${verify_root}/usr/bin/openautosar-dashboard-launch"
  printf '#!/usr/bin/env sh\nexit 0\n' >"${verify_root}/usr/bin/qml"
  chmod +x "${verify_root}/usr/bin/oa-bootstrap"
  chmod +x "${verify_root}/usr/bin/oa-dashboard-snapshot"
  chmod +x "${verify_root}/usr/bin/openautosar-dashboard-launch"
  chmod +x "${verify_root}/usr/bin/qml"

  systemd-analyze verify \
    --root="${verify_root}" \
    /etc/systemd/system/openautosar-bootstrap.service \
    /etc/systemd/system/openautosar-dashboard.service \
    >"${evidence_dir}/systemd-analyze.log" 2>&1 || {
      cat "${evidence_dir}/systemd-analyze.log" >&2
      exit 1
    }
else
  echo "systemd-analyze not available" >"${evidence_dir}/systemd-analyze.log"
fi

{
  printf '{\n'
  printf '  "layer": "meta-openautosar",\n'
  printf '  "target": "qemux86-64-agl-unagi",\n'
  printf '  "agl_release_version": "21.0.2",\n'
  printf '  "yocto_release": "scarthgap-5.0.18",\n'
  printf '  "systemd_unit": "openautosar-bootstrap.service",\n'
  printf '  "dashboard_unit": "openautosar-dashboard.service",\n'
  printf '  "log_trace_policy": "openautosar-log-trace-policy.yaml",\n'
  printf '  "local_ipc_policy": "openautosar-local-ipc-policy.yaml",\n'
  printf '  "process_launcher_policy": "openautosar-process-launcher-policy.yaml",\n'
  printf '  "state_management_policy": "openautosar-state-management-policy.yaml",\n'
  printf '  "registry_policy": "openautosar-registry-policy.yaml",\n'
  printf '  "vehicle_ucm_policy": "openautosar-vehicle-ucm-policy.yaml",\n'
  printf '  "e2e_protection_policy": "openautosar-e2e-protection-policy.yaml",\n'
  printf '  "raw_data_stream_policy": "openautosar-raw-data-stream-policy.yaml",\n'
  printf '  "api_gateway_policy": "openautosar-automotive-api-gateway-policy.yaml",\n'
  printf '  "remote_persistency_policy": "openautosar-remote-persistency-policy.yaml",\n'
  printf '  "safe_hwa_policy": "openautosar-safe-hardware-acceleration-policy.yaml",\n'
  printf '  "crypto_policy": "openautosar-crypto-policy.yaml",\n'
  printf '  "security_policy": "openautosar-security-policy.yaml",\n'
  printf '  "firewall_policy": "openautosar-firewall-policy.yaml",\n'
  printf '  "idsm_policy": "openautosar-idsm-policy.yaml",\n'
  printf '  "time_network_policy": "openautosar-time-network-policy.yaml",\n'
  printf '  "image_flavors": ["dev", "ci", "production", "safety"],\n'
  printf '  "status": "valid"\n'
  printf '}\n'
} >"${evidence_dir}/validation.json"

echo "Yocto layer metadata valid: ${evidence_dir}/validation.json"
