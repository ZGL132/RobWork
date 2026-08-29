# WP-15-T06 批处理执行

- 需求/阶段：KIN-07；B/R1
- 契约：`architecture/execution-model.md`、`architecture/public-interfaces.md`
- 前置：WP-08、WP-15-T01～T05；允许：运动学 evaluator 适配；禁止：自建调度器
- 产出：带完整身份的批处理请求和结果接纳
- Given 分支切换或迟到回调，When 接收结果，Then 只能写原分支历史
- Given 取消，When 停止派发，Then 状态和检查点符合执行契约
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：并发/取消日志；提交：`WP-15-T06: integrate batch execution`
- 停止：任务状态转移未被 WP-08 覆盖时暂停
