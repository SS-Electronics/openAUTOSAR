# SPDX-License-Identifier: MIT

add_library(openautosar_automotive_api_gateway src/automotive_api_gateway.cpp)
add_library(openautosar::automotive_api_gateway ALIAS openautosar_automotive_api_gateway)

target_include_directories(
  openautosar_automotive_api_gateway
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_automotive_api_gateway
  PUBLIC
    openautosar::com
    openautosar::core
    openautosar::iam
)
oa_apply_common_options(openautosar_automotive_api_gateway)

set_target_properties(
  openautosar_automotive_api_gateway
  PROPERTIES
    EXPORT_NAME automotive_api_gateway
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_automotive_api_gateway
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
