# REUSE-T01 只读遗漏验收

结论：**全功能审计退出 NOT PASS；本轮已发现确定的审计覆盖遗漏，不判现有95条记录错误通过，也不判产品数值实现已错误。** 当前95条全部pending是诚实状态；不能以每单元已有少量记录结束T01。本报告不授权迁移、不填写矩阵、不扩大权限。

## 冻结与限制

- 工作树：C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork，分支 reuse-t01。
- 开始HEAD：ce1af19667e6e3e439e15600e5bed7940b171942，第三批18关系当时未提交；审查期间父会话提交导致HEAD漂移。
- 按父会话要求，最终证据重新核实于 **4e142e485343e8bade52a8481eefbb447b017989**：22个发现/上游路径读取git blob并确认工作文件与冻结版本相同，冻结矩阵总95条。详见 frozen-facts.json、review.ps1；不存在用浮动文件冒充单一冻结验收。
- 未改父工作树、产品、上游、vcpkg，未提交/推送/合入，未创建子代理。临时报告保存在系统TEMP。未执行构建、GUI、产品测试或治理门禁，均为 NOT RUN（只读范围验收）。
- 全20单元做文件枚举与数值/编解码指纹检索；命中后定向阅读下面的功能。不声称对1576个产品文件逐函数全审，也不把无关键词命中当无算法。脚本、页面、测试和CMake只做定位/抽查，仍有未覆盖内容。

## 最重要的具体遗漏（按优先级）

### 1. runtime惯量SPD是未登记的自持矩阵分解核

- 产品：runtime/src/CanonicalModel.cpp:85，sdurws::ird::runtime::inertia_validation_detail::symmetrizedSpdWitness；:122-133固定顺序LDLᵀ/Schur消元，含每轴二进制平衡及FMA补偿。:394由惯量合法性校验消费。runtime/src/DescriptionValidator.cpp:436也消费同一符号，不是第二份实现。
- 缺漏事实：runtime矩阵7条没有该符号；multiplyRotation/compileWorkCell等区段不覆盖它。
- 上游候选：RobWork/RobWork/src/rw/math/LinearAlgebra.hpp:227 eigenDecompositionSymmetric，:229使用Eigen::SelfAdjointEigenSolver，目标sdurw_math；可比较严格SPD见证与特征值方案。不能未经极端尺度、次正规、近奇异oracle比较就宣称可直接替换；不能把复用Eigen本身判为重复实现。
- 影响：模型合法性入口的数值拒绝边界属于高风险核心，遗漏会让runtime审计在保留自持分解时提前退出。owner仍为runtime，不建议业务自行持模型。
- 实际检索：`rg -n '(symmetrizedSpdWitness|relativeAsymmetry)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/CanonicalModel.cpp RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/DescriptionValidator.cpp`；`rg -n '(eigenDecomposition|SelfAdjointEigenSolver)' RobWork/RobWork/src/rw/math/LinearAlgebra.hpp RobWork/RobWork/src/rw/math/LinearAlgebra.cpp`。

### 2. selection惯量综合另有闭式特征值核

- 产品：selection/src/Backfill.cpp:80 symmetricEigenvalues3x3，:101-105三角闭式求谱；:411 checkSynthesisAssertions在:424调用，关系到惯量断言。不是仅文件命名猜测。
- 缺漏事实：selection只有LinearCurveEvaluator::evaluate；modeling同名特征值矩阵行指向modeling/src/InertiaMath.hpp，不覆盖selection本符号。
- 上游候选：LinearAlgebra.hpp:227 eigenDecompositionSymmetric；实现在LinearAlgebra.cpp:84，测试RobWork/RobWork/gtest/math/LinearAlgebraTest.cpp:48，sdurw_math。
- 影响：惯量综合审计不能只补modeling而漏selection，需比较排序、近重根、容差、非有限错误语义。当前代码是否数值错误未在本轮测试。
- 实际检索：`rg -n '(symmetricEigenvalues3x3|check.*Inertia|validate.*Inertia)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp`；后续读:411-424确认消费者；上游检索同#1。

### 3. requirements生产镜像及多个交互入口自持姿态转换

