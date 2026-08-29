# WP-05-T04 结果接纳与历史查询

- Task ID：WP-05-T04
- 需求/阶段：CON-01～CON-06、EVI-01、NFR-COR-02；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/persistence-schema.md`；模块方案：`module-design/snapshot-result.md`
- 前置：WP-05-T01～T03、WP-04 追加仓库、WP-06 名称解析。
- 允许：修改 `evidence/include/.../ResultAdmission.hpp`、`IResultRepository.hpp`、`ResultQuery.hpp`、`src/ResultAdmission.cpp`、`src/ResultRepository.cpp`、`test/ResultAdmissionTest.cpp`。
- 禁止：修改项目 HEAD/revision、工作进程权限、快照字段、报告输出格式或手工 CSV。
- 产出：身份/切片/名称校验、迟到结果追加和历史查询 API。

## 数据流

`ResultEnvelope -> validate project/branch/revision/snapshot/sliceHash/policy/name map/run/attempt/object IDs -> append immutable record -> currentness index -> query filters`。迟到结果只追加到其携带的原分支历史，不提升当前结果。

## Given/When/Then

- Given project、branch、revision 或 sliceHash 不匹配，When admit，Then拒绝写入并返回稳定诊断。
- Given 名称无法由 RuntimeNameMap 反解，When admit，Then返回 `IRD-EVIDENCE-NAME-MISMATCH`。
- Given 相同 runId/attemptId 已存在，When admit，Then幂等返回已有 ResultRef，不重复 payload。
- Given 迟到结果属于旧 revision，When query current，Then结果为 Historical/Superseded，旧记录仍可按 revision 查询。
- Given过滤 evaluator、currentness、evidenceLevel、executionOutcome，When query，Then结果顺序稳定且不修改仓库。

## 测试、证据与提交

覆盖重复提交、跨项目、跨分支、名称重命名、取消后回调、查询分页和大历史量。

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_result_acceptance_test$'
```
证据：接纳/拒绝日志、仓库 append 序号、查询快照和迟到结果报告。提交：`WP-05-T04: implement result admission and history queries`。

停止：需要覆盖历史 payload、直接写项目 revision 或身份字段缺乏来源时暂停。
