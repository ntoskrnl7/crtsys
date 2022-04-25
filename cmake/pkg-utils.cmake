if (CMAKE_VERSION VERSION_GREATER 3.10 OR CMAKE_VERSION VERSION_EQUAL 3.10)
    include_guard()
endif()

function(pkg_get_version prefix header_file version_arg)
file(STRINGS ${header_file} version_defines
    REGEX "#define ${prefix}_VERSION_(MAJOR|MINOR|PATCH)")
foreach(ver ${version_defines})
    if(ver MATCHES "#define ${prefix}_VERSION_(MAJOR|MINOR|PATCH) +([^ ]+)$")
        set(VERSION_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

if (CMAKE_VERSION VERSION_GREATER 3.15 OR CMAKE_VERSION VERSION_EQUAL 3.15)
    message(DEBUG "${prefix} version ${VERSION}")
else()
    message(STATUS "${prefix} version ${VERSION}")
endif()

set(${version_arg} ${VERSION} PARENT_SCOPE)
endfunction()