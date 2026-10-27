@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T28ROOT=D:\10_Source_Repos\21_robot\RobWork
set T28E=%T28ROOT%\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T28
set T28P=%T28ROOT%\RobWork\RobWorkStudio\src\rwslibs\industrialrobot
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /I%T28P%\modeling\include /I%T28P%\core\include /I%T28P%\policy\include /I%T28P%\runtime\include /I%T28P%\evidence\include /I%T28P%\execution\include /I%T28P%\diagnostics\include /I%T28P%\io\include /I%T28ROOT%\RobWork\RobWork\src /I%T28ROOT%\vcpkg\installed\x64-windows\include /I%T28ROOT%\vcpkg\installed\x64-windows\include\eigen3 %T28E%\perf.cpp %T28E%\perf-old.cpp %T28E%\perf-new.cpp %T28E%\perf-old-check.cpp %T28E%\perf-new-check.cpp /Fo%T28ROOT%\build\ /Fe%T28ROOT%\build\t28-perf.exe /link %T28ROOT%\build\RobWork\libs\Release\sdurw_math.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%T28ROOT%\build\RobWork\bin\Release;%T28ROOT%\vcpkg\installed\x64-windows\bin;D:\software\Miniconda3;%PATH%
%T28ROOT%\build\t28-perf.exe
exit /b %errorlevel%
