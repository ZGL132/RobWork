# WP-23-T04 并行确定性

- 需求/阶段：AT-01～19、NFR-COR、NFR-PERF、NFR-REL；阶段 D / R2
- 契约：`architecture/testing-contract.md`、`architecture/execution-model.md`、`architecture/persistence-schema.md`
- 前置：由对应 WP 计划声明的前置工作包和公共接口。
- 允许：仅修改 WP-23 拥有目录、该任务测试和证据目录；禁止：修改 requirements.md 语义、其他 WP 所有的公共接口、生成 CSV 或未获批准的依赖。
- 产出：验证固定线程下候选集合、排序、稳定 ID 和 Pareto 关系一致。 以及可审计测试和结构化证据。
- Given 契约输入缺失、非法或版本不兼容，When 执行本任务，Then 返回稳定诊断并不产生部分提交或正式结果。（失败断言）
- Given 合法黄金数据和固定种子，When 执行本任务，Then 输出符合契约字段、状态、单位和容差的结果，并可由后续 WP 消费。（正常/边界断言）
- 命令：`powershell -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_determinism_test$'`；脚本尚未创建时先执行 WP-01 交付的同名入口。
- 证据：模块测试日志、契约测试结果、黄金数据或人工复核报告，包含 commit、配置、种子和输入快照身份。
- 提交：`WP-23-T04: 并行确定性`
- 停止：发现需求、架构契约、前置接口或黄金数据彼此不一致，或验证命令/环境前置缺失时暂停并报告，不自行改写权威语义。
