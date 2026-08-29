# 诊断、本地化与日志模块详细方案

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-09；阶段/发布：阶段 A / R1；任务卡：`agent-tasks/WP-09-T01～T05`
- 架构契约：`architecture/public-interfaces.md`（§6、§8）、`architecture/evaluation-semantics.md`（§1）、`architecture/symbol-registry.md`、`architecture/testing-contract.md`

## 1. 模块职责

模块拥有 `Diagnostic`（SYM-DIA-001）、`DiagnosticCategory`（SYM-DIA-002）、`IDiagnosticCatalog` 端口（public-interfaces §6 签名）、**全产品稳定码目录**（`IRD-<AREA>-<NAME>` 唯一登记处，public-interfaces §8）、中文诊断目录与术语表、单层边界映射 `DiagnosticMapper`、分级日志 sink 和脱敏策略。severity 与 `EngineeringStatus` 正交（CTR-DIA-001）：Warning 级诊断不等于工程判定，工程判定只由评估器给出。不负责业务可行性判断、项目持久化、报告渲染和 GUI 布局。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/
  include/sdurws/ird/diagnostics/
    Diagnostic.hpp DiagnosticValue.hpp DiagnosticCode.hpp StableCodeRegistry.hpp
    IDiagnosticCatalog.hpp DiagnosticMapper.hpp ILogSink.hpp LogEvent.hpp LogRedactionPolicy.hpp
  resources/diagnostics.zh-CN.json terminology.zh-CN.json
  src/Diagnostic.cpp DiagnosticJson.cpp StableCodeRegistry.cpp Catalog.cpp DiagnosticMapper.cpp LogSink.cpp Redaction.cpp
  test/DiagnosticSchemaTest.cpp CatalogTermsTest.cpp ErrorMappingTest.cpp RedactedLoggingTest.cpp StaticConsistencyTest.cpp
  testdata/ evidence/
