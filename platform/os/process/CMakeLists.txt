# SPDX-License-Identifier: MIT

add_library(openautosar_process src/process_launcher.cpp)
add_library(openautosar::process ALIAS openautosar_process)

target_include_directories(
  openautosar_process
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_process PUBLIC openautosar::core)
oa_apply_common_options(openautosar_process)

set_target_properties(
  openautosar_process
  PROPERTIES
    EXPORT_NAME process
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_process
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
