# REUSE-T53 — DYN-FD 适用性审查报告（candidate → verified）

- 日期：2026-10-24 登记（FD production 证据链补救批，四象限真实运行见 production/production-fd-evaluate.json）
- 能力：DYN-FD（边界＝真实场景、后端、初态/力/步长/确定性；ownerUnit=dynamics）
- 上游：`RobWork/RobWorkSim/src/rwsim/simulator/PhysicsEngine.hpp`（initPhysics/step/load；target=sdurwsim，经 sdurwsim_ode 实体后端 ODESimulator 承载）
- 版本钉扎：review.version=`011f45ab8cb8fd2970be892933c87d48fae09186`（[WP-09-T53] FD 迁移落地提交；该提交与工作树在 PhysicsEngine.hpp 与 rwsim/CMakeLists.txt 上逐字节一致——框架冻结面零改动）

## 1. 审查依据

1. **供给提案**：`reuse/extensions/GAP-FD-BUILD-01.md`（REUSE-T31 定向探测收口，独立验收 PASS 归档 `REUSE-T31-20261025-r1`）；A3 裁决（2026-10-26 所有者指令「A1~A4 全按推荐」）＝FD 按 REUSE-T31 提案执行，ODE 供给（vcpkg ode 0.16.6＋BUILD_sdurwsim_ode=ON）获批。
2. **本包 production 证据**：`traceability/reuse/REUSE-T53/production/production-fd-evaluate.json` 四象限（normal 119/119、oracle 解析基准/确定性/负载共源 3/3、upstreamComparison 同 XML 绑定写明、disconnected 力输入通道去除真实失败后原字节恢复复验）。
3. **对照先例**：L1 verified 升档先例＝DYN-RNE（REUSE-T52 suitability.md：独立物理 oracle＋上游直接对照＋断开实验）。本包按同一证据形态补齐 FD 面（无独立直连对照用例，以同一 XML 绑定＋说明承载，见 production 报告 upstreamComparison.command）。

## 2. 适用性结论

- **真实场景/后端**：FD 检查全链经 runtime::buildFdEngineSession 受控会话（runtime 公共工厂，模型构造权归 runtime——ARCH §3.6/SA-19）驱动真实 ODESimulator（sdurwsim_ode）initPhysics/step；无自持质量阵/消元/RK4 残留（旧核符号断开钉扎于 ForwardDynamicsBoundaryContractTest）。
- **初态/力/步长**：SI 广义力采样保持提交、maxStepS 子步推进、初速播种与摩擦补偿差异四项在会话契约登记（FdEngineSession.hpp）；业务侧无隐藏 fallback。
- **确定性**：DynForward.DeterministicRepeat_NFR_COR_02 同输入双跑逐位一致（oracle.xml 实测）；引擎为确定性固定步进（ode-fixed 词表）。
- **上游适配差异**：六项消费边界适配零上游改动（力控仅转动→移动关节 fail-fast／摩擦死代码→声明三元组补偿／初速不回种→伺服播种／基座帧身份→载体挂链／零几何 FixedBody→占位球／读回钳制差异→限速承载），逐项在 changes/REUSE-T53.json 与 FdEngineSession.cpp 凭证登记。

## 3. 升档裁定

DYN-FD 自 candidate 升 **verified**：提案（1.1）＋四象限生产证据（1.2）＋L1 先例同型证据形态（1.3）齐备；upstream 绑定符号（initPhysics/step/load）与生产消费路径一致（production.upstream=step）。limits 登记随之由占位语更新为实测口径。
