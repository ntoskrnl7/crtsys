@ECHO OFF

SET CURDIR=%~DP0

IF /I "%PROCESSOR_ARCHITECTURE%" == "x86" (
    SET ARCH_NAME=x86
)
IF /I "%PROCESSOR_ARCHITECTURE%" == "x64" (
    SET ARCH_NAME=x64
    CALL %CURDIR%..\build.bat %CURDIR%\app x86
)
IF /I "%PROCESSOR_ARCHITECTURE%" == "Win32" (
    SET ARCH_NAME=x86
)
IF /I "%PROCESSOR_ARCHITECTURE%" == "Win64" (
    SET ARCH_NAME=x64
    CALL %CURDIR%..\build.bat %CURDIR%\app x86
)
IF /I "%PROCESSOR_ARCHITECTURE%" == "AMD64" (
    SET ARCH_NAME=x64
    CALL %CURDIR%..\build.bat %CURDIR%\app x86
)
IF /I "%PROCESSOR_ARCHITECTURE%" == "ARM" (
    SET ARCH_NAME=ARM
)
IF /I "%PROCESSOR_ARCHITECTURE%" == "ARM64" (
    SET ARCH_NAME=ARM64
    CALL %CURDIR%..\build.bat %CURDIR%\app ARM
)

CALL %CURDIR%..\build.bat %CURDIR%\app %ARCH_NAME%
CALL %CURDIR%..\build.bat %CURDIR%\driver %ARCH_NAME%

@ECHO ON