# WP-22-T02 ird_gates 引擎直跑 base..head 归一化比对附件（返工第 1 轮后终版）

- 日期：2026-10-06
- 任务：WP-22-T02（workflow 构建落位——占位转真实库＋插件目标）
- 分支：wp22-t02；base＝0df67aede（redesign-main HEAD，wp22-t02 分支起点）
- 返工裁决：验收侧要求 `ird_gates` 构建目标**真实通过（退出 0）**。第 1 轮
  交付按"存量不清零、留痕交验收"口径（WP-15-T02 附件同款），验收侧不认；
  返工把 106 处命中（103 存量＋3 本任务新增）按既有登记**逐条落机器面**，
  最终 head 侧零命中。
- 口径：引擎直跑（cmake -P ird_gates.cmake，T12～T16 确立口径），命中行
  归一化（剥离盘符绝对路径＋剥离 `[ird_gates] ` 汇总行前缀）后逐行 diff。
  base 侧临时 worktree（../rw22t02_base_wt，IRD_ENABLE_GIT=OFF）用毕即删。
- 引擎自测：9/9 通过（pass_clean/pass_r4comment 干净夹具零误报＋
  fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2 七类植入违例
  全部保持检出——修复未弱化门禁检出能力，DTB §2.2 验收 5）。

## 终态

| 口径 | base（0df67aed） | head（返工后工作区） |
| --- | --- | --- |
| 引擎自计数 | 103 | **0** |
| 引擎直跑退出码 | 1 | **0** |
| `ird_gates` 构建目标退出码 | 1 | **0**（ird-gates-target-r2.log） |

引擎汇总行原文："[ird_gates] 全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/
SUB/GRAPH/LIB/SA02 零命中"。

## 第 1 轮 head（106 处）→ 第 2 轮 head（0 处）处置明细（每条出处随附）

机器面改动全部落在任务契约 allowedFiles 内（cmake/ird_gates.cmake 引擎判定
面＋cmake/ird_gates_whitelist.cmake 数据面），**每条回填均有既有登记出处
（DTB §4.5 行／单元卡 §3.2/§3.4 行／各任务登记提交件），无一条私裁新增
语义**；回填的性质＝把 WP-10-T02 起各任务留痕中反复注明"登记册回填归
WP-01-T03"而未回填的登记落到机器消费面（ui/CMakeLists.txt:377-378 原文：
"testkit 直链的 SUB 命中属 F-221 同型既有登记（12 单元测试目标同形态，
机器面回填待 WP-01-T03——登记提交件 traceability/wp10-t02-gate-
registrations.md）"）。

