@echo off
rem REUSE-T55 swap-equivalence probe build (same pattern as REUSE-T42 probe-build.cmd):
rem cl direct compile + link integration build tree Release sdurw_math.lib.
rem Output exe/obj/log under this run/ directory (log written by caller).
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
cl /nologo /std:c++17 /EHsc /utf-8 /O2 /MD /DNDEBUG /I RobWork\RobWork\src /I vcpkg\installed\x64-windows\include\eigen3 "RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T55\probe-swap-equivalence.cpp" /Fe:RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T55\run\probe-swap-equivalence.exe /Fo:RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T55\run\probe-swap.obj /link build\RobWork\libs\Release\sdurw_math.lib
