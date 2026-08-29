# WP-05-T02 不可变分析快照

- Task ID：WP-05-T02
- 需求/阶段：CON-01～CON-06、EVI-01；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/execution-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/snapshot-result.md`
- 前置：WP-05-T01、WP-04 内容对象/查询接口、WP-06 RuntimeNameMap 接口。
- 允许：修改 `evidence/include/.../AnalysisSnapshot.hpp`、`src/AnalysisSnapshot.cpp`、`src/EvidenceJson.cpp`、`test/SnapshotTest.cpp`、`testdata/evidence/snapshot/`。
- 禁止：修改 slice 字段语义、项目事务、名称解析公共接口、GUI 或结果仓库。
- 产出：深拷贝快照、资源/策略/名称/软件版本冻结和 Quick 降级规则。

## 数据流

`ProjectRevisionRef + EvaluatorInputSlice + policyRef + RuntimeNameMapRef + SoftwareBaseline + resourceRefs -> validate completeness -> deep-copy/freeze -> snapshotId`。快照创建后只读，外部路径必须已解析为不可变对象；Quick 临时引用不可进入正式证据。

## Given/When/Then

- Given 缺策略、名称映射、算法版本或 Verified 资源对象，When create，Then返回 Engineering/System 诊断，不生成 snapshotId。
- Given 合法输入，When 修改调用者原始对象，Then快照字段、hash 和资源引用不变。
- Given Quick 模式外部路径，When create，Then标记 Screening/NonFormal，正式报告接纳器拒绝。
- Given 软件 baseline 或 seed 缺失，When create，Then返回 `IRD-EVIDENCE-SNAPSHOT-INCOMPLETE`。

## 测试、证据与提交

覆盖深拷贝、序列化往返、资源篡改、版本缺失、Quick/Verified 对比和跨线程 const 访问。

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_immutable_snapshot_test$'
```
证据：snapshot JSON、所有引用 hash、输入身份、失败诊断和评审记录。提交：`WP-05-T02: implement immutable analysis snapshots`。

停止：无法证明深拷贝、资源不可变或需要临时默认版本时暂停。
