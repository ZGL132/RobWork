@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T32ROOT=D:\10_Source_Repos\21_robot\RobWork
set T32E=%T32ROOT%\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T32
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /I%T32ROOT%\RobWork\RobWork\src /I%T32ROOT%\vcpkg\installed\x64-windows\include /I%T32ROOT%\vcpkg\installed\x64-windows\include\eigen3 %T32E%\perf.cpp /Fo%T32ROOT%\build\ /Fe%T32ROOT%\build\t32-perf.exe /link %T32ROOT%\build\RobWork\libs\Release\sdurw_math.lib %T32ROOT%\build\RobWork\libs\Release\sdurw_pathplanning.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%T32ROOT%\build\RobWork\bin\Release;%T32ROOT%\vcpkg\installed\x64-windows\bin;D:\software\Miniconda3;%PATH%
%T32ROOT%\build\t32-perf.exe
exit /b %errorlevel%
