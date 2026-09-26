# WP-15-T09 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-27
- 任务：WP-15-T09（实现结果筛选/导出/批量复算域内设施——KIN-08/AT-04）
- 分支：wp15-t09；base＝0d11551404cc5e26e876362fbf7eadcaafbce31e
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T12～T16/T02～T08 确立口径），命中行归一化（捕获 `IRD-GATE-` 起的整行并
  剥离盘符绝对路径前缀——base 侧临时 worktree 与主检出版本树根不同）后逐行
  diff；存量例外未清零（DTB §4.5），本任务不做清零。base 侧临时 worktree
  （D:/10_Source_Repos/21_robot/rw15t09_base_wt，基 0d115514）用毕即删。
- 计数口径声明（F-398——三套计数分别列明，不混标）：
  ①直方图＝命中码 × 日志出现行数（grep -o 按码计数）；
  ②去重条目＝归一化命中行（`grep -o "IRD-GATE-XXX.*"` 全行捕获、剥离绝对
  路径后）排序去重（sort -u）的条目数（base_hits.norm/head_hits.norm 各
  145 行、去重各 76 条；与 T08 附件同值——本任务未触及任何被门禁扫描的
  命中源）；
  ③引擎自计数＝日志末尾 `[ird_gates] 依赖红线/补丁门禁存在 69 处命中` 的
  引擎自报数（base/head 均 69）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（0d115514） | head（wp15-t09） | 增量 |
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

去重后的命中条目数：base 76、head 76（`base_hits.norm`/`head_hits.norm`
各 145 行；`base-head.diff` 为空——零新增命中、零消除）。

- 引擎自计数：base「命中 69 项」、head「命中 69 项」（ird_gates_base.log /
  ird_gates_head.log 末段）。
- 结论：本任务对 ird_gates 门禁面零增量（Export.hpp/.cpp、Recompute.hpp/
  .cpp 与三个测试文件的 include 面全部落在六条登记边＋本单元内；CMake
  增列未新增任何表外目标引用；无新增第三方依赖、无新诊断码——§9.6
  "不预建无消费者条目"）。退出码 1 为存量命中既有状态（DTB §4.5 存量
  例外未清零），非本任务引入。
- 附件清单：ird_gates_base.log / ird_gates_head.log（引擎原始输出）、
  base_hits.norm / head_hits.norm（归一化命中行，各 145 行）、
  base-head.diff（空）。
