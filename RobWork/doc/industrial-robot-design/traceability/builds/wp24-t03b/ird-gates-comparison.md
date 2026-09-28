# WP-24-T03（T03b 收口段）ird_gates 基线归一化比对登记

- 任务：WP-24-T03（分支 wp24-t03；base 3f767d9cea5f2ed202b579521c48f22a30ad8d0d）
- 日期：2026-09-28（续接实施段 attempt 2 亲验）
- 结论：**base..head 归一化命中集完全一致——零新增、零消除**（ird_gates exit=1 为存量红，
  同 RT-T14/WP-10-T14 先例口径：存量命中按 ARCH §3.2 SA-10 判读为构建失败，逐条
  登记于 DTB §4.5 登记册，非本任务引入；引擎退出码两端同为 1）。

## 一、运行现场（两端同法）

| 端 | 源树 | 构建树 | 命令 |
| --- | --- | --- | --- |
| head（90032ede＋续接测试增量，工作树即最终提交内容） | wt-wp24-t03（git worktree @ wp24-t03） | 同树集成模式（RWS_BUILD_INDUSTRIALROBOT=ON；BUILD_RWSimulatorPlugin/BUILD_sdurwsim_bullet/BUILD_sdurwsim_gui=OFF，见下"构建树说明"） | `cmake --build <tree>/build --config Release --target ird_gates` |
| base（3f767d9c） | wt-base-t03（git worktree @ 3f767d9c） | 同法新配集成树 | 同上 |

## 二、三套计数（F-398/F-402 纪律——分开列示，不混同）

| 计数口径 | base（3f767d9c） | head | 一致性 |
| --- | --- | --- | --- |
| ① 原始命中条目数（每个 `IRD-GATE-*` 标记切分为独立条目——MSBuild 并行输出同物理行多命中已拆分） | 144 | 144 | 一致 |
| ② `sort -u` 去重条目数（含回显与输出的重复消除；**本任务命中集内引擎自测 token 零出现**——`grep -ic "selftest"`＝0，无需剔除） | 121 | 121 | 一致 |
| ③ 引擎自计数（ird_gates 退出信息"存在 72 处命中"） | 72 | 72 | 一致 |

**三套计数的组成关系（F-402/F-405/F-407 教训注明）**：①原始 144 条＝引擎 72 个命中对象
×2——MSBuild 先回显自定义构建命令行、再输出执行结果，同一命中在日志出现两次
（WP-10-T14 登记册注同源事实）；②去重 121 条＝144 条按文本行去重后的唯一行数——
部分命中（同规则同目标、仅参数不同的族，如 ui 产品面 Qt 头扫描逐文件行）文本互异故
121＞72，而另一部分命中（同文本命中重复出现）在去重后合并；③引擎自计 72＝引擎内部
命中对象数（其计数器按"规则×目标×对象"聚合），为权威口径。三者均在 base..head 两端
逐值一致。

## 三、去重命中集 diff

```
diff ird-gates-hist-base.txt ird-gates-hist-head.txt  → 空（0 行差异）
```

两端去重集（各 121 条）按类型分布（一致）：IRD-GATE-SUB 76、IRD-GATE-R3 22、
IRD-GATE-LIB 12、IRD-GATE-R1 5、IRD-GATE-T1 4、IRD-GATE-R4 2
（R5/T2 出现于原始流但不进去重集唯一行——同文本已被计）。

与既有附件 `ird_gates-run-20260928.log`（前次中断会话在主检出集成树跑的 head 侧日志，
引擎自计数同为 72）亲验对读：本续接段在独立 worktree 重跑的 head 侧自计数与其一致
（72＝72），且其"归一化比对随验收段补做"的缺口由本附件补全（base 侧为本次亲跑）。

## 四、构建树说明（诚实边界）

本任务集成/门禁构建树为**独立冷启树**（主检出被编排者租约簿记占用 state.json、不可切
分支），以框架自带配置项排除了三个与 industrialrobot 零依赖的框架模拟插件目标
（BUILD_RWSimulatorPlugin / BUILD_sdurwsim_bullet / BUILD_sdurwsim_gui = OFF）：三目标
在冷启树编译因 NOMINMAX 定义缺失（主检出 RobWork/RobWork/cmake 未跟踪支撑文件时代
漂移）触发 min/max 宏冲突——属环境层存量问题，与本任务 diff 无关（RobWorkSim 子树
base..head 零差异、forbiddenFiles 未触碰）；已登记 findings.json（F-4xx 见该文件现况）。
排除三目标不影响 industrialrobot 全部目标与其依赖链（RobWork＋RobWorkStudio＋rwsim
核心库）的构建与门禁扫描面；ird_gates 的 LIB/SUB/R1/R3/R4 扫描对象为 industrialrobot
目标图，不受影响（命中集两端一致即为直接证据）。
