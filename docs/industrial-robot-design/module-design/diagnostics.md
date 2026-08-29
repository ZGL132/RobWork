# 诊断、本地化与日志模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-09；阶段/发布：阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`、`architecture/execution-model.md`

## 1. 模块职责与目录

模块拥有诊断值对象、目录解析、错误边界映射、日志 sink 和脱敏策略；消费者只引用稳定 code/action，不自行构造用户文案。模块不判断业务可行性、不保存项目文件、不渲染报告或 UI。

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/
  include/sdurws/ird/diagnostics/
    EngineeringDiagnostic.hpp DiagnosticValue.hpp
    IDiagnosticCatalog.hpp DiagnosticMapper.hpp
    ILogSink.hpp LogEvent.hpp LogRedactionPolicy.hpp
  resources/diagnostics.zh-CN.json terminology.zh-CN.json
  src/Diagnostic.cpp DiagnosticJson.cpp Catalog.cpp
      DiagnosticMapper.cpp LogSink.cpp Redaction.cpp
  test/DiagnosticSchemaTest.cpp CatalogTermsTest.cpp
      ErrorMappingTest.cpp RedactedLoggingTest.cpp StaticConsistencyTest.cpp
```

目标：`sdurws_ird_diagnostics`、`sdurws_ird_diagnostics_test`、`sdurws_ird_diagnostics_contract_test`。允许 WP-03 core、Qt Core 和标准库；禁止 Qt Widgets、跨模块私有错误码和未经脱敏的文件/环境信息。

## 2. 13 字段诊断 Schema

字段依次为 `code:string`、`category:Input|Engineering|System`、`severity:Info|Warning|Error|Critical`、`retryable:bool`、`subjectObjectId:string|null`、`localName:string|null`、`runtimeScopedName:string|null`、`actual:DiagnosticValue`、`expected:DiagnosticValue`、`action:ActionCode`、`causeCode:string|null`、`messageKey:string`、`context:Map`。构造器验证 code/action 注册、有限数和 objectId 优先级；对象无运行时名称时名称字段为空。

## 3. 单层错误映射

真实边界（文件导入、RobWork 适配、worker/IPC、报告渲染）调用 `DiagnosticMapper::map(cause, context)` 一次。mapper 保留 `causeCode`，设置稳定 category/severity/retryable/action/messageKey；上层只附加关联 ID，不重新包装。相同 causeCode + context kind 必须得到相同 code/category/action。

## 4. 目录和术语

诊断目录条目包含 `code`、`messageKey`、`titleZhCN`、`detailZhCN`、`action`、`severityDefault`、`allowedTokens[]`。术语目录包含 canonical key、中文词、单位和禁用同义词。启动校验重复 code、缺语言项、未知 action、未声明替换标记和未使用条目；失败阻止应用进入运行态。

## 5. 日志与脱敏

`LogEvent` 必填 timestamp、level、component、eventCode、messageKey、projectId/branchId/revisionId/runId/attemptId/snapshotId（可空）、diagnosticCode、redactedContext。用户日志只输出 title/detail/action、对象显示名和安全相对路径；开发日志可输出关联 ID、算法版本和统计，但禁止 token/password/connection string、完整内部 hash、调用栈和未经允许的绝对路径。Redactor 先识别凭据/环境变量/用户目录/UNC，再替换为稳定占位符；多次处理结果相同。

## 6. 测试与证据

Schema 测试覆盖缺字段、非法类别、未知 action、NaN/Infinity、名称代替 ID 和 JSON 往返；目录测试覆盖重复 code、缺 key、占位符和启动失败；映射测试覆盖四类边界及跨入口一致性；脱敏测试覆盖 Windows 用户目录、UNC、环境变量、令牌、崩溃转储；扫描测试只允许测试期望字符串和目录资源出现硬编码词。

证据包含原始 cause、最终诊断 JSON、目录版本、脱敏前后样本、日志关联 ID、扫描报告和独立评审签名。

## 7. 迁移与评审

旧字符串错误以 adapter 过渡，未证明语义一致时标 Rewrite/EvidenceOnly。评审确认 13 字段完整、单层映射、中文术语唯一、用户/开发日志分离、脱敏不可逆且不丢 objectId 定位信息。
