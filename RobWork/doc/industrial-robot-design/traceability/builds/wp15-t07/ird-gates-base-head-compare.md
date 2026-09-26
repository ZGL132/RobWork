# WP-15-T07 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-27
- 任务：WP-15-T07（接入 policy 碰撞证据并落实 KIN-05 语义——④端口）
- 分支：wp15-t07；base＝82e56dfd6c73f6cc992b66b8594c239005a9a4bf
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T12～T16/T02～T06 确立口径），命中行归一化（剥离盘符绝对路径＋剥离
  `[ird_gates] ` 汇总行前缀）后逐行 diff；存量例外未清零（DTB §4.5），
  本任务不做清零。base 侧临时 worktree（../rw15t07_base_wt）用毕即删
  （已执行 worktree remove）。
- 计数口径声明（F-378）：本附件三套计数全程同口径并列——①直方图＝命中码 ×
  日志出现行数（grep -o 按码计数）；②去重条目＝归一化（剥绝对路径＋剥汇总
  行前缀）后 sort 的命中条目数（base_hits.norm/head_hits.norm 各 145 行、
  去重 69 条）；③引擎自计数＝日志末尾 `[ird_gates] 命中 N 项` 的引擎自报数。
  三套计数增量一致，均为 0（零新增、零消除）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（82e56dfd） | head（wp15-t07） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 12 | 12 | 0 |
| IRD-GATE-R1 | 7 | 7 | 0 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 75 | 75 | 0 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 145 | 145 | 0 |

去重后的命中条目数：base 69、head 69（`base_hits.norm`/`head_hits.norm`，
`base-head.diff` 为空——零新增命中、零消除）。

- 引擎自计数：base「命中 69 项」、head「命中 69 项」（ird_gates_base.log /
  ird_gates_head.log 末行）。
- 结论：本任务对 ird_gates 门禁面零增量（含 R-4/R-5 扫描面——碰撞接入
  全部经 policy④端口，无 proximity 直链、无名称拼串新增）。
- 退出码说明：base/head 引擎直跑退出码均为 1（存量命中 69 项未清零——
  DTB §4.5 存量例外既有状态，VS 目标 ird_gates 退出码 1 同源），非本任务
  新增。
