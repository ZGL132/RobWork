@echo off
rem 本机主线手工验收入口，PowerShell脚本会先核对分支、源码及程序摘要。
pwsh.exe -NoProfile -File "%~dp0Start-Latest.ps1"
if errorlevel 1 pause
