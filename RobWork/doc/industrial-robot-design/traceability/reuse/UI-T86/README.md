# UI-T86——ui 13 项数值链失败诊断返工（ui-13/P6，F-973）

起始 `28973eeee`（REUSE-T54 收口提交），redesign-main 主工作树串行。本包＝先诊断后修复：
WP-18 验收移交的 ui_test 14 项失败中 13 项真实数值链失败（F-973；另 1 项为预存 F-560 越界）逐项归因，
授权面内 12/13 修复核销；第 13 项修复面被 IRD-GATE-AD 权限门禁机器锁死，登记 **F-975 blocked** 待所有者裁决。
契约＝[tasks/foundation/UI-T86.json](../../../tasks/foundation/UI-T86.json)；findings F-973（fixed）/F-975（open）。

## 1. 诊断结论（逐项归因）

**根因单一**：REUSE-T18（`20522f776`，2026-10-24 01:38）把 kinematics FK/Jacobian 换核为"经 T17 锚
（`runtimeModelView()`）消费已编译 WorkCell"并删除手写链（Fk.cpp:238-243 锚缺失＝NoDevice 值面，
T17 契约明令禁止隐藏 fallback），其测试夹具迁移面**仅覆盖 kinematics 单元**（KinFkFixture.hpp 的
`compileRealSnapshot`/`TestView`），ui 单元两处视图替身未同步迁移——测试期望过期/迁移扰动，
**非产品回归、非环境性、非 T33 RPY 漂移**（F-973 登记行疑似源证伪：T33 生产面 rpyToRotation 未进入失败链；
13 项失败形态统一为 `fk.ok()=false`→NoDevice→IK 设施失败→批量 `outcomeState=not-run`）。
时序证据：KinEvaluationChannelTest.cpp 末次修改=T17 提交本身（e63cfaf8f，10-23，其时 FK 仍旧核故绿）；
HostCompilePipelineTest.cpp 末次修改早于 T17/T18（473897454，10-21）；T18 验证仅 kinematics_test/contract_test，
未跑 ui_test——故 redesign-main 主线未记录。

| # | 用例（WP-18 失败清单名） | 视图载体 | 归因 | 处置 |
| --- | --- | --- | --- | --- |
| 1 | NamedTcpClosureCompilesAndSolvesRealNumerics_REQ01_A15 | HostCompilePipelineTest.cpp:338 本地 `View` | 同上（无锚） | **blocked（F-975）**——修复面见 §3 |
| 2 | SolveChannel_BackgroundRealSolve_KIN_P4 | ExecutorHarness→`FixtureView` | 同上（无锚） | 修复核销 |
| 3 | BatchChannel_RealComputationAndOutcomes_KIN_P4 | 同上 | 同上 | 修复核销 |
| 4 | BatchFixedMixedRpyWithRotatedFrames_REQ09_AT23 | `FixtureView(worldBase,tcpR)` | 同上 | 修复核销 |
| 5 | BatchAlignFrameResolvesTargetAndRejectsDangling_REQ09_AT23 | `FixtureView` 族 | 同上 | 修复核销 |
| 6 | RegionNamedTcpAndMissingKey_REQ01_A21 | `FixtureView(model)` | 同上 | 修复核销 |
| 7 | BatchNamedTcpRealNumericsAndRejectsDangling_REQ01_A15 | `FixtureView(model)` | 同上 | 修复核销 |
| 8 | BatchHonorsEveryConstraintMask_REQ01_A16 | `FixtureView` 族 | 同上 | 修复核销 |
| 9 | BatchDirectionRulesDoNotFallBackToFixed_REQ09_A16 | `FixtureView` 族 | 同上 | 修复核销 |
| 10 | OppositeDirectionRejected_REQ09_WP14T12 | `ExecutorHarness`（h.view） | 同上 | 修复核销 |
| 11 | DirectionCorrectionFromZero_REQ09_WP14T12 | 同上 | 同上 | 修复核销 |
| 12 | BatchDirectionRulesFreeToolRoll_WP14T12 | 同上 | 同上 | 修复核销 |
| 13 | EnabledCollisionWithoutFacilityRemainsInsufficient_ARC05_A23 | 同上 | 同上（FK 先败于证据缺失轨） | 修复核销 |

（原清单第 1 项 DefectPinning.*_F560 为 REUSE-T17 在案越界项，不在本包范围，全程保持失败如实分列。）

## 2. 修复内容（12/13，产品 FK/IK 零触碰）

1. `KinEvaluationChannelTest.cpp` `FixtureView`：补 `runtimeModelView()` 覆盖——经
   `testfixture::compileRealSnapshot(m_model)` 懒编译真实快照（KinFkFixture.hpp 在案函数，S1-S5 注入替身
   CanonicalModel＋RuntimeSnapshotFactory S6-S10 全产品路径；T18 `TestView` 同款懒构造纪律）。T17 无锚值替身
   语义由 `AnchorlessValueView` 用例独立承载，两者不混同。
2. `ui/CMakeLists.txt`：`sdurws_ird_ui_test` 集成门控块内注入 `IRD_HAVE_RUNTIME_COMPILE=1`
   （kinematics_test:515 先例同款编译期开关；本守卫块整体冒烟树不注册，冒烟面零影响）。

