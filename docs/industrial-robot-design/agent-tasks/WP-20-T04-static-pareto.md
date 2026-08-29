# WP-20-T04 静态指标与 Pareto

- 需求/阶段：OPT-04、07、08 静态子集；B/R1
- 契约：`architecture/testing-contract.md`、`architecture/domain-model.md`
- 前置：WP-20-T03；允许：指标/Pareto 核心；禁止：引入隐式加权总分
- 产出：尺寸、质量、成本、节拍代理和裕量比较
- Given SoftConstraint 违反，When 比较，Then 只显示警告/次级排序，不改变可行性
- Given 指标差异低于容差，When 支配判断，Then 视为无差别
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：Pareto 黄金集和排序报告；提交：`WP-20-T04: implement static pareto`
- 停止：目标聚合或容差未冻结时暂停
