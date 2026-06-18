@REM build.bat "PATH" "x86 | x64 | ARM | ARM64" [Debug (default) | Release]
@REM build.bat "PATH" "2017 | 2019 | 2022" "x86 | x64 | ARM | ARM64" [Debug (default) | Release]

@ECHO OFF

SETLOCAL ENABLEEXTENSIONS

IF "%~1"=="" GOTO :usage

SET "WORK_PATH=%~1"
SET "ARG2=%~2"
SET "ARG3=%~3"
SET "ARG4=%~4"
SET "VS_VERSION="
SET "GENERATOR="
SET "ARCH="
SET "ARCH_NAME="
SET "CONFIG="

IF "%ARG2%"=="" GOTO :usage

IF NOT EXIST "%WORK_PATH%\CMakeLists.txt" (
    ECHO "%WORK_PATH%\CMakeLists.txt not exist"
    EXIT /B 1
)

CALL :set_arch "%ARG2%"
IF DEFINED ARCH (
    SET "CONFIG=%ARG3%"
    SET "BUILD_PATH=%WORK_PATH%\build_%ARCH_NAME%"
    GOTO :configure
)

CALL :set_vs "%ARG2%"
IF NOT DEFINED VS_VERSION (
    ECHO Unsupported Visual Studio or architecture: %ARG2%
    GOTO :usage
)

CALL :set_arch "%ARG3%"
IF NOT DEFINED ARCH (
    ECHO Unsupported architecture: %ARG3%
    GOTO :usage
)

SET "CONFIG=%ARG4%"
SET "BUILD_PATH=%WORK_PATH%\build_%VS_VERSION%_%ARCH_NAME%"
GOTO :configure

:configure
IF "%CONFIG%"=="" SET "CONFIG=Debug"
IF /I "%CONFIG%"=="Debug" SET "CONFIG=Debug"
IF /I "%CONFIG%"=="Release" SET "CONFIG=Release"
IF /I NOT "%CONFIG%"=="Debug" IF /I NOT "%CONFIG%"=="Release" (
    ECHO Unsupported configuration: %CONFIG%
    GOTO :usage
)

IF DEFINED GENERATOR (
    ECHO cmake -S "%WORK_PATH%" -B "%BUILD_PATH%" -A %ARCH% -DCMAKE_CXX_FLAGS=/MP -G "%GENERATOR%"
    cmake -S "%WORK_PATH%" -B "%BUILD_PATH%" -A %ARCH% -DCMAKE_CXX_FLAGS=/MP -G "%GENERATOR%"
) ELSE (
    ECHO cmake -S "%WORK_PATH%" -B "%BUILD_PATH%" -A %ARCH% -DCMAKE_CXX_FLAGS=/MP
    cmake -S "%WORK_PATH%" -B "%BUILD_PATH%" -A %ARCH% -DCMAKE_CXX_FLAGS=/MP
)
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

ECHO cmake --build "%BUILD_PATH%" --config %CONFIG%
cmake --build "%BUILD_PATH%" --config %CONFIG%
EXIT /B %ERRORLEVEL%

:set_arch
SET "ARCH="
SET "ARCH_NAME="

IF /I "%~1"=="x86" (
    SET "ARCH=Win32"
    SET "ARCH_NAME=x86"
    EXIT /B 0
)
IF /I "%~1"=="Win32" (
    SET "ARCH=Win32"
    SET "ARCH_NAME=x86"
    EXIT /B 0
)
IF /I "%~1"=="x64" (
    SET "ARCH=x64"
    SET "ARCH_NAME=x64"
    EXIT /B 0
)
IF /I "%~1"=="Win64" (
    SET "ARCH=x64"
    SET "ARCH_NAME=x64"
    EXIT /B 0
)
IF /I "%~1"=="ARM" (
    SET "ARCH=ARM"
    SET "ARCH_NAME=ARM"
    EXIT /B 0
)
IF /I "%~1"=="ARM64" (
    SET "ARCH=ARM64"
    SET "ARCH_NAME=ARM64"
    EXIT /B 0
)

EXIT /B 1

:set_vs
SET "VS_VERSION="
SET "GENERATOR="

IF "%~1"=="2017" (
    SET "VS_VERSION=2017"
    SET "GENERATOR=Visual Studio 15 2017"
    EXIT /B 0
)
IF "%~1"=="2019" (
    SET "VS_VERSION=2019"
    SET "GENERATOR=Visual Studio 16 2019"
    EXIT /B 0
)
IF "%~1"=="2022" (
    SET "VS_VERSION=2022"
    SET "GENERATOR=Visual Studio 17 2022"
    EXIT /B 0
)

EXIT /B 1

:usage
ECHO Usage:
ECHO   %~nx0 ^<path-with-CMakeLists.txt^> ^<x86^|x64^|ARM^|ARM64^> [Debug^|Release]
ECHO   %~nx0 ^<path-with-CMakeLists.txt^> ^<2017^|2019^|2022^> ^<x86^|x64^|ARM^|ARM64^> [Debug^|Release]
EXIT /B 1
