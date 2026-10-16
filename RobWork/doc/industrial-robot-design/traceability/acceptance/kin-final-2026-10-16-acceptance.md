# KIN 全栈总验收记录（2026-10-16）

| 字段 | 值 |
| --- | --- |
| 验收对象 | 分支栈 `kin-p1 → kin-p2 → kin-p3 → kin-p4 → kin-p5`（线性，每包＝实现提交＋治理验收记录提交），栈顶 `kin-p5` @ `08077729` |
| 五包送验 HEAD | kin-p1 `4ca1228b` / kin-p2 `8d1bf91a` / kin-p3 `7b301477` / kin-p4 `21432b0b` / kin-p5 `babb7d8c`——逐一与任务卡给定值一致（`git rev-parse` 实测） |
| 验收人 | 全栈总验收者（独立会话；只读产品代码，本记录为唯一写入库文件） |
| 验收日期 | 2026-10-16 |
| **结论** | **PASS（零 blocker；6 条承继/观察级事项见 §5）** |

---

## 1. 九反例 A01～A09 修复与回归测试全量核对（零悬空）

每条均在 HEAD（kin-p5 栈顶）代码层亲读修复点、亲验回归测试存在（grep `TEST…KIN_Px` 计数：P1=10、P2=10、P3=11、P4=20、P5=8，与各包记录一致）：

| 编号 | 归属包 | 修复点（HEAD 实读） | 回归测试（存在性+强度） |
| --- | --- | --- | --- |
| A01 世界→基座坐标 | P1 | `ui/plugin/KinEvaluationChannel.cpp:220,228`（`invertWorldBase(worldToBase())`→`baseTref = tBaseWorld * worldTref`）＋连带修正 `ui/plugin/UiPlugin.cpp:4275-4299`（`worldTbase * record.position` 直乘回投） | `KinEvaluationChannel.ProjectPlan_*` 5 用例（ui/test/KinEvaluationChannelTest.cpp）——本总验收实测 **5/5 PASSED** |
| A02 位置任务维度 | P2 | `kinematics/src/Ik.cpp` taskDim/收敛判据；旧 π 机制全仓**零活消费点**（grep 仅存"已移除"注：Envelope.hpp:112、Evaluators.cpp:2141/2145、Sampling.cpp:94、Workspace.cpp:71/74）；Envelope 通道迁移由 P2 attempt 2 自行补齐（未悬空给 P5） | `PositionOnlyTaskIndependentDimension_KIN_P2`（IkTest.cpp）＋`PositionProbeUsesTaskSpaceSemantics_KIN_P2`（EnvelopeTest.cpp）——实测通过 |
| A03 合法无穷显示 | P3 | `kinematics/plugin/KinPanelUnits.cpp:53-59`（+∞→语义词，NaN/−∞ 保持 fail-fast，三分类齐备） | `*KIN_P3` 11 用例（PluginPanelTest＋SessionGuiTest） |
| A04 包络上界免探测 | P5 | `kinematics/src/Envelope.cpp:369-378`（站点扫描 t=Rup·jj/M，**jj=M 上界点先行试探**）＋`:426-446`（站点全灭→NoSampleFound＋reason 明示"不构成确定性不可达"） | `NoSampleFoundLowConfidenceState/DegenerateChainUpperBoundProbeFirst/NarrowBandAwayFromUpperBoundFound`（均 _KIN_P5）——实测 **3/3 PASSED** |
| A05 混合链逐轴单位 | P3 | `KinPanelUnits.cpp:118-141`（projectJointAxisToDisplay——Prismatic/Revolute 判定、Fixed fail-fast） | `*KIN_P3` 用例组 |
| A06 64 位种子 | P5 | 四处派生点全数换 `streamSeedFor`（Ik.cpp:309 权威＋Envelope.cpp:70/Sampling.cpp:76/Workspace.cpp:174 自持副本——四份逐字比对**同构**，splitmix64 三步同金常数）；旧 `seed·2³²＋k` 模式 grep **零残留**；契约版本链同步（Envelope/Ik 2→3、Region/PoseReach/Batch→2，`grep ContractVersion` 实测） | `ProbeSeedFull64BitSensitivity_A06_KIN_P5`——实测通过 |
| A07 目标输入单位 | P1 | `KinPanelUnits.cpp:202`（resolveKinDisplayToSi 逆换算） | `TargetSubmit_*_KIN_P1` gui 用例组 |
| A08 按钮请求装配 | P2＋P4 收口 | `KinPanelFlows.cpp:46`（resolveSessionTcp 单点）＋`:83`（assembleSessionSolveRequest，:89 注明 KIN-P4 收拢）；通道侧 `KinEvaluationChannel.cpp:843-905` assembleSolveSlice 复用域设施单点（validateAnalysisConfiguration/evaluationIntervals/makeInitialValues） | `LK2_AssembleSessionSolveRequest*_KIN_P2`＋P4 通道/网关用例组 |
| A09 Home 具名来源 | P3＋P4 收口 | 编排四拒绝态 `KinPanelFlows.cpp:355-395`；宿主来源缝 `UiPlugin.cpp:2951-2989`（named-pose-set 保留键解析）＋`KinHostServicesAssembly.cpp:318-342`（makeKinHomePoseProvider）＋注入 `KinematicsPluginAssembly.cpp:183`；按钮门控 `KinematicsPanelWidget.cpp:322` | `*KIN_P3` 会话用例组（命令出口计数 0 钉） |

