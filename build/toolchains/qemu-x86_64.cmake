# SPDX-License-Identifier: MIT

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER c++ CACHE FILEPATH "C++ compiler for qemu-x86_64 host-compatible builds")
endif()
