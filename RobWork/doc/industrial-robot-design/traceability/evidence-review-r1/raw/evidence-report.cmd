@call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
@cmake --build build --config Release --target sdurws_ird_evidence_test_report sdurws_ird_evidence_contract_test_report > build/evidence-fix-r1/evidence-report-build.log 2>&1
@exit /b %errorlevel%