**悬空检查**：包间移交条目逐一闭合——P2 advisory A-3（设默认 TCP 空 TcpRef）→ P4 §2#3 已修；P2 advisory A-4（A03 归 P3）→ P3 已修；P2 B-1（Envelope 迁移）→ P2 attempt 2 自行补齐（`kEnvelopeContractVersion` 现=3 为 P5 再升级，链路连续）。未发现"以相邻包名义悬空"的条目。

## 2. 跨包一致性核对（后续包未破坏前包语义）

1. **A01 × P4**：P4 重写 `KinEvaluationChannel.cpp`（受理分流/批量通道）后 A01 修复块**原样存活**（:220/:228 实读）；`UiPlugin.cpp` 经 P4 大改（服务装配/项目闭包）后 A01 连带回投修正存活（:4266 注＋:4275-4299 实读）。**实测 A01 五用例 5/5 PASSED（HEAD 集成树）**。
2. **A02 × P4 批量通道**：批量任务点语义为**完整位姿**（`Evidence.hpp:184-214` BatchTaskPoint＝Transform3D 目标＋位置/姿态双容差；REQ-01"位置/姿态容差"、REQ-09 规则化姿态约束）——`solveItem`（Evaluators.cpp:1280）构造 IkRequest 用默认 FullPose 是**正确语义而非 A02 残留**；A02 迁移对象是位置存在性探针（Workspace/Envelope/Sampling/Evaluators 位置样本），两者分工明确互不冲突。
3. **A06 × P5 采样**：初值派生（Ik.cpp）、包络探针（Envelope.cpp probeSeedFor）、区域/关节采样（Sampling.cpp/Workspace.cpp）四处同构同算法；P4 批量通道经 `makeInitialValues`（Evaluators.cpp:1521）自动继承 P5 修复；全栈版本常量链与五包记录一致。
4. **框架零修改（SA-02）全栈核对**：`git diff --name-only c1f901f4..kin-p5` 过滤后**零文件**落在 industrialrobot/doc/scripts 之外——审查基线至栈顶全程未触碰框架源码。
5. **L2 零 Qt 定向 grep**：`kinematics/src`＋`kinematics/include` 零 Qt include（实测空输出）。

## 3. 本总验收真实执行的验证（与各包自报/记录区分）

