# WP-10-T05（UI-T05 阶段 A）门禁登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | UI-T05 阶段 A：中央区三维视图占位与视图契约登记（契约 `tasks/foundation/UI-T05.json`；交互实现归阶段 B） |
| 日期 | 2026-09-21 |
| 提交方 | UI-T05 实施段（分支 `wp10-t05`，base `c17ac8348a82df9ab275002d9715a69834531db7`） |
| 接收方 | WP-01（构建边界与门禁横切——`ird_gates.cmake`/`ird_gates_whitelist.cmake` 数据面维护方，DTB §4.5 登记册 WP-01-T03 维护行） |
| 性质 | **登记提交件**——只提交清单与建议，接收方处置后才生效；提交方不代行 `cmake/`、`scripts/` 门禁数据侧改动（两目录均不在本任务 allowedFiles）；不私改门禁语义与白名单数据（WP-10-T02/T03 提交件先例同口径） |
| 上游依据 | 需求 NFR-MNT-01；units/ui.md §3.1（R-3 唯一例外登记目标——例外范围"仅本目标与 §3.3 所列 ui 测试目标"）、§16.7 v0.7（本任务落位登记）；traceability/wp10-t03-gate-registrations.md §1（同型先例——ui 产品面新增源文件的 R-3 头包含半区机器面回填） |

---

## §1 R-3 例外机器面扩展请求（本任务交付面直接触发）

### 1.1 触发事实

UI-T05 阶段 A 按 §13 行交付中央区三维视图占位面板，ui 产品面（`ui/src/`）新增实现 TU `View3DPlaceholder.cpp`（QLabel/QVBoxLayout 等 Widgets 实现面——契约显式设计的占位呈现）。ird_gates 第 4b 步对 ui 产品面做 Qt 类头扫描（"X+大写约定"启发式），对该文件命中 1 处。

**基线隔离实证**（集成模式构建树 `cmake --build build --config Release --target ird_gates`，与 WP-10-T04 留痕同一执行面，2026-09-21 实测；原始日志＋归一化比对见 `traceability/builds/wp10-t05/gates-branch-run.log`＋`gates-hit-set.txt`）：

- WP-10-T04 合入后基线＝**32 处命中**；
- 本分支工作树＝**33 处命中**；
- diff 恰为本节 1 处 R3（`ui/src/View3DPlaceholder.cpp` 头包含面）——既有 32 处零新增零移除。

同型先例：WP-10-T03 引入 ShellSupport.cpp/WorkbenchShell.cpp/WorkbenchShell_p.hpp 三处同类命中（F-259，责任方 WP-01-T03）——本件为其同型增量，非新形态。

### 1.2 回填请求

| 文件 | 请求 | 辖域说明 |
| --- | --- | --- |
| `cmake/ird_gates_whitelist.cmake` / `ird_gates.cmake`（第 4b 步） | 与 WP-10-T03 提交件 §1.2 同一请求（ui 产品面 Qt 类头扫描对 **ui 单元**整体豁免）——本文件作为该请求的同型增量登记，不重复开立请求条款 | R-3 例外的人工登记册（DTB §4.5"预登记（WP-10-T02 生效）"行）已覆盖"目标链接形态"；ui 是唯一例外单元，其产品面包含 Qt 头正是例外本身，非违例；机器面数据滞后回填（文件头约定"以 DTB §4.5 为准"）。若接收方按 WP-10-T03 件以"ui 单元整体豁免"处置，本件 1 处命中自动消账，无需单独条目 |

### 1.3 零实质违反声明（O-31 常驻自证全绿）

- 产品面零对 project/evidence/execution/policy/runtime 的链接或 include：`UiBuild.NoCrossUnitInclude_O31_UI_BUILD` 用例全绿（新源文件/新公共头纳入扫描面后零命中）＋`UiLinkage` 三契约用例全绿（链接面仅 ui→core、ui→diagnostics 两条 §3.5 登记边）；
- R-4 前缀拼接/剥离、SA-16 QShortcut 全局作用域：零新增命中（`NoRobWorkPrefixLiteral_R4_UI_BUILD`/`NoGlobalShortcutPrivatization_SA16_UI_BUILD` 全绿）；
- 本任务新增命中**全部**为上表 1 处 R3 头包含面，无任何表外边/T-1/LIB/R4 新增。

## §2 findings 登记

本件 §1 的 1 处命中建议随同型先例并入 F-259 追踪（severity=suggestion，owner=WP-01-T03）；本任务验收判定不受影响（F-227"命中均为契约明文＋人工登记册已登记/同型先例，不阻塞合入"同口径）。接收方（WP-01）处置后请在本文件追加处置记录（或于各自交付物中回链本文件）。

## §3 提交方声明

- 本提交件内容为**事实登记与回填请求**，不改变任何需求/架构语义，不裁决任何未决项（O-31 已于 2026-09-19 裁决放行——本任务交付物零持有 C-3/4/5/7/8/10/11 对端类型，C-11 共同消费点归阶段 B 复核）；
- 本提交件随 UI-T05 验收请求送验。
