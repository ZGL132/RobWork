@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
"D:\software\CMake\bin\cmake.exe" --build build-smoke --config Release --target ird_gates
exit /b %errorlevel%
