# wp10-t16 门禁命中集登记（UI-T16 / WP-10-T16）

- 任务：工作台宿主插件 `sdurws_ird_ui_plugin`（RobWorkStudio 集成入口＋开发期动态加载验证通道）
- base 修订：`e4d70ada744eb10df9bd203dcc59e3ae517b5f34`（redesign-main 分支 HEAD，UI-T16 契约发布提交）
- 分支：`wp10-t16`
- 日期：2026-09-23
- 引擎：`industrialrobot/cmake/ird_gates.cmake`（WP-01-T01；数据面 `ird_gates_whitelist.cmake`）
- 留痕：`traceability/builds/wp10-t16/ird_gates-base.txt`（base 全量命中，经 base
  工作树 `e4d70ada` 实跑）、`ird_gates-wp10-t16.txt`（本分支全量命中）、
  `ird_gates-hit-set-diff.txt`（比对口径＝命中行排序去重后逐行 diff，
  路径前缀归一化为 IRD_ROOT 相对路径）

## 命中集比对结论

恰增 **4 条唯一命中**：SUB×2（插件目标登记面——本登记册第 1 节）＋R3×2
（ui 例外目标源集文件级延伸——本登记册第 2 节）。其余各码
（R-1/R-2/R-4/R-5/T-1/T-2/GRAPH/LIB/SA02）命中集与 base **逐行一致，零变化**。

### §1 插件目标登记面（IRD-GATE-SUB×2——契约 acceptance 2 预期新增）

| 命中码 | 命中明细 | 登记依据 |
| --- | --- | --- |
| IRD-GATE-SUB | 表外依赖边 ui->project（sdurws_ird_ui_plugin → sdurws_ird_project）不在 ARCH §3.5 白名单 | DTB §4.5 登记行（2026-09-23 生效）：**宿主集成边**——插件装配层复用 harness PortAdapters 适配形态（O-38 裁决②："插件复用 harness PortAdapters 适配形态（ui 自有端口→project 对端，O-31 装配层特权边同款登记模式）"；UI-T15 同款 SUB 家族）。打开五步协议适配（StoreFactoryPortAdapter/StorePortAdapter/ProjectDiagnosticsBridge）编译入插件目标，ui 库产品面（sdurws_ird_ui 源集）零对 project 链接/include |
| IRD-GATE-SUB | 表外依赖边 ui->ui（sdurws_ird_ui_plugin → sdurws_ird_ui）不在 ARCH §3.5 白名单 | 同上登记行：插件目标链接本单元产品库的自边——与既有 `sdurws_ird_ui_app`（app 形态，UI-T15 登记）同型；插件目标被引擎分类为 plugin 形态（`_plugin` 后缀首次启用），命中行呈现为 `ui->ui` |

### §2 ui 例外目标源集文件级延伸（IRD-GATE-R3×2——既有例外域，非新例外形态）

| 命中码 | 命中明细 | 登记依据 |
| --- | --- | --- |
| IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | ui/src/WorkbenchContent.cpp | 内容装配层实现 TU（§10.1 v1.10 壳拆分的语义主体——五区内容/命令设施/布局记忆编排；QMainWindow/QLabel/QMenu 等 Widgets 类头）。ui 是 R-3 例外本体目标 sdurws_ird_ui 的源集文件，与基线中 WorkbenchShell.cpp/ShellSupport.cpp 等命中同型同根源——同一例外登记范围（DTB §4.5"ui 单元界面目标（Widgets 唯一例外）"行），非新例外形态。机器面豁免回填请求沿 wp10-t03-gate-registrations.md §1（WP-01-T03 辖权），本件为该常驻请求的实施侧增量登记 |
| IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | ui/src/WorkbenchContent_p.hpp | 内容装配层私有实现头（R-2：src/ 不进 include/；QLabel/QListWidget/QStatusBar 等类头）。同上同型同根源 |

## 其余核对项

- **R-3 链接面零新增**：插件目标被引擎分类为 plugin 形态（不属于 R-3 链接面
  检查的 product/worker 集合——Qt 链接合法，DTB §5.1 v0.17 行"插件目标属
  ird_gates plugin 分类面（Qt 链接合法）"）；`sdurws_ird_ui` 既有 R-3 链接面
  命中行零变化。
- **目标集合 diff**：恰增 1 个目标 `sdurws_ird_ui_plugin`（MODULE；集成树专属
  ——独立冒烟模式无框架目标 sdurws，插件目标不注册，登记 ui.md §13 UI-T16
  落位登记注）；`sdurws_ird_ui` 等既有目标零变化（ui 库源集增列
  WorkbenchContent.cpp 属同一目标源集扩展，不新增目标）。
- **AUTOMOC 口径**：插件类 Q_OBJECT/插件元数据所需 AUTOMOC 仅该目标显式
  开启（DTB §5.1 v0.17 行；集成树全局 AUTOMOC 对既有目标本就生效且零
  Q_OBJECT 无 moc 产物——构建形态零变化，见 ui/CMakeLists.txt 插件段注释）。
- **DTB §4.5 登记册**：随本任务新增"SUB（ARCH §3.5 表外边）"行一条
  （2026-09-23，插件目标 ui->project＋ui->ui 两边一行登记），机器消费面
  （本文件）与人工登记册（DTB §4.5）一致。

## 责任方

登记册维护责任方：WP-01-T03（ird_gates 白名单与 DTB §4.5 双面一致性；
R-3 头包含面机器豁免的回填权）。
本提交件为实施侧自登记（沿 UI-T03/T05/T13/T15 同款流程），验收段核对。
