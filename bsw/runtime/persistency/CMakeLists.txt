# SPDX-License-Identifier: MIT

add_library(openautosar_persistency src/persistency.cpp)
add_library(openautosar::persistency ALIAS openautosar_persistency)

target_include_directories(
  openautosar_persistency
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_persistency PUBLIC openautosar::core)
oa_apply_common_options(openautosar_persistency)

set_target_properties(
  openautosar_persistency
  PROPERTIES
    EXPORT_NAME persistency
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_persistency
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
