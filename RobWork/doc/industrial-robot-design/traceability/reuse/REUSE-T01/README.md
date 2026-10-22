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

第二批增加53条候选关系，累计77条、涉及10单元，全部仍为pending；未覆盖文件/函数继续待审。实际定向检索的10条命令与完整输出见 [batch2-search.json](batch2-search.json)，exitCode=1只表示本次范围无命中。新增候选覆盖DH正构造、全局随机源和球面采样，并登记DH转换内的LM/差分/消元、采样/姿态小核、SHA-256、JSON/UTF-8和高阶基函数。逐功能位置与差异只维护在 `reuse/functions/`，本段不构成另一份决策台账。

本批没有运行新的适用性或生产实验。`sdurw_sandbox`有真实目标但默认关闭，优化器源存在不表示它在当前配置可用；`FirstDifferences`与示例头路径/安装列表还需核查。`generateGridPositions`是确定性体心枚举，未错误绑定随机能力。Random全局32位种子、HyperSphere按delta生成数量、DHParameterSet仅取已有属性，这些差异均不构成新核心批准。下一批继续审查其余单元、实际消费者与测试/构建工具；没有把77条当成全项目功能总数，也没有关闭库存债务。

第二批首轮独立验收FAIL（`4e2239f7672f367fff436ab83b7db56d49d19cfe`）：UTF-8 DFA误绑定资源预算需求，HyperSphere catalog检索路径通配符在Windows返回2。返工将前者追溯到IO-T04/units/io.md §5.9.1编码契约，后者显式列出.hpp/.cpp；真实重跑见 `batch2-rework-search.json`，exitCode=0。不修改权威需求、不覆盖原搜索或失败报告，不把仅修正登记声称为能力适用性验证。

第三批增加18条候选/权威边界记录，累计95条、20单元均有实际定向审读样本，但这**不等于20单元全功能审计完成**。新增日志/线程/文件/宿主场景候选的边界和8条真实检索见 `batch3-search.json`；未运行新适用性实验。ThreadPool不替代worker隔离，IOUtil不替代project不可变发布，Log不替代diagnostics稳定码，evidence聚合与workflow门控不搬入适配层。policy已有碰撞复用与宿主现有worldTframe调用也需核生命周期、权限和结果消费；预览空WorkCell锚保持pending，不用HOST标签自动洗白。新增发现 `modeling::(anonymous namespace)::zeroConfigFK`，优先核runtime上游FK端口，禁止modeling自建计算模型。

后续公共API探测留在 `probe-r3/`，passed=14、failed/skipped/notRun均0。新增4项：ParallelIK在不可达半径目标耗尽预算仍返回候选（独立圆半径残差1 m，限制证据）；标准DH与四因子定义一致、Craig约定不同；EAA反向对齐默认轴与产品固定X半周相差滚转π；七次多项式非零端导数及0.5/2 s时间缩放（物理时间系数替换后求导，对照解析端点）。日志中的ParallelIK超预算错误是该限制用例的预期输出，不是正常求解能力通过。当前仅调用公开API和既有testkit报告入口，未修改生产算法或新建数值核。
