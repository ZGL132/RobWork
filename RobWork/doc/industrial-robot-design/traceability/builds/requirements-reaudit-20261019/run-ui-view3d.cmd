@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
set QT_QPA_PLATFORM=windows
set QT_QPA_PLATFORM_PLUGIN_PATH=D:\software\Qt\6.11.1\msvc2022_64\plugins\platforms
"C:\Users\zgl18\.codex\worktrees\486b\RobWork\build\RobWorkStudio\bin\Release\sdurws_ird_ui_gui_test.exe" --gtest_filter="WorkbenchShellGuiTest.CentralView3DPlaceholder_RegistrationAndRedLines_UX11_KIN06:HostView3DGateway.*" --gtest_output=xml:"C:\Users\zgl18\.codex\worktrees\486b\RobWork\RobWork\doc\industrial-robot-design\traceability\builds\requirements-reaudit-20261019\ui-view3d.xml" --gtest_color=no
