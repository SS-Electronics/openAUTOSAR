# SPDX-License-Identifier: MIT

add_library(openautosar_phm src/platform_health_manager.cpp)
add_library(openautosar::phm ALIAS openautosar_phm)

target_include_directories(
  openautosar_phm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_phm PUBLIC openautosar::core)
oa_apply_common_options(openautosar_phm)

set_target_properties(
  openautosar_phm
  PROPERTIES
    EXPORT_NAME phm
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_phm
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
