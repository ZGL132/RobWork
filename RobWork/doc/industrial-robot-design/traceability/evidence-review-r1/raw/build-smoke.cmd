@call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
@cmake --build build/core-smoke --config Release --target sdurws_ird_evidence_test sdurws_ird_evidence_contract_test > build/evidence-fix-r1/build-smoke.log 2>&1
@exit /b %errorlevel%
