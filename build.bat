@ECHO OFF

CD test

SETLOCAL ENABLEDELAYEDEXPANSION

IF "%1" == "Win32" (
    SET ARCH=Win32
    SET ARCH_NAME=x86
)
IF "%1" == "x86" (
    SET ARCH=Win32
    SET ARCH_NAME=x86
)
IF "%1" == "Win64" (
    SET ARCH=x64
    SET ARCH_NAME=x64
)
IF "%1" == "x64" (
    SET ARCH=x64
    SET ARCH_NAME=x64
)

if "%2" == "" (
:DEFAULT_GENERATOR 
    ECHO cmake -S . -B build_!ARCH_NAME! -A !ARCH! -DCMAKE_CXX_FLAGS=/MP
    cmake -S . -B build_!ARCH_NAME! -A !ARCH! -DCMAKE_CXX_FLAGS=/MP
    ECHO cmake --build build_!ARCH_NAME!
    cmake --build build_!ARCH_NAME!
) ELSE (
    IF "%2" == "Win32" (
        SET ARCH=Win32
        SET ARCH_NAME=x86
    )
    IF "%2" == "x86" (
        SET ARCH=Win32
        SET ARCH_NAME=x86
    )
    IF "%2" == "Win64" (
        SET ARCH=x64
        SET ARCH_NAME=x64
    )
    IF "%2" == "x64" (
        SET ARCH=x64
        SET ARCH_NAME=x64
    )
    SET GENERATOR=
    IF %1 == 2017 (
        SET GENERATOR="Visual Studio 15 2017"
    )
    IF %1 == 2019 (
        SET GENERATOR="Visual Studio 16 2019"
    )
    IF %1 == 2022 (
        SET GENERATOR="Visual Studio 17 2022"
    )
    if "!GENERATOR!" == "" (
        ECHO Unknown Target [%1]
        GOTO DEFAULT_GENERATOR
    )
    ECHO cmake -S . -B build_%1_!ARCH_NAME! -A !ARCH! -DCMAKE_CXX_FLAGS=/MP -G !GENERATOR!
    cmake -S . -B build_%1_!ARCH_NAME! -A !ARCH! -DCMAKE_CXX_FLAGS=/MP -G !GENERATOR!
    ECHO cmake --build build_%1_!ARCH_NAME!
    cmake --build build_%1_!ARCH_NAME!
)

ENDLOCAL
CD ..