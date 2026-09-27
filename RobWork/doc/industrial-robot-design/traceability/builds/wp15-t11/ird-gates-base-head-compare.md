# WP-15-T11 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-24
- 任务：WP-15-T11（产出失败点/薄弱区三维渲染数据——KIN-07）
- 分支：wp15-t11；base＝7093f9de3fd25fc2e06b81cce49334f153050faf
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T02~T10 确立口径），命中行归一化（捕获 `IRD-GATE-` 起的整行；base 侧临时
  worktree 与主检出的版本树根不同，故将两处树根前缀
  `D:/10_Source_Repos/21_robot/rw15t11_base_wt/RobWork/RobWorkStudio` 与
  `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio` 统一归一为
  `<IR>`——行内逐文件相对路径保留，逐文件可区分性不破坏）后比对；存量例外
  未清零（DTB §4.5），本任务不做清零。base 侧临时 worktree
  （D:/10_Source_Repos/21_robot/rw15t11_base_wt，基 7093f9de）用毕即删。
- 计数口径声明（F-398——三套计数分别列明，不混标）：
  ①直方图＝命中码 × 日志出现行数（归一化行 `grep -o "IRD-GATE-[A-Z0-9]*"`
  按码计数）；
  ②去重条目＝归一化命中行排序去重（sort -u）的条目数（base_hits.norm/
  head_hits.norm 各 145 行、去重各 76 条；与 T08/T09/T10 附件同值——本任务
  未触及任何被门禁扫描的命中源）；
  ③引擎自计数＝日志末尾 `[ird_gates] 依赖红线/补丁门禁存在 69 处命中` 的
  引擎自报数（base/head 均 69）。
  ②与③的组成关系（F-402/F-405/F-407）：76（去重条目）＝69（引擎自计的
  源码命中）＋7（引擎自测段 token——fail_r1/fail_t1/fail_r5/fail_sub/
  fail_r3/fail_r4/fail_t2 七行"按预期检出"自测输出，非源码命中；两侧对称
  出现，归一化后同串故参与去重计数但不参与比对差）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（7093f9de） | head（wp15-t11） | 增量 |
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
- 结论：本任务对 ird_gates 门禁面零增量（Render.hpp/.cpp 与 RenderTest.cpp
  的 include 面全部落在六条登记边＋本单元内——core（Evaluation/Identity）＋
  policy（PolicySet 头内值面 JointThresholds——无库符号消费，两模式可编译）
  ＋本单元（KinTypes/Evidence/SolutionSet/Commands）；SolutionSet.hpp 的
  表尾追加（filteredRecords() 纯虚访问器）不改变任何 include 边；CMake 增列
  未新增任何表外目标引用；无新增第三方依赖、无新诊断码）。退出码 1 为存量
  命中既有状态（DTB §4.5 存量例外未清零），非本任务引入。
- 附件清单：ird_gates_base.log / ird_gates_head.log（引擎原始输出）、
  base_hits.norm / head_hits.norm（归一化命中行，各 145 行）、
  base-head.diff（空）。
