set(CRTSYS_FLT_RUNTIME_APP_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/communication_advanced.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/coroutine_runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/data_scan_runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/mini_spy_runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/operation_status_runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/self_issued_io_runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/transaction_runtime.cpp"
)

set(CRTSYS_FLT_RUNTIME_APP_LIBRARIES
  FltLib.lib
  KtmW32.lib
)
