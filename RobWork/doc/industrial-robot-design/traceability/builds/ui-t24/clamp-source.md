# UI-T24 P2 宽度收束钳制源定位记录

- 任务：UI-T24（宿主默认布局收敛与域命令文案语义化）acceptance 2 前置——"修前必须先定位最小宽度来源，禁止盲调数值"（契约 knownPitfalls 第 3 条）。
- 定位手段：实施段新增布局度量冒烟通道（`IRD_UI_PLUGIN_SMOKE=layout`，本任务 commit 1 落地），净室 CWD 启动 `build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe`，在宿主装载语义全部落定后（1200 ms 度量拍）递归走查各 IRD Dock 内容子树的 `minimumSizeHint`。
- 度量现场：本目录 `probe1/`（`probe-run.log` 驱动留痕、`probe-console.log` 控制台原始输出、`geometry-report.txt` UTF-8 全量走查报告）。
- 度量环境：宿主窗口 2406×1510（本机全屏），净室（无 rwsettings.xml 用户记忆）。

## 结论：两处钳制源（数值与契约已知陷阱逐字吻合）

### 源 1（主源，契约 knownPitfalls 第 3 条的嫌疑实锤）：需求域命令按钮行

- `requirements-dock` 整体 `minHint = 2092×301`；子树走查定位到 `RequirementsPanelWidget` 顶部命令条（一行 QHBoxLayout）：12 个按钮（九条域命令按钮＋三个撤销按钮）单行排布，按钮最小宽 148~200 px，一行累计 `minHint = 2074×25`，加 Dock 边框即 2092。
- 与 tick#424（WP-24-T09 冒烟日志）"目标 962 px 实际 2092 px" 的钳制数值逐字吻合；本次度量复现实测同为 962/2092（probe1/probe-run.log 收束行）。
- 机理：需求 Dock 与主 Dock/建模 Dock/运动学 Dock 同列（Qt::LeftDockWidgetArea 纵排共享列宽），列宽被列内最宽内容最小宽度钳制 → 整列收不下去 → 中央区被挤占。
- 修复归属：P3 流式栅格重排（按钮换行，行最小宽坍缩到单按钮最宽 ≈200 px），同时消账 F-430 家族需求域按钮一族（UX-02）。

### 源 2（次源，本次定位新发现）：主 Dock 顶栏占位标签行

- `main-dock` 整体 `minHint = 1054×168`；子树走查定位到 `ird_top_bar_content`（内容装配层 buildTopBar）：一行 QHBoxLayout 内三个占位 QLabel（"阶段导航（本阶段将在后续版本提供）"，各 `minHint = 204×25`）＋项目入口钮＋四个写命令按钮＋只读徽标，累计 `minHint = 1054`。
- 后果：即使源 1 修掉、域 Dock 按收口默认隐藏，主 Dock 仍缩不到 ui.md §4.4 规定的左栏最小内容尺寸 240 px（超限约 4.4 倍）——最小窗口（1280×720）场景下主窗口被钳在约 1390 px 无法到达 1280，中央区仍将被压至中央最小宽 18 px（≈归零）。
- 修复归属：P2 装配侧尺寸策略——顶栏同用流式栅格（换行承载），使主 Dock 恢复 §4.4 的可缩性。

### 中央区受害实证（P2 保障必要性）

- 度量拍实测 `central cur = 18×1234`（`geometry-report.txt` 首段）：中央三维视图 RWStudioView3D 在六 Dock 平铺下被挤压到 18 px（框架中央控件 `minimumSizeHint = 18×18`，Qt 布局层面几乎不设防）——"中央区归零"不是推测而是实测事实。

## 处置声明

- 钳制源已定位（两处），P2 不触发契约"钳制源无法定位时转 blocked"分支。
- P2 实施面：①源 1/源 2 经流式栅格消除（内容最小宽度坍缩）；②装配侧新增中央区最小可见宽度保障（插件侧事件观察＋`resizeDocks` 回推钳制——SA-02 框架零修改，不触碰框架中央控件自身属性），极端拖拽与最小窗口下中央区不归零。
