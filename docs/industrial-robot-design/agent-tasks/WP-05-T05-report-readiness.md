# WP-05-T05 正式可行与报告就绪

- Task ID：WP-05-T05
- 需求/阶段：EVI-01、NFR-COR-02、NFR-COR-04；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/snapshot-result.md`
- 前置：WP-05-T03、WP-05-T04、WP-03 正式可行谓词、WP-12 报告接口。
- 允许：修改 `evidence/include/.../ReportReadiness.hpp`、`EvidenceGap.hpp`、`src/ReportReadiness.cpp`、`test/ReportReadinessTest.cpp`、`testdata/evidence/readiness/`。
- 禁止：复制/修改 `isFormallyFeasible`、改变报告措辞、把 Quick/Partial 结果提升为正式证据或修改需求。
- 产出：RequiredEvidenceProfile 缺口计算和报告就绪判定。

## 数据流

`accepted ResultEnvelope[] + RequiredEvidenceProfile -> group by requirement/evaluator -> check Current + Completed + Complete + evidence level + resource fidelity -> call WP-03 predicate -> EvidenceGap[] + readiness`。模块只解释缺口，不替代领域可行谓词。

## Given/When/Then

- Given全部 Must 约束通过且所需评估器、资源和证据等级齐全，When assess，Then复用 WP-03 谓词并返回 Ready/Feasible。
- Given缺任一评估器、资源保真度或最低证据等级，When assess，Then返回 NotReady 和可定位 `EvidenceGap`。
- Given Quick、Partial、DataInsufficient、NotEvaluated 或 Superseded 结果，When assess，Then不得成为正式报告证据。
- Given同一 requirement 多个结果，When assess，Then只选择 Current 且身份匹配的结果，其他保留为历史。

## 测试、证据与提交

覆盖缺口排序、证据等级边界、Must/Should 区分、当前性、取消结果和报告消费者契约。

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_report_readiness_test$'
```
证据：RequiredEvidenceProfile、缺口 JSON、谓词输入输出、报告就绪矩阵和独立评审。提交：`WP-05-T05: implement report readiness and evidence gaps`。

停止：报告层要求重新定义工程状态、缺口无法映射 requirementId 或证据等级含义未冻结时暂停。
