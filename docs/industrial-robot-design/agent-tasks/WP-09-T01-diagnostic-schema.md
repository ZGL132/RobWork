# WP-09-T01 诊断 Schema 与字段完整性

- **Task ID / 需求 ID / ADR / 阶段：**WP-09-T01；ERR-01、UX-03、NFR-REL-05、NFR-MNT-03、NFR-SEC-07；ADR-005（severity 与 EngineeringStatus 正交）、ADR-004（诊断为共享语义权威）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（无 industrialrobot/diagnostics 目录；基线错误为各插件字符串 `rw::core::Log` 输出）；契约 `architecture/public-interfaces.md` §6（CTR-DIA-001 冻结）、`architecture/symbol-registry.md`（SYM-DIA-001/002）；方案 `module-design/diagnostics.md` v0.3 §3
- **前置任务及必需工件：**WP-03-T01（受限值类型）、WP-03-T02（ObjectId）、WP-01-T02（`sdurws_ird_diagnostics` 目标骨架）、WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/` 下 `include/sdurws/ird/diagnostics/{Diagnostic.hpp,DiagnosticValue.hpp,DiagnosticCode.hpp}`、`src/{Diagnostic.cpp,DiagnosticJson.cpp}`、`test/DiagnosticSchemaTest.cpp`、`testdata/diagnostics/schema/`
- **禁止修改的文件和公共接口：**public-interfaces §6 的 13 字段冻结集与枚举值域、requirements 错误类别规则、业务模块错误判断、其他模块公共接口、日志输出策略、手工 CSV；`EngineeringDiagnostic` 为 v0.2 禁用旧名不得再出现
- **修改前接口：**无 `Diagnostic` 类型；错误以字符串日志和返回码表达，无 code/category/severity/action 结构
- **修改后接口：**`Diagnostic`（SYM-DIA-001，v0.2 旧名 `EngineeringDiagnostic` 按登记名重命名）13 字段不可变：`code/category/severity/retryable/subjectObjectId/localName/runtimeScopedName/actual/expected/action/causeCode/messageKey/context`；`category: Input|Engineering|System`；`severity: Info|Warning|Error` 三值（缺陷严重级别 Blocker/Critical/Major/Minor 为独立口径不进入 severity）
- **实施步骤：**定义枚举与值类型 → 构造器校验（code/action 已注册、数值有限、objectId 优先、名称仅显示不得替代）→ 固定字段顺序与枚举字符串的 JSON 序列化 → 往返反序列化 → 未知未来 schema 版本拒绝
- **RED 测试：**`test/DiagnosticSchemaTest.cpp`（注册于 `sdurws_ird_diagnostics_test`）：缺 code/actual/expected/action、非法 category（第四类）、非法 severity（三值之外）、NaN/Infinity、名称替代 objectId → 构造拒绝并返回 `IRD-DIA-SCHEMA-INVALID`（Input）——先确认测试在无实现时失败
- **最小实现：**13 字段值对象＋构造校验＋确定性 JSON；不含目录查找（T02）与边界映射（T03）
- **正常/边界/失败测试：**正常：合法 13 字段 JSON 往返后字段、枚举与关联 ID 完全一致且输出 UTF-8 固定顺序。边界：缺 runtime name 时 localName/runtimeScopedName 可空、缺 objectId 显式为空但不用名称替代、context 空映射。失败：上述构造拒绝路径、未知 schema 版本拒绝且不静默降级
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_diagnostics_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_diagnostics(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`rg -n "EngineeringDiagnostic" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；无 `float` NaN 直通序列化路径；无第四 category/severity 枚举值；severity 不与 EngineeringStatus 混用（无转换函数）
- **证据工件：**`diagnostics/out/test-evidence/wp-09/<run-id>/`：13 字段矩阵、JSON 样例（正常与拒绝各一组）、`IRD-DIA-SCHEMA-INVALID` 诊断样例、命令日志与评审签名
- **提交格式：**`WP-09-T01: 新增诊断 Schema 与字段完整性`

  - 新增 Diagnostic 13 字段不可变值对象与确定性 JSON 序列化
  - 新增 构造拒绝路径测试与目标登记
  - 新增 字段矩阵与 JSON 样例证据记录
- **停止与升级条件：**字段数量、可空性或错误类别与 public-interfaces §6 不一致时暂停并升级至 ADR-004/005 所有者，不得增删字段
