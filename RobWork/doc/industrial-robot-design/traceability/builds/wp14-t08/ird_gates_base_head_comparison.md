# WP-14-T08 · ird_gates base..head 归一化比对（引擎直跑口径）

> 口径：门禁判定以引擎直跑（`cmake -P ird_gates.cmake`）base..head 归一化比对为准
> （T12~T16/T02~T07 确立口径）。VS 目标 `ird_gates` 退出码 1 属存量命中既有状态，
> 判读看**增量**。
>
> - base＝17ff09969db3d4adf9a07b12491c61c22498a148（WP-14-T07 合入收尾）
> - head＝本任务分支 wp14-t08 工作树（实施段末次验证时点）
> - 引擎调用：`cmake -DIRD_ROOT=<industrialrobot 源码根> -P cmake/ird_gates.cmake`
>   （base 侧临时 worktree `D:/wt-wp14-t08-base` 用毕即删；head 侧为本任务
>   worktree `D:/wt-wp14-t08`）
> - 归一化：引擎原始输出逐行去前缀→按命中文本聚合计数（worktree 路径前缀
>   归一为 `<IRD_ROOT>`）→ base/head 两份直方图 diff

## 1. 直方图总览（按命中码计数；引擎对每条命中输出两行——正文＋[ird_gates] 汇总行，故计数均为偶数）

| 码 | base | head | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-SUB | 63 | 65 | +2（一条新增命中的双行输出） |
| IRD-GATE-R3  | 41 | 41 | 0 |
| IRD-GATE-LIB | 10 | 10 | 0 |
| IRD-GATE-T1  | 5  | 5  | 0 |
| IRD-GATE-R1  | 3  | 5  | +2（一条新增命中的双行输出） |
| IRD-GATE-R4  | 3  | 3  | 0 |
| IRD-GATE-T2  | 1  | 1  | 0 |
| IRD-GATE-R5  | 1  | 1  | 0 |

## 2. 语义增量（归一化 diff 全量——无其他差异）

```
> 2 表外依赖边 requirements->ui（sdurws_ird_requirements_plugin → sdurws_ird_ui）不在 ARCH §3.5 白名单
> 2 业务域目标互链：sdurws_ird_requirements_plugin → sdurws_ird_requirements（ARC-02；R-1 无例外）
```

恰两条新增命中，均为**插件目标 sdurws_ird_requirements_plugin 链接面的引擎
解析结果**（每条双行输出故计数 +2）：

1. `requirements->requirements`（插件 → 本单元计算库，自边——R-1 码面命中）；
2. `requirements->ui`（插件 → ui 平台公共面，表外 SUB）。

两处与 `units/requirements.md` §3.2 明文"插件目标 → 本单元计算库＋
sdurws_ird_ui"完全一致，属契约 sanctioned 形态——与 modeling 插件
WP-13-T15 落位时"恰增 modeling->modeling（自边）与 modeling->ui（表外 SUB）
两处既有模式命中"同型同源（modeling/CMakeLists.txt 插件链接块注释登记在案；
本单元 CMakeLists.txt 插件链接块同款登记）。白名单登记册回填归 WP-01-T03
治理面（本任务 allowedFiles 不含 cmake/ird_gates_whitelist.cmake 与 DTB），
裁决权在验收者。

除上述两条外，base..head 零新增命中；R-3（零 Qt）对插件目标的链接面不判
（plugin 目标形态不在 R-3 的"裸计算库/worker"扫描集合，文件域扫描域为
include/**＋src/**——plugin/ 目录不在其列，与 modeling 同款）。

## 3. 附件

- `ird_gates-base.log`／`ird_gates-head.log`——引擎原始输出；
- `ird_gates_base_hits_normalized.txt`／`ird_gates_head_hits_normalized.txt`
  ——归一化直方图（路径前缀已归一）。
