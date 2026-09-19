# WP-10-T02（UI-T02）门禁登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | UI-T02 构建落位（ui 单元 INTERFACE→STATIC＋三测试目标＋testkit_qt 启用；契约 `tasks/foundation/UI-T02.json`） |
| 日期 | 2026-09-19 |
| 提交方 | UI-T02 实施段（分支 `wp10-t02`，base `4d827120c5a28b7cbcec9adf7aa089d66688fb54`） |
| 接收方 | WP-01（构建边界与门禁横切——`ird_gates.cmake`/`ird_gates_whitelist.cmake` 数据面维护方，DTB §4.5 登记册 WP-01-T03 维护行） |
| 性质 | **登记提交件**——本文件只提交清单与建议，接收方处置后才生效；提交方不代行 `cmake/` 门禁数据侧改动（两文件均不在本任务 allowedFiles）；不私改门禁语义与白名单数据（EX-T10 提交件先例同口径——traceability/wp08-t10-gate-registrations.md） |
| 上游依据 | 需求 NFR-MNT-01；units/ui.md §3.1（R-3 唯一例外登记目标＋测试目标表）、§12.1；DTB §4.5（R-3 行"预登记（WP-10-T02 生效）"）；ird_gates_whitelist.cmake 文件头约定（人工登记册＝DTB §4.5 为准、机器面随登记同步回填）；CORE-T10/EV-T12/RT-T13/POL-T12/EX-T10 同型先例 |

---

## §1 R-3 例外机器面回填请求（本任务交付面直接触发）

### 1.1 触发事实

UI-T02 按契约 acceptance 1 将 `sdurws_ird_ui` 由 INTERFACE 占位升级 STATIC，并 PUBLIC 链接 `Qt6::Core/Gui/Widgets`（units/ui.md §3.1 目标表原文；本任务前 ui 为 INTERFACE 占位、无链接边，不触发 R-3 扫描）。ird_gates 第 2b 步对 product 形态目标的 Qt 链接做 R-3 检查，命中与否取决于机器面豁免表 `IRD_R3_EXCEPTION_TARGETS`——当前为空册（whitelist 注释自述"该例外以目标形态承载（_plugin／应用壳目标），不落入本门禁的『L2 裸计算库』扫描集合"，该假设与 ui.md §3.1 的落位形态不符：sdurws_ird_ui 本体即 R-3 例外目标，非 _plugin 形态）。

人工登记册（DTB §4.5）已有对应行且以本任务为生效点：

```
| R-3（Qt 禁入计算核心） | ui 单元界面目标（Widgets 唯一例外） | ARCH §3.2（SA-10 既有） | 预登记（WP-10-T02 生效） | 2026-09-10 |
```

### 1.2 回填请求

| 文件 | 请求 | 辖域说明 |
| --- | --- | --- |
| `cmake/ird_gates_whitelist.cmake` | `IRD_R3_EXCEPTION_TARGETS` 增补 `sdurws_ird_ui`；登记注释更新为精确范围文本：**例外范围仅 `sdurws_ird_ui` 与 ui.md §3.3 所列 ui 测试目标（sdurws_ird_ui_test/_contract_test/_gui_test），任何其他目标出现 Widgets 头即构建失败**（"仅 ui"——全产品唯一允许 Qt Widgets 的平台单元，NFR-MNT-01） | 机器消费面回填随人工登记册同步（文件头约定"两处必须一致，以 DTB §4.5 为准"）；登记册行已存在，无 DTB 增补需求 |
| `cmake/ird_gates.cmake` | `ird_classify_target` 增补 `_gui_test` 目标族识别（`sdurws_ird_<unit>_gui_test` → test 形态），或等价的白名单形态机制 | 见 §2.2——目标名 `sdurws_ird_ui_gui_test` 为 ui.md §3.1 v0.2 更正后的设计权威名（ARCH §1.4 命名约定），不可更名迁就解析正则；该目标族当前全产品仅 ui 一例（UI-T14 沿用），词表扩展影响面可控 |

### 1.3 回填后预期

回填后 `ird_gates` 命中集合回归基线 19（本任务引入的 8 处命中全消，见 §3）；后续任务（UI-T03+ 界面实现引入 Qt 头包含、workflow GUI 测试目标）落地时的同面扩展（含第 4b 步按单元扫描的 ui 产品面 Qt 头例外、testkit_qt 设施头的 testkit 单元例外）届时由对应任务按本件同口径登记。

