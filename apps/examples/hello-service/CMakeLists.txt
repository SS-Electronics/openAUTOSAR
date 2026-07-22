# SPDX-License-Identifier: MIT

add_executable(oa-bootstrap src/main.cpp)
target_link_libraries(oa-bootstrap PRIVATE openautosar::bootstrap)
oa_apply_common_options(oa-bootstrap)
oa_enable_link_map(oa-bootstrap)

install(TARGETS oa-bootstrap RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
