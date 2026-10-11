# WP-13-T17 构建与验证留痕（WorkCell 反向导入通道——MDL-18/AT-33，R2/阶段 D）

实施分支：`wp13-t17`（工作树 build/pipeline-wt2，基线＝origin/redesign-main @ fc75ccfa）
实施日期：2026-10-12

## 交付物

- io 侧：`ResourceIo.hpp/.cpp` 增补 `IResourceReader::openGenericXml`（io.md §9.6 v0.10 冻结签名）＋
  `GenericXmlNode`/`GenericXmlDoc` 值类型——expat 自实现文档序结构树（属性/子元素保文档序、
  前缀原文保留、直接子文本分层聚合）＋全文件 SHA-256（core ContentDigester）；防护流程与
  snapshot 同序（SafePath→SingleFileBytes 预检→SP-6 打开→每块预算/取消检查点→摘要→解析）；
  不引入框架 sdurw_loaders（DTB §4.6 表行）。
- modeling 侧：`WorkCellReverseImport.hpp/.cpp`（提取引擎——§6.6 提取语义逐条落位）＋
  `Import.cpp::mapWorkCellXml` 从 R1 NotImplemented 边界响应切换为委托真实提取；
  `'workcell-channel-r2'` 引导词表退役＋ImportUnsupportedItem kind 追加 WorkCell 通道六值
  （device-type/scene-object/collision-setup-external/q-config-residual/joint-acceleration-limit/
  base-placement）。
