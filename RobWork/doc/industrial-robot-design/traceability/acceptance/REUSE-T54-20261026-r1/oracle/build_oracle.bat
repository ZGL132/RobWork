@echo off
rem REUSE-T54 independent acceptance oracle build driver (one-off, artifacts stay here)
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (echo VCVARS-FAIL & exit /b 2)
cd /d D:\10_Source_Repos\21_robot\RobWork
set ACC=RobWork\doc\industrial-robot-design\traceability\acceptance\REUSE-T54-20261026-r1\oracle
cl /nologo /EHsc /std:c++17 /utf-8 /MD ^
  /I RobWork\RobWorkStudio\src\rwslibs\industrialrobot\project\src ^
  /I RobWork\RobWorkStudio\src\rwslibs\industrialrobot\project\include ^
  /I RobWork\RobWorkStudio\src\rwslibs\industrialrobot\core\include ^
  /I vcpkg\installed\x64-windows\include ^
  "%ACC%\acc_oracle.cpp" ^
  /Fe:"%ACC%\acc_oracle.exe" /Fo:"%ACC%\\" ^
  /link build\RobWorkStudio\libs\Release\sdurws_ird_project.lib build\RobWorkStudio\libs\Release\sdurws_ird_core.lib vcpkg\installed\x64-windows\lib\boost_json-vc143-mt-x64-1_92.lib
if errorlevel 1 (echo CL-FAIL & exit /b 3)
echo CL-OK
exit /b 0
