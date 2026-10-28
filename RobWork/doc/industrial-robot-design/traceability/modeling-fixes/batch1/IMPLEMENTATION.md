# MDL-CR-1 关节值交接实施记录（待独立验收）

现场：C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork，codex/core-review-fixes；起点 a32c040d41b129dc974c0b61da51934dd947b696。所有者 2026-10-28 批准 F02/F04/F09。未修改框架、依赖、支持范围或误差阈值。

保留 RobotDesign 工程参数、SourcedValue 的状态/来源、runtime 模型唯一 owner。删除不区分关节类型的零偏置旋转折叠及 DH 搬运中遗漏速率的行为；适配既有 rw::math::Transform3D/Vector3D 与 core 单位合同。Prismatic 的偏置为局部轴方向平移（m），Revolute/Continuous 为旋转（rad）；runtime 不再补偿一次。DH 展开和正式 UI 权威切换均完整透传最大速度/加速度及来源。LinearVelocity/LinearAcceleration 单位 token 分别 m/s、m/s^2。

公共接口仅 DhChainJoint 新增两个来源值字段；没有新增数值核心，C03 求解器骨架未动。回滚范围为本批提交，后续依赖批须逆序回滚。

真实验证（Release、VS2022 x64、Qt6.11.1；C: 源码/构建，D: 只读依赖）：

|验证|passed|failed|skipped|证据|
|---|---:|---:|---:|---|
|新增正式回归修复前 RED|0|2|0|red.log / red.xml|
|sdurws_ird_modeling_test|482|0|0|unit.log / unit.xml / unit.ird-test-report.json|
|sdurws_ird_modeling_contract_test|17|0|0|contract.log / contract.xml / contract.ird-test-report.json|
|原评审行为探针全组|3|11|0|replay.log / replay.xml|
|混合链正负偏置＋速率真实 runtime 编译|1|0|0|mixed.log / mixed.xml|

原探针三个对应项 PrismaticOffsetMustTranslateNotRotate、PrismaticVelocityAndAccelerationAreAccepted、StandardDhPreservesJointRateLimits 全部 PASS。其余 11 项属于后续批；同 TU 头共存探针仍 C2011，归 F14，未报为通过。混合链补充探针同时验证 ±0.25m 偏置、多个非零 q、非平凡基座/TCP，以及编译设备中 0.3m/s、0.4m/s²；最大位置误差 2.23e-16m。

命令：cmake --build build --config Release --target sdurws_ird_modeling_test；同理 contract_test；按既有 XML/report 参数执行本 C: executable；pwsh -NoProfile -File build/modeling-review-20261028/reproduce-probes.ps1。新增隔离源码、响应文件和日志保存在本目录；GUI 本批 NOT RUN（无交互改动），不引用旧 GUI 结果为本批证明。

门禁运行及其实际结论见 gates.log、reuse.log、docs.log；未关闭历史复用债务，不自签独立验收。F02 对 kinematics/trajectory 的偏置空间交接恢复一致；F04/F09 对 runtime 与后续轨迹限值消费者保留合法输入；未对消费者另加补偿。

门禁实际结论：ird_gates 的 R/T/SUB/GRAPH/LIB/SA02 静态检查通过，附带复用检查失败；validate-reuse 与 validate-docs 均 FAIL 于未改动的 runtime/CMakeLists.txt：REUSE-T52/T53 的历史凭证摘要 5ef2e38b… 与当前 23b072f9… 不符（baseline-freshness.json）。未修写历史凭证或降低检查，也未据此声称全局 PASS。C03 冻结的 pending 核心判定保持原样，未虚构迁移结项。
