# WP-13-T03 URDF 导入

- 需求/阶段：MDL-03、11、12；B/R1
- 契约：`architecture/domain-model.md`、`architecture/persistence-schema.md`
- 前置：WP-02、WP-11；允许：`plugins/modeling/import/`、fixtures；禁止：名称解析实现
- 产出：`UrdfImportAdapter`、字段映射报告和诊断
- Given 缺失/零轴，When 导入，Then 分别生成 +X 草稿或仅报告并阻止 Verified
- Given continuous/prismatic，When 编译，Then 保留类型和轴语义
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_modeling_test$'`
- 证据：URDF 样例、映射报告；提交：`WP-13-T03: add urdf adapter`
- 停止：RobWork 轴约定无法由契约证明时暂停
