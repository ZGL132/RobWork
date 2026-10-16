# KIN-P5 独立验收记录（2026-10-16）

- **验收对象**：工作包 KIN-P5（工作空间与算法质量——审查 A04/A06 修复＋KIN-09 采样语义闭合）
- **分支 / HEAD**：`kin-p5` @ `babb7d8c5f19f202d927443cb9b37a64fa6bf32e`（已推送 `origin/kin-p5`，与本地一致）
- **验收人**：对抗式独立验收人（与实施者零共享上下文，全新会话）
- **结论**：**PASS（零 blocker）**
- **验收方式**：代码级根因核对（旧实现 da7db502 → 新实现 babb7d8c 逐层对照）＋自构新反例独立探针（31/31）＋定向复跑关键用例（12/12）＋ird_gates 独立自跑（零命中）＋留痕/提交/文档核对。

---

## 1. 逐项核对表

| # | 验收项 | 结果 | 证据（本验收人真实执行/阅读） |
| --- | --- | --- | --- |
| 1 | A04 根因修复：上界点免探测废除 | ✅ | 旧实现（`git show da7db502:.../Envelope.cpp` 3a 段）确认根因：`t₀=Rup 被注释称"解析已证不可达点，不探测"`、锚点全灭→`Computed＋rMax=0`。新实现 `Envelope.cpp:369-374` 站点扫描 `t=Rup·jj/M`（jj=M 即上界点先行试探）、`Envelope.cpp:426-443` 站点全灭→`NoSampleFound`（reason 必填含"未找到可达样本/不构成确定性不可达"）。词表 `Envelope.hpp:157-173` 新增 `NoSampleFound`；"已证不可达"错误表述全单元清除（残留 3 处均为引用旧错误的修正说明，`Envelope.hpp:45,49`） |
| 2 | A04 反例等价入库 | ✅ | 审查探针 `audit/kin-current/AuditProbe.cpp:69-76`（circleModel、directionCount=1、ReferenceQ 单初值、期望 rMax≈1.0）与入库用例 `EnvelopeTest.cpp:809`（coaxialChainModel 同几何同预算）参数同源等价；窄带反例 `EnvelopeTest.cpp:859` 断言带外缘 0.4736 双边界（GE −0.003 / LE +1e-3），非恒真 |
| 3 | A06 根因修复：完整 64 位种子混合 | ✅ | 旧实现三处 `seed*0x100000000ull + k` 确认（da7db502 版 Envelope.cpp probeSeedFor、Ik.cpp:318、Sampling.cpp:214）；新实现 `streamSeedFor`（`Ik.cpp:314` 唯一算法说明点；`Envelope.cpp:81`、`Workspace.cpp:190` 自持副本同构）——splitmix64 双射，固定 index 下 seed→输出单射，高 32 位不再与序号同加法域 |
| 4 | A06 算法身份同步（不假借同一身份） | ✅ | 旧→新实测：`kEnvelopeContractVersion` 2→3（Envelope.hpp:109）、`kIkSolverContractVersion` 2→3（Ik.hpp:84）、`kRegionCoverageContractVersion` 1→2（Sampling.hpp:109）、`kPoseReachabilityContractVersion` 1→2（PoseReach.hpp:106）、`kTaskPointsBatchContractVersion` 1→2（Evidence.hpp:85）、IRDWSP1 codec 1→2（Workspace.cpp:495 含 mode/关节采样定义字段）、IRDSSID codec 1→2、点云 CSV v2 增 sampling-mode 列（Workspace.cpp:1252） |
| 5 | KIN-09 关节空间采样语义 | ✅ | `Workspace.cpp:232-297` `generateJointSpaceConfigurations`：Random=逐轴均匀（streamSeedForLocal (seed,k)）；Grid=**逐轴取点笛卡尔积全网格含两端**（counts=1 取中点；odometer 轴 0 慢末轴快）——非对角线。`Workspace.cpp:695-790` 关节模式逐构型经 `FkEvaluator` FK 产点云，FK 值面错误=DataInsufficient 不虚构可达；`Workspace.cpp:531-607` 校验按模式分派（关节模式不消费区域盒；网格维度/零格/∏≤100000 闸 fail-fast）；CSV/点云模式溯源列实测（探针 P4） |
| 6 | KIN-09 修复前问题（JointGrid 对角）处置 | ✅ | 初值策略 JointGrid 对角线语义在 `Ik.hpp` 如实登记并与 JointSpaceGrid 分工交叉引用；单元卡 v0.27 行登记闭合（kinematics.md:7、1005） |
| 7 | 回归测试真实性 | ✅ | 新增/改造用例全部存在且断言有强度：`NoSampleFoundLowConfidenceState`（回退 Computed+rMax=0 即红）、`DegenerateChainUpperBoundProbeFirst`（rMax NEAR 1.0/1e-4）、`NarrowBandAwayFromUpperBoundFound`（双边界）、`ProbeSeedFull64BitSensitivity`（初值矩阵替身直接观测 NE＋同种子复现 EQ）、`SeededRandomFull64BitSeedSensitivity`（高/低位＋复现＋采样域）、`JointSpaceGridFkPointCloudAnalytic`（9 点解析 FK 1e-12）、`JointSpaceRandomDeterministicAndSeedSensitive`、`JointSpaceQueryValidationAndFkErrorFace`、RegionCoverageTest:663-673（8 样本逐一高位敏感）。用例名带 KIN-10/KIN-09/KIN-13/NFR-COR-01/AT-25/AT-26 追溯（IRD_TEST_INFO） |
| 8 | 修复前红实证 | ✅（留痕核对） | `traceability/builds/kin-p5/red-probe-before-fix.log`：旧实现上同源断言 2/2 复现失败（A04 rMax 实测 0；A06 两 seed 初值矩阵逐字节相同），方法（stash→旧树挂临时探针→定向运行→恢复）与 gtest 输出摘录完整。实施者自报，本验收人以旧实现代码层逻辑复核成立（旧算法在该几何/种子下必然输出 0/相同序列）＋探针文件未入库符合约定 |
| 9 | 集成/冒烟测试与构建 | ✅（留痕核对，未重跑全量） | kin-p5 留痕目录 17 个文件齐全；`kinematics-test.xml` 汇总 tests=258/failures=0，8 个 KIN_P5 用例在 XML 中有执行记录；smoke/xml/log/ird-gates.log/ird-test-report-*.json 与自报数字一致。按验收要求第 4 条未重跑全量（编排脚本已执行全绿） |
| 10 | 中文注释规范（AGENTS.md §2） | ✅ | 抽查 `Envelope.cpp` 文件头/算法段（旧缺陷①②与新算法说明）、`Ik.cpp` streamSeedFor（"为什么不用 seed·2³²＋k"完整推导）、`Sampling.cpp`（A06 修复＋身份升级指针）、`Workspace.cpp` generateJointSpaceConfigurations（Doxygen @param/@return 全套＋网格语义＋黄金序）——中文、带单位（m/rad）、带出处（审查条目/需求 ID/卡章节）、解释为什么 |
| 11 | 架构红线（AGENTS.md §5） | ✅ | kinematics src/include 零 Qt include（grep 实测）；零业务域互链 include（R-1，grep 实测）；提交 36 文件全部位于 `industrialrobot/` 与 `doc/industrial-robot-design/` 下——框架零源码修改（SA-02）；ird_gates 独立自跑零命中（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02） |
| 12 | 提交信息四段式 | ✅ | babb7d8c 标题 `[KIN-P5] 修复审查 A04/A06 并闭合 KIN-09 关节空间采样语义`，正文 What/Why/How/影响面四段完整、验证数字如实（含 skip 登记） |
| 13 | 文档同步（DTB §5.4） | ✅ | `units/kinematics.md` 版本行 v0.27＋§14.6 v0.27 详情行（:1005）同提交落位，含诚实边界（PNG/多岛/分辨率） |
| 14 | 分支与推送 | ✅ | `kin-p5` @ babb7d8c 已推送 `origin/kin-p5`（git log origin/kin-p5 实测一致）；未触碰 main；无 force 推送痕迹 |

