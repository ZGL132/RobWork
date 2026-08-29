# WP-05-T01 输入切片与依赖失效

- Task ID：WP-05-T01
- 需求/阶段：CON-01～CON-06、NFR-COR-04；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/snapshot-result.md`
- 前置：WP-03 core、WP-04 查询接口、WP-01 构建脚本。
- 允许：修改 `evidence/include/.../EvaluatorDependencyManifest.hpp`、`EvaluatorInputSlice.hpp`、`src/DependencyResolver.cpp`、`src/InputSlice.cpp`、`test/InputSliceTest.cpp`、`testdata/evidence/slice/`。
- 禁止：修改 requirements、WP-03 枚举、WP-04 revision 格式、评估算法和手工 CSV。
- 产出：字段级依赖注册、规范化切片、`sliceHash` 和失效矩阵。

## 数据流

`evaluator declaration + ProjectRevision -> select dependency paths -> normalize IDs/units/lists -> canonical JSON -> SHA-256 -> EvaluatorInputSlice`。依赖原因按 fieldPath 字节序排序；显示开关、当前选择和名称拼写排除在物理 hash 外但可记录为 NonPhysical。

## Given/When/Then

- Given 相同切片字段顺序不同，When build，Then 得到相同规范 JSON 和 `sliceHash`。
- Given TCP/工具/负载变化，When compare，Then分别失效运动学/轨迹/动力学等契约声明的下游。
- Given 电机成本变化，When compare FK/IK/轨迹，Then保持有效；选型/优化标记失效。
- Given 显示开关或当前选择变化，When compare，Then不产生物理失效。
- Given 缺字段、非有限值、空版本或线程数为 0，When build，Then返回 Input 诊断且不创建快照。

## 测试、证据与提交

正常、边界、重复字段、未知 semanticRole 和大列表测试。命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_input_slice_test$'
```

证据：失效矩阵、canonical JSON、sliceHash、诊断 JSON、命令日志和评审签名。提交：`WP-05-T01: implement evaluator input slices`。

停止：需求未定义某字段是否影响评估器、需要改变 WP-03 类型或 hash 规则不一致时暂停并报告。
