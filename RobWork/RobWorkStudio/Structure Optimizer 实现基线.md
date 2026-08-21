# Structure Optimizer 实现基线

此记录是重构计划的 Phase 0 入口点。它记录了 2026-08-18 发现的仓库状态，且必须仅使用最新的验证证据进行更新。

## 仓库与受保护工作

* 仓库根目录：`D:/10_Source_Repos/21_robot/RobWork/RobWork`

* 起始版本（Revision）：`5550d10`（`docs: specify hybrid kinematic optimization algorithm`）


* 当前分支：`main`（用户明确要求就地实现）


* 受保护的既有修改：`docs/superpowers/specs/2026-08-17-kinematic-design-space-compiler-design.md`、两个未跟踪的 UR fixture 目录以及两个未跟踪的重构计划文档。



## 现有 StructureOptimizer 接口与表面

* 核心目标（Core target）：`sdurws_structureoptimizer_core`。


* 插件目标（Plugin target）：`sdurws_structureoptimizer`。


* 测试目标（Test target）：`sdurws_structureoptimizer_test`。


* 现有的兼容入口包括 `StructureOptimizationProblem`、`StructureOptimizationResult`、`StructureOptimizationController`、项目适配器、表格模型角色（table-model roles）、预览控制器、JSON 读取器以及旧版评估器接口。


* 现有的 KinematicAnalysis 依赖项已经链接了 `ConfigurationEvaluator`、`TargetEvaluator`、`RegionCoverageEvaluator` 以及需求执行类型。新代码必须复用它们，而不是克隆 IK/FK/碰撞逻辑。



## 构建与测试入口点

* Visual Studio x64 构建辅助脚本：`scripts/build-msvc-debug.cmd`。


* 已配置的 Debug 构建目录：`build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug`。


* 测试可执行文件：`build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_structureoptimizer_test.exe`。


* CTest 注册项：`sdurws_structureoptimizer_test`、`sdurws_structureoptimizer_test_evaluator_consistency` 和 `sdurws_structureoptimizer_test_cache`。


* Widget 测试套件在运行时设置 `QT_QPA_PLATFORM=windows`，一次仅运行一个绝对路径测试可执行文件。纯模型（Model-only）测试套件使用 `QCoreApplication`。



## 重构边界

旧的 `StructureOptimizationProblem` 在引入规范模型阴影（canonical-model shadow）之前仍作为兼容输入保留。初始契约与数学辅助函数仅限核心（core-only），不改变旧版的候选生成、评分或 UI 行为。

## Phase 0 退出证据 (2026-08-18)

* **S03 契约：** `StructureOptimizationContracts.hpp/.cpp` 将候选生命周期与可行性、证据阶段和质量分离开来。它还提供了 `EvaluationCompletion`、稳定的诊断 POD 以及唯一的旧版状态映射投影。


* **S04 数学约定：** `KinematicConventions.hpp/.cpp` 固化了 $\text{SE}(3)$ 关节方程、$q_{\text{model}} = q_{\text{input}} + \text{zeroPositionOffset}$、单位（m/rad）、真旋转校验以及稳定的轴偏转切线坐标。这些辅助函数不依赖任何 Widget 或 WorkCell。


* **S05 冻结需求边界：** `EngineeringRequirementArtifactAdapter` 仅消费冻结的 v4 `artifact.execution` 契约。它会以 `REQ_V3_REQUIRES_REFREEZE` 拒绝用于 Verified 评估的 v3 工件，校验出处/源路径和指纹，并在失败时保持目标问题不变。


* **S07 JSON 安全性：** 非有限数值写入为 JSON `null`；不可用的候选总分额外写入 `totalScoreAvailability: "Unavailable"`。未知的根字段和 `extensions` 能够原样往返保留；未知枚举值会被拒绝。经过回归测试后，合法的 `Continuous` 变量域依然保持受支持状态。


* **验证：** 全新编译的 StructureOptimizer 可执行文件通过了所有默认测试套件（包括 `contracts`、`kinematic_conventions`、`json_safety`、`json_roundtrip`、`frozen_adapter`、`frozen_requirements` 和 `evaluator_consistency`），退出代码为 0，输出 `All tests passed.`。跨插件可执行文件也全部通过：`sdurws_robotanalysiscore_test.exe requirementExecution`、`sdurws_engineeringrequirements_test.exe` 以及 `sdurws_kinematicanalysis_test.exe configuration`。


* **已知非阻断警告：** 在相关断言通过后，完整的 StructureOptimizer 测试套件中会出现可选的 `fixture.stl` 查找警告。


* `git diff --check` 未报告任何空白字符错误。GUI 调用均使用了 Windows 平台插件，且每个命令仅启动一个绝对路径测试可执行文件。



Phase 0 已关闭。Phase 1 仅可添加规范核心模型及其专用测试；不得切换 UI、优化算法或 CandidateCompiler。

## Phase 1 / S10 证据 (2026-08-18)

* 添加了仅限核心的 `CanonicalKinematicModel` POD 及其验证器。该模型分离了 Frame（坐标系）、Joint（关节）、DOF（自由度）、Chain（链）和 TCP 绑定；它存储完整的 `Transform3D` 值和显式轴，而非 DH、RPY 或名义长度。


* 稳定的验证诊断覆盖了重复 Frame ID、固定/可动 DOF 归属、连续唯一的 Q 索引、单位/类型一致性、连通链以及法兰到工具（Flange-to-Tool）绑定。合法 fixture 包含 Revolute（旋转）、Fixed（固定）、Prismatic（移动）和 Tool（工具）元素，且不假设为六轴设备。


* RED 证据：首次测试构建失败，原因是 `CanonicalKinematicModel.hpp` 不存在。随后针对重复可动 `dofId` 的专用 RED 测试如期失败，报出预期的缺失 `KINEMATIC_MOVABLE_JOINT_DOF_DUPLICATE` 诊断。


* GREEN 证据：在完成每个最小实现步骤后，`scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` 构建成功，且绝对路径可执行文件的 `canonical_model` 套件退出代码为 0。相邻的 `kinematic_conventions` 套件退出代码为 0；`frozen_requirements` 也成功完成（带有既有的可选 `fixture.stl` 警告）。


* 新启动的默认 GUI 套件运行并通过了其既有测试，但因超过了交互式工具窗口时间，其实际测试进程被中止；因此未记录为全套件通过。未报告任何 Qt 平台插件错误。



## Phase 1 / S11 证据 (2026-08-18)