**修复树验证（green13-\*）**：ui_test 全量 345 项＝344 passed＋1 failed（仅预存 F-560）——13 项全部 PASS
（含 NamedTcpClosure 原位修复候选，见 §3）；ird_gates 全绿；ui_contract_test 63/63（相邻面回归）。

## 3. 第 13 项 blocked（F-975）——修复面被权限门禁机器锁死

NamedTcpClosure 的修复（本地 `View` 补 `runtimeModelView()`＝同源物化快照 `materialized.snapshot` 本体）在
修复树全量实证 PASS（green13-\*），但落地面 `ui/test/HostCompilePipelineTest.cpp` 携带 UI-T78 渲染面
`#include <rw/graphics/DrawableNode.hpp>`（:72，:952 分组常量断言消费）。门禁链（reuse-contract.ps1 实测）：

- 变更文件提取 `graphics` 类别（:133 提取表，include 行生效）；
- grant 携带 graphics 类别必须 `origin=host-presentation`（:96）；
- host-presentation 要求文件路径 ∈ `hostExceptions[].files`（:100-104）；
- hostExceptions 文件族被硬校验为恰 `UiPlugin.{hpp,cpp}` 五对（:85-90）——测试路径恒不可入。

**门禁本体实测**（三文件临时清单＋契约在位后）：`REUSE-AD : .../ui/test/HostCompilePipelineTest.cpp :
restricted upstream use needs exact file/target/operation grant (IRD-GATE-AD)`，exit 1——见 gate-ad-blocked.log。
UI-T86 终态按增量回退该文件（修复不入树），登记 F-975 blocked；终态树复跑（final-\*）：345 项＝343 passed＋
2 failed（F-560＋NamedTcpClosure）。

**候选补丁（所有者裁决后落点，已实证）**——HostCompilePipelineTest.cpp:337-343 替换为：

```cpp
    // 仅借用已发布不可变快照。适配器是IKinRuntimeView端口，不包含计算替身。
    // UI-T86（F-973 诊断返工）：REUSE-T18 起 FK/IK 只经 runtimeModelView 锚
    // 消费已编译 WorkCell（锚缺失＝NoDevice 值面，T17 契约禁止隐藏
    // fallback）——锚＝同源物化快照本体（RuntimeSnapshot 即
    // IRuntimeModelView；生产快照包装视图必须覆盖——KinTypes.hpp 表尾注），
    // 不经第二包装层伪造。
    struct View final : kinematics::IKinRuntimeView {
        const runtime::CanonicalModel& value;
        std::shared_ptr<const runtime::IRuntimeModelView> anchor;
        View(const runtime::CanonicalModel& model,
             std::shared_ptr<const runtime::IRuntimeModelView> a)
            : value(model), anchor(std::move(a)) {}
        const runtime::CanonicalModel& model() const override { return value; }
        rw::math::Transform3D<double> worldToBase() const override { return value.world().T_world_base; }
        std::shared_ptr<const runtime::IRuntimeModelView> runtimeModelView() const override
        {
            return anchor;
        }
    } view(materialized.snapshot->model(), materialized.snapshot);
```

所有者三选一：①门禁为 `/test/` 路径开 graphics 允许面（GOV 治理批次）；②宿主例外扩族接纳该测试文件
（ARCH §3.6.2＋reuse-contract.ps1 names 表同步）；③UI-T78 渲染面用例重组（语义改动涉及 UI-T78 已验收断言面）。

## 4. 治理同步（Digest 口径，sync-matrix-ui-t86.mjs 重跑可复现）

- `functions/ui.json`：ui/CMakeLists.txt 40 处 inputs 重钉；`ui.CMake.targetWiring` 整文件行 1..1311 重定位；
  `ui.FixtureView` 区段 77..153 重定位；`ui.ObservedView` 区段 992..1006 重定位；
  `ui.AnchorCaptureExecutorTest` 整文件行 1..1955 重定位；FixtureView/ObservedView nonOverlapEvidence 重钉。
- `permissions.json`：`REUSE-T17-KinEvaluationChannelTest.cpp` source/build 摘要重钉＋lifetime 登记锚能力迁移；
  `REUSE-T41-UiPlugin.{hpp,cpp}` build 摘要重钉。
- `reuse/changes/UI-T86.json`：KinEvaluationChannelTest.cpp（business）＋ui/CMakeLists.txt（governance）两件凭证。
- `validate-reuse.ps1` 终态实测 PASS（governance only）。

## 5. 证据清单

| 文件 | 内容 |
| --- | --- |
| green13-full-suite.xml/.log、green13-ird-test-report.json | 修复树全量（345=344+1，13 项全 PASS） |
| final-full-suite.xml/.log、final-ird-test-report.json | 终态树全量（345=343+2，blocked 项如实失败） |
| gate-ad-blocked.log | IRD-GATE-AD 门禁本体实测阻断（exit 1） |
| sync-matrix-ui-t86.mjs | 矩阵/凭证同步脚本（-includeHost＝取证模式） |

受影响消费者回归：ui_contract_test 63/63 PASS（终态树实测）；kinematics/runtime 产品面零改动，
其既有证据引用不变。
