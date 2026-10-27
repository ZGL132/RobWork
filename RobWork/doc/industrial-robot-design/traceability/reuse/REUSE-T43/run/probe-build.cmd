@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
cl /nologo /std:c++17 /EHsc /utf-8 /I RobWork\RobWork\src /I vcpkg\installed\x64-windows\include /I vcpkg\installed\x64-windows\include\eigen3 "RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T43\probe-eigen-upstream.cpp" /Fe:RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T43\run\probe-eigen-upstream.exe /Fo:RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T43\run\probe.obj /link build\RobWork\libs\Release\sdurw_math.lib vcpkg\installed\x64-windows\lib\gtest.lib