* 添加了 `CanonicalForwardKinematics.hpp/.cpp`，这是针对已验证活动设备链的只读评估器。它将每个关节委托给固化的 S04 约定处理，并在 Flange 变换之后应用 ToolBinding；它不截断（clamp）Q 或应用运行限位。


* RED 证据：首次针对性测试构建失败，原因是 FK 头文件缺失。GREEN 证据：最终目标构建成功，绝对路径可执行文件的 `canonical_fk` 套件退出代码为 0。它验证了 q=0 以及带有零偏置的非零 Q、任意固定变换、以米为单位的移动关节轴、TCP 复合、不可用 Frame 诊断以及 Q 维度不匹配拒绝。


* 在 FK 构建后，相邻的 `canonical_model` 和 `kinematic_conventions` 套件退出代码均为 0。



## Phase 1 / S12 证据 (2026-08-18)

* 添加了 `KinematicModelImporter`、`KinematicImportResult`、不可变导入出处以及逐项源映射。唯一接受的垂直输入是显式选定的 WorkCell、SerialDevice 和 TCP；导入器绝不会默认选择第一个设备或 TCP。传入的 `RobotModelSpec` 仅作为审计快照复制，不用于构建规范运动学。


* 导入器保留了从 WorkCell 世界到设备基座的完整固定路径，后接 FixedFrame、Revolute 和 Prismatic 链成员。它按源顺序连续映射活动 DOF，按关节类型分配弧度/米单位，携带有限物理限位，在成功前调用规范验证，且不保留借用的源指针。


* 稳定的导入诊断涵盖缺失或非成员源选择、TCP 不在链中/非末端、断链、重复 ID、不支持的关节或 Frame、无效限位以及基座树故障。每个诊断均携带稳定的代码、源对象 ID 和源字段路径。


* RED 证据：导入器测试套件起初因 `KinematicModelImporter.hpp` 不存在而失败。随后以测试驱动方式添加快照保留契约；其 RED 构建因 `sourceSnapshot` 和结果快照字段不存在而失败。在导入 world-to-base 前缀之前，非零 Base FK 对比也发生了失败。


* GREEN 证据：在完成最终 Debug 目标构建后，绝对路径可执行文件的 `canonical_importer` 套件退出代码为 0。它涵盖显式空选择、多设备无选择拒绝、TCP 链从属关系、无效限位、不支持的关节、源映射/出处、固定坐标系 Q 排除、移动关节米单位以及关键坐标系在 q=0/非零 Q 下的 FK 对比。相邻的 `canonical_model` 和 `canonical_fk` 套件退出代码均为 0。


* Fixture 说明：由于 RobWork `StateStructure` 无法安全构建同名 WorkCell（框架在析构清理期间不会返回），因此重复规范 ID 的行为仍由规范验证器边界覆盖。受支持的 RobWork Revolute/Prismatic 源暴露出固定的非零局部 Z 轴；导入器的零轴防护被保留，以供未来暴露任意轴的源扩展使用。



## Phase 1 / S13 证据 (2026-08-18)

* 导入器/FK 等价性将规范世界坐标系变换直接与 `rw::kinematics::Kinematics::frameTframe` 进行对比，绝不与 RPY 字符串对比。位置和旋转以 `1e-12` 容差的完整 $\text{SE}(3)$ 独立表达对比。


* Fixture 矩阵覆盖了六旋转关节设备、Revolute/Fixed/Prismatic 混合链、非零 WorkCell Base 与 TCP 变换、非零 RPY（包含 pitch）、Pos-Y 偏置以及旋转关节偏置。三组确定性的 6-Q 配置对比了每个关节坐标系和 TCP；混合链在 q=0 和非零 Q 下对比了所有关键坐标系。DOF 顺序和弧度/米单位在导入时均进行了断言验证。


* RED 证据：首次非零 Base 的 world-to-TCP 断言失败，原因是导入器从设备 Base 开始构建其规范链。GREEN 证据：导入器现在会在设备链之前导入 WorkCell 根节点和固定祖先；针对性的 `canonical_importer` 套件退出代码为 0。最终构建后，相邻的 `canonical_model` 和 `canonical_fk` 套件退出代码也为 0。



## Phase 1 / S14 证据 (2026-08-18)

* 添加了 `DhProjection` 作为明确的只读兼容视图。它具有 `Exact`（精确）、`Lossy`（有损）和 `Unsupported`（不支持）状态、行级常规 DH 参数、丢失分量标识符和稳定诊断；不存在转换或回写 API，规范模型也不包含 DH 真值字段。


* 其精确域被有意限制在较窄范围：Z 轴关节运动、父级 Z 旋转/Z 平移以及子级 X 平移/X 旋转。Pitch、横向平移和其他旋转均报告为 Lossy；倾斜的规范运动轴报告为 Unsupported。投影绝不修改源 $\text{SE}(3)$ 数据。


* RED 证据：针对性测试因 `DhProjection.hpp` 不存在而失败。GREEN 证据：在添加核心目标源码后，绝对路径可执行文件的 `dh_projection` 套件在精确、pitch 有损、Pos-Y 有损、轴不支持以及非修改用例下的退出代码均为 0。相邻的 `canonical_model` 和 `canonical_fk` 套件退出代码均为 0。



## Phase 1 / S15 证据 (2026-08-19)

* 添加了 `KinematicFingerprint` 和 `KinematicBaselineSnapshot`。带版本的、与区域设置（locale）无关的规范序列化采用固定的 `fnv1a-64` 算法，并在哈希前拒绝所有非有限数值。它绝不哈希内存地址、指针值、Qt 容器或文件时间戳。


* 指纹域被有意分离：`forModel` 包含规范 Frame/Joint/DOF/Chain 语义；`forTool` 包含 TCP $\text{SE}(3)$ 和视觉几何绑定标识符；`forEnvironment` 包含外部环境指纹和碰撞绑定标识符。仅用于显示的颜色明确置于规范模型之外，不影响任何指纹。快照记录了 Schema、序列化版本、算法 ID 以及所有三个指纹，并附带名义规范模型的副本。


* RED 证据：首次快照测试因头文件/API 不存在而失败；接下来的构建暴露了预期的 `KinematicBaselineSnapshot::create` API 契约。随后的 RED 构建需要独立的 tool/environment 指纹入口点，最后的 RED 构建需要序列化版本出处。


* GREEN 证据：全新 Debug 构建成功，绝对路径可执行文件的 `kinematic_fingerprint`、`canonical_model`、`canonical_fk`、`canonical_importer` 和 `dh_projection` 这组 QCoreApplication 套件退出代码均为 0。指纹套件覆盖了插入顺序无关性、重复调用稳定性、双向 $\text{SE}(3)$ 变换、轴、物理/运行限位、Q 映射、TCP/几何/碰撞分离、NaN/Inf 拒绝、出处以及名义模型快照恢复。


