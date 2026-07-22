# SPDX-License-Identifier: MIT

add_executable(oa_unit_tests runtime_smoke_test.cpp)
target_link_libraries(
  oa_unit_tests
  PRIVATE
    openautosar::bootstrap
    openautosar::core
    openautosar::execution
    openautosar::log
)
oa_apply_common_options(oa_unit_tests)
oa_enable_link_map(oa_unit_tests)

add_test(NAME oa_unit_tests COMMAND oa_unit_tests)

add_executable(oa_log_trace_tests log_trace_test.cpp)
target_link_libraries(
  oa_log_trace_tests
  PRIVATE
    openautosar::log
)
oa_apply_common_options(oa_log_trace_tests)
oa_enable_link_map(oa_log_trace_tests)

add_test(NAME oa_log_trace_tests COMMAND oa_log_trace_tests)

add_executable(oa_platform_health_tests platform_health_test.cpp)
target_link_libraries(
  oa_platform_health_tests
  PRIVATE
    openautosar::phm
)
oa_apply_common_options(oa_platform_health_tests)
oa_enable_link_map(oa_platform_health_tests)

add_test(NAME oa_platform_health_tests COMMAND oa_platform_health_tests)

add_executable(oa_persistency_tests persistency_test.cpp)
target_link_libraries(
  oa_persistency_tests
  PRIVATE
    openautosar::persistency
)
oa_apply_common_options(oa_persistency_tests)
oa_enable_link_map(oa_persistency_tests)

add_test(NAME oa_persistency_tests COMMAND oa_persistency_tests)

add_executable(oa_update_management_tests update_management_test.cpp)
target_link_libraries(
  oa_update_management_tests
  PRIVATE
    openautosar::ucm
)
oa_apply_common_options(oa_update_management_tests)
oa_enable_link_map(oa_update_management_tests)

add_test(NAME oa_update_management_tests COMMAND oa_update_management_tests)

add_executable(oa_vehicle_update_management_tests vehicle_update_management_test.cpp)
target_link_libraries(
  oa_vehicle_update_management_tests
  PRIVATE
    openautosar::vehicle_ucm
)
oa_apply_common_options(oa_vehicle_update_management_tests)
oa_enable_link_map(oa_vehicle_update_management_tests)

add_test(NAME oa_vehicle_update_management_tests COMMAND oa_vehicle_update_management_tests)

add_executable(oa_registry_tests registry_test.cpp)
target_link_libraries(
  oa_registry_tests
  PRIVATE
    openautosar::registry
)
oa_apply_common_options(oa_registry_tests)
oa_enable_link_map(oa_registry_tests)

add_test(NAME oa_registry_tests COMMAND oa_registry_tests)

add_executable(oa_state_management_tests state_management_test.cpp)
target_link_libraries(
  oa_state_management_tests
  PRIVATE
    openautosar::state
)
oa_apply_common_options(oa_state_management_tests)
oa_enable_link_map(oa_state_management_tests)

add_test(NAME oa_state_management_tests COMMAND oa_state_management_tests)

add_executable(oa_process_launcher_tests process_launcher_test.cpp)
target_link_libraries(
  oa_process_launcher_tests
  PRIVATE
    openautosar::execution
    openautosar::process
)
oa_apply_common_options(oa_process_launcher_tests)
oa_enable_link_map(oa_process_launcher_tests)

add_test(NAME oa_process_launcher_tests COMMAND oa_process_launcher_tests)

add_executable(oa_diagnostic_management_tests diagnostic_management_test.cpp)
target_link_libraries(
  oa_diagnostic_management_tests
  PRIVATE
    openautosar::diagnostics
)
oa_apply_common_options(oa_diagnostic_management_tests)
oa_enable_link_map(oa_diagnostic_management_tests)

add_test(NAME oa_diagnostic_management_tests COMMAND oa_diagnostic_management_tests)

