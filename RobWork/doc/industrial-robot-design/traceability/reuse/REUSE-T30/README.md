# REUSE-T30：WP-06/T02 runtime 动力学模型映射复用上游

任务契约：[tasks/REUSE-T30.json](../../../tasks/REUSE-T30.json)（migration §13.3 WP-06 行；父任务 REUSE-T02 保持 planned）。
起始 SHA：`c816a2c482088b9d03d537c103cf9ac314259187`；分支 `redesign-main`；依赖 GOV-T21/GOV-T22（验收引用在契约）。

## 定向交接索引（适用上述基线）

| 内容 | 位置 |
| --- | --- |
| 上游适用性/被否候选/GAP 登记 | [suitability.md](suitability.md) |
| 逐功能矩阵 | [reuse/functions/runtime.json](../../../reuse/functions/runtime.json)（runtime.compileDynamicWorkCell、runtime.DynamicWorkCellConstView.accessors＝adapter/migrated；tryDynamicWorkCell 条目 capabilityId/search 纠错为 DWC 面维持 pending） |
| capability | [reuse/catalog.json](../../../reuse/catalog.json)（RUNTIME-DWC-MODEL＝verified，upstream 9 文件含 rwsim/dynamics 全套＋探针面） |
| 逐文件凭证 | [reuse/changes/REUSE-T30.json](../../../reuse/changes/REUSE-T30.json) |
| production 证据（四面） | [production/runtime.compileDynamicWorkCell.json](production/runtime.compileDynamicWorkCell.json)、[production/runtime.DynamicWorkCellConstView.json](production/runtime.DynamicWorkCellConstView.json) |
| 权限 | reuse/permissions.json（REUSE-T30 两条 grant：src＝construct-model/mutate-model；test＝write-state/mutate-model，任务私有 State） |

## 实施与适配边界

- **代码零核心变更**：S7 映射（`runtime/src/DynamicWorkCellCompiler.cpp`）已直接构造上游对象（BodyInfo/FixedBody/RigidDevice/DWC/MaterialDataMap——RT-T08 既有实现），本包仅注释级同步复合刚体裁决（3c 段）与 §8.5 4c 存在性校验语义（4c 段）。
- **新增 3 个 oracle 测试**（`runtime/test/DynamicWorkCellCompilerTest.cpp`）：
  1. `DwcDefaultStatePosesAtZeroOffsetReference_REUSE_T30_RT_EQ_1`——非零 zeroOffset（0.4/−0.25 rad）默认 State 下位姿/bounds/Device 同一性（期望取值点 q_auth=zeroOffset+q_rw，REUSE-T13 语义复用）；
  2. `DwcFixedSegmentCompositeRigidBody_REUSE_T30`——固定段复合刚体：body→关节多重集（{j1,j1,j2}，set 同构盲区实证 3 去重 2）、同段角速度相等＋独立期望 (0,0,1.5)、跨段两关节贡献期望；
  3. `DwcProductConsumableByUpstreamRne_REUSE_T30`——RNE 可消费性（单 z 轴非退化：validate、solve DOF+1、静止 cross(com,F) 全分量、ddq 合成 (I_zz+m(rx²+ry²))·α＝0.16、setPayload 通道）。
- 公共视图 `Adapter.hpp`/`Snapshot.hpp` 零变更；`runtime/CMakeLists.txt` 零变更（无新测试文件——区段入既有测试 TU）。

## 原始证据与当前结论（2026-10-25）

| 证据 | 结果 |
| --- | --- |
| 全量 runtime 测试（`run-tests.ps1 -Regex '^sdurws_ird_runtime_test$'`） | 309 项＝308 passed＋1 skipped（`ContractSuite.ChildProcessWorker_RT_ID_1` 既有环境跳过）＋0 failed＋0 notRun（[ird-test-report](../../../../../build/RobWorkStudio/src/rwslibs/industrialrobot/runtime/ird-test-report.json) 同款副本 [normal-report-t30.json](normal-report-t30.json)） |
| 本包全量复跑（evidence normal/oracle/upstreamComparison 面） | 308 tests ran、308 passed、exitCode=0（[runtime-t30.xml](runtime-t30.xml)、[runtime-t30.log](runtime-t30.log)） |
| modeling 消费者（`sdurws_ird_modeling_test` 全量） | 479 passed＋0 failed（含 `MdlPackageWorkCell.WorkCellDwcXmlExportFromSnapshotView_WP13T13_ACC5`——评审缺口 2 触发条件） |
| mutation 聚合断开（mass 置零＋重力偏移 0.01，作用于登记生产源码） | exitCode=1、`CompiledPathBodiesAndExplicitValues_RT_T08`＋`DwcProductConsumableByUpstreamRne_REUSE_T30` 真实失败（[disconnected.xml](disconnected.xml)/[disconnected.log](disconnected.log)/[disconnected-source.txt](disconnected-source.txt)）；恢复后摘要复验 f296c73e… 并复跑 308 passed |
| 上游语义探针（临时 gtest，用后即删） | j2 轴 y、com=(0,0,0.2)：rest==bent（τ_x=±5.886）、bent j2 帧旋转 Z 列=权威轴（R_align 在关节帧链上）——GAP-RNE-FRAME 定标数据；getVelocity linear 定标（AD 附加项 2×量级） |

## GAP 登记（所有者 2026-10-25 escalate 裁决：接受为登记缺口，包内不实施修复）

**GAP-RNE-FRAME**：权威轴≠z 时上游 `RecursiveNewtonEuler`/`RigidLink` 把 `BodyInfo.masscenter` 乘关节帧旋转（含 T13 轴对齐 R_align），与 S7 承载帧系（权威连杆系、Body.hpp body-frame 契约）错位——质心世界位置错位。修复落点提案比较（①S7 改填 R_alignᵀ 变换值——动 §8.2 逐字断言＋modeling DWC 导出＋RigidLink 速度参考面；②RNE 消费边界薄适配（H3）——可能不动两断言面）见 [suitability.md §6](suitability.md)；**WP-07/WP-09 在该缺陷处置前不得实施依赖部分**（migration §13.2 交接索引行）。

次要发现（归 WP-07 深核）：①RNE 重力不入映射产物（ctor 默认零——消费方须 `setGravity` 显式传递）；②`getJointForces` payload 力矩传递含 `cross(dr−com_link, F_p)` 项，与直接物理手算（payload 质心臂）不一致；③`RigidLink::getVelocity` linear 分量经 `inverse(bTf)·(bJf·vel)` AD 变换含体位置相关附加项（参考点语义未文档化）。

## 独验状态

实施完成待独验（状态＝ready、note 已注明；实施者不自签）。独验由所有者另开会话执行；三门禁（commit-lint/validate-docs/verify-task-scope 显式 Base/Head）在收口提交时复跑留痕。
