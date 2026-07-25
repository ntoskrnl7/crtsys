cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_flt_runtime_test LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_flt_runtime_test
  MINIFILTER
  NTL
  WINVER 0x0602
  driver/communication_advanced.cpp
  driver/main.cpp
  driver/mini_spy_runtime.cpp
  driver/operation_status_runtime.cpp
  driver/ownership_runtime.cpp
)

crtsys_add_driver(crtsys_flt_io_buffer_runtime_test
  MINIFILTER
  NTL
  io_buffer_driver/main.cpp
)

crtsys_add_driver(crtsys_flt_name_changer_runtime_test
  MINIFILTER
  NTL
  name_changer_driver/main.cpp
)

crtsys_add_driver(crtsys_flt_simrep_runtime_test
  MINIFILTER
  NTL
  simrep_driver/main.cpp
)

crtsys_add_driver(crtsys_flt_delete_runtime_test
  MINIFILTER
  NTL
  WINVER 0x0A00
  delete_driver/main.cpp
)

crtsys_add_driver(crtsys_flt_scanner_runtime_test
  MINIFILTER
  NTL
  WINVER 0x0A00
  scanner_driver/main.cpp
)

crtsys_add_driver(crtsys_flt_metadata_runtime_test
  MINIFILTER
  NTL
  WINVER 0x0A00
  metadata_driver/main.cpp
)

crtsys_add_driver(crtsys_flt_cdo_runtime_test
  MINIFILTER
  NTL
  WINVER 0x0A00
  cdo_driver/main.cpp
)

add_custom_command(TARGET crtsys_flt_io_buffer_runtime_test POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_LIST_DIR}/io_buffer_driver/crtsys_flt_io_buffer_runtime_test.inf"
    "$<TARGET_FILE_DIR:crtsys_flt_io_buffer_runtime_test>/crtsys_flt_io_buffer_runtime_test.inf"
  VERBATIM
)

add_custom_command(TARGET crtsys_flt_name_changer_runtime_test POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_LIST_DIR}/name_changer_driver/crtsys_flt_name_changer_runtime_test.inf"
    "$<TARGET_FILE_DIR:crtsys_flt_name_changer_runtime_test>/crtsys_flt_name_changer_runtime_test.inf"
  VERBATIM
)

add_custom_command(TARGET crtsys_flt_simrep_runtime_test POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_LIST_DIR}/simrep_driver/crtsys_flt_simrep_runtime_test.inf"
    "$<TARGET_FILE_DIR:crtsys_flt_simrep_runtime_test>/crtsys_flt_simrep_runtime_test.inf"
  VERBATIM
)

add_custom_command(TARGET crtsys_flt_delete_runtime_test POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_LIST_DIR}/delete_driver/crtsys_flt_delete_runtime_test.inf"
    "$<TARGET_FILE_DIR:crtsys_flt_delete_runtime_test>/crtsys_flt_delete_runtime_test.inf"
  VERBATIM
)

add_custom_command(TARGET crtsys_flt_scanner_runtime_test POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_LIST_DIR}/scanner_driver/crtsys_flt_scanner_runtime_test.inf"
    "$<TARGET_FILE_DIR:crtsys_flt_scanner_runtime_test>/crtsys_flt_scanner_runtime_test.inf"
  VERBATIM
)

include("${CMAKE_CURRENT_LIST_DIR}/app/runtime_sources.cmake")
add_executable(crtsys_flt_runtime_test_app
  ${CRTSYS_FLT_RUNTIME_APP_SOURCES}
)

add_executable(crtsys_flt_io_buffer_runtime_test_app
  io_buffer_app/main.cpp
)

add_executable(crtsys_flt_name_changer_runtime_test_app
  name_changer_app/directory_tests.cpp
  name_changer_app/main.cpp
  name_changer_app/namespace_tests.cpp
  name_changer_app/record_validation_tests.cpp
)

add_executable(crtsys_flt_simrep_runtime_test_app
  simrep_app/main.cpp
)

add_executable(crtsys_flt_delete_runtime_test_app
  delete_app/main.cpp
)

add_executable(crtsys_flt_scanner_runtime_test_app
  scanner_app/main.cpp
)

add_executable(crtsys_flt_metadata_runtime_test_app
  metadata_app/main.cpp
)

add_executable(crtsys_flt_cdo_runtime_test_app
  cdo_app/main.cpp
)

