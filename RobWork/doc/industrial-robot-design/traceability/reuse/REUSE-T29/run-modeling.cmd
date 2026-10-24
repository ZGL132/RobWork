@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T29/run-modeling.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label modeling -Filter MdlBasePlacementEdit.*:MdlRobotDesign.IMdl7*:MdlTemplate.CreateDraft*:MdlTemplate.SixAxisDefaults*
exit /b %errorlevel%
