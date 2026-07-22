# SPDX-License-Identifier: MIT

add_library(openautosar_diagnostics src/diagnostic_manager.cpp)
add_library(openautosar::diagnostics ALIAS openautosar_diagnostics)

target_include_directories(
  openautosar_diagnostics
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_diagnostics PUBLIC openautosar::core openautosar::iam)
oa_apply_common_options(openautosar_diagnostics)

set_target_properties(
  openautosar_diagnostics
  PROPERTIES
    EXPORT_NAME diagnostics
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_diagnostics
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
