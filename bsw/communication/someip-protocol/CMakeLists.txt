# SPDX-License-Identifier: MIT

add_library(
  openautosar_someip_protocol
  src/message.cpp
  src/request_correlation.cpp
  src/service_discovery.cpp
  src/tcp_stream.cpp
  src/udp_endpoint.cpp
)
add_library(openautosar::someip_protocol ALIAS openautosar_someip_protocol)

target_include_directories(
  openautosar_someip_protocol
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_someip_protocol PUBLIC openautosar::core)
oa_apply_common_options(openautosar_someip_protocol)

set_target_properties(
  openautosar_someip_protocol
  PROPERTIES
    EXPORT_NAME someip_protocol
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_someip_protocol
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
