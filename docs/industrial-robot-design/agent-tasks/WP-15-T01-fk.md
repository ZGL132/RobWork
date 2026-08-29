# WP-15-T01 FK 与规范模型

- 需求/阶段：KIN-01；B/R1
- 契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`
- 前置：WP-06、WP-13、WP-14；允许：运动学 FK 核心和测试；禁止：改变 SE(3) 约定
- 产出：零位/Home/边界 FK 和编译工件适配
- Given 缺失运行时引用，When FK，Then 返回诊断且无结果
- Given 合法模型，When FK，Then TCP/轴线符合第 15.3 节容差
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_kinematics_test$'`
- 证据：解析 FK 报告；提交：`WP-15-T01: implement fk`
- 停止：世界坐标定义不一致时暂停
