# Modeling Reliability Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复建模模块中影响工程设计可信度的数值边界、交接防御和多文件导出一致性问题，并补齐可复核证据。

**Architecture:** 继续以 modeling 为参数化权威，以 runtime 为编译真值；数值函数保持纯函数，UI 只消费报告。每个工作包独立提交，测试先行。

**Tech Stack:** C++17、Qt6 Widgets、GoogleTest、CMake、MSVC 2022 x64、现有 RobWork math/runtime 接口。

---

### Task 1: 惯量特征值边界

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/InertiaMath.hpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/PropertyEstimationTest.cpp`

- [ ] 写各向同性张量特征值测试，断言 `diag(2,2,2)` 返回 `{2,2,2}`，并覆盖 `1e-300` 与 `1e300` 的有限输入。
- [ ] 运行单测确认修复前边界失败或出现非有限中间结果。
- [ ] 在 `p2 == 0` 时直接返回 `{trThird,trThird,trThird}`，保持零矩阵分支优先。
- [ ] 运行 PropertyEstimationTest 和 modeling_test，确认所有物性公式、SPD、三角不等式测试通过。
- [ ] 提交 `[WP-13-T23] 修复各向同性惯量特征值边界` 并推送。

### Task 2: CanonicalBridge 非法轴防御

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/CanonicalBridge.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/CanonicalBridgeTest.cpp`

- [ ] 写 zeroOffset 非零且 axis 为零、NaN、Inf 的映射测试。
- [ ] 运行测试确认修复前会进入除零或产生非有限旋转。
- [ ] 在归一化前检查平方范数有限且严格大于零，返回 `UnitIllegal`/既有错误面。
- [ ] 运行 CanonicalBridgeTest、contract_test 和 modeling_test。
- [ ] 提交并推送独立修复。

### Task 3: WC/DWC 整体原子导出

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/PackageWorkCell.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Package.hpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/PackageWorkCellTest.cpp`

- [ ] 写 WC 已存在、DWC 目标不可写的故障测试，断言 WC 旧内容保持不变。
- [ ] 运行测试确认修复前 WC 会先被替换。
- [ ] 将两面序列化到统一临时区，全部写入和校验成功后再提交目标文件；任一失败清理临时文件并恢复旧输出。
- [ ] 运行 PackageWorkCellTest、modeling_test 和 contract_test。
- [ ] 提交并推送独立修复。

### Task 4: DH 求解尺度与多初值可靠性

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/DhConvert.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/DhConvertTest.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/DhConvertEquivalenceTest.cpp`

- [ ] 写混合 m/rad 尺度、近奇异轴线和固定初值不佳的转换测试。
- [ ] 运行测试确认修复前出现 AnalysisFailed/错误停滞或错误 Approximate。
- [ ] 对参数和残差做显式尺度化，轴向残差使用稳定的旋转误差表达，并用确定性候选种子选择最小合格残差。
- [ ] 保证只有 Exact/ExactNonUnique 且 FK 等价通过时才允许权威切换。
- [ ] 运行全部 DH、golden、contract 和 modeling 测试，保存 XML 留痕。
- [ ] 提交并推送独立修复。

### Task 5: 耦合矩阵独立参考验证

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/PartsTest.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/test/CodecTest.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/CMakeLists.txt` only if an already-registered reference dependency is needed

- [ ] 写 2×2、3×3、6×6 随机确定性矩阵和阈值附近矩阵的独立参考条件数测试。
- [ ] 运行测试确认参考对照能捕获 SVD 结果漂移。
- [ ] 只在已有 vcpkg 依赖和任务登记允许的范围内接入参考实现；否则用测试内独立 Gram-Schmidt/特征值实现，不引入新依赖。
- [ ] 运行 PartsTest、CodecTest、contract_test 和 modeling_test。
- [ ] 提交并推送独立修复。

### Task 6: 工程师界面提示与就绪报告

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelModel.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/Readiness.cpp` only for missing execution state data
- Test: corresponding modeling GUI tests

- [ ] 写 UI 测试验证模板速度/加速度显示“未进入 runtime schema”，而不是显示为已生效限制。
- [ ] 写 UI 测试验证低层阻断后后续层显示“未执行”。
- [ ] 实现状态徽标、来源/单位/参考系提示和 DH 误差列，保持计算逻辑在 modeling/core。
- [ ] 按 Windows Qt 规则运行单个 GUI executable，保存报告。
- [ ] 提交并推送独立修复。

### Task 7: 全量验证与交付留痕

- [ ] 重新配置集成模式并确认 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`。
- [ ] 构建 Release，运行 modeling、contract、GUI 三个目标，分别生成 XML/JSON/日志。
- [ ] 运行 `ird_gates`、文档验证和 `git diff --check`。
- [ ] 复核所有工作包提交、分支状态和远程同步情况。
- [ ] 形成中文验收报告，明确已通过、未执行和环境跳过项。
