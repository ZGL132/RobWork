# RobWorkStudio Workflow Project Entry Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 RobWorkStudio 的项目创建入口和顶部工作流改造成一条可验证的“建模 → 需求 → 运动学 → 结构优化”流水线，并保证三种入口共享同一项目上下文、资源指纹、事务回滚和阶段门控。

**Architecture:** 主窗口保留项目事务和插件生命周期管理；新增独立的 WorkflowStageController，以稳定资源 ID、指纹和发布状态计算阶段，而不是依赖 Tab 顺序或 WorkCell 是否打开。三个入口统一使用向导状态对象和可回滚的候选项目事务；RobotModelBuilder、WorkCellConverter、RobotProjectSourcePackager 通过明确的 options 结构接收导入策略，最终所有下游插件只消费已发布且指纹一致的项目工件。

**Tech Stack:** C++14/Qt Widgets/Qt Meta-Object、RobWork WorkCellLoader、现有 ProjectManager/ProjectDocumentRegistry/CallbackProjectDocumentProvider、GTest/CTest。

---

## 设计约束

- 顶部 Dock Tab 的显示文本与内部身份分离。内部身份使用 `objectName` 或稳定 workflow ID，不再使用 `tabText()`。
- Tab 顺序固定为 `RobotModelBuilder`、`EngineeringRequirements`、`KinematicAnalysis`、`StructureOptimizer`；Jog 保持右侧独立 Dock。
- 每个阶段允许返回前一阶段，但禁止跳过未满足的阶段。
- `Save and Load` 仍是机器人模型生成 WorkCell 的唯一正式发布动作；向导完成只产生草稿，不自动发布未经检查的场景。
- 每次模型、场景、需求或运动学验证发生变化，都必须使下游阶段重新变为 stale/locked。
- 所有项目内资源使用 project-relative 路径；外部来源只作为导入输入，不作为下游运行时依赖。
- Xacro 必须显式展开后再进入 URDF 管线；无法展开时明确失败，不把未展开宏当作普通 URDF。

## 文件边界

### 新增

- `RobWorkStudio/src/rws/WorkflowStageController.hpp/.cpp`：计算四阶段状态、锁定原因和失效传播。
- `RobWorkStudio/src/rws/ProjectCreationWizard.hpp/.cpp`：统一向导壳层、入口类型、公共元数据和完成结果。
- `RobWorkStudio/src/rws/RobotProjectImportOptions.hpp`：URDF/Xacro 导入策略和 Mesh 缺失策略。
- `RobWorkStudio/src/rws/WorkCellProjectImportOptions.hpp`：WorkCell 伴生文件、Target Device、TCP 绑定策略。
- `RobWorkStudio/src/rws/WorkflowBinding.hpp/.cpp`：项目级 Device/TCP/阶段绑定和指纹序列化。
- `RobWorkStudio/gtest/rws/WorkflowStageControllerTest.cpp`。
- `RobWorkStudio/gtest/rws/ProjectCreationWizardTest.cpp`。

### 修改

- `RobWorkStudio/src/rws/WorkflowDockLayoutController.cpp`：固定 Tab 顺序，委托阶段状态门控。
- `RobWorkStudio/src/rws/RobWorkStudio.cpp/.hpp`：三个入口统一接入向导和候选项目事务；菜单名称更新。
- `RobWorkStudio/src/rws/ProjectManager.cpp/.hpp`：扩展候选项目事务的 metadata、绑定和回滚保护。
- `RobWorkStudio/src/rws/RobotProjectSourcePackager.cpp/.hpp`：接收 URDF 导入 options，支持关闭 Mesh 和缺失 Mesh 回退。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp/.hpp`：接收统一 options，显式处理 Mesh policy。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp/.hpp`：增加目标设备/TCP/伴生文件 options 的公开转换入口。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp/.hpp`：新增无对话框向导 API 和模型发布状态通知。
- `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp/.hpp`：发布冻结工件状态和需求指纹。
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp/.hpp`：发布运动学验证工件。
- `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp/.hpp`：要求运动学工件，并接收变量预勾选种子。
- 四个插件的 `plugin.json`、对应 CMakeLists 和现有 GTest。
- `RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp`、`RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`、`RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`。
- `docs/RobotModelBuilder.md`、`docs/RobotModelBuilderDynamics.md` 及新增项目工作流文档。

## 阶段与数据契约

### 阶段枚举

```cpp
enum class WorkflowStage {
    Modeling = 0,
    Requirements,
    Kinematics,
    StructuralOptimization
};

enum class WorkflowStageState {
    Locked,
    Available,
    Complete,
    Stale
};
```

