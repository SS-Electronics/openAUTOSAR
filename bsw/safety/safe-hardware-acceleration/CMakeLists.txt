# SPDX-License-Identifier: MIT

add_library(openautosar_safe_hwa src/safe_hardware_acceleration.cpp)
add_library(openautosar::safe_hwa ALIAS openautosar_safe_hwa)

target_include_directories(
  openautosar_safe_hwa
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_safe_hwa PUBLIC openautosar::core)
oa_apply_common_options(openautosar_safe_hwa)

set_target_properties(
  openautosar_safe_hwa
  PROPERTIES
    EXPORT_NAME safe_hwa
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_safe_hwa
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
