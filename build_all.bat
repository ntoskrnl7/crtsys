@ECHO OFF

SETLOCAL ENABLEDELAYEDEXPANSION

IF EXIST "%1/CMakeLists.txt" (
    IF "%2" == "" (
        START "2020 x64" CMD /C "build.bat %1 2022 x64 && build.bat %1 2022 x64 Release"
        START "2020 x86" CMD /C "build.bat %1 2022 x86 && build.bat %1 2022 x86 Release"
        START "2019 x64" CMD /C "build.bat %1 2019 x64 && build.bat %1 2019 x64 Release"
        START "2019 x86" CMD /C "build.bat %1 2019 x86 && build.bat %1 2019 x86 Release"
        START "2017 x64" CMD /C "build.bat %1 2017 x64 && build.bat %1 2017 x64 Release"
        START "2017 x86" CMD /C "build.bat %1 2017 x86 && build.bat %1 2017 x86 Release"
    ) ELSE (
        START "2020 x64" build.bat %1 2022 x64 %2
        START "2020 x86" build.bat %1 2022 x86 %2
        START "2019 x64" build.bat %1 2019 x64 %2
        START "2019 x86" build.bat %1 2019 x86 %2
        START "2017 x64" build.bat %1 2017 x64 %2
        START "2017 x86" build.bat %1 2017 x86 %2
    )
) ELSE (
    ECHO "%1/CMakeLists.txt not exist"
)

ENDLOCAL