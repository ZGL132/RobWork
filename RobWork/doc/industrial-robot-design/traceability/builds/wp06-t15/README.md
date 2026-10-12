# WP-06-T15（≙RT-T15）验证留痕

任务：编译器基本图元几何挂接（S6 增量——圆柱连杆/标记盒＋mesh 资源通道接线预留；契约 tasks/foundation/RT-T15.json）
分支/基线：rt-t15（base＝redesign-main@8e494e59——立项立卡治理提交后）
日期：2026-10-12

## 构建结论（双模式）

| 模式 | 命令面 | 结论 |
| --- | --- | --- |
| 集成模式 | `cmake --build build --config Release --target sdurws_ird_runtime sdurws_ird_runtime_test`（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 已确认） | 零错误（integration-build.log） |
| 独立冒烟 | 全树 build_smoke_t15-rt（`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build_smoke_t15-rt`＋vcpkg toolchain＋Qt prefix 绝对路径；全新目录未复用历史树） | 零错误（smoke-build.log） |

**Gating 核查**：WorkCellCompiler.cpp 增量与 test/WorkCellGeometryTest.cpp 均在 `if(TARGET sdurw_kinematics)` gated 块内（消费真实 Cylinder/Box/RigidObject 非模板符号——RT-T07 系同因）——冒烟编列实测 gtest_list_tests **零命中** WorkCellGeometry/RT_GEO，冒烟 179/179＝既有基线不减。

## 测试结论

| 目标 | 集成树 | 冒烟树 | 基线对照 |
| --- | --- | --- | --- |
| sdurws_ird_runtime_test | **276 通过＋1 设计跳过**（ContractSuite.ChildProcessWorker_RT_ID_1——worker 占位按设计跳过，既有口径）＝277 例 | 179/179 PASS | ＋RT-GEO 族 6 例（271→277） |
| ctest -L "^ird$"（runtime 目录） | 1/1 通过 | — | 既有登记（RT-T14 补登） |

新用例（acceptance 具名自证，RT-GEO 族——gtest XML＝gtest-runtime.xml）：
- RT_GEO_1 PrimitivesAttachedPerMappedFrame（存在性：连杆圆柱×3＋标记盒×3 各绑定其映射帧＋载体对象每帧一个＋BaseMount/关节帧零越界）
- RT_GEO_1 GeometryPoseFollowsFrameThroughFk（位姿随帧：载体 base＝帧树同一对象＋帧内位姿随 State 恒定＋法兰 worldTframe 随 q 变化——同源随动）
- RT_GEO_2 DerivedDimensionsAreSiTruth（单位：圆柱高＝跨度、半径＝clamp(0.15×L, 5 mm, 80 mm) 三分支、盒恒 20 mm、朝向/中点位级；基线 Cylinder 以 float 承载尺寸——1×10⁻⁶ m 容差复核，测试常量注释登记）
- RT_GEO_3 SameModelSamePrimitives（派生确定性：两次独立编译图元集合位级相等——NFR-COR-01）
- RT_GEO_4 HonestSkipWithoutProviderOrManifest（诚实跳过：manifest 空/provider 缺位→编译成功零虚构零错误＋provider 在位→S6 零调用〔计数替身〕——mesh 通道只预留接口位）
- RT_GEO_5 S6ProductSurfacesUnchangedByGeometry（零回归：runtimeNames 逐字节/帧数/限位逐字＋模型 RT-Codec 编码编译前后逐字节不变——显示代理不入身份域）

## ird_gates 结论（base..head 归一化比对）

- 引擎直跑：head（rt-t15 工作树）与 base（8e494e59，detached worktree C:/Users/zgl18/AppData/Local/Temp/t15_base_wt）各自 **exit 0＋"全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中"**；IRD-GATE-\* 归一化直方图行完全一致（7 行——均为引擎自测预期检出行，非真实违例）＝**零新增零消除**。
- 比对件：ird-gates-hist-{base,head}.txt＋双端原始日志（同目录）。

## 治理校验

- validate-docs：PASS（20 units, 12 trace, 315 task files）——validate-docs.log
- validate-task（RT-T15）：PASS——validate-task.log。★ 实施段卡面同步两处（卡面 note 已登记）：①designRefs 散文条目（指向已移除的欠账注释）收编为合法锚点形态；②knownPitfalls 中 findings 系 id（F-550）按 O-nn/CR-nn/P-XX-nn 句法规则收编（F-550 指向由 designRefs＋note 承载）——均非语义裁决，status 仍＝ready 待独立评审放行（置 done 归合入收尾段）。
- 契约 verify 五条全部执行：①runtime 目标构建零错误 ②ctest -L "^ird$" 1/1 ③ird_gates 零新增（本文件口径）④validate-docs PASS ⑤validate-task PASS

## 实施段卡面同步与如实登记

- **保真度升级（真实 visual/collision mesh 渲染）＝待所有者裁决**，本批只保留接口位（provider 句柄经 compileWorkCell 私有签名接线预留、诚实跳过零消费——RT_GEO_4 计数替身断言钉住），未私裁。
- 派生规则随落位冻结：跨度＝子关节 T_parent_joint 平移（DH a/d 显式表示对应量）、零跨度阈值 1×10⁻⁹ m（零长连杆诚实缺省）、半径＝clamp(0.15×L, 5 mm, 80 mm)、标记盒恒 20 mm——units/runtime.md §15.4 v0.22 登记。
- 实施期内测试自检迭代两次（非验收返工）：夹具 ratioPerJoint 长度不变量修正、Cylinder float 精度断言修正——详见 ird-test-report.json reworkNote。

## 交付物清单

- 增量：runtime/src/WorkCellCompiler.hpp（签名＋文档）、runtime/src/WorkCellCompiler.cpp（S6 第 9 步几何挂接＋spanPlacement/attachPrimitive/attachResourceMeshGeometry＋派生常量）、runtime/src/CompilerImpl.cpp（request.resources 透传 S6）
- 新增：runtime/test/WorkCellGeometryTest.cpp（RT-GEO 族 6 例，gated）
- 增列：runtime/CMakeLists.txt（测试 gated 块）
- 文档同步：units/runtime.md v0.22（版本头＋§12 RT-T15 落位标记＋§15.4 v0.22 行＋§8.1 P-RT-3 补记）；tasks/foundation/RT-T15.json（note 实施段同步）
- 留痕：本目录（README＋gtest XML＋ird-test-report＋双端 gates 日志与直方图＋integration/smoke 构建与测试日志＋validate 双证）
