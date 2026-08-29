# WP-09-T03 边界错误单层映射

- **Task ID / 需求 ID / ADR / 阶段：**WP-09-T03；ERR-01、UX-03、NFR-REL-05、NFR-MNT-03；ADR-004（单层映射为共享语义权威）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线各层重复包装错误字符串，同一故障不同入口文案不一致）；契约 `architecture/public-interfaces.md` §6/§8；方案 `module-design/diagnostics.md` v0.3 §3（约 100 码稳定码登记表）/§5
- **前置任务及必需工件：**WP-09-T01（Diagnostic/causeCode）、WP-09-T02（目录与 severityDefault）；六类真实边界中的阶段 A 已交付者：RobWork 适配（WP-06 `IRD-RUNTIME-*`）、worker/IPC（WP-08 `IRD-EXEC-*`）、持久化读写（WP-04/05 `IRD-PERSIST-*`/`IRD-RESULT-*`）；文件导入（WP-11）与报告渲染（WP-12）入口以契约替身在测试中接入，集成期衔接
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/` 下 `include/sdurws/ird/diagnostics/{DiagnosticMapper.hpp,StableCodeRegistry.hpp}`、`src/{DiagnosticMapper.cpp,StableCodeRegistry.cpp}`、`test/ErrorMappingTest.cpp`、`testdata/diagnostics/mapping/`
- **禁止修改的文件和公共接口：**上层重复包装、causeCode 改写、创建第四错误类别、修改业务模块状态、登记表既有 code 语义（category/默认值变更须评审）、其他模块公共接口
- **修改前接口：**无 mapper；错误在插件/服务/UI 多层各自转字符串，无 code/category/action 一致性
- **修改后接口：**`DiagnosticMapper::map(cause, context)` 为唯一一次映射；`StableCodeRegistry` 承载 module-design/diagnostics.md §3 登记表（约 100 码，`IRD-<AREA>-<NAME>` 唯一登记处）；六类边界（文件导入、RobWork 适配、worker/IPC、报告渲染、持久化读写、缓存/检查点反序列化）各调用一次
- **实施步骤：**落码登记表 → 按"错误类别规则"实现归类（用户数据/参数→Input；工程不可行/数据不足→Engineering；文件、进程、第三方与版本故障→System）→ 实现六类边界的 cause→code 映射 → 相同 causeCode＋context kind 幂等一致 → 上层仅附加关联 ID
- **RED 测试：**`test/ErrorMappingTest.cpp`（注册于 `sdurws_ird_diagnostics_test`）：同一 cause 从不同入口映射必须得到一致 code/category/action；已有诊断再次 map 必须保留原 code/causeCode，不生成同义链——先确认测试在无实现时失败
- **最小实现：**registry＋mapper 核心＋阶段 A 已交付边界的映射表；WP-11/12 入口留注册点以契约替身验证
- **正常/边界/失败测试：**正常：三类错误样本各映射到正确 category；severity 取目录 severityDefault（mapper 可按边界上下文调整，调整记录在案）。边界：causeCode 为 null 的顶层故障、同一 code 不同 context kind、retryable 传递。失败：未登记 code 调用 map → 拒绝；第四类别请求 → 拒绝；causeCode 被改写 → 测试失败
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_diagnostics_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_diagnostics(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；登记表与 module-design/diagnostics.md §3 逐码一致（脚本对照记录）；mapper 之外无新建 Diagnostic 的业务路径；无同义新增 code
- **证据工件：**`diagnostics/evidence/WP-09/T03/`：跨入口映射对照表、causeCode 链样例、诊断 JSON、severity 调整记录、命令日志与评审签名
- **提交格式：**`WP-09-T03: implement single-boundary error mapping`
- **停止与升级条件：**同一故障需要多个 code、边界归属无法确定或需变更既有 code 的 category/默认值时暂停并升级至 WP-09 所有者评审