### 项目资源要求

| 阶段 | 必需资源/条件 | 完成证据 |
|---|---|---|
| Modeling | `robot-model.main`；`mainWorkCell` 可选 | 模型指纹、场景指纹一致且主场景已加载 |
| Requirements | `engineering-requirements.main` | frozen artifact 已保存；包含模型/场景指纹 |
| Kinematics | `kinematic-analysis.main` | `kinematic-validation.main` 已发布；包含模型、需求指纹和验证摘要 |
| Structural Optimization | `structure-optimization.main` | 模型、需求、运动学指纹全部匹配 |

`kinematic-analysis.main` 仍保存用户配置；新增的 `kinematic-validation.main` 才是“运动学已完成”的门控工件。结构优化资源依赖 `scene.main`、`robot-model.main`、`engineering-requirements.main` 和 `kinematic-validation.main`。

### 失效传播

- 模型发布、模型文件指纹变化或主 WorkCell 变化：锁定 Requirements/Kinematics/Optimization。
- 需求编辑或 unfreeze：锁定 Kinematics/Optimization。
- 运动学配置变化、验证结果失效或模型指纹变化：锁定 Optimization。
- 关闭项目：清空所有阶段状态和插件上下文。

## Task 1: 先锁定现有行为的回归测试

**Files:**

- Modify: `RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp`
- Modify: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`
- Modify: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`

- [ ] 写出当前顺序、项目关闭、模型加载、候选项目回滚的基线测试，并把测试中的期望顺序改为未来目标顺序的 RED 版本：`RobotModelBuilder → EngineeringRequirements → KinematicAnalysis → StructureOptimizer`。
- [ ] 增加失败场景：只存在 `robot-model.main` 时 Requirements 可用、Kinematics/Optimization 不可用；只有冻结需求而没有运动学验证时 Optimization 不可用。
- [ ] 增加失效场景：模型指纹变化后下游全部锁定；需求 unfreeze 后 Kinematics/Optimization 锁定。
- [ ] 使用现有 CMake 目标编译并运行单独测试，确认新断言在实现前失败。

Run from the Visual Studio x64 developer environment:

```powershell
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_sdurws-gtest
$env:QT_QPA_PLATFORM='windows'
& 'D:/10_Source_Repos/21_robot/RobWork/RobWork/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/sdurws_sdurws-gtest.exe' --gtest_filter='WorkflowDockLayout.*:RobWorkStudioTest.*:ProjectSystemTest.*'
```

## Task 2: 实现 WorkflowStageController

**Files:**

- Create: `RobWorkStudio/src/rws/WorkflowStageController.hpp`
- Create: `RobWorkStudio/src/rws/WorkflowStageController.cpp`
- Create: `RobWorkStudio/gtest/rws/WorkflowStageControllerTest.cpp`
- Modify: `RobWorkStudio/src/rws/CMakeLists.txt`

- [ ] 定义 `WorkflowProjectSnapshot`，包含项目根目录、资源路径、资源 fingerprints、当前主 WorkCell、插件完成状态。
- [ ] 实现 `evaluate(snapshot)`，每个阶段返回 `state`、`reason`、`requiredResourceIds`、`modelFingerprint`、`requirementFingerprint`。
- [ ] 实现 `invalidateFrom(WorkflowStage stage)`，只清除该阶段及其后的完成证据。
- [ ] 实现稳定的 `stageChanged(WorkflowStage, WorkflowStageState)` 信号或回调，使主窗口能够刷新 Dock 和 Tab。
- [ ] 测试四种正常阶段、模型变化、需求变化、运动学变化、缺失资源、资源指纹不匹配。

## Task 3: 修正 Dock Tab 排列和身份识别

**Files:**

- Modify: `RobWorkStudio/src/rws/WorkflowDockLayoutController.cpp/.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudioPlugin.cpp/.hpp`
- Modify: `RobWorkStudio/gtest/rws/WorkflowDockLayoutControllerTest.cpp`

