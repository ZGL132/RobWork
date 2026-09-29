# UI-T23 验收对抗项前置自查：PIPE §6.2 共享 UI 文件互斥合规审计（三域迁移任务允许面）

| 字段 | 值 |
| --- | --- |
| 审计对象 | B.1 迁移链五项已合并任务的**实现域触碰面**（UI-T21、UI-T22、WP-13-T20、WP-14-T10、WP-15-T18）——对照其任务契约 allowedFiles 与 PIPE §6.2 互斥红线（"域迁移任务只允许修改本域目录与域契约明确的接缝文件；共享面变更只发生在 UI-T21/UI-T22/UI-T23 三个 ui 任务中"） |
| 审计方法 | git show --name-only 逐主交付提交（acceptanceHead＝各任务 history.commits 首条）枚举触碰文件；域迁移三任务（WP-13-T20/WP-14-T10/WP-15-T18）重点核对 ui/ 单元零触碰 |
| 审计结论 | **零越界**：三域迁移任务全部触碰在本域目录＋traceability 留痕，共享 UI 装配面（ui/ 单元）零修改；UI-T21/T22 触碰面在 ui/ 单元＋自身卡（其契约即共享面任务，PIPE §6.2 允许方） |
| 本任务自查 | UI-T23 自身触碰面全部在 allowedFiles 内：`industrialrobot/ui/**`（plugin/src/include/test/CMakeLists）＋`units/ui.md`＋`traceability/**`；forbiddenFiles（三域产品目录/框架/上游两文档）零触碰（git diff 核对） |

## 逐任务触碰面（主交付提交）

### WP-13-T20（建模迁移，0cb8d53b）

`modeling/` 域内：CMakeLists、app/HarnessMain.cpp、assembly/…/ModelingPluginAssembly.hpp、plugin/（HostMigrationProviders、ModelingPanelWidget、ModelingPluginAssembly、ModelingUiModule）、test/（HostMigrationModuleTest、HostMigrationTest）＋单元卡/traceability——**零 ui/ 触碰**。

### WP-14-T10（需求迁移，d84008e3）

`requirements/` 域内：CMakeLists、app/RequirementsHarnessMain.cpp、contract_test/BuildGraphContractTest.cpp、plugin/（HostMigrationProviders、RequirementsPanelWidget、RequirementsUiModule）、test/HostMigrationTest.cpp＋traceability——**零 ui/ 触碰**。

### WP-15-T18（运动学迁移，7e015c6f）

`kinematics/` 域内：CMakeLists、app/HarnessMain.cpp、assembly/…/KinematicsPluginAssembly.hpp、plugin/（KinHostMigrationProviders、KinematicsPanelWidget、KinematicsPluginAssembly、KinematicsUiModule）、test/HostMigrationTest.cpp＋traceability——**零 ui/ 触碰**。

### UI-T21／UI-T22（共享面任务本体）

触碰面在 `industrialrobot/ui/`（SelectionService/IndustrialProjectTree/PropertyInspector 及其测试）＋units/ui.md＋traceability——属 PIPE §6.2 允许方（共享面变更只归 UI-T21/T22/T23），各自验收已过（history pass-merged），此处仅备案。

## 备注

- O-45 裁决（WP-14-T11 需求域装配门面补建）触碰 `requirements/assembly/`＋`requirements/plugin/`——属所有者裁决的域内接缝文件（"只出线不重写"），与本审计结论不冲突（域内接缝文件＝PIPE §6.2 允许的"域契约明确的接缝文件"）。
- 本审计为验收段对抗项（契约 acceptance 7）的**实施侧自查**——验收者应独立复核（重跑 git show 枚举）。
