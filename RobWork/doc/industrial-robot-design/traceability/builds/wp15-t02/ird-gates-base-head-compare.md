# WP-15-T02 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-26
- 任务：WP-15-T02（kinematics 构建落位）
- 分支：wp15-t02；base＝bcf22c083a3349f0a1e519d5619af31fe4b4ad84
- 口径：引擎直跑（cmake -P ird_gates.cmake，T12～T16 确立口径），命中行归一化
  （剥离盘符绝对路径＋剥离 `[ird_gates] ` 汇总行前缀）后逐行 diff；存量例外未
  清零（DTB §4.5），本任务不做清零。base 侧临时 worktree（../rw15t02_base_wt）
  用毕即删。
- 计数口径声明（F-378）：本附件两套计数全程同口径——①直方图＝命中码 × 日志
  出现行数（message 即时行＋`[ird_gates]` 末尾汇总行均计，R1/SUB 部分含多行
  detail）；②去重条目＝归一化（剥绝对路径＋剥汇总行前缀）后 sort -u 的命中
  条目数。两套计数的差值恒等于重复呈现行数，不混用。

## 直方图（命中码 × 日志出现行数）

| 码 | base（bcf22c08） | head（工作区） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 10 | 10 | 0 |
| IRD-GATE-R1 | 7 | 7 | 0 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 71 | 75 | +4 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 139 | 143 | +4 |

去重后的命中条目数：base 73、head 75（`ird_gates_{base,head}_hits.norm` 逐行
diff＝4 行新增，为 2 条新命中各自的 message 行＋汇总行）。
引擎自计数（日志末尾汇总原文，F-378 第三口径并列声明）：base 66 处、head
68 处——增量同＋2，三套口径的增量一致（+2），差异仅在重复呈现行是否计数。

## 新增命中（head − base，去重后恰 2 条，直方图 +4 为两处命中各计两行所致）

1. `IRD-GATE-SUB: 测试目标 sdurws_ird_kinematics_test 直链他单元产品目标
   sdurws_ird_testkit（允许形态仅同单元被测目标；跨单元需求经 testkit 替身
   或 DTB §4.5 登记）`
2. `IRD-GATE-SUB: 测试目标 sdurws_ird_kinematics_contract_test 直链他单元
   产品目标 sdurws_ird_testkit（同上）`

## 新增命中定性（供验收者裁决；实现侧不私改白名单/登记册）

- 模式归属：与 base 侧既有同型命中完全一致（各单元 `_test`/`_contract_test`
  → `sdurws_ird_testkit`——见 ird_gates_base.log 同码段），即各单元测试目标
  消费 testkit 报告设施（TestRecordListener→ird-test-report.json）时引擎
  "测试目标直链他单元产品目标"判定的存量例外模式。
- sanctioned 依据：
  - 任务契约 WP-15-T02.json acceptance 2："`_test`/`_contract_test` 目标按
    §3.2 注册（自持 ird_add_gtest）"——测试目标落位即含其链接面；
  - units/testkit.md §2.4 T-1 允许形态（引擎白名单文件头同文）："测试目标 →
    { 同单元产品目标, sdurws_ird_testkit, gtest 系 }"——testkit 仅测试目标
    可链，产品目标零 testkit（本单元契约测试
    KinBuildGraph.NoTestkitEdgeOnProductTarget_WP15T02_ACC3 与配置期守卫
    双重钉住）；
  - 先例：IO-T07/PRJ-T15/WP-13-T02/WP-14-T02 同款报告设施接入
    （ird-test-report.json 与 gtest XML 并存——AGENTS §4.2 验证留痕）。
- 登记册状态：DTB §4.5 对该模式已有存量登记（未清零）；本次 +2 为同模式新
  实例，登记册回填属 WP-01-T03 治理面（与 WP-14-T02 落位时同款处置——照实
  留痕交验收侧）。
- GRAPH/SA02/SELF 检查：head 侧零命中——dependency-graph.json 已随任务同步
  刷新（kinematics 六边入图，⊆ 方向成立），与 IRD_EXTRA_EDGE_REFS 双面留痕。

## 附件清单

- `ird_gates_base.log`：base bcf22c08 引擎直跑全量日志（临时 worktree 用毕即删）
- `ird_gates_head.log`：head（本任务工作区）引擎直跑全量日志
- `base_histogram.txt` / `head_histogram.txt`：命中码直方图（口径①）
- `base_hits.norm` / `head_hits.norm`：归一化命中行清单（diff＝4 行新增，
  去重条目口径②下＝2 条新命中）
