# SPDX-License-Identifier: MIT

add_library(openautosar_idsm src/idsm_manager.cpp)
add_library(openautosar::idsm ALIAS openautosar_idsm)

target_include_directories(
  openautosar_idsm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_idsm
  PUBLIC
    openautosar::core
    openautosar::iam
)
oa_apply_common_options(openautosar_idsm)

set_target_properties(
  openautosar_idsm
  PROPERTIES
    EXPORT_NAME idsm
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_idsm
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
