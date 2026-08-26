# GenericSixAxis 桌面取放工作站实施计划

> **For Codex:** Required execution skill: use `superpowers:executing-plans` to implement this plan task-by-task.

**目标：** 将新建项目的 GenericSixAxis 默认值升级为可直接保存、加载、碰撞检查、任务验证及结构优化的桌面六轴取放工作站，同时保持既有 `Base`、`Joint1`…`Joint6`、`TCP` 契约不变。

**架构：** `RobotModelXmlWriter::makeDefaultSixAxisModel()` 是新建项目和“恢复默认值”共同的唯一模板源。该函数返回包含机器人、场景帧、场景几何和碰撞设置的 `RobotModelSpec`；现有 XML writer、项目资源流程和 WorkCell loader 继续负责序列化与装载。测试以生成的临时 WorkCell 为唯一夹具，避免维护第二套手写 XML 真值。

**技术栈：** C++17、Qt、RobWork `WorkCellLoader`/运动学/碰撞、RobotModelBuilder、Structure Optimizer。

## 先决约束

- 保持 `Base`、`Joint1` 至 `Joint6` 和 `TCP` 原名；不新增或改名为 `ToolFrame`。
- 不引入 STL、OBJ 或任何外部网格；视觉和碰撞均使用生成器已有的基础体。
- `PickPart`、`InspectionPart` 是独立 `MovableFrame`；场景物体不是优化变量。
- 默认状态没有碰撞；测试必须另构造一个确定的碰撞状态，不能以“没有异常”代替碰撞证据。
- 不修改现有 `RobWork/example/ModelData/.../GenericSixAxis*.wc.xml`。它们是独立示例资产；生成器测试将把新项目模板固定为唯一真值，避免两个来源再次漂移。

## Task 1：先锁定默认模板契约与端到端验收（RED）

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

1. 在 `RobotModelXmlWriterTest.cpp` 的默认模型校验附近新增 `testDefaultDesktopWorkcellContract()`（或同等的独立测试段）。先断言当前实现尚不满足的可观察契约：

   ```cpp
   const RobotModelSpec spec = RobotModelXmlWriter::makeDefaultSixAxisModel(dir.path());
   CHECK(spec.generateScene);
   CHECK(hasFrame(spec, "RobotBase", SceneFrameType::Fixed));
   CHECK(hasFrame(spec, "PickPart", SceneFrameType::Movable));
   CHECK(hasFrame(spec, "InspectionPart", SceneFrameType::Movable));
   CHECK(hasGeometry(spec, "WorkTableTop", "WorkTable"));
   CHECK(hasGeometry(spec, "PickPartGeometry", "PickPart"));
   CHECK(hasGeometry(spec, "InspectionPartGeometry", "InspectionPart"));
   CHECK(hasGeometry(spec, "SafetyPost", "Obstacle"));
   CHECK(hasDrawable(spec, "GripperPalm", "TCP"));
   CHECK(hasDrawable(spec, "FingerLeft", "TCP"));
   CHECK(hasDrawable(spec, "FingerRight", "TCP"));
   CHECK(spec.robotName == "GenericSixAxis");
   CHECK(frameNamesRemainUnique(spec));
   ```

2. 将该 spec 用 `saveFiles()` 写入 `QTemporaryDir`，以 `WorkCellLoader::Factory::load()` 读回。断言 device、`Base`、`TCP`、两个 MovableFrame、取放/检验目标与 CollisionSetup 都存在；用独立 State 改动两个工件并断言它们互不覆盖状态。
3. 使用 `CollisionDetector` 对 Ready pose 的加载 State 断言无碰撞；将 `PickPart` 放进 `Obstacle` 或台面体积内后断言有碰撞。测试应输出碰撞 frame pair，便于将来定位几何回归。
4. 在 `RobotModelBuilderWidgetTest.cpp` 对 `applyDefaultProjectModel()` 新增 UI 无关断言：默认“Generate Scene file”勾选、场景预览不再是 placeholder、保存路径生成 scene/collision 文件。该测试只经 Widget 公共行为取证，不依赖控件创建顺序。
5. 在 `StructureOptimizationTest.cpp` 增加一个端到端的“默认桌面模型消费”夹具：由 `makeDefaultSixAxisModel()` 保存并加载，使用 `TCP` 和 Pick/Place/Inspection 的固定目标建立最小需求；验证 model fingerprint、目标解析、基线评估与候选预览输入均可完成。只断言结果有明确状态和证据，不硬编码优化分数。
6. 构建并运行三个测试，确认新增断言在改实现前失败：

   ```powershell
   cmake --build D:\10_Source_Repos\21_robot\RobWork\RobWork\build\codex-vs-debug --config Debug --target sdurws_robotmodelbuilder_xmltest sdurws_robotmodelbuilder_widgettest sdurws_structureoptimizer_test
   ```

   仅对 Widget/StructureOptimizer 可执行文件，在 VS x64 Developer PowerShell 中设置 `$env:QT_QPA_PLATFORM='windows'`，以绝对路径一次启动一个；XML test 是模型测试，可直接执行。
