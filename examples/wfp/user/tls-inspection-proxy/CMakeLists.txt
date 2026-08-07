cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_wfp_tls_inspection_proxy LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/CPM.cmake")

CPMAddPackage(NAME crtsys SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../..")
include("${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_wfp_tls_inspection_proxy
  WFP
  NTL
  driver/main.cpp
)
add_executable(crtsys_wfp_tls_inspection_proxy_service
  app/inspection_policy.cpp
  app/main.cpp
  app/proxy_engine.cpp
)
add_executable(crtsys_wfp_tls_inspection_proxy_acceptance
  ../../../../test/wfp/runtime/fixtures/user/tls-inspection-proxy/main.cpp
)
add_executable(crtsys_wfp_tls_inspection_proxy_live_acceptance
  ../../../../test/wfp/runtime/https-live/tls-inspection-proxy/live_host.cpp
  ../../../../test/wfp/runtime/https-live/tls-inspection-proxy/main.cpp
)

target_include_directories(crtsys_wfp_tls_inspection_proxy
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shared"
)
target_include_directories(crtsys_wfp_tls_inspection_proxy_service
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${CMAKE_CURRENT_LIST_DIR}/app"
)
target_include_directories(crtsys_wfp_tls_inspection_proxy_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/fixtures/common"
)
target_include_directories(crtsys_wfp_tls_inspection_proxy_live_acceptance
  PRIVATE
    "${crtsys_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_LIST_DIR}/shared"
    "${crtsys_SOURCE_DIR}/test/wfp/runtime/https-live/tls-inspection-proxy"
)
target_link_libraries(crtsys_wfp_tls_inspection_proxy WDK::NDIS)
target_link_libraries(crtsys_wfp_tls_inspection_proxy_service
  PRIVATE
    advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32
)
target_link_libraries(crtsys_wfp_tls_inspection_proxy_acceptance
  PRIVATE bcrypt crypt32 ncrypt secur32 ws2_32
)
target_link_libraries(crtsys_wfp_tls_inspection_proxy_live_acceptance
  PRIVATE advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32
)
add_dependencies(crtsys_wfp_tls_inspection_proxy_acceptance
  crtsys_wfp_tls_inspection_proxy_service)

foreach(target
    crtsys_wfp_tls_inspection_proxy
    crtsys_wfp_tls_inspection_proxy_service
    crtsys_wfp_tls_inspection_proxy_acceptance
    crtsys_wfp_tls_inspection_proxy_live_acceptance)
  target_compile_options(${target}
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
      "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
  )
endforeach()
foreach(user_target
    crtsys_wfp_tls_inspection_proxy_service
    crtsys_wfp_tls_inspection_proxy_acceptance
    crtsys_wfp_tls_inspection_proxy_live_acceptance)
target_compile_options(${user_target}
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
)
endforeach()

add_custom_command(TARGET crtsys_wfp_tls_inspection_proxy POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_LIST_DIR}/crtsys_wfp_tls_inspection_proxy.inf"
          "$<TARGET_FILE_DIR:crtsys_wfp_tls_inspection_proxy>"
)

include("${crtsys_SOURCE_DIR}/cmake/std-cxx.cmake")
set_std_cxx_latest(crtsys_wfp_tls_inspection_proxy)
set_std_cxx_latest(crtsys_wfp_tls_inspection_proxy_service)
set_std_cxx_latest(crtsys_wfp_tls_inspection_proxy_acceptance)
set_std_cxx_latest(crtsys_wfp_tls_inspection_proxy_live_acceptance)

include(CTest)
if(BUILD_TESTING)
  add_executable(crtsys_wfp_tls_inspection_http1_contracts
    app/inspection_policy.cpp
    test/http1_contracts.cpp
  )
  add_executable(crtsys_wfp_tls_inspection_http2_contracts
    app/inspection_policy.cpp
    test/http2_contracts.cpp
  )
  add_executable(crtsys_wfp_tls_redirect_handoff_contracts
    test/redirect_handoff_contracts.cpp
  )
  add_executable(crtsys_wfp_redirected_tls_session_contracts
    test/redirected_tls_session_contracts.cpp
  )
  foreach(contract_target
      crtsys_wfp_tls_inspection_http1_contracts
      crtsys_wfp_tls_inspection_http2_contracts
      crtsys_wfp_tls_redirect_handoff_contracts
      crtsys_wfp_redirected_tls_session_contracts)
    target_include_directories(${contract_target}
      PRIVATE
        "${crtsys_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/../../shared"
        "${CMAKE_CURRENT_LIST_DIR}/app"
        "${CMAKE_CURRENT_LIST_DIR}/shared"
    )
    target_link_libraries(${contract_target}
      PRIVATE
        advapi32 bcrypt crypt32 fwpuclnt ncrypt secur32 uuid ws2_32
    )
    target_compile_options(${contract_target}
      PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
        "$<$<COMPILE_LANGUAGE:CXX>:/WX>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MTd>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MT>"
    )
    set_std_cxx_latest(${contract_target})
  endforeach()
  add_test(NAME crtsys_wfp_tls_inspection_http1_contracts
           COMMAND crtsys_wfp_tls_inspection_http1_contracts)
  add_test(NAME crtsys_wfp_tls_inspection_http2_contracts
           COMMAND crtsys_wfp_tls_inspection_http2_contracts)
  add_test(NAME crtsys_wfp_tls_redirect_handoff_contracts
           COMMAND crtsys_wfp_tls_redirect_handoff_contracts)
  add_test(NAME crtsys_wfp_redirected_tls_session_contracts
           COMMAND crtsys_wfp_redirected_tls_session_contracts)
endif()
