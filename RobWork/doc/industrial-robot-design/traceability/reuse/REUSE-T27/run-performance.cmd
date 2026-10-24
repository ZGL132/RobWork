@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T27ROOT=D:\10_Source_Repos\21_robot\RobWork
set T27E=%T27ROOT%\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T27
set T27P=%T27ROOT%\RobWork\RobWorkStudio\src\rwslibs\industrialrobot
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /I%T27P%\modeling\include /I%T27P%\core\include /I%T27P%\policy\include /I%T27P%\runtime\include /I%T27P%\evidence\include /I%T27P%\execution\include /I%T27P%\diagnostics\include /I%T27P%\io\include /I%T27ROOT%\RobWork\RobWork\src /I%T27ROOT%\vcpkg\installed\x64-windows\include /I%T27ROOT%\vcpkg\installed\x64-windows\include\eigen3 %T27E%\perf.cpp %T27E%\perf-old.cpp %T27E%\perf-new.cpp /Fo%T27ROOT%\build\ /Fe%T27ROOT%\build\t27-perf.exe /link %T27ROOT%\build\RobWork\libs\Release\sdurw_math.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%T27ROOT%\build\RobWork\bin\Release;%T27ROOT%\vcpkg\installed\x64-windows\bin;D:\software\Miniconda3;%PATH%
%T27ROOT%\build\t27-perf.exe
exit /b %errorlevel%
