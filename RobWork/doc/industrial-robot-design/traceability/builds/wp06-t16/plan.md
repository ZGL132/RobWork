# RT-T16 实施步骤

目标：按 REQUIREMENTS §25 附录 D #6/#7 修复 F-784，不变更输入、内容身份或公共布局。

基线：`3444cbdee5ecd946ebd59e487a2c0e961285411c`。在任务分支承接主线 `a914d8d5`、验收记录 `fdd22562` 以及已验收冻结依赖 `3826ac6c`；两次依赖合并零冲突。主线未改动。

- [x] 双模式配置，集成 runtime 基线真实执行；建立 XML/日志与原生 JSON 缺口记录。
- [x] 仅修改两份测试文件：小/大尺度误判、相对阈值两侧、对称化近退化矩阵、零/纯反对称/NaN/±Inf、上下尺度及不改值/编码往返。
- [x] 真实运行旧实现，保存回归翻红证据。
- [x] 修改 CanonicalModel.cpp / DescriptionValidator.cpp：同一相对范数口径，实际对称化，严格 SPD；仅临时数值缩放，不回写原值。
- [x] 双模式构建 runtime_test、相关回归、ird_gates；执行契约 verify、文档验证，核对差异与注释。
- [x] 同步 runtime 单元卡与 RT-T16 留痕，提交并立即推送 wp06-t16，发出独立验收请求。

本任务仅实施与自测，不代行独立验收，不将 QP-02 整包标为完成。契约状态不自行改 done。

## 自测结果（2026-10-15）

- 旧实现红证据：`smoke-red/` 与 `integration-red/`，8 个回归中 6 个失败，复现 F-784 两类尺度误判。
- 冒烟最终：`smoke-final/`，构建成功；187/187 通过；原生报告 187 passed、0 failed、0 skipped。
- 集成最终：`integration-final/`，构建成功；284 passed、1 既有 `ContractSuite.ChildProcessWorker_RT_ID_1` skipped；无失败。
- 两模式 `ird_gates` 均通过；`validate-task.ps1` 与 `validate-docs.ps1` 均通过。
- 独立代码预审未发现阻断问题；本目录证据仅代表实施自测，尚待独立验收。
