# WP-14-T02 CSV 导入导出

- 需求/阶段：REQ-05、07；B/R1
- 契约：`architecture/persistence-schema.md`、`architecture/testing-contract.md`
- 前置：WP-11、WP-14-T01；允许：需求 CSV 适配器和模板；禁止：直接执行公式
- 产出：模板、字段映射、行级错误和安全导出
- Given 公式样式文本或错误行，When 导入/导出，Then 不执行且定位到行列
- Given 合法 CSV，When 往返，Then 值、来源和顺序稳定
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_requirements_test$'`
- 证据：恶意 CSV、往返报告；提交：`WP-14-T02: implement requirements csv io`
- 停止：CSV 字段字典未冻结时暂停
