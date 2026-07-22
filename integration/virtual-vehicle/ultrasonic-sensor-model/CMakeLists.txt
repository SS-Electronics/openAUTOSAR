# SPDX-License-Identifier: MIT

add_library(
  openautosar_virtual_vehicle
  src/classic_ultrasonic_pdu.cpp
  src/socketcan_endpoint.cpp
  src/socketcan_ultrasonic_frame.cpp
  src/ultrasonic_sensor_model.cpp
  src/ultrasonic_signal_validator.cpp
  src/ultrasonic_types.cpp
)
add_library(openautosar::virtual_vehicle ALIAS openautosar_virtual_vehicle)

target_include_directories(
  openautosar_virtual_vehicle
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_virtual_vehicle PUBLIC openautosar::core)
oa_apply_common_options(openautosar_virtual_vehicle)

set_target_properties(
  openautosar_virtual_vehicle
  PROPERTIES
    EXPORT_NAME virtual_vehicle
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_virtual_vehicle
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
