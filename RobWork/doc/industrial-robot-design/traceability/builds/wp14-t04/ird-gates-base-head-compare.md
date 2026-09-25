# WP-14-T04 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-26
- 任务：WP-14-T04（requirements CSV/JSON 需求导入与副本导出）
- 分支：wp14-t04；base＝7ac106cd69a71e7b08d3d4ef92c2e885bdd41166
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake`，
  T12～T16/T02/T03 确立口径），命中行归一化（剥离盘符绝对路径
  `D:/10_Source_Repos/21_robot/RobWork/` 与
  `D:/10_Source_Repos/21_robot/rw_base_wt04/` 前缀）后逐行 diff；存量例外
  未清零（DTB §4.5），本任务不做清零。
- 侧据：base 侧临时 worktree（../rw_base_wt04，引擎直跑后已删除——用毕即
  删惯例）；head 侧＝主检出工作区（wp14-t04 分支）。

## 直方图（命中码 × 行数）

| 码 | base（7ac106cd） | head（wp14-t04 工作区） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 10 | 10 | 0 |
| IRD-GATE-R1 | 3 | 3 | 0 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 63 | 63 | 0 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 127 | 127 | **0** |

## 归一化逐行 diff

`base_hits.norm`（127 行）与 `head_hits.norm`（127 行）`sort` 后 `diff` 输出
为空——**命中集逐行完全一致，零新增命中**（本任务未改任何产品目标的链接
面：requirements 五条登记边不变；CMake 仅增列 src/Import.cpp 与
test/ImportTest.cpp 两个既有目标内的源文件——目标集合、目标名、链接清单
全部不变）。

## 结论

WP-14-T04 对 ird_gates 门禁口径的增量为 **0**（存量 127 行命中＝T02 期已
登记的既有状态，DTB §4.5 存量例外未清零，裁决权在验收者）。
