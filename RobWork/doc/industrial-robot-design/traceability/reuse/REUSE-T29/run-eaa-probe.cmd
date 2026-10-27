@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /DEIGEN_DONT_ALIGN_STATICALLY=1 /IRobWork\RobWork\src /Ivcpkg\installed\x64-windows\include /Ivcpkg\installed\x64-windows\include\eigen3 RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T29\eaa-probe.cpp /Fobuild\t29-eaa.obj /Febuild\t29-eaa.exe /link build\RobWork\libs\Release\sdurw_math.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%CD%\build\RobWork\bin\Release;%CD%\vcpkg\installed\x64-windows\bin;D:\software\Miniconda3;%PATH%
build\t29-eaa.exe
exit /b %errorlevel%
