# SPDX-License-Identifier: MIT

add_library(openautosar_state src/state_manager.cpp)
add_library(openautosar::state ALIAS openautosar_state)

target_include_directories(
  openautosar_state
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_state
  PUBLIC
    openautosar::core
    openautosar::execution
)
oa_apply_common_options(openautosar_state)

set_target_properties(
  openautosar_state
  PROPERTIES
    EXPORT_NAME state
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_state
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
