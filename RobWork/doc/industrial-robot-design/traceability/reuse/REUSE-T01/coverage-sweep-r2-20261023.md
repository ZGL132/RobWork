# REUSE-T01 覆盖清查 R2（定向返工，2026-10-23）

**对 R1 的返工范围**：修正文件/需求/已知遗漏统计并给出可重算依据；将"字符串命中 / 语义区段覆盖 / 独验关闭"三档严格区分；保留 R1 快照（`coverage-sweep-r1-20261023.md` 原样不动，其过时数字以本档 §2 修正为准）；按主重构会话新提交做增量核对。**优先交付**：旋转/位姿链与 runtime→FK/Jacobian 链的具体遗漏与上游适用证据，同步核对相关 oracle 与构建接线。未扩大通用治理深审；未修改产品代码、权限或任务状态；未修改主会话矩阵；未操作远程。

**现场**：工作树 `C:/Users/zgl18/.codex/worktrees/coverage-sweep/RobWork`，分支 `codex/coverage-sweep`；R1 提交 `d8bc82403` 之后合入主会话 `reuse-t01` 增量（`8e4a08ac8` T07 SVD 实施 + `eae9d213e` T08 旋转候选核实/EAA 提案），合并后 HEAD `3a61e300f`。增量核对基准：`reuse-t01` = `eae9d213e`。

## §1 三档核对口径与 R1 判定修正

三档定义（脚本 `coverage-sweep-r2/r2-gap-tiers.mjs`，结果 `r2-gap-tiers.tsv`）：

- **A 字符串命中**：矩阵行文本含文件/符号子串。**只证明登记提及，不证明行为已审**。
- **B 语义覆盖**：符号的实际定义行号落在该文件某个已登记 `sources`/`auditedSections` 区段 `[startLine,endLine]` 内。这是"已登记"的判定档。
- **C 独验关闭**：acceptance 归档以 **functionId**（非文件名）引用且结论为 PASS。文件名命中无区分度，本档一律按 functionId 判定；本轮开放项的 C 档均为"无"。

**R1 判定修正（2 处）**：

| 项 | R1 判定 | R2 修正 | 依据 |
| --- | --- | --- | --- |
| COV-07 `putU32` | 符号 MISS（开放） | **B 档语义覆盖，关闭** | `execution.ChannelProtocol.putU16` 区段 22–63 为字节写入函数族区段，覆盖 `putU32` 定义行 28；族粒度登记语义成立 |
| 旋转链 `RpyMath.hpp` | （R1 未单列，但按 A 档脚本会误判） | **B 档已覆盖** | `modeling.rpyToRotation`(47–64)、`modeling.rpyToEaa`(74–93) 在 c27349a80 已登记（非主会话新增），经 T08 反例独验 |

**定义行精确化**（修正 R1/二报的引用行混淆）：`ObjectStore::scanObjects` 定义 :441（:441–591 区间同二报）；`WorkerSupervisor::handleHandshake` 定义 :999（:852 为调用处）；`TaskScheduler::dispatchOne` 定义 :538。三者均为 **A 档（文件已登记、该符号区段不相交）**，开放状态不变。

**36 项已知遗漏 @ `eae9d213e` 重跑**：开放集合与 R1 完全一致（10 项：FCOV-09/11/12/13/17/18/20/S1/S2/S3）——主会话增量（T07/T08 提交）未关闭其中任何一项。主会话增量对覆盖的实际影响：T07 已把 CouplingMath 实施落地（CouplingMath.hpp 重写 + CouplingReuseTest.cpp 新增 + CMake 接线），其消费链登记（CS-01 提示）仍未见 modeling.json 增量。

## §2 统计修正（可重算）

1. **需求统计修正**：R1 摘要中"116 个单元功能需求 ID 零引用"**有误**，正确数为 **81**。可重算口径：REQUIREMENTS.md 表格内 ID，限定单元功能需求前缀（MDL/REQ/KIN/TRJ/DYN/SEL/OPT/RPT/PM/UX/EVI/CON/ARC/IO/TASK/TK/ERR/CORE/HOST/GOV/SA/H1–H6），剔除 NFR-*（非逐功能登记对象）、AT-*（验收标准编号）、WP-*（工作包编号）、P-*/PILOT/DEL。功能需求定义 141 个，矩阵 1,105 行 requirements 字段引用后未覆盖 **81 个**：MDL×12、PM×16、UX×11、OPT×9、REQ×9、KIN×5、SEL×5、RPT×5、DYN×3、TRJ×2、EVI×2、ARC×1（ARC-01）、CON×1（CON-03）。分项明细与 R1 §4 一致，仅总数修正。
2. **文件统计口径**：R1 枚举 1,752 个"产品文件"含 `AGENTS.md`、`patches/`、`share/`、根 `CMakeLists.txt` 等非实现文件；实现候选口径维持"src 非测试 .cpp ＋含实现特征 .hpp ＋ mjs/ps1/cmake"，零引用实现候选 **938** 不变（R1 的 `uncovered-files.tsv` 抽查无同名误报，RpyMath.hpp 未被误列）。该数是候选入口计数，非缺口结论。
3. 重算命令随证据目录归档（`coverage-sweep-r2/`），全部只读。

