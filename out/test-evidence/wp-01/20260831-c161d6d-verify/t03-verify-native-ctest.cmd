@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"