- 产品：requirements/src/TemplateArray.cpp:230 rotationFromRpy、:256 matMul、:289 reflectRpy，:681生成派生需求消费reflectRpy。:301-328包含欧拉提取/万向锁分支。requirements/plugin/RequirementsPanelWidget.cpp:72 targetEditRotation、:80 targetEditRpy，:1378/:1392在交互编辑生产路径消费；requirements/plugin/RequirementsCommandFlows.cpp:75 rotationToRpyPresentation、:799消费。
- 另定位 modeling/plugin/PanelModel.cpp:72 rotationToRpy，:89消费；ui/plugin/UiPlugin.cpp:5035-5037还有欧拉提取lambda（该处仅定位，未追完消费者，不将其作为已审闭环）。
- 缺漏事实：requirements仅SamplingPlanBuilder两关系；modeling已登记src/RpyMath.hpp正向转换，不覆盖plugin/PanelModel反解；ui两条宿主关系也不覆盖上述lambda。
- 上游候选：RobWork/RobWork/src/rw/math/RPY.hpp:68 从Rotation3D构造、:73 toRotation3D；实现RPY.cpp:28/:74，sdurw_math。普通旋转乘法候选Rotation3D.hpp:317；反射矩阵M为det=-1，不能随意当Rotation3D传入，业务镜像共轭规则本身也不是机器人FK。
- 影响：要求把“业务镜像规则”和通用欧拉/矩阵运算分开审，比较欧拉非唯一性、gimbal阈值、rad/表达顺序与确定性。呈现操作例外不豁免数学复用，也不能由此获得模型写权。
- 实际检索：`rg -n '(rotationFromRpy|reflectRpy|matMul)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/src/TemplateArray.cpp`；`rg -n '(targetEditRotation|targetEditRpy)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp`；`rg -n '(RPY|toRotation3D|operator\*)' RobWork/RobWork/src/rw/math/RPY.hpp RobWork/RobWork/src/rw/math/RPY.cpp RobWork/RobWork/src/rw/math/Rotation3D.hpp`。

### 4. dynamics正向动力学未审加速度右端、质量阵提取和RK4

- 产品：dynamics/src/ForwardDynamics.cpp:251 ForwardDynamicsValidator::validate；:509 evalAcceleration lambda提取h及M各列；:612-692自持四stage RK4及推进。现矩阵仅登记该文件:170-225 solveLinearSystem，不能把同文件/大入口视为内部功能闭合。
- 上游候选：RobWorkSim/src/rwsim/simulator/PhysicsEngine.hpp及其step/initPhysics接口（DYN-FD已有候选）；RobWorkSim/src/rwsim/rwphysics/RK4Integrator.hpp:21/:24/:28/:30是RigidBody*+State接口，**不是可直接消费关节ODE的公共泛型积分器**。实际检索rwsim/CMakeLists.txt未命中RK4Integrator注册，不能只因文件存在就称已导出可用。sdurwsim目标及配置适用性仍需核实。
- 影响：DYN-05数值核心和固定采样/事件语义尚未有功能决策，上游接口差异要保留pending/proposal与缺口证据，不能本轮直接替换或宣布无能力。
- 实际检索：`rg -n '(RK4|ForwardDynamics.*::|stage)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp`；`rg --files RobWork/RobWorkSim | rg '(RK4|Integrator|PhysicsEngine)'`；`rg -n '(rk4|RK4|rwphysics)' RobWork/RobWorkSim/src/rwsim/CMakeLists.txt`；实际阅读RK4Integrator.hpp全体接口。

### 5. trajectory只有Hermite基函数登记，路径剔点/切线与偏差探针尚未审

- 产品：trajectory/src/Smooth.cpp:378 canDropPoint（关节中点及FK双域偏差），:429 buildTangents（中心差分+lambda收缩），:466 measureDeviation（每段64点），:511 smooth在:544消费canDropPoint并删除/回溯。矩阵中的四个hermite基函数只覆盖各自区段。
- 上游候选：RobWork/RobWork/src/rwlibs/pathoptimization/pathlength/PathLengthOptimizer.hpp:91 pathPruning（i/i+2可连则剔除中点）；目标rwlibs_pathoptimization归属尚未完整核实。本轮不称直接等价：产品必经点、双容差、确定性回溯、取消与约束语义不同，需排除/适配理由。CubicSpline不自动覆盖五次Hermite。
- 影响：TRJ-04路径处理算法仍未审；已有样条候选不足以覆盖整条smooth语义。
- 实际检索：`rg -n '(PathSmoother|path.*prun|prun|PartialShortcut|Shortcut)' RobWork/RobWork/src/rwlibs/pathoptimization -g '*.{cpp,hpp}'`；定向阅读Smooth.cpp:378-455/:525-570和PathLengthOptimizer.hpp:45-120。