* 评审闭环：规范验证器现在会拒绝空或重复的工具绑定 ID（`KINEMATIC_TOOL_BINDING_ID_DUPLICATE`），这为 tool/environment 序列化器提供了确定性的排序键。最后的 RED 测试还证明了 ToolBinding 中的 NaN 此前逃过了模型和环境哈希检查；现在共享的有限值预检会在生成任何缓存键之前在所有三个指纹域中将其拒绝。



## Phase 1 / S16 证据 (2026-08-19)

* `StructureOptimizationProblem` 现拥有一个可选的 `CanonicalModelShadow`。旧项目保留默认的 `CanonicalModelMissing` 状态，并继续走未改变的候选评估器/评分器路径。该 Shadow 没有设计变量、候选结果或旧版评估器的回写 API。


* 项目 JSON 持久化了完整且经过验证的 `KinematicBaselineSnapshot`：快照出处、所有三个指纹、Frame/Joint/DOF/Chain/Tool 拓扑、限位、轴、有序 Q 映射、绑定 ID 以及完整的 $\text{SE}(3)$ 平移/旋转矩阵。采用稳定的文本枚举值；畸形矩阵、非有限值、未知枚举和无效规范拓扑在加载时均会被拒绝。


* `CanonicalModelShadowService` 仅通过显式的 WorkCell/SerialDevice/TCP 导入器附加快照，并将新导入的源与持久化快照进行对比，标记为 `Current`（最新）、`Stale`（陈旧）或 `Invalid`（无效）。`StructureOptimizationProjectFactory` 和 `StructureOptimizationProjectAdapter` 中的新重载方法以此显式源边界创建和加载项目；常规的旧版重载方法特意保持不导入。


* RED 证据：全模型往返断言最初失败，因为此前仅保存了三个指纹字符串。随后的 Shadow 状态测试因服务头文件/API 缺失而失败；由导入器支持的创建和加载期陈旧性测试在添加对应重载前均在编译期失败。


* GREEN 证据：全新 Debug 构建成功。绝对路径测试可执行文件的 QCoreApplication 套件 `canonical_shadow`、`kinematic_fingerprint`、`canonical_model`、`canonical_fk`、`canonical_importer` 和 `dh_projection` 退出代码均为 0。测试覆盖了完整快照保存/读取/重新生成指纹、导入器创建的项目指纹、旧版项目 `CanonicalModelMissing`、源指纹陈旧性，以及存在 Shadow 时旧版候选分数/状态/违反项的不变性。



Phase 1 已关闭。Phase 2 仅可引入独立的类型化设计空间 POD 以及注册表/绑定工作；在指定的后续迁移门控完成之前，不得切换旧版候选编译器或评估流水线。

## Phase 2 / S20 证据 (2026-08-19)

* 添加了独立且仅限核心的 `DesignVariableDefinition` 和 `ParameterBinding` POD。它们面向编译器的语义、目标对象和目标属性标识均为稳定枚举并带有显式字符串转换；`displayPath` 被排除在运行时相等性判断之外。


* 验证机制会拒绝重复 ID、未知语义、非有限数值、无效的独立范围/步长、缺失离散选项 ID、位姿语义缺失坐标系、无表达式 ID 的派生变量以及无类型化目标对象/属性的绑定。派生变量特意不要求优化范围或步长。


* RED 证据：针对性构建最初因 `DesignVariable.hpp` 缺失而失败。GREEN 证据：在添加两个核心源文件后，全新 Debug 构建以及绝对路径可执行文件的 QCoreApplication `design_variable` 套件退出代码为 0。未修改任何旧版变量表格模型或评估器路径。



## Phase 2 / S21 证据 (2026-08-19)

* 添加了纯 `DesignSpaceRegistry` 和 `AdapterCapabilityQuery`。注册表使用显式的域、单位和适用性元数据注册了完整的第一阶段语义白名单；能力表按类型化目标对象标识建立索引，绝不存储适配器指针。


* 初始建议工厂特意仅实现了受能力门控的 `JointZeroOffset`。即使可动关节的名义值为零，它也会为其创建变量，为 Revolute 分配弧度、为 Prismatic 分配米，并排除 Fixed 关节。几何、基座、TCP、法兰、限位和其他已注册语义在计划的模板和编译器任务完成前仍仅保留为元数据；未更改任何适配器应用路径、UI、WorkCell、旧版表格模型或旧版评估器。


* RED 证据：针对性的 `design_registry` 套件最初失败，因为注册表遗漏了第一阶段白名单的一部分。GREEN 证据：在仅补齐这些元数据注册后，全新 Debug 构建成功，绝对路径可执行文件的 QCoreApplication `design_registry`、`design_variable` 和 `canonical_model` 套件退出代码均为 0。



## Phase 2 / S22 证据 (2026-08-19)

* 保留了现有的 `StructureOptimizationTemplate` 目标权重 API（`balanced`、`reachability-first`、`compactness-first` 和 `workspace-first`），并添加了独立的、带版本的仅限核心的设计意图目录：`KinematicBasic`、`KinematicWithJointAxis`、`KinematicWithBaseTcp` 和 `FullKinematicDesign`。


* 添加了 `DesignTemplateApplication::preview`。它是非修改性的，返回包含 `toAdd`、`alreadyPresent`、`conflicts`、`inapplicable`、`disabled` 和诊断信息的 `TemplateApplicationPreview`。它仅过滤由已声明能力支持的注册表建议，确定性地对新增项排序，保留用户已编辑的既有变量，并将不可用的模板语义报告为 `BindingUnavailable` 而不是凭空捏造绑定。


* 第一个新增的建议扩展支持受能力门控的关节轴 U/V 偏置。它们的零名义/当前值仍然是有效建议；不使用数值非零检查作为可参数化的代理依据。


* RED 证据：初始针对性构建因 `DesignTemplateApplication.hpp` 不存在而失败。GREEN 证据：全新 Debug 构建以及绝对路径可执行文件的 QCoreApplication `design_template`、`design_registry` 和 `design_variable` 套件退出代码为 0。随后使用 `QT_QPA_PLATFORM=windows` 启动了一次完整的 Widget 回归测试；其退出代码为 0，输出 `All tests passed.`。未修改任何 UI 代码、WorkCell、旧版目标预设行为或候选编译器路径。



## Phase 2 / S23 证据 (2026-08-19)

