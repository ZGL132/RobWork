# WP-13-T24 B4c-3a WIP检查点

**整体不通过。本批闭合旧五命令与历史保留部件互操作及运行时同步；正式宿主冲突重建、失败保护和完整47项三维仍开放。**

- [PASS] 十套件1495通过、0失败、0跳过：集成modeling444、modeling_contract17、ui328、ui_contract60、core65；独立modeling348、contract8；Windows GUI三套80+90+55。XML与ird报告逐套对账，core由原生XML派生；`checkpoint-summary.py`可重算。相对上批新增4项建模用例；core额外执行61项既有用例，不能称新增测试。
- [PASS] 新旧token统一按根引用投影活动闭包，未活动历史对象原字节保留为inverse素材。旧Restore按固定修订及局部覆盖重建完整候选，接受原合法槽序/无根槽，拒绝重复/错token/内嵌身份不符/不存在/allocateNew；拒绝零写计划。
- [PASS] 真实ProjectStore+HostModelCompilePort贯通：完整命令保留两份历史位姿/传动后，旧五Apply各自编辑、逐命令undo/redo完整值对照；旧五Restore重新挂接首份历史工具/场景/位姿/传动，逐条撤销重做并最终磁盘重开；固定旧修订原字节保持。
- [PASS] 数据断言通过后新增正式快照工具身份断言，真实复现旧named-poses Restore漏双编译，见runtime-restore-red；规划级相同红例见runtime-plan-red。修复按实际活动闭包差异合并编译要求，恢复工具等对象后正式快照同步；仅位姿内容/引用恢复仍跳过编译，避免一律重编译掩盖契约。
- [PASS] GUI在VS2022 x64、QT_QPA_PLATFORM=windows下按绝对路径逐个执行；runner全程检查跨树窗口进程重叠，最终无重叠记录。已查看完整草稿恢复工具页及工具物性底部截图。225项窗口测试不替代真实mesh/安装/导航/遮挡验收。
- [PASS] 集成全量Release、独立三目标构建、verify-task机器三条通过；已有编译警告完整保留。注释同步后的最终构建结果见full-build.log和standalone-build.log；封存后索引/格式/范围检查留在运行目录，随下一批只增保存。
- [NOT RUN] 本批正式modeling-tour未重复执行已知失败；上批FAIL原始证据见`../modeling-completion-20261020-b4c2-checkpoint/`。真正读取当前基线的冲突重建、旧tour空快照崩溃保护、新完整工作集正式窗口应用和全部47项三维仍待修复/验证。未声称这些已通过。
- [NOT RUN] 全新上下文独立验收。仅本地WIP，不推送、不合入，不使用子代理；F06/F07未修改/集成。

证据分类：legacy-red为历史多单例误拒绝；restore-validation-red为旧Restore非法槽误受理；runtime-restore-red/runtime-plan-red为正式运行时与规划漏双编译。named-pose-test-setup-failure（旧用户位姿槽误塞Home）、legacy-chain-test-setup-build-failure（误用不存在的信封命令ID）、undo-sequence-test-contract-mismatch（误把连续undo当未声明的多级游标栈）均为测试前置/契约错误，单独保留，不算产品红证。未修改project撤销产品代码。

run.ps1保留本机实际环境和绝对路径；其他检出复跑须适配路径。只统计最终完整套件，不重复累计专项。上批B4c2封存后的commit-lint/范围/索引日志放在previous-checkpoint-postchecks；冻结证据只增不改。
