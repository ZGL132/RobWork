# WP-14-T01 需求数据模型

- 需求/阶段：REQ-01～08；B/R1
- 契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`
- 前置：WP-03、WP-13；允许：`plugins/requirements/model/`；禁止：修改领域公共枚举
- 产出：任务点、区域、工艺段、负载事件和约束类型
- Given 非有限值/缺失引用，When 校验，Then 返回 Input 诊断且不生成执行工件
- Given 合法数据，When 序列化，Then ID、单位、来源和版本稳定
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：字段矩阵和 JSON 往返；提交：`WP-14-T01: define requirement model`
- 停止：需求字段与架构契约冲突时暂停
