# RobWorkStudio 项目系统第三阶段实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 RobotModelBuilder、EngineeringRequirements 和 StructureOptimizer 的权威 JSON 文档接入 `*.rwproj` 的 Provider 生命周期。

**Architecture:** 每个插件拥有一个 `ProjectDocumentProvider`，主窗口只负责注册、保存事务、关闭确认和标题刷新。Provider 将清单资源 ID、项目相对路径和依赖关系转换为插件已有的 JSON 读写接口；业务 Widget 保持其领域模型与编辑行为，不依赖 `ProjectManager`。

**Tech Stack:** C++、Qt Widgets/Core、GoogleTest、现有 RobotModelSpecJson/RequirementSetJson/StructureOptimizationProjectAdapter。

---

## 实施约束

- 所有新增或修改的核心逻辑使用详细中文注释，说明设计原因、边界和失败策略。
- 严格遵循测试驱动：先让新增测试因 Provider 未实现而失败，再写最小实现。
- 不改写用户已有的业务 JSON Schema；资源 ID 与依赖关系由 `*.rwproj` 清单维护。
- 项目内业务 JSON 禁止保存项目根目录外的绝对模型路径；加载时由 Provider 解析依赖资源路径。
- 原有文件对话框保留为项目外导入或导出副本入口，不得绕过项目保存事务覆盖活动项目资源。
- 本阶段不创建 Git 提交，工作区保留可审查差异。

### Task 1: 主窗口 Provider 注册边界

**Files:**
- Modify: `RobWorkStudio/src/rws/ProjectDocumentRegistry.hpp`
- Modify: `RobWorkStudio/src/rws/ProjectDocumentRegistry.cpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.hpp`
- Modify: `RobWorkStudio/src/rws/RobWorkStudio.cpp`
- Test: `RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp`

- [x] 先新增“注册 Provider 后按 kind 可查询、注册重复 kind 被拒绝”的失败测试。
- [x] 公开 `RobWorkStudio::registerProjectDocumentProvider` 与 `notifyProjectDocumentChanged`；注册失败保留错误文本，通知只刷新标题，不直接保存资源。
- [x] 运行 `sdurws_sdurws-gtest --gtest_filter=ProjectDocumentRegistryTest.*`。

### Task 2: RobotModelBuilder Provider

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelProjectDocumentProvider.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelProjectDocumentProvider.cpp`
- Modify: `RobotModelBuilderWidget.hpp/.cpp`
- Modify: `RobotModelBuilderPlugin.hpp/.cpp`
- Modify: `robotmodelbuilder/CMakeLists.txt`
- Test: `RobotModelBuilderWidgetTest.cpp`

- [x] 新增失败测试：`robwork.robot-model` 通过 Provider 往返 JSON，保存后才清除脏状态。
- [x] Widget 暴露项目加载、暂存保存和 JSON 快照脏状态接口；项目加载不弹文件对话框，保存只写 Provider 给定暂存路径。
- [x] 插件初始化后注册 Provider，并在用户编辑后通知主窗口刷新项目标题。
- [x] 运行 RobotModelBuilder 专项测试。

### Task 3: EngineeringRequirements Provider

**Files:**
- Create: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsProjectDocumentProvider.hpp`
- Create: `RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsProjectDocumentProvider.cpp`
- Modify: `EngineeringRequirementsWidget.hpp/.cpp`
- Modify: `EngineeringRequirementsPlugin.hpp/.cpp`
- Modify: `engineeringrequirements/CMakeLists.txt`
- Test: `EngineeringRequirementsTest.cpp`

- [ ] 新增失败测试：需求文档从清单依赖解析机器人模型和 WorkCell；保存时写项目相对模型路径。
- [x] Widget 复用冻结证据校验，Provider 仅提供已解析依赖路径；模型或 WorkCell 缺失时返回明确错误且不替换当前需求。
- [x] 原“加载/保存需求”按钮改为项目外导入/导出副本，活动项目文档只由主窗口保存。
- [x] 运行工程需求专项测试。

### Task 4: StructureOptimizer Provider

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationProjectDocumentProvider.hpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationProjectDocumentProvider.cpp`
- Modify: `StructureOptimizerWidget.hpp/.cpp`
- Modify: `StructureOptimizerPlugin.hpp/.cpp`
- Modify: `structureoptimizer/CMakeLists.txt`
- Test: `StructureOptimizationTest.cpp`

- [ ] 新增失败测试：优化文档依赖模型、WorkCell 和可选冻结需求，保存后可重新加载且资源 ID 不依赖绝对路径。
- [x] Widget 暴露项目加载、事务暂存保存、运行中拒绝关闭和快照脏状态；Provider 复用既有 `StructureOptimizationProjectAdapter`。
- [x] 将打开/保存优化项目按钮改为导入/导出副本，项目资源由统一保存事务写入。
- [x] 运行结构优化专项测试。

### Task 5: 集成验证与文档

**Files:**
- Modify: `RobWorkStudio/gtest/CMakeLists.txt`
- Modify: `RobWorkStudio/gtest/rws/ProjectDocumentRegistryTest.cpp`
- Modify: `docs/superpowers/plans/2026-07-31-robworkstudio-project-system.md`

- [ ] 新增跨 Provider 集成测试，确认依赖顺序、全部脏资源保存和任一 Provider 暂存失败时保持正式文件不变。
- [ ] 构建 `RobWorkStudio` 及全部三个业务插件测试目标，运行阶段一至三项目测试。
- [ ] 更新阶段三复选项和验收标准，执行 `git diff --check`。
