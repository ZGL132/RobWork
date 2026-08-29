# WP-14-T05 就绪状态机

- 需求/阶段：REQ-06；B/R1
- 契约：`architecture/domain-model.md`、`architecture/execution-model.md`
- 前置：WP-03、WP-14-T01～T04；允许：就绪校验和测试；禁止：执行调度
- 产出：Draft/Ready/Invalid/DataInsufficient 判定
- Given 非法 Must，When 请求计算，Then 阻止执行并返回诊断
- Given 仅 Should 警告，When 请求计算，Then 可执行且警告可见
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：状态转移矩阵；提交：`WP-14-T05: implement readiness state`
- 停止：状态组合未被 WP-03 契约覆盖时暂停
