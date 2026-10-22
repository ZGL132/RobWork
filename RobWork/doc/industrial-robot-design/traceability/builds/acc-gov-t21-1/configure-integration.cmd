@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
"D:\software\CMake\bin\cmake.exe" -S RobWork -B build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
exit /b %errorlevel%
