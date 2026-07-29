cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_stream_edit LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_stream_edit
  WFP
  NTL
  driver/main.cpp
)
add_executable(crtsys_wfp_stream_edit_app app/main.cpp)

target_include_directories(crtsys_wfp_stream_edit
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_stream_edit_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_link_libraries(crtsys_wfp_stream_edit
  WDK::NDIS
)
target_link_libraries(crtsys_wfp_stream_edit_app
  PRIVATE fwpuclnt ws2_32
)

foreach(target
    crtsys_wfp_stream_edit
    crtsys_wfp_stream_edit_app)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
target_compile_options(crtsys_wfp_stream_edit_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

add_custom_command(TARGET crtsys_wfp_stream_edit POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_stream_edit.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_stream_edit>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_stream_edit)
set_std_cxx_latest(crtsys_wfp_stream_edit_app)