* 添加了带有稳定第一阶段替代方案的 `ParameterizationModeRegistry`，用于连杆放置、关节原点以及 TCP/法兰位姿归属。纯解析器为每个组应用单一显式选择，并将每个未选变量保留为 `DisabledByParameterization`（附带稳定的序列化原因）；它不会静默删除变量。


* 添加了 `WriteSetValidator`。仅激活变量参与验证；每个激活变量都需要已声明的绑定，派生写入者需要已声明的所有者，声明相同类型化对象/属性的两个激活绑定将被拒绝，报 `PARAMETER_WRITE_CONFLICT`。读取集合特意不被视为冲突，适配器执行顺序在解析中不起任何作用。


* RED 证据：针对性构建起初因 `ParameterizationMode.hpp` 和 `WriteSetValidator.hpp` 不存在而失败。测试调用最初使用了非稳定的短模式名称，被拒绝为 `PARAMETERIZATION_SELECTION_INVALID`；测试现使用已注册的稳定 ID `JointOriginMode=AlongAxis`。GREEN 证据：全新 Debug 构建以及绝对路径 QCoreApplication `parameterization` 套件退出代码为 0，随后 `design_variable`、`design_registry` 和 `design_template` 套件退出代码也均为 0。未修改任何 UI、WorkCell 或旧版候选编译器。



## Phase 2 / S24 证据 (2026-08-19)

* 添加了纯粹的、类型化的 `DerivedExpression`/`DependencyGraph` 评估器。它仅接受 Constant、VariableRef、Add、Subtract、Multiply、Divide、Min、Max、Clamp、Norm 以及显式注册的函数；首个注册的函数是一元 `abs`。不存在脚本、UI、WorkCell 或运行时代码注入路径。


* 表达式仅读取传入的已解析值映射和其他表达式。按 Map 排序的 DFS 生成确定性的拓扑顺序。任何故障（重复 ID、缺失引用、直接/间接循环、操作数元数错误、单位不匹配、除以零、不支持的传播或非有限输出）均返回诊断信息和空的派生值集合。


* 单位规则：当乘以或除以 Unitless（无量纲）时保留物理单位；加法、Clamp 和 Norm 输入要求单位匹配；拒绝不支持的组合。`DerivedExpressionTargetValidator` 还拒绝将 `JointLimitLower` 和 `JointLimitUpper` 作为派生目标：它们是编译器需要检查的约束，而不是覆盖表达式。


* RED 证据：初始构建因两个表达式/DAG 头文件缺失而失败；Clamp/Norm 测试在其实现前失败，已注册函数测试在 `registeredFunctionId` 存在前失败。GREEN 证据：全新 Debug 构建以及绝对路径 QCoreApplication `derived_expression` 套件退出代码为 0，随后 `parameterization` 和 `design_template` 套件退出代码也均为 0。未修改旧版候选编译器或 UI。



## Phase 2 / S25 证据 (2026-08-19)

* 添加了纯 `DesignSpaceCompiler` 和 `CompiledDesignSpace` 核心边界。编译器是针对传入规范模型、语义注册表、适配器能力声明、变量、类型化绑定、参数化选择和派生表达式的唯一 Phase 2 预检解析器。它不依赖 Widget、WorkCell 修改、候选生成或旧版评估器。


* 成功的输出包含确定性排序的独立变量与派生变量、激活的类型化绑定、变量组、选定模式、派生依赖顺序、稳定的纯独立变量向量 Schema、禁用原因、诊断以及带版本的内容指纹。指纹包含规范模型指纹、能力声明、向量相关的变量数据、绑定、分组、模式、依赖顺序和禁用状态原因。仅用于 UI 的名称和显示路径被排除在外。


* 预检会阻断无效规范模型、缺失输入、无效/重复绑定、未绑定或语义不匹配的激活变量、未注册或单位/域不匹配的语义、参数化冲突、重复写入者、缺失/孤立/循环派生表达式以及派生结果单位不匹配。被禁用的变量保留在 `disabledReasons` 中并收到信息性的 `DESIGN_SPACE_VARIABLE_DISABLED` 诊断，但不会出现在绑定和搜索向量中。


* RED 证据：扩展的 `design_space_compiler` 测试在能力和派生表达式输入添加到请求边界之前无法编译。GREEN 证据：全新 Debug 构建成功，绝对路径可执行文件的 QCoreApplication `design_space_compiler`、`derived_expression`、`parameterization` 和 `design_variable` 套件退出代码均为 0。S25 套件覆盖了等价顶层输入顺序、稳定 Schema 索引、纯独立向量、禁用变量诊断、写入冲突、未绑定变量、派生单位不匹配与循环阻断，以及改变设计空间指纹的能力变更。旧版生成器/UI 特意保持未改动，直至其后续的迁移门控。



## Phase 2 / S26 证据 (2026-08-19)

* 添加了纯 `DesignVector` 编解码器。其按 Schema 定位的独立值具有显式的 `CompiledDesignSpace` 指纹、工程单位表示、规范字节序列以及固定的 `fnv1a-64` 值。字节形式采用固定字段顺序和 IEEE-754 位编码，将 `-0.0` 归一化为 `+0.0`，绝不通过区域设置（locale）格式化工程数值。


* `fromNormalized`、`fromEngineering` 和 `toNormalized` 严格实现连续映射 $x = \text{min} + u \cdot (\text{max} - \text{min})$ 及其逆映射。它们不会截断超出范围的值，也不会对未对齐的整数步长进行四舍五入。离散变量仅携带已声明的稳定选项 ID；其数值占位符必须为规范零，以防止被忽略的数值输入改变或混淆候选。


* 编解码器独立校验传入的已编译 Schema：精确长度、索引/顺序、变量 ID、单位、指纹、有限数值输入、有效边界、整数对齐以及非空/唯一的离散选项 ID。因此，它仅接受规范 Schema 中的独立变量；派生变量和禁用条目无法进入 `DesignVector`。


* RED 证据：初始针对性构建因 `DesignVector.hpp` 缺失而失败。随后的 RED 用例表明，有效离散变量的默认数值字段、非零离散占位符以及重复选项 ID 需要显式的编解码处理。GREEN 证据：全新 Debug 构建以及绝对路径可执行文件的 QCoreApplication `design_vector`、`design_space_compiler`、`derived_expression`、`parameterization` 和 `design_variable` 套件退出代码均为 0。向量套件覆盖了归一化和工程数值往返、边界、整数步长、稳定离散 ID、Schema/长度错误、规范字节/哈希相等性、`-0.0`、非有限输入以及纯独立成员资格。



## Phase 2 / S27 证据 (2026-08-19)