## 次级覆盖缺口（已定位，不把它们直接定罪）

- dynamics/src/Replay.cpp:230 DynamicsReplayProjector::sampleAt，:299/:335-341五个量的线性显示插值未登记。候选rw::trajectory::LinearInterpolator<double>在LinearInterpolator.hpp:51/:70。std::upper_bound/lower_bound是合法std查找；按关节匹配、非Ok样本过滤属业务逻辑，不应整段称平行运动学。命令：`rg -n '(interpol|alpha|lower_bound)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/Replay.cpp`，并读:230-345。
- ui/plugin/HostView3DPreviewBackend.cpp:253 SampleGridRender::draw，:324-340自绘三平面24段高亮环，未由ensureSessionPreviewAnchor的:471-504区段覆盖。候选rwlibs/opengl/RenderLines.hpp:37/:66、RenderGeometry.hpp:35。仅宿主呈现：未证实是违规核心或必须替换；透明、颜色、线宽及宿主例外精确范围待审。不能把HOST授权扩大到worker或模型构造。
- drivetrain/src/DriveTrainCodec.cpp:47 ByteWriter、:95 ByteReader、:413 encodeDriveTrainModel，selection/src/Backfill.cpp:114/:186同类编解码；policy/src/PolicyInput.cpp:75/:140也有小核，均无对应矩阵行。候选rw/common/BINArchive.hpp:36-38、Serializable.hpp:52/:64，目标sdurw_common。产品冻结小端/IEEE754/ID/摘要/字段顺序协议不等于BINArchive格式，不能凭同为序列化要求替换。实际命令：`rg -n '(class ByteWriter|class ByteReader)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot -g '*.cpp'`；`rg -n '(BinaryArchive|BinaryOutputArchive|BinaryInputArchive|class.*Archive)' RobWork/RobWork/src/rw/common RobWork/RobWork/src/rw/core -g '*.{hpp,cpp}'`。
- kinematics/test/KinFkFixture.hpp:380 rodrigues/:408 mul4/:429 referenceFk是明确标注独立FK/Jacobian oracle，FkTest.cpp:154/:198及IkTest.cpp使用。测试独立oracle按audit-contract §6可合理保留，**不判平行生产核心**；但全功能审计退出时仍应登记测试用途、不可生产复用边界和上游对照，现矩阵无该测试符号。实际命令：`rg -n '(referenceFk\(|KinFkFixture)' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/test -g '*.cpp'`。

## 已排除的误报及未覆盖面

- diagnostics::findingIdEngine、reporting/core/project等身份随机值的std::mt19937_64用法，不因random关键词认定采样核心。optimization::SplitMix64确为自持，但generateSeededLhsCandidates已有pending并在differences提到SplitMix64；它的辅助区段覆盖是否充分需后续审，不重复宣称“完全未登记”。
- Selection PreferenceFilter是企业偏好筛选，非信号滤波；未从名字推定存在低通算法。本轮数值滤波搜索没有形成明确生产低通核发现，不代表全库无滤波。
- 原有pending FK/IK/RNE/SVD、模型适配与现有std容器/算法不重新报同一遗漏。本轮未对全部权限grant或direct/adapter正向证据作验收，也没有把diagnostics权威、evidence聚合规则、worker进程边界判为已合规。
- 20单元记录数（均pending）：core1, diagnostics3, drivetrain2, dynamics4, evidence1, execution2, io2, kinematics21, modeling24, optimization1, policy2, project2, reporting1, requirements2, runtime7, selection1, testkit2, trajectory14, ui2, workflow1。
- 数量只是定位事实，不是覆盖率。测试/契约/GUI/perf目录、完整CMake链接、全部脚本、inline/template/macro、页面内代码仍未逐功能全审。脚本检索大量治理/JSON匹配，仅定位没有语义结论；user-manual/index.html脚本关键词搜索未命中，不把这一搜索升级为安全证明。
- 审查中部分定位命令因PowerShell不支持shell花括号扩展、rg显式通配路径或猜测不存在的路径而失败；已用明确路径/目录+glob重查。失败命令不作为无命中证据。本报告引用的发现均来自后续成功检索及实际源阅读。

后续由实施者在冻结版本上继续逐批审计、记录pending/排除理由、必要探针和独立验收；本验收不实施矩阵填补或迁移。
