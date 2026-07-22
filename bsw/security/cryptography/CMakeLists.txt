# SPDX-License-Identifier: MIT

find_package(OpenSSL REQUIRED)

add_library(openautosar_crypto src/crypto_provider.cpp)
add_library(openautosar::crypto ALIAS openautosar_crypto)

target_include_directories(
  openautosar_crypto
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_crypto
  PUBLIC
    openautosar::core
    OpenSSL::Crypto
)
oa_apply_common_options(openautosar_crypto)

set_target_properties(
  openautosar_crypto
  PROPERTIES
    EXPORT_NAME crypto
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS openautosar_crypto
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
