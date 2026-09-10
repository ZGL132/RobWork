# 基础单元公共契约 API 差异记录

| 字段 | 值 |
|---|---|
| 任务 | FOUNDATION-CR-01（契约冻结审查的逐项差异产出） |
| 日期 | 2026-09-10 |
| 范围 | core、evidence、policy、runtime、testkit 两两公共接口 |
| 方法 | 逐项提取双方详设的接口原文（签名/字段/错误语义）比对；差异即裁决；裁决落地为文档修正＋任务门禁 |
| 结论 | 8 项全部设计级关闭；CR-05 发现并修正 1 处实质接口冲突；CR-06/CR-07 保留实现级门禁；无新增 blocked 任务 |

## CR-01 core 词表 ↔ evidence ResultEnvelope 状态 —— 已关闭

| 侧 | 位置 | 接口事实 |
|---|---|---|
| core | §4.7/§5.6 | `EvaluationMode{preview/quick/verified}`、`TaskOutcome{completed/canceled/failed/interrupted}`、`EngineeringStatus{feasible/engineering-infeasible/data-insufficient/not-applicable}`、`TaskState` 九态；每枚举一对 `toToken`/`*FromToken`（未知 token→`nullopt`） |
| evidence | §7.1/§3.4/§6.4 | `ResultEnvelope` 字段直接使用 `core::TaskOutcome/EngineeringStatus/EvaluationMode`；§3.4 明确回复 P-D-1"本文消费且不重复定义"；§6.4 五级汇总是判定轴另一维度，不复用 outcome 枚举 |

差异发现：无冲突。组合矩阵（Completed×NotApplicable 拒绝、非 Completed×NotApplicable 显式标记、Preview 构造边界拒绝）与 ERR-01、表 1、表 3 一致。

联合契约测试（随实现执行）：CORE-T06（UT-EVAL token 往返）＋CORE-T07（DiagData C-1～C-3）＋EV-T07（EV-ENV-1 全组合矩阵）。EV-T07 的 `_contract_test` 增加断言：evidence 翻译单元仅经 `core/Evaluation.hpp` 取得词表（无本地枚举定义），测试内 token 字面量与 `core::toToken` 输出逐一相等。

## CR-02 canonical 编码与内容身份边界 —— 已关闭

一致纪律（四家编码器逐项核对相同）：确定性二进制＋magic＋长度前缀＋大端＋presence 字节（缺失≠空值≠零）＋浮点 IEEE754 双精度位模式＋NaN/±Inf 编码入口拒绝＋浮点近似相等禁止作身份键；摘要算法唯一＝core `ContentDigester`（SHA-256，D-05）；编码器版本写入编码并参与身份。

排除字段登记（"对什么做摘要"的边界，实现期以各自 codec 单测钉住）：

| 单元 | 编码器 | magic | 身份排除项 |
|---|---|---|---|
| evidence | SnapshotCodec / SliceCodec | `IRDSNAP1` / `IRDSLCE1` | 快照身份取 refs-only 形态（对象载荷字节由 contentVersion 承诺，物化不改变身份）；切片含全部冻结条目＋Environment 版本要素 |
| policy | PolicyCodec | `IRDPOL1` | 排除 policyObject/origin/校验状态/诊断/兼容注记（仅语义闭包字段入身份） |
| runtime | RT-Codec（CanonicalModel/NameMap/Snapshot） | `IRDCANO` 等 | 排除 diagnostics、capabilities、contentIdentity 自身、revisionSeq、installPreset 来源标记 |

差异发现：无冲突；magic 互异、无跨单元同构编码、无第二摘要实现。

## CR-03 evidence ↔ project 对象闭包 —— 已关闭（project 完整复核为后续事项）

| evidence 侧（§3.3） | project 侧（§5.2 现文） | 适配 |
|---|---|---|
| `IObjectBytesSource::tryObjectBytes(ObjectId, ContentVersion) → optional<vector<uint8_t>>` | `IProjectQueryPort::tryObject(ObjectId, ContentVersion) → optional<vector<uint8_t>> noexcept` | 一比一转发 |
| `IRevisionClosureSource::objectInRevision(RevisionId, ObjectId, ContentVersion) → bool` | `RevisionView{objectRefs 全量引用集（含 digest）}` | 适配器做引用集成员查找 |

差异发现：无签名冲突。登记：project.md 历史磁盘缺失已消除（2026-09-10 当前 §1～§15 齐备，P-EV-6/P-POL-6 的联合接口复核仍待完成）——最小接口与 §5.2 现文一致，维持原裁决"project 完整设计复核前不扩大接口"；该复核属 project 单元详设评审，不阻塞五基础单元。

