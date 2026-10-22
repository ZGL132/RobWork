## 问题与变更行为

说明触发条件、变更前后行为、任务ID和冻结SHA。

## H1—H6 复用证据（缺一项不得合入）

- 任务契约、reuse/changes/<taskId>.json，逐文件摘要与能力ID：
- 上游版本、文件/符号/构建目标、检索命令、真实生产调用证据：
- 选择直接API/薄适配的原因，单位/坐标系/线程/所有权/错误差异：
- 能力缺口提案与所有者明确批准（提案摘要和范围；治理批准不替代）：
- GOV-T21独立验收记录 {commit,path,reviewedCommit}：

## 验证与审查

- [ ] validate-reuse、validate-task、ird_gates通过；未把存量债务标为合规
- [ ] 单元/集成/回归/独立oracle/上游直接调用：日志、XML、JSON
- [ ] passed / skipped / failed 分列，未执行项及原因
- [ ] 文档/约束/测试/CI同步，review-checklist.md逐项核查
- [ ] 冻结SHA独立验收通过，范围/双模式/负实验/语义重复已审查
- [ ] 上游只读/patch登记、main冻结、所有者决定合入
