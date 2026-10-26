# 快照 WC 访问器 capability 错配更正（2026-10-26，runtime 定向复用审查批）

## 背景与定性

REUSE-T30 独立验收（[REUSE-T30-20261026-r1](../../acceptance/REUSE-T30-20261026-r1.json)，verdict=pass）的评审缺口 7 已认定：`原 KIN-FK/setQ|baseTframe|baseJframe` 系历史错配，并在该包范围内（allowedFiles）纠正了同组第四行 `runtime.RuntimeSnapshot.tryDynamicWorkCell`（capabilityId→RUNTIME-DWC-MODEL、search/conclusion 改 DWC 面、decision 保持 pending 不假关）。

本批（所有者指令的 runtime 定向复用审查与必要修复，串行主线 `redesign-main`）复核 runtime 全矩阵 187 行后发现同族错配残留恰三行：`runtime.RuntimeSnapshot.workCell` / `runtime.RuntimeSnapshot.makeState` / `runtime.RuntimeSnapshot.workCellHandle`。三者均为快照侧受控视图与任务私有 State 工厂的转发面（Snapshot.cpp:753–766/811–816），不做 FK 计算；catalog `RUNTIME-WC-MODEL` 边界明确含"受控视图"，且兄弟行 `runtime.WorkCellConstView.findFrame/findDevice/defaultState` 已判 RUNTIME-WC-MODEL/direct。KIN-FK（Device.hpp setQ/baseTframe/baseJframe，kinematics 消费面）对该三行不成立。

## 变更内容（零产品代码）

`reuse/functions/runtime.json` 三行：`capabilityId: KIN-FK → RUNTIME-WC-MODEL`；`search.command/conclusion` 更正为真实上游面。`decision/status` 保持 pending 不假关（逐功能生产正向证据未产出，不冒充关闭）；differences/inputs/auditEvidence（T01 批审计史）原样保留。不涉及 permissions、catalog、changes 凭证与任何生产文件。

## 验证（命令与结果）

- `rg -n "getDefaultState|findFrame|findDevice" RobWork/RobWork/src/rw/models/WorkCell.hpp` → 9 处命中（findFrame:251/findDevice:306 等，命令真实执行）。
- `rg -c '"capabilityId": "KIN-FK"' reuse/functions/runtime.json` → 更正前 3（恰为本三行）、更正后 0。
- `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1` → 结果见提交信息（PASS 方为收口）。

## NOT RUN（如实分列）

- C++ 构建/测试：零产品代码变更，按根 AGENTS §4 文档/治理数据口径无需构建（NOT RUN）。
- validate-docs `-DeepEvidence` 全库证据索引深查：普通提交不要求重建全库索引（NOT RUN）。
- REUSE-T30 既有验证（runtime 312 项/modeling 消费者等）：不受本批影响，不重跑（引用原独验记录）。

## 关联与边界

- 同批 runtime 审查结论：S6/S7 编译链、offset（REUSE-T13 既有修复，规范反例 0.25+0.125=0.375 m 独立 oracle 在案）、受控视图/任务私有 State/worker 值传递、名称解析均达标零变更；GAP-RNE-FRAME 按五处既有登记保留，修复落点（B1/H3）仍归 WP-07 提案批准，本批不动 runtime 公共接口、不动上游。
- 本文件为 T30 收口后的同族补充登记，不改变 REUSE-T30 冻结送验范围（`c816a2c48..e74886dfc`）与其独验结论。
