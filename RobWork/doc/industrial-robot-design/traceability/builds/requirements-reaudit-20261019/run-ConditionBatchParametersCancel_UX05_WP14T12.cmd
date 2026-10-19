@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
set QT_QPA_PLATFORM=windows
set QT_QPA_PLATFORM_PLUGIN_PATH=D:\software\Qt\6.11.1\msvc2022_64\plugins\platforms
"C:\Users\zgl18\.codex\worktrees\486b\RobWork\build\RobWorkStudio\bin\Release\sdurws_ird_requirements_gui_test.exe" --gtest_filter=RequirementsSessionGuiTest.ConditionBatchParametersCancel_UX05_WP14T12 --gtest_color=no
