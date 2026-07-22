# SPDX-License-Identifier: MIT

option(OA_ENABLE_SANITIZERS "Enable host sanitizers for Debug builds" ON)
option(OA_ENABLE_LINK_MAPS "Generate linker map files for executables" ON)
option(OA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(openautosar_project_options INTERFACE)
target_compile_features(openautosar_project_options INTERFACE cxx_std_20)

add_library(openautosar_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    openautosar_warnings
    INTERFACE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wconversion
      "$<$<BOOL:${OA_WARNINGS_AS_ERRORS}>:-Werror>"
  )

  if(OA_ENABLE_SANITIZERS)
    target_compile_options(
      openautosar_project_options
      INTERFACE
        "$<$<CONFIG:Debug>:-fsanitize=address,undefined>"
        "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
    )
    target_link_options(
      openautosar_project_options
      INTERFACE
        "$<$<CONFIG:Debug>:-fsanitize=address,undefined>"
    )
  endif()
endif()

function(oa_apply_common_options target_name)
  target_link_libraries(${target_name} PUBLIC openautosar_project_options)
  target_link_libraries(${target_name} PRIVATE openautosar_warnings)
endfunction()

function(oa_enable_link_map target_name)
  if(NOT OA_ENABLE_LINK_MAPS)
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    return()
  endif()

  get_target_property(target_type ${target_name} TYPE)
  if(
    target_type STREQUAL "EXECUTABLE"
    OR target_type STREQUAL "SHARED_LIBRARY"
    OR target_type STREQUAL "MODULE_LIBRARY"
  )
    target_link_options(
      ${target_name}
      PRIVATE
        "LINKER:-Map=$<TARGET_FILE_DIR:${target_name}>/$<TARGET_FILE_BASE_NAME:${target_name}>.map"
    )
  endif()
endfunction()
