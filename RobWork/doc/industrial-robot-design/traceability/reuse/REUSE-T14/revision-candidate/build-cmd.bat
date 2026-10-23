@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /std:c++17 /O2 /EHsc /utf-8 /nologo /I "D:\10_Source_Repos\21_robot\reuse-foundation\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\core\include" /I "D:\10_Source_Repos\21_robot\RobWork\vcpkg\installed\x64-windows\include" candidate-probe.cpp compare-candidate.cpp /Fe:c4-rev-probe.exe
