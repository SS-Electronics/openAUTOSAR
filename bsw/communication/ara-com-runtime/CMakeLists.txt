# SPDX-License-Identifier: MIT

add_library(openautosar_com src/service_registry.cpp src/service_types.cpp)
add_library(openautosar::com ALIAS openautosar_com)

target_include_directories(
  openautosar_com
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_com PUBLIC openautosar::core openautosar::iam)
oa_apply_common_options(openautosar_com)

set_target_properties(
  openautosar_com
  PROPERTIES
    EXPORT_NAME com
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_com
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
