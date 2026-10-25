# REUSE-T41：F-967 ReferenceEventBus 生产装配核验换 execution 现有总线（blocked——候选留存）

实施日期：2026-10-25；分支 redesign-main（主工作树串行）；起始 SHA `41a59c67005ad0b0f45ccd32b3a6809fb4258154`。
任务契约：[REUSE-T41](../../../tasks/REUSE-T41.json)（WP-18 残包——F-964 已由 [REUSE-T23](../REUSE-T23/README.md) 实施待独验，本包零触碰 RunRegistry 及其测试）。
**状态＝blocked（所有者 2026-10-25 escalate 裁决：采 T33/T36/T40 先例收口，产品树 HEAD 零改动，候选留存；F-967 保持 open，实施者不自签、不假关）。**

## 问题（B32 审计在案的 F-967）

[core-event-lifetime-audit.md](../../../reuse/core-event-lifetime-audit.md)：生产装配链越界——`UiPlugin.cpp:749` 在 `IrdWorkbenchHostPlugin::initialize` 创建 **core::ReferenceEventBus**，而 Events.hpp 明文该类"**非生产组件：生产总线归 execution·ui，本类不进入任何产品链接面**"；audit 实证三缺口：①ReferenceSubscription 无析构退订（RAII 缺口）；②同步回调在发布线程（store 提交持写锁线程）；③UI 在 QTimer 入队前读取非原子 `m_lastStoreAdapter`（"不能因已用 QTimer 就认定跨线程安全"）。后续路径审计明示："先复用现有总线契约与唯一实现，核 UI 线程转接、sink/句柄/总线销毁序、退订与在投回调交错及项目切换；禁止再造第三总线或让 core 变为生产实现 owner"。上游 rw::core::Event 不采用（CORE-EVENT 维持 candidate——同步 listener 表，无 RAII/线程/队列/drain）。

## 候选内容（已实现并全量验证，未落产品——`candidate/*.diff` 六件）

1. **生产装配换核**（UiPlugin.hpp/cpp diff）：`m_eventBus` 类型 `shared_ptr<core::ReferenceEventBus>` → `shared_ptr<core::IDomainEventBus>`（头文件保持接口类型——适配器裸指针/订阅/ShellWiring 注入四处消费面零改动）；生产实例＝`execution::DomainEventBusImpl`（§10.4：publish 线程安全即返——store 提交持写锁期间发布零阻塞、专用投递线程串行分发、RAII 句柄、drain on close）。头文件零 execution 依赖；成员序保证句柄→sink→总线 drain 销毁纪律；快照互斥声明于总线之前（晚于总线析构）。换核后 ReferenceEventBus 产品使用点归零（仅剩 core 定义与测试——回归"非生产组件"本位）。
2. **投递线程转接修正**（audit 明示"不能直接换类型洗平"——本候选实质工作量）：两 sink 的适配器快照读取移入互斥访问器 `snapshotEventStoreAdapter()`（与打开拍写侧同锁 `m_eventAdapterMutex`；返回值拷贝随队列转移所有权），投递线程零裸读宿主非原子状态；队列转投改 `QMetaObject::invokeMethod(receiver, …, Qt::QueuedConnection)`（投递线程→UI 线程显式排队；宿主销毁时未投递排队体自动丢弃）；迟到守卫（`adapter==source/isClosed/projectId`）与 PM-04/REQ-10"事务发布持写锁不得同步回查 HEAD"语义逐字保持。
3. **关停/析构 drain 序**：先停发布源（content shutdown/落盘执行器 stop）→两订阅句柄 reset（RAII 退订，幂等）→`waitForIdle()` 确定性排空（execution 退订不等在投帧，waitForIdle 是唯一确定性静止点）→成员逆序销毁（句柄→sink→总线 drain join 时订阅表已空零回调）；总线保留到成员析构（store 适配器持非 owning 裸指针，提前 reset 会留悬垂发布指针）；`waitForIdle` 装配者向下转型（唯一写入点＝make_shared）。
4. **目标级装配边登记与源面同批**（ASM-PLUG 先例）：ui_plugin 链接段增 `sdurws_ird_execution`＋逐边注释；whitelist L10 `IRD_TARGET_LEVEL_EDGES` 增 `sdurws_ird_ui_plugin->sdurws_ird_execution`；候选登记册 [candidate/gate-registrations.md](candidate/gate-registrations.md)。ui 产品库零 execution 红线（O-31/C-3/4/5）不动；单元级边零新增。
5. **EventBus.hpp:144 注释更正**（零行为变化）："core ReferenceEventBus 同语义"不实——core `removeSink` 删该 sink 全部订阅位，execution 只删一个订阅位；改如实分述。
6. **装配清单锚点同步**：candidate 插入使 [production-assembly-manifest.json](../../production-assembly-manifest.json) wiredAnchor 行号漂移（AssemblyManifest 契约测试行级核对按设计红），候选同批更正 2781→2855、2855→2929（一致 +74）。

