@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T27/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label final -Filter MdlInertiaReuse.*:MdlPropertyEstimation.*:MdlRobotDesign.IMdl5*:MdlRobotDesign.IMdl3*:MdlParts.ToolBodyAssertions*:MdlImport.*:MdlReadiness.*:MdlCommandPrepare.HardAssert*:LinAlgeTest.*:ModelingManualBody.*:MdlCanonicalBridge.LinksMappingBodyAndGeometry*
exit /b %errorlevel%
