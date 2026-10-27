@echo off
rem CORE-T12：只生成隔离基准，不注册到产品CMake；VS x64环境。
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /DNDEBUG /MD /EHsc /std:c++17 /utf-8 /I"..\..\..\..\RobWorkStudio\src\rwslibs\industrialrobot\core\src" SeedInitializationBench.cpp /Fe:seed-bench.exe
