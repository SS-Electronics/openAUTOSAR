# SPDX-License-Identifier: MIT

add_library(openautosar_local_ipc src/local_ipc_binding.cpp)
add_library(openautosar::local_ipc ALIAS openautosar_local_ipc)

target_include_directories(
  openautosar_local_ipc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_local_ipc PUBLIC openautosar::com openautosar::core)
oa_apply_common_options(openautosar_local_ipc)

set_target_properties(
  openautosar_local_ipc
  PROPERTIES
    EXPORT_NAME local_ipc
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_local_ipc
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
