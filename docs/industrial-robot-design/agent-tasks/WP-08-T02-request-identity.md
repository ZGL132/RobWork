# WP-08-T02 请求身份与迟到保护

- Task ID：WP-08-T02
- 需求/阶段：TASK-01～03、CON-04、NFR-COR-02、NFR-REL-02；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/execution-platform.md`
- 前置：WP-08-T01、WP-05 快照和结果接纳接口、WP-04 查询。
- 允许：修改 `execution/include/.../EvaluationRequest.hpp`、`WorkerProtocol.hpp`、`src/RequestValidator.cpp`、`src/LateResultGuard.cpp`、`test/RequestIdentityTest.cpp`。
- 禁止：修改 snapshot/slice 字段、项目 revision 写入、WP-05 currentness 语义和手工 CSV。
- 产出：完整 request 身份校验、切换项目后的迟到结果隔离。

## Given/When/Then

- Given缺 project/branch/revision/snapshot/run/attempt/evaluator/version/seed/budget，When submit，Then拒绝入队并返回 Input 诊断。
- Given切换项目或分支后旧 request 回调，When guard，Then只追加原历史，不进入当前结果。
- Given相同 runId/attemptId 重复提交，When submit，Then幂等拒绝或返回已有 handle，不创建重复运行。

## 测试与证据

覆盖跨项目、跨分支、旧 revision、版本不符和迟到顺序。命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_request_identity_test$'
```
证据：request JSON、guard 决策、结果接纳回执和旧/当前身份对比。提交：`WP-08-T02: enforce request identity and late-result isolation`。

停止：缺少身份字段来源或需要 scheduler 直接修改 currentness 时暂停。
