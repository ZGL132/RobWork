# WP-05-T03 结果正交状态

- Task ID：WP-05-T03
- 需求/阶段：CON-01～CON-06、EVI-01、NFR-COR-04；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/snapshot-result.md`
- 前置：WP-05-T02、WP-03 EvaluationSemantics。
- 允许：修改 `evidence/include/.../ResultEnvelope.hpp`、`EvidenceBundle.hpp`、`ResultStatusValidator.hpp`、`src/ResultEnvelope.cpp`、`test/ResultStatusTest.cpp`。
- 禁止：重新定义 WP-03 正式可行谓词、改变任务状态机、修改报告渲染或缓存策略。
- 产出：状态枚举组合校验、currentness 索引字段和不可变 payload 引用。

## 数据流

`evaluator output -> validate executionOutcome/engineeringStatus/payloadCompleteness/evidenceLevel -> attach currentness -> ResultEnvelope`。Currentness 初始为 Current/Historical，由后续服务更新索引；payload 和诊断历史不变。

## Given/When/Then

- Given `Completed + Pass + Complete`，When validate，Then允许进入接纳流程但仍需 RequiredEvidenceProfile。
- Given `Canceled/Failed/Interrupted + Pass` 或 `Complete`，When validate，Then拒绝并返回 Input/System 诊断。
- Given `Completed + DataInsufficient/Partial`，When validate，Then允许作为历史结果保存但不可正式可行。
- Given Superseded，When currentness update，Then只改索引状态，原 JSON/payload/evidence hash 不变。

## 测试、证据与提交

参数化覆盖状态笛卡尔积、Unknown enum、缺 payload、证据等级不足、重复 resultId 和 JSON 往返。

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_orthogonal_status_test$'
```
证据：组合矩阵、拒绝诊断、payload hash 前后比对和评审签名。提交：`WP-05-T03: enforce orthogonal result status`。

停止：发现状态组合与 WP-03 不一致或需要用一个布尔值替代正交字段时暂停。
