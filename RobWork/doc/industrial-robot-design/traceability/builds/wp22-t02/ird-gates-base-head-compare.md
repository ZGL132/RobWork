# WP-22-T02 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-10-06
- 任务：WP-22-T02（workflow 构建落位——占位转真实库＋插件目标）
- 分支：wp22-t02；base＝0df67aede（redesign-main HEAD，wp22-t02 分支起点）
- 口径：引擎直跑（cmake -P ird_gates.cmake，T12～T16 确立口径；WP-15-T02
  附件同款），命中行归一化（剥离盘符绝对路径＋剥离 `[ird_gates] ` 汇总行
  前缀）后逐行 diff；存量例外未清零（DTB §4.5 登记册既有状态），本任务
  不做清零。base 侧临时 worktree（../rw22t02_base_wt，IRD_ENABLE_GIT=OFF）
  用毕即删。
- 计数口径（WP-15-T02 附件 F-378 三口径并列）：①直方图＝命中码 × 日志
  出现行数（message 即时行）；②去重条目＝归一化后 sort -u 命中条目数；
  ③引擎自计数＝日志末尾汇总原文。本次三口径增量一致（+3 呈现行／+3 去重
  条目／base 103→head 106），无重复呈现差异。

## 直方图（命中码 × 日志出现行数）

| 码 | base（0df67aed） | head（工作区） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 6 | 6 | 0 |
| IRD-GATE-R1 | 6 | 6 | 0 |
| IRD-GATE-R3 | 30 | 30 | 0 |
| IRD-GATE-R4 | 1 | 1 | 0 |
| IRD-GATE-SUB | 56 | 59 | +3 |
| IRD-GATE-T1 | 4 | 4 | 0 |
| 合计（引擎自计数） | 103 | 106 | +3 |

去重后的命中条目数：base 103、head 106（base_hits.norm／head_hits.norm
逐行 diff＝恰 3 行新增，见下）。

## 新增命中（head − base，去重后恰 3 条）

1. `IRD-GATE-SUB: 测试目标 sdurws_ird_workflow_test 直链他单元产品目标
   sdurws_ird_testkit（允许形态仅同单元被测目标；跨单元需求经 testkit
   替身或 DTB §4.5 登记）`
2. `IRD-GATE-SUB: 测试目标 sdurws_ird_workflow_contract_test 直链他单元
   产品目标 sdurws_ird_testkit（同上）`
3. `IRD-GATE-SUB: 表外依赖边 workflow->workflow（sdurws_ird_workflow_plugin
   → sdurws_ird_workflow）不在 ARCH §3.5 白名单`

## 新增命中定性（供验收者裁决；实现侧不私改引擎/白名单塞自边）

- **第 1/2 条（testkit 两 SUB）模式归属**：与 base 侧既有同型命中完全一致
  （各单元 `_test`/`_contract_test` → `sdurws_ird_testkit`——ird_gates_base.log
  同码段），即各单元测试目标消费 testkit 报告设施
  （TestRecordListener→ird-test-report.json）的存量例外模式。
- 第 1/2 条 sanctioned 依据：
  - 任务契约 WP-22-T02.json acceptance 1："`_test`/`_contract_test` 目标
    随文件注册（ird_add_gtest 同款）"——测试目标落位即含其链接面；
  - units/testkit.md §2.4 T-1 允许形态（引擎白名单文件头同文）："测试目标
    → { 同单元产品目标, sdurws_ird_testkit, gtest 系 }"——testkit 仅测试
    目标可链，产品目标零 testkit（本单元契约测试
    WfBuildGraph.NoTestkitEdgeOnProductTarget_WP22T02_ACC1 与配置期守卫
    双重钉住）；
  - 先例：IO-T07/PRJ-T15/WP-13-T02/WP-14-T02/WP-15-T02 同款报告设施接入
    （ird-test-report.json 与 gtest XML 并存——AGENTS §4.2 验证留痕）；
    WP-15-T02 的 base..head 比对即出现同模式 +2，登记册回填归 WP-01-T03。
- **第 3 条（plugin 自边 SUB）模式归属**：sdurws_ird_workflow_plugin（插件
  界面目标）→ sdurws_ird_workflow（本单元产品目标）的同单元装配边。引擎
  按"产品目标之间边必须落 §3.5 白名单"粗粒度判定；自边不是架构依赖边，
  ARCH §3.5 无自边条目。base 侧既有同型命中：modeling_plugin→modeling／
  requirements_plugin→requirements／kinematics_plugin→kinematics（R-1 码，
  因三单元属 IRD_BUSINESS_UNITS；workflow 是编排单元不在业务域清单故落
  SUB 码——同一边的两种码面）。sanctioned 依据：
  - 任务契约 WP-22-T02.json acceptance 1 明文"创建 _plugin 目标（最小可
    注册实现）"——插件目标链接本单元计算库是 §10.9 装配门面（assembly/
    WorkflowPluginAssembly）消费 ui::IPluginUiModule 契约的结构前提；
  - units/workflow.md §3.2 目标表 `_plugin` 行＋ui.md §11.2（IPluginUiModule
    实现即插件目标的定义性内容）；
  - DTB §4.5 登记册对业务域 plugin 自边已有存量登记（未清零）；workflow
    侧同模式新实例（SUB 码面），登记册回填归 WP-01-T03 治理面。
- **零新增项核对**：R-1（无业务域互链——acceptance 2）、R-3（计算库产品
  面 include/**＋src/** 零 Qt；plugin/ 与 assembly/ 不在门禁产品面扫描域）、
  R-4/R-5/T-1/T-2/LIB 全部零变化；GRAPH（八边白名单登记＋
  dependency-graph.json 随任务同步刷新，⊆ 方向成立）、SA02（框架零修改）、
  SELF（引擎自测）head 侧零命中。

## ird_gates 构建目标口径说明（契约 verify 第 2 条）

`cmake --build build --config Release --target ird_gates`（ird-gates-target.log）
退出码 1＝全仓 106 处命中（103 存量＋3 新增）触发引擎 FATAL——这是
DTB §4.5 登记册既有存量状态的如实机器呈现（WP-15-T02 及其后各落位任务
同口径：目标失败＝机器面滞后既有状态，本任务不私改门禁/白名单清零存量，
新增 3 处如上定性交验收侧）。

## 附件清单

- `ird_gates_base.log`：base 0df67aed 引擎直跑全量日志（临时 worktree 用毕即删）
- `ird_gates_head.log`：head（本任务工作区）引擎直跑全量日志
- `base_histogram.txt` / `head_histogram.txt`：命中码直方图（口径①）
- `base_hits.norm` / `head_hits.norm`：归一化命中行清单（diff＝3 行新增）
- `ird_gates-target.log`：ird_gates 构建目标运行日志（口径见上节）