- 草稿红线：导入结果永远是草稿——不经确认不产生修订、不落盘、不经 project/**（PA-1）；
  "从 WorkCell XML 新建项目来源"维持明确不做。

## 等价调整登记（诚实条款）

io.md §9.6 v0.10 冻结的 `GenericXmlDoc.digest` 字段类型原文写作 `core::ContentDigest`——该类型名
在 core 公共头不存在（摘要值类型唯一＝`core::Digest256`，`ResourceSnapshot.contentDigest` 同型
先例）。实现按 `core::Digest256` 落位：算法（ContentDigester/SHA-256）、字节宽度（32）、语义
（内容身份）与卡面完全一致，零语义变更（io.md §9.0 "签名均为实现建议，实现期允许等价调整、
语义不变"条款）。io.md 未改动（units/io.md 不在本卡授权文件内）。

## 构建结论（如实）

| 项 | 命令 | 结论 |
| --- | --- | --- |
| 集成构建（industrialrobot 全链） | `cmake --build build --config Release`（逐目标：sdurws_ird_{core,diagnostics,io,policy,runtime,modeling,project,evidence,requirements,kinematics,trajectory,dynamics,selection,optimization,ui}＋modeling 的 test/contract_test/gui_test/app/demo6r＋io 的 test/contract_test） | 零错误 |
| 集成构建（框架侧例外） | 同上全树构建 | RobWorkSim 侧 `RWSimulatorPlugin`/`bt_plugin` 两目标**预存在编译失败**（框架头 rw/math/Math.hpp 与 vcpkg eigen 头宏冲突——本任务改动前即不可构建，SA-02 零源码修改不予触碰；与 industrialrobot 无依赖关系） |
| 独立冒烟 | `cmake -S industrialrobot -B build-smoke -DCMAKE_TOOLCHAIN_FILE=vcpkg... -DCMAKE_PREFIX_PATH=Qt6.11.1/msvc2022_64` ＋ `--build ... --config Release` | 配置＋构建零错误 |
| 构建缓存开关 | `grep RWS_BUILD_INDUSTRIALROBOT build/CMakeCache.txt` | `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` |

## 测试结论（如实——全部真实执行，_report 目标口径）

主测试（14 目标，全绿 2659/2659）：

| 目标 | 用例 | 结论 |
| --- | --- | --- |
| sdurws_ird_io_test | 127（含新增 GenericXml 通道 4 条） | 全绿（gtest XML：gtest-reports/wp-13-t17/sdurws_ird_io_test.xml） |
| sdurws_ird_modeling_test | 371（含新增 MdlWorkCellImport 9 条＋R1 退役用例更新） | 全绿（sdurws_ird_modeling_test.xml） |
| 其余 12 目标（requirements 212/kinematics 183/trajectory 179/dynamics 111/selection 284/optimization 172/core 64/evidence 205/policy 206/project 257/runtime 271/diagnostics 117） | 2426 | 零回归全绿 |

契约/附加（全绿 496/496）：contract_test 13 目标 417（io 9/modeling 17/requirements 16/kinematics 57/
trajectory 69/dynamics 30/selection 64/optimization 49/evidence 14/policy 23/project 28/diagnostics 26/
drivetrain 15）＋modeling_gui_test 42＋drivetrain_test 37。（core/runtime 无 contract_test 目标——既有布局。）

新增用例与 AT/需求追溯（用例名与断言内登记）：

- `MdlWorkCellImport.Extract_JointTypeLimitPoseAxis_WP13T17_ACC1`——MDL-18/MDL-11/AT-33：
  关节类型/限位（Revolute 源 deg→rad、Prismatic m）/位姿（Pos m＋RPY 源 deg→rad）/轴（固定局部 z）逐项。
- `Extract_QConfigToZeroOffsetResidualListed_WP13T17_ACC1`——Q 首配置→zeroOffset＋备选构型未保留逐项。
- `Extract_GeometryResourceRecorded_WP13T17_ACC2`——Drawable→visual/CollisionModel→collision/
  Polytope→Recorded（缺失叶 IO-RES-MISSING 事实＋missing 状态，不置阻断）。
- `Extract_LossyReportItemsListed_WP13T17_ACC2`——有损报告逐项列出（collision-setup-external/
  base-placement/joint-velocity-limit/scene-object/Camera ignored/defaults 下界）。
- `Extract_UnsupportedTypesNoDraft_WP13T17_ACC2`——Spherical/Movable/Depend→无草稿＋
  UnsupportedJointType＋逐项清单（MDL-10/12 同一判定）。
- `Extract_MultiDeviceBranchSelection_WP13T17_ACC1`——分支报告/未选链拒绝/选链成功/选链不存在。
- `DraftIsPureValueNoRevision_WP13T17_ACC3`——草稿不产生修订（确定性临时句柄两次调用逐字段相等＋
  checkInvariants 全量核查；PA-1）。
- `CapabilityAndDiagOrder_WP13T17_ACC1`——BeyondTemplateRange（V12-01 类型保留）＋诊断行序稳定。
- `InterfaceDelegatesToEngine_WP13T17_ACC1`——mapWorkCellXml≡extractWorkCellDevice（公共接口消费钉扎）。
- io：`GenericXmlChannelParsesDocumentOrderAndDigest`（MDL-18/AT-33/NFR-SEC-01——文档序/前缀原文/
  text 分层/digest 与 snapshot 同算法）＋`GenericXmlChannelMalformedReportsSyntaxWithLocation`
  ＋`GenericXmlChannelMissingSourceIsNotFound`＋`GenericXmlChannelCancelReturnsCancelledState`。
- 更新：`MapWorkCellXml_ChannelImplementedR1Retired_WP13T17_ACC1`（原 WP13T05 R1 边界用例——通道
  实现后语义更新）；PackageTest `ImportRejectsForeignArtifact_WP13T13_ACC3` 中 mapWorkCellXml 断言
  更新（NotImplemented→SourceInconsistent——非 WorkCell 字节仍无草稿 fail-closed）。

## 门禁与文档验证

- `ird_gates`（构建目标直跑）：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 **零命中**
  （日志 ird-gates-target.log）——基线（主线 fc75ccfa）本为零命中，故零新增。
- `validate-docs.ps1`：PASS（20 units, 12 trace entries, 313 task files）。
- `validate-task.ps1 -TaskFile tasks/foundation/WP-13-T17.json`：PASS。

## 文档同步

- units/modeling.md：§6.6 更新为已实现（提取语义逐条＋DH 提取边界登记）；§2 分期承接行/
  §11 T17 行/§12 边界行/§13 AT 承接声明同步；头部版本行 v0.47＋§15 修订记录 v0.47 行。
- development-task-breakdown.md：§2.14 WP-13-T17 表行更新为已实现。
- tasks/foundation/WP-13-T17.json：status=done＋note 落位登记（含等价调整）。
