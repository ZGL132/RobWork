# ird_gates base↔head 归一化比对（WP-14-T10 实施段留痕）

- 任务：WP-14-T10（需求迁移三接入面）；分支 wp14-t10；base 60befce8a63b758a7d5daf952e03082726d7d5af。
- 本附件口径：登记册回填归 WP-01-T03 治理面（本任务 allowedFiles 不含
  `cmake/ird_gates_whitelist.cmake`），实施段出具命中集增量声明与留痕，
  base 侧冷启复现与裁决归验收段（wp13-t20 先例同型）。

## head（wp14-t10 工作树）门禁结论

- 命令：`cmake --build build --config Release --target ird_gates`（集成构建树）。
- 结果：**引擎退出 1**——命中集共 82 条，其中存在登记册外新增（失败消息
  原文口径："命中集未登记/登记册不匹配，82 条……例外仅经 DTB §4.5 登记"）。
- 全文留痕：`ird_gates-head.log`；去重归一化清单：`ird_gates_head_hits_normalized.txt`。

## 新增命中声明（head 相对 base 60befce8）

恰增 **1 条**，为 `_app` harness 目标的既定形态命中：

| 码 | 命中 | 形态依据 |
| --- | --- | --- |
| IRD-GATE-R1 | 自边 `requirements_app → requirements_plugin`（目标 sdurws_ird_requirements_app 链接 sdurws_ird_requirements_plugin） | modeling 同款先例 `modeling_app → modeling_plugin`（owner 2026-09-26 harness 约定，WP-13-T20 命中集既含）；单行链接语句＝构建图契约测试登记形态；_app 后缀＝门禁分类面预留形态（plugin/worker/app——Qt 链接合法） |

本域插件面既有命中（WP-14-T08 已登记，本次零变化）：
`requirements_plugin → requirements`（R1 自边）、`requirements → ui`（SUB，
插件链接面卡 §3.2 sanction）。

## base 侧说明

base＝60befce8（WP-13-T20 合入后主线）。其门禁命中集在 WP-13-T20 验收
记录为 81 条（引擎直跑 base↔head 归一化 81↔81 双向零新增）。本任务 head
82 条－新增声明 1 条＝81 条，与 base 相容；精确双向归一化由验收段在
detached worktree 冷启复现（acceptance-protocol §3/§4）。
