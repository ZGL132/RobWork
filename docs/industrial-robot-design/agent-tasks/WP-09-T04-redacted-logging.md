# WP-09-T04 分级日志与脱敏

- **Task ID / 需求 ID / ADR / 阶段：**WP-09-T04；UX-06、NFR-REL-05、NFR-SEC-07；无新 ADR（脱敏规则权威 requirements §10.3/§12 与 module-design/diagnostics.md §6）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线日志经 `rw::core::Log` 输出绝对路径与环境信息，崩溃转储内嵌日志）；契约 `architecture/public-interfaces.md` §6；方案 `module-design/diagnostics.md` v0.3 §5/§6
- **前置任务及必需工件：**WP-09-T01（Diagnostic 构造与 objectId 保留）、WP-09-T02（目录与用户文案来源）、WP-08-T02（RunIdentity 关联字段：project/branch/revision/run/attempt/snapshot）、WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/` 下 `include/sdurws/ird/diagnostics/{ILogSink.hpp,LogEvent.hpp,LogRedactionPolicy.hpp}`、`src/{LogSink.cpp,Redaction.cpp}`、`test/RedactedLoggingTest.cpp`、`testdata/diagnostics/redaction/`
- **禁止修改的文件和公共接口：**输出 token/password/连接串、未经配置允许的绝对路径、完整调用栈或崩溃转储内容；丢失 objectId；`ILogSink/LogEvent/LogRedactionPolicy` 为模块公共未登记符号仅随本模块使用；不得改动其他模块日志调用约定之外的接口
- **修改前接口：**无分级 sink 与脱敏器；日志字符串直接含绝对路径、环境变量值与堆栈
- **修改后接口：**`ILogSink` 按级别接收 `LogEvent`（关联 ID＋脱敏后 context）；`Redaction` 幂等、不可逆；崩溃转储使用独立文件权限与引用 ID，日志仅嵌引用；`IRD-DIA-REDACT-FAILED` 时整体省略 context 并保留 code 与 objectId
- **实施步骤：**定义级别与 LogEvent → 实现脱敏规则（Windows 用户目录、UNC/网络共享、环境变量值、令牌、密码、连接串、完整内部 hash、调用栈；路径仅保留配置允许的项目相对路径或 basename）→ 用户/开发双通道渲染 → 转储引用机制
- **RED 测试：**`test/RedactedLoggingTest.cpp`（注册于 `sdurws_ird_diagnostics_test`）：含令牌/密码/连接串/用户目录/UNC/环境变量值的样本脱敏后替换为稳定占位符且二次脱敏结果不变（幂等）、不可逆——先确认测试在无实现时失败
- **最小实现：**脱敏器＋双通道 sink＋转储引用；不做日志采集平台
- **正常/边界/失败测试：**正常：开发事件保留 project/branch/revision/run/attempt/snapshot 关联 ID、算法版本与统计，不写凭据或完整 hash；用户日志仅含 title/detail/action、对象显示名与安全相对路径。边界：路径恰为项目相对路径（保留）vs 绝对路径（basename 化）、重复脱敏幂等、转储引用可回查。失败：脱敏器自身异常 → `IRD-DIA-REDACT-FAILED` 且保留 code/objectId；任何凭据泄漏样例命中 → 测试失败
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_diagnostics_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_diagnostics(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`rg -n "getenv|PASSWORD|token" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src` 仅脱敏规则命中；样本日志无原始凭据回显；转储文件权限设置记录在案
- **证据工件：**`diagnostics/out/test-evidence/wp-09/<run-id>/`：脱敏前后样本对照、日志级别矩阵、关联 ID 样例、崩溃转储引用记录、命令日志与评审签名
- **提交格式：**`WP-09-T04: 新增分级日志与脱敏`

  - 新增 LogSink 双通道分级与幂等不可逆脱敏实现
  - 新增 脱敏样本测试与目标登记
  - 新增 脱敏前后对照与转储引用证据记录
- **停止与升级条件：**脱敏导致 objectId 无法定位、或发现任何凭据泄漏路径时立即暂停并升级至安全负责人，不得继续提交
