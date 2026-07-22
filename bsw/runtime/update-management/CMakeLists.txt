# SPDX-License-Identifier: MIT

add_library(openautosar_ucm src/update_manager.cpp)
add_library(openautosar::ucm ALIAS openautosar_ucm)

target_include_directories(
  openautosar_ucm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_ucm PUBLIC openautosar::core openautosar::phm)
oa_apply_common_options(openautosar_ucm)

set_target_properties(
  openautosar_ucm
  PROPERTIES
    EXPORT_NAME ucm
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_ucm
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
