# ird_gates 引擎直跑归一化比对（WP-14-T07）

- base：1b3a73712cd8305a9476c855297ea240c06412a3（临时 worktree `rw-wp14-t07-base`，比对后即删）；
- head：wp14-t07（任务分支——独立 worktree `rw-wp14-t07` 实测面，含本任务全部源码改动）；
- 引擎：`cmake -DIRD_ROOT=<industrialrobot 源码根> -P cmake/ird_gates.cmake`（head 侧引擎文件——base..head 间该引擎文件零改动，两侧同源）；
- 归一化：引擎原始输出中绝对路径统一替换为 `<IRD>` 占位后，仅取 IRD-GATE 行排序比对；
- 口径：DTB §4.5/编排者补丁第 3 条——门禁判定以引擎直跑 base..head 归一化比对为准，VS 目标 ird_gates 退出码 1 属存量命中既有状态（白名单未登记边等）。

## 命中码直方图（base 侧与 head 侧完全一致）

```
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
- 本任务零新增门禁命中（requirements 单元新增 TemplateArray.hpp/.cpp、
  Editor/DiagCodes 增列均落在既有五条登记边内：core/diagnostics/project/
  io/evidence——零新链接边、零 Qt、零表外 include）；
- 两侧 exit=1 均为 DTB §4.5 登记的存量例外既有状态，非本任务引入；
- ird_gates_whitelist.cmake 属治理面待办，本任务未触碰（allowedFiles 外）。

## 附件

- `ird_gates_head_engine_raw.txt` / `ird_gates_base_engine_raw.txt`（引擎原始输出）；
- `ird_gates_head_hits_normalized.txt` / `ird_gates_base_hits_normalized.txt`（归一化命中清单）。
