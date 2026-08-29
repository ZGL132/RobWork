# WP-13-T07 单机械臂与身份回归

- 需求/阶段：ARC-04、MDL-08、14；B/R1
- 契约：`architecture/domain-model.md`、`architecture/execution-model.md`
- 前置：WP-04、WP-06；允许：建模测试和夹具；禁止：更改 objectId 算法
- 产出：复制、导入、删除、重命名和分支回归套件
- Given 重命名或跨项目复制，When 重新解析，Then ID 规则符合契约且删除 ID 不复用
- Given 多可动分支，When 未选择目标链，Then 不进入计算模型但保留证据
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：身份矩阵、AT-18 阶段 B 报告；提交：`WP-13-T07: cover identity regression`
- 停止：发现跨 WP 身份语义时暂停
