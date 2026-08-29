# WP-20-T02 候选补丁

- 需求/阶段：OPT-01、02；B/R1
- 契约：`architecture/domain-model.md`、`architecture/persistence-schema.md`
- 前置：WP-13～15、WP-20-T01；允许：候选编译目录；禁止：修改项目修订
- 产出：`CandidateInputSnapshot`、`CompiledCandidateArtifact` 和差异诊断
- Given 编译失败，When 生成候选，Then 无项目修订且错误可定位
- Given 合法向量，When 编译，Then 基线差异、来源和对象 ID 完整
- 命令：`run-tests.ps1 ... -Regex '^sdurws_ird_optimization_definition_test$'`
- 证据：候选差异报告；提交：`WP-20-T02: compile candidate patches`
- 停止：候选与修订归属混淆时暂停
