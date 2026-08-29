# WP-14-T04 负载与工艺事件

- 需求/阶段：REQ-06、08；B/R1
- 契约：`architecture/domain-model.md`、`architecture/execution-model.md`
- 前置：WP-14-T01、WP-13-T05；允许：负载/工艺模型；禁止：动力学求值
- 产出：驻留、接近撤离、负载切换和完整循环工件
- Given 缺失关键负载，When 就绪检查，Then 返回 DataInsufficient
- Given 完整事件序列，When 保存，Then 顺序、持续时间和引用稳定
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：事件时间线、缺失数据报告；提交：`WP-14-T04: model load events`
- 停止：驻留和循环口径不一致时暂停
