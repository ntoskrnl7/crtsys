# MsQuic provides the proxy service's QUIC/TLS 1.3 transport. The pinned msh3
# dependency is retained only for deterministic controlled clients and origin
# fixtures. Keep both revisions pinned.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(MSH3_TOOL OFF CACHE BOOL "" FORCE)
set(MSH3_PING OFF CACHE BOOL "" FORCE)
set(MSH3_TEST OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(QUIC_ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
set(QUIC_TLS_LIB schannel CACHE STRING "" FORCE)
CPMAddPackage(
  NAME msquic_xdp_headers
  GITHUB_REPOSITORY microsoft/xdp-for-windows
  GIT_TAG f23b1fb4d492d9c20bcd7767bba2278f94355df8
  GIT_SUBMODULES ""
  GIT_SUBMODULES_RECURSE FALSE
  DOWNLOAD_ONLY YES)
set(EXTRA_PLATFORM_INCLUDE_DIRECTORIES
    "${msquic_xdp_headers_SOURCE_DIR}/published/external")
CPMAddPackage(
  NAME msh3
  GITHUB_REPOSITORY nibanks/msh3
  GIT_TAG 92f23c72b50b0714d7cd39850e7b97e8ed1129e1
  GIT_SUBMODULES "ls-qpack;msquic"
  GIT_SUBMODULES_RECURSE FALSE)

# The pinned msh3 C API does not expose MsQuic's peer-certificate callback.
# Extend only this fetched acceptance dependency so a private test CA can be
# validated without weakening trust or modifying a machine/browser store.
set(_msh3_public_header "${msh3_SOURCE_DIR}/msh3.h")
file(READ "${_msh3_public_header}" _msh3_public_source)
if(NOT "${_msh3_public_source}" MATCHES
       "MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED")
  string(REPLACE
    "MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION  = 0x00000004,"
    "MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION  = 0x00000004,\n    MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED   = 0x00000008,"
    _msh3_public_source
    "${_msh3_public_source}")
  string(REPLACE
    "MSH3_CONNECTION_EVENT_NEW_REQUEST                       = 4,"
    "MSH3_CONNECTION_EVENT_NEW_REQUEST                       = 4,\n    MSH3_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED          = 5,"
    _msh3_public_source
    "${_msh3_public_source}")
  string(REPLACE
    "        struct {\n            MSH3_REQUEST* Request;\n        } NEW_REQUEST;"
    "        struct {\n            MSH3_REQUEST* Request;\n        } NEW_REQUEST;\n        struct {\n            MSH3_CERTIFICATE_CONTEXT* Certificate;\n            void* Chain;\n        } PEER_CERTIFICATE_RECEIVED;"
    _msh3_public_source
    "${_msh3_public_source}")
  if(NOT "${_msh3_public_source}" MATCHES
         "MSH3_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED" OR
     NOT "${_msh3_public_source}" MATCHES
         "PEER_CERTIFICATE_RECEIVED;")
    message(FATAL_ERROR
      "The pinned msh3 public certificate-callback layout changed")
  endif()
  file(WRITE "${_msh3_public_header}" "${_msh3_public_source}")
endif()

set(_msh3_implementation "${msh3_SOURCE_DIR}/lib/msh3.cpp")
file(READ "${_msh3_implementation}" _msh3_implementation_source)
if(NOT "${_msh3_implementation_source}" MATCHES
       "MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED")
  string(REPLACE
    "    if (Flags & MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION) {\n        QuicFlags |= QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;\n    }"
    "    if (Flags & MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION) {\n        QuicFlags |= QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;\n    }\n    if (Flags & MSH3_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED) {\n        QuicFlags |= QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;\n    }"
    _msh3_implementation_source
    "${_msh3_implementation_source}")
  string(REPLACE
    "    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:"
    "    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:\n        h3Event.Type = MSH3_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED;\n        h3Event.PEER_CERTIFICATE_RECEIVED.Certificate =\n            (MSH3_CERTIFICATE_CONTEXT*)Event->PEER_CERTIFICATE_RECEIVED.Certificate;\n        h3Event.PEER_CERTIFICATE_RECEIVED.Chain =\n            Event->PEER_CERTIFICATE_RECEIVED.Chain;\n        return Callbacks((MSH3_CONNECTION*)this, Context, &h3Event);\n    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:"
    _msh3_implementation_source
    "${_msh3_implementation_source}")
  if(NOT "${_msh3_implementation_source}" MATCHES
         "QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED" OR
     NOT "${_msh3_implementation_source}" MATCHES
         "QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED")
    message(FATAL_ERROR
      "The pinned msh3 implementation certificate-callback layout changed")
  endif()
  file(WRITE
    "${_msh3_implementation}" "${_msh3_implementation_source}")
endif()

set(_msh3_internal_header "${msh3_SOURCE_DIR}/lib/msh3_internal.hpp")
file(READ "${_msh3_internal_header}" _msh3_internal_source)
if("${_msh3_internal_source}" MATCHES "uint8_t HeadersBuffer\\[256\\];")
  string(REPLACE
    "uint8_t HeadersBuffer[256];"
    "uint8_t HeadersBuffer[64 * 1024];"
    _msh3_internal_source
    "${_msh3_internal_source}")
  file(WRITE "${_msh3_internal_header}" "${_msh3_internal_source}")
elseif(NOT "${_msh3_internal_source}" MATCHES
       "uint8_t HeadersBuffer\\[64 \\* 1024\\];")
  message(FATAL_ERROR
    "The pinned msh3 request-header buffer layout changed")
endif()
if("${_msh3_internal_source}" MATCHES "char DecodeBuffer\\[4096\\];")
  string(REPLACE
    "char DecodeBuffer[4096];"
    "char DecodeBuffer[32 * 1024];"
    _msh3_internal_source
    "${_msh3_internal_source}")
  file(WRITE "${_msh3_internal_header}" "${_msh3_internal_source}")
elseif(NOT "${_msh3_internal_source}" MATCHES
       "char DecodeBuffer\\[32 \\* 1024\\];")
  message(FATAL_ERROR
    "The pinned msh3 decoded-header buffer layout changed")
endif()