## CR-04 policy ↔ runtime 碰撞场景 —— 已关闭（含 policy 过期记载更正）

接口映射（policy 零 runtime 编译依赖维持；适配器归 L5/请求方）：

| policy 侧（§3.3/§6.1） | runtime 侧（§7.3/§8.3/§9.1/§10.5） | 适配方式 |
|---|---|---|
| `IPolicyNameContext::tryObjectId/tryRuntimeName/nameMapContentIdentity` | `IRuntimeNameResolver::resolveRuntimeName/resolveObjectId/nameMapIdentity`（Expected 非抛出） | Expected 错误→`nullopt`（不可解析不猜测，ARC-04） |
| `IPolicyBytesSource::tryObjectBytes` | project `IProjectQueryPort::tryObject` | 与 CR-03 同形 |
| `CollisionScene.workcell: shared_ptr<const WorkCell>`（共享只读） | RuntimeSnapshot 私有 `workcell_`，经 `WorkCellConstView` 暴露 const 引用，生命周期随快照 `shared_ptr` | 适配器以别名构造 `shared_ptr<const WorkCell>(快照指针, &view.workCell())`——所有权语义等价，runtime 无需改接口 |
| `CollisionScene.sceneContentIdentity` | `RuntimeSnapshot.workCellCompileIdentity`（WC 层编译产物身份） | **裁决**：取 `workCellCompileIdentity`（`snapshotIdentity` 含修订定位字段，不入）；policy 不消费 DWC |
| `CollisionScene.objects / adjacentLinkPairs` | CanonicalModel 对象/层级索引事实（§4.6） | 请求方自编译产物装配（两文档一致） |
| worker 侧名称上下文 | runtime worker 物化重建（§9.5，同 nameMapIdentity） | 装配侧保证身份一致 |

差异发现：接口无冲突；但 policy.md §1.2/§2.1/§10.2/P-POL-1 仍登记"runtime.md 未产出/待其详设核对"——与事实不符（runtime.md v0.1 已产出且 §10.5 承接全部期待）。本次已更正并关闭 P-POL-1。

## CR-05 runtime 快照身份 ↔ evidence 切片身份 —— 已关闭（发现 1 处实质冲突，已修正）

**冲突描述**：runtime.md §10.3 声称向 evidence 提供 `modelIdentity`、`robworkBaselineVersion`（"入 evidence 复现块与切片 Environment 条目——其 §4.1.2 预留字段"）；但 evidence.md §4.1.2 `ReproductionBlock` 仅有 `{productVersion, evidenceContractVersion, codecVersions, compilerContractVersion?, collisionBackendVersion?}`，无此二槽位；且 §4.1.1 称规范模型身份"＝f(对象闭包, 编译器契约版本)"可自快照条目推导——与 runtime 实际身份组成（另含编译器版本、RobWork 基线版本、nameMapRuleVersion、codecVersions、compileOptions，§4.5/§9.1）不一致。若按 evidence 原文实现，基线/编译器升级不改变 sliceId → 跨基线错误缓存复用（违背 CON-05；NFR-DEP-05"基线变化＝产物不可比"失控）。

**裁决**（沿 CR-05 既定方向"身份值由 runtime 计算，evidence 不自行重新编码 CanonicalModel"）：

1. evidence 切片 Environment 依赖新增两个 token（值由组装方经值传递录入，evidence 不重算）：
   - `runtime.model-identity`：值＝`RuntimeSnapshot.modelIdentity` 规范文本（`cid-<64hex>`）；
   - `runtime.robwork-baseline`：值＝`robworkBaselineVersion`（commit/tag＋选项摘要）。
   - 消费 CanonicalModel 的评估**必填**此二条目（与 `compilerContractVersion` 必填口径并列）。
2. 此二条目属**基准类要素**，进入 `inputBaselineId`（EVI-02 比较基准一致性据此拦截跨基线/跨编译器比较）。
3. evidence.md §4.1.1"身份＝f(对象闭包, 编译器契约版本)"的推导式表述更正为 runtime 计算值传递；runtime.md §10.3 落点表述同步更正（复现块仅 `compilerContractVersion`）。

**联合契约测试**（RT-T09 ↔ EV-T04，共享同一样例）：固定 CanonicalModel 夹具下，runtime 计算的 `modelIdentity` 与 evidence 切片 Environment 条目值逐字节一致；仅改变基线版本字符串 → `sliceId` 与 `inputBaselineId` 均变化；仅改变求解类 Configuration → `sliceId` 变化而 `inputBaselineId` 不变。

## CR-06 testkit ↔ core 容差接口 —— 已关闭（设计级）；实现级门禁保留

