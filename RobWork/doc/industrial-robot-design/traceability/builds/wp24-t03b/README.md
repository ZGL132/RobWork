# traceability/builds/wp24-t03b/ — WP-24-T03（T03b 收口段）验证留痕

任务：WP-24-T03（应用壳装配收口 T03b：域命令入册、汇聚投影、中央区挂位、策略与
名称适配、会话全同步、About 接线）。分支 `wp24-t03`；base
`3f767d9cea5f2ed202b579521c48f22a30ad8d0d`。

留痕主体＝**续接实施段（attempt 2，2026-09-28）全部亲跑**：前次中断会话的两笔提交
（4dbca0d8 实施＋90032ede 验证修复/文档同步）由本段逐项亲验（构建、全部测试、门禁
两端比对均亲手重做，不引用前次留痕）；前次留痕 `ird_gates-run-20260928.log` 保留在册
（其"归一化比对随验收段补做"缺口已由 `ird-gates-comparison.md` 补全）。

## 一、构建

| 文件 | 内容 |
| --- | --- |
| configure-integrated.log | 集成模式配置（worktree 独立冷启树；RWS_BUILD_INDUSTRIALROBOT=ON；含三次重配历史——最终口径见 comparison §四） |
| build-integrated.log | 集成模式全量 Release 构建日志（**零 error**；BUILD_EXIT=0） |
| build-test-targets.log | 受影响测试目标增量构建（sdurws_ird_modeling_test/sdurws_ird_ui_gui_test——新增测试文件落地过程） |

构建树说明（诚实边界）：主检出被编排者租约簿记（state.json 未提交改动）占用、不可切
分支，故全部工作在独立 worktree（git worktree @ wp24-t03）完成；集成树以框架自带配置
项排除三个与 industrialrobot 零依赖的框架模拟插件目标（BUILD_RWSimulatorPlugin/
BUILD_sdurwsim_bullet/BUILD_sdurwsim_gui=OFF）——三目标冷启编译失败属环境层存量漂移，
与本任务 diff 无关，已登记 findings.json F-420（比对附件 §四有完整说明）。

## 二、测试（gtest——全部亲跑，退出码全 0）

| 文件 | 目标 | 结果 |
| --- | --- | --- |
| run-modeling-test.log ＋ sdurws_ird_modeling_test.xml | sdurws_ird_modeling_test | **270/270 PASSED**（含 T03B 族 5 例：原 3 例迁移＋新增 2 例） |
| run-ui-test.log ＋ sdurws_ird_ui_test.xml | sdurws_ird_ui_test | **158/158 PASSED** |
| run-ui-gui-test.log ＋ sdurws_ird_ui_gui_test.xml | sdurws_ird_ui_gui_test | **40/40 PASSED**（含 T03B 族 4 例：原 2＋新增 2；§12.2 规程 QT_QPA_PLATFORM=windows 亲跑） |
| run-ui-contract-test.log ＋ sdurws_ird_ui_contract_test.xml | sdurws_ird_ui_contract_test | **22/22 PASSED** |
| run-modeling-contract-test.log ＋ sdurws_ird_modeling_contract_test.xml | sdurws_ird_modeling_contract_test | **17/17 PASSED** |
| run-smoke-*.log（见 §四） | 冒烟树测试 | 见 §四 |

## 三、门禁（ird_gates——base..head 两端亲跑）

| 文件 | 内容 |
| --- | --- |
| ird-gates-comparison.md | **归一化比对登记（三套计数分开列示＋组成关系说明）**——结论：命中集完全一致，零新增零消除；引擎退出码两端同为 1（存量红，F-019 先例口径） |
| ird-gates-head.log / ird-gates-hist-head.txt | head 侧引擎直跑日志／去重命中集 |
| ird-gates-base.log / ird-gates-hist-base.txt | base（3f767d9c）侧引擎直跑日志／去重命中集（git worktree @ 3f767d9c 独立亲跑） |
| ird_gates-run-20260928.log | 前次中断会话的 head 侧留痕（在册保留；自计数 72 与本次亲验一致） |

## 四、独立冒烟模式

| 文件 | 内容 |
| --- | --- |
| configure-smoke.log | 冒烟配置（独立目录 build_smoke_wp24t03；vcpkg toolchain＋CMAKE_PREFIX_PATH=<Qt>/msvc2022_64——AGENTS §4.1/CORE-T01 G-2 口径） |
| build-smoke.log | 冒烟全量构建日志（**零 error**；BUILD_EXIT=0；含 modeling_test 链接修复实证——T03B 族迁入 gated 文件后冒烟链接恢复，`sdurws_ird_modeling_test.exe` 冒烟编列成功产出） |
| run-smoke-modeling-test.log ＋ smoke sdurws_ird_modeling_test.xml | 冒烟 modeling_test：**215/215 PASSED**（270−215＝集成模式专属 gated TU 用例：ReadinessTest/CommandHandlersTest/DhConvertEquivalenceTest/Golden* 等既有 gating＋本任务 PluginModuleT03BTest gated 增列——冒烟编列排除，与既定先例同口径） |
| run-smoke-ui-test.log | 冒烟 ui_test：**158/158 PASSED** |
| run-smoke-ui-contract-test.log | 冒烟 ui_contract_test：**22/22 PASSED** |
| run-smoke-modeling-contract-test.log | 冒烟 modeling_contract_test：**8/8 PASSED**（17−8＝集成 gated 差，既有口径） |

## 五、治理自查

| 文件 | 结果 |
| --- | --- |
| validate-task.log | validate-task: **PASS（1 tasks）**——分支树亲跑（`-TaskFile tasks/foundation/WP-24-T03.json`） |
| validate-docs.log | validate-docs: **PASS**（20 units, 12 trace entries, 202 task files）——在主检出 redesign-main@fecdf104 亲跑 |

**validate-docs 分支树口径的如实说明**：在分支 wp24-t03 树上直跑 validate-docs 失败——根因＝
base 3f767d9c 自身引入的 `tasks/foundation/WP-24-T08.json` note JSON 转义缺陷（未转义双引号
截断 note，ConvertFrom-Json 于 note line 63 position 903 解析失败），redesign-main 侧已由
fecdf104（"[governance] 修复 WP-24-T08 契约 note JSON 转义缺陷…"）修复；本分支先于该修复
分叉故继承缺陷。WP-24-T08.json 属他任务契约、不在本任务 allowedFiles——不私改；分支合并
redesign-main 时该缺陷即消解（主检出的 PASS 即合并后状态的实证）。非本任务 diff 因果。

## 六、如实声明（未执行项）

- **GUI 冒烟（--rwsplugin 直载：域命令经注册表提交实证＋关于框真实清单）未执行**——
  编排者现场约束明令"不要启动 RobWorkStudio.exe"（环境补丁 §2）；交互式 GUI 冒烟与
  截图按 UI-T17 先例归所有者人工通道（ui.md v1.13-r4 同款登记）或验收段补做。域命令
  经注册表提交与关于框清单真实性的机制级自证由 gtest 承载
  （WorkbenchContentGuiTest.DomainCommand* 四例＋AboutDialog 模型/GUI 既有族）。
- ird_gates exit=1 为存量红（F-019 先例口径），与本任务 diff 无关——base..head 三套
  计数一致（144/144、121/121、72/72）、去重集 diff 为空。
- validate-docs 分支树失败＝base 继承缺陷（见 §五说明），非本任务因果。