- [ ] 给每个 workflow Dock 设置稳定 `objectName`：`workflow.modeling`、`workflow.requirements`、`workflow.kinematics`、`workflow.optimization`。
- [ ] 将 `LeftWorkflowDockNames` 改成 Builder、Requirements、Analysis、Optimizer，并按该顺序执行 `tabifyDockWidget`。
- [ ] 删除通过 `tabText()` 判断 workflow Tab 的逻辑，改为 Dock objectName 或插件稳定 ID。
- [ ] 将显示文本设为 `1. 建模`、`2. 需求`、`3. 运动学`、`4. 结构优化`，内部逻辑不依赖本地化文字。
- [ ] 将 `setReady(bool)` 替换为 `applyStageSnapshot()`，只允许当前阶段和已完成阶段启用。
- [ ] 保留 Jog 右侧 Dock 的独立行为。
- [ ] 将布局版本号从 `7` 升级为 `8`，避免旧 Qt 布局恢复出错误的 Tab 顺序。

## Task 4: 统一项目级绑定和资源指纹

**Files:**

- Create: `RobWorkStudio/src/rws/WorkflowBinding.hpp/.cpp`
- Modify: `RobWorkStudio/src/rws/ProjectManifest.hpp`、`RobWorkStudio/src/rws/ProjectManifestJson.cpp`
- Modify: `RobWorkStudio/src/rws/ProjectManager.cpp/.hpp`
- Create: `RobWorkStudio/gtest/rws/WorkflowBindingTest.cpp`

- [ ] 定义 `WorkflowBinding`：project ID、target device、TCP frame、scene resource ID、model resource ID、source kind、source fingerprint、schema version。
- [ ] 将绑定保存为 `workflow/binding.json`，资源 ID 固定为 `workflow-binding.main`，ownership 为 `generated`，required 为 true。
- [ ] 所有资源路径继续使用 project-relative；打开项目时校验路径、资源存在性和 JSON schema。
- [ ] 项目复制、另存为、导出/导入 rwpack 时同步复制并重写绑定中的相对路径，保留稳定资源 ID。
- [ ] 增加兼容逻辑：旧项目没有绑定时，从 `mainWorkCell` 和 `robot-model.main` 推导 device；TCP 由插件首次打开时要求确认并保存。

## Task 5: 重构 New Project 向导

**Files:**

- Create: `RobWorkStudio/src/rws/ProjectCreationWizard.hpp/.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp/.cpp`
- Modify: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] 将 New Project 页面拆为 Project Name、Location、Template 三个字段；Project Name 写入 manifest，不能覆盖机器人 `robotName`。
- [ ] 增加模板描述结构：stable ID、显示名、预览信息、创建函数；初版注册 `generic-six-axis`，后续可加入 UR10/SCARA。
- [ ] 将模板选择结果传给 `bootstrapNewRobotProject(projectRoot, templateId)`；保留 `makeDefaultSixAxisModel` 作为 generic 模板实现。
- [ ] 向导确认前执行路径、覆盖、权限和插件能力预检。
- [ ] 使用现有 snapshot/restore/rollback 机制创建候选项目；成功后只加载 Builder 草稿并显示 `1. 建模`。
- [ ] 不自动生成或发布 `mainWorkCell`；用户修改后仍通过 `Save and Load` 发布。
- [ ] 测试取消、非法路径、重复项目、模板不存在、bootstrap 失败以及当前项目恢复。

## Task 6: 重构 URDF/Xacro 导入管线

**Files:**

- Create: `RobWorkStudio/src/rws/RobotProjectImportOptions.hpp`
- Modify: `RobWorkStudio/src/rws/RobotProjectSourcePackager.hpp/.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp/.hpp`
- Modify: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] 定义 `MeshImportMode { Disabled, VisualOnly, VisualAndCollision }` 和 `MissingMeshPolicy { Fail, GenerateCylinder }`。
- [ ] 定义 `packageRoots`、source kind、xacro expansion result、generated fallback list，并由向导一次收集。
- [ ] 对 `.xacro` 先执行受控展开，保存原始 `.xacro`、展开后的 `sources/robot/robot.urdf` 和展开命令/版本摘要；禁止网络 URI 和未展开宏。
- [ ] 修改 packager：`Disabled` 时不解析/复制 Mesh；`GenerateCylinder` 时记录缺失几何，不让项目事务失败。
- [ ] 修改 importer：缺失几何时生成确定性圆柱 `fallback_<link>_<index>`，并把 warning 写入导入结果。
- [ ] 使用用户提供的 `packageRoots`，并在原始预检、staged URDF 预检、commit 前验证三处复用相同配置。
- [ ] 向导 Step 3 展示关节树；预勾选结果写入 `structure-optimization-seed.main`，不修改 RobotModelSpec 指纹。
- [ ] 完成后调用 `importFile`、`applyImportedProjectModel`，自动显示 Builder；不调用 `publishAndLoad`。
- [ ] 测试纯运动学导入、缺失 Mesh fail、圆柱回退、关闭几何、packageRoots、xacro 展开失败和回滚。

