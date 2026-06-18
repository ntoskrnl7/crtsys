@ECHO OFF

SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

IF "%~1"=="" GOTO :usage

SET "WORK_PATH=%~1"

IF NOT EXIST "%WORK_PATH%\CMakeLists.txt" (
    ECHO "%WORK_PATH%\CMakeLists.txt not exist"
    EXIT /B 1
)

IF "%~2"=="" GOTO :build_default
IF /I "%~2"=="Debug" GOTO :build_single
IF /I "%~2"=="Release" GOTO :build_single

GOTO :usage

:build_default
CALL :build_config Debug
IF ERRORLEVEL 1 EXIT /B !ERRORLEVEL!

CALL :build_config Release
EXIT /B !ERRORLEVEL!

:build_single
CALL :build_config %~2
EXIT /B !ERRORLEVEL!

:build_config
SET "CONFIG=%~1"

FOR %%A IN (x64 x86 ARM64 ARM) DO (
    FOR %%V IN (2022 2019 2017) DO (
        CALL :build_one %%V %%A !CONFIG!
        IF ERRORLEVEL 1 EXIT /B !ERRORLEVEL!
    )
)

EXIT /B 0

:build_one
SET "VS_VERSION=%~1"
SET "ARCH=%~2"
SET "CONFIG=%~3"

ECHO.
ECHO ===== build.bat "%WORK_PATH%" %VS_VERSION% %ARCH% %CONFIG% =====
CALL "%~dp0build.bat" "%WORK_PATH%" "%VS_VERSION%" "%ARCH%" "%CONFIG%"
IF ERRORLEVEL 1 (
    ECHO Failed: %VS_VERSION% %ARCH% %CONFIG%
    EXIT /B !ERRORLEVEL!
)

EXIT /B 0

:usage
ECHO Usage: %~nx0 ^<path-with-CMakeLists.txt^> [Debug^|Release]
EXIT /B 1