## 2. 独立对抗探针（本验收人自构，与实施者用例零共享断言数据）

探针为临时产物（%TEMP%，已清理，不入库），零 gtest、直接链接产品库（build/RobWorkStudio/libs/Release），31/31 通过：

| 探针 | 新反例设计（实施者测试未覆盖的参数） | 实测结果 |
| --- | --- | --- |
| P1（A04） | 极窄环带 L1=0.9/L2=0.6、j2∈[π−0.1,π+0.1]，带宽 0.0089 m **窄于**站点分辨率 Rup/M=1.5/32=0.0469 m | state=NoSampleFound（非 Computed）、rMax=0 占位、probes=32 全探、reason 含"未找到/不构成"——漏检以诚实低置信呈现，**旧行为（Computed+rMax=0）未复现** |
| P2（A04） | 折叠带 L1=0.9/L2=0.6、j2∈[π−0.9,π+0.9]，解析带外缘 0.706159（独立公式） | rMax=0.706146（差 1.3e-5 < 容差 1e-4），Computed、不越带外缘——数值正确而非仅"非零" |
| P3（A06） | 四对种子：审查原始对 1 vs 4294967297；高位单比特翻转（bit38）；低位相邻；同种子复现；另关节随机采样高位敏感＋样本集身份分立 | 全部符合：不同 64 位种子序列必不同、同种子逐位复现、身份不同 |
| P4（KIN-09） | **非对称网格 [2,3]**（对角线实现只产 2 点必破）：j1∈[−2.97,2.97]×j2∈[−π,π] | 6 构型全网格、逐点网格坐标 1e-12、逐点独立解析 FK 1e-12、首末点含两端、点云 6 点、CSV 逐行 `,joint-grid,` 无 `,cartesian-region,` |

