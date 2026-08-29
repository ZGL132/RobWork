# WP-15-T08 跨入口契约

- 需求/阶段：ARC-05、NFR-COR-05；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-07、WP-15-T05；允许：契约测试；禁止：复制碰撞规则
- 产出：运动学与 WP-20 入口一致性测试
- Given 同一快照/策略，When 两入口求值，Then 对象 ID 对、判定和原因完全一致
- Given 仅改变显示开关，When 再求值，Then 不创建修订、不改变判定
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：AT-19 阶段 B 记录；提交：`WP-15-T08: add cross-entry contract tests`
- 停止：发现第二个策略实现时暂停
