@call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
@cmake --build build --config Release --target sdurws_ird_optimization_test sdurws_ird_optimization_perf_test > build/evidence-fix-r1/build-opt-final.log 2>&1
@exit /b %errorlevel%