## §3 旋转/位姿链（迁移顺序第 1 位）：具体遗漏与上游适用证据

**主会话已覆盖（本轮不重复）**：`modeling.rpyToRotation`/`modeling.rpyToEaa` 已登记并经 T08 反例独验；上游 `RPY(yaw,pitch,roll)` 与产品 `(roll,pitch,yaw)` 字段换序已固定；GAP-EAA-01 提案（上游 `EAA::angleAxis` acos 小角损失、`EAA::axis` theta<1e-6 归零、`Quaternion::ln` 零/极小角非有限）已登记待所有者确认。

**新增遗漏 ROT-02：`runtime/src/BaseWorldTransform.cpp`（530 行）文件内 12 个函数零区段**。该文件已登记 5 区段：`multiplyRotation`(69–83)、`rotateVector`(86–93)、`rotateTransposed`(97–104)、`rotationFromCustomEaa`(207–244，B9 反例独验)、`composeWorldBaseTcp`(477–490)。实读确认未登记定义：

| 符号 | 行 | 行为 | 上游对应（catalog KIN-FK/MATH-ROT 域） |
| --- | --- | --- | --- |
| `makeRotation`/`identityRotation` | :54/:63 | 旋转构造小辅助 | `Rotation3D` 构造族，可按语义成组 |
| `determinant` | :107 | 旋转阵行列式 | 上游无同名直替；数值护栏业务面 |
| `orthogonalityDeviation` | :115 | 正交性偏差度量 | 对照 `Rotation3D::equal`（Rotation3D.hpp:269，容差比较语义）——差异：产品输出连续偏差量而非布尔 |
| `maxRotationDeviation`/`maxPositionDeviation` | :133/:147 | 旋转/位置最大偏差 | 同上；`MetricUtil`/`MetricFactory` 为距离度量候选（MATH-METRIC），业务阈值三态不同 |
| `installationPresetRotation` | :171 | 安装预设旋转词表→R | 产品词表业务面（NONE 初判）；各 preset 的 R 本体可对照 RPY 构造 |
| `checkWorldBaseTransform` | :305 | 变换有限性/正交校验 | 业务校验面；原语对照 `Rotation3D::equal` |
| `checkPresetConsistency` | :353 | 预设一致性三态校验 | 业务面 |
| `invertWorldBase` | :420 | 世界↔基变换求逆 | **`Transform3D::inverse`/`Rotation3D::inverse`（Rotation3D.hpp:528 引用）直接对应** |
| `transformBasePointToWorld`/`transformWorldPointToBase` | :446/:455 | 双向系变换 | **上游 `Kinematics.hpp:62–63` `baseTframe = inverse(worldTframe(from))*worldTframe(to)` 同构**；差异：产品走受控视图而非 State 直读 |
| `gravityToBase` | :467 | 重力向量系变换（动力学供给） | `R*g` 平凡乘法；消费方为动力学重力设置（DYN 域） |
| `checkBaseMountConsistency` | :496 | 基座安装一致性 | 业务面 |

**上游适用证据摘要**（上游基点仍为 catalog 锁定 `b12e8503`，本轮只读源码取证）：求逆与系变换的核心数学上游已直接提供（`Transform3D::inverse`、`Rotation3D::inverse`、`Kinematics.hpp` worldTframe/baseTframe 组合）；偏差度量上游为布尔 `equal`，产品为连续偏差+三态诊断，属适配差异而非缺口。**建议**：这些未登记函数与已登记 5 区段同文件同职责，若 T08 后续进入 runtime EAA→R 迁移（GAP-EAA-01 第 2 项影响面），应同批补登记并按 policy §1.1 判定 direct/adapter 边界；本会话不代为登记。

## §4 runtime→FK/Jacobian 链（第 2–3 位）具体遗漏