7. Commit: `test: specify default desktop workcell contract`

## Task 2：实现比例合理的六轴本体与固定开口夹爪（GREEN）

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

1. 在 `makeDefaultSixAxisModel()` 把当前 UR5 风格的任意默认数值替换为具名的桌面尺寸常量，避免散落 magic number：

   ```cpp
   constexpr double kTableTopHeight = 0.740;
   constexpr double kBaseDiameter = 0.200;
   constexpr double kBaseHeight = 0.180;
   constexpr double kUpperArmLength = 0.340;
   constexpr double kForearmLength = 0.290;
   constexpr double kWristLength = 0.150;
   constexpr double kGripperLength = 0.140;
   constexpr double kGripperOpening = 0.070;
   ```

2. 用这些常量定义 6 个 authoritative `JointRPYPos` 变换、关节限位、Ready pose 与动力学比例，使展开半径约 0.85 m。保留现有 `appendLinks()` / `applyLinkGeometry()` 单一几何推导路径；不要手工覆写自动连杆的中心、姿态或长度。
3. 保留 `BasePedestal`、`ShoulderHousing` 与 `ToolFlange`，但以桌面比例调整半径和长度；以 `TCP` 为附着 frame 新增 `GripperPalm`、`FingerLeft`、`FingerRight` 三个基础体。两根手指固定为 0.07 m 开口，颜色使用低饱和深灰机械臂、浅灰腕部、蓝色夹爪的统一配色。
4. 为视觉体生成对应简化 collision primitives，或明确让已有 collision-model 生成路径从同一 drawable 尺寸派生；禁止仅增加视觉体导致夹爪在碰撞系统中缺失。
5. 更新 Task 1 的尺寸、TCP 与夹爪断言，并补充“自动连杆仍由关节变换导出”的回归，确保未来尺寸改动不会使可视化偏离运动学。
6. Run `sdurws_robotmodelbuilder_xmltest`；确认此前的 DH/JSON/转换测试中任何依赖旧 UR 数值的断言改为验证不变量或更新为新的设计尺寸。
7. Commit: `feat: refine GenericSixAxis desktop arm and gripper`

## Task 3：实现桌面取放与避障场景，并让新建项目默认生成它（GREEN）

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp`

1. 将 `spec.generateScene` 默认设为 `true`，将机器人基座固定到台面左后侧；`RobotBase` 位置应与 `kTableTopHeight` 对齐，且不能改变 device 内 `Base` frame 名称。
2. 以明确名称填充 `sceneFrames`：`WorkTable`、`PickBin`、`PlaceBin`、`Obstacle`、`PickPart`、`InspectionPart`、`PickApproach`、`PickTarget`、`PlaceApproach`、`PlaceTarget`、`InspectionTarget`。除两个工件外均为 Fixed/Normal frame；两个工件为 `Movable` 且各自有可识别初始 State。
3. 以同一局部坐标系填充 `sceneGeometries`：1.4×0.9 m 台面（含足够的厚度/腿部以便视觉识别）、两个料盒、立柱/挡块、两个工件。每个需要碰撞的物体设 `collisionModel = true`，颜色区分桌面、容器、障碍物、工件和机器人。
4. 从机械臂 Ready pose 正向运动学计算/校准 Pick、Place、Inspection 操作点与接近点，保证在关节限位内；工件、料盒和障碍初始位置须留出夹爪和连杆净空。不要通过 CollisionSetup 排除 robot-vs-scene pair 来伪造无碰撞，只保留已有的合理自碰撞相邻 link 排除。
5. 保存后加载 scene，验证场景 include、碰撞配置相对路径和默认 Ready state；Widget test 验证新项目的 XML Preview/Save-and-Load 会选择 scene 而非裸 SerialDevice。
6. Run XML 与 Widget tests；Widget test 严格按 Windows Qt GUI/Test Launch Rule 单进程执行。
7. Commit: `feat: add pick-and-place desktop scene to GenericSixAxis`

## Task 4：把默认场景作为工程需求、可达性、覆盖率和优化回归夹具

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogicTest.cpp`（若该独立测试目标存在；否则放在前一文件）
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

