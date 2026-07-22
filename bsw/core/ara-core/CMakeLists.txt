# SPDX-License-Identifier: MIT

add_library(openautosar_core src/instance_specifier.cpp)
add_library(openautosar::core ALIAS openautosar_core)

target_include_directories(
  openautosar_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

oa_apply_common_options(openautosar_core)

set_target_properties(
  openautosar_core
  PROPERTIES
    EXPORT_NAME core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_core
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
