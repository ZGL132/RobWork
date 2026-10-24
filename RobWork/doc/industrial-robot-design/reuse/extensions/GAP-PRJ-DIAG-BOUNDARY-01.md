# TxEngine 项目级诊断承接边界：待所有者裁决

REUSE-T25 / WP-17，基线 `c3eb1f8d7a4b9dadccbf6fca275fd4fb1fbc2215`。本提案未批准，不改变公共接口、文案、subject 或模型 owner；不是上游无能力的结论。

## 具体阻断

- `project/src/TxEngine.cpp::makeIgnoredUncommittedRecord/makeStoreCorruptRecord` 直接以 `core::DiagnosticRecord::make` 生成中文 context/cause/action，subject 为 nullopt。B20 矩阵已将正式模板职责列为未决，不得用“数据面”注释替代审查。
- ARCHITECTURE §3.6.3 要求 diagnostics 统一稳定码/正式文案，调用侧仅提供约定参数/对象/因果关联。`project/src/ProjectStoreImpl.cpp::CollectingSink::report` 原样收集并转发记录，未证明它会换用正式文案。
- `diagnostics/src/Factory.cpp::DiagnosticsFactory::create` 已有注册表、严重级、参数模式、文案载体及校验入口，但非 Dev 且无 subject 会抛 SubjectMissing；这两个码在 `DiagCodes.cpp::builtinSpecs` 为 Error/Info，paramSchema 均为空。不能直接强接工厂、伪造 ObjectId、改成 Dev 或塞入未声明参数。
- `reuse-contract.ps1` 的 changedSources 整文件准入要求该文件全部功能已解决并为 migrated/closed（audit-contract §7.1）。因此仅 SDK 错误映射正确还不足以准入；保留 pending，不为本包改变规则。

## 已有复用与缺口边界

沿用 [B20 实审](../../traceability/reuse/REUSE-T01/project-transaction-audit.md) 及其 batch20-search.json：RobWork `rw::core::IOUtil::{readFile,getFilesInFolder,getFileSize}` 不承载 Windows 持久写、唯一 HEAD 提交点或产品诊断协议。本包应继续复用 AtomicFile/IFileOps、ObjectStore、RevisionIndex、codec/core 摘要，不新建事务核心或诊断平台。

[候选与验证](../../traceability/reuse/REUSE-T25/README.md) 仅删除错误的 SDK 数字副本。独立错误身份来自已安装 Windows SDK；不涉及 RobWork/RobWorkSim 扩展。

## 建议的后续裁决与实施边界

建议先由 diagnostics/project 所有者明确“项目级诊断”的合法 subject 及正式文案来源，再在既有诊断入口内形成最小适配；若现有工厂无法表达，另交确切公共语义增量供所有者确认。需涵盖 RecoveryReport 与 IDiagnosticsSink 两个消费者，不改变持久化项目字节或唯一写 owner。此次不预先选择新增虚函数、默认字段、模板系统或宽泛空 subject 豁免。

否决：虚造对象身份；绕开工厂再建码表/文案体系；将上述函数一律标为 business-specific/migrated；拆文件逃过覆盖；降低门禁；顺手修 F-962 或所有 project 诊断。保留旧行为作历史基线不是合规豁免。

后续验收：项目级正例、对象级缺 subject 负例、未知/废弃码与参数模式拒绝、原恢复/打开报告和正式呈现链，以及精确范围独验。当前以上语义/接线验证 **NOT RUN**；生产保持原状，F-962/F-963 均未关闭。本阻断不妨碍无依赖的 RobWork 直接复用包。
