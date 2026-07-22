# SPDX-License-Identifier: MIT

add_library(openautosar_iam src/identity_access_manager.cpp)
add_library(openautosar::iam ALIAS openautosar_iam)

target_include_directories(
  openautosar_iam
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_iam PUBLIC openautosar::core)
oa_apply_common_options(openautosar_iam)

set_target_properties(
  openautosar_iam
  PROPERTIES
    EXPORT_NAME iam
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_iam
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
