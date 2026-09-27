# WP-06-T14（≙RT-T14）验证留痕

任务：宿主呈现 WorkCell 契约（方案 B.1 迁移链阶段 2；契约 tasks/foundation/RT-T14.json）
分支/基线：rt-t14（base＝redesign-main@c766807b——UI-T19 收尾提交后）；流水线 tick#395 领取
日期：2026-09-27

## 构建结论（双模式）

| 模式 | 命令面 | 结论 |
| --- | --- | --- |
| 集成模式 | `cmake --build build --config Release --target sdurws_ird_runtime sdurws_ird_runtime_test`（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 已确认） | 零错误 |
| 独立冒烟 | 全树 build_smoke_t14-rt（vcpkg toolchain＋Qt prefix 配置；新目录——build_smoke_t14 为历史任务既有树，未复用） | 零错误 |

**Gating 核查**：HostPresentationView.cpp／HostPresentationViewTest.cpp 按既有 RT-T07/T08/RT-T12 同因（消费 Snapshot.cpp→需框架库）列入 `if(TARGET sdurw_kinematics)` gated 块——冒烟编列实测不含 RT-PRES 族（gtest_list_tests 零命中），冒烟 176/176＝既有基线不减。

## 测试结论

| 目标 | 集成树 | 冒烟树 | 基线对照 |
| --- | --- | --- | --- |
| sdurws_ird_runtime_test | **265 通过＋1 设计跳过**（ContractSuite.ChildProcessWorker_RT_ID_1——worker 占位按设计跳过，既有口径）＝266 例 | 176/176 PASS | 261→266（＋RT-PRES 族 5 例） |
| ctest -L "^ird$"（runtime 目录） | 1/1 通过 | — | **本任务新增标签登记**：runtime 单元此前未登记 `ird` 标签（ui/modeling/requirements 均有），致契约 verify[1] 零匹配——随本任务补登记（set_tests_properties LABELS "ird"，先例同型） |

新用例（acceptance 具名自证，RT-PRES 族）：
- RT-PRES-1 IdentityBindingsVerifiable（三类身份：modelIdentity 对照快照/appliedRevisionId 对照入参/presentationIdentity 每构造新生成）
- RT-PRES-2 ViewLifecycleDoesNotTouchSnapshotIdentity（构建/销毁/重建快照身份逐位不变）
- RT-PRES-3 PresentationStateMutationIsolatedFromSnapshot（setQ 副本修改零触快照——反向隔离）
- RT-PRES-4 ResolutionAndTransformReuseSameSource（反解双向往返同源＋worldToBase/baseToWorld 同值——P-RT-4/AT-37）
- RT-PRES-5 FactoryFailsFastOnInvalidInput（空快照/全零修订 InputInvalid 整体失败）

## ird_gates 结论（base..head 归一化比对）

- 引擎直跑：head（rt-t14 工作树）与 base（c766807b，detached worktree）各 **151 条 IRD-GATE-\* 归一化直方图行完全一致——零新增零消除**（新增文件全部落在 runtime gated 块，零链接语义变更）。
- 比对件：ird-gates-hist-{base,head}.txt＋双端原始日志（同目录）。
- 如实登记：构建期 ird_gates 目标即红为 F-019 存量事实（base 同红），非本任务引入。

## 治理校验

- validate-docs：PASS（20 units, 12 trace, 201 task files）——validate-docs.log
- 契约 verify 四条全部执行：①runtime 目标构建零错误 ②ctest -L "^ird$" 1/1（标签随本任务补登记）③ird_gates（本文件口径）④validate-docs PASS

## 交付物清单

- 新增：include/sdurws/ird/runtime/HostPresentationView.hpp、src/HostPresentationView.cpp（gated 块）、test/HostPresentationViewTest.cpp（gated 块）
- 增量：runtime/CMakeLists.txt（产品/测试 gated 增列×2＋ird 标签登记）
- 文档同步：units/runtime.md v0.17（§12 RT-T14 落位登记注＋§15.4 v0.17 行）