1. 建立一个测试辅助函数，只从已保存/加载的默认 WorkCell 读取 `TCP`、Pick/Place/Inspection frame 和方向区域；不要重写与生成器不相同的硬编码坐标。
2. 用该辅助函数覆盖以下链路，并分别断言错误码/证据而非只断言 boolean：

   ```text
   目标解析 -> Frozen requirement 生成 -> fingerprint 一致
   Pick/Place/Inspection IK -> 每项至少一个可行解
   方向区域采样 -> position/orientation coverage 有可解释指标
   默认 Ready baseline -> 载荷、碰撞、可达性状态一致
   一次受限候选预览 -> 使用运行快照、frame 名和 collision setup 均有效
   导出 -> 写出的模型可以重新加载，且保持 TCP 与两个 MovableFrame
   ```

3. 对碰撞约束新增反例：工件置入障碍体积后，评估必须产出碰撞证据或不可行原因；不得将“未检查”转写为无碰撞。
4. 若默认任务存在多解，测试只检查最少一个可行解和非零的、有限的覆盖率；不将具体 score 作为固定金标准，避免以后调整权重时产生伪回归。
5. Run `sdurws_structureoptimizer_test`，随后重新运行 xmltest、jsontest 和 workcellconvertertest，确保模板仍可 JSON 往返、导入/导出与 XML 读回。
6. Commit: `test: exercise optimization with desktop GenericSixAxis workcell`

## Task 5：完整验证、交互验收与文档收尾

**Files:**
- Modify: `docs/superpowers/specs/2026-08-26-generic-six-axis-desktop-workcell-design.md`（仅在实现发现尺寸/名称必须微调时同步）
- Modify: `docs/superpowers/plans/2026-08-26-generic-six-axis-desktop-workcell.md`（仅记录与计划不同的已批准取舍）

1. 使用绝对路径、一次一个进程运行：

   ```powershell
   # Model-only
   & <absolute-path>\sdurws_robotmodelbuilder_xmltest.exe
   & <absolute-path>\sdurws_robotmodelbuilder_jsontest.exe
   & <absolute-path>\sdurws_robotmodelbuilder_workcellconvertertest.exe

   # Windows GUI tests, after entering VS x64 Developer PowerShell
   $env:QT_QPA_PLATFORM = 'windows'
   & <absolute-path>\sdurws_robotmodelbuilder_widgettest.exe
   & <absolute-path>\sdurws_structureoptimizer_test.exe
   ```

2. 在 RobWorkStudio 手工创建一个新项目，确认默认模型中场景开关已启用；Save and Load 后观察桌面、料盒、障碍、两工件和两指夹爪。分别拖动两个 MovableFrame，核对默认无碰撞和障碍物碰撞反例。
3. 在 Engineering Requirements/Structure Optimization 中以 `TCP` 添加 Pick、Place、Inspection 与方向区域任务，冻结后依次运行 Preflight、Baseline、一个短候选预览与导出。记录是否有具体拒因；不得通过关闭碰撞或降级需求来让验收通过。
4. 检查 `git diff --check`、全部目标测试结果以及只包含本功能文件的 staged diff；不将工作区中无关的用户修改、删除或未跟踪文件提交。
5. Commit: `docs: document GenericSixAxis desktop workcell validation`（仅当 Task 5 实际改了文档）。

## 最终验收矩阵

| 能力 | 自动化证据 | 人工复核 |
|---|---|---|
| 默认模型/尺寸/夹爪 | XML writer contract test | 新项目预览比例与配色 |
| 场景/可移动工件 | WorkCell 加载、独立 State 测试 | 拖动两工件 |
| 碰撞 | 默认无碰撞 + 反例有碰撞 | 场景中观察并复现反例 |
| IK/覆盖率/冻结 | Structure optimizer fixture | 需求冻结与 baseline |
| 保存/导出 | XML、JSON、converter round trips | Save-and-Load 与导出再载入 |
