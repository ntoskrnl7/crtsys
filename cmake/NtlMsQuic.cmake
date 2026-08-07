include_guard(GLOBAL)

set_property(GLOBAL PROPERTY CRTSYS_NTL_MSQUIC_MODULE_DIR
             "${CMAKE_CURRENT_LIST_DIR}")
set(CRTSYS_NTL_MSQUIC_HEADER_REVISION
    "b3945bb0c9e44463c93dac13e40975a7c3a526ca")
set(CRTSYS_NTL_MSQUIC_HEADER_SHA256
    "C9ABFDD02C45910649DD335D6BD82718E4DDD2FDB35FE550567C78F032551E0C")
set(CRTSYS_NTL_MSQUIC_WINDOWS_CHECKOUT_SHA256
    "BF58566E13DD633050D52B8A1F42803CF70E328AA58D841A4597E08FAA646D57")
set(CRTSYS_NTL_MSQUIC_HEADER_MANIFEST
    "msquic.h|C9ABFDD02C45910649DD335D6BD82718E4DDD2FDB35FE550567C78F032551E0C|BF58566E13DD633050D52B8A1F42803CF70E328AA58D841A4597E08FAA646D57"
    "msquic_winuser.h|7C54AEA27C784BD9F2F609668F7141E299F0E35209015BD559A5BA12AA136D08|DDD1532A4C1CA38D01F2CC8CB457EC8BF6FBE08A87EB1F168363A3941FBEBB06"
    "msquic_winkernel.h|153A3B639E6494DC1E978A4C921CB2A62B6D10BF5FDB6039A37234C147C3CC65|9559D97E7CF84C14D57B9A0F0764400BA19F8DE91505FC13E602A2762399F7CA")
set(CRTSYS_NTL_MSQUIC_INCLUDE_DIR "" CACHE PATH
    "Directory containing the pinned public msquic.h ABI")

function(_crtsys_validate_ntl_msquic_header INCLUDE_DIR)
  foreach(_entry IN LISTS CRTSYS_NTL_MSQUIC_HEADER_MANIFEST)
    string(REPLACE "|" ";" _fields "${_entry}")
    list(GET _fields 0 _name)
    list(GET _fields 1 _raw_sha256)
    list(GET _fields 2 _windows_sha256)
    set(_header "${INCLUDE_DIR}/${_name}")
    if(NOT EXISTS "${_header}")
      message(FATAL_ERROR
        "The NTL MsQuic include directory does not contain ${_name}: "
        "${INCLUDE_DIR}")
    endif()
    file(SHA256 "${_header}" _actual_sha256)
    string(TOUPPER "${_actual_sha256}" _actual_sha256)
    if(NOT "${_actual_sha256}" STREQUAL "${_raw_sha256}" AND
       NOT "${_actual_sha256}" STREQUAL "${_windows_sha256}")
      message(FATAL_ERROR
        "The public ${_name} ABI does not match the NTL pin "
        "${CRTSYS_NTL_MSQUIC_HEADER_REVISION}: expected ${_raw_sha256} "
        "(raw) or ${_windows_sha256} (CRLF checkout), got "
        "${_actual_sha256}.")
    endif()
  endforeach()
endfunction()

# Keep every NTL MsQuic consumer on one audited public ABI revision.  This
# target supplies headers only: applications still deploy msquic.dll and
# kernel drivers still bind to an installed MsQuic NMR provider at runtime.
function(crtsys_add_ntl_msquic_headers)
  if(TARGET crtsys_ntl_msquic_headers)
    return()
  endif()

  get_property(_module_dir GLOBAL PROPERTY CRTSYS_NTL_MSQUIC_MODULE_DIR)

  set(_include_candidates)
  if(CRTSYS_NTL_MSQUIC_INCLUDE_DIR)
    list(APPEND _include_candidates "${CRTSYS_NTL_MSQUIC_INCLUDE_DIR}")
  endif()

  # NtlMsQuic.cmake is present both at <bundle>/cmake and at
  # <bundle>/share/crtsys/cmake. Prefer the offline header carried by the
  # release/NuGet bundle before considering a network fetch.
  list(APPEND _include_candidates
    "${_module_dir}/../build/native/msquic/include"
    "${_module_dir}/../../../build/native/msquic/include")

  set(_msquic_include_dir "")
  foreach(_candidate IN LISTS _include_candidates)
    if(EXISTS "${_candidate}/msquic.h")
      get_filename_component(_msquic_include_dir "${_candidate}" ABSOLUTE)
      break()
    endif()
  endforeach()

  if(NOT _msquic_include_dir)
    include("${_module_dir}/CPM.cmake")
    CPMAddPackage(
      # Keep the internal dependency name short. Visual Studio still applies
      # MAX_PATH to FetchContent's generated stamp files, where the name is
      # repeated several times under the caller's build directory.
      NAME ntl_quic
      GITHUB_REPOSITORY microsoft/msquic
      GIT_TAG "${CRTSYS_NTL_MSQUIC_HEADER_REVISION}"
      GIT_SUBMODULES ""
      GIT_SUBMODULES_RECURSE FALSE
      DOWNLOAD_ONLY YES)
    set(_msquic_include_dir "${ntl_quic_SOURCE_DIR}/src/inc")
  endif()

  _crtsys_validate_ntl_msquic_header("${_msquic_include_dir}")

  add_library(crtsys_ntl_msquic_headers INTERFACE)
  target_include_directories(crtsys_ntl_msquic_headers SYSTEM INTERFACE
    "${_msquic_include_dir}")
  set_property(TARGET crtsys_ntl_msquic_headers PROPERTY
    CRTSYS_NTL_MSQUIC_HEADER_REVISION
    "${CRTSYS_NTL_MSQUIC_HEADER_REVISION}")
endfunction()
