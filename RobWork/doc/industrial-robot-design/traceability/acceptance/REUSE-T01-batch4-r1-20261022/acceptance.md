# REUSE-T01 第四批 pending 登记独立验收

**结论：FAIL（1项P2需求语义误绑）。** 仅验收新增26条pending关系及候选登记，不评价T01完成，也不批准迁移。源码符号/命名空间/摘要及搜索重放均通过；task/docs/reuse均真实通过。失败不是产品数值测试失败。

冻结提交：`c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5`；比较基点：`f6ee77c91c36f40a4cd573a24335405afc06f68b`。隔离检出：本目录 `repo/`（shared clone、detached HEAD）。未改父工作树，不提交/推送，不派子代理。源码/上游/vcpkg在本批diff中零变更。初次checkout日志发现Windows长路径导致11个历史证据文件缺失，已在此隔离clone启用local core.longpaths并restore；最终status-final.log为空，docs在修复后重跑PASS。初次日志原样保留。

## 必须修正

### [P2] 连杆占位几何被错误绑定到DH/显式参数化需求

`RobWork/doc/industrial-robot-design/reuse/functions/modeling.json` 中新增 `modeling.makeLinkPlaceholderCylinder` 仅声明 `requirements=["MDL-02"]`。MDL-02的权威语义是StandardDH/显式Joint+Origin+Axis双参数化及模式互斥（REQUIREMENTS.md:211）；它不是占位几何需求。该功能在modeling/src/Template.cpp:1097-1178计算连杆线段中点和圆柱Z轴对齐、返回视觉几何引用；头文件Template.hpp:870+明确是占位几何。

已有直接权威映射：`units/modeling.md:220`，“连杆几何 Drawables＋自动连杆圆柱（autoLink/regenerateLinkHelpers/computeLinkPose）” → **MDL-04、AT-01**；`:517`定义“生成连杆占位几何”辅助。REQUIREMENTS.md:213的MDL-04为视觉/碰撞几何等配置。请按此真实语义修正本行需求绑定；不修改需求原文或产品。原件检索记录见 `requirement-evidence.log`。本项违反逐功能矩阵需求驱动追溯要求，不能以MDL-02存在且门禁通过替代语义绑定。

## 本批已核实通过的事实

- 新增26条，7单元：dynamics2、modeling2、optimization4、requirements9、runtime4、selection1、trajectory4；均decision/status=pending。累计121条，closedFiles=0，未扩大权限或关闭库存。
- 26个sources区段、26个CMake inputs及1个auditedSections的规范化SHA全部一致（独立计算，53项）；真实函数起止、lambda闭合和命名空间逐项核对，详见 `bindings.json`、`source-excerpts.txt`、`new-rows.json`。
- sources中的真实目标在绑定CMake中登记，文件名在对应源注册处出现；门禁也核对目标声明。未编译候选或产品，不把“文件名出现”当真实构建通过。
- 模型所有权仍是runtime，非runtime数学值消费按adapter待审；没有HOST例外新增、worker借持句柄扩权或跨单元私有头修改。

## 数值和候选语义核查

