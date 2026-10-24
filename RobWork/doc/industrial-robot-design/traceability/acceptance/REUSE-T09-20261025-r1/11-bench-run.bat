@echo off
rem REUSE-T09 acceptance bench rerun (MSVC /O2, same toolchain as production)
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T09\benchmark"
set PATH=D:\10_Source_Repos\21_robot\RobWork\build\RobWork\bin\Release;D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\bin;%PATH%
cl /O2 /DNDEBUG /std:c++17 /utf-8 /EHsc /I "D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\src" /I "D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\include\eigen3" polynomial-replace-bench.cpp /Fe:acc-bench-rerun.exe /link /LIBPATH:"D:\10_Source_Repos\21_robot\RobWork\build\RobWork\libs\Release" sdurw_math.lib sdurw_core.lib
if errorlevel 1 exit /b 2
acc-bench-rerun.exe
exit /b %errorlevel%
