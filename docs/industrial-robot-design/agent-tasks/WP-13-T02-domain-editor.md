# WP-13-T02 领域编辑器

- 需求/阶段：MDL-01、08、13；B/R1
- 契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`
- 前置：WP-03、WP-04；允许：`plugins/modeling/`；禁止：`project/` 实现
- 产出：`RobotDesignEditor` 草稿校验和命令 DTO
- Given 重复 robotId/关节不足，When 应用，Then 拒绝且修订号不变
- Given 合法草稿，When 提交，Then 只产生一个新修订
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：状态矩阵、命令日志；提交：`WP-13-T02: implement domain editor`
- 停止：公共字段与契约不一致时暂停
