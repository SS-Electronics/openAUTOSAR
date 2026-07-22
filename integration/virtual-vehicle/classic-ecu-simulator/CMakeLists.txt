# SPDX-License-Identifier: MIT

add_executable(oa-classic-ecu-sim src/main.cpp)
target_link_libraries(oa-classic-ecu-sim PRIVATE openautosar::virtual_vehicle)
oa_apply_common_options(oa-classic-ecu-sim)
oa_enable_link_map(oa-classic-ecu-sim)

install(TARGETS oa-classic-ecu-sim RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
