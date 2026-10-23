# B31 只读重现

在VS x64开发环境、仓库根执行；`RobWork_DIR`、`RWSIM_TARGETS_DIR`、`CMAKE_PREFIX_PATH`使用[B30的真实供给参数](../batch30/README.md)。本批仅使用math/core，无GUI或新依赖安装。

```powershell
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch31 -B .cache/b31-build -G 'Visual Studio 17 2022' -A x64 '-DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake' '-DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets' '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64'
cmake --build .cache/b31-build --config Release --target ird_units_audit
```

PATH需含既有`build/RobWork/bin/Release`、`build/RobWork/libs/Release`、`vcpkg/installed/x64-windows/bin`的绝对目录。创建本次输出目录后，使用当前现场绝对exe路径`.cache/b31-build/Release/ird_units_audit.exe`，参数`--gtest_output=xml:<绝对输出目录>/gtest.xml`、`--ird_report=<绝对输出目录>/ird-test-report.json`；每次只运行一个exe。

真实结果22跑19passed/3failed/0skipped、exit1。原Units七例、Compare八例均通过（包括Q）；三个失败是合法有限极值下的C4反例。`oracle.py`用Fraction独立核对2>3/2。审计CMake将真实`RW::sdurw_math`供给原core以启用Q，测试#error阻止Q缺失时静默缩减测试面；这不修改产品CMake。

`search.json`保留实际命令/输出及路径缺失时的更正重搜；缺路径不得当“无能力”。`findings-prefix.json`核F-966前所有历史原始字节保持；`artifact-manifest.json`核本批原件，Git index/blob也应保持原字节。schema/文档门禁只证明记录有效，局部验收与全T01准入仍需分别判断。
