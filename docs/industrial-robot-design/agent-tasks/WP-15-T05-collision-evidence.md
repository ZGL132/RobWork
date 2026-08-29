# WP-15-T05 碰撞证据

- 需求/阶段：KIN-05；B/R1
- 契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`
- 前置：WP-07、WP-15-T01；允许：运动学碰撞适配；禁止：本地 CollisionPolicy
- 产出：共享评估器结果、对象 ID 对、策略版本和分辨率证据
- Given 距离查询不可用，When 验证路径，Then 不推断安全，按步长细分或 DataInsufficient
- Given 无碰撞，When 报告，Then 使用限定措辞“在本策略与分辨率下未发现碰撞”
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：碰撞黄金数据和 AT-19 报告；提交：`WP-15-T05: add collision evidence`
- 停止：共享策略接口未冻结时暂停
