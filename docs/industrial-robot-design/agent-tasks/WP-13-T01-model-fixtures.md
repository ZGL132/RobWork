# WP-13-T01 模型类型与失败夹具

- 需求/阶段：MDL-01～04、11～12；B/R1
- 契约：`architecture/domain-model.md`、`architecture/testing-contract.md`
- 前置：WP-02；允许：`industrialrobot/plugins/modeling/test/fixtures/`；禁止：公共接口目录
- 产出：DH、显式关节、任意轴、缺失轴、零轴、4/7 轴和不支持拓扑黄金输入
- Given 非法轴或拓扑，When 导入，Then 返回明确诊断且不产生修订
- Given 合法边界模型，When 加载，Then 字段、单位和来源完整
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：夹具清单、失败断言日志；提交：`WP-13-T01: add modeling fixtures`
- 停止：黄金预期未冻结或需求语义冲突时暂停并提交 ADR
