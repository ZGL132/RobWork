# REUSE-T19：FK/Jacobian 上游复用（kinematics 消费已编译模型）

> 编号映射：REUSE-T19（编号映射：本包实施期登记为 REUSE-T18，因主线同号〔F-958 testkit 监听器收口〕冲突，经协调流程改登记为 REUSE-T19；原验收受检 SHA 20522f776/434500c43 与用例标识保留不变）。下文与代码/用例中的 REUSE-T18 字样为实施期标识，受检 SHA 与证据绑定不受改名影响。

实施记录（智能体A，2026-10-24，分支 codex/reuse-foundation）。精确契约：[REUSE-T18](../../../tasks/REUSE-T19.json)。
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

## 2026-10-24 所有者裁决与本地集成结果

本节是本包的最新交付状态；前文及冻结验收记录保留其实施/验收时点事实，不回写历史 PASS/FAIL。

- 所有者本轮原话：“授权 A 本地合入，上述 T13 前置门按照你的建议进行裁决”。协调者据此作有界裁决：T13 的 runtime 实现已经存在于主线 `691cbd42e`，本次 A 增量未修改 runtime、core 或治理脚本，允许已独验的 T17/T19 增量进行本次本地集成；本节明确替代此前“T13 独验 PASS 前不集成”的本次合入限制。T13 独验仍为 NOT RUN，未自签 PASS、未关闭债务，仍约束其自身结项与最终交付；不据此批准后续能力包、新核心或上游扩展，也不豁免 GOV-T21/GOV-T22 阶段门。
- 合入前复核：主线 `691cbd42e3f9ac951539f01edfafe99afd7055d1`，A 交付 `0a6b1cde0d22a5de35079cee02558f98812ca903`，两工作树干净，主线为 A 祖先，A 领先 12、落后 0。12 提交范围为治理补充/C4 R2 提案与实验（未实施产品提案）、T17 锚接口及独验、T19 FK/Jacobian 迁移及独验、编号修正、主线同步和复核归档；复核冻结 `8babc8d7068a44ae2b7818b7526406f73de7cdae` 到交付仅新增复核记录，产品内容未变。
- 本地合并命令：`git merge --no-ff codex/reuse-foundation`；合并 SHA＝`37a07c80922a4f818785ed02ce7464d629a31536`。第一父＝上述主线，第二父＝上述 A 交付；合并树与 A 交付树均为 `74ec91472d558b2cfa4c948e8090f0e8afc9e0d9`。无冲突、无 rebase/squash/cherry-pick，未操作远程。
- 使用主工作树现有 `build` 集成缓存（`RWS_BUILD_INDUSTRIALROBOT=ON`），未手工重新配置全库。VS2022 x64 下执行 `cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test sdurws_ird_kinematics_gui_test sdurws_ird_ui_test sdurws_ird_ui_plugin ird_gates`；CMake 自动更新既有构建文件，前五个受影响目标均完成构建，整体命令因末尾门禁失败返回 1，不能报告为构建/门禁整体 PASS。原件：[a-build.log](integration-20261024/a-build.log)。
- **新增集成阻断：ird_gates FAIL**。`IRD-GATE-LIB` 三项分别是 `${_ird_kin_rw_math}`、`${_ird_kin_rw_kinematics}`、`${_ird_kin_rw_models}`。来源为 T19 的 kinematics/CMakeLists.txt 三个局部目标选择变量：集成分支均解析为现有 `sdurw_math/sdurw_kinematics/sdurw_models`，实际编译链接成功；静态扫描按字面 token 判断，未解析这些变量。该问题在 A 冻结交付中已存在，本轮首次实际集成门禁检出；此前复核的 validate-reuse/validate-docs PASS 不能替代 ird_gates。门禁在此停止，内嵌 validate-reuse 未执行。本轮不修改冻结产品/CMake、不扩白名单或跳过扫描；由 A 返工修正并完成受影响验证/独立复核，协调者不自签消除此阻断。
- 主线 T17 锚接线定向回归（`sdurws_ird_ui_test.exe --gtest_filter=KinSessionAnchor.*:KinAnchorCapture.*`）：passed=2、skipped=0、failed=0，exit 0；VS2022 x64、`QT_QPA_PLATFORM=windows`、绝对路径、单进程。该程序是 QCoreApplication 模型测试，不冒充 Widgets 点验。日志/XML/per-exe 报告：[a-ui-anchor.log](integration-20261024/a-ui-anchor.log)、[a-ui-anchor.xml](integration-20261024/a-ui-anchor.xml)、[a-ui-anchor-report.json](integration-20261024/a-ui-anchor-report.json)。
- 数值/业务证据按相同产品输入直接引用：T19 原报告 291 passed、1 envUnavailable、0 failed；契约 57 passed、0 skipped、0 failed；断开实验 7 项预期失败；T17 Widgets 48 项通过，原 ui 全套既有 F-560 失败继续保留。生产 normal XML 仅含定向 FK 20 项，不能将该 XML 冒称全量 291 项原件。主机冒烟配置故障仍未修复，本轮冒烟测试 NOT RUN，不标 PASS。
- 文档、T17/T19 契约与全量复用检查使用 `validate-docs.ps1 -TaskFile` 数组批次执行，实际结果及退出码保留于 `integration-20261024/a-docs.log`；不另行重复其包含的 validate-task/validate-reuse。
- 当前结论：**A 已本地合入，但集成门禁存在明确阻断，非整体准入 PASS**。允许 B 仅同步最新本地主线并补 kinematics 消费者回归，同时继承并保留该阻断；不在本轮合入 B。T18＝监听器、T19＝本包、T20＝SetCheck 固定，历史编号映射保留。A/B 分支、工作树与构建缓存保留；两包收口前不叠加新能力包，新任务号由协调者统一预登记。