```

CMake target：`sdurws_ird_diagnostics`、`sdurws_ird_diagnostics_test`、`sdurws_ird_diagnostics_contract_test`。允许依赖：WP-03 core（代码前置 WP-03，总纲 §5.2）、Qt Core JSON/Locale、标准库；禁止 Qt Widgets、其他模块私有头、直接写项目 revision、未经脱敏的文件/环境信息。`DiagnosticValue`、`ILogSink/LogEvent/LogRedactionPolicy` 为模块公共未登记符号，仅随本模块使用；公共类型名按符号表使用 `Diagnostic`（v0.2 的 `EngineeringDiagnostic` 为同物旧名，T01 实现时按登记名重命名）。

## 3. 数据与接口

13 字段 Schema 以 public-interfaces §6（CTR-DIA-001）冻结为准：`code`（`IRD-<AREA>-<NAME>`）、`category:Input|Engineering|System`、`severity:Info|Warning|Error`、`retryable:bool`、`subjectObjectId:string|null`、`localName/runtimeScopedName:string|null`、`actual/expected:DiagnosticValue`（有限值）、`action`、`causeCode:string|null`、`messageKey:string`、`context:Map`。构造器校验 code/action 已注册、数值有限、objectId 优先（名称仅显示不得替代）；诊断 JSON 固定 UTF-8、字段顺序与枚举字符串，未知未来 schema 拒绝。

**稳定码登记表**（本模块拥有；severity 为目录默认值 `severityDefault`，mapper 可按边界上下文调整，category/默认值变更须评审）：

| 码 | 首次出现契约 | 类别 | severity |
| --- | --- | --- | --- |
| IRD-PERSIST-UNCOMMITTED | persistence-schema §5 | System | Warning |
| IRD-PERSIST-FUTURE-SCHEMA | persistence-schema §5 | System | Error |
| IRD-PERSIST-LEGACY-FORMAT | persistence-schema §5 | System | Error |
| IRD-PERSIST-SOURCE-MISSING | module-design/persistence.md §4 | Engineering | Error |
| IRD-PERSIST-SOURCE-CHANGED | module-design/persistence.md §4 | Engineering | Error |
| IRD-PERSIST-PATH-ESCAPE | module-design/persistence.md §4 | Input | Error |
| IRD-PERSIST-HASH-MISMATCH | module-design/persistence.md §4 | System | Error |
| IRD-PERSIST-LOCKED | persistence-schema §6 | System | Error |
| IRD-PERSIST-COMMIT-FAILED | module-design/persistence.md §4 | System | Error |
| IRD-RESULT-SLICE-MISMATCH | execution-model §5 | Engineering | Error |
| IRD-RESULT-BRANCH-MISMATCH | execution-model §5 | Engineering | Error |
| IRD-RESULT-DUPLICATE-ATTEMPT | execution-model §5 | System | Error |
| IRD-RESULT-CONFLICT | execution-model §5 | System | Error |
| IRD-EXEC-RESOURCE-BUDGET | execution-model §2 | System | Warning |
| IRD-EXEC-CAPABILITY-UNSUPPORTED | execution-model §1 | Input | Warning |
| IRD-EXEC-ALREADY-TERMINAL | execution-model §1 | Input | Info |
| IRD-EXEC-CHECKPOINT-INCOMPATIBLE | execution-model §4 | System | Error |
| IRD-OPT-UNREGISTERED-BINDING | candidate-compilation §3 | Input | Error |
| IRD-OPT-WRITE-CONFLICT | candidate-compilation §3 | Input | Error |
| IRD-OPT-DOMAIN-VIOLATION | candidate-compilation §4 | Input | Error |
| IRD-OPT-CYCLE | candidate-compilation §3 | Input | Error |
| IRD-OPT-PATCH-REJECTED | candidate-compilation §4 | Input | Error |
| IRD-OPT-STAGE-LOCKED | candidate-compilation §6 | Input | Error |
| IRD-PROJ-BRANCH-MISMATCH | public-interfaces §1 | Input | Error |
| IRD-PROJ-STALE-REVISION | public-interfaces §1 | Input | Error |
| IRD-PROJ-VALIDATION-FAILED | public-interfaces §1 | Input | Error |
| IRD-PROJ-NOTHING-TO-UNDO | public-interfaces §1 | Input | Info |
| IRD-PROJ-NOTHING-TO-REDO | public-interfaces §1 | Input | Info |
| IRD-NAME-AMBIGUOUS | public-interfaces §2 | Input | Error |
| IRD-NAME-UNRESOLVED | public-interfaces §2 | Input | Error |
| IRD-NAME-DUPLICATE-PREFIX | public-interfaces §2 | Input | Error |
| IRD-CORE-VALUE-INVALID | module-design/core-domain.md | Input | Error |
| IRD-CORE-IDENTITY-INVALID | module-design/core-domain.md | Input | Error |
| IRD-CORE-REFERENCE-UNRESOLVED | module-design/core-domain.md | Input | Error |
| IRD-CORE-COMBINATION-ILLEGAL | module-design/core-domain.md（evaluation-semantics §2） | System | Error |
| IRD-CORE-SCHEMA-FUTURE | module-design/core-domain.md | System | Error |
| IRD-RESULT-CORRUPT | module-design/snapshot-result.md（evaluation-semantics §1 读回赋 Corrupt） | System | Error |
| IRD-EVIDENCE-NAME-MISMATCH | agent-tasks/WP-05-T04（触发收窄为 nameMapId 内容不一致） | Engineering | Error |
| IRD-RUNTIME-AXIS-INVALID | module-design/runtime-model.md（canonical-kinematics §5） | Input | Error |
| IRD-RUNTIME-DUAL-OFFSET | module-design/runtime-model.md（canonical-kinematics §3） | System | Error |
| IRD-RUNTIME-NAME-COLLISION | module-design/runtime-model.md | Input | Error |
| IRD-RUNTIME-COMPILE-FAILED | module-design/runtime-model.md | System | Error |
| IRD-RUNTIME-ARTIFACTS-MISMATCH | module-design/runtime-model.md | System | Error |
| IRD-POLICY-CONFLICT | module-design/policy-collision.md | Input | Error |
| IRD-POLICY-PAIR-OVERLAP | module-design/policy-collision.md（同一对象对同时属 excluded 与 allowed） | Input | Error |
| IRD-POLICY-UNRESOLVED-OBJECT | module-design/policy-collision.md | Input | Error |
| IRD-POLICY-BACKEND-UNAVAILABLE | module-design/policy-collision.md | System | Error |
| IRD-EXEC-REQUEST-INVALID | module-design/execution-platform.md | Input | Error |
| IRD-EXEC-WORKER-LOST | module-design/execution-platform.md（execution-model §1 区分 Failed/Canceled） | System | Error |
| IRD-EXEC-ILLEGAL-TRANSITION | module-design/execution-platform.md（execution-model §1） | System | Error |
| IRD-UI-PASTE-INVALID | module-design/session-ui.md | Input | Warning |
| IRD-UI-PROJECTION-FAILED | module-design/session-ui.md | System | Error |
| IRD-MDL-CONVERSION-FAILED | module-design/robot-modeling.md（AnalysisFailed 场景） | System | Error |
| IRD-MDL-CONVERSION-INEXACT | module-design/robot-modeling.md（Approximate 只读投影） | Engineering | Warning |
| IRD-MDL-IMPORT-BLOCKED | module-design/robot-modeling.md（URDF 非法轴/不支持关节阻止修订） | Input | Error |
| IRD-MDL-PROPERTIES-INSUFFICIENT | module-design/robot-modeling.md | Engineering | Warning |
| IRD-MDL-TOOL-REF-UNRESOLVED | module-design/robot-modeling.md | Input | Error |
| IRD-REQ-NOT-READY | module-design/requirements-definition.md（REQ-06 就绪校验） | Input | Warning |
| IRD-REQ-REFERENCE-UNRESOLVED | module-design/requirements-definition.md | Input | Error |
| IRD-REQ-ROW-INVALID | module-design/requirements-definition.md（CSV 逐行错误） | Input | Error |
| IRD-REQ-SAMPLING-BUDGET | module-design/requirements-definition.md（区域组合上限） | Engineering | Error |
| IRD-KIN-IK-NO-SOLUTION | module-design/kinematics.md | Engineering | Warning |
| IRD-KIN-SOLVER-FAILED | module-design/kinematics.md | System | Error |
| IRD-KIN-JOINT-LIMIT | module-design/kinematics.md（候选过滤） | Engineering | Warning |
| IRD-KIN-COLLIDING | module-design/kinematics.md（候选过滤） | Engineering | Warning |
| IRD-KIN-LSTAR-INVALID | module-design/kinematics.md（§15.3 L* 回退失败→DataInsufficient） | Engineering | Warning |
| IRD-KIN-EVIDENCE-MISSING | module-design/kinematics.md（KIN-05 无检测器→数据不足） | Engineering | Warning |
| IRD-KIN-CONTINUOUS-UNBOUNDED | module-design/kinematics.md（Continuous 关节未确认工程工作范围，排序归一跳过） | Input | Info |
| IRD-TRJ-NO-PATH | module-design/trajectory-planning.md | Engineering | Warning |
| IRD-TRJ-PLANNER-FAILED | module-design/trajectory-planning.md | System | Error |
| IRD-TRJ-PLANNER-TIMEOUT | module-design/trajectory-planning.md | System | Error |
| IRD-TRJ-BRANCH-JUMP | module-design/trajectory-planning.md（IK 连续性） | Engineering | Warning |
| IRD-TRJ-SINGULARITY | module-design/trajectory-planning.md | Engineering | Warning |
| IRD-TRJ-TIME-PARAM-FAILED | module-design/trajectory-planning.md | Engineering | Error |
| IRD-TRJ-VALIDATION-REJECTED | module-design/trajectory-planning.md（避障复检） | Engineering | Warning |
| IRD-TRJ-UPSTREAM-MISSING | module-design/trajectory-planning.md | Input | Error |
| IRD-DYN-FD-NOT-CONVERGED | module-design/dynamics.md（h/h2 收敛判据） | System | Error |
| IRD-DYN-FD-DIVERGED | module-design/dynamics.md | System | Error |
| IRD-DYN-FRICTION-MISSING | module-design/dynamics.md | Engineering | Warning |
| IRD-DYN-INERTIA-INVALID | module-design/dynamics.md（正定性/三角不等式） | Input | Error |
| IRD-DYN-PROPERTIES-MISSING | module-design/dynamics.md（→DataInsufficient） | Engineering | Warning |
| IRD-DYN-STATE-DISCONTINUOUS | module-design/dynamics.md | System | Error |
| IRD-DYN-UPSTREAM-MISSING | module-design/dynamics.md | Input | Error |
| IRD-DTM-RATIO-INVALID | module-design/drivetrain.md | Input | Error |
| IRD-DTM-EFFICIENCY-MISSING | module-design/drivetrain.md（→DataInsufficient 分项） | Engineering | Warning |
| IRD-DTM-REVERSE-EFFICIENCY-MISSING | module-design/drivetrain.md | Engineering | Warning |
| IRD-DTM-INERTIA-INVALID | module-design/drivetrain.md | Input | Error |
| IRD-DTM-ROTARY-ONLY | module-design/drivetrain.md（SEL-09 首版范围） | Engineering | Error |
| IRD-SEL-CATALOG-UNAVAILABLE | module-design/device-selection.md | System | Error |
| IRD-SEL-CURVE-OUT-OF-RANGE | module-design/device-selection.md（禁止外推） | Input | Error |
| IRD-SEL-DERATING-MISSING | module-design/device-selection.md（→DataInsufficient，不默认无降额） | Engineering | Warning |
| IRD-SEL-PAIR-NOT-LISTED | module-design/device-selection.md（Compatibility 外键） | Input | Error |
| IRD-SEL-ALL-ELIMINATED | module-design/device-selection.md（离散组合为空） | Engineering | Warning |
| IRD-SEL-UPSTREAM-MISSING | module-design/device-selection.md | Input | Error |
| IRD-SEL-TRANSMISSION-OUT-OF-SCOPE | module-design/device-selection.md（移动关节阻断） | Engineering | Error |
| IRD-WF-NOT-COMPAREABLE | module-design/workflow-integration.md | Input | Warning |
| IRD-WF-EVIDENCE-MISSING | module-design/workflow-integration.md | Engineering | Warning |
| IRD-WF-APPLY-BLOCKED | module-design/workflow-integration.md | Input | Error |
| IRD-INST-PATH-LEAK | module-design/installation-release.md（开发机路径残留） | System | Error |
| IRD-INST-WHITELIST-MISMATCH | module-design/installation-release.md | System | Error |
| IRD-INST-INVENTORY-INCOMPLETE | module-design/installation-release.md | System | Error |

模块自有码（`IRD-<AREA>-*`）由各 v0.3 方案提名，经本目录登记后方可使用；新增码不得与既有码同义。目录条目含 `code`、`messageKey`、`titleZhCN/detailZhCN`、`action`、`severityDefault`、`allowedTokens[]`；`terminology.zh-CN.json` 含 canonical key、中文词、单位和禁用同义词。本地化策略：代码、稳定码和持久化字段用英文，用户可见中文仅由本目录与术语表提供（需求 §10.3）；首版只交付 zh-CN，`messageKey` 与术语 key 稳定，不随文案调整变化。

## 4. 调用与状态

```text
boundary error -> DiagnosticMapper::map(cause, context)（唯一一次映射）-> immutable Diagnostic
  -> catalog lookup(messageKey, zh-CN) -> 用户呈现 / LogSink(level, IDs, redactedContext) -> evidence JSON/JSONL
