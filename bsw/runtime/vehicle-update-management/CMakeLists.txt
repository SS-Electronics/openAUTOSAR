# SPDX-License-Identifier: MIT

add_library(openautosar_vehicle_ucm src/vehicle_update_manager.cpp)
add_library(openautosar::vehicle_ucm ALIAS openautosar_vehicle_ucm)

target_include_directories(
  openautosar_vehicle_ucm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_vehicle_ucm
  PUBLIC
    openautosar::core
    openautosar::ucm
)
oa_apply_common_options(openautosar_vehicle_ucm)

set_target_properties(
  openautosar_vehicle_ucm
  PROPERTIES
    EXPORT_NAME vehicle_ucm
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_vehicle_ucm
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
