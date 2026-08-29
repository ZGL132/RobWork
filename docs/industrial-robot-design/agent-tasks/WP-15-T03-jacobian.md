# WP-15-T03 Jacobian 与奇异性

- 需求/阶段：KIN-04；B/R1
- 契约：`architecture/domain-model.md`、`architecture/testing-contract.md`
- 前置：WP-02、WP-15-T01；允许：Jacobian 核心和测试；禁止：自行定义 L*
- 产出：`J_norm`、任务子空间、可操作度和条件数
- Given L* 非有限/非正，When 计算，Then 使用契约回退或返回 DataInsufficient
- Given 合法模型，When 对照解析结果，Then 误差符合容差
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：Jacobian 对照报告；提交：`WP-15-T03: implement jacobian`
- 停止：尺度定义与架构契约冲突时暂停
