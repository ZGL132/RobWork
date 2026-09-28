# UI-T20 ird_gates 命中集增量登记（契约 acceptance 6）

任务：UI-T20 宿主运行时发布桥（方案 B.1 D10 消费侧；契约 tasks/foundation/UI-T20.json）
分支：ui-t20（base＝redesign-main@564b80748e1eaa5354ad22cf02cf85abf04d2ad7）
日期：2026-09-28
责任方：WP-01-T03（机器面回填登记册——本文件为实施侧登记提交件，沿 wp10-t02/t13/t14/t15/t16 先例口径）

## 口径

引擎（`cmake/ird_gates.cmake`）以 `cmake -P` 直跑双端全量收集（引擎终局 FATAL
不影响命中行收集）：head＝ui-t20 任务工作树、base＝`564b8074` detached worktree
（`build/ird-t20-base-wt`）。命中行归一化（去 `[ird_gates] ` 前缀、去 IRD_ROOT
绝对路径前缀、排序去重）后逐行 diff——沿 WP-24-T08 留痕同款方法。

## 比对结论

- base：78 条归一命中；head：79 条归一命中。
- **增量恰 1 条**（见下）；既有 78 条存量命中逐一保持不变（除该行外 diff 为空）。

```text
IRD-GATE-SUB: 测试目标 sdurws_ird_ui_contract_test 直链他单元产品目标 sdurws_ird_runtime（允许形态仅同单元被测目标；跨单元需求经 testkit 替身或 DTB §4.5 登记）
```

## 逐条依据

| 命中 | 依据 |
| --- | --- |
| SUB：sdurws_ird_ui_contract_test → sdurws_ird_runtime | UI-T20 契约测试增列 `PresentationBridgeContractTest.cpp`（集成模式专属，`if(TARGET sdurw_kinematics)` 判别）——ui 自有端口面 ↔ runtime RT-T14 契约面（HostPresentationView）的**测试侧直链**：RT-T14 面形状钉（工厂签名/三类身份访问器/反解面/只读视图六处 static_assert）＋工厂唯一入口空快照 fail-fast 实例级契约。O-31 裁决的测试面既定形态（"对端公共头直链归测试目标；产品面零对端知识不变"），**UI-T14 同型**——该目标既有 project/execution/diagnostics 三条直链已在册，本条为同一机制下的对端扩面。产品库 `sdurws_ird_ui` 链接块仍仅 core/diagnostics＋Qt 三件套（ui/CMakeLists 配置期守卫硬断言＋LinkageContractTest.ArchEdgesCoreDiagnosticsOnly 文本钉＋BuildRedLineTest include 面扫描三道防线不变，均通过）。 |

## 比对件（同目录 builds/wp10-t20/）

| 文件 | 内容 |
| --- | --- |
| `ird-gates-head.log` / `ird-gates-base.log` | 引擎原始输出（cmake -P 双端） |
| `ird-gates-{base,head}-norm.txt` | 归一化命中集（比对基准：78 ↔ 79） |
| `ird-gates-hitset-diff.txt` | 归一 diff（唯一增量＝上述 1 条 SUB） |
