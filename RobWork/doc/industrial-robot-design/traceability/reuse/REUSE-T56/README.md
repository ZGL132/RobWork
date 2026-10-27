# REUSE-T56 — RC-06 Snapshot 面定案记录（2026-10-28，零产品代码改动）

计划来源：[reuse/migration.md §13.5.3 RC-06](../../reuse/migration.md)（:417 原文「`runtime/src/Snapshot.cpp` 四访问器按实际受控视图/任务私有 State 转发定案」）。契约：[tasks/REUSE-T56.json](../../../tasks/REUSE-T56.json)（status=ready，实施完成待独验）；凭证：[reuse/changes/REUSE-T56.json](../../changes/REUSE-T56.json)（零产品文件变动，files 如实留空）。授权口径＝所有者 2026-10-27 委托「按建议裁决」；独验不可自签，相关项如实 NOT RUN。起始 SHA=`454d4cfc6`。

## 1. 查重

2026-10-28 实测（`ls`）：`tasks/`、`reuse/changes/`、`traceability/reuse/` 均无 REUSE-T56（T55 已用），建档成立。

## 2. 四访问器逐个定案

矩阵行（`reuse/functions/runtime.json`，capabilityId 系 REUSE-T30 同族更正后的权威值）：

| functionId | 现状实现（file:line） | 数据通路 | 判定 | 消费方 |
| --- | --- | --- | --- | --- |
| `runtime.RuntimeSnapshot.workCell` | `Snapshot.cpp:753-756` 返回 `m_workCellView`（构造期一次建立：`Snapshot.cpp:664`，借持快照 `Ptr<const WorkCell>`，空 Ptr 构造 fail-fast） | 受控视图转发 | 已合规保留（零代码改动） | `IRuntimeModelView` 统一只读入口（§8.3，kinematics/trajectory/dynamics/policy 共同消费点）；测试钉：SnapshotTest.cpp:271（frameCount>0）/:522（worker 等价）/:706（借持保活） |
| `runtime.RuntimeSnapshot.workCellHandle` | `Snapshot.cpp:758-766` rw Ptr 裸构造零所有权借持（析构不释放；const_cast 仅为 CollisionDetector 遗留非 const 签名），生命周期由快照 `m_workCell` 持有背书 | 上游兼容借持直出 | 已合规保留——ARCH §3.6.1 明文登记「已存在的非 const 上游兼容借持」：仅按碰撞适配契约使用、必须由快照保活、禁止结构写入/跨进程传递/扩大暴露；「不能因返回非 const 就整类判违规」「收窄接口须经兼容设计与独立验收，不在本轮改产品接口」 | 生产：CollisionSession.cpp:69（policy 碰撞会话）；测试：SnapshotTest.cpp:937 |
| `runtime.RuntimeSnapshot.tryDynamicWorkCell` | `Snapshot.cpp:768-784` 有 DWC→`DynamicWorkCellConstView` 只读包装（§8.2 收敛 const）；无 DWC→`UnknownObject`＋能力定位（非崩溃路径） | 受控视图转发＋Expected 错误面 | 已合规保留（兄弟行 `runtime.DynamicWorkCellConstView.accessors`=adapter/migrated 同判） | 测试钉：SnapshotTest.cpp:826（物性缺失成因）/:841（未请求 DWC 成因） |
| `runtime.RuntimeSnapshot.makeState` | `Snapshot.cpp:811-816` `m_workCellView.defaultState()` 值拷贝上游 `WorkCell::getDefaultState`（`RobWork/RobWork/src/rw/models/WorkCell.hpp:347` 实测命中） | 任务私有 State 转发 | 已合规保留——ARCH §3.6.1「业务适配器可持任务私有 State」既有口径；§8.7/§9.2 每线程私有、跨线程绝不共享 | SnapshotTest.cpp:453（多线程各自 setQ 读回隔离）；ContractSuiteTest.cpp:677/682/716/992（四替身同 State 纪律）；HostPresentationView.cpp:96（呈现会话值拷贝隔离） |

共同面：四访问器均零构造、零 WC/DWC 拓扑与物性写入、零句柄逃逸出既有契约、零 State 共享——模型真值唯一归属 runtime owner（ARCH §3.6.1），与 T52 构造权裁决（buildRneConsumptionDevice＝runtime 公共工厂单点）不冲突（本批不涉构造面）。先例对照：T19-Fk.cpp 消费侧（模型消费经 `runtimeModelView()` 锚、`Device::setQ` 任务私有 State、无 WC 构造无发布面）与本批供给侧四访问器为同一受控视图/任务私有 State 口径的两端。

## 3. 实测验证（本会话真实执行）

1. **区段/全文件摘要（reuse-contract.ps1 Digest 口径：BOM 剥离＋CRLF→LF＋尾换行规整）**：
   - `Snapshot.cpp` 全文件 Digest = `756c110afc4c2b8f265ab6dbf8ba58eadc607fe0a65d8774a5a6181d13735004`（与 HEAD 逐字节一致，`git diff` 零产品改动）；
   - 四区段逐一 MATCH 矩阵登记值：753-756=`6c7afba0…`、758-766=`527e41a9…`、768-784=`1d7153ea…`、811-816=`76031e32…`（复算脚本输出在案于任务记录）。
