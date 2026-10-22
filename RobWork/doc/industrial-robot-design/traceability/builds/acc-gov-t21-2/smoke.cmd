@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\Users\zgl18\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell;D:\software\CMake\bin;%PATH%"
cd /d D:\10_Source_Repos\21_robot\acc-gov-t21-r2-test
"D:\software\CMake\bin\cmake.exe" -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B smoke -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
if errorlevel 1 exit /b %errorlevel%
"D:\software\CMake\bin\cmake.exe" --build smoke --config Release --target ird_gates
