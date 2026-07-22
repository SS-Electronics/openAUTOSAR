# SPDX-License-Identifier: MIT

add_library(openautosar_raw_data_stream src/raw_data_stream.cpp)
add_library(openautosar::raw_data_stream ALIAS openautosar_raw_data_stream)

target_include_directories(
  openautosar_raw_data_stream
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(openautosar_raw_data_stream PUBLIC openautosar::core)
oa_apply_common_options(openautosar_raw_data_stream)

set_target_properties(
  openautosar_raw_data_stream
  PROPERTIES
    EXPORT_NAME raw_data_stream
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_raw_data_stream
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