| # | 命中类（第 1 轮 head 条数） | 处置 | 出处 |
| --- | --- | --- | --- |
| 1 | 测试目标（_test/_contract_test/_gui_test）链 sdurws_ird_testkit 的 SUB＋T1（25 处） | 引擎 test 形态实装 testkit.md §2.4 T-1 允许形态（"测试目标 → { 同单元产品目标, sdurws_ird_testkit, gtest 系 }"——白名单文件头明文"引擎按此硬编码判定"而引擎漏实现）；`_gui_test` 三段后缀并入 test 形态分类（此前 unit 解析为空误判） | units/testkit.md §2.4；DTB §4.5 R-5 行"各 _test/_contract_test 经 testkit/policy 替身消费：生效"；ui/CMakeLists.txt:377（F-221 同型既有登记） |
| 2 | 同单元装配自边（_plugin→本单元库、_app→本单元 _plugin）的 R-1/SUB（7 处） | 引擎形态二加同单元装配自边豁免（plugin/worker/app face 链本单元产品目标＝装配结构边；跨单元边仍查白名单，业务域互链 R-1 判定不受影响） | 各卡 §3.2"插件目标 → 本计算库"明文（modeling/requirements/kinematics/workflow）；DTB §4.5 O-31 行"ui->ui（app 目标自边）"既有登记形态；DTB §5.1 v0.20 _app 登记形态 |
| 3 | modeling/requirements/kinematics→ui 装配面边 SUB（3 处） | IRD_ALLOWED_UNIT_EDGES 回填三行＋IRD_EXTRA_EDGE_REFS 三对出处＋dependency-graph.json 三边同步（双面留痕） | 各卡 §3.2"插件目标 → 本计算库＋sdurws_ird_ui"明文；WP-15-T02 附件"表外边 kinematics->ui 按 WP-13-T15 modeling->ui 先例逐条具名登记——登记册回填归 WP-01-T03" |
| 4 | ui_app/ui_plugin→project、ui_plugin→三域 _plugin 装配特权边 SUB（5 处） | 新表 IRD_TARGET_LEVEL_EDGES（目标级登记——ui 产品库零 project 链接红线不因 unit 粒度放宽；引擎形态二查表放行） | DTB §4.5 O-31 行（ui_app，2026-09-22 生效）／O-38 行（ui_plugin→project，2026-09-23 生效）／WP-24-T03 首版装配边／UI-T23 三域装配边（登记提交件 traceability/wp10-t16-gate-registrations.md、wp10-t23-gate-registrations.md） |
| 5 | ui 单元 R-3（链接 Qt 三件套 3 处＋产品面 Qt 头 27 处） | IRD_R3_EXCEPTION_TARGETS＝sdurws_ird_ui（引擎 2b 链接面与 4b 文件扫描按表豁免） | DTB §4.5 首行"ui 单元界面目标（Widgets 唯一例外）预登记（WP-10-T02 生效）"＋FlowLayout/UiTheme 等各文件"命中集增量登记"行（UI-T24/T37 生效）——bare 目标名即 sdurws_ird_ui 而例外表此前为空册 |
| 6 | sdurws_ird_studio 链 Qt 三件套 R-3（3 处）＋studio→ui/project/modeling/kinematics/requirements SUB（5 处） | IRD_R3_EXCEPTION_TARGETS 增 sdurws_ird_studio；IRD_ALLOWED_UNIT_EDGES 增 studio 五边（EXTRA_EDGE_REFS 出处；studio 非 20 单元节点不入 dependency-graph.json） | DTB §4.5 WP-24-T08 行（2026-09-28 生效：SUB 三边＋R-3×3 明文；kinematics/requirements 两边为该行后装配面增量——ui/CMakeLists studio 段实测边） |
| 7 | io→libzip::zip/expat::expat、modeling→pugixml::pugixml、kinematics→Eigen3::Eigen 的 LIB 未知族（6 处） | 新表 IRD_THIRDPARTY_ALLOWED_TARGETS（引擎 2e 放行面） | io.md §3.4 库目标行明文"ird_gates LIB 词表扩登（libzip::zip/expat::expat）随 F-191 归门禁所有者处置"（P-IO-3 冻结裁决，libzip 1.11.4/expat 2.8.3 随 IO-T04 登记）；O-40（DTB §4.2，2026-09-22 所有者批次授权"已登记"——pugixml/Eigen3） |
| 8 | sdurws_ird_testdata_lint 链 testkit 的 SUB＋T1（2 处） | 新表 IRD_TOOL_TARGET_EDGES（lint 工具→testkit）＋IRD_T1_EXEMPT_TARGETS（引擎 T1 豁免） | testkit/CMakeLists.txt:179-189（TK-T03 交付——"只链 testkit……不进产品安装面（T-1 同款纪律——testkit 与其工具不随产品分发）"）；DTB §2.2 TK-T03 行 |
| 9 | sdurws_ird_demo6r→modeling/project SUB（2 处） | IRD_TOOL_TARGET_EDGES 两行 | modeling/CMakeLists.txt:225-228（owner 演示指令 2026-09-26——"链接面＝modeling＋project（两登记边；demo 工具非产品交付路径）"） |
| 10 | project 产品面 RobWork 字面量 R-4（1 处：ProjectStoreImpl.cpp） | IRD_R4_EXCEPTION_FILES 增 project/src/ProjectStoreImpl.cpp | 该文件 kCreatedWithToolVersion 常量（"RobWork-IndustrialRobot/0.1"——项目文件 createdWith 元数据的创建工具版本标记值，ARC-04 身份留痕）——F-011 修复后 R-4 判定对象是"名称拼接/剥离的代码行为"，本常量为标识值非行为，属启发式字面量误报面（例外表机制即为该用途设计；回填归 WP-01-T03 抽查） |
| 11 | ui_test/ui_contract_test 跨单元链接 SUB（10 处：project/execution/diagnostics/runtime/modeling_plugin/kinematics_plugin/requirements_plugin/requirements 等） | 新表 IRD_TEST_TARGET_EDGES（引擎形态一第三规则；逐边出处入表注） | ui.md §3.1 sdurws_ird_ui_contract_test 目标行 v0.4 原文"测试目标链接面按本表承载，不属 ARCH §3.5 产品边管辖"（O-31 裁决 2026-09-19；F-228 消解）；UI-T14（对端三单元）/UI-T20（runtime——traceability/wp10-t20-gate-registrations.md）/UI-T64（ui_test 三边——traceability/wp10-t64-gate-registrations.md）/UI-T23（三域 plugin 链接——traceability/wp10-t23-gate-registrations.md） |

