# SPDX-License-Identifier: MIT

add_library(openautosar_someip_binding src/someip_binding.cpp)
add_library(openautosar::someip_binding ALIAS openautosar_someip_binding)

target_include_directories(
  openautosar_someip_binding
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_someip_binding
  PUBLIC
    openautosar::com
    openautosar::someip_protocol
)
oa_apply_common_options(openautosar_someip_binding)

set_target_properties(
  openautosar_someip_binding
  PROPERTIES
    EXPORT_NAME someip_binding
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_someip_binding
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
