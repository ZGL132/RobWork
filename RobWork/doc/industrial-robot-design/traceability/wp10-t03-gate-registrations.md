# WP-10-T03（UI-T03）门禁登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | UI-T03 工作台壳与五区布局（WorkbenchShell/布局记忆/无项目首页；契约 `tasks/foundation/UI-T03.json`） |
| 日期 | 2026-09-20 |
| 提交方 | UI-T03 实施段（分支 `wp10-t03`，base `efed7256c618c583ede69493b2202ceb05ff0494`） |
| 接收方 | WP-01（构建边界与门禁横切——`ird_gates.cmake`/`ird_gates_whitelist.cmake` 数据面与 `gate-all.ps1` 维护方，DTB §4.5 登记册 WP-01-T03 维护行） |
| 性质 | **登记提交件**——只提交清单与建议，接收方处置后才生效；提交方不代行 `cmake/`、`scripts/` 门禁数据侧改动（两目录均不在本任务 allowedFiles）；不私改门禁语义与白名单数据（EX-T10/WP-10-T02 提交件先例同口径） |
| 上游依据 | 需求 NFR-MNT-01；units/ui.md §3.1（R-3 唯一例外登记目标——例外范围"仅本目标与 §3.3 所列 ui 测试目标"）、§12.1/§12.2（GUI 测试目标与执行纪律）；traceability/wp10-t02-gate-registrations.md §1.3（"UI-T03+ 界面实现引入 Qt 头包含……届时由对应任务按本件同口径登记"的预言点——本件即该登记） |

---

## §1 R-3 例外机器面扩展请求（本任务交付面直接触发）

### 1.1 触发事实

UI-T03 按 §13 行交付工作台壳实现，ui 产品面（`ui/src/`）首次引入 Qt 头包含（`WorkbenchShell.cpp`/`WorkbenchShell_p.hpp`/`ShellSupport.cpp` 三文件——§4.1 五区 DockWidget 组装、§4.5 QSettings 布局记忆等 Widgets 实现面）。ird_gates 第 4b 步对 ui 产品面做 Qt 类头扫描（"X+大写约定"启发式），命中 3 处（每文件 1 处）。

**基线隔离实证**（T02 gates-hit-set-diff 同型口径，脚本模式 `cmake -DIRD_ROOT=<tree> -DIRD_ENABLE_GIT=OFF -P cmake/ird_gates.cmake`，2026-09-20 实测）：

- base `efed7256`（detached worktree，源树扫描）＝**29 处命中**；
- 本分支工作树＝**32 处命中**；
- diff 恰为本节 3 处 R3（骨架证据 `traceability/builds/wp10-t03/gates-hit-set-diff.txt`＋原始日志 `gates-base-scriptmode.log`/`gates-branch-scriptmode.log`——既有 29 处零新增零移除）。

说明：base 29 处与 T02 登记的"变更后 27 处"差 2 处，为 T02 合入后其他任务（RPT-T13 波次等）合入所引入，与本任务无关且不在本件归因范围（base 复跑已将其隔离在基线侧）。

### 1.2 回填请求

| 文件 | 请求 | 辖域说明 |
| --- | --- | --- |
| `cmake/ird_gates_whitelist.cmake` / `ird_gates.cmake`（第 4b 步） | ui 产品面 Qt 类头扫描对 **ui 单元**整体豁免（或将 `ui` 加入第 4b 步例外单元表）；例外文本按 ui.md §3.1 R-3 登记原文钉住："全产品唯一允许 Qt Widgets 的平台单元，例外范围仅 `sdurws_ird_ui` 与 §3.3 所列 ui 测试目标" | R-3 例外的人工登记册（DTB §4.5"预登记（WP-10-T02 生效）"行）已覆盖"目标链接形态"（§1.2/§1.3 首例），本件请求的是同一例外的**头包含面**半区——ui 是唯一例外单元，其产品面包含 Qt 头正是例外本身，非违例；机器面数据滞后回填（文件头约定"以 DTB §4.5 为准"） |

### 1.3 零实质违反声明（acceptance 4/O-31 常驻自证全绿）