add_executable(oa_identity_access_tests identity_access_test.cpp)
target_link_libraries(
  oa_identity_access_tests
  PRIVATE
    openautosar::com
    openautosar::diagnostics
    openautosar::iam
)
oa_apply_common_options(oa_identity_access_tests)
oa_enable_link_map(oa_identity_access_tests)

add_test(NAME oa_identity_access_tests COMMAND oa_identity_access_tests)

add_executable(oa_firewall_tests firewall_test.cpp)
target_link_libraries(
  oa_firewall_tests
  PRIVATE
    openautosar::firewall
)
oa_apply_common_options(oa_firewall_tests)
oa_enable_link_map(oa_firewall_tests)

add_test(NAME oa_firewall_tests COMMAND oa_firewall_tests)

add_executable(oa_idsm_tests idsm_test.cpp)
target_link_libraries(
  oa_idsm_tests
  PRIVATE
    openautosar::idsm
)
oa_apply_common_options(oa_idsm_tests)
oa_enable_link_map(oa_idsm_tests)

add_test(NAME oa_idsm_tests COMMAND oa_idsm_tests)

add_executable(oa_time_network_management_tests time_network_management_test.cpp)
target_link_libraries(
  oa_time_network_management_tests
  PRIVATE
    openautosar::time_network
)
oa_apply_common_options(oa_time_network_management_tests)
oa_enable_link_map(oa_time_network_management_tests)

add_test(NAME oa_time_network_management_tests COMMAND oa_time_network_management_tests)

add_executable(oa_cryptography_tests cryptography_test.cpp)
target_link_libraries(
  oa_cryptography_tests
  PRIVATE
    openautosar::crypto
)
oa_apply_common_options(oa_cryptography_tests)
oa_enable_link_map(oa_cryptography_tests)

add_test(NAME oa_cryptography_tests COMMAND oa_cryptography_tests)

add_executable(oa_safe_hwa_tests safe_hardware_acceleration_test.cpp)
target_link_libraries(
  oa_safe_hwa_tests
  PRIVATE
    openautosar::safe_hwa
)
oa_apply_common_options(oa_safe_hwa_tests)
oa_enable_link_map(oa_safe_hwa_tests)

add_test(NAME oa_safe_hwa_tests COMMAND oa_safe_hwa_tests)

add_executable(oa_communication_tests communication_test.cpp)
target_link_libraries(
  oa_communication_tests
  PRIVATE
    openautosar::com
)
oa_apply_common_options(oa_communication_tests)
oa_enable_link_map(oa_communication_tests)

add_test(NAME oa_communication_tests COMMAND oa_communication_tests)

add_executable(oa_e2e_protection_tests e2e_protection_test.cpp)
target_link_libraries(
  oa_e2e_protection_tests
  PRIVATE
    openautosar::e2e
)
oa_apply_common_options(oa_e2e_protection_tests)
oa_enable_link_map(oa_e2e_protection_tests)

add_test(NAME oa_e2e_protection_tests COMMAND oa_e2e_protection_tests)

add_executable(oa_raw_data_stream_tests raw_data_stream_test.cpp)
target_link_libraries(
  oa_raw_data_stream_tests
  PRIVATE
    openautosar::raw_data_stream
)
oa_apply_common_options(oa_raw_data_stream_tests)
oa_enable_link_map(oa_raw_data_stream_tests)

add_test(NAME oa_raw_data_stream_tests COMMAND oa_raw_data_stream_tests)

add_executable(oa_automotive_api_gateway_tests automotive_api_gateway_test.cpp)
target_link_libraries(
  oa_automotive_api_gateway_tests
  PRIVATE
    openautosar::automotive_api_gateway
)
oa_apply_common_options(oa_automotive_api_gateway_tests)
oa_enable_link_map(oa_automotive_api_gateway_tests)

add_test(NAME oa_automotive_api_gateway_tests COMMAND oa_automotive_api_gateway_tests)

add_executable(oa_local_ipc_binding_tests local_ipc_binding_test.cpp)
target_link_libraries(
  oa_local_ipc_binding_tests
  PRIVATE
    openautosar::local_ipc
)
oa_apply_common_options(oa_local_ipc_binding_tests)
oa_enable_link_map(oa_local_ipc_binding_tests)