| # | 命令 | 结果 |
| --- | --- | --- |
| 1 | `git rev-parse kin-p1…kin-p5`＋`git log --oneline redesign-main..kin-p5` | 14 提交线性栈，各 HEAD 与任务卡一致 |
| 2 | 五份验收记录通读 + `git rev-parse` 比对 | 记录所载 SHA/数字与分支事实一致 |
| 3 | `cmake -DIRD_ROOT=<industrialrobot> -P cmake/ird_gates.cmake`（**独立复跑**，非采信脚本留痕） | 退出正常，**"全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中"**，自测项全部按预期检出 |
| 4 | 集成树 `sdurws_ird_kinematics_test.exe --gtest_filter=KinEnvelope.NoSampleFound*…PositionOnlyTaskIndependentDimension_KIN_P2…ProbeSeedFull64BitSensitivity…`（exe mtime 新于最新源码，反映 HEAD） | **6/6 PASSED**（31 ms） |
| 5 | `sdurws_ird_ui_test.exe --gtest_filter=KinEvaluationChannel.ProjectPlan_*` | **5/5 PASSED**（76 ms） |
| 6 | `sdurws_ird_kinematics_gui_test.exe --gtest_filter=KinHostLoop.SixAxisVariantClosedLoop_KIN_P4` | **1/1 PASSED**（22 ms，六轴端到端真实控件闭环） |
| 7 | 代码级核对 §1/§2 所列 path:line 全部本会话亲读 | 见各表 |

## 4. 注释与红线抽查（每包≥1 处）

- **P1**：KinEvaluationChannel.cpp:210-228（A01 复合读法值语义/错误语义逐段中文注）；UiPlugin.cpp:4266 连带修正注。
- **P2**：Ik.cpp:427-433 请求校验块（KIN-TARGET-ILLEGAL 语义锚/单位 m/fail-fast 归类）。
- **P3**：KinPanelUnits.cpp:48-59（三分类无穷契约、"禁止截断成有限数字——截断即伪造数据"理由注）。
- **P4**：KinHostServicesAssembly.cpp:318-342（零缓存/绝不以当前姿态充当/保留键纪律）；KinEvaluationChannel.cpp:869-899（后台执行器不读会话态的线程约束理由注）；Evaluators.cpp:1279-1300。
- **P5**：Envelope.cpp:63-77（A06 推导完整"为什么"）；Envelope.cpp:369-374（A04 措辞修正落点）。
- 均符合 AGENTS.md §2（中文、Why 导向、单位/坐标系/需求 ID 齐备），未发现 §2.6 反面模式。

## 5. 承继与观察级事项（不阻断）

1. **P3 A-1 承继**：域外 +∞ 呈现白名单未按行收紧（建议归 P4 接线时评估，P4 未实施；P3 已裁不阻断）。
2. **P3 A-2 承继（已代码层复核现状）**：具名 Home 来源链无逐值非有限检查（KinHostServicesAssembly.cpp:325-341＋KinPanelFlows.cpp:355-395 仅四拒绝态）——若建模侧位姿集条目携带 NaN，写入口 fail-fast 异常将穿越编排达 Qt 槽（与显式提示轨双轨不一致）；正常装配下 Home 来自 modeling codec 解码、上游有校验面，风险理论性。建议后续呈现批次在编排层补非有限→提示轨。
3. **P4 A-1 承继**：改型复算路径局部搜索收敛风险（温和偏移目标复算 0 解如实呈现）——P5 范围为 A04/A06/KIN-09，未专门处理复算初值策略；既有登记边界维持。
4. **P4 A-4 openIssues**：位姿页完整姿态编辑、面板"取消"按钮、批量归档链、named-pose-set 编辑入口、studio 实机人工交互——任务书允许的如实登记遗留，归所有者。
5. **P5 advisory**：EnvelopeRayState 词表 2→3 与 CSV v2 属破坏性演进，下游呈现批次接线时需感知；同查询同种子区域样本集与旧版本不可比（IRDSSID v2 身份分立符合预期）。
6. **工作树观察**：`industrialrobot/kinematics/gui_test/ird-test-report.json` 为未跟踪脚本运行残留（不在任何分支交付内，不入库）。

## 6. 未执行事项（诚实边界）

- 未重跑全量构建与四套件全量（编排脚本已全绿——任务卡口径；本总验收以独立 ird_gates 复跑＋12 项定向回归实测替代）。
- 未重建旧版本二进制复现"修复前红"（各包验收记录已含红证/旧码推导，本总验收核对其一致性）。
- 未做 RobWorkStudio 主程序实机人工全交互（任务书归所有者）。

---

**结论：PASS——九反例全部在根因层修复且回归测试在位，五包间无语义回归、无悬空条目，框架零修改贯穿全栈，ird_gates 独立复跑零命中，定向回归 12/12 实测通过。全栈可进入合入段（所有者决策）。**
