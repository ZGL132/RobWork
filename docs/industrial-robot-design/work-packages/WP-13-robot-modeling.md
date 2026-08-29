# WP-13 机械臂建模实施计划

**目标：** 在单项目单机械臂范围内建立 `RobotDesign`，从 DH、显式关节和 URDF 生成一致的运行时工件。

**阶段/发布：** 阶段 B，R1；仅实现 OPT-B 所需模型能力。

**需求与契约：** MDL-01～14；AT-01、AT-15～18；引用 `architecture/domain-model.md`、`persistence-schema.md`、`public-interfaces.md`。

**拥有目录：** `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/`、对应测试目录和建模夹具。不得修改 WP-06 的名称解析实现、WP-04 的持久化实现或其他业务插件 Widget。

**输入/输出：** 输入为导入文件、模板或编辑草稿；输出为 `RobotDesignDraft`、`RobotDesign`、`CompiledCandidateArtifact` 和结构化诊断。所有应用命令经 `IProjectCommandService` 提交。

## 任务

1. **WP-13-T01 模型类型与失败夹具**：建立 DH、显式关节、URDF 任意轴、缺失轴、非单位轴、零轴、4/7 轴边界和不支持拓扑的黄金输入；先写失败断言。
2. **WP-13-T02 领域编辑器**：实现 `RobotDesignEditor` 草稿校验、唯一 `robotId`、目标主链选择、关节限制和来源/可信度保留；非法草稿不得产生修订。
3. **WP-13-T03 URDF 导入**：实现字段映射、任意轴和 continuous/prismatic 语义；缺失轴生成 +X 草稿，零轴只生成报告并阻止 Verified。
4. **WP-13-T04 DH 转换**：实现 `DhConversionService`，区分 `Exact`/`ExactNonUnique`/`Approximate`/`NotRepresentable`，派生表示只读且不得取代权威类型。
5. **WP-13-T05 物性与工具**：实现 `MaterialInertiaEstimator`、工具/TCP、负载引用、环境网格和 `ValueProvenance`；路径和资源校验调用 WP-11。
6. **WP-13-T06 运行时编译**：调用 WP-06 完成 WorkCell/DynamicWorkCell 全成或全败编译；不得在本 WP 拼接运行时名称。
7. **WP-13-T07 单机械臂与身份回归**：覆盖复制、导入、删除、重命名、目标链切换和 objectId 稳定性；多可动分支仅保留证据，不进入首版计算模型。
8. **WP-13-T08 插件与 GUI**：提供最薄建模入口、错误定位和应用确认；GUI 不直接写文件或领域对象。

## 任务卡

详见 `agent-tasks/WP-13-T01-model-fixtures.md`～`WP-13-T08-modeling-ui.md`。

## 验证

前置：WP-01 构建入口、WP-02 黄金数据、WP-06 名称解析、WP-10 公共 UI、WP-11 安全导入已通过。

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_test$'
```

必须提交：模型黄金数据、失败诊断样例、编译日志、GUI 回归报告、需求追踪更新和独立评审记录。

## 迁移与删除

迁移旧建模数据前保留只读输入和转换证据；阶段 B 验收后删除旧建模目标及仅验证旧接口的测试。

## 独立评审

由未参与实现的建模工程师和测试人员复核黄金数据、失败诊断、对象身份和 GUI 报告。

## 退出条件

MDL-01～14、AT-01、AT-15～17 及 AT-18 阶段 B 子链路通过；任意编译失败不产生修订；所有对象引用使用 objectId；旧建模目标不进入 R1 安装包。