| core（§4.5/§5.5） | testkit（§4.3/§5.3） | 判定 |
|---|---|---|
| `Tolerance{relative,absolute}`＋`make(rel,abs)`（非法抛 CoreError） | `CompareDetail.tolerance: core::Tolerance` 值传递；`ToleranceProfile.resolve(fieldPath)` | 同一类型，无副本 |
| `closeWithin(value, reference, t) noexcept`（C4 公式；非有限→false） | `checkCloseWithin(fieldPath, actualSi, referenceSi, t, sourceTag)`（参数序同构，SI 域） | 语义实现唯一归 core，testkit 只组织与报告——两文档声明一致 |
| `allCloseWithin(Q/vector<double>)`（长度不等抛） | `checkAllCloseWithin(vector<double>…)`（长度不等→失败并枚举差异索引） | 测试侧转报告为失败详情，合理；语义无冲突 |
| `runtimeAbsoluteTolerance(QuantityKind)` | ToleranceProfile C7 对照用例（TK-T04） | 一致 |

**残留门禁（维持原裁决）**：CORE-T04/T05 实现落地后按真实头文件重出 API diff；通过前 TK-T04/TK-T05 保持 `planned` 不得推进 `ready`。

## CR-07 gtest 接入 —— 已关闭（机制定稿）；安装门禁保留

**事实核查（2026-09-10 复核）**：`RobWork/cmake/gtestTargets.cmake` **不存在**（testkit.md 原"实测存在"记载失实）；构建树 `USE_gtest=OFF`；vcpkg installed 无 GTest（ports 有 gtest port）；`RobWork/RobWork/gtest` 等为框架自带源码目录（不消费）。

**裁决**：维持 development-task-breakdown v0.2 §5.5 定稿为唯一机制——`vcpkg install gtest:x64-windows`＋`find_package(GTest CONFIG REQUIRED)`（失败即停，不静默跳过、不回落）；不消费从未生成的 `RW::gtest`、不源码 vendor、不混用两份 gtest；测试 main 自持；沿用 `ADD_RW_GTEST` 宏语义。

**修正**：testkit.md §3.2 失实记载更正（P-TK-1 关闭、风险 R-2 消除）；core.md P-ENV-2 关闭；五单元 T01 行 gtest 引用统一改指 breakdown §5.5。

**残留门禁**：vcpkg 安装动作与版本登记随 TK-T01（≙WP-02-T01）执行；`find_package` 失败 → TK-T01 转 `blocked`，不自行引入第二框架。

## CR-08 跨单元错误类型转换边界 —— 已关闭

| 单元 | 错误类型 | 跨边界纪律（文档原文位置） |
|---|---|---|
| core | `CoreError`（§4.10） | 不捕获不吞；不跨进程边界（§6.2） |
| evidence | `EvidenceError`（稳定 token，§3.5） | 不跨进程边界（D-15）；envelope 非法组合在构造边界抛出 |
| policy | `PolicyError`（稳定 token，§3.1） | 评估内部异常→`status=Failed`＋诊断**不抛出**（§9.3/§10.3）；RobWork 异常必须捕获（§6.3） |
| runtime | `RuntimeError`＋`Expected<T,E>` 非抛出查询（§3.4） | RobWork 异常经 `translateRobWorkError`→`DiagnosticRecord`（§8.4） |
| testkit | `TestKitError`（kind 枚举，§6） | 仅测试侧，不进入产品边界 |

统一规则（各文档现状一致，无修正项）：跨单元只传**稳定 token＋`core::DiagnosticRecord`**，L5 适配器负责转译；五类异常互不跨单元边界抛出。

## 任务状态影响汇总（2026-09-10）

| 动作 | 任务 |
|---|---|
| `planned → ready`（构建落位类，冻结规则本就允许） | CORE-T01、EV-T01、POL-T01、RT-T01、TK-T01 |
| 保持 `planned`＋挂 CR 门禁（acceptance 已登记） | TK-T04/TK-T05（CR-06：待 CORE-T04/05 实现后 API diff）、TK-T01（CR-07：find_package 失败→blocked） |
| 契约载体任务（acceptance/designRefs 已补 CR 联合测试条目） | CORE-T05/06/07、EV-T04、EV-T07、RT-T09、POL-T06 |
| 治理收尾 | FOUNDATION-CR-01 完成；新增 DOC-T01（DETAILED-DESIGN.md 过期状态表/示例路径更正）；`tasks/CORE-T04.json`（根目录游离旧版）删除；`foundation-tasks.json` 与 57 份 canonical 任务文件同步（EVI-T01→EV-T01 等不一致修正） |
