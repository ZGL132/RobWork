# WP-13-T06 运行时编译

- 需求/阶段：MDL-06、14；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/persistence-schema.md`
- 前置：WP-06；允许：`plugins/modeling/compile/`；禁止：自行拼接运行时名称
- 产出：WorkCell/DynamicWorkCell 全成或全败编译适配
- Given 任一引用非法，When 编译，Then 无部分工件、修订不变
- Given 合法模型，When 编译，Then RuntimeNameMap 双向一致
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：故障注入日志、名称映射报告；提交：`WP-13-T06: compile runtime artifacts`
- 停止：WP-06 接口未冻结时暂停
