# ird_gates 引擎直跑归一化比对（WP-14-T06）

- base：b1bdc8792be52a448a0cb7b6de4252b2a75f52a5（临时 worktree，比对后即删）
- head：wp14-t06（分支实施面，本目录留痕时点工作区）
- 引擎：cmake -DIRD_ROOT=<industrialrobot 源码根> -P cmake/ird_gates.cmake（两侧同一引擎文件）；
- 归一化：引擎原始输出中绝对路径（head 侧工业根／base 侧 worktree 工业根）统一替换为 <IRD> 占位后，
  取 IRD-GATE 行排序比对；
- 口径：DTB §4.5/编排者补丁第 3 条——门禁判定以引擎直跑 base..head 归一化比对为准，
  VS 目标 ird_gates 退出码 1 属存量例外既有状态。

## 命中码直方图（base 侧与 head 侧完全一致）

```
base(head)  码
     10 IRD-GATE-LIB
      3 IRD-GATE-R1
     41 IRD-GATE-R3
      3 IRD-GATE-R4
      1 IRD-GATE-R5
     63 IRD-GATE-SUB
      5 IRD-GATE-T1
      1 IRD-GATE-T2
```

## 结论

- 归一化命中集逐行 diff 为空（IDENTICAL_HIT_SETS，各 127 行 IRD-GATE 记录）；
- 本任务零新增门禁命中；两侧 exit=1 均为 DTB §4.5 登记的存量例外既有状态（白名单未登记边等），非本任务引入；
- ird_gates_whitelist.cmake 属治理面待办，本任务未触碰（allowedFiles 外）。
