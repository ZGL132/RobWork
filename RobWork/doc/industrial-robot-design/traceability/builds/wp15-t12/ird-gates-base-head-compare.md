# WP-15-T12 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-27
- 任务：WP-15-T12（实现 kinematics 插件界面——四面板与域命令注册）
- 分支：wp15-t12；base＝93c9f5a4a55c3f7d18a36557cd775bc0e50b5b1c
- 现场：实施全程在独立 worktree（D:/10_Source_Repos/21_robot/rw-wt-wp15-t12，
  检出 wp15-t12）完成——主检出（redesign-main）当时被其他会话活动占用工
  作区，按编排者环境补丁第 1 条处置，不争抢；base 侧临时 worktree
  （D:/10_Source_Repos/21_robot/rw15t12_base_wt，基 93c9f5a4）用毕即删。
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P
  cmake/ird_gates.cmake`，T02~T11 确立口径），命中行归一化（捕获
  `IRD-GATE-` 起的整行；base 侧临时 worktree 与实施 worktree 的版本树根
  不同，故将两处树根前缀 `D:/10_Source_Repos/21_robot/rw15t12_base_wt/...`
  与 `D:/10_Source_Repos/21_robot/rw-wt-wp15-t12/...` 统一归一为 `<IR>`；
  另剥引擎双行输出的 `[ird_gates] ` 前缀——每条命中在日志中成对出现，
  前缀行与裸行同 detail，去重计数前归并）后逐行比对；存量例外未清零
  （DTB §4.5），本任务不做清零。
- 计数口径声明（F-398——三套计数分别列明，不混标）：
  ①直方图＝命中码 × 日志出现行数（含引擎双行输出；`grep -o
  "IRD-GATE-[A-Z0-9]*"` 按码计数）；
  ②去重条目＝归一化命中行剥 `[ird_gates] ` 前缀后排序去重（sort -u）的
  条目数（base_hits.dedup 76 条 / head_hits.dedup 79 条）；
  ③引擎自计数＝日志末尾 `[ird_gates] 依赖红线/补丁门禁存在 N 处命中` 的
  引擎自报数（base 69 / head 72）。
  ②与③的组成关系（F-402/F-405/F-407）：base 76（去重条目）＝69（引擎自
  计的源码命中）＋7（引擎自测段 token——fail_r1/fail_t1/fail_r5/fail_sub/
  fail_r3/fail_r4/fail_t2 七行"按预期检出"自测输出，非源码命中）；head
  79＝72（引擎自计）＋7（同一组自测 token，两侧对称出现）。①与②的关系：
  151（head 直方图行数）＝2×72（引擎对每命中打印带前缀与裸两行）＋7（自
  测 token 单行）；145＝2×69＋7 同式。

## 直方图（命中码 × 日志出现行数）

| 码 | base（93c9f5a4） | head（wp15-t12） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 12 | 12 | 0 |
| IRD-GATE-R1 | 7 | 11 | +4 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 75 | 77 | +2 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 145 | 151 | +6 |

## 归一化比对结论

head 相对 base 恰增 **3 处逻辑命中**（直方图 +6 行＝每命中双行输出；去重
+3 条；引擎自计 +3——三套计数自洽），全部由本任务 acceptance 明文规定的
插件链接面（卡 §3.2："插件目标 `sdurws_ird_kinematics_plugin` → 本计算库
＋`sdurws_ird_ui`"；DTB §5.1 v0.20："单元自持开发验证 harness
`sdurws_ird_<unit>_app`"）经引擎文本解析产生：

1. `IRD-GATE-R1: 业务域目标互链：sdurws_ird_kinematics_plugin →
   sdurws_ird_kinematics`——插件目标链接本单元计算库的**自边**。引擎的
   R-1 判定按"两端均属 IRD_BUSINESS_UNITS"粗粒度归类，同单元自边与业务
   域互链共用同一码面；R-1 红线语义（业务域单元**之间**互链禁止）不含
   同单元自边——本边即卡 §3.2 二分结构"插件消费计算库"的既有 sanctioned
   面。**WP-13-T15 先例同款**（modeling_plugin→sdurws_ird_modeling，比对
   附件 traceability/builds/wp13-t15/ird-gates-base-head-compare.md 第 1
   条）。
2. `IRD-GATE-SUB: 表外依赖边 kinematics->ui（sdurws_ird_kinematics_plugin
   → sdurws_ird_ui）`——插件目标链接 ui 公共面（acceptance 3/4 明文链接
   面：IPluginUiModule/IPluginUiRegistrar/CommandDescriptor/FormEditCommon
   等装配与表单公共件消费）。表外边登记册（DTB §4.5）回填归 WP-01-T03
   治理面——本任务 allowedFiles 不含 `cmake/ird_gates_whitelist.cmake`
   与 `development-task-breakdown.md`。**WP-13-T15 先例同款**（modeling->ui
   同族表外边，同附件第 2 条；登记模式＝WP-10-T15/T16 的 ui->project/
   ui->ui 已登记行"插件目标链接本单元产品库自边"的同类扩展）。
3. `IRD-GATE-R1: 业务域目标互链：sdurws_ird_kinematics_app →
   sdurws_ird_kinematics_plugin`——开发验证 harness 链接插件目标的同单元
   自边链（DTB §5.1 v0.20"链接面＝仅 sdurws_ird_<unit>_plugin"的登记
   形态；modeling_app→modeling_plugin 同款存量先例）。两处 R-1 同理为
   同单元自边的码面粗粒度归类，非业务域互链。

**除此之外零新增命中**（R-2/R-3/R-4/R-5/T-1/T-2/LIB 全码面零增量；R-3
对 plugin/_app 分类面豁免——Qt 链接合法，与 modeling/ui 插件目标同口径）。
存量命中零消除（base 76 条全部仍在 head）。

## 治理面待办（非本任务辖域，沿 WP-13-T15 同口径登记）

DTB §4.5 增行＋whitelist 增 "kinematics->kinematics"/"kinematics->ui" 机器
面（若按 ui 先例仅登记 §4.5 手册面，则上述 3 处为登记存量命中）——归
WP-01-T03 治理面回填。

## 数据文件

- base_hits.norm / head_hits.norm：引擎原始命中行（树根归一 `<IR>`，未剥
  双行前缀——直方图①的计数源）
- base_hits.dedup / head_hits.dedup：剥 `[ird_gates] ` 前缀后 sort -u
  （去重条目②的计数源）
- head_engine_selfcount.txt：head 侧引擎自计数行（③）
