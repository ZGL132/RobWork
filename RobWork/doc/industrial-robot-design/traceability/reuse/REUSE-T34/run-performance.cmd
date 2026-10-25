@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set T34ROOT=D:\10_Source_Repos\21_robot\RobWork
set T34E=%T34ROOT%\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T34
set T34P=%T34ROOT%\RobWork\RobWorkStudio\src\rwslibs\industrialrobot
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /DNDEBUG /I%T34P%\runtime\include /I%T34P%\core\include /I%T34ROOT%\RobWork\RobWork\src /I%T34ROOT%\vcpkg\installed\x64-windows\include /I%T34ROOT%\vcpkg\installed\x64-windows\include\eigen3 %T34E%\perf.cpp /Fo%T34ROOT%\build\t34-perf.obj /Fe%T34ROOT%\build\t34-perf.exe /link %T34ROOT%\build\RobWork\libs\Release\sdurw_math.lib
if errorlevel 1 exit /b %errorlevel%
set PATH=%T34ROOT%\build\RobWork\bin\Release;%T34ROOT%\vcpkg\installed\x64-windows\bin;%PATH%
%T34ROOT%\build\t34-perf.exe
exit /b %errorlevel%
