# WP-18/REUSE-T41 ird_gates 目标级装配边登记（落地批正本）

> 本文件为 REUSE-T41 候选登记册（candidate/gate-registrations.md）的落地正本：
> 2026-10-26 所有者批量批准复用落地裁决表第 1~4 项后，候选随批登记随产品树
> 一并落地（落地起始 SHA cf00a7dc9，治理前置批 1cb967892）。下文为候选态
> 登记原文（起始 SHA 41a59c670 时代），登记内容与机器验证口径不变；落地批
> 复验见同目录 REUSE-T41/README.md 落地章节与 landing-* 证据。

# WP-18/REUSE-T41 ird_gates 目标级装配边登记（ASM-PLUG 先例同款）

任务：REUSE-T41 F-967 ReferenceEventBus 生产装配核验换 execution 现有总线（契约 tasks/REUSE-T41.json；migration §13.3 WP-18 行批准方向）
分支：redesign-main（主工作树串行）；起始 SHA `41a59c67005ad0b0f45ccd32b3a6809fb4258154`
日期：2026-10-25
责任方：REUSE-T41 实施会话（登记与源面修改同批——ASM-PLUG 收口批先例："边是放行面，登记与源面修改同批，登记记录另立"）

## 登记内容

`cmake/ird_gates_whitelist.cmake` `IRD_TARGET_LEVEL_EDGES`（L10 段）新增一条：

```text
"sdurws_ird_ui_plugin->sdurws_ird_execution"
```

- 性质＝装配层特权边（目标级）：宿主装配目标 `sdurws_ird_ui_plugin`（MODULE）
  消费 execution 单元事件总线实现 `DomainEventBusImpl`（公共头
  `sdurws/ird/execution/EventBus.hpp`，R-2 公共面——4a 步跨单元公共头检查零命中）。
  架构依据＝core Events.hpp §4.9"总线实现归 execution·ui 并由 L5 装配注入"＋
  units/execution.md §10.4/§13 交接行（UI 线程 marshaling 由宿主 sink 承担）。
- **边界不动面**：ui 产品库 `sdurws_ird_ui` 链接块仍仅 core/diagnostics＋Qt
  三件套（O-31/C-3/4/5 红线零放宽；ui/CMakeLists.txt 配置期守卫与
  LinkageContractTest 文本钉不变）；单元级白名单 `IRD_ALLOWED_UNIT_EDGES`
  零新增（不建 ui→execution 单元边——同 ASM-PLUG 三域先例，目标级登记
  先于单元级判定，ird_gates 第 2b 步 asm_edge 分支承载）。
- 与本登记同批的源面修改＝ui/CMakeLists.txt ui_plugin 链接段增
  `sdurws_ird_execution` 一行（注释块逐边依据同步）。

## 机器验证（原件见 traceability/reuse/REUSE-T41/）

| 探针 | 命令 | 结果 |
| --- | --- | --- |
| 正例（边已登记） | `cmake -DIRD_ROOT=<industrialrobot> -P cmake/ird_gates.cmake` | exit 0，11 类零命中（`ird-gates-positive.log`） |
| 负例（撤登记） | 同上，临时移除该登记行 | exit 1，恰一条 `IRD-GATE-SUB: 表外依赖边 ui->execution（sdurws_ird_ui_plugin → sdurws_ird_execution）不在 ARCH §3.5 白名单`（`ird-gates-negative-unregistered.log`），随后恢复登记并复验 exit 0 |

负例证明登记行为放行的唯一依据：无登记即 SUB 命中、登记后零命中——放行面
未扩大到该边之外（正例日志与基线命中集一致，无其他新增行）。

## 待裁决边界（本批不实施）

- `execution/EventBus.cpp:66-67` 与 :144 同款的"core 参考总线同语义"失实
  注释：EventBus.cpp 被 execution.audit-b32.EventBus.* 11 行 pending 审计行
  全文件覆盖，任何改动触发全文件解决要求——超出本包边界（T33/T36/T40 先
  例），待所有者批准 execution 单元迁移批后同批更正。
- core `ReferenceSubscription` RAII 恢复＋`EventsTest.cpp` FIFO 用例句柄持有
  契约纠正、`BuildRedLineTest` 增 ReferenceEventBus 禁入产品目标扫描：未获批
  不实施（tasks/REUSE-T41.json note 登记待裁决与受影响消费者）。