## Task 7: 重构 WorkCell 导入和伴生文件绑定

**Files:**

- Create: `RobWorkStudio/src/rws/WorkCellProjectImportOptions.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp/.cpp`
- Modify: `RobWorkStudio/src/rws/ProjectManager.cpp/.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp/.hpp`
- Modify: `RobWorkStudio/gtest/rws/ProjectSystemTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] 定义 `WorkCellProjectImportOptions`：source WorkCell、companion overrides、target device ID、TCP frame、allow external dependency、copy policy。
- [ ] 将 `mergeCompanionXmlMetadata` 保持 private；新增公开 `WorkCellConverter::convert(workcell, state, saveDirectory, options, warnings)`，内部统一调用伴生 XML 合并逻辑。
- [ ] 将 `extractSerialDevice` 改为按指定 device ID 选择；没有指定时，多设备场景必须要求用户选择，不能静默取第一个。
- [ ] 校验 Target Device 和 TCP Frame 存在于复制后的 WorkCell；将绑定写入 `workflow-binding.main`。
- [ ] WorkCell 文件、CollisionSetup、ProximitySetup、DWC、Include 和几何资产统一进入 project manifest；外部路径必须先复制并重写为项目内相对路径。
- [ ] 对 DWC 不再只按机器人名称猜文件；向导列出候选文件并允许显式选择，未选中则保持动力学关闭。
- [ ] 复制完成后执行 WorkCellLoader 验证，再执行 Converter 预览；任一失败都删除候选项目并恢复旧项目。
- [ ] 成功后显示 Builder 并进入 `1. 建模`，但保留原始 WorkCell 来源资源用于审计。
- [ ] 测试多设备、TCP 不存在、多个 DWC、手动 companion override、外部依赖、绝对路径拒绝和回滚。

## Task 8: 需求阶段发布和门控

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsPlugin.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp/.hpp`
- Modify: `RobWorkStudio/src/rws/WorkflowStageController.cpp`
- Modify: `RobWorkStudio/gtest/rws/RobWorkStudioTest.cpp`

- [ ] 冻结前强制解析 `workflow-binding.main`、`robot-model.main` 和主 WorkCell，校验 Device/TCP 与当前场景一致。
- [ ] 冻结工件保存模型 fingerprint、scene fingerprint、binding fingerprint、state provenance 和 requirement fingerprint。
- [ ] 冻结成功后通知主窗口，主窗口只在项目保存事务提交成功后将 Requirements 标记为 Complete。
- [ ] 编辑、unfreeze、模型切换或场景切换立即发送 `invalidateFrom(Requirements)`。
- [ ] 保留现有 Must/Should/Info 诊断规则，不因新增门控改变编译语义。

## Task 9: 运动学验证工件

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp/.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicValidationArtifact.hpp/.cpp`
- Modify: `RobWorkStudio/src/rws/WorkflowStageController.cpp`
- Create: `RobWorkStudio/gtest/rws/KinematicValidationArtifactTest.cpp`

- [ ] 区分“分析配置”和“验证完成”：配置保存到 `kinematic-analysis.main`，验证结果保存到 `kinematic-validation.main`。
- [ ] 验证结果至少包含模型 fingerprint、需求 fingerprint、scene fingerprint、Device、TCP、运行参数、通过/失败状态、诊断列表和 schema version。
- [ ] 运动学插件只在 Requirements 工件冻结且指纹一致时允许执行验证。
- [ ] 任何配置修改、需求变化、模型变化或场景变化都删除/标记 stale 的验证结果。
- [ ] 验证成功后通知 WorkflowStageController，允许进入结构优化。

## Task 10: 结构优化绑定和变量预勾选

**Files:**

- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp/.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationSeed.hpp/.cpp`
- Modify: `RobWorkStudio/gtest/rws/StructureOptimizationTest.cpp`

- [ ] 结构优化打开前校验模型、需求、运动学验证三组 fingerprint 一致。
- [ ] 从 `structure-optimization-seed.main` 读取向导预勾选项；若无 seed，沿用当前自动建议，但显式标注“自动建议”。
- [ ] 变量 ID 使用稳定的 `targetName + kind + axis` 规则，避免导入后重命名导致错误绑定。
- [ ] 结构优化资源增加 `kinematic-validation.main` 依赖。
- [ ] 模型陈旧度继续由 `RobotModelStalenessChecker` 报告，但从非阻塞提示提升为运行前阻断，除非用户创建新的优化快照。
- [ ] 测试缺少验证工件、指纹不匹配、seed 预勾选、变量重命名和 stale 模型阻断。