## §2 本任务落位引入的门禁命中逐条归因（8 处，全部契约明文面）

### 2.1 R-3 链接面（3 处）

`sdurws_ird_ui` PUBLIC 链 `Qt6::Core`/`Qt6::Gui`/`Qt6::Widgets` 各命中一次：契约 acceptance 1 明文（"PUBLIC 链 RWS::ird::core＋RWS::ird::diagnostics＋Qt Core/Gui/Widgets"）；DTB §4.5 人工登记册已预登记（§1.1），机器面滞后。

### 2.2 测试目标链接 testkit（3×SUB＋1×T1）

三个 ui 测试目标（`sdurws_ird_ui_test`/`sdurws_ird_ui_contract_test`/`sdurws_ird_ui_gui_test`）按 DTB §5.5/§7.3 以自有 main 安装 testkit `TestRecordListener`（ird-test-report.json 留痕通道——AGENTS §4.2），直链 `sdurws_ird_testkit`：T-1 允许形态（testkit.md §2.4），F-173/F-115/F-221 同型既有登记（全仓 12 个测试目标基线同形态）。

其中 `sdurws_ird_ui_gui_test` 另产生 1×SUB（→被测目标 `sdurws_ird_ui`）＋1×T1（→testkit）：ird_gates `ird_classify_target` 的形态正则 `^sdurws_ird_([a-z]+)_(test|contract_test)$` 不识别 `_gui_test` 后缀（`[a-z]+` 不含下划线），该目标被判 other 形态——其"被测目标"链接被按产品面表外边检查、"testkit"链接被按产品目标 T-1 检查。目标名为设计权威名不可更名（ui.md §3.1 v0.2 更正注记）；机器面处置即 §1.2 词表扩展请求。

### 2.3 零实质违反声明

本任务交付面（acceptance 5/O-31 处置）：仅 ARCH §3.5 表内边 `ui→core`、`ui→diagnostics` 两条链接边＋R-3 例外登记；零指向 project/evidence/execution/policy/runtime 的链接或 include（配置期守卫＋`UiBuild.NoCrossUnitInclude_O31_UI_BUILD` 常驻扫描用例双面钉住）；ui 产品面 R-4 前缀拼接/剥离零命中（`NoRobWorkPrefixLiteral_R4_UI_BUILD`）；QShortcut 全局作用域零命中（`NoGlobalShortcutPrivatization_SA16_UI_BUILD`——SA-16/UI-HKY-3 口径）；P-UI-5 按 CF-1 口径（命令注册表类型归 ui 自有头）落位、ui→core 边照常建立。

## §3 ird_gates 命中集合状态（事实登记，非请求）

1. **基线复跑实证**：base `4d827120` detached worktree 复跑 `ird_gates`＝19 处命中（SUB×13＋LIB×4＋T1×1＋R4×1，全部为 findings.json/DTB §4.5 既有归属）；本分支工作树复跑＝27 处（新增恰为本件 §2 的 8 处，既有 19 处零新增零移除）——diff 证据 traceability/builds/wp10-t02/gates-hit-set-diff.txt（EX-T10 GATES-IDENTICAL 同型口径）。
2. **F-227 已登记**：本件 §2 的 8 处命中已登记 findings.json（F-227，severity=suggestion，owner=WP-01-T03）；本任务验收判定不受影响（EX-T10 F-221"不阻塞合入"同口径——命中均为契约明文＋人工登记册已登记/同型既有先例）。
3. **CI 执行面缺口**（既有，非本任务引入）：与 EX-T10 §2.3 登记的同一处缺口——CI run-tests 正则仍钉 `^sdurws_ird_core_test$`，ui 三测试目标（含 `ird_gui` 串行标签面）尚未进入 CI 执行面，归 WP-01-T02 CI 扩展处置，本件不重复登记。

## §4 提交方声明

- 本提交件内容为**事实登记与回填请求**，不改变任何需求/架构语义，不裁决任何未决项（O-31 维持登记未决——本任务交付面仅表内边，不在 O-31 阻断范围；P-UI-5 按 ui.md CF-1 结论落位，消账权在架构所有者）。
- 本提交件随 UI-T02 验收请求送验；接收方（WP-01-T03）处置后请在本文件追加处置记录（或于各自交付物中回链本文件）。