2. **检索（均已执行）**：`rg -n "WorkCellConstView|DynamicWorkCellConstView|defaultState|workCellHandle" …/runtime/src/Snapshot.cpp`（:753/:758/:768/:783/:815 命中）；`rg -n "getDefaultState" RobWork/RobWork/src/rw/models/WorkCell.hpp`（:347）；`rg -n "workCellHandle" …/industrialrobot`（生产消费 CollisionSession.cpp:69＋HostPresentationView.cpp:79 经视图句柄＋测试 :937）；`rg -n "tryDynamicWorkCell|DynamicWorkCellConstView|hasDynamicWorkCell" …Snapshot.cpp …DynamicWorkCell.hpp`（行注册命令原样复跑）。
3. **全量消费者回归**：`build/RobWorkStudio/bin/Release/sdurws_ird_runtime_test.exe`（增量构建确认后运行，QT_QPA_PLATFORM=windows）→ **312 项＝311 passed / 0 failed / 1 skipped / 0 NOT RUN，exit 0**，ird-test-report `decisive=true`。skipped＝`ContractSuite.ChildProcessWorker_RT_ID_1`（子进程 worker 契约测试环境排除项，T13 证据运行同款）。原始证据：[run/runtime.xml](run/runtime.xml)、[run/runtime.log](run/runtime.log)、[run/runtime-report.json](run/runtime-report.json)。
4. **ird_gates**：`cmake -DIRD_ROOT=… -P cmake/ird_gates.cmake` → **2 处既有 IRD-GATE-SA02 命中**（「登记册引用的补丁文件不存在：0004-sandbox-optimization-levenbergmarquardt.patch」×2）。归因（代码读证 `ird_gates.cmake:713`：从 PATCHES.md 全文 MATCHALL 提取 `*.patch` 文件名并要求存在于 `industrialrobot/patches/`）：0004 补丁本体按 §13.5.6/RC-04 裁决存设计树 `traceability/reuse/REUSE-T55/0004-…patch`（PATCHES.md 0004 行明文登记该交付形态，REUSE-UNDECLARED 基线冻结点约束所致）——**454d4cfc6 引入的登记交付形态与门禁位置核对不一致，属 RC-04 治理批责任面的既有无关全局阻断**；PATCHES.md 与门禁脚本均不在 T56 授权面，本批不处置、不冒称全局 PASS。T56 相关命中＝0。

## 4. NOT RUN（如实分列）

- **生产正向证据四象限（逐功能 normal/disconnected/oracle/upstreamComparison 报告与 decision/status 迁移）**：NOT RUN——本次实施发现的**结构性阻塞**：四访问器均为纯恒等转发/值转发，任何语义断开即崩溃，无法产出 gate 形态（无 error 节点＋真实断言失败）的 disconnected 证据：
  - `workCell`/`workCellHandle`：断开即返回空视图/空句柄→视图构造 fail-fast 或空指针解引用崩溃（SnapshotTest.cpp:937 直接解引用无守卫）；
  - `tryDynamicWorkCell`：always-err 变异使 `:279 EXPECT_TRUE(dwc.ok())` 非致命失败后继续执行 `:280 dwc.get()`→`Expected::get()` 错误态抛 `std::logic_error`（`Errors.hpp:240` 实证）→进程终止、无有效 XML；
  - `makeState`：返回扰动 State（设备 Q 置非零）对全部可见断言不可见（ContractSuite/SnapshotTest 均自行 setQ 覆写；快照多线程隔离用例只读回自写值）；返回空 State 则帧变换读取越界崩溃。
  - T13 聚合断开先例（WorkCellCompiler.cpp 逻辑承载面）不适用于纯转发面。处置选项（聚合断开口径放宽 / nonOverlap 式登记 / 证据形态豁免）待所有者裁决，随 RC-11 能力包批次；矩阵行因此保持 pending 不假关（T30 同行口径）。
- **独立验收**：NOT RUN（所有者另开会话或人工执行，实施者不自签）。
- **commit-lint / verify-task-scope**：NOT RUN（本批不执行 git 写命令、无送验 SHA；随提交批显式 `-Base 454d4cfc6… -Head <送验SHA>` 执行）。
- **validate-docs `-DeepEvidence` 与 evidence-index.json 重建**：NOT RUN（索引非本批授权面）。

## 5. 边界

不触 catalog.json（待 T44）、DhConvert.cpp/Sampling.cpp（LM proposal 锁定面）、RpyMath.hpp（冻结）、requirements 面（待 T42）、reporting/ui 权限扩面（RC-09 另批）、他人改动（trace-matrix.json 等）；不执行 git push/fetch。RC-06 其余三面（RequirementsPanelWidget::targetEditRotation／BaseWorldTransform.cpp／DescriptionValidator::singularValuesOneSidedJacobi）不在本批，状态不变；T29 维持 blocked（无新证据，不重启）。