- **`kinematics/include/sdurws/ird/kinematics/Fk.hpp` 零登记**：KIN-01 载体——`PoseMetrics` 值模型（tcpInBase/tcpInWorld 双系位姿）、`IFkEvaluator::evaluate`（§9.2 七个必需接口之首）、`encodePoseMetricsCanonical`。KIN-01 需求已被矩阵其他行引用，但该头与其接口实现未登记——**需求引用与功能登记不对称**。
- **实现者零登记**：`IFkEvaluator` 产品实现集中于 `kinematics/plugin/KinPanelModel.cpp`（零登记）等 plugin 面；`Evaluators.cpp::solveItem`(:1285，调 `IIkSolver`)、`Workspace.cpp::generateJointSpaceConfigurations`(:216)、`PoseReach.cpp::computePoseReachCoverage`(:364) 均零登记（R1 CS-03 维持，本档补精确定义行）。
- **runtime 供给面零登记**：`CompilerImpl.cpp`（845 行，十段编译链事务、`buildCanonicalModel` 分段入口）、`DynamicWorkCellCompiler.cpp`、`CacheKey.cpp` 零登记（R1 CS-02 维持）。FK 端口（`IKinRuntimeView`）的权威关节向量与 worldTbase 映射由该链供给。
- **上游证据**：FK 数学本体即 `Kinematics.hpp:53 worldTframe`（KIN-FK 能力，catalog 已有）；产品增量是 PoseMetrics 业务指标集与 canonical 编码。Jacobian 侧上游候选（`JacobianUtil`/`Device::baseJframe`）在 catalog KIN-FK 检索命令内，本轮未逐符号取证，留待该能力包准入时按 policy §1.1 实证。

## §5 相关 oracle 与构建接线核对

- **新发现 ORC-01**：FK 黄金数据集 **`kin-fk-analytic` 全链零登记**——生成器具名函数 `refFk`(:81)、`jacobiEigen`(:130)、`singularValues`(:162)、`planar2rInverse`(:206)（索引见二报 mjs 表）在全部 functions/*.json 零命中，且**不在两次 coverage 已知遗漏清单内**（FCOV-18 仅覆盖 `rt-fk-equation::closedFormFk`）。含 Jacobian 特征值 oracle 的解析参考未审，直接影响未来 FK/Jacobian 能力包的独立对照可信度。`rt-fk-equation::closedFormFk` 维持 FCOV-18 开放。
- **消费/登记不对称**：两数据集的**消费侧已登记**——`kinematics/test/GoldenKinTest.cpp`（V-01 FK 解析对照，AT-03 组）与 `runtime/test/GoldenDataLintTest.cpp`（RT-T12 数据集 lint）均在矩阵；但被消费的**生成器 oracle 与数据集身份零登记**。
- **构建接线**（限 kinematics/runtime，未扩大通用治理深审）：库目标 `sdurws_ird_kinematics`(+`_plugin`)、`sdurws_ird_runtime`；测试目标 `sdurws_ird_kinematics_test`/`_gui_test`/`_contract_test`、`sdurws_ird_runtime_test`，均经 `ird_add_gtest` 宏（宏消费者已由 B10 登记）。两单元 CMakeLists 仅 inputs 摘要绑定、无功能登记——FCOV-17 范畴结论维持，本轮不重复深审 `ird_gates.cmake`。

## §6 阻断判定（增量更新）

- **对 REUSE-T07（已实施提交 `8e4a08ac8`）：无阻断发现**。CS-01（消费链 Parts.cpp/CommandHandlers.cpp/ModelingPanelWidget.cpp 零登记）在其冻结独验前仍待主会话处理，判定不变。
- **对 REUSE-T08（旋转/位姿，planned）**：ROT-02 与 §4/§5 发现**不阻断**其现有契约（T08 allowedFiles 仅 modeling.json+提案文档）；但若后续扩大到 runtime EAA→R 迁移或 FK/Jacobian 能力包，ROT-02 的 12 个未登记函数与 ORC-01 的 `kin-fk-analytic` oracle 是该能力包 policy §1.1 独验的准备面，建议届时一并补登记。
- 开放已知遗漏（§1 的 10 项）均与当前两项能力无真实依赖，维持"排入后续"判定。

## §7 限制

1. 三档核对中 B 档的符号定义行匹配为文本级正则，个别带限定符/模板符号的 defLine 需人工复核（已对关键项人工确认：scanObjects/handleHandshake/dispatchOne/putU32/rpyToEaa）。
2. C 档为 acceptance 归档文本的 functionId 浅层引用，最终独验状态以验收原件为准；本轮开放项 C 档均为无引用。
3. 本轮全程只读静态取证；未构建/执行任何产品或探测；主会话在途（未提交）改动仍未消费。
4. 上游证据只覆盖本轮点名符号（Transform3D/Rotation3D inverse、Kinematics worldTframe/baseTframe、Rotation3D::equal）；Jacobian 侧与全部 12 个 ROT-02 函数的逐符号适用性不在本轮结论内。