## blocked 机制（结构性门禁冲突——所有者裁决登记）

候选落地必须改 `UiPlugin.cpp/UiPlugin.hpp`；validate-reuse 对**变动产品文件**做无 AuditOnly 的全文件权限扫描（reuse-contract.ps1:271-273），实测命中：

```
REUSE-AD : ui/plugin/UiPlugin.cpp : restricted upstream use needs exact file/target/operation grant (IRD-GATE-AD)
```

扫描判定（复刻 CheckSourcePermission 正则实核）：UiPlugin.cpp categories=**model,state,graphics,host**＋operations=write-state（1 处 setQ——D8 Jog 桥）；UiPlugin.hpp categories=**host**（rws/RobWorkStudioPlugin.hpp）。grant 体系三层约束叠加（reuse-contract.ps1:76/:83 与 validate-reuse.ps1:172-179）：host/graphics 类别必须 origin=host-presentation → exceptionId 须命中 hostExceptions 封闭清单文件面 → 该清单机器冻结（恰 HOST-RENDER/PRESENT/COMPILE/GATEWAY 四项、恰 4 ID、文件精确比对、禁止新增或扩文件）且**不含 UiPlugin.{hpp,cpp}** → 结构性无任何合法 grant 形态。机器盲区如实登记：UiPlugin.cpp 基线前即含 rws//rw/graphics include，仅因本次改动才首次进入扫描集（"基线后首改才扫"）。

