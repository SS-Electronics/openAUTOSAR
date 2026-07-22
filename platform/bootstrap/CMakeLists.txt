# SPDX-License-Identifier: MIT

add_library(openautosar_bootstrap src/platform_bootstrap.cpp)
add_library(openautosar::bootstrap ALIAS openautosar_bootstrap)

target_include_directories(
  openautosar_bootstrap
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_bootstrap
  PUBLIC
    openautosar::core
    openautosar::execution
    openautosar::log
    openautosar::state
)
oa_apply_common_options(openautosar_bootstrap)

set_target_properties(
  openautosar_bootstrap
  PROPERTIES
    EXPORT_NAME bootstrap
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_bootstrap
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
