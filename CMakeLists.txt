# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)

project(
  openAUTOSAR
  VERSION 0.1.0
  DESCRIPTION "Original Adaptive Platform-oriented runtime bootstrap"
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake")
include(OpenAutosarCompilerOptions)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

install(
  TARGETS openautosar_project_options openautosar_warnings
  EXPORT OpenAutosarTargets
)

add_subdirectory(bsw/core/ara-core)
add_subdirectory(platform/os/process)
add_subdirectory(bsw/runtime/log-and-trace)
add_subdirectory(bsw/runtime/execution-management)
add_subdirectory(bsw/runtime/state-management)
add_subdirectory(bsw/runtime/platform-health-management)
add_subdirectory(bsw/runtime/persistency)
add_subdirectory(bsw/runtime/update-management)
add_subdirectory(bsw/runtime/vehicle-update-management)
add_subdirectory(bsw/runtime/registry)
add_subdirectory(bsw/security/cryptography)
add_subdirectory(bsw/security/identity-access-management)
add_subdirectory(bsw/security/firewall)
add_subdirectory(bsw/security/intrusion-detection-system-management)
add_subdirectory(bsw/runtime/diagnostic-management)
add_subdirectory(bsw/runtime/time-network-management)
add_subdirectory(bsw/safety/safe-hardware-acceleration)
add_subdirectory(bsw/communication/ara-com-runtime)
add_subdirectory(bsw/communication/automotive-api-gateway)
add_subdirectory(bsw/communication/e2e-protection)
add_subdirectory(bsw/communication/raw-data-stream)
add_subdirectory(bsw/communication/local-ipc-binding)
add_subdirectory(bsw/communication/someip-protocol)
add_subdirectory(bsw/communication/someip-binding)
add_subdirectory(bsw/communication/dds-rtps)
add_subdirectory(bsw/communication/dds-binding)
add_subdirectory(integration/virtual-vehicle/ultrasonic-sensor-model)
add_subdirectory(integration/virtual-vehicle/classic-ecu-simulator)
add_subdirectory(integration/virtual-vehicle/gateway-peer)
add_subdirectory(platform/bootstrap)
add_subdirectory(apps/dashboard/openautosar-dashboard)
add_subdirectory(apps/examples/hello-service)

option(OA_BUILD_TESTS "Build openAUTOSAR tests" ON)
if(OA_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(tests/unit)
  endif()
endif()

install(
  EXPORT OpenAutosarTargets
  NAMESPACE openautosar::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/openautosar"
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake/OpenAutosarConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenAutosarConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/openautosar"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/OpenAutosarConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OpenAutosarConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenAutosarConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/openautosar"
)
