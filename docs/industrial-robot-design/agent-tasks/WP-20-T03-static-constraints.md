# WP-20-T03 静态硬约束

- 需求/阶段：OPT-03、04 静态子集；B/R1
- 契约：`architecture/testing-contract.md`、`architecture/public-interfaces.md`
- 前置：WP-07、WP-15、WP-20-T02；允许：静态 evaluator；禁止：轨迹/动力/器件求值
- 产出：拓扑、输入、运动学、碰撞硬约束流水线
- Given 任一 Must 失败，When 评估候选，Then 不进入可行集合并保留原因
- Given Quick 结果，When 输出，Then 不标记为 Verified
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：硬约束失败矩阵；提交：`WP-20-T03: implement static constraints`
- 停止：硬约束顺序与需求不一致时暂停
