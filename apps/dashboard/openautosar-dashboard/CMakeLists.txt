# SPDX-License-Identifier: MIT

add_library(openautosar_dashboard src/dashboard_snapshot.cpp)
add_library(openautosar::dashboard ALIAS openautosar_dashboard)

target_include_directories(
  openautosar_dashboard
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  openautosar_dashboard
  PUBLIC
    openautosar::gateway_peer
    openautosar::phm
    openautosar::ucm
)
oa_apply_common_options(openautosar_dashboard)

set_target_properties(
  openautosar_dashboard
  PROPERTIES
    EXPORT_NAME dashboard
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_executable(oa-dashboard-snapshot src/snapshot_main.cpp)
target_link_libraries(oa-dashboard-snapshot PRIVATE openautosar::dashboard)
oa_apply_common_options(oa-dashboard-snapshot)
oa_enable_link_map(oa-dashboard-snapshot)

find_package(Qt6 COMPONENTS Core Qml Quick QUIET)
if(Qt6_FOUND)
  set(
    oa_dashboard_qml_file
    "${CMAKE_INSTALL_FULL_DATADIR}/openautosar/dashboard/qml/OpenAutosarDashboard.qml"
  )
  add_executable(oa-dashboard src/qt_main.cpp)
  target_link_libraries(
    oa-dashboard
    PRIVATE
      Qt6::Core
      Qt6::Qml
      Qt6::Quick
      openautosar::dashboard
  )
  target_compile_definitions(
    oa-dashboard
    PRIVATE
      "OA_DASHBOARD_QML_FILE=\"${oa_dashboard_qml_file}\""
  )
  oa_apply_common_options(oa-dashboard)
  oa_enable_link_map(oa-dashboard)
  install(TARGETS oa-dashboard RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(
  TARGETS openautosar_dashboard oa-dashboard-snapshot
  EXPORT OpenAutosarTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
  DIRECTORY qml/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/openautosar/dashboard/qml"
)
