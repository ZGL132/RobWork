# 设计区规则

执行仓库根 AGENTS.md §1 的最小上下文、§6 的分级流程；不再强制读取 context/README.md、资产清单或全量 reuse 库存。

- 需求唯一权威：REQUIREMENTS.md；架构：ARCHITECTURE.md；单元接口/状态机：units/<unit>.md；任务拆分与构建约定：development-task-breakdown.md。
- tasks/ 中已有契约按任务 ID 定位；普通修正无需另编契约。只同步语义实际变化的权威条目，不重复登记同一事实。
- traceability/ 保留真实验证与历史验收证据；不改写历史证据，不批量删除。新证据注明命令、结果及相关任务，不要求同时维护全库摘要索引。
- context/README.md 是可选工具说明；新增设计根级资产仍登记 asset-inventory.json，普通文件编辑不用刷新清单。
- 复用政策原文在根 §0.1；需复用迁移时定向读 reuse/policy.md、相关 catalog/inventory 条目和 migration 阶段。治理独立验收阶段门继续有效。
- 流水线状态只在自动编排时读写 traceability/pipeline/state.json，不复制状态到其他台账。纯人工任务不走 PIPE。
