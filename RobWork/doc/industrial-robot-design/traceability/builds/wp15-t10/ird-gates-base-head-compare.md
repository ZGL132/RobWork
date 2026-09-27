# WP-15-T10 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-24
- 任务：WP-15-T10（实现显示单位投影与求解配置编解码——KIN-12/13/AT-27）
- 分支：wp15-t10；base＝997b1fa839c35eea0d3b6c364bf578aea1efcdf8
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T02~T08/T12~T16 确立口径），命中行归一化（捕获 `IRD-GATE-` 起的整行并
  剥离盘符绝对路径前缀——base 侧临时 worktree 与主检出版本树根不同）后逐行
  diff；存量例外未清零（DTB §4.5），本任务不做清零。base 侧临时 worktree
  （D:/10_Source_Repos/21_robot/rw15t10_base_wt，基 997b1fa8）用毕即删。
- 计数口径声明（F-398——三套计数分别列明，不混标）：
  ①直方图＝命中码 × 日志出现行数（归一化行 `grep -o "IRD-GATE-[A-Z0-9]*"`
  按码计数）；
  ②去重条目＝归一化命中行（`grep -o "IRD-GATE-XXX.*"` 全行捕获、剥离绝对
  路径后）排序去重（sort -u）的条目数（base_hits.norm/head_hits.norm 各
  145 行、去重各 76 条；与 T08/T09 附件同值——本任务未触及任何被门禁扫描
  的命中源）；
  ③引擎自计数＝日志末尾 `[ird_gates] 依赖红线/补丁门禁存在 69 处命中` 的
  引擎自报数（base/head 均 69；引擎自测段 token 不计入②——自测行非源码
  命中，两侧对称出现不参与比对差）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（997b1fa8） | head（wp15-t10） | 增量 |
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
- 结论：本任务对 ird_gates 门禁面零增量（AnalysisConfig.hpp/.cpp 与
  AnalysisConfigTest.cpp 的 include 面全部落在六条登记边＋本单元内——
  core（DiagData/Digest/Units）＋evidence（Snapshot/Slice）＋本单元；
  CMake 增列未新增任何表外目标引用；无新增第三方依赖、无新诊断码——
  KIN-CONFIG-ILLEGAL 为 §9.6 行 12 既有在册码的产码消费面，非新登记）。
  退出码 1 为存量命中既有状态（DTB §4.5 存量例外未清零），非本任务引入。
- 附件清单：ird_gates_base.log / ird_gates_head.log（引擎原始输出）、
  base_hits.norm / head_hits.norm（归一化命中行，各 145 行）、
  base-head.diff（空）。
