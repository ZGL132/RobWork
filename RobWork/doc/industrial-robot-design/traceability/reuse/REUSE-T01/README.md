# REUSE-T01 首批公共 API 探测

本目录是只读审计实验，不是生产后端。上游基点沿用 catalog 的 `b12e8503cc1c2a1acc040f506842a02e126b9515`；文件规范化摘要见 catalog，实验源码/构建文件由本次提交冻结。产品源码、上游源码和 vcpkg 均无修改。`reuse/functions/` 中首批记录为 pending，不代表文件或单元已全面审计。

`probe/` 保留首轮真实 configure/build/tests 日志、gtest XML 与既有 testkit 生成的 `ird-test-report.json`，不改写历史证据。首轮10项均运行通过，但独立验收发现 RPY 参数顺序解释错误，因此不能以首轮通过证明 Euler 掩码及不可达半周反例。修正后的重跑在 `probe-r2/`：passed=10，failed=0，skipped=0，notRun=0。既有上游完整测试集、生产迁移、性能验收及远程 CI 未执行。限制用例预期当前上游的错误/未实现行为，不能当成该功能正常或 DYN-05 已通过。

实验使用 RobWork 的 CONFIG components 和现有 RobWorkSim 导出目标；不手工拼接上游 lib。为避开当前完整 Sim 包对 Studio 的装配依赖，只导入本实验所需 `sdurwsim.cmake` 与 `sdurwsim_bullet.cmake`。GTest、Qt 配置来自同一已安装 vcpkg/Qt 环境；testkit 由现有 CMake 与 `core/test/TestMainReport.cpp` 接入，不复制报告框架。Qt Core 在 testkit 配置期需要解析，但探测无 QApplication/Widgets，不启动 GUI。

复现（PowerShell 7，VS2022 x64，Release；路径按本机既有构建给出，其他机器显式替换）：

```powershell
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake -S RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01 -B .cache/t01-probe-export -G 'Visual Studio 17 2022' -A x64 -DRobWork_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake '-DCMAKE_PREFIX_PATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows;D:/software/Qt/6.11.1/msvc2022_64' -DRWSIM_TARGETS_DIR=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkSim/cmake/targets
cmake --build .cache/t01-probe-export --config Release --target ird_reuse_audit_probe
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWorkSim/bin/Release;D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
# 使用本工作树的绝对路径；报告目录必须存在。此处以可复现的本地输出为例。
& 'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/t01-probe-export/Release/ird_reuse_audit_probe.exe' --gtest_output=xml:.cache/t01-probe.xml --ird_report=.cache/t01-probe.json
```

10个用例的边界与独立解析断言见 `UpstreamSuitabilityTest.cpp`。RobWork `RPY(yaw,pitch,roll)` 与产品字段 `(roll,pitch,yaw)` 须显式换序：修正反例固定 yaw/pitch，只改 roll；不可达半周目标改成 `RPY(0,0,Pi)`（绕 X，单 Z 关节不能到达），旧 `RPY(Pi,0,0)` 实为可达绕 Z。两项修正重跑通过后才恢复相应反例结论。另修正 `runtime.rotationFromCustomEaa` 的全限定符号：它位于 runtime 命名空间，未位于匿名空间。

当前发现为 SVD/极小尺度对称特征值/主惯量可调用、同链单 leg 位置掩码可用、七次通用多项式可用、移动轴 Wrench 可投影；Euler/EAA 误差不能互换，半周局部求解成功标志须独立复验，Bullet addDevice 不能使用。它们没有证明任意机型/任意端导数/完整工况的适用性。

前期失败的环境归因：完整无 components 的 RobWork 包尝试解析未生成的 umbrella DLL；带 vcpkg toolchain 的外部配置遇上游 `C:\local` HINTS 转义；未进入 VS x64 环境时 Boost atomic 缺 synchronization 导入；MSVC 默认 GBK 不能解析本实验 UTF-8 注释。最终采用精确 components、既有安装前缀、VS x64 与 `/utf-8` 后构建通过。没有以这些环境失败为由保留自写核心。

仍未解决的能力差异集中登记于 [缺口入口](../../../reuse/extensions/required-proposals.md)，不在本目录维护另一份功能决策表。远程 push 仍因 `127.0.0.1:17890` 代理不可达失败；本地证据不能替代远程 CI 或主线合入。
