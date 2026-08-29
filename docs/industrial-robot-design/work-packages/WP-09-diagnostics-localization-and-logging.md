# WP-09 诊断、本地化与日志实施计划

> 阶段/发布：阶段 A / R1；诊断公共接口所有者：WP-09。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 建立唯一诊断 Schema、稳定诊断码、简体中文术语和单层错误映射，使所有入口对同一故障返回相同 code/category/action，同时确保用户日志、开发日志和崩溃证据不泄露敏感信息。

## 1. 目标与非目标

交付不可变 `Diagnostic`（SYM-DIA-001 登记名；`EngineeringDiagnostic` 为 v0.2 旧名，module-design/diagnostics.md 裁决）、诊断目录、术语表、边界 mapper、分级日志 sink、脱敏策略和静态一致性扫描。诊断必须可定位对象、实际值、期望值、原因和建议动作；名称仅作显示，不能替代 objectId。

不实现业务错误判断、各模块私有状态、报告渲染、GUI 文案布局、日志采集平台或新的错误类别。

## 2. 需求、契约和发布切片

- 需求：ERR-01、UX-02、UX-03、UX-06、NFR-REL-05、NFR-MNT-03、NFR-SEC-07。
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/testing-contract.md`、`architecture/execution-model.md`。
- 模块方案：`module-design/diagnostics.md`。
- 阶段/发布：阶段 A / R1；后续 WP 只能引用本模块 code/category/action，不自行新增同义码。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/`，含 `include/sdurws/ird/diagnostics/`、`resources/`、`src/`、`test/`、`testdata/`、`evidence/`。允许 WP-03 core、Qt Core JSON/Locale 和标准库；禁止 Qt Widgets、模块私有诊断 Schema、直接写项目 revision、日志中输出凭据或手工 CSV。

目标：`sdurws_ird_diagnostics`、`sdurws_ird_diagnostics_test`、`sdurws_ird_diagnostics_contract_test`。

## 4. 诊断字段契约

固定 13 字段：`code`、`category`、`severity`、`retryable`、`subjectObjectId`、`localName`、`runtimeScopedName`、`actual`、`expected`、`action`、`causeCode`、`messageKey`、`context`。类别只有 `Input`、`Engineering`、`System`；severity 为 `Info/Warning/Error` 三值（`architecture/public-interfaces.md` §6 冻结；缺陷严重级别 Blocker/Critical/Major/Minor 是独立口径，不进入诊断 severity）；缺名称时可空，缺 objectId 时显式为空但不得用名称替代。`actual/expected` 使用受限值类型，禁止 NaN/Infinity。

诊断 JSON 固定 UTF-8、字段顺序和枚举字符串；未知未来 schema 拒绝。`causeCode` 保留底层原因，边界 mapper 只映射一次，不重复包装同义诊断。错误类别规则：用户数据/参数为 Input，工程不可行/数据不足为 Engineering，文件、进程、第三方和版本故障为 System。

## 5. 目录、本地化和日志数据流

```text
boundary error -> DiagnosticMapper (one mapping) -> immutable diagnostic
  -> catalog lookup(messageKey, zh-CN) -> user presentation
  -> LogSink(level, correlation IDs, redacted context)
  -> evidence JSON/JSONL with schema/version
```

`diagnostics.zh-CN.json` 为 code→messageKey/title/detail/action 的唯一中文目录；`terminology.zh-CN.json` 为单位、关节、姿态、碰撞、证据和任务术语。启动时检查重复 code、缺 messageKey、未知 action、未声明占位符和孤立条目。用户日志只保留可行动信息；开发日志可含 project/branch/revision/run/attempt/snapshot，但路径、令牌、密码、环境变量值和转储内容必须脱敏。

## 6. 脱敏规则

默认隐藏 Windows 用户目录、UNC/网络共享、环境变量值、访问令牌、密码、连接串、完整内部 hash 和调用栈；路径只保留配置允许的项目相对路径或 basename。崩溃转储使用独立文件权限和引用 ID，日志不嵌入转储内容。脱敏必须幂等、不可逆且保留诊断定位所需 objectId。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-09-T01 | 13 字段 Schema、值类型和 JSON | [T01](../agent-tasks/WP-09-T01-diagnostic-schema.md) |
| WP-09-T02 | 中文诊断目录、术语和启动校验 | [T02](../agent-tasks/WP-09-T02-catalog-terms.md) |
| WP-09-T03 | 导入/适配/worker/报告单层 mapper | [T03](../agent-tasks/WP-09-T03-error-mapping.md) |
| WP-09-T04 | 用户/开发日志、路径和转储脱敏 | [T04](../agent-tasks/WP-09-T04-redacted-logging.md) |
| WP-09-T05 | 硬编码状态词、重复 code 静态扫描 | [T05](../agent-tasks/WP-09-T05-static-consistency.md) |

依赖：T01 → T02 → T03；T04 依赖 T01/T02；T05 依赖 T01/T02。每张任务卡一个 worktree、分支和提交。

## 7. 失败分类与证据

- 输入错误：缺字段、非法值、未知 code/action；返回 Input 诊断并保留原始上下文的安全摘要。
- 工程不可行：数据不足、约束不满足；返回 Engineering，不包装成 System。
- 系统错误：文件/进程/第三方故障；保留 causeCode、retryable 和建议动作，不泄露内部堆栈给用户。

证据必须含诊断 schema/version、code/category/severity/action、subjectObjectId、messageKey、causeCode、脱敏前后测试样本、关联 IDs、命令日志和独立评审签名。

## 验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_diagnostics_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

## 8. 迁移

旧字符串错误先由只读 adapter 映射；无法证明 code/category/action 一致的标 Rewrite/EvidenceOnly。

## 退出条件

每个诊断可定位对象、实际值、期望值、原因和动作；同一故障跨入口稳定一致；用户日志、开发日志和崩溃转储无凭据、令牌及未允许的绝对路径；5 张任务卡证据和独立评审齐全。