| 本批功能 | 核查结果与边界 |
| --- | --- |
| dynamics.evalAcceleration | `ForwardDynamics.cpp:509-567`摘要正确；509是qdd输出缓存，lambda真实声明510、闭合567，完整包含h提取、M各列、rhs、线性求解与失败返回。qualifiedSymbol用外层validate内lambda说明性定位，没有伪造独立自由函数。DYN-05为正动力学一致性检查；没有把多次RNE直接称完整FD复用。 |
| dynamics.ForwardDynamicsValidator.validate.rk4Stages | sources真实外层函数251-874且摘要正确；auditedSections真实612-692含四stage及加权推进，独立摘要正确。differences与auditBoundary明确只审内部积分功能，未冒充整个validate已审。693+异常检测、后续统计等没有因此获得完整语义审计结论。 |
| modeling.rotationToRpy | `PanelModel.cpp:72-81`匿名命名空间属实；矩阵至roll/pitch/yaw呈现。候选上游RPY确有入口；冒烟外联不足不作为重复公式豁免。MDL-02与参数位姿显示存在上下文关联，本轮不把该行列为需求误绑。 |
| modeling.makeLinkPlaceholderCylinder | 函数/源码/目标/摘要真实，Rodrigues、反Z固定X半周及中点差异真实；**需求绑定失败如上**。 |
| optimization.nextU64/nextUnit/makeLayerPermutation | JointStrategy.cpp匿名SplitMix64成员及Fisher-Yates实际存在，区段正确。OPT-06含确定性随机种子；全局上游Random与任务私有64位流不等价，记录保留pending合理。不是因标准库随机ID名称误报。 |
| optimization.generateStageDJointCandidates | 704-909真实公共函数；业务组合规范化/轮转、授权变量筛选与内部LHS采样分开描述，没有整函数认定business-specific。与前三小核实际调用关系成立。 |
| requirements.reflectPosition/rotationFromRpy/reflectionMatrix/matMul/reflectRpy | 219-330各自独立真实区段；REQ-11镜像需求匹配。M为det=-1反射、MRM为业务共轭，不将M塞入proper Rotation3D。ZYX展开上游候选需按实现换序。 |
| requirements.targetEditRotation/targetEditRpy/finiteTargetEditPose/rotationToRpyPresentation | 真实匿名函数，REQ-10捕获/三维交互位姿上下文成立。正解/反解、有限性、SO3检查、1e-8姿态容差及接近万向锁的1e-8rad偏离分别保留；没有宣称与上游默认阈值等价。 |
| runtime.symmetrizedSpdWitness/relativeAsymmetry | inertia_validation_detail真实具名命名空间，MDL-06对称SPD校验语义正确，DescriptionValidator及CanonicalModel共用。上游对称谱与已依赖Eigen作为候选；严格>0、次正规/混合尺度/FMA不被假称已验证。 |
| runtime.axisAlignmentRotation/spanPlacement | 真实匿名函数与源区段；对齐普通分支调用rotationFromCustomEaa，后者已登记，非本函数内重复一份完整Rodrigues。差异“自持Rodrigues”应按整条调用理解，不能据此声称该函数直接内联Rodrigues公式。反Z固定X旋转与EAA(from,to)的反轴选择差异真实，m与局部Z语义真实。MDL-12是导入背景，后续完整审计可补强MDL-11任意轴、MDL-04几何直接追溯；本轮不把它判成已闭合。 |
| selection.symmetricEigenvalues3x3 | 匿名函数、闭式求谱与排序属实；checkSynthesisAssertions消费；MDL-06/SEL-10合成物性语义正确。 |
| trajectory.rotationAngleBetween | 匿名函数，实际用上游Quaternion后再规范化求最短角；EAA::angle存在，0/小角/pi差异保持待测，TRJ-02/06上下文有效。 |
| trajectory.canDropPoint | 378-409真实双域中点偏差判据；PATH-SIMPLIFY只列候选，没有把PlannerConstraint边可连性等价于点偏差。必经点保护在smooth调用者中，未宣称其在canDropPoint内部。 |
| trajectory.buildTangents/measureDeviation | 匿名真实符号、427-447与466-503摘要正确。固定64探针、端导/收缩、FK端口值消费真实；TRJ-04语义合理。候选三次Spline不能冒充五次几何，本批明确保留差异。 |

### PATH-SIMPLIFY目标和候选

