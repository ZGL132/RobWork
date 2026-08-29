# WP-14-T03 姿态与区域语义

- 需求/阶段：REQ-02～04；B/R1
- 契约：`architecture/domain-model.md`、`architecture/testing-contract.md`
- 前置：WP-02、WP-14-T01；允许：区域/姿态计算模块；禁止：更改容差
- 产出：部分姿态约束、任务子空间和区域采样定义
- Given 4/5 轴任务，When 生成约束，Then 只使用声明的任务子空间
- Given 边界点，When 覆盖采样，Then 按边界包含和位置/姿态分母计算
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：区域黄金数据和分母报告；提交：`WP-14-T03: define pose regions`
- 停止：任务坐标系未明确时暂停