add_test(NAME oa_local_ipc_binding_tests COMMAND oa_local_ipc_binding_tests)

add_executable(oa_someip_protocol_tests someip_protocol_test.cpp)
target_link_libraries(
  oa_someip_protocol_tests
  PRIVATE
    openautosar::someip_protocol
)
oa_apply_common_options(oa_someip_protocol_tests)
oa_enable_link_map(oa_someip_protocol_tests)

add_test(NAME oa_someip_protocol_tests COMMAND oa_someip_protocol_tests)

add_executable(oa_someip_binding_tests someip_binding_test.cpp)
target_link_libraries(
  oa_someip_binding_tests
  PRIVATE
    openautosar::someip_binding
)
oa_apply_common_options(oa_someip_binding_tests)
oa_enable_link_map(oa_someip_binding_tests)

add_test(NAME oa_someip_binding_tests COMMAND oa_someip_binding_tests)

add_executable(oa_dds_rtps_tests dds_rtps_test.cpp)
target_link_libraries(
  oa_dds_rtps_tests
  PRIVATE
    openautosar::dds_rtps
)
oa_apply_common_options(oa_dds_rtps_tests)
oa_enable_link_map(oa_dds_rtps_tests)

add_test(NAME oa_dds_rtps_tests COMMAND oa_dds_rtps_tests)

add_executable(oa_dds_binding_tests dds_binding_test.cpp)
target_link_libraries(
  oa_dds_binding_tests
  PRIVATE
    openautosar::dds_binding
)
oa_apply_common_options(oa_dds_binding_tests)
oa_enable_link_map(oa_dds_binding_tests)

add_test(NAME oa_dds_binding_tests COMMAND oa_dds_binding_tests)

add_executable(oa_virtual_vehicle_tests virtual_vehicle_test.cpp)
target_link_libraries(
  oa_virtual_vehicle_tests
  PRIVATE
    openautosar::com
    openautosar::virtual_vehicle
)
oa_apply_common_options(oa_virtual_vehicle_tests)
oa_enable_link_map(oa_virtual_vehicle_tests)

add_test(NAME oa_virtual_vehicle_tests COMMAND oa_virtual_vehicle_tests)

add_executable(oa_gateway_diagnostic_tests gateway_diagnostic_test.cpp)
target_link_libraries(
  oa_gateway_diagnostic_tests
  PRIVATE
    openautosar::gateway_peer
)
oa_apply_common_options(oa_gateway_diagnostic_tests)
oa_enable_link_map(oa_gateway_diagnostic_tests)

add_test(NAME oa_gateway_diagnostic_tests COMMAND oa_gateway_diagnostic_tests)

add_executable(oa_classic_gateway_config_tests classic_gateway_config_test.cpp)
target_link_libraries(
  oa_classic_gateway_config_tests
  PRIVATE
    openautosar::gateway_peer
)
oa_apply_common_options(oa_classic_gateway_config_tests)
oa_enable_link_map(oa_classic_gateway_config_tests)

add_test(NAME oa_classic_gateway_config_tests COMMAND oa_classic_gateway_config_tests)

add_executable(oa_dashboard_snapshot_tests dashboard_snapshot_test.cpp)
target_link_libraries(
  oa_dashboard_snapshot_tests
  PRIVATE
    openautosar::dashboard
)
oa_apply_common_options(oa_dashboard_snapshot_tests)
oa_enable_link_map(oa_dashboard_snapshot_tests)

add_test(NAME oa_dashboard_snapshot_tests COMMAND oa_dashboard_snapshot_tests)

add_test(
  NAME oa_ultrasonic_gateway_smoke
  COMMAND oa-ultrasonic-gateway-smoke --samples 4 --fault crc_error --fault-at 2
)

add_test(
  NAME oa_classic_ecu_sim_dry_run
  COMMAND oa-classic-ecu-sim --dry-run --samples 4 --fault counter_jump --fault-at 2
)
