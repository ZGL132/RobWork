@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /std:c++17 /O2 /EHsc /utf-8 /nologo /I "D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\core\include" /I "D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\include" "D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T21\production-probe.cpp" "D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\core\src\Compare.cpp" /Fe:c4-production-probe.exe
exit /b %errorlevel%