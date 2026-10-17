---
name: ird-discover-change
description: 工业机械臂任务侦察——接到任务 ID 或变更请求后的第一个动作。输出变更画像（分支、范围、适用规则、需求/单元/WP、依赖边、禁止路径、最小上下文），全程只读不改源码。适用于 RobWork 仓库 industrialrobot 开发区接手任何编码/治理任务之前。
---

# ird-discover-change：任务侦察（只读）

**时机**：接手任何任务的第一步（实施会话开工、验收会话对账前、人工排查切入）。
**输入**：任务 ID（如 `WP-13-T24`）或一段变更描述。
**路径提示**：仓库根目录内含同名 `RobWork/` 子目录（源码与设计文档都在该子树内）——相对路径一律按根 AGENTS.md §1 目录表口径解析，勿按"仓库根＝源码根"直觉拼接（演练实录摩擦①）。
**权威依据**：任务契约（`ird-task/1` schema）＋阅读路径（`doc/industrial-robot-design/context/reading-paths.md`）＋依赖白名单（`traceability/dependency-graph.json`）。

## 步骤

1. **定位契约** `doc/industrial-robot-design/tasks/<taskId>.json`（foundation 子目录同样规则），读取：`status`（仅 ready 可实施）、`unit`、`requirements[]`、`designRefs[]`、`allowedFiles`、`forbiddenFiles`、`dependsOn[]`、`branch`、`verify[]`。
2. **前置核对**：逐项查 `dependsOn` 中各任务契约的 `status` 是否 done；需要流水线相位时**读 `traceability/pipeline/state.json` 本体**（禁止从别处转抄状态——PIPE 权威红线）。
3. **选阅读路径**：按 `unit` 域匹配 reading-paths 五条路径之一，列出本任务"必须读"清单（章节级，禁止整册加载）。
4. **分支确认**：目标分支＝契约 `branch`（`wp<nn>-t<kk>`）；当前不在该分支先切/建；**严禁在 `main` 上操作**（分支红线）。
5. **依赖边核对**：涉及跨单元协作 → 对照 `dependency-graph.json` 允许边；业务域互链禁止（R-1），只可经六类端口。
6. **红线速查**：源码区 `AGENTS.md` §2（L2 零 Qt/互链禁止/框架只读/PA-1/PA-2）。

## 输出格式（变更画像）

```markdown
## 变更画像：<taskId>
- 分支：<branch>（base <SHA>）
- 变更范围：allowedFiles 摘要（逐条）
- 禁止路径：forbiddenFiles（逐条）
- 适用规则：根 AGENTS §… / 源码区 AGENTS §… / 单元卡 §…
- 需求 / 单元 / WP：requirements[] → REQUIREMENTS 条目 / units/<unit>.md#锚点 / masterWp
- 依赖边：涉及的跨单元边（dependency-graph 允许与否）
- 前置状态：dependsOn → 各自 status
- 最小上下文（必须读）：文件＋章节清单
```

## 禁止项

- 修改任何源码/文档（本 Skill 全程只读）；
- 跳过 `forbiddenFiles` 核对就开始规划改动；
- 在 `main` 分支上执行任何写操作；
- 通读 DTB/REQUIREMENTS 全文（只取任务行与需求条目）。
