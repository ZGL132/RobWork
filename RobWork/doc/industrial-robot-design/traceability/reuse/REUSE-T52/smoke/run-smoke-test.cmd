@echo off
set PATH=D:\10_Source_Repos\21_robot\RobWork\build\RobWorkSim\bin\Release;D:\10_Source_Repos\21_robot\RobWork\build\RobWork\bin\Release;D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;%PATH%
cd /d D:\10_Source_Repos\21_robot\RobWork\build\wp07-smoke\dynamics\Release
sdurws_ird_dynamics_test.exe --gtest_output=xml:D:/10_Source_Repos/21_robot/RobWork/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/smoke/dynamics-test-smoke.xml > D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\reuse\REUSE-T52\smoke\dynamics-test-smoke.log 2>&1
echo EXITCODE=%ERRORLEVEL%
