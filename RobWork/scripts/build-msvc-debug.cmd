@echo off
setlocal

set "VSDEVCMD=D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
set "BUILD_DIR=%~dp0..\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug"
set "TARGET=sdurws_structureoptimizer_test"

if not "%~1"=="" set "TARGET=%~1"

if not exist "%VSDEVCMD%" (
    echo Visual Studio developer environment was not found: %VSDEVCMD%
    exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --target "%TARGET%" --config Debug
exit /b %errorlevel%
