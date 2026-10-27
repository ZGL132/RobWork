@call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
@cmake --build build --config Release --target sdurws_ird_evidence_test sdurws_ird_evidence_contract_test ird_gates > build/evidence-fix-r1/final-build-gates.log 2>&1
@exit /b %errorlevel%