- `RobWork/RobWork/src/rwlibs/pathoptimization/CMakeLists.txt:1`给出`SUBSYS_NAME=sdurw_pathoptimization`，`:20`调用rw_add_library，`:14-18`注册PathLengthOptimizer.cpp/.hpp。本批target/targetFile准确，非猜测的rwlibs_pathoptimization。
- 真实命名空间`rwlibs::pathoptimization`；PathLengthOptimizer.hpp:91的`pathPruning(const QPath&) const`存在。实现.cpp:83的QList版本检查validPath、剔点并回退，:283的公共QPath版本转调；随机shortCut/partialShortCut不等价确定性规则。
- catalog新能力公共头、CMake及gtest摘要全部匹配，版本仍candidate。未编译和运行该候选，上游适用性仍pending。构造器持PlannerConstraint/metric，后续需继续核错误/取消/生命周期；当前描述没有假称授权业务创建模型。

### 其余上游候选的事实检查

- RPY.hpp声明看似roll/pitch/yaw，但RPY.cpp:74实际a/b/c分别用于Z/Y/X，生产roll/pitch/yaw需换序；本批rotationFromRpy明确写`RPY(yaw,pitch,roll)`，正确识别这一版本实现差异。反解epsilon默认1e-5，与产品1e-12分支不同，不得只换构造器。
- Random.cpp:35匿名全局boost::mt19937，:57 seed(unsigned)；真实是全局流/unsigned种子，不等价任务私有SplitMix64流，差异记录诚实。
- LinearAlgebra.hpp:227的eigenDecompositionSymmetric调用SelfAdjointEigenSolver，目标sdurw_math；不是上游没有分解功能。
- RK4Integrator.hpp面向RigidBody*及State，rwsim/CMakeLists.txt未注册该RK4文件（有BodyIntegrator/EulerIntegrator）；本批没有声称它是已导出的通用关节ODE求解器。DYN-FD的PhysicsEngine真实step/initPhysics/addDevice及ODE候选需后续力控制/冻结语义实验，本轮不将候选搜索当生产通过。

## 真实执行

| 验证 | 结果 | 原件 |
| --- | --- | --- |
| pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json | PASS / exit0 | task.log、task.exit.txt |
| pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 | PASS / exit0，长路径修复后重跑也PASS；DeepEvidence NOT RUN | docs.log、docs.exit.txt、docs-after-restore.log/.exit.txt |
| pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -Head c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json | PASS governance only / exit0；DEBT16、UNRESOLVED1399 | reuse.log、reuse.exit.txt |
| batch4-search.json新增5命令原样重放 | 5 PASS / 0 FAIL，stderr为空，规范化路径分隔符后stdout集合完全一致 | search-replay.json、search-01..05.stdout.txt/.stderr.txt/.exit.txt |
| 独立摘要核验 | 53 PASS / 0 FAIL；PATH-SIMPLIFY公共头/目标/gtest另3摘要PASS | check.ps1、bindings.json、catalog-bindings.json、facts.json |
| git diff --check base..freeze | FAIL / exit2，仅新增保存的上一批验收diff.log:4内历史差异原文含行尾空格；原始证据不改写，此项非新增产品缺陷 | diff-check.log、diff-check.exit.txt |
| 产品/上游/vcpkg变更核查 | 无变更 | scope.log、scope.exit.txt |
| C++/GUI/数值生产验证、远程CI | NOT RUN：本批无产品改动，pending登记不提供生产迁移结项证据 | 本报告 |

`auditedSections`是本批补充定位字段，当前reuse-contract.ps1并不独立校验它；本验收已手动重算其摘要。sources外层大区段使机器covered表包含整函数，但当前decision/status=pending不会成为迁移结项通过；未来闭合前仍须对其余内部功能逐项实审，不能沿用本报告冒充全函数覆盖。

所有统计均为本批pending登记的结构/语义验收，非20单元覆盖率。第一轮覆盖报告的回放插值、序列化、宿主图形小核、测试oracle登记等仍在本批范围外，不要求本批顺带关闭，也不因本批新增而消失。父树未写产品；隔离clone只为恢复正确checkout而restore冻结文件。补充记录脚本最初两次PowerShell语法错误后修正重跑，失败不被计入通过；最终supplement.ps1执行结果单独保留。
