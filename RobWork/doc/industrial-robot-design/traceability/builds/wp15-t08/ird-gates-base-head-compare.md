# WP-15-T08 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-27
- 任务：WP-15-T08（实现会话姿态承载与设默认命令门面——KIN-06/14）
- 分支：wp15-t08；base＝1b166a7f9a4529eda48dbbaed69a0b186e0c3aec
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T12～T16/T02～T07 确立口径），命中行归一化（剥离盘符绝对路径）后逐行
  diff；存量例外未清零（DTB §4.5），本任务不做清零。base 侧临时 worktree
  （D:/10_Source_Repos/21_robot/rw15t08_base_wt，基 1b166a7f）用毕即删。
- 计数口径声明（F-398——三套计数分别列明，不混标）：
  ①直方图＝命中码 × 日志出现行数（grep -o 按码计数）；
  ②去重条目＝归一化命中行（grep -o "IRD-GATE-XXX.*" 全行捕获）排序后
  sort -u 的条目数（base_hits.norm/head_hits.norm 各 145 行、去重 76 条；
  T07 附件去重 69 系其捕获模式差异——本任务两侧行同口径，增量判定不受
  影响）；
  ③引擎自计数＝日志末尾 `[ird_gates] 依赖红线/补丁门禁存在 69 处命中`
  的引擎自报数（base/head 均 69）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（1b166a7f） | head（wp15-t08） | 增量 |
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
- 结论：本任务对 ird_gates 门禁面零增量（Commands.hpp/.cpp 与两测试文件
  的 include 面全部落在六条登记边＋本单元内；CMake 增列未新增任何表外
  目标引用——新诊断码 KIN-ROOT-BYTES-ILLEGAL 为 kinematics 自有 §9.6
  登记码，不触 diagnostics 白名单面）。
- 退出码说明：base/head 引擎直跑退出码均为 1（存量命中未清零——DTB
  §4.5 存量例外既有状态，VS 目标 ird_gates 退出码 1 同源），非本任务新增。