* 添加了纯只读的 `LegacyDesignSpaceAdapter::preview`。它消费常量（const）旧版 `StructureOptimizationProblem`，返回逐变量迁移条目以及独立映射的规范变量/绑定，绝不写入项目状态或旧版 JSON 表示。


* 带有目标的 `BaseHeight` 直接映射到 `BaseTz`/`BasePlacementAdapter`。旧模型没有明确的 `LinkLength` 种类，因此 LinkLength 映射需要显式类型化的 `LegacyDesignSpaceBindingHint`；绝不单独猜测标签和目标字符串。原始旧版变量被复制到每个条目中，保留了旧版范围、单位、域、偏好和其他兼容性字段。仅当转换显式且有限时，映射值才转换为规范 m/rad 单位。


* DH A/D 变量仅作为“旧版/仅限投影”条目保留（`LEGACY_DH_PROJECTION_ONLY`）；缺失、无效、不支持、非有限或单位不兼容的绑定将变为带稳定诊断的已禁用 `legacy/unbound` 条目。不存在向首个目标的回退，没有 DH 主要变量，也不修改旧版项目。使用相同输入重复预览会产生相同的条目和绑定标识。


* RED 证据：针对性测试起初因 `LegacyDesignSpaceAdapter.hpp` 不存在而失败。GREEN 证据：全新 Debug 构建以及绝对路径可执行文件的 QCoreApplication `legacy_design_space`、`design_vector`、`design_space_compiler`、`derived_expression`、`parameterization` 和 `design_variable` 套件退出代码均为 0。迁移测试覆盖了显式 LinkLength 映射、BaseHeight → BaseTz、DH 投影诊断、安全未绑定处理、旧版 mm 范围/单位保留、非修改性以及幂等的重复预览。



## Phase 3 / S30 证据 (2026-08-19)

* 添加了纯 `IModelParameterAdapter` 契约、显式拥有的 `AdapterRegistry`、类型化的纯数据 `CandidatePatch` 以及适配器诊断辅助函数。适配器接收借用的不可变规范基线并返回补丁；该边界不包含 Widget 或可变 WorkCell，也不连接旧版优化器或评估器。


* 注册机制拒绝缺失的适配器、空 ID、非正版本号、未知/空语义声明以及重复 ID。语义查询和指纹材料是确定性的（`std::map` 注册表顺序以及已排序的已声明语义/能力），并包含适配器版本。


* 在适配器调度之前，Registry 总是先运行通用的 `ParameterBindingValidator`，然后运行特定于适配器的验证，要求声明非空读/写集合，并对绑定目标进行能力门控。补丁必须标明已注册适配器/版本和绑定；每次类型化写入都必须以声明的写入集条目为目标，并带有有限标量或非空文本值。未声明的写入将被阻断。


* 来自验证、编译结果和嵌套补丁路径的适配器诊断会保留其传入数据，但会补齐任何缺失的绑定、对象和字段上下文。任何合并的 Error 诊断都会导致编译失败，因此名义上 `ok=true` 的结果无法将半有效的候选向后泄漏。


* RED 证据：针对性测试最初因补丁头文件缺失而失败；随后的针对性 RED 用例暴露了可绕过的通用绑定验证、缺失诊断上下文、验证诊断丢失以及编译结果与补丁双通道中的 Error 诊断。GREEN 证据：全新 Debug 构建成功，QCoreApplication `adapter_registry`、`design_variable`、`design_registry`、`design_template`、`parameterization`、`derived_expression`、`design_space_compiler`、`design_vector` 和 `legacy_design_space` 套件退出代码均为 0。该切片特意将所有真实变量、CandidateCompiler、评估器、优化器和 UI 切换留给后续计划的任务。



## Phase 3 / S31 证据 (2026-08-19)

* 添加了纯 `JointOriginAdapter` 和 `ParameterizedLinkAdapter` 实现。它们仅针对借用的不可变规范基线编译类型化 `CandidatePatch` 数据；未引入通用补丁应用器、RobotModelSpec 投影、WorkCell 修改、视觉/碰撞工件、评估器、优化器或 UI 连线。通用补丁合并和原子模型提交仍属于后续 S36 的职责。


* 关节原点笛卡尔 X/Y/Z 语义从基线平移加上所选偏置生成准确的三个 `ParentToJointTranslation*` 写入。`JointOffsetAlongAxis` 在应用其相对基线的增量之前，将有限的、归一化的基线 `motionAxisInJoint` 旋转到父坐标系中；零或非有限轴将被拒绝，而不会变为无操作（no-op）或静默缩放物理距离。


* LinkLength 需要有限的单位 `referenceDirection`（参考方向）以及等于目标关节父坐标系的显式参考方向坐标系。它使用不可变基线投影作为名义长度，并计算 $p_{\text{new}} = p_{\text{baseline}} + (\text{requested} - \text{nominal}) \cdot \text{direction}$；小于或等于 $1\times 10^{-6}\text{ m}$ 的长度将被拒绝。不存在标签/名称推断、世界轴默认值或几何中心替换方案。


* 读/写声明作为精确的无序目标集合进行对比（重复项被拒绝），主要平移属性特定于具体语义，且所有解析输入均恰好为一个有限的米制标量。因此，这两个适配器无法将补丁应用到未声明或语义不相关的规范属性上。DesignSpace 既有的写入者冲突验证会阻断在相同平移目标上同时使用 LinkLength 和 JointOrigin。


* 适配器兼容性现在通过借用的受信任 `AdapterRegistry` 生成指纹，绝不使用调用方传入的指纹字符串。绑定携带正数 `ownerAdapterVersion`；通用绑定验证和直接 Registry 编译均要求它在适配器验证/补丁构建之前与已注册适配器匹配。参考方向和所有者版本参与绑定相等性与已编译设计空间标识判定。


* RED 证据：S31 套件最初因 `JointOriginAdapter.hpp` 不存在而失败。随后的 RED 测试捕获到了缺失受信任注册表输入、未归一化/非有限轴、有序集合对比、语义/属性不匹配、版本不匹配以及易错的绑定元数据。Phase 2 集成运行还捕获到旧版类型化 LinkLength fixture 缺失其现已强制要求的显式 +X/基座方向；仅补齐了该 fixture 元数据，未添加任何生产回退逻辑。GREEN 证据：全新 Debug 构建以及 QCoreApplication `joint_origin_link_adapter`、`adapter_registry`、`design_variable`、`design_registry`、`design_template`、`parameterization`、`derived_expression`、`design_space_compiler`、`design_vector`、`legacy_design_space`、`canonical_fk` 和 `canonical_model` 套件退出代码均为 0。S31 测试通过仅将发出的写入应用到测试本地的规范克隆上，证明了笛卡尔 X/Y/Z、父坐标系 AlongAxis、显式 LinkLength 方向、较小长度拒绝、确定性重新编译、基线非修改性以及预期的 FK 位移。



