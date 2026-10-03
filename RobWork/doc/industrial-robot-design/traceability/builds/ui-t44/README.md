# UI-T44 实施留痕（右 Dock 诊断摘要半区填实＋右栏占位退役）

分支 `ui-t44`（基点 redesign-main@1a3c9cd7）；任务契约 `tasks/foundation/UI-T44.json`。

## 改动面

- **D1 诊断摘要填实**：新增 `DomainReadinessSummaryCard`（ui 公共头＋实现）——跨域就绪摘要卡（标题＋逐域"域名：判定词（附注）"行）；判定词映射 `engineeringStatusDisplayName` 经 UiText 新增 `verdict.*` 四键（可行/工程不可行/输入不完整/待提交后判定，与 core::EngineeringStatus 一一对应）。宿主装配：UiPlugin 右 Dock 检查器与右栏之间挂卡；`refreshReadinessSummary` 三域 §11.2 `readonlyProjections` 现取投卡，挂 `refreshSharedSurfaces` 编排首位（项目打开/关闭/应用/撤销/重做/装配首刷全覆盖）。
- **D2 撤占位**：WorkbenchContent 右栏两条"本阶段将在后续版本提供"占位标签退役（属性编辑半区已由共享检查器承载——B1-SPEC D5；策略摘要卡保留）；右栏注释同步收口为 UI-T44 后形态。

## 测试（`tests/`，t＝集成树、s＝冒烟树）

| 套件 | 集成 | 冒烟 |
| --- | --- | --- |
| sdurws_ird_ui_test | 239/239（t1） | 239/239（s1） |
| sdurws_ird_ui_gui_test | **70/70**（67＋3 新用例，t2） | 70/70（s2） |
| sdurws_ird_ui_contract_test | 32/32（t3——链接图零变化） | — |
| sdurws_ird_modeling_gui_test | 9/9（t4——宿主装配回归面） | — |

新增用例（`DomainReadinessSummaryCardGuiTest.cpp`）：`VerdictMapping_CoversFourStatuses_UX02_UI_T44`（词表对账＋工程用语抽验）、`SetRows_RendersDomainVerdictLines_UI_T44`（逐域行＋附注不重复语义）、`SetRows_EmptyRows_HonestEmptyState_ERR01_UI_T44`（诚实空态）。

## 构建证据

- `build-integrated.log`：集成五目标零错误；`smoke-configure.log`/`smoke-build.log`：冒烟冷树（`build-smoke-ui-t44`）零错误。
- `validate-task` PASS（UI-T44）；`validate-docs.log` PASS；右栏两占位词形全仓零残留（grep 实证）。

## 诚实边界

- 卡随共享面刷新点重取——编辑未应用的中间态不实时投递（域面板就绪条承载实时明细，摘要卡只做跨域总览，不冒名实时）。
- 域装配缺席（§11.3 失败隔离）＝该域零行，卡呈剩余域如实汇总；三域全缺席＝诚实空态行。
- O-31 纪律：卡消费面只见行值＋core::EngineeringStatus（表内登记边），零域模块类型依赖；测试替身不引入 project 完整类型（ui gui_test 目标对 project 零包含——实施期以替身模块形态尝试后按纪律撤除，改为行值直投测试）。
