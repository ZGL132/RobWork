# WP-15-T05 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-26
- 任务：WP-15-T05（批量任务点验证评估器 kin.task-points-batch）
- 分支：wp15-t05；base＝1ae740cfab4e78187bc386c0550d224293802f2a
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T12～T16/T02～T04 确立口径），命中行归一化（剥离盘符绝对路径＋剥离
  `[ird_gates] ` 汇总行前缀）后逐行 diff；存量例外未清零（DTB §4.5），
  本任务不做清零。base 侧临时 worktree（../rw15t05_base_wt）用毕即删。
- 计数口径声明（F-378）：本附件三套计数全程同口径并列——①直方图＝命中码 ×
  日志出现行数（grep -o 按码计数）；②去重条目＝归一化（剥绝对路径＋剥汇总
  行前缀）后 sort 的命中条目数（base_hits.norm/head_hits.norm 各 69 行）；
  ③引擎自计数＝日志末尾 `[ird_gates] 命中 N 项` 的引擎自报数。三套计数增量
  一致，均为 0（零新增、零消除）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（1ae740cf） | head（wp15-t05） | 增量 |
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

去重后的命中条目数：base 69、head 69（`base_hits.norm`/`head_hits.norm`
逐行 diff＝**空 diff**——归一化后逐字节一致，base-head.diff 0 行）。

引擎自计数：base 69 项、head 69 项（增量 0）。

## 结论

本任务（Evidence.hpp/.cpp 新增＋Evaluators.hpp/.cpp 表尾追加＋DiagCodes
行 16 KIN-POINT-REF-DANGLING 表尾追加＋两测试目标扩容）**零新增命中、零
消除命中**：全部命中均为 DTB §4.5 登记的存量例外既有状态（含 sanctioned
形态 O-40 Eigen PRIVATE 等——base 侧已含，本次无白名单触碰、无私改白
名单）。

引擎退出码两侧均为 1（存量命中既有状态——DTB §4.5 存量例外未清零，
base=head=1 属同一既有事实，非本任务引入）。

## 附：产物清单

| 文件 | 内容 |
| --- | --- |
| ird_gates_base.log | base worktree（1ae740cf）引擎直跑全量输出 |
| ird_gates_head.log | head（wp15-t05 工作树）引擎直跑全量输出 |
| base_hits.norm | base 命中归一化表（69 行） |
| head_hits.norm | head 命中归一化表（69 行） |
| base-head.diff | 归一化 diff（0 行＝零差异） |
