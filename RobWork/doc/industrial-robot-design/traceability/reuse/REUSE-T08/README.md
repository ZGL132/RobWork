# REUSE-T08 旋转适用性证据（非生产迁移）

已确认RPY正向可直接换序复用；EAA最小提案见 `../../../reuse/extensions/GAP-EAA-01.md`。产品/上游均未修改，无迁移关闭/独验结论。

- probe/Probe.cpp只调用既有RPY/EAA/Quaternion API；没有另写转换算法。probe/CMakeLists.txt直接编译上游原RPYTest.cpp，使用同vcpkg GTest::gtest_main。
- 原始无ABI宏的t08-probe-run、current-dll、trace等日志保留，仅用于环境归因，不可作为Quaternion算法结论。
- 最终t08-probe-abi*：与RobWorkSetup.cmake的EIGEN_DONT_ALIGN_STATICALLY=1匹配，VS2022 x64 Release；EAA回构差异仍存在，Quaternion::ln在零/极小角非有限。退出1是候选不满足原契约，不是已迁移产品回归失败。
- t08-upstream-abi-run：原RPYTest.Test真实通过1，0失败/跳过；没有声称覆盖所有旋转能力。上游测试仅生成原生gtest XML，未声称产生产品testkit报告。
- 复现：VS x64环境下 `cmake -S .cache/t08-probe-src -B .cache/t08-probe -G "Visual Studio 17 2022" -A x64 -DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake -DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows`，再 `cmake --build .cache/t08-probe --config Release`。
- 执行绝对路径 `.cache/t08-probe/Release/rpy_suitability.exe` 与upstream_rpy_test.exe（各自单独执行），后者 `--gtest_output=xml:.cache/t08-upstream-abi-gtest.xml`。PATH前置当前集成树RobWork/bin/Release、原vcpkg/bin、Miniconda3。原配置缺VS环境失败亦保留，不改上游路径规则。

性能、真实生产正向/断开、业务回归及完整独验：**未执行**（尚无批准的完整可实施方案），不把候选探针冒充它们。
