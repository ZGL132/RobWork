@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T26ROOT=D:\10_Source_Repos\21_robot\RobWork
set T26E=%T26ROOT%\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T26
set T26P=%T26ROOT%\RobWork\RobWorkStudio\src\rwslibs\industrialrobot
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /I%T26P%\trajectory\include /I%T26P%\core\include /I%T26P%\policy\include /I%T26P%\runtime\include /I%T26P%\evidence\include /I%T26P%\execution\include /I%T26P%\diagnostics\include /I%T26P%\io\include /I%T26ROOT%\RobWork\RobWork\src /I%T26ROOT%\vcpkg\installed\x64-windows\include /I%T26ROOT%\vcpkg\installed\x64-windows\include\eigen3 %T26E%\perf.cpp %T26E%\perf-old.cpp %T26E%\perf-new.cpp /Fo%T26ROOT%\build\ /Fe%T26ROOT%\build\t26-perf.exe /link %T26ROOT%\build\RobWork\libs\Release\sdurw_math.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%T26ROOT%\build\RobWork\bin\Release;%T26ROOT%\vcpkg\installed\x64-windows\bin;D:\software\Miniconda3;%PATH%
%T26ROOT%\build\t26-perf.exe
exit /b %errorlevel%
