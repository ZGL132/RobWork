# UI-T22 ird_gates 命中集增量登记（契约 acceptance 4）

任务：UI-T22 共享属性检查器和复杂编辑区（方案 B.1 迁移链，SA-18 D5/D6；契约 tasks/foundation/UI-T22.json）
分支：ui-t22（base＝redesign-main@ad44305b190062d20010cb0d4893017ba5220bf0）
日期：2026-09-28
责任方：WP-01-T03（机器面回填登记册——本文件为实施侧登记提交件，沿 wp10-t02/t13/t14/t15/t16/t20/t21 先例口径）

## 口径

引擎（`cmake/ird_gates.cmake`）以 `cmake -P` 直跑双端全量收集（引擎终局 FATAL
不影响命中行收集——F-019 存量命中判失败为既有事实，双端同红）：head＝ui-t22
任务工作树、base＝`ad44305b` detached worktree（`build/ird-t22-base-wt`，采集后
已移除）。命中行归一化（去 `[ird_gates] ` 前缀、去 IRD_ROOT 绝对路径前缀〔仅剥
物理路径段、保留门禁码与文案前缀〕、排序去重）后逐行 diff——WP-24-T08/
WP-10-T20/T21 留痕同款方法。

## 比对结论

- base：87 条归一命中；head：88 条归一命中。
- **增量恰 1 条**（见下）；既有 87 条存量命中逐一保持不变（除该行外 diff
  为空）。
- 归一化粒度口径注：本侧"仅剥物理路径段、保留门禁码前缀"的归一化计数
  （87↔88）与 wp10-t21 留痕的归一化粒度（92↔93）不同属既有先例内的粒度
  差（WP-10-T17 v1.13-r2 注"归一化粒度不同不影响差集结论"）——差集形态
  （零消除＋恰增一条）为比对不变量。

```text
IRD-GATE-R3: ui 产品面疑似包含 Qt 类头（Q+大写约定）：ui/src/PropertyInspector.cpp
```

## 逐条依据

| 命中 | 依据 |
| --- | --- |
| R3 疑似 Qt 类头：ui/src/PropertyInspector.cpp | UI-T22 面板实现 TU 包含 `<QHBoxLayout>`/`<QLabel>`/`<QPushButton>`/`<QVBoxLayout>`/`<QWidget>`（Q+大写类头约定形态——引擎"疑似"档）。**NFR-MNT-01 已登记例外类的同族既定形态**：ui 是全产品唯一允许 Qt Widgets 的平台单元（R-3 例外目标），既有 16 个 ui 产品面 TU 同款命中在册（IndustrialProjectTree.cpp/ParamTablePanel.cpp/WorkbenchShell.cpp 等——见 base 归一集同族行）。本任务新增 TU 属同一例外类（Qt 渲染层——共享检查器面板），产品库链接面零变化（`sdurws_ird_ui` 链接块仍仅 core/diagnostics＋Qt 三件套——配置期守卫硬断言通过）；PropertyInspector.hpp 头文件 QWidget 仅前置声明零命中（模型层零 Qt——FormEditCommon.hpp 同款口径）。 |

## 比对件（同目录 builds/wp10-t22/）

| 文件 | 内容 |
| --- | --- |
| `ird-gates-head.log` / `ird-gates-base.log` | 引擎原始输出（cmake -P 双端） |
| `ird-gates-{base,head}-norm.txt` | 归一化命中集（比对基准：87 ↔ 88） |
| `ird-gates-hitset-diff.txt` | 归一 diff（唯一增量＝上述 1 条 R3；零消除） |
