# RobWorkStudio 项目文件系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 RobWorkStudio 从以单个 WorkCell 为顶层文档的工作方式，逐步升级为以 `*.rwproj` 项目清单统一组织 WorkCell、机器人模型、工程需求和结构优化文件的工业软件工作方式。

**Architecture:** `*.rwproj` 采用 UTF-8 JSON，仅保存项目元数据、稳定资源 ID、相对路径、入口资源和依赖关系；现有 XML/JSON 继续作为独立权威资源。主程序通过 `ProjectManager` 管理项目生命周期，后续插件通过 `ProjectDocumentProvider` 接入统一加载、保存和脏状态管理。

**Tech Stack:** C++、Qt Core/Widgets、CMake、GoogleTest、RobWorkStudio 插件体系。

---

## 实施约束

- 所有新增和修改的核心逻辑必须包含详细中文注释，重点说明设计原因、边界条件、失败策略和路径安全规则。
- 不修改用户当前未提交的机器人模型与 WorkCell 示例文件。
- 不在本任务中创建 Git 提交；每阶段完成后保留可审查的工作区差异。
- 严格按测试驱动顺序实施：先新增失败测试，再实现最小代码，再运行专项测试和目标构建。
- 每个阶段都必须形成可独立构建、可独立验证的工作状态。

## 阶段一：项目清单与主程序入口

- [x] 为项目清单往返、格式校验和路径安全新增失败测试。
- [x] 实现 `ProjectManifest`、JSON 持久化及 Schema 校验。
- [x] 实现 `ProjectPathResolver`，拒绝项目资源越界路径。
- [x] 实现 `ProjectManager` 的创建、打开、保存和关闭。
- [x] 将项目操作最小接入主窗口，并保留旧的单文件打开入口。
- [x] 运行专项测试和主程序目标构建。

## 阶段二：统一文档注册与脏状态

- [x] 定义 `ProjectDocumentProvider` 和文档注册表。
- [x] 实现 Provider 依赖排序、脏状态汇总和多文件保存事务。
- [x] 将 WorkCell 包装为首个 Provider，并接入标题栏星号和关闭提示。

## 阶段三：接入现有业务插件

- [x] RobotModelBuilder 接入资源 ID、统一打开保存和脏状态。
- [x] EngineeringRequirements 接入模型、WorkCell 依赖及冻结证据校验。
- [x] StructureOptimizer 接入需求、模型和 WorkCell 资源依赖。
- [x] 插件内部文件对话框调整为导入或导出副本入口。

## 阶段四：旧文件迁移与项目另存为

- [ ] 实现基于旧 WorkCell 创建项目。
- [ ] 实现业务 JSON 导入当前项目。
- [ ] 实现项目另存为、克隆和新项目 ID 生成。
- [ ] 验证移动整个项目目录后仍可打开。

## 阶段五：恢复、完整性检查与项目打包

- [ ] 实现 `.rwproject/autosave` 恢复快照。
- [ ] 实现缺失资源、未引用资源和指纹变化诊断。
- [ ] 实现 `*.rwpack` 打包和解包。
- [ ] 验证崩溃恢复与损坏包拒绝策略。

## 第一阶段完成标准

- `*.rwproj` 可以创建、保存、重新打开并保持语义一致。
- 所有项目自有资源路径都以项目目录为根解析，`..` 越界路径会被拒绝。
- 重复资源 ID、缺失入口资源、未知格式和过高 Schema 版本会返回明确错误。
- 主程序可以通过项目文件自动加载主 WorkCell。
- 旧 WorkCell 和 Drawable 打开功能仍然可用。
- 第一阶段新增测试通过，`RobWorkStudio` 主目标构建成功。

## 第二阶段完成标准

- 每种项目资源通过唯一 Provider kind 接入，重复 Provider ID 或资源 kind 会被拒绝。
- 项目资源按依赖顺序加载、按逆序关闭；缺失必需 Provider 时整体拒绝打开项目。
- 所有脏资源先写入同目录暂存文件，全部暂存成功后才替换正式文件；提交失败会回滚已替换资源。
- WorkCell 的加载、保存、脏状态、关闭检查均通过 Provider 纳入项目生命周期。
- 标题栏星号同时反映项目清单和业务文档脏状态，关闭项目或应用时统一执行保存确认。
- “新建空 WorkCell”和“打开单个 WorkCell”会先安全退出当前项目，避免临时资源继续污染原项目状态。
- `ProjectSystemTest.*`、`ProjectDocumentRegistryTest.*` 与 `RobWorkStudio.LaunchTest` 通过，`RobWorkStudio` 主目标构建成功。