```

| 码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| IRD-DIA-SCHEMA-INVALID | 构造拒绝：缺字段、未注册 code/action、NaN/Infinity、名称替代 objectId | Input | Error | 修正构造输入后重建诊断 |
| IRD-DIA-CATALOG-INVALID | 启动校验失败：重复 code、缺语言项、未知 action、未声明占位符、孤立条目 | System | Error | 修复资源后重启；阻止进入运行态 |
| IRD-DIA-REDACT-FAILED | 脱敏器自身异常 | System | Error | 整体省略 context，保留 code 与 objectId |

## 5. 关键实现约定

- 单层映射：真实边界（文件导入、RobWork 适配、worker/IPC、报告渲染、持久化读写、缓存/检查点反序列化）各调用 mapper 一次；相同 causeCode + context kind 得到相同 code/category/action；上层只附加关联 ID，不重复包装（需求 §12）。
- 脱敏与日志分级：默认隐藏 Windows 用户目录、UNC/网络共享、环境变量值、令牌、密码、连接串、完整内部 hash 和调用栈；路径只保留配置允许的项目相对路径或 basename；崩溃转储用独立文件权限与引用 ID；脱敏幂等、不可逆且保留 objectId 定位；用户日志只输出 title/detail/action、对象显示名和安全相对路径，开发日志可含 project/branch/revision/run/attempt/snapshot 关联 ID、算法版本和统计（NFR-REL-05、NFR-SEC-07）。

## 6. 测试与证据

| 测试 | 覆盖 | 目标 |
| --- | --- | --- |
| DiagnosticSchemaTest | 缺字段、非法类别、未知 action、NaN/Infinity、名称替代 ID、JSON 往返 | `sdurws_ird_diagnostics_test` |
| CatalogTermsTest | 重复 code、缺 key、占位符、启动失败、登记表与资源一致 | 同上 |
| ErrorMappingTest | 六类边界及跨入口一致性 | 同上 |
| RedactedLoggingTest | 用户目录、UNC、环境变量、令牌、转储引用、幂等 | 同上 |
| StaticConsistencyTest | 硬编码状态词与重复 code 静态扫描 | 同上 |
| DiagnosticCatalogContractTest | lookup/codesByCategory 失败/正常/边界三例 | `sdurws_ird_diagnostics_contract_test` |

验证命令（脚本与原生双形式，均在仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_diagnostics_test sdurws_ird_diagnostics_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_diagnostics(_contract)?_test$"
```

证据包含原始 cause、最终诊断 JSON、目录版本、脱敏前后样本、关联 ID、扫描报告和独立评审签名。

## 7. 迁移与删除表

| 旧资产 | 处置 | 说明 |
| --- | --- | --- |
| 各插件字符串错误消息 | Rewrite | adapter 只读过渡；证明 code/category/action 语义一致后迁入目录 |
| 分散中文文案与同义状态词 | Delete | 统一并入 `terminology.zh-CN.json`，禁用同义词删除 |
| 输出绝对路径/调用栈/内嵌转储的旧日志与崩溃报告 | Rewrite | 经 Redactor 处理；转储改独立权限文件 + 引用 ID |
