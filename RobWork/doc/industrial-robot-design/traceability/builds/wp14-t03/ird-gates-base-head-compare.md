# WP-14-T03 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-25
- 任务：WP-14-T03（requirements 领域对象与 canonical 编解码）
- 分支：wp14-t03；base＝6394918b87eb1acfa9939cd7a809c5a7cb12aa95
- 口径：引擎直跑（cmake -P ird_gates.cmake，T12～T16/T02 确立口径），命中行
  归一化（剥离盘符绝对路径 `D:/10_Source_Repos/21_robot/RobWork/` 与
  `D:/10_Source_Repos/21_robot/rw_base_wt03/` 前缀）后逐行 diff；存量例外未
  清零（DTB §4.5），本任务不做清零。
- 侧据：base 侧临时 worktree（../rw_base_wt03，引擎直跑后已删除——用毕即删
  惯例）；head 侧＝主检出工作区（wp14-t03 分支）。

## 直方图（命中码 × 行数）

| 码 | base（6394918b） | head（wp14-t03 工作区） | 增量 |
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
面：requirements 五条登记边不变；rw::math 头依赖按 runtime RT-T03 冒烟
机制仅注入 include 路径，不进入链接清单——ird_gates 的 LIB 门禁按链接清单
核对，header-only 头可达不构成库链接边）。

## 结论

WP-14-T03 对 ird_gates 门禁口径的增量为 **0**（存量 127 行命中＝T02 期已
登记的既有状态，DTB §4.5 存量例外未清零，裁决权在验收者）。
