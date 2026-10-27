## 问题与变更行为

说明触发条件、变更前后行为、任务ID和冻结SHA。

## H1—H6 复用证据（适用项缺失不得合入）

按根 AGENTS.md §4/§6 选择验证；纯文档写明事实/链接及文档门禁结果，产品变更填写以下凭证。新增规则的实施状态见 reuse/audit-contract.md，不把未实现/未执行项打勾为通过。

- 任务契约、reuse/changes/<taskId>.json，逐文件摘要与能力ID：
- 上游版本、文件/符号/构建目标、检索命令、真实生产调用证据：
- 选择直接API/薄适配的原因，单位/坐标系/线程/所有权/错误差异：
- 能力缺口提案与所有者明确批准（提案摘要和范围；治理批准不替代）：
- GOV-T21独立验收记录 {commit,path,reviewedCommit}：
- 功能矩阵 functionId/需求/真实调用链与 catalog 能力、ownerUnit/usageRole：
- ARCH §3.6 / SA-19 权限引用，文件/目标/操作、宿主例外或 worker 边界：
- direct/adapter 生产入口→上游→结果消费，独立 oracle/上游对照/隔离负实验及证据新鲜度：
- 新增门禁补强独立验收与当前 migration §11 阶段（旧 GOV-T21 pass 不替代）：

## 验证与审查

- [ ] 复用迁移按 reuse/policy.md §1.1 附实施前设计判断（文件/符号、保留/删除/适配/职责归属）与准确性/架构/简洁性/运行效果证据；说明必要保留项及性能变化，不以调用上游或复用数量替代验收
- [ ] validate-reuse、validate-task、ird_gates通过；未把存量债务标为合规
- [ ] 单元/集成/回归/独立oracle/上游直接调用：日志、XML、JSON
- [ ] passed / skipped / failed 分列，未执行项及原因
- [ ] 文档/约束/测试/CI同步，review-checklist.md逐项核查
- [ ] 适用时冻结SHA独立验收通过，相关范围/双模式/负实验/语义重复已审查
- [ ] 逐功能覆盖不限 16 文件；未加通用 facade/第二模型层/重复工具链，无隐藏旧核心兜底
- [ ] 上游只读/patch登记、main冻结、所有者决定合入
