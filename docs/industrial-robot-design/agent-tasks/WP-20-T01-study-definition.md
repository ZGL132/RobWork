# WP-20-T01 研究定义

- 需求/阶段：OPT-01、02、06 静态子集；B/R1
- 契约：`architecture/domain-model.md`、`architecture/execution-model.md`
- 前置：WP-03、WP-05；允许：`plugins/optimization/definition/`；禁止：WP-21 联合搜索
- 产出：变量字段路径、连续/量化/离散域、锁定、预算和种子版本
- Given 引用不属于基线作用域，When 创建研究，Then 拒绝并给出 Input 诊断
- Given 合法改型，When 保存，Then 非授权字段默认锁定且可追溯
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：研究定义 JSON 和校验矩阵；提交：`WP-20-T01: define optimization study`
- 停止：字段路径或锁定规则未冻结时暂停
