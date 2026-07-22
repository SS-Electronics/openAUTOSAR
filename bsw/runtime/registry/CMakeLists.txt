# SPDX-License-Identifier: MIT

add_library(openautosar_registry src/registry_manager.cpp)
add_library(openautosar::registry ALIAS openautosar_registry)

target_include_directories(
  openautosar_registry
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_registry PUBLIC openautosar::core)
oa_apply_common_options(openautosar_registry)

set_target_properties(
  openautosar_registry
  PROPERTIES
    EXPORT_NAME registry
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_registry
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
