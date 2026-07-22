# SPDX-License-Identifier: MIT

add_library(openautosar_dds_binding src/dds_binding.cpp)
add_library(openautosar::dds_binding ALIAS openautosar_dds_binding)

target_include_directories(
  openautosar_dds_binding
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_dds_binding
  PUBLIC
    openautosar::com
    openautosar::dds_rtps
)
oa_apply_common_options(openautosar_dds_binding)

set_target_properties(
  openautosar_dds_binding
  PROPERTIES
    EXPORT_NAME dds_binding
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_dds_binding
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
