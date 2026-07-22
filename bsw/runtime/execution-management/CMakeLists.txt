# SPDX-License-Identifier: MIT

add_library(openautosar_execution src/execution_manager.cpp)
add_library(openautosar::execution ALIAS openautosar_execution)

target_include_directories(
  openautosar_execution
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_execution
  PUBLIC
    openautosar::core
    openautosar::process
)
oa_apply_common_options(openautosar_execution)

set_target_properties(
  openautosar_execution
  PROPERTIES
    EXPORT_NAME execution
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_execution
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