## Phase 3 / S32 证据 (2026-08-19)

* 添加了纯 `JointAxisAdapter`。它消费借用的规范基线，并仅为正在编译的绑定发出一个类型化的 `MotionAxisTiltU` 或 `MotionAxisTiltV` 标量写入。它绝不写入零偏置或父/子安装变换，也不直接修改规范模型。S36 仍负责将成组补丁应用到 `motionAxisInJoint`。


* 适配器使用固化的 `KinematicConventions::tiltedAxis` 基底和公式。一对解析后的 U/V 值仅在两者均为有限弧度、具有具体的 U/V 语义且属于与绑定相同的规范 `axis-tilt:<jointId>` 组时才被接受。完整偏转校验为 $\rho = \text{hypot}(U, V)$，而不是通过逐分量截断或折叠后的旋转后夹角来校验；这是实际的锥面坐标。


* 轴偏转仅对具有有限、非零名义轴的 Revolute 和 Prismatic 关节有效。锥面是显式的绑定元数据，在 $[0, \pi]$ 内有限，并参与指纹计算。严格的 $\rho = \text{maxAxisTiltAngle}$ 边界是被接受的，而更大、周期折叠（例如 $2\pi$）、非有限或跨关节组输入将被拒绝。绑定还要求规范组标识，防止伪造的共享标签将一个关节的 U 与另一个关节的 V 混淆。


* 更新了固化辅助函数，使 `tiltedAxis` 使用 `hypot` 并在偏转严格为零时才返回名义轴；有限的极小非零 U/V 不再被容差分支静默抹除。这使得 S04 数学和适配器补丁语义保持一致，并避免了锥面评估中的平方分量溢出。


* RED 证据：针对性测试起初因 `JointAxisAdapter.hpp` 缺失而失败。后续 RED 用例暴露了缺失 U/V 组标识、周期性锥面折叠、伪造的跨关节组 ID、微小非零倾斜抑制以及超过 $\pi$ 的锥面。GREEN 证据：全新 Debug 构建以及 QCoreApplication `joint_axis_adapter`、`joint_origin_link_adapter`、`adapter_registry`、`design_variable`、`design_registry`、`design_template`、`parameterization`、`derived_expression`、`design_space_compiler`、`design_vector`、`legacy_design_space`、`kinematic_conventions`、`canonical_fk` 和 `canonical_model` 套件退出代码均为 0。轴套件覆盖了零和正/负 U/V、实际 $\rho$、微小非零倾斜、确定性基底、精确/超出锥面边界、测试本地克隆中的单位最终轴、基线不可变性、Revolute/Prismatic 支持以及 Fixed 拒绝。



## Phase 3 / S33 证据 (2026-08-19)

* 添加了纯 `JointZeroAdapter` 和 `JointLimitAdapter` 实现。两者均消费借用的不可变规范基线并仅发出类型化 `CandidatePatch` 数据；两者都不修改运行中的 WorkCell，也不引入后续的 S36 补丁应用器、评估器、优化器或 UI 连线。


* 关节零位补丁仅拥有 `ZeroPositionOffset`。旋转关节坐标为弧度，移动关节坐标为米；Fixed 关节会被拒绝。固化方程保持为 $q_{\text{model}} = q_{\text{input}} + \text{zeroPositionOffset}$，因此更改零位绝不会旋转运动轴或安装变换。


* 物理和运行的下限/上限是独立的类型化目标，具有显式的 lower/upper 组、作用域、最小范围、绝对包络和 q 坐标约定。物理限位修改需要显式的绑定授权，默认不建议启用。运行补丁设置 `affectsStructuralCapability = false`，且运行边界不能超过已启用的物理边界。


* 规范模型现在针对启用的限位记录了显式的 `QInput` 或 `QModel` 约定。导入器记录 `QInput`，JSON 和指纹保留该约定，验证机制会拒绝非有限、未排序、错误单位、无效约定或 Fixed 关节的限位。运行对物理的检查在对比前使用固化的零位偏置转换边界；这可防止非零偏置静默允许超出物理包络的范围。


* 默认注册表或全运动学模板不再生成 `JointZeroOffset`。它仅在 Home、装配或编码器零位需求提供必要的设计意图时，通过显式绑定保持可用。


* RED 证据：针对性套件最初因 `JointZeroAdapter.hpp` 缺失而失败。后续 RED 用例暴露了缺失作用域/组元数据、物理锁定绕过、q 坐标歧义、非有限与错误单位基线限位、默认零位建议、跨约定边界以及伪造的未知坐标枚举。GREEN 证据：全新 Debug 构建成功，QCoreApplication `joint_zero_limit_adapter`、`adapter_registry`、`design_variable`、`design_registry`、`design_template`、`parameterization`、`derived_expression`、`design_space_compiler`、`design_vector`、`legacy_design_space`、`joint_axis_adapter`、`joint_origin_link_adapter`、`kinematic_conventions`、`canonical_model`、`canonical_importer`、`kinematic_fingerprint` 和 `canonical_fk` 套件退出代码均为 0。`git diff --check` 未报告任何空白字符错误（仅有既有的 CRLF 转换警告）。



## Phase 3 / S34 证据 (2026-08-19)

* 添加了纯 `BasePlacementAdapter`、`FlangePoseAdapter` 和 `TcpPoseAdapter` 实现，以及固化的位姿增量元数据。它们接收借用的不可变规范基线并仅发出类型化 `CandidatePatch` 数据；在计划阶段之前未添加通用补丁应用器、几何/碰撞重建、评估器、优化器或 UI 集成。


* 位姿增量使用显式的右乘旋转向量约定 $T_{\text{next}} = T_{\text{baseline}} \cdot \text{Exp}(\delta)$。欧拉角不作为规范状态存储。平移绑定必须在变量和绑定中指定相同的坐标系，防止声明坐标系/补丁坐标系不匹配。


* 基座变量仅限于不可变系统根坐标系中的规范基座坐标系，并描述 `SystemPlacement`；适配器没有任务或环境写入目标。法兰变量要求在活动设备链上恰好有一个入向的 Fixed 安装边。TCP 变量仅以有效的 Flange-to-Tool `ToolBinding` 为目标，并要求工具位姿、参数化几何和参数化碰撞能力，因此仅提升 TCP 可达率的改动在生成匹配工件前会被阻断。


