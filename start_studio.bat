@echo off
rem RobWorkStudio 启动脚本 (commit 31b8184 构建产物)
set "ROOT=D:\10_Source_Repos\21_robot\RobWork"
set "PATH=%ROOT%\vcpkg\installed\x64-windows\bin;%ROOT%\build\RobWork\bin\Release;D:\software\Qt\6.11.1\msvc2022_64\bin;%PATH%"
set "WORKCELL=%ROOT%\RobWork\RobWork\gtest\testfiles\workcells\simple_wc\SimpleWorkcell.wc.xml"
start "" "%ROOT%\build\RobWorkStudio\bin\Release\RobWorkStudio.exe" %*
