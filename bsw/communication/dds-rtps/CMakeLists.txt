# SPDX-License-Identifier: MIT

add_library(
  openautosar_dds_rtps
  src/endpoint_discovery.cpp
  src/participant_discovery.cpp
  src/rtps_message.cpp
  src/udp_endpoint.cpp
)
add_library(openautosar::dds_rtps ALIAS openautosar_dds_rtps)

target_include_directories(
  openautosar_dds_rtps
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_dds_rtps PUBLIC openautosar::core)
oa_apply_common_options(openautosar_dds_rtps)

set_target_properties(
  openautosar_dds_rtps
  PROPERTIES
    EXPORT_NAME dds_rtps
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_dds_rtps
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
