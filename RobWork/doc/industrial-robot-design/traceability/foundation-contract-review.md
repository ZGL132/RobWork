# 基础单元公共契约审查记录

| 字段 | 值 |
|---|---|
| 范围 | core、evidence、policy、runtime、testkit |
| 审查状态 | `contract-review`（设计级关闭完成 2026-09-10，见 §4；五单元转 `frozen` 仍待联合契约测试通过） |
| 审查目标 | 在开始实现 T01 前冻结跨单元公共类型、端口、身份和错误边界 |
| 权威依据 | REQUIREMENTS.md v1.16、ARCHITECTURE.md v0.11、五份 units 详设 |

## 1. 已对齐契约

| 契约 | 所有者 | 消费者 | 结论 |
|---|---|---|---|
| ObjectId、ContentVersion、ContentIdentity、ContentDigester | core | evidence、policy、runtime、testkit | core 唯一定义，消费者只引用 |
| Tolerance、UnitToken、QuantityKind、closeWithin | core | policy、testkit、业务单元 | core 唯一实现，testkit 只承载档案和断言 |
| DiagnosticRecord、ComparativeFields、SourcedValue | core | evidence、policy、testkit | core 定义值契约，diagnostics 后续拥有码表和文案 |
| AnalysisSnapshot、InputSlice、ResultEnvelope、Currentness | evidence | execution、reporting、业务域 | evidence 唯一定义，project 只保存字节和引用 |
| EngineeringPolicySet、PolicyCodec、ICollisionEvaluator | policy | evidence、runtime、业务域 | policy 唯一定义，业务域不得复制碰撞算法或策略默认值 |
| CanonicalModel、RuntimeNameMap、RuntimeSnapshot | runtime | modeling、policy、execution、evidence | runtime 唯一定义，消费端通过端口或注入访问 |
| DatasetManifest、ToleranceProfile、CheckResult | testkit | 各单元测试目标 | testkit 仅测试侧使用，不进入产品目标 |

## 2. 必须在实现前解决的交接项

| 编号 | 交接项 | 当前问题 | 处理结论 |
|---|---|---|---|
| CR-01 | core 词表与 evidence ResultEnvelope 状态 | core 负责 outcome/engineeringStatus 词表，evidence 负责组合校验；两份文档必须使用同一 token 集 | 进入 CORE-T06/CORE-T07 与 EV-T07 的联合契约测试；禁止各自定义枚举 |
| CR-02 | core ContentIdentity 与 evidence/runtime/policy 编码 | 三个单元都有 canonical 编码和内容身份，需要明确摘要输入边界 | 各单元保留本地编码，但摘要算法只调用 core ContentDigester；每个编码器必须声明排除字段 |
| CR-03 | evidence 与 project 对象闭包 | evidence 需要只读对象字节和修订闭包，project 详设提供端口但历史文档曾不完整 | 以 `IObjectBytesSource`、`IRevisionClosureSource` 最小接口为唯一交接；project 完整设计复核前不扩大接口 |
| CR-04 | policy 与 runtime 碰撞场景 | policy 需要 RuntimeNameMap/场景对象，runtime 不应暴露 RobWork 私有实现 | 只经 `IPolicyNameContext` 和值句柄 `CollisionScene`；禁止 policy 直接依赖 runtime 库 |
| CR-05 | runtime 与 evidence 快照身份 | RuntimeSnapshot 的 CanonicalModel 身份必须作为 evidence InputSlice 依赖参与计算 | 在 RT-T09、EV-T04 建立同一身份样例；不允许 evidence 自行重新编码 CanonicalModel |
| CR-06 | testkit 与 core 容差接口 | testkit 当前消费 core v0.1，尚未有冻结签名 | CORE-T04/05 完成后生成 API diff；TK-T04/05 只有在 diff 通过后才能 ready |
| CR-07 | gtest 接入 | 五份文档仍保留 P-TK-1/P-ENV-2 的历史未决描述 | 已按 §4 关闭：经 vcpkg 安装 gtest，统一 `find_package(GTest CONFIG REQUIRED)`；失败转 blocked，不引入第二渠道 |
| CR-08 | 错误类型跨单元转换 | core/evidence/policy/runtime/testkit 各自拥有错误类型，尚未统一边界 | 内部错误保留各自类型；跨单元只通过稳定 token + 结构化诊断转换，不跨边界抛出对方异常类型 |

## 3. 冻结规则

- CR-01～CR-08 未关闭前，五个单元只能执行构建落位、头文件骨架和契约测试夹具任务。
- 任何接口修改必须同时更新本文件、对应 unit 详设、任务 JSON 和追踪矩阵。
- 需求或架构语义冲突必须标记任务 `blocked`，不能在实现中隐式裁决。
- 五个单元只有在全部 CR 项关闭、契约测试通过后，状态才能从 `contract-review` 变为 `frozen`。

## 4. 关闭记录（2026-09-10，FOUNDATION-CR-01 执行）

逐项差异比对与接口原文对照见 [foundation-api-diff.md](foundation-api-diff.md)。结论：

| 编号 | 结论 | 关键动作 | 残留门禁 |
|---|---|---|---|
| CR-01 | 已关闭 | 词表唯一归 core；EV-T07 契约测试断言无本地枚举、token 与 `core::toToken` 一致 | 无 |
| CR-02 | 已关闭 | 四家编码器纪律一致；摘要只经 core ContentDigester；排除字段登记入差异记录 | 无 |
| CR-03 | 已关闭 | 最小接口与 project §5.2 现文一比一适配；project 当前正文已完整，联合接口复核仍待执行 | project 完整设计复核（不阻塞五基础单元） |
| CR-04 | 已关闭 | policy↔runtime 映射成立（场景身份取 `workCellCompileIdentity`；WorkCell 共享只读经快照别名构造）；policy.md"runtime 未产出"过期记载已更正，P-POL-1 关闭 | 无 |
| CR-05 | 已关闭（含冲突修正） | 唯一实质冲突：evidence 缺 `modelIdentity`/`robworkBaselineVersion` 槽位且声称可自行推导。裁决＝Environment 新增 `runtime.model-identity`/`runtime.robwork-baseline`（runtime 计算值传递、必填、入 inputBaselineId）；evidence.md §4.1.1/§4.2.1/§5.1 与 runtime.md §10.3 已修正 | RT-T09↔EV-T04 联合身份样例（随实现） |
| CR-06 | 已关闭（设计级） | 签名/语义无冲突；语义实现唯一归 core | TK-T04/TK-T05 待 CORE-T04/05 实现后 API diff 通过方可 ready |
| CR-07 | 已关闭（机制定稿） | 实测 `RW::gtest` 从未生成；维持 breakdown §5.5 定稿（vcpkg＋`find_package(GTest CONFIG REQUIRED)`）；testkit.md/core.md 失实记载已更正，P-TK-1/P-ENV-2 关闭 | vcpkg 安装＋版本登记随 TK-T01；find_package 失败→TK-T01 转 blocked |
| CR-08 | 已关闭 | 五类错误类型边界登记；跨单元只传稳定 token＋DiagnosticRecord | 无 |

状态影响：CORE-T01/EV-T01/POL-T01/RT-T01/TK-T01 → `ready`（构建落位类，冻结规则本就允许）；TK-T04/05 保持 `planned` 挂 CR-06 门禁；`tasks/CORE-T04.json`（根目录游离旧版）删除；`foundation-tasks.json` 与 canonical 任务文件同步；DETAILED-DESIGN.md 过期状态表/示例路径更正登记为 DOC-T01。
