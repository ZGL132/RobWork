# 治理检查定位表（可选参考）

根 AGENTS.md 为执行约束。本表仅帮助定位工具，不要求每条规则再维护一份执法登记。

| 检查 | 工具/方式 |
| --- | --- |
| 文档结构、任务 ID、资产链接、台账/装配清单一致性 | validate-docs.ps1 |
| 上游库存、变动产品文件、批准引用及迁移阶段门 | validate-reuse.ps1（文档/任务/构建入口共用） |
| 门禁真实拒绝能力 | test-reuse-governance.ps1；修改门禁时须正负例 |
| 契约字段、范围、相关验证 | validate-task / verify-task-scope / verify-task |
| 新增提交标题 | commit-lint.ps1；取消固定四段正文 |
| 架构依赖、Qt/头文件等红线 | ird_gates 与相关契约测试 |
| 全库证据索引审计 | 显式 validate-docs.ps1 -DeepEvidence，普通提交/CI 不要求 |
| 上下文包 | 使用某包时 validate-context-pack.ps1 -TaskId <id>，不全量刷新 |
| 整体验证 | gate-all.ps1 / CI；小改按影响矩阵执行即可 |
| 语义等价、批准身份、物理单位/坐标系与真实 GUI 接线 | 相关测试与根 §6.4 独立验收，不能由关键词 lint 证明 |

不会自动核验的事实就按实际审阅/运行报告，不用固定文案、章节数或新增台账模拟质量保证。自动编排的并发锁和状态校验保持原协议，普通任务不加载 PIPE。
