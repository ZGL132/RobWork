# WP-14-T09 · ird_gates base..head 归一化比对（引擎直跑口径）

> 口径：门禁判定以引擎直跑（`cmake -P ird_gates.cmake`）base..head 归一化比对为准
> （T12~T16/T02~T08 确立口径）。VS 目标 `ird_gates` 退出码 1 属存量命中既有状态，
> 判读看**增量**。

- base＝a5b3b397f96109adb2fc377aefb0a9c1e5349fae（WP-14-T08 合入收尾＝redesign-main HEAD）
- head＝本任务分支 wp14-t09 工作树（实施段末次验证时点）
- 引擎调用：`cmake -DIRD_ROOT=<industrialrobot 源码根> -P cmake/ird_gates.cmake`
  （base 侧临时 worktree `D:/wt-wp14-t09-base` 用毕即删；head 侧为本任务 worktree
  `D:/10_Source_Repos/21_robot/wt-wp14-t09`）
- 归一化：引擎原始输出逐行将 worktree 路径前缀归一为 `<IRD_ROOT>` → 按命中码聚合计数
  → base/head 两份直方图 diff

## 1. 直方图总览（按命中码计数）

| 码 | base | head | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 5 | 5 | 0 |
| IRD-GATE-R1 | 3 | 3 | 0 |
| IRD-GATE-R3 | 20 | 20 | 0 |
| IRD-GATE-R4 | 1 | 1 | 0 |
| IRD-GATE-SUB | 33 | 33 | 0 |
| IRD-GATE-T1 | 2 | 2 | 0 |

## 2. 结论

- 归一化 diff：（归一化后 base/head 引擎输出逐行全等）
- 本任务无任何新增门禁命中（WP-14-T09 只增测试目标源文件与 testdata 资产，
  不改产品目标链接面/include 面——既有存量命中与 base 完全一致）。
