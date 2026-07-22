# SPDX-License-Identifier: MIT

add_library(openautosar_e2e src/e2e_protection.cpp)
add_library(openautosar::e2e ALIAS openautosar_e2e)

target_include_directories(
  openautosar_e2e
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_e2e PUBLIC openautosar::core)
oa_apply_common_options(openautosar_e2e)

set_target_properties(
  openautosar_e2e
  PROPERTIES
    EXPORT_NAME e2e
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_e2e
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