* 法兰和 TCP 语义元数据现在具有不同的适用性类别。即使直接调用而非通过 `AdapterRegistry` 调用，所有三个适配器也会强制执行精确的类型化读/写集、SI/弧度值、确定性位姿组、受信任适配器版本以及通用绑定验证。这可防止畸形的直接绑定和歧义的侧支法兰拓扑创建补丁。


* RED 证据：针对性套件最初因 `BasePlacementAdapter.hpp` 缺失而失败。后续 RED 用例暴露了缺失声明坐标系检查、不正确的法兰适用性元数据、可绕过的直接绑定验证以及不活动的 Fixed 侧支被误认为独立法兰。GREEN 证据：全新 Debug 构建成功，QCoreApplication `base_flange_tcp_adapter`、`joint_zero_limit_adapter`、`joint_axis_adapter`、`joint_origin_link_adapter`、`adapter_registry`、`design_variable`、`design_registry`、`design_template`、`parameterization`、`derived_expression`、`design_space_compiler`、`design_vector`、`legacy_design_space`、`kinematic_conventions`、`canonical_model`、`canonical_importer`、`kinematic_fingerprint` 和 `canonical_fk` 套件退出代码均为 0。`git diff --check` 未报告任何空白字符错误（仅有既有的 CRLF 转换警告）。



## Phase 3 / S36 证据 (2026-08-20)

* 添加了纯 `CandidatePatchMerger` 和 `CandidatePatchApplier` 边界。合并器保持类型化写入处于确定性目标顺序，接受幂等的重复写入，拒绝冲突值和位姿组，聚合诊断信息，并对生成的工件和派生值 ID 进行排序/去重。应用器复制规范基线，仅应用显式的类型化目标处理程序，要求成对的 U/V 轴坐标，并在目标、能力、成组操作或应用后规范验证失败时不返回任何候选模型。


* 支持的 S36 应用目标涵盖关节平移、轴偏转、零位偏置、物理/运行限位、基座/法兰/TCP 平移与右乘旋转向量增量、所属的视觉/碰撞尺寸以及所属的网格工件引用。输入基线保持不变。未连接 `RobotModelSpec`、WorkCell、UI、旧版优化器、评估器或候选运行时路径。


* RED 证据：针对性断言最初在稳定的派生工件排序、合并补丁诊断以及缺失轴 U/V 兄弟项拒绝方面失败。GREEN 证据：通过 `scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` 进行的全新 VS x64/MSVC Debug 构建成功。在 `QT_QPA_PLATFORM=windows` 下，绝对路径测试可执行文件在 `candidate_patch_merge_apply`、`adapter_registry`、`parameterized_geometry_collision_adapter`、`canonical_model`、`canonical_fk`、`design_space_compiler`、`design_vector`、`base_flange_tcp_adapter`、`joint_zero_limit_adapter` 和 `legacy_design_space` 中的退出代码均为 0。


* `git diff --check` 未报告任何空白字符错误；仅发出了仓库既有的 LF 转 CRLF 转换警告。下一个计划切片是 S37 `CandidateCompiler`；S36 仍与旧版执行路径保持隔离。



## Phase 3 / S37 证据 (2026-08-20)

* 添加了纯规范 `CandidateCompiler` 和 `CompiledCandidate`。编译器校验规范基线和按 Schema 定位且带指纹的 `DesignVector`，计算 `CompiledDesignSpace` 中保留的表达式，按绑定组解析独立值和派生值，调用受信任的 `AdapterRegistry`，合并补丁，并原子性发布复制/验证后的规范模型。


* `CompiledCandidate` 记录了编译状态、候选 ID、设计向量、规范模型、派生值、生成的工件指纹、诊断信息以及确定性指纹。编译失败不会保留任何部分候选模型。基线输入绝不被修改。


* 强化了 S36 位姿合并边界，使得独立的 Base/TCP/Flange 位姿组可以共存；仅当一个类型化目标被不同组声明拥有时才报告位姿组冲突。S37 还会拒绝被篡改的向量规范字节/指纹以及适配器 `ok == false` 结果。


* RED 证据：初始 S37 构建因缺失编译器头文件而失败；首个真实的组合适配器运行暴露了过于全局的 S36 位姿组冲突。GREEN 证据：通过 `scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test` 进行的全新 VS x64/MSVC Debug 构建成功。在 `QT_QPA_PLATFORM=windows` 下，绝对路径测试可执行文件在 `candidate_compiler`、`candidate_patch_merge_apply`、`adapter_registry`、`canonical_model`、`canonical_fk`、`design_space_compiler`、`design_vector`、`base_flange_tcp_adapter`、`joint_zero_limit_adapter` 和 `legacy_design_space` 中的退出代码均为 0。S37 测试覆盖了真实的 LinkLength + Axis U/V + Base + TCP 组合、重复编译相等性、派生值、Schema/指纹拒绝、原子失败以及基线不可变性。


* S37 仍与 `RobotModelSpec`、WorkCell、UI、旧版优化器和旧版评估器保持隔离。S38 负责评估设备投影。



## Phase 3 / S38 证据 (2026-08-20)

* 添加了 `RobotModelSpecProjectionAdapter` 作为只读的规范到输出投影。它发出显式 $\text{SE}(3)$ `transformJoints`，保留 Revolute/Prismatic/Fixed/Tool 语义，将 DH 投影留空，映射规范限位以及视觉/碰撞绑定，并发出规范零 Q。


* 添加了 `EvaluationDeviceBuilder`，它投影到复制的工作者（worker）`RobotModelSpec` 中，调用既有隔离的 `CandidateModelFactory`，并在投影/构建失败时不返回任何工件。活动的 WorkCell 和 State 绝不被借用或修改。


* 更新了 XML 写入器以保留显式 ToolFrame 行，而不是追加零 TCP 回退项，并修复了新公共构建器头文件暴露出的旧版工厂头文件保护宏（include guard）冲突。


* RED 证据：S38 最初在缺失投影 API 处失败，随后首个运行时 fixture 暴露了无效 ToolBinding 和 Windows 加载器的限定 TCP 名称；两者现已显式诊断/处理。GREEN 证据：全新 VS x64/MSVC Debug 构建成功。在 `QT_QPA_PLATFORM=windows` 下，绝对路径可执行文件在 `s38_projection`、`candidate_compiler`、`canonical_fk` 和 `canonical_model` 中的退出代码均为 0。`git diff --check` 成功完成。



## Phase 4 / S40, S43, S44 证据 (2026-08-21)