foreach(_security_probe IN ITEMS allowed denied)
  add_executable(crtsys_flt_security_probe_${_security_probe}
    app/security_probe.cpp
  )
  target_include_directories(crtsys_flt_security_probe_${_security_probe}
    PRIVATE
      "${crtsys_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_LIST_DIR}/shared"
  )
  target_link_libraries(crtsys_flt_security_probe_${_security_probe}
    PRIVATE FltLib.lib
  )
  target_compile_definitions(crtsys_flt_security_probe_${_security_probe}
    PRIVATE
      CRTSYS_SECURITY_PROBE_EXPECT_ALLOWED=$<IF:$<STREQUAL:${_security_probe},allowed>,1,0>
  )
  target_compile_options(crtsys_flt_security_probe_${_security_probe}
    PRIVATE
      /W4
      /WX
      /EHsc
      "$<$<CONFIG:Debug>:/MTd>"
      "$<$<CONFIG:Release>:/MT>"
  )
endforeach()

target_include_directories(crtsys_flt_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
)

target_include_directories(crtsys_flt_io_buffer_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/io_buffer_shared"
)

target_include_directories(crtsys_flt_io_buffer_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/io_buffer_shared"
)

target_include_directories(crtsys_flt_name_changer_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/name_changer_shared"
)

target_include_directories(crtsys_flt_name_changer_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/name_changer_shared"
)

target_include_directories(crtsys_flt_simrep_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/simrep_shared"
)

target_include_directories(crtsys_flt_simrep_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/simrep_shared"
)

target_include_directories(crtsys_flt_delete_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/delete_shared"
)

target_include_directories(crtsys_flt_delete_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/delete_shared"
)

target_include_directories(crtsys_flt_scanner_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/scanner_shared"
)

target_include_directories(crtsys_flt_scanner_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/scanner_shared"
)

target_include_directories(crtsys_flt_metadata_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/metadata_shared"
)

target_include_directories(crtsys_flt_metadata_runtime_test_app
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/metadata_shared"
)

target_include_directories(crtsys_flt_cdo_runtime_test
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cdo_shared"
)

target_include_directories(crtsys_flt_cdo_runtime_test_app
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cdo_shared"
)

target_link_libraries(crtsys_flt_runtime_test_app
  PRIVATE ${CRTSYS_FLT_RUNTIME_APP_LIBRARIES}
)
target_link_libraries(crtsys_flt_io_buffer_runtime_test_app
  PRIVATE FltLib.lib Advapi32.lib
)
target_link_libraries(crtsys_flt_name_changer_runtime_test_app
  PRIVATE FltLib.lib Advapi32.lib
)
target_link_libraries(crtsys_flt_simrep_runtime_test_app
  PRIVATE FltLib.lib
)
target_link_libraries(crtsys_flt_delete_runtime_test_app
  PRIVATE FltLib.lib
)
target_link_libraries(crtsys_flt_scanner_runtime_test_app
  PRIVATE FltLib.lib KtmW32.lib
)

target_link_libraries(crtsys_flt_metadata_runtime_test_app
  PRIVATE FltLib.lib
)

target_link_libraries(crtsys_flt_cdo_runtime_test_app
  PRIVATE FltLib.lib Advapi32.lib
)

target_compile_options(crtsys_flt_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)

target_compile_options(crtsys_flt_io_buffer_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_io_buffer_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

target_compile_options(crtsys_flt_name_changer_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_name_changer_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

target_compile_options(crtsys_flt_simrep_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_simrep_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

target_compile_options(crtsys_flt_delete_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_delete_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

target_compile_options(crtsys_flt_scanner_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_scanner_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

target_compile_options(crtsys_flt_metadata_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_metadata_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

target_compile_options(crtsys_flt_cdo_runtime_test
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
)

target_compile_options(crtsys_flt_cdo_runtime_test_app
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/MT>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_flt_runtime_test)
set_std_cxx_latest(crtsys_flt_runtime_test_app)
set_std_cxx_latest(crtsys_flt_io_buffer_runtime_test)
set_std_cxx_latest(crtsys_flt_io_buffer_runtime_test_app)
set_std_cxx_latest(crtsys_flt_name_changer_runtime_test)
set_std_cxx_latest(crtsys_flt_name_changer_runtime_test_app)
set_std_cxx_latest(crtsys_flt_simrep_runtime_test)
set_std_cxx_latest(crtsys_flt_simrep_runtime_test_app)
set_std_cxx_latest(crtsys_flt_delete_runtime_test)
set_std_cxx_latest(crtsys_flt_delete_runtime_test_app)
set_std_cxx_latest(crtsys_flt_scanner_runtime_test)
set_std_cxx_latest(crtsys_flt_scanner_runtime_test_app)
set_std_cxx_latest(crtsys_flt_metadata_runtime_test)
set_std_cxx_latest(crtsys_flt_metadata_runtime_test_app)
set_std_cxx_latest(crtsys_flt_cdo_runtime_test)
set_std_cxx_latest(crtsys_flt_cdo_runtime_test_app)
set_std_cxx_latest(crtsys_flt_security_probe_allowed)
set_std_cxx_latest(crtsys_flt_security_probe_denied)
