@ECHO OFF

SETLOCAL ENABLEDELAYEDEXPANSION

IF EXIST "%1/CMakeLists.txt" (
    IF "%2" == "" (
        (
            START cmake -S %1 cmake -B %1/build_2017_x64 -A x64 -G "Visual Studio 15 2017"
            START cmake -S %1 cmake -B %1/build_2017_x86 -A Win32 -G "Visual Studio 15 2017"
            START cmake -S %1 cmake -B %1/build_2019_x64 -A x64 -G "Visual Studio 16 2019"
            START cmake -S %1 cmake -B %1/build_2019_x86 -A Win32 -G "Visual Studio 16 2019"
            START cmake -S %1 cmake -B %1/build_2022_x64 -A x64 -G "Visual Studio 17 2022"
            START cmake -S %1 cmake -B %1/build_2022_x86 -A Win32 -G "Visual Studio 17 2022"
        ) | PAUSE
        START build_all.bat %1 Debug
        START build_all.bat %1 Release
    )
    IF /I "%2" == "Release" (
        START CMD /C "build.bat %1 2022 x64 Release && build.bat %1 2019 x64 Release && build.bat %1 2017 x64 Release"
        START CMD /C "build.bat %1 2022 x86 Release && build.bat %1 2019 x86 Release && build.bat %1 2017 x86 Release"
    )
    IF /I "%2" == "Debug" (
        START "2022 x64 Debug" build.bat %1 2022 x64 Debug
        START "2022 x86 Debug" build.bat %1 2022 x86 Debug
        START "2019 x64 Debug" build.bat %1 2019 x64 Debug
        START "2019 x86 Debug" build.bat %1 2019 x86 Debug
        START "2017 x64 Debug" build.bat %1 2017 x64 Debug
        START "2017 x86 Debug" build.bat %1 2017 x86 Debug
    )
) ELSE (
    ECHO "%1/CMakeLists.txt not exist"
)

ENDLOCAL