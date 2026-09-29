@echo off
rem =====================================================================
rem 开发辅助启动脚本（WP-24-T09 修订：启动目标切换为正式产品主程序）
rem ---------------------------------------------------------------------
rem 产品交付口径（B1-SPEC §5.4 / ARCH §5.4）：正式产品主程序为
rem   sdurws_ird_studio（宿主融合形态唯一装配路径，WP-24-T08 落位），
rem   交付形态＝staging 安装树（本任务 acceptance 2 专项审计留痕
rem   traceability/builds/wp24-t09/）。
rem 方案 A 时代本脚本启动的框架 RobWorkStudio.exe 已随"方案 A 正式产品
rem   路径"退役（WP-24-T09 处置登记）：框架程序仅保留为框架开发交付物，
rem   不再是产品入口。
rem 本脚本属【开发辅助定位】（DTB §5.1 口径复查登记）：路径硬编码开发机
rem   构建树，其运行不构成"无开发机路径依赖"证据——产品交付验证以
rem   staging 安装树绝对路径直启为准（契约 acceptance 2②）。
rem =====================================================================
set "ROOT=D:\10_Source_Repos\21_robot\RobWork"
set "PATH=%ROOT%\vcpkg\installed\x64-windows\bin;%ROOT%\build\RobWork\bin\Release;D:\software\Qt\6.11.1\msvc2022_64\bin;%PATH%"
set "WORKCELL=%ROOT%\RobWork\RobWork\gtest\testfiles\workcells\simple_wc\SimpleWorkcell.wc.xml"
start "" "%ROOT%\build\RobWorkStudio\bin\Release\sdurws_ird_studio.exe" %*
