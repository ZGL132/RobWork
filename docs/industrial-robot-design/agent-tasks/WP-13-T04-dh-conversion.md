# WP-13-T04 DH 转换

- 需求/阶段：MDL-01、02、09、10；B/R1
- 契约：`architecture/domain-model.md`、`architecture/testing-contract.md`
- 前置：WP-02、WP-03；允许：`plugins/modeling/conversion/`；禁止：修改权威类型定义
- 产出：`DhConversionService` 和 Exact/Approximate 判定
- Given 不可表达模型，When 转换，Then 返回 NotRepresentable 且不替换权威模型
- Given 可转换模型，When 往返，Then FK/世界轴线满足第 15.3 节容差
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：四类黄金模型和误差报告；提交：`WP-13-T04: implement dh conversion`
- 停止：非唯一方案规范化规则未冻结时暂停
