@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T29ROOT=D:\10_Source_Repos\21_robot\RobWork
set T29E=%T29ROOT%\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T29
set T29P=%T29ROOT%\RobWork\RobWorkStudio\src\rwslibs\industrialrobot
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /DEIGEN_DONT_ALIGN_STATICALLY=1 /I%T29P%\modeling\include /I%T29P%\core\include /I%T29P%\policy\include /I%T29P%\runtime\include /I%T29P%\evidence\include /I%T29P%\execution\include /I%T29P%\diagnostics\include /I%T29P%\io\include /I%T29ROOT%\RobWork\RobWork\src /I%T29ROOT%\vcpkg\installed\x64-windows\include /I%T29ROOT%\vcpkg\installed\x64-windows\include\eigen3 %T29E%\perf.cpp %T29E%\perf-old.cpp %T29E%\perf-new.cpp /Fo%T29ROOT%\build\ /Fe%T29ROOT%\build\t29-perf.exe /link %T29ROOT%\build\RobWork\libs\Release\sdurw_math.lib %T29ROOT%\build\RobWorkStudio\libs\Release\sdurws_ird_runtime.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%T29ROOT%\build\RobWork\bin\Release;%T29ROOT%\vcpkg\installed\x64-windows\bin;D:\software\Miniconda3;%PATH%
%T29ROOT%\build\t29-perf.exe
exit /b %errorlevel%
