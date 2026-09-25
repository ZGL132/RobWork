# WP-14-T02 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-25
- 任务：WP-14-T02（requirements 构建落位）
- 分支：wp14-t02；base＝e42de11423db492622bfd4759a3c0379eaad4e1a
- 口径：引擎直跑（cmake -P ird_gates.cmake，T12～T16 确立口径），命中行归一化
  （剥离盘符绝对路径）后逐行 diff；存量例外未清零（DTB §4.5），本任务不做清零。

## 直方图（命中码 × 行数）

| 码 | base（e42de114） | head（工作区） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 10 | 10 | 0 |
| IRD-GATE-R1 | 3 | 3 | 0 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 59 | 63 | +4 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 123 | 127 | +4 |

说明：uniq -c 直方图按"日志中出现次数"统计（每次命中在日志中出现两行——
message 即时行＋末尾汇总行，R1/R4/SUB 部分含多行 detail）；去重后的命中
条目数：base 58、head 60（`ird_gates_{base,head}_hits.norm` 逐行 diff）。

## 新增命中（head − base，去重后恰 2 条，直方图 +4 为两处命中各计两行所致）

1. `IRD-GATE-SUB: 测试目标 sdurws_ird_requirements_test 直链他单元产品目标
   sdurws_ird_testkit（允许形态仅同单元被测目标；跨单元需求经 testkit 替身
   或 DTB §4.5 登记）`
2. `IRD-GATE-SUB: 测试目标 sdurws_ird_requirements_contract_test 直链他单元
   产品目标 sdurws_ird_testkit（同上）`

## 新增命中定性（供验收者裁决；实现侧不私改白名单/登记册）

- 模式归属：与 base 侧**既有 36 条同型命中**完全一致（sdurws_ird_{diagnostics,
  evidence,execution,io,modeling,policy,project,reporting,runtime,ui} 各单元
  `_test`/`_contract_test` → `sdurws_ird_testkit`——见 ird_gates_base.log 同码
  段），即各单元测试目标消费 testkit 报告设施（TestRecordListener→
  ird-test-report.json）时引擎"测试目标直链他单元产品目标"判定的存量例外
  模式。
- sanctioned 依据：
  - 任务契约 WP-14-T02.json acceptance 4："_test/_contract_test 目标随文件
    注册（ird_add_gtest 自持宏，LABELS ird）"——测试目标落位即含其链接面；
  - units/testkit.md §2.4 T-1 允许形态（引擎白名单文件头同文）："测试目标 →
    { 同单元产品目标, sdurws_ird_testkit, gtest 系 }"——testkit 仅测试目标
    可链，产品目标零 testkit（本单元契约测试 ReqBuildGraph.NoTestkitEdgeOn-
    ProductTarget_WP14T02_ACC2 与配置期守卫双重钉住）；
  - 先例：IO-T07/PRJ-T15/WP-13-T02 同款报告设施接入（ird-test-report.json
    与 gtest XML 并存——AGENTS §4.2 验证留痕）。
- 登记册状态：DTB §4.5 对该模式已有 36 条存量登记（未清零）；本次 +2 为
  同模式新实例，登记册回填属 WP-01-T03 治理面（与 WP-13-T02 落位时建模侧
  同款处置——当时亦未私改登记册，照实留痕交验收侧）。

## 附件清单

- `ird_gates_base.log`：base e42de114 引擎直跑全量日志（临时 worktree 用毕即删）
- `ird_gates_head.log`：head（本任务工作区）引擎直跑全量日志
- `base_histogram.txt` / `head_histogram.txt`：命中码直方图
- `base_hits.norm` / `head_hits.norm`：归一化命中行清单（diff＝2 行新增）
