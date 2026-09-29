# 方案 A 正式产品路径退役清单核对表（WP-24-T09 acceptance 1~4 执行留痕）

| 字段 | 值 |
| --- | --- |
| 任务 | WP-24-T09 方案 A 正式产品路径退役（宿主融合收口，B.1 批次序 12 收尾任务；SA-18 D13） |
| 依据 | 契约 tasks/foundation/WP-24-T09.json acceptance 1~5；B1-SPEC §5.4 退役四条件＋§2.1 处置表；UI-T23 移交草案 traceability/builds/wp10-t23/retirement-draft.md；ARCH §7.12/§5.4；DTB §2.25 行＋§5.1 v0.26 行 |
| 分支/基线 | wp24-t09；base＝79d1d8e1c07c4ae9172e8cc33bd8a597d414f5f9 |
| 日期 | 2026-09-29（单会话顺序执行模式——实施角色留痕；验收角色另出记录） |

---

## 1. B1-SPEC §5.4 退役四条件核对（acceptance 1）

| # | 条件 | 结论 | 证据 |
| --- | --- | --- | --- |
| ① | 正式产品装配（WP-24-T08 骨架）以宿主融合形态为唯一装配路径 | **满足**。正式产品主程序 `sdurws_ird_studio`（WP-24-T08 落位）为唯一装配入口；本任务新增安装规则只装 studio＋worker 两目标（见 §3），方案 A 顶层窗口路径不入安装树 | ui/CMakeLists.txt 安装规则段（本分支）；WP-24-T08 验收记录 traceability/acceptance/WP-24-T08-20260928.md |
| ② | 三域迁移验收全部 pass 且 UI-T23 集成收口 pass | **满足**。WP-13-T20（modeling）/WP-14-T10（requirements）/WP-15-T18（kinematics）/WP-14-T11（需求域装配门面补建）/UI-T23（多领域集成收口）全部 pass-merged | state.json history（五条 pass-merged 登记，mergedAt 2026-09-28/29）＋各自验收记录 traceability/acceptance/ |
| ③ | 退役清单逐项登记并经所有者确认 | **满足（确认链声明）**。清单本体＝UI-T23 移交草案 retirement-draft.md（A/B/C 三组逐项登记）＋本核对表 §2 逐项复核处置；确认链＝①B1-SPEC §2.1 处置表 v1.1 冻结（所有者 2026-09-27 批准方案 B.1/SA-18，处置表为封闭清单——本执行**未越出处置表口径**）；②契约 ready 放行（2026-09-27 独立评审 PASS＋CCP §3.1 原子放行，编译日志放行块）；③退役决定权与执行权按草案"地位"条款归本任务（UI-T23 契约 acceptance 6 移交）。**诚实边界**：执行侧逐项复核结论全部落在处置表与草案既定口径内（无新增创造性处置）；如需超出处置表口径的处置（本执行无），须事前所有者裁决——本核对表随验收记录公开，供所有者事后抽查（PIPE §7 单会话独立性降级背景下的监督补偿） | 本表 §2/§3；retirement-draft.md |
| ④ | `sdurws_ird_ui_plugin`/各 `_app` 处置按 D13 登记 | **满足**。处置＝**保留为开发验证通道＋移出产品交付路径**（D13 二选一的保留支）：不入安装规则（物理上不在安装树）、DTB §5.1 v0.26 行登记交付路径声明 | 本表 §3；DTB §5.1"产品交付路径（v0.26，WP-24-T09 处置登记）"行 |

## 2. 退役清单逐项复核处置（UI-T23 草案 A/B/C 组——本任务执行记录）

### 2.1 A 组：方案 A 顶层窗口路径（草案 §一）

| # | 对象 | 复核结论 | 处置 | 依据 |
| --- | --- | --- | --- | --- |
| A-1 | `sdurws_ird_ui_app`（ui harness） | 五区交互与插件内容装配面同源（UI-T15~T18 落位）；开发期 L5 验证与 GUI 冒烟载体仍在用 | **保留为开发验证通道，移出产品交付路径**（不入安装规则）；不物理删除 | D13 二选一保留支；acceptance 4"顺手删除仍被开发验证依赖的 harness 而不留登记＝验收阻断项"的反向适用 |
| A-2 | `sdurws_ird_modeling_app`/`sdurws_ird_requirements_app`/`sdurws_ird_kinematics_app`（域 harness） | 域迁移演示与 `--auto` 自动化冒烟载体（WP-13-T20/WP-14-T10/WP-15-T18 验收口径在用） | 同 A-1 统一处置 | 同 A-1；域 `--auto` 冒烟序列随开发验证通道保留登记 |
| A-3 | WorkbenchShell 顶层 QMainWindow 形态分支（WorkbenchHostKind::TopLevelWindow） | 产品装配只走 EmbeddedDock（WP-24-T08 装配序列）；顶层分支为 harness 载体代码 | **代码路径保留**（随 harness 保留）；不入产品装配路径 | 草案 A-3"不属删除必选——D11『不整体重写』的反向约束"；退役范围外重构禁止（acceptance 4） |