- 产品面零对 project/evidence/execution/policy/runtime 的链接或 include：`UiBuild.NoCrossUnitInclude_O31_UI_BUILD` 用例全绿（新源文件纳入扫描面后零命中）＋`UiLinkage` 三契约用例全绿（链接面仅 ui→core、ui→diagnostics 两条 §3.5 登记边）；
- R-4 前缀拼接/剥离、SA-16 QShortcut 全局作用域：零新增命中（`NoRobWorkPrefixLiteral_R4_UI_BUILD`/`NoGlobalShortcutPrivatization_SA16_UI_BUILD` 全绿）；
- 本任务新增命中**全部**为上表 3 处 R3 头包含面，无任何表外边/T-1/LIB/R4 新增。

## §2 gate-all.ps1 执行面缺口（本任务实测发现——登记为门禁脚本待办，非契约违反）

UI-T03 是全产品**首个**引入（a）真实 GUI 测试用例、（b）独立冒烟模式 Qt 依赖的单元，gate-all 两处供给缺口因此首次显形：

| # | 缺口 | 实测形态 | 归因与请求 |
| --- | --- | --- | --- |
| 1 | 集成测试步缺 GUI 执行环境供给 | `sdurws_ird_ui_gui_test` 在 gate-all 下 ctest 退出码 8——Qt 平台插件初始化失败（无 `QT_QPA_PLATFORM=windows`/`QT_QPA_PLATFORM_PLUGIN_PATH`）；同机按 ui.md §12.2 纪律直接运行**6/6 全绿**（证据 `traceability/builds/wp10-t03/ctest.log`＋`sdurws_ird_ui_gui_test.*`） | §12.2 执行纪律要求 VS x64 环境＋windows 平台插件——ctest 驱动 GUI 目标前需设置该环境（或ctest 属性注入）。归 WP-01 CI/门禁扩展（T02 提交件 §3 已登记的同一缺口家族，本件不重复开编号、仅补 GUI 环境维度） |
| 2 | 冒烟树配置步不传递 Qt 前缀 | gate-all 冒烟步 `cmake -S ... -B $SmokeDir -DCMAKE_TOOLCHAIN_FILE=$Toolchain` 未带 `-DCMAKE_PREFIX_PATH`（`$QtPrefix` 参数仅用于集成树全新配置步）——ui 单元 `find_package(Qt6 REQUIRED)`（ui.md §3.1 PUBLIC 链 Qt 三件套）在 standalone 冒烟无前缀即配置失败（F-007 同口径：Qt 前缀属调用方供给，单元侧 REQUIRED 失败即停为正确行为） | gate-all 冒烟步透传 `$QtPrefix`/`$env:CMAKE_PREFIX_PATH`（与集成步同款）。带 `-DCMAKE_PREFIX_PATH=<Qt>` 的冒烟配置＋构建零错误（证据 `traceability/builds/wp10-t03/smoke-configure.log`/`smoke-build.log`） |
| 3 | 非本任务面观察（不随本任务处置） | `sdurws_ird_testkit_test::ProductBoundary.ZeroTestkitHeadersAndTestIfdef_UT_FAULT` 在 base（efed7256）即失败：project/src/win32/IFileOps.hpp:12、ILockOps.hpp:14 的**注释散文**字面"#ifdef TEST"命中扫描（注释未剥离的既有扫描面议题） | 本分支对 project/testkit 零改动（`git diff efed7256 -- <project/testkit 路径>` 为空）；归属 testkit/project 侧任务处置，仅登记观测供所有者分派 |

## §3 findings 登记

本件 §1 的 3 处命中已登记 findings.json（F-259，severity=suggestion，owner=WP-01-T03）；本任务验收判定不受影响（F-227"命中均为契约明文＋人工登记册已登记/同型先例，不阻塞合入"同口径）。§2 的两项 gate-all 缺口随本件登记，接收方（WP-01）处置后请在本文件追加处置记录（或于各自交付物中回链本文件）。

## §4 提交方声明

- 本提交件内容为**事实登记与回填请求**，不改变任何需求/架构语义，不裁决任何未决项（O-31 已于 2026-09-19 裁决放行，本任务按其裁决形态落位——见 ui.md v0.5 §10.1 增量登记）；
- 本提交件随 UI-T03 验收请求送验；接收方（WP-01-T03）处置后请在本文件追加处置记录。