* 添加了 `EvaluationPlan` 和 `EvaluationPlanCompiler`，作为 `RequirementExecutionSet` 的仅限执行投影。编译器保留 Must、Should 和 Info 语义，拒绝不受支持的 v3 Verified 区域，在执行前检查模型/环境指纹和评估器能力，拒绝未知指标和不安全的区域采样，并发出确定性的计划指纹。它不读取 UI 状态，也不调用评估器。
* 添加了可取消的 `EvaluationPipeline`/`EvaluationStage` 契约。阶段按注册顺序执行，保留版本和完成情况事实，将缺失的能力报告为 `DataInsufficient`，并在协作取消点干净利落地停止。畸形计划会在任何阶段运行前失败。
* 添加了带有显式可用性状态和生产者/单位/方向/能力元数据的 `MetricRegistry`/`MetricResult`。标准注册表包括任务、工作空间、区域、关节裕度、雅可比矩阵（Jacobian）和碰撞指标；缺失的证据绝不表示为数值零。
* RED 证据：`StructureOptimizationTest.cpp` 最初因 `EvaluationPlan.hpp` 缺失而编译失败。GREEN 证据：全新 VS x64/MSVC Debug 构建成功，绝对路径 Windows Qt 测试可执行文件在 `evaluation_plan`、`evaluation_pipeline`、`candidate_compiler`、`s38_projection` 和 `canonical_fk` 中的退出代码均为 0。`git diff --check` 未报告任何空白字符错误（仅有既有的 LF 转 CRLF 转换警告）。
* 添加了 `ConstraintEvaluator` 和 `ObjectiveAggregator` 边界。保留了约束证据可用性，安全约束不可被静默软化，归一化会拒绝无效范围，且无论目标分数如何，硬违反均使聚合结果保持不可行。针对性的 CTest 套件 `sdurws_structureoptimizer_constraint_objective_test` 测试通过。


## Phase 6 / S60 证据 (2026-08-21)

* 定义了当前权威 JSON Envelope（`StructureOptimizationDocument`）：根 `schemaVersion` 加上 designSpace/plan/objectives/constraints/config 各自独立的子 schemaVersion；`currentEnvelopeToJson` 是唯一写出当前 schema 的路径，内部经旧序列化做一次边界转换后重组为 canonical 分区，不再把旧根字段与新根字段同时作为权威输出。

* 写入门在边界处完成 SI 转换（mm/cm/µm→m，deg→rad，按变量种类区分角度/长度族），并为每个变量生成持久化 Binding（id/adapterId/version/targetName/kind/unit）；results、运行时指针与候选证据一律不进入主配置。`currentEnvelopeFingerprint` 对按键排序的规范紧凑 JSON 计算 SHA-256，对象字段插入顺序不影响指纹，数组顺序保留为语义。

* `currentEnvelopeFromJson` 是严格读取门：拒绝旧根类型或缺失/版本不符的 canonical 分区；未知根字段归档进 `extensions` 且显式扩展不得覆盖协议字段；校验 Binding 的对象形状、id 唯一性、adapterId/version 存在性与正整数性、变量引用存在性以及 SI 单位一致性。旧 `problemToJson`/`problemFromJson` 按计划保留给 S61 迁移，Project Provider 仍先调用 facade。

* 契约审计发现并修复一个缺口：写入门原先会把无法解释的单位（如 "in"、空串）静默改标为 m/rad，伪造 canonical 语义。现在 `siFactor` 只接受与变量种类同族的明确单位，其余情况 `currentEnvelopeToJson` 抛出 `std::invalid_argument`，写出门显式失败而不是产出损坏文档。

* RED 证据一：新增断言暴露未知根字段被 reader 静默丢弃、缺失 id/adapterId/version 的 Binding 被接受，两处失败。RED 证据二：单位审计断言（未知单位/空单位/角度种类配长度单位）在收紧前全部失败（FAILED (3)）。GREEN 证据：VS x64/MSVC Debug 增量构建成功；`QT_QPA_PLATFORM=windows` 下绝对路径可执行文件 `current_json_envelope`、`json_roundtrip`、`json_safety` 与完整 `ui` 相邻回归退出代码均为 0；CTest 注册确认 25 个 structureoptimizer 测试项，`sdurws_structureoptimizer_current_json_envelope_test` 通过；`git diff --check` 无空白字符错误（仅既有 LF 转 CRLF 警告）。

## Phase 6 / S61-S65 证据 (2026-08-21)

* S61 将旧版根 JSON 作为只读输入迁入 S60 当前 Envelope。迁移结果保留显式迁移报告、警告和未绑定变量诊断；输入 JSON 不被回写，未知字段不会伪装成当前协议字段，迁移后的输出只能经 `currentEnvelopeToJson` 写出。

* S62 新增 `OptimizationRunSnapshot`、`OptimizationRunJson` 和 `OptimizationRunStore`。运行快照冻结项目 Envelope、模型/环境/需求/设计空间/评估计划/最终验证计划、工具链和适配器注册表指纹；候选结果和证据以带 SHA-256、字节数和相对路径的项目资源引用保存，运行时指针、WorkCell、State、QObject 和求解器对象不进入持久化结果。

* S63 新增 `StructureOptimizationWorkflowResolver`，把项目打开状态、绑定目标、当前与持久化指纹、评估器/编译器版本和适配器注册表统一解析为可启动、缓存可复用、历史运行可读三类门控结果。模型、场景、环境、需求、运动学验证、TCP、版本或适配器变化会生成稳定失效码；失效只能阻止复用，不能静默继续旧运行。

* S64 新增纯核心 `OptimizationPreflight`。启动前逐项检查模型、需求、运动学验证、内容指纹、设计空间、适配器、指标、评估器、归一化、证据阶段、基线、独立变量、Grid 规模、候选/最终验证计数，并为每个发现提供稳定 code、对象、字段路径、中文说明和修复建议；现有 UI preflight 仍可复用同一入口。

* S65 增加 `phase6_integration` 集成门，将当前 Envelope、旧版迁移、运行快照、运行资源存储、工作流失效、启动前检查和模型过期性放入同一进程连续回归；CTest 同时注册各独立套件和集成门，确保单项定位与跨边界协作均可审计。

* GREEN 证据：VS x64/MSVC Debug 目标 `sdurws_structureoptimizer_test` 全新构建成功；`QT_QPA_PLATFORM=windows` 下绝对路径可执行文件的 `phase6_integration` 通过 7 项（退出代码 0），S60-S64 独立套件和相邻 StructureOptimizer 核心套件均作为最终门控运行；`git diff --check` 通过（仅既有 LF 转 CRLF 警告）。