**所有者 2026-10-25 escalate 裁决原文要点**：(B) 采 T33/T36/T40 先例收口，产品树 HEAD 零改动，候选留存 blocked；不选 A（扩宿主例外）的理由＝机器冻结治理配置＋ARCH §3.6/SA-19 保留审查事项、WP-19 行明文不扩宿主例外，属所有者独立治理批次（改 validate-reuse.ps1 冻结清单＋permissions.json 登记＋ARCH 审查记录）。**解阻路径**＝该治理批次批准后（如 HOST-WORKBENCH 条目纳 UiPlugin.hpp/cpp，permissionRef ARCH:3.6.2）按 candidate/*.diff 原样低成本落地。

## 验证（候选态真实执行；gtest XML＋ird-test-report 原件在本目录；分列见 verification.json）

| 检查 | passed / failed / skipped / NOT RUN | 原件 |
| --- | --- | --- |
| sdurws_ird_core_test（二进制未重建——core 零输入变化） | 74 / 0 / 0 / 0 | core_test.xml＋report＋log |
| sdurws_ird_execution_test（被采纳实现契约＝oracle 承载） | 157 / 0 / 0 / 0 | execution_test.xml＋report＋log |
| sdurws_ird_execution_contract_test（FIFO/RAII/死锁/waitForIdle——线程交错确定性同步） | 38 / 0 / 0 / 0 | execution_contract_test.xml＋report＋log |
| sdurws_ird_ui_test | 331 / 14 / 0 / 0 | ui_test.xml＋report＋log |
| sdurws_ird_ui_contract_test（首轮 62/1 清单锚点红→候选同批修正→复跑绿） | 63 / 0 / 0 / 0 | ui_contract_test.xml＋report＋log |
| ird_gates 正例（11 类，新边已登记） | exit 0 零命中 | ird-gates-positive.log |
| ird_gates 负例（撤登记探针） | exit 1 恰一条 SUB 命中→恢复复验 exit 0 | ird-gates-negative-unregistered.log |
| GUI 真实生产路径（delivered after commit） | **NOT RUN**——HEAD 既有启动段错误 | gui-smoke-launch.log＋gui-smoke-head-baseline-segfault.log |

- **红/绿口径**：F-967 原型红证引用不重跑＝[B32 审计](../../../reuse/core-event-lifetime-audit.md)批（句柄出作用域仍投递、subscriberCount 仍 1——原件 [REUSE-T01/batch32](../REUSE-T01/batch32/)）；候选绿证＝被采纳实现的既有契约用例全绿＋生产装配点换核。本候选无独立可单测的宿主装配缝（两 sink 为 initialize 内局部类），未新增合成测试。
- **ui_test 14 失败与候选无因果**（登记 [F-973](../../../traceability/findings.json)）：ui_test 编入 TU 源集＝test/*.cpp＋plugin/{HostCompilePort,HostPresentationAdapters,QtTextBillboard,KinEvaluationChannel,KinHostServicesAssembly}.cpp——候选五产品文件零编入（二进制等同 HEAD）。#1＝F-560 预存（[REUSE-T17 README](../REUSE-T17/README.md) 在案）；#2~#14＝13 项真实数值链（runtime 物化＋真实 FK/IK）**redesign-main 未记录失败**——候选源疑似 T33 RPY 换核跨单元漂移（待证），现 HEAD 即可复现，归所有者归类。
- **GUI 冒烟归因**（登记 [F-974](../../../traceability/findings.json)）：候选工作树运行在"共享面装配完成"后段错误（exit 139）；**stash 候选五文件重建 HEAD 基线复跑同点同形段错误**——归因＝HEAD 41a59c670 既有缺陷。dev-diagnostics.log 历史：10-21 前后运行曾全程达 draft applied/宿主已关闭工作单元，10-24 起两次运行同早停。修订事件双 sink"delivered after commit"真实 GUI 路径验证随本阻断 NOT RUN；替代证据＝execution 总线契约用例＋ui_contract_test 63 项（PortAdapters 同源编入——StoreFactoryPortAdapter 总线转交面回归覆盖）。

## 治理数据面零残留声明

候选驱动的矩阵/权限/manifest 改动已随产品树同批回退（functions/ui.json、execution.json、core.json、foundation-governance.json、permissions.json、changes/ manifest——HEAD 治理数据面零变化）；矩阵同步器原件 [candidate 同目录 sync-matrix.mjs](sync-matrix.mjs) 留存（productionWiring 迁移态/UiPlugin 与 ui-CMake/EventBus.hpp 整文件覆盖行/ird_add_gtest T32 定案同款翻转/L10 区段/101 处摘要重钉——落地批可直接复用）。`ird_add_gtest` 定案（business-specific 薄宏）独立成立于 migration §13.3 WP-11 T32 在案记录，本包未抢跑翻转。落地批随批工作清单见契约 note。

## 待所有者裁决残留（未获批不实施）

1. **core ReferenceSubscription RAII 恢复＋EventsTest.cpp FIFO 用例句柄持有契约纠正**（审计预告；core.json 全文件解决要求 41 处 Events.cpp 绑定超包边界——T33/T36/T40 先例）。
2. **execution/EventBus.cpp:66-67 同款失实注释更正**（11 行 pending 审计行全文件解决超包边界）。
3. **BuildRedLineTest 增 ReferenceEventBus 禁入产品目标扫描**（新门禁须独立验收批准）。
4. **F-973（ui_test 13 项真实数值链失败）与 F-974（HEAD studio 启动段错误）**：跨任务缺陷归类与修复归所有者。

## 移交

blocked 收口：候选留存待宿主例外治理批次落地；findings F-967 保持 open（现 HEAD 生产装配仍是越界 core::ReferenceEventBus＋同步回调读非原子状态）；F-964 独验仍归所有者另开会话。独验/落地批核对点：候选 diff 与本 README/verification 计数、门禁正负例原件、blocked 机制三方约束引用、解阻治理批次的 ARCH 审查记录。
