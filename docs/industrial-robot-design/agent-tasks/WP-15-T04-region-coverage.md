# WP-15-T04 区域覆盖

- 需求/阶段：KIN-05；B/R1
- 契约：`architecture/testing-contract.md`、`architecture/execution-model.md`
- 前置：WP-14、WP-15-T01；允许：覆盖评估核心；禁止：改写 Partial 语义
- 产出：位置/姿态分母、边界、取消和预算耗尽结果
- Given 用户取消，When 批处理停止，Then Canceled + Partial/None，不得 Verified
- Given 正常完成但证据不足，When 结束，Then Completed + DataInsufficient
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：覆盖率矩阵；提交：`WP-15-T04: implement region coverage`
- 停止：分母和采样口径未冻结时暂停