## Task 11: 三个向导的统一 UX 和异步行为

**Files:**

- Modify: `RobWorkStudio/src/rws/ProjectCreationWizard.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`
- Modify: 三个插件 Widget 的状态提示代码

- [ ] 每个向导都使用相同步骤导航：Back、Next、Cancel、Finish；Finish 只在预检通过时启用。
- [ ] 文件解析、Mesh 扫描、WorkCell 依赖复制和 Xacro 展开放入 `QFutureWatcher`，所有 UI 更新回主线程。
- [ ] 后台任务期间禁止关闭项目、切换输入文件和重复 Finish；取消时清理 staging 目录。
- [ ] 错误消息区分“输入无效”“资产缺失”“项目路径不可写”“插件能力缺失”“发布失败”。
- [ ] 完成后显示导入摘要：设备、关节数、TCP、Mesh 数量、回退数量、伴生文件和 warning 数量。
- [ ] 不把帮助性说明写成长期占据界面的说明卡；使用步骤标题、状态标签和错误摘要即可。

## Task 12: 迁移、文档和兼容性

**Files:**

- Modify: `docs/RobotModelBuilder.md`
- Modify: `docs/RobotModelBuilderDynamics.md`
- Create: `docs/WorkflowProjectLifecycle.md`
- Modify: `RobWorkStudio/src/rws/ProjectManifestJson.cpp`
- Modify: all affected plugin `README.md`

- [ ] 写明四阶段状态、资源 ID、指纹规则、发布顺序、失败回滚和旧项目迁移行为。
- [ ] 旧项目首次打开时生成 `workflow-binding.main`；无法确定 Device/TCP 时停在 Builder 并要求用户确认。
- [ ] 旧项目没有 `kinematic-validation.main` 时，Kinematics 可用但 Optimization 保持锁定，直到用户重新验证。
- [ ] 旧项目没有结构 seed 时使用现有自动建议，不修改现有模型文件。
- [ ] 增加项目 manifest schema version，并对未知字段保持向前兼容。

## Task 13: 集成验证和发布门槛

**Files:**

- Modify: affected CMakeLists and CTest registration.
- Test: all new and existing project/workflow tests.

- [ ] 运行 model-only 测试：RobotModelXmlWriter、RobotModelUrdfImporter、WorkCellConverter、ProjectSystem、WorkflowStageController。
- [ ] 运行 Qt Widget/Meta 测试：每个可执行文件单独运行，使用 Visual Studio x64 developer environment、`$env:QT_QPA_PLATFORM='windows'`，并使用绝对路径。
- [ ] 运行完整 RobWorkStudio 项目测试，确认旧 WorkCell 单资源打开、项目打开/保存、Save As、rwpack 导出/导入、自动恢复不回归。
- [ ] 手工验证三条入口：新建模板、URDF 无 Mesh、URDF package Mesh、WorkCell 多设备、多个 companion、错误回滚。
- [ ] 手工验证阶段门控：模型修改、需求 unfreeze、运动学失败、切换项目、关闭项目、恢复旧项目。
- [ ] 只有以下条件全部满足才合并：所有现有测试通过；新增阶段测试通过；没有未绑定外部资源；所有完成工件指纹可追溯；失败路径不改变旧项目。

## 推荐实施顺序

1. Task 1：先建立回归测试和目标行为。
2. Task 2-4：完成阶段状态、Tab 身份和项目绑定基础。
3. Task 5：先落地 New Project，因为它最少依赖外部资产。
4. Task 6：改造 URDF/Xacro 打包和导入策略。
5. Task 7：改造 WorkCell 多设备和伴生文件导入。
6. Task 8-10：接通需求、运动学、结构优化工件链。
7. Task 11-13：完善异步 UX、迁移、文档和集成验证。

## 方案验收结果

完成后，用户无论从 New Project、Robot URDF 还是 WorkCell 进入，都得到同一项目结构：

```text
workflow/binding.json
robot-model.main
scene.main
engineering-requirements.main
kinematic-analysis.main
kinematic-validation.main
structure-optimization-seed.main
structure-optimization.main
```

主界面始终显示：

```text
1. 建模  →  2. 需求  →  3. 运动学  →  4. 结构优化
```

用户可以回退修改，但任何修改都会通过 fingerprint 和资源依赖自动锁定受影响的后续阶段，避免数据上下文断裂。
