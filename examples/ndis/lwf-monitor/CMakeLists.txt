cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_ndis_lwf_monitor LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

set(CRTSYS_NTL_MAIN OFF)
CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_ndis_lwf_monitor NDIS NTL
  driver/main.cpp
)
target_include_directories(crtsys_ndis_lwf_monitor
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)

add_executable(crtsys_ndis_lwf_monitor_app
  app/main.cpp
)
target_include_directories(crtsys_ndis_lwf_monitor_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_link_libraries(crtsys_ndis_lwf_monitor_app
  PRIVATE iphlpapi ws2_32
)

foreach(target
    crtsys_ndis_lwf_monitor
    crtsys_ndis_lwf_monitor_app)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
target_compile_options(crtsys_ndis_lwf_monitor_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_ndis_lwf_monitor POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_ndis_lwf_monitor.inf"
          "$<TARGET_FILE_DIR:crtsys_ndis_lwf_monitor>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_ndis_lwf_monitor)
set_std_cxx_latest(crtsys_ndis_lwf_monitor_app)
