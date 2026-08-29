# WP-13-T05 物性与工具

- 需求/阶段：MDL-05、07、13；B/R1
- 契约：`architecture/domain-model.md`、`architecture/persistence-schema.md`
- 前置：WP-03、WP-11；允许：`plugins/modeling/material/`、`tool/`；禁止：直接写项目文件
- 产出：`MaterialInertiaEstimator`、Tool/TCP 和负载引用
- Given 缺失物性，When 计算，Then 降级证据并显示来源，不伪装精确
- Given 合法工具引用，When 保存，Then TCP 与几何不复制且引用可解析
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：解析算例、来源往返；提交：`WP-13-T05: add material and tool model`
- 停止：单位或参考坐标不明确时暂停
