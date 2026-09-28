# WP-24-T08 ird_gates 命中集增量登记（契约 acceptance 6）

任务：WP-24-T08 正式产品程序骨架（宿主融合形态装配路径）
分支：wp24-t08（base＝34cec77a28820327bb1c13f20888f171722127da）
日期：2026-09-28

## 口径

门禁结论按现行实践以**收集态命中集对比**承载：引擎（`cmake/ird_gates.cmake`）
分别对 base 冻结点与任务分支全量运行（`cmake -P`，引擎终局 FATAL 不影响命中
行收集），命中行归一化（去 `[ird_gates] ` 前缀、去 IRD_ROOT 绝对路径前缀、
排序去重）后逐行 diff。`ird_gates` 构建目标在库内既有登记态命中基线上按同一
命中集运行（base 即 79 条归一命中——历次任务按 §4.5/证据登记的存量，WP-24-T03
留痕 151 行同口径含自测行，本任务两跑均含自测行故归一后 79 对 79 可比）。

## 命中集文件

| 文件 | 内容 |
| --- | --- |
| `ird-gates-base.log` / `ird-gates-head.log` | 引擎原始输出（cmake -P，base＝worktree @34cec77a，head＝任务分支工作树） |
| `ird-gates-lineuniq-{base,head}-norm.txt` | 归一中间态（未去绝对路径） |
| `ird-gates-{base,head}-norm.txt` | **比对基准**（去路径前缀＋排序去重：base 79 条，head 85 条） |

## diff 结论（base→head）

增量恰为以下 **6 条**，全部由新增目标 `sdurws_ird_studio` 产生，逐条登记于
DTB §4.5「WP-24-T08」行（机器消费面即本文件与本目录命中集文件）：

```text
IRD-GATE-R3: L2 目标 sdurws_ird_studio 链接 Qt 库 Qt6::Core（NFR-MNT-01：计算核心零 Qt；例外仅 ui 界面目标形态）
IRD-GATE-R3: L2 目标 sdurws_ird_studio 链接 Qt 库 Qt6::Gui（NFR-MNT-01：计算核心零 Qt；例外仅 ui 界面目标形态）
IRD-GATE-R3: L2 目标 sdurws_ird_studio 链接 Qt 库 Qt6::Widgets（NFR-MNT-01：计算核心零 Qt；例外仅 ui 界面目标形态）
IRD-GATE-SUB: 表外依赖边 studio->modeling（sdurws_ird_studio → sdurws_ird_modeling_plugin）不在 ARCH §3.5 白名单
IRD-GATE-SUB: 表外依赖边 studio->project（sdurws_ird_studio → sdurws_ird_project）不在 ARCH §3.5 白名单
IRD-GATE-SUB: 表外依赖边 studio->ui（sdurws_ird_studio → sdurws_ird_ui）不在 ARCH §3.5 白名单
```

**零意外命中**（除上述 6 条外 diff 为空；既有 79 条存量命中逐一保持不变）。

## 逐条依据（详见 DTB §4.5 WP-24-T08 行）

- **R-3 ×3**：门禁把产品名形态目标（`sdurws_ird_<unit>` 裸名）按"L2 计算内核"
  分类，而 `sdurws_ird_studio` 是宿主融合形态的**正式产品主程序**（B1-SPEC
  §2.1 目标名冻结；SA-18 D1）——产品 main 构造 rws::RobWorkStudio 主窗口，
  链接框架 rws 公共库与 Qt 是该目标的定义性要求。属 NFR-MNT-01"ui 界面目标"
  例外类（与 `sdurws_ird_ui` 既有命中同款模式），非 L2 计算内核 Qt 破线。
- **SUB studio->ui**：产品目标链接本单元产品库 `sdurws_ird_ui`——装配基座
  语义（WorkbenchContent/UiSessionController/CommandRegistry 等）的承载面，
  ui_plugin/ui_app"ui->ui 自边"先例同款。
- **SUB studio->project**：装配基座同源 TU（PortAdapters——打开五步协议适配）
  携带的既有 ui->project 宿主集成边在本目标的复现，ui_plugin 先例同款
  （O-31 装配层特权边）。
- **SUB studio->modeling**：装配基座同源 TU（DomainAssembly——建模装配门面
  消费）携带的既有 ui->modeling 宿主集成边在本目标的复现，WP-24-T03 先例同款。

白名单数据面（`ird_gates_whitelist.cmake`）与依赖图（dependency-graph.json）
本任务零改动——装配层特权边沿既有实践以登记＋命中集承载，图不变式保持。
SA-02 补丁核对：框架工作区零改动（引擎 5b 步两跑均未新增 SA02 命中）。