## 3. 本验收人真实执行的命令与结果

1. `git branch --show-current` / `git rev-parse HEAD` / `git log origin/kin-p5 -1` → kin-p5 @ babb7d8c，与远程一致。
2. `git show da7db502:<旧 Envelope.cpp / 旧 hpp 常量>` → A04/A06 旧根因与旧版本常量基线（2/2/1/1/1、IRDWSP1 v1）逐项确认。
3. 独立探针编译运行（VsDevCmd x64＋cl /std:c++17 链接产品库）→ `31/31 通过`（首跑 30/31——唯一失败系探针自身解析自检公式漏取向量模长，修正探针后全过；产品行为无失败）。
4. `sdurws_ird_kinematics_test.exe --gtest_filter=<8 个 KIN_P5 用例>` → `8 tests ran, 8 passed`。
5. `--gtest_filter=KinRegionCoverage.GoldenGeneratorsAndSameOrderRerun_WP15T06_ACC4:KinEnvelope.Acc1_RayBoundaryDefault180SphericalShell_WP15T15:KinEnvelope.Acc1_CancelPropagatedFromSolver_WP15T15:KinEnvelope.FacilityFailureNonTerminalStopsAll_WP15T15` → `4 tests ran, 4 passed`（定向复跑合计 12/12）。
6. `cmake -DIRD_ROOT=... -P .../cmake/ird_gates.cmake` → 退出码 0，`全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中`。
7. grep 红线抽查（Qt include/业务域互链）→ 零命中。
8. 留痕目录、red-probe-before-fix.log、kinematics-test.xml（tests=258/failures=0/8 个 KIN_P5 记录）、单元卡 v0.27 行与 §14.6 v0.27 详情行 → 逐项核对一致。

## 4. 未执行与诚实边界

- **未重跑全量构建与四套测试套件**（验收要求第 4 条明示编排脚本已全绿、不重跑）；集成/冒烟数字以留痕 XML/log 与自报一致性核对为准。
- **未独立复现"修复前红"**（红实证以留痕 log＋旧实现代码层逻辑复核确认；未重复 stash 挂探针流程）。
- 完整三维可视化浏览/PNG 导出入口未验收（任务书尽力项，实施者已登记 openIssues 与单元卡诚实边界——不阻断）。
- 非星形（射线多岛）工作空间更外侧可达岛不可发现、窄于站点分辨率 Rup/M 的环带整带漏检为算法预算固有边界（本验收 P1 探针实证漏检时以 NoSampleFound 低置信态呈现、不产生误导确定结论——符合验收标准"不再给出误导性确定结论"）。
- ui/plugin 面板未暴露关节空间采样模式编辑入口（域查询值面已就绪，实施者登记归 ui 呈现批次——不阻断）。

## 5. Advisories（不阻断）

1. `EnvelopeRayState` 词表 2→3 值与多点契约版本升级属破坏性演进（CON-04 本意）——下游消费方需感知新态与 CSV v2 列，建议后续呈现批次接线时同步。
2. 区域模式随机序列随 A06 换派生，同查询同种子的区域样本集与旧版本结果不可比（IRDSSID v2 已使身份分立，符合预期，提示缓存类消费方注意版本核对）。
3. P1 探针显示站点全灭时 probeCount=M 全额消耗（预算如实花费）——文档已明示，无碍。

---

**结论：PASS——A04/A06/KIN-09 三项修复在根因层成立，反例等价入库且断言有强度，独立对抗探针（31/31）与定向复跑（12/12）均未能打破修复；零 blocker。**