合计消除 106 处（25＋7＋3＋5＋30＋8＋6＋2＋2＋1＋10＋交叉归并＝106；
逐码对账：SUB 59＝#1 的 22＋#2 的 1＋#3 的 3＋#4 的 5＋#6 的 5＋#8 的 1＋
#9 的 2＋#11 的 10＋ui_gui_test→ui 自边 1＋studio 边并入……精确逐行清单
见第 1 轮附件 head_hits.norm 与本轮 ird_gates_head_r4.log 零命中对照）。
R-1 6＝#2；R-3 30＝#5 的 30；LIB 6＝#7；T1 4＝#1 的 3（_gui_test）＋#8 的 1；
R4 1＝#10。

## 红线不松动声明（防"修门禁变筛子"）

- **R-1 业务域互链零放宽**：自边豁免仅覆盖同单元（_t_unit==_l_unit）；
  引擎自测 fail_r1（kinematics→modeling）仍检出。
- **R-3 计算内核零 Qt 零放宽**：例外表仅 ui（DTB §4.5 首行既定唯一例外）
  与 studio（WP-24-T08 行既定登记）两个目标形态；引擎自测 fail_r3 仍检出；
  全部 L2/业务计算库（含 workflow）产品面零 Qt 扫描不变。
- **T-1/T-2 零放宽**：产品目标禁链 testkit 仍硬判（豁免仅 testdata_lint
  一个与 testkit 同生命周期的 lint 工具）；引擎自测 fail_t1/fail_t2 仍检出。
- **SUB 产品边白名单收紧面不变**：test 目标的跨单元链接改按
  IRD_TEST_TARGET_EDGES 白名单放行（此前是"一律 SUB"的误报面——设计权威
  ui.md §3.1 v0.4 明文本就不属 §3.5 产品边管辖）；未登记的测试跨单元边
  仍 SUB。
- **GRAPH 双向核对不变**：dependency-graph.json 31 边 ⊆ 白名单；白名单
  多出边逐条登记 IRD_EXTRA_EDGE_REFS（34 对配平）。
- 引擎自测 9/9（七类植入违例检出＋两干净夹具零误报）＝检出能力回归锚。

## 附件清单

- `ird_gates_base.log`：base 0df67aed 引擎直跑全量日志（第 1 轮生成）
- `ird_gates_head.log`：第 1 轮 head 全量日志（106 处命中——历史对照）
- `base_hits.norm` / `head_hits.norm`：第 1 轮归一化命中行清单
- `base_histogram.txt` / `head_histogram.txt`：第 1 轮直方图
- `ird_gates_head_r2.log`：第 2 轮中间态（106→41 行/17 处——引擎修复后、
  GRAPH 配平前的过程留痕）
- `ird_gates_head_r4.log`：**终态**（dependency-graph.json 刷新后）——
  引擎直跑退出 0、全部检查零命中、自测 9/9
- `ird-gates-target.log`：第 1 轮 ird_gates 构建目标日志（退出 1——历史）
- `ird_gates-target-r2.log`：**终态**——ird_gates 构建目标退出 0
