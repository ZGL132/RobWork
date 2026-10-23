# REUSE-T18：FK/Jacobian 上游复用（kinematics 消费已编译模型）

实施记录（智能体A，2026-10-24，分支 codex/reuse-foundation）。精确契约：[REUSE-T18](../../../tasks/REUSE-T18.json)。
状态＝实施完成待独验：复用基础包 REUSE-T13 独立验收仍无 PASS 记录（所有者安排在途），本包不解除合入门、不据此关闭债务。

## 保留/删除/适配边界

| 符号/位置 | 复用来源 | 适配边界 |
| --- | --- | --- |
| `FkEvaluator::evaluate` 第4-7步 | `Device::setQ/baseTframe/baseJframe`（rw/models/Device.hpp）经 T17 锚→编译产物 WC＋任务私有 State | 锚获取/设备查找/DOF 防御/任务私有 State；q_rw＝q_auth−zeroOffset（src/JointMapping.hpp）；命名 TCP 一次偏置复合＋参考点移轴 |
| 默认 TCP 位姿 | 编译产物 TCP FixedFrame（WorkCellCompiler 第6步，tcpOffset 即帧变换）→`baseTframe` | 帧名经锚 nameResolver（R-4），不拼串 |
| 奇异值 | `rw::math::LinearAlgebra::svd`（JacobiSVD 降序） | sigma→容器；Eigen JacobiSVD 直调与"rw::math 无 SVD"旧注释删除（GOV-T21 勘误兑现） |
| 条件数/可操作度/关节裕量/IRDPM01 编码 | 保留产品实现（逐字节） | 无 |
| 错误面 | IllegalQ/NoDevice/NoTcp/FrameUnresolved 4 值不变 | 锚缺失/设备未命中→NoDevice（§9.2 @pre 运行期承载） |
| 手写链式 FK/几何雅可比/JacobiSVD 直调/toEigen·toRw | 删除（H2） | — |

q_auth→q_rw 全库检索：dynamics 为快照透传不参与计算、runtime 映射归编译器 owner——无既有公共入口，三行纯算术落位单元私有 src/JointMapping.hpp（薄适配，不建跨单元面）。

## 上游符号核对结论

- `Device::baseTframe(f,state)`＝T_base_f（robabx{b}{f}{T}——与契约同读法）。
- Jacobian 行义：`Jacobian::addPosition`＝行 0-2、`addRotation`＝行 3-5（math/Jacobian.cpp 源码）——与 PoseMetrics 契约逐行一致，零重排；列＝设备活动关节＝可动链序（SerialDevice 构造序，S8 保证）。
- `LinearAlgebra::svd`＝Eigen JacobiSVD（ComputeFullU/FullV）——奇异值降序与旧 ThinU/ThinV 直调同核。
- WC 关节坐标＝q_rw：编译链将 zeroOffset 烘焙进上游关节（`setFixedTransform(getTransform(zeroOffset))`，T13），State 从 q_rw=0 起。
- 编译产物含每工具 TCP FixedFrame（挂法兰）与 `RobotChain.deviceName` 随模型携带——设备/TCP 均经受控视图按名（映射生成物）查询。

## 验证（真实验证留痕，本目录 production/）

- normal：集成树 sdurws_ird_kinematics_test 全量 **291/291 PASSED**（1 项预存 envUnavailable skip 与本包无关）——FK 全套经真实编译快照锚；契约测试 57/57。gtest XML＋ird-test-report＋log 原件在 production/normal、production/contract。
- oracle：独立 4×4 齐次参考实现（夹具 referenceFk，与产品零共享代码）逐元素对照（解析算例/六轴恒等式/跨视图隔离）。
- upstreamComparison：`KinFk.UpstreamDirectDeviceComparison_REUSE_T18`——同模型同 State 直接调用上游 Device 与评估器输出 EXPECT_DOUBLE_EQ 逐位一致。
- disconnected：变异 Fk.cpp（setQ 输入切断为恒零构型，mutation/originalSha256/mutatedSource 绑定在 evidence JSON）→重建→`--gtest_filter=KinFk.*`＝**exit 1、7 用例失败**——业务断言真实依赖上游返回值，断开后即红。实验后源码逐字节还原（digest c54affbf… 核对）。
- 构建面：集成模式 kinematics 库/test/contract 全量 Release 构建零错误。冒烟＝REUSE-T07/T12 同机制（集成真实目标/冒烟 RW:: 导出包）；本机 .cache/smoke 树重新配置在 **modeling 的既有 find_package(RobWork)（T07 落位代码，本包未触碰）** 处失败（主 checkout RobWork 构建树路径失效："Looking for sdurw - not found"）——环境级回归早于本包，未在本包扩权修复，交所有者裁决；kinematics 冒烟源集未新增排除 TU（Fk.cpp 两模式编译）。

## 语义变化登记（契约同步）

- `AuthoritativeQZeroOffsetNotAdded`：T03 旧断言＝同几何双模型（zeroOffset 0/0.5）**字节级**同输出（值域 FK 不消费该字段）；T18 起偏置由上游关节承载＋输入映射相消，浮点复合路径不同→同断言改容差（附录 D 第 9 项标量容差）。消费语义不变；单模型"同输入同字节"确定性契约不变（DeterministicByteOutput/ACC5 bitwise 用例照旧通过）。
- 锚缺失视图（runtimeModelView()==nullptr）从"静默可用"变为 NoDevice 值面拒绝——§9.2 @pre 的运行期承载；生产提交路径（T17 锚捕获）不受影响。

## 未完成与移交

- T13 独验 PASS 前不集成、不关债务；本包验收由所有者授权的独立会话执行（智能体A不自签）。
- 冒烟树环境回归（RobWork 导出包解析）需所有者裁决：修复主 checkout RobWork 构建树/导出，或登记冒烟基线快照。
