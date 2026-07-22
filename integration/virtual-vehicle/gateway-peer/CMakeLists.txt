# SPDX-License-Identifier: MIT

add_library(
  openautosar_gateway_peer
  src/classic_gateway_config.cpp
  src/ultrasonic_diagnostic_bridge.cpp
)
add_library(openautosar::gateway_peer ALIAS openautosar_gateway_peer)

target_include_directories(
  openautosar_gateway_peer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_gateway_peer
  PUBLIC
    openautosar::diagnostics
    openautosar::virtual_vehicle
)
oa_apply_common_options(openautosar_gateway_peer)

set_target_properties(
  openautosar_gateway_peer
  PROPERTIES
    EXPORT_NAME gateway_peer
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_executable(oa-ultrasonic-gateway-smoke src/main.cpp)
target_link_libraries(
  oa-ultrasonic-gateway-smoke
  PRIVATE
    openautosar::com
    openautosar::gateway_peer
    openautosar::phm
)
oa_apply_common_options(oa-ultrasonic-gateway-smoke)
oa_enable_link_map(oa-ultrasonic-gateway-smoke)

install(
  TARGETS openautosar_gateway_peer
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(TARGETS oa-ultrasonic-gateway-smoke RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
