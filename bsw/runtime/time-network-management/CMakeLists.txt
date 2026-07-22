# SPDX-License-Identifier: MIT

add_library(openautosar_time_network src/time_network_manager.cpp)
add_library(openautosar::time_network ALIAS openautosar_time_network)

target_include_directories(
  openautosar_time_network
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_time_network
  PUBLIC
    openautosar::core
    openautosar::execution
    openautosar::iam
)
oa_apply_common_options(openautosar_time_network)

set_target_properties(
  openautosar_time_network
  PROPERTIES
    EXPORT_NAME time_network
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_time_network
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
