# 方案 A 正式产品路径退役清单草案（UI-T23 acceptance 6——交 WP-24-T09 执行）

| 字段 | 值 |
| --- | --- |
| 依据 | 任务契约 tasks/foundation/UI-T23.json acceptance 6（"deprecated 自持导航清单固化＋退役清单草案产出——方案 A 顶层窗口路径、deprecated 导航、临时桥接件逐项登记——交 WP-24-T09 执行"）；B1-SPEC §5.4（退役四条件） |
| 地位 | **草案**——本清单是 WP-24-T09 的输入，退役决定权与执行权在 WP-24-T09（UI-T23 只集成收口不退役——契约红线）；条目以三域迁移落位形态与集成收口实测为准，WP-24-T09 执行时逐项复核后处置 |
| 退役前置（B1-SPEC §5.4 复述） | ①WP-24-T08 正式装配为唯一装配路径（SA-01 不变）；②三域迁移验收全 pass＋UI-T23 集成收口 pass（本任务即②的最后一项）；③本清单逐项登记经所有者确认；④`sdurws_ird_ui_plugin` 动态通道与各 `_app` harness 处置按 D13 |

## 一、方案 A 顶层窗口路径（B1-SPEC D1/D2/D13）

| # | 对象 | 现状（集成收口实测） | 退役动作（归 WP-24-T09） |
| --- | --- | --- | --- |
| A-1 | `sdurws_ird_ui_app`（ui harness 顶层窗口） | 产品形态声明"宿主为唯一主窗口"后降格为纯开发验证通道（D13；UI-T19 落位）；其五区交互与插件内容装配面同源 | 决定退役或保留为回归工具；保留则移出产品交付路径声明（DTB §5.1 口径复查）＋安装树排除 |
| A-2 | `sdurws_ird_modeling_app`／`sdurws_ird_requirements_app`／`sdurws_ird_kinematics_app`（域 harness） | 迁移演示与 GUI 冒烟载体（--auto 自动化冒烟仍在用——WP-14-T10/WP-15-T18 验收口径） | 同 A-1 统一处置（D13"各域 `_app` 退役决定归 WP-24-T09"）；保留时其 --auto 冒烟序列随回归面登记 |
| A-3 | WorkbenchShell 顶层 QMainWindow 形态分支（WorkbenchHostKind::TopLevelWindow） | 纯开发验证通道登记（UI-T19）；产品装配只走 EmbeddedDock | 代码路径保留或收敛随 WP-24-T09 复核（不属删除必选——D11"不整体重写"的反向约束） |

## 二、deprecated 自持导航（B1-SPEC §5.2"标记保留可用，删除归 WP-24-T09"）

| # | 对象 | 标记载体（objectName／冻结文案） | 落位任务 |
| --- | --- | --- | --- |
| B-1 | 建模面板自持导航 | `ird_modeling_nav_deprecated_marker`（WP-13-T20 落位） | WP-24-T09 删除标记与自持导航区 |
| B-2 | 需求面板自持导航 | `requirementsNavDeprecationLabel`（WP-14-T10 落位；--auto 冒烟步 1 断言其在场） | 同上 |
| B-3 | 运动学面板自持导航 | `kinematicsNavDeprecationLabel`（WP-15-T18 落位；v1 措辞"接入面已注册但暂无本域树对象，本面板保留可用"） | 同上＋随域树对象落位复核措辞 |

## 三、迁移期临时桥接件（集成收口实测盘点）

| # | 对象 | 现状 | 处置建议（归 WP-24-T09 裁量） |
| --- | --- | --- | --- |
| C-1 | 共享面 NameMap 空映射适配器（`HostEmptyNameMapPort`——ui/plugin 内部） | L3 反解失败分支的宿主常态承载（诚实二态）；真映射注入点单一（SelectionService::Deps.nameMap） | WP-24-T08 呈现装配注入真 RuntimeNameMap 端口后，空适配器降为无装配兜底——退役随呈现装配完成复核 |
| C-2 | requirements 编辑器会话缺席（attachEditor(nullptr)——DomainAssembly 诚实边界） | 树供给空集／buildDraftCommand nullopt 的合法二态；编辑器基线数据面随域会话任务 | 域会话任务落位后本临时形态自然消解；WP-24-T09 复核残留 |
| C-3 | kinematics 服务缝缺席（KinPanelServices 不注入——R-2 私有类型不可达） | 面板降级语义如实；D8 桥 applyHostJointState 写入返回 false 降级留痕 | 服务缝公共化任务落位后注入载体；退役随载体注入完成复核 |
| C-4 | DomainRevisionEventBridge 的 modeling 单域转发 | 修订事件→建模基线前移（WP-24-T03b 形态；requirements 无 onRevisionCommitted 门面出口） | 需求域撤销/重做基线同步随其门面增量接续；WP-24-T09 复核 |
| C-5 | `sdurws_ird_ui_plugin` 动态加载通道本体 | 开发期验证通道（O-38/UI-T16 装载呈现自证口径）；正式产品 sdurws_ird_studio 同源装配（SA-02 合规） | **退役主体**（B1-SPEC §2.1 处置表"运行时动态 Load/Unload Plugin＝禁止"）；D13 口径按 WP-24-T09 决定 |

## 四、不退役项（处置表保留——备案防误伤）

- 官方 TreeView/Jog/Playback/Log（B1-SPEC §2.1 保留四项——WP-24-T08 已装配；本任务 L3/D8 桥消费其公开事件面）；
- PropertyView/WorkcellEditorPlugin 排除判据（装配清单不含＋框架侧无独立动态插件产物——WP-24-T08 已核）；
- 方案 A 面板的**业务功能本体**（域面板编辑流等——deprecated 标记≠功能删除；删除的只是导航路径，编辑能力随 D6 复杂编辑页面与共享检查器承载）。
