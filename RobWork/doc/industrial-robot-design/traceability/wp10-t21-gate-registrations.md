# UI-T21 ird_gates 命中集增量登记（契约 acceptance 6）

任务：UI-T21 工业项目树和选择服务（方案 B.1 迁移链，SA-18 D3/D4；契约 tasks/foundation/UI-T21.json）
分支：ui-t21（base＝redesign-main@a1f91eeaeec751f60979fe762093e56937c69205）
日期：2026-09-28
责任方：WP-01-T03（机器面回填登记册——本文件为实施侧登记提交件，沿 wp10-t02/t13/t14/t15/t16/t20 先例口径）

## 口径

引擎（`cmake/ird_gates.cmake`）以 `cmake -P` 直跑双端全量收集（引擎终局 FATAL
不影响命中行收集——F-019 存量命中判失败为既有事实，双端同红）：head＝ui-t21
任务工作树、base＝`a1f91eea` detached worktree（`build/ird-t21-base-wt`，采集后
已移除）。命中行归一化（去 `[ird_gates] ` 前缀、去 IRD_ROOT 绝对路径前缀、
排序去重）后逐行 diff——WP-24-T08/WP-10-T20 留痕同款方法。

## 比对结论

- base：92 条归一命中；head：93 条归一命中。
- **增量恰 1 条**（见下）；既有 92 条存量命中逐一保持不变（除该行与计数
  行/扫描根行外 diff 为空）。

```text
IRD-GATE-R3: ui 产品面疑似包含 Qt 类头（Q+大写约定）：ui/src/IndustrialProjectTree.cpp
```

## 逐条依据

| 命中 | 依据 |
| --- | --- |
| R3 疑似 Qt 类头：ui/src/IndustrialProjectTree.cpp | UI-T21 面板实现 TU 包含 `<QTreeWidget>`/`<QTreeWidgetItem>`（Q+大写类头约定形态——引擎"疑似"档）。**NFR-MNT-01 已登记例外类的同族既定形态**：ui 是全产品唯一允许 Qt Widgets 的平台单元（R-3 例外目标），既有 15 个 ui 产品面 TU 同款命中在册（AboutDialog.cpp/ParamTablePanel.cpp/WorkbenchShell.cpp 等——见 base 归一集 20~35 行同族）。本任务新增 TU 属同一例外类（Qt 渲染层——五分组树面板），产品库链接面零变化（`sdurws_ird_ui` 链接块仍仅 core/diagnostics＋Qt 三件套——配置期守卫硬断言＋LinkageContractTest.ArchEdgesCoreDiagnosticsOnly 文本钉均通过）；SelectionService.cpp 零 Qt（新 TU 中唯一纯模型 TU，无命中）。 |

## 比对件（同目录 builds/wp10-t21/）

| 文件 | 内容 |
| --- | --- |
| `ird-gates-head.log` / `ird-gates-base.log` | 引擎原始输出（cmake -P 双端） |
| `ird-gates-{base,head}-norm.txt` | 归一化命中集（比对基准：92 ↔ 93） |
| `ird-gates-hitset-diff.txt` | 归一 diff（唯一增量＝上述 1 条 R3；计数行 79→80 与扫描根行差异为归一化保留的引擎结论行） |
