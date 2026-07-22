# SPDX-License-Identifier: MIT

add_library(openautosar_firewall src/firewall_manager.cpp)
add_library(openautosar::firewall ALIAS openautosar_firewall)

target_include_directories(
  openautosar_firewall
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_firewall
  PUBLIC
    openautosar::core
    openautosar::iam
)
oa_apply_common_options(openautosar_firewall)

set_target_properties(
  openautosar_firewall
  PROPERTIES
    EXPORT_NAME firewall
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_firewall
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