### 2.2 B 组：deprecated 自持导航（草案 §二）

| # | 对象 | 复核结论 | 处置 | 依据 |
| --- | --- | --- | --- | --- |
| B-1 | 建模面板自持导航＋`ird_modeling_nav_deprecated_marker` | 标记与自持导航区落点＝modeling/plugin/ModelingPanelWidget.cpp（**forbiddenFiles**：requirements/modeling/kinematics/runtime 四单元及其 CMakeLists 均在契约 v1.2 撤回通配后的禁改清单） | **保留（标记与自持树原样）**；产品宿主装配面不呈现该区域（域面板自持树仅域 Dock 内）；staging 直启冒烟截图实证标记在场且如实标注 | 契约 v1.2 注记"按 DTB §5.4 转 blocked 报告所有者扩展范围，不得越界"——删除须扩 forbiddenFiles；B1-SPEC §5.2 冻结形态即"标记 deprecated（保留可用）" |
| B-2 | 需求面板自持导航＋`requirementsNavDeprecationLabel` | 落点＝requirements/plugin/RequirementsPanelWidget.cpp（forbiddenFiles）；`requirements/app/RequirementsHarnessMain.cpp` 冒烟步 1 断言其在场（requirements/** 同禁改） | 同 B-1 保留 | 同 B-1；删除将破坏 `--auto` 冒烟断言（开发验证依赖——acceptance 4 反向适用） |
| B-3 | 运动学面板自持导航＋`kinematicsNavDeprecationLabel` | 落点＝kinematics/plugin/KinematicsPanelWidget.{hpp,cpp}（forbiddenFiles）；v1 措辞已如实标注"本面板自持导航保留可用（退役归 WP-24-T09）" | 同 B-1 保留（措辞已含本任务归属，语义如实——"保留可用"即本执行结论） | 同 B-1 |

**B 组处置小结**：三处标记与自持导航区全部保留。删除属 forbidden 单元修改（契约明令不得越界）且破坏开发验证冒烟依赖（acceptance 4 阻断项风险）；标记语义本身已如实呈现"deprecated——保留可用，退役归 WP-24-T09"，本执行将该归属结论落地为"保留＋移出产品交付路径＋DTB 登记声明"，与 B1-SPEC §5.2 迁移终态语义一致。标记文案中"退役归 WP-24-T09"字样指本任务作出退役决定——决定已作出（保留），文案事实不失实；若所有者后续裁决物理删除，随 DTB §5.4 扩围登记另行执行。

### 2.3 C 组：迁移期临时桥接件（草案 §三）

| # | 对象 | 复核结论 | 处置 |
| --- | --- | --- | --- |
| C-1 | 共享面 NameMap 空映射适配器（HostEmptyNameMapPort） | L3 反解失败分支的宿主常态承载（诚实二态）；真映射注入点单一（SelectionService::Deps.nameMap）；WP-24-T08 呈现装配面 v1 未注入真 RuntimeNameMap 端口（B1-SPEC §2.1 与 WP-24-T08 契约的呈现装配增量） | **保留**——空适配器仍为宿主常态兜底；退役随呈现装配真映射注入完成复核（域外落点，非本任务范围） |
| C-2 | requirements 编辑器会话缺席（attachEditor(nullptr)） | 域会话任务未落位，空集/nullopt 为合法二态 | **保留**——随域会话任务自然消解；复核登记无残留缺陷 |
| C-3 | kinematics 服务缝缺席（KinPanelServices 不注入） | R-2 私有类型不可达的诚实降级；D8 桥 applyHostJointState 返回 false 降级留痕 | **保留**——服务缝公共化任务落位后注入载体；非本任务范围 |
| C-4 | DomainRevisionEventBridge 的 modeling 单域转发 | 修订事件→建模基线前移（WP-24-T03b 形态） | **保留**——需求域门面增量接续随其会话任务；非本任务范围 |
| C-5 | `sdurws_ird_ui_plugin` 动态加载通道本体 | 开发期验证通道（O-38）；正式产品 sdurws_ird_studio 同源装配（SA-02 合规） | **退役主体处置＝保留通道＋移出产品交付路径**：安装树零 `_plugin` 产物（staging 清单零命中实证）；动态 Load/Unload 入口在正式产品装配中已移除（WP-24-T08 菜单处置）；通道仅开发期经框架 RobWorkStudio 手动装载使用（O-38 裁决原文） |

### 2.4 不退役项复核（草案 §四——防误伤备案）

官方 TreeView/Jog/Playback/Log 四项宿主组件在 staging 安装树运行形态中正常装配（直启冒烟 Dev 日志＋截图实证）；PropertyView/WorkcellEditor 排除见 §3；方案 A 面板业务功能本体不删（deprecated 标记≠功能删除——编辑能力随 D6 复杂编辑页与共享检查器承载）。

## 3. 处置登记（acceptance 1④／DTB §5.1 口径复查）

| 对象 | 处置 | 产品交付路径 |
| --- | --- | --- |
| `sdurws_ird_studio` | **正式产品主程序**（唯一装配入口） | **入**（安装规则 bin/） |
| `sdurws_ird_execution_worker` | 计算 worker（execution 派发链必要进程） | **入**（安装规则 bin/） |
| `sdurws_ird_ui_app`＋各域 `_app` harness | 开发验证通道（`--auto` 冒烟载体）——保留 | **不入**（安装规则排除） |
| `sdurws_ird_ui_plugin`（MODULE） | 开发期动态加载验证通道（O-38/D13）——保留 | **不入**（安装树零 `_plugin` 动态加载产物——staging 清单零命中实证，acceptance 2③） |
| PropertyView/WorkcellEditorPlugin | 框架侧仅开发验证组件（B1-SPEC §2.1）——正式装配清单不含其静态装配调用＋框架侧无独立动态插件产物（RWS_USE_STATIC_LINK_PLUGINS=ON 静态形态，WP-24-T08 已核＋本任务 staging 清单复核） | **不入**（acceptance 2⑤ 排除判据两半同时成立） |
| 测试目标（`*_test`/`*_contract_test`/`*_gui_test`）、`sdurws_ird_demo6r` | 测试与演示目标 | **不入**（staging 清单零 testkit/testdata/`_app` 命中——acceptance 2④） |

登记落点：DTB §5.1 新增"产品交付路径（v0.26，WP-24-T09 处置登记）"行（交付路径唯一＝studio＋worker；开发验证通道保留但移出）。

## 4. 零未引用目标与废弃构建选项审查（acceptance 3 代码审查清单）

- **构建选项**：industrialrobot 顶层与 cmake/ 目录无 `option()` 定义（实测 grep 零命中）——无迁移期废弃构建开关残留；`RWS_BUILD_INDUSTRIALROBOT` 为框架侧注册开关（非本产品废弃面）。
- **未引用目标**：逐目标核对——产品面（sdurws_ird_* 库八真实 STATIC＋四 INTERFACE 占位）全部被可执行/测试/装配目标消费；`sdurws_ird_studio`（产品）、`sdurws_ird_execution_worker`（worker）、`sdurws_ird_ui_app`/`sdurws_ird_modeling_app`/`sdurws_ird_requirements_app`/`sdurws_ird_kinematics_app`（开发验证，§3 登记）、`sdurws_ird_ui_plugin`（开发验证，§3 登记）、`sdurws_ird_demo6r`（owner 演示指令载体，modeling/CMakeLists.txt §6 登记）——**零未引用目标**；本任务未删除任何目标，ird_gates 两态 65↔65 零差异为"目标图不变"的机器实证。
- **废弃代码路径**：方案 A 顶层窗口路径（WorkbenchShell TopLevelWindow 分支）随 harness 保留（§2.1 A-3）；无死代码新增，无越界重构（acceptance 4）。

## 5. 启动脚本/安装规则/About/依赖清单同步（acceptance 3）

| 项 | 同步内容 | 状态 |
| --- | --- | --- |
| 启动脚本 | start_studio.bat 启动目标 RobWorkStudio.exe→**sdurws_ird_studio.exe**（正式产品），头部注明开发辅助定位（契约 v1.1 注记：硬编码开发机路径，其运行不构成无路径依赖证据） | ✅ 本分支 |
| 安装规则 | ui/CMakeLists.txt 新增全产品唯一 install 段（COMPONENT IRD_PRODUCT；ARCH §5.4 bin/plugins/share 三目录；运行库钉 Release；相对前缀 qt.conf） | ✅ 本分支 |
| About | 核查结论：AboutDialog 数据驱动（白名单∩装配报告现取＋版本基线注入），退役不改变清单语义——staging 直启 About 截图实证三域"已装配"计数与装配日志逐项一致、其余白名单"未装配"如实占位；Log 定位注明已随 WP-24-T08 落位（StudioMain.cpp 装配序列注＋wp24-t08/README.md）；版本基线"未装载"占位为 WP-24-T01 基线接入面（装配任务增量），**非退役范畴——按 acceptance 4 不越界重构**，本任务仅核查登记 | ✅ 核查（无代码变更——正当性如上） |
| 依赖清单 | ird/share/baseline.md（WP-24-T01 冻结版本基线：框架 commit/Qt/编译器/碰撞后端/第三方许可证清单）经安装规则入 share/（只读引用不修改——修订纪律归 WP-24-T01） | ✅ 本分支（安装引用） |

## 6. staging 安装树专项审计（acceptance 2 七项判据）

执行：`cmake --install build --config Release --prefix build/staging-wp24-t09 --component IRD_PRODUCT`（清空目录后执行；组件化排除框架自带未分组安装规则的 include/lib 开发交付物）。

| # | 判据 | 结论 | 证据 |
| --- | --- | --- | --- |
| ① | studio＋worker 来自安装树 | ✅ bin/ 下 sdurws_ird_studio.exe＋sdurws_ird_execution_worker.exe | staging-tree-listing.txt |
| ② | 无开发机构建目录 PATH 依赖（安装树绝对路径直启） | ✅ PATH 剥离 vcpkg/构建树/Qt（只留 System32）直启成功；三域装配/共享面/工作台呈现日志全出线；WM_CLOSE 优雅退出退出码 0 | staging-smoke-run.log＋staging-smoke-1-main-window.png |
| ③ | 无工业业务动态插件目录与 `_plugin` 动态加载产物 | ✅ 清单 grep `ird_ui_plugin/_app/testkit/testdata/PropertyView/WorkcellEditor` 零命中 | staging-tree-listing.txt |
| ④ | 无 `_app`/testkit/testdata 测试交付物 | ✅ 同上零命中 | 同上 |
| ⑤ | PropertyView/WorkcellEditor 排除判定 | ✅ 安装树无其产品入口与专用动态插件产物（排除判据两半：装配清单不含〔WP-24-T08 装配序列〕＋框架侧无独立动态插件产物〔静态链接形态〕） | 同上＋StudioMain.cpp 装配序列注 |
| ⑥ | Qt 平台插件/RobWork/RWSim 运行库/share 按部署结构保留 | ✅ plugins/platforms/qwindows.dll（＋qminimal/qoffscreen）＋plugins/imageformats 四件（styles 目录该 Qt 发行版无独立样式插件，空目录如实登记）；bin/ 44 个 DLL（sdurw_*＋boost＋Qt6 六件＋sdurwsim.dll 等）；share/baseline.md | staging-tree-listing.txt＋bin/qt.conf（相对前缀 `Prefix=.`＋`Plugins=../plugins`——零开发机路径） |
| ⑦ | 从安装树启动核对 About 清单与静态装配内容 | ✅ 命令面板→help.about 打开关于对话框：三域"已装配"（建模 1 面板/10 命令、需求 1/9、运动学 2/8）与装配日志逐项一致；轨迹/动力等白名单"未装配"如实占位；版本基线"未装载"诚实占位；Esc 关闭后 WM_CLOSE 退出码 0 | staging-smoke-4-about-dialog.png＋staging-smoke-about-run.log |

诚实边界：①VC 运行时（vcruntime140 等）未捆入安装树——本机系统自带，离线安装包打包（ARCH §5.4"卸载与升级"）归后续交付任务，不在本契约五条 acceptance 内；②plugins/styles 为空目录（该 Qt 6.11.1 发行版无独立样式插件 DLL——Windows 默认样式内建），如实登记不作人工填充。

## 7. 验证汇总（acceptance 3/5）

| 项 | 结论 | 留痕 |
| --- | --- | --- |
| 集成模式构建（唯一交付口径） | **零错误**（含 CMake 重配置；构建日志 grep error 零命中） | build_release.log |
| 独立冒烟模式构建（vcpkg toolchain＋Qt 前缀） | **零错误**（目标注册验证；产品/插件目标按登记不注册——集成树专属） | cmake_config_smoke.log＋build_smoke.log |
| ird_gates 收集态两跑 | base（worktree @ 79d1d8e1）↔ head 均 65 条命中，**路径归一后 diff 零差异——零新增命中**（"退役不减登记册义务"达成；引擎对库内存量基线照实 FATAL，不粉饰） | ird-gates-base{,-norm2}.txt＋ird-gates-head{,-norm2}.txt＋ird-gates-{base,head}.log |
| validate-docs | PASS（20 units，203 task files） | 本行（脚本 stdout 结论） |
| staging 直启冒烟 | STAGING_SMOKE_PASS＋STAGING_ABOUT_SMOKE_PASS（退出码均 0） | staging-smoke-run.log＋staging-smoke-about-run.log |
| 契约 verify 三条 | 逐条亲手执行：①集成构建 ✅ ②--target ird_gates ✅（收集态对比口径） ③validate-docs ✅ | 上述各留痕件 |
