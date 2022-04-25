if(MSVC)
    add_compile_options("/Zc:__cplusplus")
endif()

macro(set_std_cxx_latest target)
    if(MSVC)
        check_std_cxx(latest _cxx_latest_flag_supported)
        if(_cxx_latest_flag_supported)
            set(_version latest)
        else()
            set(_version 17)
        endif()

    else()
        check_std_cxx(2a _cxx_2a_flag_supported)
        if(_cxx_2a_flag_supported)
            set(_version 2a)
        else()
            set(_version 17)
        endif()
    endif()

    if(MSVC)
        set(_std_cxx_option "/std:c++${_version}")
    else()
        set(_std_cxx_option "-std=c++${_version}")
    endif()

    target_compile_options(${target} PRIVATE ${_std_cxx_option})
endmacro()

macro(set_std_cxx) #optional argumanet version
    set (extra_macro_args ${ARGN})
    list(LENGTH extra_macro_args num_extra_args)
    if(${num_extra_args} GREATER 0)
        list(GET extra_macro_args 0 optional_arg)
        set(version ${optional_arg})
    endif()
    
    if (version)
        if(MSVC)
            set(_std_cxx_option "/std:c++${version}")
        else()
            set(_std_cxx_option "-std=c++${version}")
        endif()
        
        check_std_cxx(${version} _std_cpp_flag_supported)
        if (_std_cpp_flag_supported)  
            add_compile_options(${_std_cxx_option})
        else()
            message(AUTHOR_WARNING "The compiler ${CMAKE_CXX_COMPILER} has no C++${version} support")
            set_std_cxx_latest()
        endif()
    else()
        set_std_cxx_latest()
    endif()
endmacro()

set(std_cpp_check_code "
#include<stdio.h>
int main() {
    printf(\"%lu\", __cplusplus);
    return 0;
}")

function(check_std_cxx version output_variable)
    enable_language(CXX)
    
    file(WRITE ${CMAKE_BINARY_DIR}/std_cpp_check.cpp "${std_cpp_check_code}")

    get_directory_property(_compile_options COMPILE_OPTIONS)
    get_directory_property(_compile_definations COMPILE_DEFINITIONS)
    
    if(MSVC)
        set(_compile_std_cxx_option "/std:c++${version}")
    else()
        set(_compile_std_cxx_option "-std=c++${version}")
    endif()

    try_run(SHOULD_PASS COMPILE_RESULT
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/std_cpp_check.cpp
        COMPILE_DEFINITIONS ${_compile_std_cxx_option} ${_compile_options} ${_compile_definations}
        COMPILE_OUTPUT_VARIABLE TRY_OUT
        RUN_OUTPUT_VARIABLE __cplusplus
    )
    if(COMPILE_RESULT)
        if(__cplusplus GREATER 201703) #Clang 201707L, gcc 8.2 201709L, MSVC stdc++:lastet 201704L
            set(_std_cxx_version 20)
        elseif(__cplusplus EQUAL 201703)
            set(_std_cxx_version 17)
        elseif(__cplusplus EQUAL 201402)
            set(_std_cxx_version 14)
        elseif(__cplusplus EQUAL 201103)
            set(_std_cxx_version 11)
        elseif(__cplusplus EQUAL 199711)
            set(_std_cxx_version 98)
        else()
            set(_std_cxx_version "pre-standard(${__cplusplus})")
        endif()

        message("${version} == ${_std_cxx_version}")
        if(MSVC)
            if(version STREQUAL latest)
                set(${output_variable} TRUE PARENT_SCOPE)
            endif()
        else()
            if(version STREQUAL 2a AND _std_cxx_version EQUAL 20)
                set(${output_variable} TRUE PARENT_SCOPE)
            endif()
        endif()
        if(version EQUAL _std_cxx_version)
            set(${output_variable} TRUE PARENT_SCOPE)
        endif()
    else()
        message(ERROR "should pass failed ${TRY_OUT}")
    endif()
endfunction(check_std_cxx)

function(get_std_cxx output_variable)
    enable_language(CXX)
    
    file(WRITE ${CMAKE_BINARY_DIR}/std_cpp_check.cpp "${std_cpp_check_code}")

    if("${CMAKE_CXX_STANDARD}")
        set(FLAGS_OPT "CMAKE_FLAGS")
        set(FLAGS_VAL "-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}")
    endif()

    get_directory_property(_compile_options COMPILE_OPTIONS)
    get_directory_property(_compile_definations COMPILE_DEFINITIONS)

    try_run(SHOULD_PASS COMPILE_RESULT
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/std_cpp_check.cpp
        COMPILE_DEFINITIONS ${_compile_options} ${_compile_definations}
        COMPILE_OUTPUT_VARIABLE TRY_OUT
        RUN_OUTPUT_VARIABLE __cplusplus
        ${FLAGS_OPT} ${FLAGS_VAL}
        )

    if(NOT COMPILE_RESULT)
        message(ERROR "should pass failed ${TRY_OUT}")
    endif()
    
    if(__cplusplus GREATER 201703) #Clang 201707L, gcc 8.2 201709L, MSVC stdc++:lastet 201704L
        set(_STD_CPP_VERSION 20)
    elseif(__cplusplus EQUAL 201703)
        set(_STD_CPP_VERSION 17)
    elseif(__cplusplus EQUAL 201402)
        set(_STD_CPP_VERSION 14)
    elseif(__cplusplus EQUAL 201103)
        set(_STD_CPP_VERSION 11)
    elseif(__cplusplus EQUAL 199711)
        set(_STD_CPP_VERSION 98)
    else()
        set(_STD_CPP_VERSION "pre-standard(${__cplusplus})")
    endif()
    set(${output_variable} ${_STD_CPP_VERSION} PARENT_SCOPE)
endfunction()