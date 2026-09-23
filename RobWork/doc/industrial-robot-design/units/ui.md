# ui 单元详细设计（阶段 A：工作台壳、命令入口与状态投影）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 项目 | 内容 |
| --- | --- |
| 文档代号 | UNIT-UI |
| 文档版本 | v1.9（UI-T16 立项登记：工作台宿主插件——`sdurws_ird_ui_plugin` 开发期 RobWorkStudio 集成入口＋壳内容装配层/顶层窗口宿主层拆分授权＋宿主模式 RWStudioView3D 共存最小接入（O-38 裁决承接）——详见 §13 UI-T16 行＋UI-T16 立项登记注＋§16.7 v1.9 行；v1.8（WP-10-T15 返工登记：诊断桥对端记录拒绝显式纪律＋打开编排兜底错误页——ProjectDiagnosticsBridge::report 的 create 拒绝两分组处置〔装配缺陷 fail-fast 上抛／对端契约缺口 Dev 具名上报后协议继续〕＋HarnessMain 打开编排 try/catch 兜底〔exit 5 可观测错误页〕＋v0.1 诚实边界增补〔PRJ-* 项目级事件条目未入目录〕＋PM-07 双可写意图实例复测——详见 §13 UI-T15 落位登记注勘误＋UI-T15 返工登记注＋§16.7 v1.8 行；v1.7（WP-10-T15 落位登记：工作台验证 harness——`sdurws_ird_ui_app` 开发期 L5 装配雏形〔app/PortAdapters 端口适配器族＋HarnessMain 装配序列〕＋ui/CMakeLists 目标登记〔_app 后缀首次启用〕＋§13 UI-T15 行＋落位登记注＋§16.7 v1.7 行；v1.6＝WP-10-T14 落位登记：ui 契约测试套件——详见 §16.7 尾注与本节 §13 UI-T14 行；v1.5（WP-10-T13 落位登记：诊断/确认/任务呈现模型与 CommandInteractionBridge——IDiagnosticPresentationModel.hpp 诊断呈现模型〔§9.1/§10.8：查询/原因链/动作映射/三要素同显/恢复横幅 PM-15/Tier-U 面板/脱敏转发〕＋ITaskPresentationModel.hpp 任务呈现模型〔§9.4/§9.5/§10.7：清单双区＋进度三规则＋能力启用＋强杀独立确认＋归档子标＋当前性徽标〕＋CommandInteractionBridge.hpp 确认交互桥〔§9.2/§9.3：Marshal＋阻塞等待＋isAlive 探针＋principal 缓存〕＋C-6/C-8/C-9 呈现面端口首消费冻结〔IUiCommandInteraction/IUiTaskPresentationPort/IUiFindingQueryPort/IUiUserLogSource/IUiRedactionPolicyPort〕＋任务投影族〔TaskProgressProjection/TaskViewProjection/UiTaskAck/UserLogEntry〕＋UiText 键族⑧＋§13 UI-T13 落位登记注＋§16.7 v1.5 行；v1.4＝WP-10-T12 落位登记：DraftController＋草稿写半区端口——IDraftController.hpp 草稿控制器〔§8 保存/应用分离＋未保存标记＋定时保存＋打开恢复＋Stale 冲突＋两栈〕＋IUiDraftStorePort（C-5 写半区）＋草稿投影族（DraftDocumentProjection 等＋StaleRevisionDetailProjection 随 CommandResultProjection.staleDetail）＋DraftRowProjection 增列＋UiSessionController.reportSessionDirty 生产者接线点＋§10.5 实现冻结登记注＋§13 UI-T12 落位登记注＋§16.7 v1.4 行；v1.3＝WP-10-T11 落位登记：UiSessionController＋关闭/切换对话框机制——UiSessionController.hpp 会话状态机编程入口〔§5.2 七态＋INV-SES-1~4＋§5.6 四级防线，T_force/T_force2 按 §5.6 默认保守值装配期可配——P-UI-8〕＋C-3/5/8 会话端口首消费冻结〔IUiStoreFactoryPort/IUiProjectStorePort/IUiDraftQueryPort/IUiSessionTaskPort＋SessionPortBundle〕＋会话投影族〔打开结果/锁持有者/只读横幅/草稿行/任务行/统一确认对话框数据与决议〕＋UiTypes 会话词表〔UiSessionState/UiOpenMode/UiCloseIntent〕＋§13 UI-T11 落地登记注＋§16.7 v1.3 行；v1.2＝WP-10-T10 落位登记：帮助入口与关于对话框——AboutDialog.hpp 呈现契约〔静态白名单∩装配报告清单＋冻结版本基线投影＋手册入口〕＋IUiAboutDataSource 端口＋ShellWiring.aboutSource 增量＋§10.1/§11.4/§12.3 落地登记注＋§16.7 v1.2 行；v1.1＝WP-10-T09 落位登记：阶段状态投影与工程用语——IStageNavigationModel.hpp 阶段导航投影＋UiTypes 阶段值类型/§6.5 汇聚投影＋UiText.hpp 文案体系＋C-12 阶段门控/域就绪源端口〔P-UI-6 单侧冻结〕＋§3.3/§3.5/§6.4/§6.5 落位登记注＋§16.7 v1.1 行；v1.0＝WP-10-T08 阶段 B 落位登记：参数表与表单公共件——FormEditCommon.hpp 公共契约头〔ParamEditModel 编辑会话模型＋IFormEditOutlet 编辑出口＋KIN-12 显示投影纯函数＋advancedPanelDefaultFields〕＋ParamTablePanel.cpp 参数表面板、§12.3 UI-FRM-1 用例行、§14.1 承接行更新、§16.7 v1.0 行；v0.9＝WP-10-T07 阶段 A 落位登记、v0.8＝WP-10-T06 落位登记、v0.7＝WP-10-T05 阶段 A 落位登记、v0.6＝WP-10-T04 落位登记、v0.5＝WP-10-T03 落位登记、v0.4＝O-31 裁决落地、v0.3＝WP-10-T02 落位登记、v0.2＝全链一致性审计消账、v0.1＝首版草案） |
| 日期 | 2026-09-23 |
| 状态 | `Draft`（不自行宣布 Accepted） |
| 单元 / 层 | ui／L3 平台服务（界面支撑）——**全产品唯一允许 Qt Widgets 的平台单元（R-3 唯一例外登记）** |
| 主任务包 | WP-10（阶段 A 实施；产品化交付主 WP 归 WP-I） |
| 任务编号前缀 | `UI-Txx`（本文 §13；与 development-task-breakdown §2.11 的 WP-10-T01～T11 逐条映射） |
| 验证用例前缀 | `UI-*`（本文 §12；如 UI-WB-1、UI-SES-2、UI-CMD-3） |
| 上游 | REQUIREMENTS.md v1.16（Accepted）；ARCHITECTURE.md v0.11（Draft） |
| 本版范围 | **阶段 A**：工作台壳与五区布局、会话与项目生命周期、只读状态投影、阶段导航、命令注册表、全局快捷键唯一注册点、命令面板、DraftController、诊断与确认 UI 契约、任务进度与关闭协调、静态插件装配、Qt GUI 契约测试流程。阶段 B/C/D 的业务面板、向导与报告预览只登记承接（§14），不展开设计 |

---

## 1. 文档信息、上游基线与目标

### 1.1 输入版本登记（以磁盘实际版本为准，2026-09-10 核对）

| 输入 | 版本／状态 | 说明 |
| --- | --- | --- |
| `doc/industrial-robot-design/REQUIREMENTS.md` | v1.16，`Accepted`（2026-09-09 签署），文件mtime 2026-09-09 | 需求与验收标准唯一权威；本文引用 UX-01～14、PM-01～18、ERR/EVI/TASK/CON、NFR-PERF-01、NFR-MNT-01/02/03、AT 清单 |
| `doc/industrial-robot-design/ARCHITECTURE.md` | v0.11，`Draft`（变更记录至 2026-09-09） | 单元边界、端口、依赖红线权威；本文引用 §2.2/§2.3/§3.2/§3.4/§3.5、§4、§6.8、§7.1/§7.7/§7.11、§9 决策登记册（SA-01/10/15/16/17）、§10.3、§11.2 |
| `doc/industrial-robot-design/DETAILED-DESIGN.md` | 20 单元索引（无版本号） | ui 行登记"待产出"——本文即消账该行 |
| `doc/industrial-robot-design/development-task-breakdown.md` | v0.2，`Draft`（2026-09-10） | WP-10-T01～T11 任务卡与 §3 追踪矩阵为本文任务拆分对齐对象 |
| `units/core.md` | v0.1，`Draft`（2026-09-09） | 共享类型与词表、事件端口、诊断数据契约、单位换算（本文 §3.2 消费清单） |
| `units/testkit.md` | v0.1，`Draft`（2026-09-09） | 契约测试基座与 §6.7 Windows GUI 测试规则 |
| `units/project.md` | v0.1，`Draft`（2026-09-09） | ProjectStore/ProjectCommandService/DraftService/ICommandInteraction 等契约（P-PR-7 要求本文交叉核对） |
| `units/evidence.md` | v0.1，`Draft`（2026-09-09） | CurrentnessResult/ResultEnvelope/EvidenceManifest/VerdictTrace 契约与 P-EV-4 |
| `units/runtime.md` | v0.1，`Draft`（2026-09-09） | IRuntimeModelView/RuntimeNameMap 只读视图契约 |
| `units/policy.md` | v0.1，`Draft`（2026-09-10） | IPolicyProvider 策略只读投影契约（P-POL-9：策略编辑命令适配器宿主＝ui） |
| `units/execution.md` | v0.1，`Draft`（2026-09-10） | ITaskScheduler/ITaskController/TaskSnapshot/ProgressReport/ArchivePhase 契约与 §13 交接行 |
| `units/diagnostics.md` | v0.1，`Draft`（2026-09-10） | DiagProjectionItem/FindingRecord/IDiagnosticSink/码表契约与 §12.2 ui 交接行 |
| `units/ui.md` | **本次新建前不存在**（units/ 目录仅 8 个文件，已确认） | 本文为首个版本 |
| `units/workflow.md` | **不存在**（DTB §4.1 缺失清单在列，编卡任务 WP-22-T01） | 阶段门控规则、生命周期入口整合归 workflow；本文仅单侧冻结消费接口（P-UI-6） |
| `AGENTS.md`（仓库工作路径） | 仓库内仅 `vcpkg/AGENTS.md`（vcpkg 端口维护规则），**不在本文工作路径、不适用** | 已核对 |
| `C:\Users\zgl18\.codex\AGENTS.md`（用户级） | 存在（2026-08-10） | **适用**：Windows Qt GUI/测试启动规则（VS x64 开发环境、`QT_QPA_PLATFORM=windows`、单实例绝对路径、禁 offscreen、不合并 Widget/Meta 测试、平台插件故障处置）——testkit.md §6.7 已承接，本文 §12.2 同口径承接 |
| 代码落位 `RobWorkStudio/src/rwslibs/industrialrobot/ui/` | 仅 `include/sdurws/ird/ui/README.md` 占位（不参与编译，指向本文 §9——**章节号按本文实际结构修正为 §13**，属骨架注释滞后，不阻塞） | 无源码 |
| `industrialrobot/CMakeLists.txt` | WP-01 构建骨架 | `sdurws_ird_ui` 为 INTERFACE 占位（别名 `RWS::ird::ui`）；升级 STATIC 时目标名不变（DTB §5.1）；红线门禁随首批源码启用 |

**缺失文件如实登记**（v0.2 更正）：`units/workflow.md` 不存在（本文对 workflow 的全部引用为"对未产出单元的单侧冻结"，见 P-UI-6）；`units/io.md` 与 `units/reporting.md` **已产出**（v0.1 Draft——reporting §9.8/§12 已承接 P-UI-9 预览接口，io 经 §10.10/§13.2 双向交接；原"不存在"表述系编写时快照，已过期）；其余 9 个业务域单元详设仍不存在（本文仅按 ARCH §3.1 单元总表登记消费边界，不引用其内部契约）。

### 1.2 单元目标（阶段 A）

ui 是 L3 平台服务中的界面支撑单元，为产品提供**唯一**的工作台壳层设施：

1. **工作台壳与五区布局**（UX-09）：顶/左/中/右/底五区、停靠、隐藏与恢复默认布局，布局记忆为用户级设置（PM-14，不入项目）。
2. **会话与项目生命周期协调**：UI 会话状态机（无项目→打开→切换→关闭→等待归档/释放），UI 关闭与 project 存储上下文释放分离（ARCH §6.8 A7、SA-17）。
3. **只读状态投影**：汇聚 core/project/execution/evidence/diagnostics/policy 的只读投影，UX-10 七态呈现（含七态×core 九态映射——core.md §10.3 指定本文冻结）。
4. **阶段导航**（UX-12 投影侧）：StageStatusModel 汇聚各域就绪状态投影、七阶段导航呈现；门控规则归 workflow，ui 只消费（ARCH §3.4）。
5. **命令注册表＋全局快捷键唯一注册点＋命令面板**（UX-13、SA-16）：命令项注册、冲突拒绝、模糊搜索、键盘导航；一切用户命令最终进入唯一命令服务或显式会话态命令。
6. **DraftController**：草稿定时落盘（默认 60 s）、保存/恢复/应用协调、未保存标记投影（PM-04 界面侧；草稿数据与落盘归 project）。
7. **诊断、确认与任务 UI 契约**：ICommandInteraction 实现（SA-15 确认对话）、诊断表/日志面板/恢复横幅呈现（PM-15）、任务清单与进度呈现（PM-03 九态短标签）。
8. **静态插件装配**（SA-01）：白名单静态注册业务插件的界面模块，无运行时动态加载/卸载。

### 1.3 非目标（本单元不拥有）

见 §2.3"不拥有"表。特别强调：业务算法、工程判定、项目持久化、任务调度、模型编译、报告渲染、诊断码语义、当前性计算均不在 ui；**ui 不恢复 `old/` 界面实现**（REQUIREMENTS 附录 B 口径，old/ 仅功能范围对照）。

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有

#### 2.1.1 ui 拥有

| # | 职责 | 需求依据 | 备注 |
| --- | --- | --- | --- |
| O-1 | 工作台壳与五区布局（含停靠、隐藏、恢复默认、最小可用布局） | UX-09 | 布局状态＝用户级会话设置，不入项目 |
| O-2 | 会话级 UI 状态（选择模型、会话姿态显示、视图显示开关、面板可见性） | ARCH §7.7、KIN-06/12 | 不产生修订、不进缓存身份 |
| O-3 | 只读状态投影模型（UiProjectionStore，§6） | UX-10、PM-11 | 投影不回写权威对象 |
| O-4 | 阶段状态与导航投影（StageStatusModel 汇聚＋导航视图） | UX-01/02/12 投影侧 | 门控规则归 workflow |
| O-5 | DraftController 的界面协调部分（定时器、汇总视图、未保存标记、恢复编排） | PM-04 界面侧 | 草稿数据与落盘归 project DraftService |
| O-6 | 命令注册表（CommandRegistry）与命令面板 | UX-13、SA-16 | 唯一命令入口设施 |
| O-7 | 全局快捷键唯一注册点（GlobalShortcutRegistry） | UX-13（M-13）、SA-16 | 绑定为用户级设置（PM-14） |
| O-8 | UI 线程调度与后台任务订阅（UiThreadEventRelay、进度轮询） | ARCH §4.2、NFR-PERF-01 | 事件总线 UI 线程投递实现归 ui（core.md §4.9） |
| O-9 | 诊断、确认、进度和只读模式的界面投影（诊断表/日志面板/恢复横幅/确认对话/任务清单） | UX-03 呈现、PM-15、SA-15 | 事实与阈值归 diagnostics/project |
| O-10 | 窗口关闭、项目切换和会话生命周期协调（UiSessionController＋统一确认对话框机制） | PM-03 机制侧 | 入口编排归 workflow（WP-22-T06） |
| O-11 | UX-10 七态状态词表与 core 九态 TaskState 的映射 | UX-10、core.md §10.3 | 本文 §6.3 冻结（P-UI-1 登记需求侧确认） |
| O-12 | 策略编辑命令适配器宿主（编辑表单→①命令端口的适配） | UX-08、P-POL-9（policy.md 定稿承接） | 判定权与阈值权威归 policy/EngineeringPolicySet |
| O-13 | UI-\* 平台诊断码注册（Qt 层异常映射，分类 internal/Dev 为主） | diagnostics.md §8.9 | 码值经 StableCodeRegistry 注册 |
| O-14 | 帮助入口与"关于"对话框（插件清单展示） | UX-14、SA-01 | 版本数据与冻结基线一致（NFR-DEP-05） |

#### 2.1.2 ui 消费

| # | 消费对象 | 形态 | 用途 | 提供方契约锚点 |
| --- | --- | --- | --- | --- |
| C-1 | core 共享类型与诊断契约 | 接口依赖（链接 core） | 身份类型、四词表、DiagnosticRecord/ConfirmableFinding、单位换算 | core.md §4.1/§4.4/§4.7/§4.8/§4.9 |
| C-2 | core 事件端口（IDomainEventBus） | 运行时注入（L5 装配） | 订阅 RevisionCommitted/DependencyInvalidated/TaskStatusChanged/ResultArchived | core.md §4.9 |
| C-3 | project 项目会话与存储上下文（ProjectStore/Factory） | 运行时注入（O-31 裁决 2026-09-19：ui 自有最小端口接口＋值投影，L5 装配适配；产品面零对端链接/include） | 打开/只读判断/锁信息/关闭协议（requestClose/closed/subscribeClose） | project.md §5.1 |
| C-4 | project 命令服务（ProjectCommandService/UndoRedoService） | 运行时注入（O-31 裁决 2026-09-19：ui 自有最小端口接口＋值投影，L5 装配适配；产品面零对端链接/include） | 一切"应用/撤销/重做"的唯一写路径 | project.md §5.3/§5.5 |
| C-5 | project 草稿服务（DraftService）与查询端口（IProjectQueryPort） | 运行时注入（O-31 裁决 2026-09-19：ui 自有最小端口接口＋值投影，L5 装配适配；产品面零对端链接/include） | 草稿落盘/恢复/汇总；修订/分支/元数据只读查询 | project.md §5.2/§5.4 |
| C-6 | project ICommandInteraction（ui 实现该接口） | 运行时注入（O-31 裁决方向外翻：L5 适配器实现 project::ICommandInteraction，委托 ui 自有交互回调端口） | SA-15 确认对话回调、principal 采集、Marshal | project.md §5.3.3（P-PR-7） |
| C-7 | evidence 结果与当前性投影 | 运行时注入（O-31 裁决 2026-09-19：ui 自有最小端口接口＋值投影，L5 装配适配；产品面零对端链接/include） | CurrentnessResult（过期原因）、VerdictTrace（缺项清单）、FormalPassEligibility、EvidenceManifest | evidence.md §6/§7/§8 |
| C-8 | execution 任务状态、进度与控制 | 运行时注入（O-31 裁决 2026-09-19：ui 自有最小端口接口＋值投影，L5 装配适配；产品面零对端链接/include） | tasksByProject（PM-03 清单）、TaskSnapshot/ProgressReport、requestCancel/Pause/Resume/ForceTerminate、shutdown(DrainPolicy) | execution.md §10.1/§10.2 |
| C-9 | diagnostics 诊断目录与确认请求 | 接口依赖 | DiagProjectionItem/FindingRecord 投影、订阅刷新、Tier-U 日志面板数据 | diagnostics.md §9.3/§9.7 |
| C-10 | policy 策略只读投影 | 运行时注入（O-31 裁决：ui 自有策略摘要端口——不直接 include policy::IPolicyProvider；PolicyResolution/EngineeringPolicySet 语义经 §3.2 冻结基准投影，L5 适配） | UX-08 策略摘要（域启用/阈值/过滤对计数） | policy.md §9.1/§10.6 |
| C-11 | runtime 只读运行时视图 | 运行时注入（O-31 裁决：ui 自有名称解析端口——resolveObjectId 语义按 runtime.md §7.3 冻结；IRuntimeModelView 共同消费点归阶段 B 随增量修订复核） | 三维渲染共同消费点、ObjectId↔局部名呈现 | runtime.md §7.3/§8.3 |
| C-12 | workflow 阶段门控与下一步建议 | 接口依赖（**workflow.md 未产出，单侧冻结**） | 七阶段解锁/锁定、下一步建议文案数据 | ARCH §3.4；P-UI-6 |
| C-13 | 业务插件界面模块与只读业务投影 | 运行时注入（IPluginUiRegistrar，静态白名单） | 阶段面板、域编辑器、业务命令注册 | SA-01；本文 §11 |
| C-14 | reporting 报告预览接口 | 接口依赖（阶段 B 承接） | 报告导出命令的目标服务 | units/reporting.md 未产出；§14 登记 |
| C-15 | Qt/RobWorkStudio 基线 | 框架 | Qt Core/Gui/Widgets；RWStudioView3D（三维区，阶段 B 接入） | ARCH §5.1 |

> 注入形态统一细则见 §3.1（O-31 裁决 2026-09-19）：对端类型一律经 ui 自有端口/值投影进入 ui，C 表"消费对象"列自 v0.4 起读作语义协作面对象。

#### 2.1.3 ui 不拥有

| # | 不拥有内容 | 权威所有者 | 红线依据 |
| --- | --- | --- | --- |
| N-1 | 项目文件、HEAD、修订、分支、锁和事务 | project | ARCH §6.1"磁盘写入唯一归属 project"；ui 不得直写 `.rwdesign` 正式目录（含 drafts/ 由 DraftService 代写） |
| N-2 | RobotDesign、CanonicalModel、碰撞、运动学、轨迹、动力学和优化算法 | 各业务域＋runtime/policy | R-3；ui 零计算 |
| N-3 | EngineeringPolicySet 实现与阈值语义 | policy | ARC-05；ui 只读摘要＋编辑经命令端口 |
| N-4 | RunRegistry、worker、检查点和缓存治理 | execution | TASK-01～03；ui 只呈现与请求控制 |
| N-5 | 证据等级、工程判定、结果当前性计算 | evidence | CON-02/EVI-01；ui 只投影（"七态"为呈现投影而非新判定） |
| N-6 | 外部资源解析、包编解码 | io | SA-14；向导仅收集用户选择 |
| N-7 | 诊断码定义、严重级别、确认语义 | diagnostics（码表）/core（数据契约） | SA-12；ui 拥有的是"文案键→本地化文本"资源与呈现 |
| N-8 | 报告结构化对象与最终渲染数据源 | reporting | RPT-01；ui 只提供预览宿主与命令入口 |
| N-9 | 阶段门控规则（进入条件的业务判定） | workflow | ARCH §3.4；ui 的 StageStatusModel 只汇聚投影 |
| N-10 | 草稿数据模型、落盘格式、事务 | project | PM-04；ui 只持定时器与界面状态 |
| N-11 | 第二套命令入口／第二套快捷键注册表／第二套阶段状态机 | （不存在） | 任务约束；SA-16；ui 设施是唯一注册点，业务插件不得绕过 |
| N-12 | 业务真值 | （不存在） | UI 文本、控件状态、窗口生命周期不是业务真值；一切判定回查权威投影 |

### 2.2 需求承接表（ui 视角）

主责＝ui 拥有实现；机制＝ui 提供设施、他单元编排；呈现＝ui 只做显示；消费＝只读引用。

| 需求 | ui 角色 | 承接位置 |
| --- | --- | --- |
| UX-01 阶段目标/必需输入/下一步 | 呈现（数据归 workflow 门控输出与各域就绪投影） | §6.4/§6.5 |
| UX-02 工程用语（零哈希/Schema/插件名） | 主责（呈现层文案键体系） | §6.6、§3.5 |
| UX-03 失败提示字段 | 呈现（字段规范归 ERR-01/diagnostics；ui 渲染 DiagProjectionItem） | §9.1 |
| UX-04 高级面板 | 主责（面板承载，阶段 B 填充内容） | §4.3（右区）、§14 |
| UX-05 参数表批量编辑 | 主责（公共件，WP-10-T08；本文登记接口，阶段 B 详化） | §14 |
| UX-06 统一状态词与图例 | 主责（七态词表唯一所有者） | §6.3 |
| UX-07 表单级确认应用 | 主责（应用前确认交互；断言与放行归 project） | §7.5/§9.2 |
| UX-08 统一工程策略入口 | 机制（摘要只读＋跳转＋编辑适配器；权威归 policy） | §6.7 |
| UX-09 五区布局 | 主责 | §4 |
| UX-10 七态统一呈现 | 主责（七态词表＋映射） | §6.3 |
| UX-11 三维视图交互 | 主责（阶段 A 只登记区域契约；交互实现 WP-10-T05 阶段 B 交付） | §4.3（中区）、§14 |
| UX-12 阶段门控导航 | 机制（投影＋导航呈现；门控规则归 workflow WP-22-T03） | §6.4/§6.5 |
| UX-13 命令面板/快捷键 | 机制（注册表/快捷键表/面板归 ui；入口整合主责 WP-22-T11/T12） | §7 |
| UX-14 帮助与关于 | 主责（帮助入口＋关于对话框——插件清单/版本呈现；版本数据由 L5 提供） | §11.4 |
| PM-01/02 新建/打开向导 | 消费（向导归 workflow；ui 提供对话框基件；阶段 B 承接） | §14 |
| PM-03 关闭/切换/退出统一确认 | 机制（对话框＋会话状态机归 ui；编排主责 WP-22-T06） | §5 |
| PM-04 保存/应用分离 | 机制（DraftController 界面侧；主责 WP-04-T12 project） | §8 |
| PM-05 另存/包导出导入 | 呈现（进度/取消 UI；操作归 project/io） | §9.4、§14 |
| PM-06 旧格式拒绝呈现 | 呈现（诊断＋升级指引入口） | §9.1 |
| PM-07 只读打开 | 主责（只读呈现与写入口禁用） | §5.5 |
| PM-08 崩溃恢复 | 呈现（恢复横幅，PM-15） | §9.1 |
| PM-10 无项目首页 | 呈现（页面归 ui；入口编排 workflow） | §4.4 |
| PM-11 标题/状态栏 | 主责（格式 `<显示名>[*][（只读）]`） | §4.3 |
| PM-14 用户级设置持久化 | 主责（布局/快捷键绑定/草稿周期等设置项） | §4.6/§7.4/§8.4 |
| PM-15 恢复横幅/诊断表/日志 | 主责（呈现层） | §9.1 |
| PM-17 启动/闪屏/异常诊断 | 横切（主责 WP-24；ui 承担壳装配显示面） | §11.2 |
| PM-18 撤销/重做入口 | 机制（菜单/命令面板绑定归 ui；命令归 project WP-04-T13） | §7.5 |
| ERR-01 | 消费（字段渲染＋"不适用"标记） | §9.1 |
| EVI-01/02 | 消费（FormalPassEligibility 只读投影；证据不足不得显示通过） | §6.3 |
| TASK-01/02/03 | 消费（九态事件流、合法组合由构造边界保证；ui 不复判） | §9.4 |
| CON-02/05 | 消费（CurrentnessResult 投影；Superseded 历史可查看） | §6.3 |
| NFR-PERF-01 | 主责（UI 线程纪律：≤200 ms 交互、>1 s 转后台、无 2 s 无响应窗） | §3.4、§12 |
| NFR-MNT-01 | 消费红线（ui 是例外持有者，计算核心零 Widgets 的红线不因 ui 放松） | §3.1 |
| NFR-MNT-02 | 主责参与（Widget/控件互读零命中静态扫描） | §3.1 |
| NFR-MNT-03 | 主责（状态词/文案键唯一权威在 ui 域内不重复定义） | §6.3、§3.5 |

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 构建目标与依赖红线

| 目标 | 类型 | 链接 | 说明 |
| --- | --- | --- | --- |
| `sdurws_ird_ui` | STATIC（自 UI-T02 起，由 INTERFACE 占位升级，目标名不变） | `RWS::ird::core`、`RWS::ird::diagnostics`、Qt Core/Gui/Widgets | **R-3 唯一例外登记目标**：全产品唯一允许 Qt Widgets 的平台单元；例外范围仅限本目标与 §3.3 所列 ui 测试目标 |
| `sdurws_ird_ui_test` | 可执行 | 被测目标＋`RWS::ird::testkit`＋GTest | 无界面模型测试（QCoreApplication 级，无需 GUI 平台插件——AGENTS 模型测试豁免） |
| `sdurws_ird_ui_contract_test` | 可执行 | 被测目标＋sdurws_ird_testkit＋GTest（落位直链面，WP-10-T02；core/diagnostics 经被测目标 PUBLIC 传染可达；project/execution 直链随 UI-T14 契约测试套件落地登记——测试目标链接面按本表承载，不属 ARCH §3.5 产品边管辖，O-31 裁决 2026-09-19 消解 F-228） | 跨单元契约测试（无界面；带 Qt 事件循环 headless 用 QCoreApplication） |
| `sdurws_ird_ui_gui_test` | 可执行（GUI，单列） | 被测目标＋testkit（＋`sdurws_ird_testkit_qt`，见 P-TK 登记）＋`Qt6::Test`（UI-T06 增量：QTest 键盘注入——§7.4 面板键盘导航/§7.3 物理键触发的 GUI 观测面；测试目标链接面增量，产品面不变——v0.8 行） | Widgets/GUI 契约测试；**ctest LABELS `ird_gui` 串行**；不与模型/Meta 测试合并命令（v0.2 更名：原名 `ird_ui_gui_test` 违反 ARCH §1.4 `sdurws_ird_<unit>_*` 命名约定，本卡内三处同步更正） |

依赖边（对齐 ARCH §3.5，不新增）：`ui → core, diagnostics`（接口依赖）；`project / execution / evidence / policy / runtime` 对 ui **零编译依赖**——与它们的协作一律经运行时注入（C-3～C-12 中的实例由 L5 应用壳装配期注入；ui 持有的是 core/diagnostics 中定义的接口类型，或 **ui 自有公共头定义的最小端口接口与值投影**——O-31 裁决 2026-09-19：对 project/evidence/execution/policy/runtime 不增边，产品面零对端链接/include，`NoCrossUnitInclude_O31_UI_BUILD` 守卫即其常驻执行面；端口/投影值形态以 §3.2 对端锚点为冻结基准，L5 适配器归装配层任务，ui 测试以可控替身承载——reporting.md §3.3／evidence.md §3.3／EX ICompileCacheJudge 同款惯例）。**禁止 ui 链接或 include 任何业务域单元（modeling/requirements/.../workflow）的私有头**；业务插件界面经 §11 注册端口装配。

线程与红线自检（静态扫描承载，UI-T02 门禁）：

- R-3 例外登记文本注明"仅 ui"；任何其他目标出现 Widgets 头即构建失败。
- ui 源码禁止出现 RobWork 名称前缀拼接/剥离（R-4）；显示名一律经 `IRuntimeNameResolver::resolveObjectId` 取 `localName`（runtime.md §7.3）。
- ui 不读取其他插件控件/内存对象/私有文件（ARC-02/NFR-MNT-02：Widget 互读零命中）。

### 3.2 消费的上游类型清单（投影值形态冻结基准）

按 §3.1 注入边界（O-31 裁决 2026-09-19），以下类型是 ui 值投影的**语义冻结基准**：ui 产品面不直接 include 对端公共头，对端能力经 ui 自有端口接口以值投影形态进入 ui；投影 schema 逐字段语义以本表对端锚点为唯一权威，**不重定义权威类型、不另立语义**（NFR-MNT-03 单一权威不变——投影≠重定义，reporting ModelSummary／runtime §13.2 同案），对端锚点语义变更时投影随单元卡增量修订同步。

- 身份：`core::ObjectId/ProjectId/BranchId/RevisionId/RunId/AttemptId/EventId`、`core::ContentVersion/ContentIdentity`、`core::TaskIdentity`（五元组）。
- 词表：`core::EvaluationMode`、`core::TaskOutcome`、`core::EngineeringStatus`、`core::TaskState`（九态）。
- 事件：`core::DomainEvent/DomainEventKind`、`core::IDomainEventBus/IDomainEventSink/IEventSubscription`。
- 诊断：`core::DiagnosticRecord/ComparativeFields/ConfirmableFinding/ConfirmationState/ConfirmationCredential`、`core::UnitToken`、`core::Quantity`/`displayValueIn`。
- project：`ProjectStore/ProjectStoreFactory/OpenStoreRequest/OpenStoreResult/OpenMode/LockInfo/RecoveryReport`、`ProjectCommandService/CommandEnvelope/CommandResult`、`UndoRedoService/UndoRedoStatus`、`DraftService/DraftDocument/DraftProjection/DraftInfo`、`IProjectQueryPort/RevisionView/BranchTip/ProjectMetadataView`、`ICommandInteraction`、`StoreError/StoreErrorCode`。
- execution：`ITaskScheduler/SubmitResult/ResourceBudget/DrainPolicy`、`ITaskController/CancelAck/StatusAck`、`TaskSnapshot/ProgressReport`。
- evidence：`CurrentnessResult/InvalidationReason/CurrentnessIndex`、`ResultEnvelope/EvidenceManifest/EvidenceItemStatus`、`VerdictResult/VerdictTrace`、`FormalPassEligibility`。
- diagnostics：`IDiagnosticSink/DiagProjectionItem/DiagQuery`、`FindingRecord/FindingId/FindingState`、`IDiagnosticRegistry/CodeDescriptor`、`IDiagObserver/ISubscription`、`ILogger/LogTier`、`IRedactionService/RedactionPolicy`、`DiagnosticCategory/DiagnosticSeverity`。
- policy：`IPolicyProvider/PolicyResolution`、`EngineeringPolicySet`（只读字段投影）。
- runtime：`IRuntimeModelView`、`IRuntimeNameResolver`、`RuntimeNameMap/RuntimeName/ObjectRef/NameScope`。

### 3.3 公共头文件布局

```
ui/include/sdurws/ird/ui/          命名空间 sdurws::ird::ui
  UiTypes.hpp            本单元公共值类型词表（CommandId/TextKey/MenuPath 别名——§7.1，UI-T06 落位；StageId/StageViewStatus/UiStateToken＋DomainReadinessItem/StageReadinessSnapshot〔§6.5 汇聚投影〕——UI-T09 增量落位，§16.7 v1.1）
  UiPorts.hpp           C-3/4/5/7/8/10/11/12 最小端口接口（O-31 裁决注入面：会话/命令/草稿/查询/任务/当前性/策略摘要/名称解析/交互回调；接口形状随 UI-T03+ 各消费任务在其 §10 对应节实现冻结并增量修订登记，L5 适配；C-4 命令网关 IUiCommandGateway 随 UI-T06 冻结；C-12 阶段门控 IUiStageGate＋域就绪源 IUiDomainReadinessSource 随 UI-T09 冻结——P-UI-6 单侧冻结；关于框数据源 IUiAboutDataSource〔§11.4 装配报告＋冻结基线〕随 UI-T10 冻结——§16.7 v1.2；C-3/5/8 会话生命周期端口 IUiStoreFactoryPort/IUiProjectStorePort＋IUiStoreCloseObserver/IUiDraftQueryPort/IUiSessionTaskPort＋SessionPortBundle〔打开结果随注入的会话绑定集〕随 UI-T11 冻结——§16.7 v1.3）
  UiProjections.hpp     值投影承载（StatusWordProjection〔§6.3〕及会话/元数据/任务/当前性等投影——§3.2 冻结基准的 ui 侧载体；C-4 CommandEnvelopeProjection/CommandResultProjection 随 UI-T06 增量；UI-T11 增量＝会话生命周期投影族：LockHolderProjection/ReadOnlyOpenCause/ReadOnlyBannerProjection＋assembleReadOnlyBanner〔PM-07 显示差异〕/OpenedProjectFacts/OpenStoreFailureFacts/OpenStoreOutcome〔INV-SES-1 唯一数据源〕/DraftRowProjection/TaskRowProjection〔PM-03 行模型〕/CloseDialogData/CloseDecision/CloseDialogResolution〔§5.4 统一确认对话框〕/ForceCloseDialogData〔§5.6 防线 3〕/ContextInvalidNotice〔UI-SESSION-CONTEXT-INVALID 呈现面〕——§16.7 v1.3）
  View3DContract.hpp    三维视图占位契约（UI-T05 阶段 A 登记：UX-11 九项交互清单词表＋KIN-06/AT-04 会话姿态语义钉住常量——阶段 B 交互实现的代码侧承接面；仅契约数据零运行时行为，§16.7 v0.7）
  PolicySummaryCard.hpp 工程策略摘要卡呈现契约（UI-T07 阶段 A 登记：分组异名词表〔POL-ID-3——分组标题/行模型/行键〕＋阈值显示换算纯函数〔core Units——KIN-12 显示投影〕＋卡面文案单点；模型层与 GUI 同源消费，§16.7 v0.9）
  FormEditCommon.hpp    参数表与表单公共件编辑契约（UI-T08 阶段 B 登记：QuantityFieldSpec 字段宿主描述＋ParamEditModel 编辑会话模型〔基线/暂存/就地错误三层值语义＋筛选/批量粘贴/表单级确认应用/取消恢复——UX-04/05/07 公共编辑规则唯一规则承载，零 Qt〕＋IFormEditOutlet 编辑出口端口〔ui 自有——域编辑器实现、经 IDraftController.attachModule 接入；O-31〕＋KIN-12 显示投影纯函数〔core Units 唯一换算入口〕＋advancedPanelDefaultFields 高级面板字段集〔UX-04 数值半区〕＋createParamTablePanel 面板工厂；QWidget 仅前置声明不拖入 Widgets，§16.7 v1.0）
  IWorkbenchShell.hpp    §10.1 工作台壳门面
  IStageNavigationModel.hpp  §10.2 阶段导航投影（UI-T09 落位：StageView/NavigateResult/IStageViewObserver＋工厂与装配依赖——stageViews/requestNavigate/subscribe/readinessSnapshot 四方法＋stageIdSequence/stageToken 序与 token 唯一映射；§16.7 v1.1）
  ICommandRegistry.hpp   §10.3 命令注册表（UI-T06 落位：描述符/谓词/面板投影/提交语义＋C-4 值投影承载——O-31）
  IGlobalShortcutRegistry.hpp §10.4 全局快捷键唯一注册点（UI-T06 落位：HotkeyBinding/HotkeyResult/QShortcut 唯一创建点契约）
  IDraftController.hpp   §10.5 草稿控制器
  IDiagnosticPresentationModel.hpp  §10.8 诊断呈现模型（UI-T13 落位：DiagProjectionItem 查询转发〔过滤/去重折叠/稳定序全在 sink——模型零再加工〕＋expandChain 原因链〔链查询缝 chainOf 单侧冻结——diagnostics §9.7 投影不携带链字段，L5 经 diagnostics CauseLink::chainOf 供给，未注入＝退化"起点自身即根"零虚构，§16.7 v1.5〕＋动作映射表 diagActionFor〔§9.1 ui 冻结呈现层映射——全部路由跳转/命令，不直调领域服务〕＋呈现映射 diagnosticRouteFor〔取消→无错误呈现 UX-03〕＋三要素同显 formatComparison〔四态占位不伪造数值 ERR-01/MDL-06〕＋恢复横幅 assembleRecoveryBanner〔PM-15 一句话汇总＋三动作〕＋Tier-U 面板 assembleUserLogPanel〔flush 仅关闭路径——模型无 flush 面〕＋redactedPath 只读转发〔呈现层不自行脱敏 NFR-SEC-07〕＋subscribe Marshal〔目录通知线程→UI 线程，§3.4 M-1〕；头文件零 Qt，§16.7 v1.5）
  ITaskPresentationModel.hpp  §10.7 任务呈现模型（UI-T13 落位：TaskRow 行模型〔TaskViewProjection snap＋九态短标签键＋合并进度〕＋ITaskViewObserver＋activeTasks/backgroundTasks 双区〔后台只读——P-UI-7 建议口径〕＋控制四请求即发即忘〔强杀独立确认 userConfirmed 前置〕＋forceTerminateConfirmation 后果说明数据＋startProgressPolling/stopProgressPolling/pollOnce〔默认 200 ms，kDefaultProgressPollInterval〕＋纯函数装配唯一权威：mergeProgressDecision〔percent 单调/回退丢弃/新 attempt 重置/重复幂等〕＋archiveBadgeFor〔完成≠归档完成；ArchiveFailed 警告位〕＋taskRowActionsFor〔崩溃/中断重跑入口＋历史可查看〕＋resultBadgeFor〔C-7 搬运不计算，无「正式通过」字样〕＋attachProject/demoteCurrentToBackground 会话联动；零 Qt——模型层可测，§16.7 v1.5）
  CommandInteractionBridge.hpp  §9.2 ICommandInteraction 的 ui 实现（P-PR-7；UI-T13 落位：实现 ui 自有 IUiCommandInteraction 端口〔C-6 外翻——L5 适配器实现 project::ICommandInteraction 并委托本端口，确认对话/principal 采集/Marshal 语义零变化〕＋IUiConfirmDialogPresenter 呈现缝＋ConfirmDialogData/Item/Resolution 对话数据契约〔§9.3 映射：码表键推导＋FindingRecord 补全回调双层＋Dev 折叠区只带命令类型 token（零哈希进用户文本 UX-02）〕＋markSessionDismantled 拆除探针〔Aborted(interaction-lost)——无永久等待〕＋noteRevisionCommitted 失效标注〔"输入已变化"提示重开对话——UI-CMD-6〕＋assembleConfirmDialogData 装配纯函数；命令执行线程同步回调→Marshal 至 UI 线程→阻塞等待（P-DIAG-6 无超时——等待谓词＝决议∨拆除，决议优先）；头文件零 Qt，§16.7 v1.5）
  IUiProjectionStore.hpp §10.6 只读投影存储
  IPluginUiRegistrar.hpp §10.9 插件界面注册端口
  UiSessionController.hpp  §5 会话状态机（对 L5/workflow 的编程入口；UI-T11 落位：UiSessionController〔§5.2 七态唯一执行者＋§5.4 关闭/切换统一确认对话框机制〔assembleCloseDialogData 数据装配纯函数＋beginClose/beginSwitch/resolveCloseDialog 决议解析〕＋§5.6 Draining 四级防线〔pollDrain 轮询/forceCloseDialogData/resolveForceCloseDialog/cancelDrainWait——T_force/T_force2 按 §5.6 默认保守值 120 s/300 s 经 UiSessionControllerDeps 装配期可配，P-UI-8 未闭合不私定终值〕＋INV-SES-1~4 不变量实现〔writable 唯一只读判定源/单 Open* 绑定/后台持有点保活/有界等待〕＋makeContextInvalidNotice 迟到写提示〔UI-SESSION-CONTEXT-INVALID 双通道出线〕；§16.7 v1.3）
  UiThreadEventRelay.hpp   §6.1 事件总线 UI 线程投递实现（core 总线契约的 ui 实现）
  UiText.hpp              文案键→本地化文本解析（UX-02 工程用语唯一出口；UI-T09 落位：resolveText 键解析＋位置参数替换纪律＋formatQuantityText 数值带单位〔core displayValueIn 唯一换算入口〕＋notApplicableText 占位＋ensureNoInternalIdentity 哈希形态守卫＋registeredTextKeys 键盘点面＋内建中文过渡文案表——键冻结值过渡，§16.7 v1.1；UI-T13 增键族⑧ 32 键：诊断三要素占位/恢复横幅 PM-15/日志面板/确认对话/任务呈现——§16.7 v1.5）
  AboutDialog.hpp        帮助入口与关于对话框呈现契约（UI-T10 落位：pluginUiWhitelist 静态白名单〔§11.1 八 token 编译期词表〕＋PluginAssemblyReport 值形状〔§10.9 冻结形状承载——注册端口落位时迁移其自有头〕＋AboutVersionBaseline 冻结基线投影〔NFR-DEP-05 呈现值面〕＋aboutPluginRows/aboutVersionRows 行装配唯一权威〔UI-PLG-2 数据源断言锚〕＋pluginTitleKey/pluginAssemblyStatusLabelKey 键族〔§3.5 UI-T10 冻结登记〕＋userManualPath/openUserManual 手册入口〔share\ 帮助文件落位口径〕＋createAboutDialog 对话框工厂；QDialog/QWidget 仅前置声明不拖入 Widgets，§16.7 v1.2）
```

私有实现（`ui/src/`）：`WorkbenchShell_p.hpp`、五区 DockWidget 组装、`CommandPalette_p`、`DraftController_p`、`CloseDialog_p` 等，不进 `include/`（R-2）。ui 面向 workflow/L5 暴露的装配入口集中在 `IWorkbenchShell`＋`UiSessionController`＋`IPluginUiRegistrar` 三个头。

### 3.4 线程模型（ARCH §4.2 的 ui 侧细化）

| 线程 | ui 侧职责 | 约束 |
| --- | --- | --- |
| UI 线程（Qt 主线程） | 全部 Widget 状态、投影消费、命令提交入口、DraftController 定时器 | **零长计算**（NFR-PERF-01：>1 s 转后台；交互 P95 ≤200 ms；无 >2 s 无响应窗）；UI 线程禁止模型编译（runtime.md §9.2）、禁止文件事务、禁止等待 flush（ILogger flush 仅关闭路径——diagnostics.md §9.6 非法调用表） |
| 命令执行线程（project 所有） | 承载 `ProjectCommandService::submit` 与 `ICommandInteraction::requestConfirmations` 同步回调 | ui 的 CommandInteractionBridge 在该线程被调用，**内部 Marshal 到 UI 线程并阻塞等待结果**（P-PR-7）；桥内禁止再回调命令/查询端口以外的写入口 |
| 调度线程（execution 所有） | 发布 TaskStatusChanged/ResultArchived、推进状态机 | ui 经 UiThreadEventRelay 订阅，回调线程＝总线投递线程，**ui 负责 Marshal**（execution.md §6.2/§10.4） |
| 诊断通知线程（diagnostics 所有） | 目录订阅回调派发 | ui 负责 Marshal（diagnostics.md §9.8） |
| ui 后台落盘线程（ui 自有，1 条） | 草稿 save 调用、布局/设置写盘 | 串行队列；DraftService 内部经 writer 互斥与命令线程汇合（project.md §9.8）；禁止访问任何 Widget |

**Marshal 纪律（全单元不变量 M-1）**：任何非 UI 线程进入的回调（事件总线 sink、诊断 observer、ICommandInteraction）必须经 `QMetaObject::invokeMethod(qGuiContext, Qt::QueuedConnection)`（或等价 queued signal）转入 UI 线程后才触碰 Widget/模型；FIFO 保持按"同一发布者"排队序投递。

### 3.5 文案键体系与 UI-\* 诊断码

- **文案键（TextKey）**：`cmd.<id>.title`、`stage.<id>.title`、`state.<token>.label`、`diag.<code-lower>.title/detail`（diagnostics 码表键约定）、`plugin.<id>.title`／`plugin.assembly.<state>.label`（UI-T10 冻结登记——关于框插件清单的标题/装配状态呈现值，id 词表＝§11.1 白名单 token、state 词表＝not-assembled/ok/failed；UX-02：token 本身不进用户文本）等；键在本文与各码表冻结，**值（中英文资源）归 ui 文案资源文件**（diagnostics.md P-DIAG-9 交接）。`UiText::resolve(TextKey, params)` 是唯一出口；界面禁止出现哈希、Schema 版本号、内部插件名（UX-02）。（UiText 已随 UI-T09 落位——`UiText.hpp`：内建中文过渡表承载值源〔键不变，迁资源文件只换值源〕＋参数替换纪律＋数值带单位/不适用占位＋哈希形态守卫；`statusWordTransitionalLabel` 等三过渡函数值源已切换至 UiText、键与值逐字不变。§16.7 v1.1。UI-T10 增补键族⑥⑦共 11 键——§16.7 v1.2。）
- **UI-\* 诊断码**（ui 在 StableCodeRegistry 注册，码值权威归 diagnostics 码表；本文登记建议值，最终以注册表现为准）：

| 码 | 类别/严重 | 用户可见 | 语义 |
| --- | --- | --- | --- |
| `UI-HOTKEY-CONFLICT` | internal / Warning | 是 | 全局快捷键重复绑定被注册边界拒绝（冲突键＋已占用命令在 context；数值比较字段标记"不适用"——非数值判定，ERR-01 口径） |
| `UI-CMD-DUPLICATE` | internal / Dev | 否 | 命令 ID 重复注册被拒绝（装配期） |
| `UI-CMD-UNKNOWN` | internal / Warning | 是 | 提交了未注册命令 |
| `UI-CMD-NOT-EXECUTABLE` | internal / Info | 是 | 命令在当前上下文不可执行（含只读模式写命令），显示 reason |
| `UI-PLUGIN-ASSEMBLY-FAILED` | internal / Error | 是 | 白名单插件界面模块装配失败（降级占位显示） |
| `UI-LAYOUT-RESTORE-FAILED` | internal / Dev | 否 | 布局记忆损坏，回退默认布局 |
| `UI-DRAFT-AUTOSAVE-FAILED` | internal / Warning | 是 | 定时草稿落盘失败（保留脏标记；StoreError 详情透传） |
| `UI-SESSION-CONTEXT-INVALID` | internal / Warning | 是 | 已释放上下文的迟到写请求被拒（context-closed 呈现） |
| `UI-QT-PLATFORM-FAULT` | internal / Error | 否 | Qt 平台插件/事件循环异常（PM-17 异常诊断路径；Dev 级） |

`DiagContext.sourceUnit = "ui"`（diagnostics.md §4.2 sourceUnit 词表含 ui）。

---

## 4. 工作台壳与五区布局

### 4.1 布局总图（UX-09）

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ T 顶部工具栏（TopBar）                                                        │
│  [项目入口▾] [阶段导航条(七阶段,§6.4)] [当前方案/工况指示]                      │
│  [保存草稿] [应用修改] [撤销] [重做] [任务状态指示(§9.4)] [只读徽标]            │
├──────────┬───────────────────────────────────────────┬───────────────────────┤
│ L 左栏    │ C 中央工作区（CentralWorkArea）             │ R 右栏                 │
│  项目对象 │  按当前阶段装配的阶段面板（IPluginUiRegistrar│  属性编辑区（当前选择, │
│  树＋阶段 │  注册的面板栈，每阶段一个激活面板）          │  只读投影绑定）         │
│  任务列表 │  阶段 A：占位面板（含"本阶段将在后续版本提供"│  诊断与设置区（诊断摘要│
│  （阶段   │  说明，不虚构业务能力）                     │  ＋工程策略摘要入口    │
│  就绪投影 │                                              │  UX-08）              │
│  驱动）   │                                              │                       │
├──────────┴───────────────────────────────────────────┴───────────────────────┤
│ B 底部任务和状态区（BottomBar，页签式）                                        │
│  [结果] [任务进度] [诊断] [日志] [下一步建议]        状态栏：项目名[*][(只读)] │
└──────────────────────────────────────────────────────────────────────────────┘
      菜单栏：文件/编辑/视图/阶段/工具/帮助            命令面板：Ctrl+Shift+P
      全局快捷键：唯一经 GlobalShortcutRegistry 安装（§7.3）
```

五区均为 `QDockWidget` 族容器（中央区为主视图区），支持停靠、浮动、隐藏与"恢复默认布局"命令（`view.resetLayout`）。命令面板为无边框浮层对话框。

### 4.2 区域所有权、输入投影与交互边界

| 区 | 所有者组件 | 输入投影（只读） | 交互边界（红线） |
| --- | --- | --- | --- |
| 顶部工具栏 | `WorkbenchShell`＋`TopBarPresenter` | UiProjectionStore：项目摘要、当前分支/修订（ProjectMetadataView/BranchTip）、草稿脏标记、UndoRedoStatus、任务汇总（TaskPresentationModel）、只读/锁状态 | 按钮全部路由到 CommandRegistry（`draft.save`/`draft.apply`/`project.undo`/`project.redo`/`workbench.commandPalette`）；**工具栏不直接调用任何领域服务** |
| 左栏 | `StageTaskListPresenter` | StageNavigationModel（阶段状态/就绪投影）、对象树（IProjectQueryPort 对象引用集＋IRuntimeNameResolver 局部名） | 导航请求只经 IStageNavigationModel 发出（进入条件判定归 workflow）；对象树选中只写 SelectionModel（会话态） |
| 中央工作区 | `CentralAreaHost` | 各阶段面板自备（阶段 A 仅占位） | 面板装配经 IPluginUiRegistrar（§11）；面板不得互读控件（ARC-02）；中央区不因面板缺失推断业务能力（§11.4） |
| 右栏 | `PropertyPanelHost`＋`DiagnosticsQuickPanel` | SelectionModel→对象只读投影；DiagProjectionItem 摘要；EngineeringPolicySet 只读摘要（IPolicyProvider） | 属性编辑产生的修改只能进草稿（经域编辑器接口，阶段 B）或会话显示设置；**不得就地写权威对象**；策略"修改"跳转编辑适配器→①命令端口（§6.7） |
| 底部 | `BottomBarPresenter`（结果/任务/诊断/日志/建议页签） | TaskPresentationModel、DiagnosticPresentationModel、Tier-U 日志（ILogger 面板数据）、下一步建议（workflow 提供，阶段 A 占位） | 任务控制（取消/暂停/继续/强杀）只调 ITaskController；诊断表只读，不提供反写；日志面板 flush 仅关闭路径 |
| 状态栏 | `WorkbenchShell` | 标题/状态栏格式 `<显示名>[*][（只读）]`（PM-11）：显示名取 ProjectMetadataView.projectDisplayName；`*`＝存在未应用草稿（DraftProjection.present ∨ 会话脏标记）；`(只读)`＝writable=false | 纯显示；结果状态徽标来自当前性投影，不由 ui 计算 |

### 4.3 状态呈现要点

- **空项目首页**（PM-10）：中央区显示首页（新建/打开/最近项目三入口＋项目状态摘要）。最近项目（上限 10、按规范路径去重、失效项保留＋"项目位置不可用"＋重新选择＋移除）为用户级设置（§4.6）。无项目时：七阶段导航禁用、运行/应用/报告类命令 `enabled=false`（可见性按 PM-10"仅留项目菜单"），命令面板仍可用（可搜索到打开/新建）。
- **只读项目**：`(只读)` 徽标＋锁原因横幅（§5.5）；全部写命令按 §7.6 只读条件禁用。
- **项目关闭/切换中**：五区进入 `Switching` 遮罩（§5.4），仅允许取消切换（在候选验证完成前）。
- **错误状态**：打开失败（StoreError）→ 中央区错误页（具体文件定位＋不动当前项目保证说明）；关闭失败→诊断横幅，不阻塞流程（§5.6）。

### 4.4 响应式尺寸与最小可用布局

- 最小窗口 1280×720（推荐 1920×1080）；低于最小尺寸时左/右栏自动折叠为图标条，底栏折叠为单行状态条＋弹出页签。
- 各区最小内容尺寸：左栏 240 px、右栏 280 px、底栏 160 px、中央区剩余空间；浮动窗口不小于 320×240。
- **最小可用布局**定义：顶栏（单行紧凑）＋中央区＋状态行（8 px 内响应）三要素恒在；其余区可隐藏。恢复默认布局命令把五区恢复出厂位形并清除该会话的浮动窗口记忆。

### 4.5 布局状态归属与恢复

- **布局状态是会话/用户级状态，不是项目持久化数据**：窗口几何、停靠位形、页签顺序、工具栏可见性、命令面板历史，写入用户级设置（QSettings 或用户目录，PM-14），**绝不写入 `.rwdesign`**；用户自定义布局不随项目携带（切换项目保留同一用户布局）。
- 恢复失败（设置损坏/版本不识别）：回退出厂默认布局＋`UI-LAYOUT-RESTORE-FAILED`（Dev 诊断），**不得阻塞启动**；损坏的用户设置段整段丢弃。
- 会话显示开关（渲染分组、线框/透明、碰撞高亮显示、单位显示）同属用户级/会话级，与计算策略开关严格分组异名（UX-08；policy.md POL-ID-3 数据层保证——显示设置不存在于策略字段）。

### 4.6 用户级设置项（ui 拥有，PM-14）

| 设置项 | 默认 | 持久化 |
| --- | --- | --- |
| 窗口/五区布局 | 出厂布局 | 用户级 |
| 最近项目（≤10，路径去重，失效保留） | 空 | 用户级 |
| 命令快捷键绑定（用户改绑） | §7.4 默认表 | 用户级 |
| 草稿定时落盘周期 | 60 s（R2 起 60–600 s 用户可调，PM-04-S1） | 用户级 |
| 上次打开目录 | — | 用户级 |
| 包导出默认勾选记忆 | 全选 | 用户级 |
| 显示单位偏好（KIN-12 显示投影） | m／rad | 用户级（独立于求解配置 KIN-13） |
| 脱敏策略 pathPolicy 配置入口 | RootOnly（diagnostics P-DIAG-7 默认） | 用户级（注入 IRedactionService::setPolicy，L5/ui 会话变更） |

求解配置（AnalysisConfiguration）、工程策略等**不入本表**（独立持久化，KIN-13/ARC-05）。

---

## 5. 会话、项目与存储上下文生命周期

### 5.1 五种生命周期概念的区分

| 概念 | 所有者 | 起点 | 终点 | 内容 |
| --- | --- | --- | --- | --- |
| UI 会话 | ui | WorkbenchShell 装配完成 | UI 会话状态机进入 Closed（界面拆离当前项目） | 五区状态、选择模型、会话投影、DraftController 定时器 |
| 项目会话（界面意义上的"已打开"） | ui | 打开协议成功、投影首建 | 关闭确认完成、界面切走 | 当前 project/branch/revision 投影集合 |
| project 存储上下文（ProjectStore） | project | `ProjectStoreFactory::open/createNew` 成功 | `requestClose()` 后在途引用清零（closed()==true），锁句柄显式释放 | 写锁、对象缓存、命令服务、草稿服务 |
| execution 在途运行 | execution | 任务受理（TaskId 分配） | 终态＋归档 finalize（ResultArchived）或 releaseResources | RunRegistry 登记、worker、检查点 |
| UI 关闭请求 | ui | 用户关闭窗口/项目/切换 | 统一确认对话框得出结论（执行/取消） | 草稿三选＋任务二选（PM-03） |

**分离原则（SA-17/A7）**：UI 会话结束≠存储上下文结束。界面切走后，旧项目存储上下文保持到其在途运行全部接纳归档完成、草稿落盘完成；迟到结果在此期间照常归档（§5.7）。ui 不得提前销毁 ProjectStore 引用（保持 shared 持有直到 `subscribeClose` 回调）。

### 5.2 UI 会话状态机

```
                 ┌──────────┐
   应用启动 ───► │ 无项目    │ NoProject（首页 PM-10）
                 └────┬─────┘
        openProject(path)│（workflow 编排入口→UiSessionController）
                 ┌────▼─────┐
                 │ 打开中    │ Opening（五步协议①~④在 ProjectStoreFactory 内；
                 └────┬─────┘  失败→错误页，停留/回退 NoProject，不动当前项目）
        成功（OpenStoreResult）│
        ┌────────────────────┴────────────────────┐
   writable=true                            writable=false（锁竞争/介质只读/权限）
        │                                          │
 ┌──────▼──────┐                            ┌──────▼──────┐
 │ 已打开/可写   │ OpenWritable               │ 已打开/只读   │ OpenReadOnly
 └──────┬──────┘                            └──────┬──────┘
        │ closeRequest / switchRequest / exitRequest
        │      （统一确认对话框：草稿三选＋任务二选；取消→回到原状态）
 ┌──────▼──────────────────────────────────────────▼──────┐
 │ 关闭请求确认通过 CloseConfirmed（对旧项目执行所选处置）      │
 └──────┬──────────────────────────────────────────────────┘
        │ 若切换：候选项目先经 ProjectStoreFactory 验证（PM-03"候选验证成功才切"）
 ┌──────▼──────────────────────────────────────────────────┐
 │ 等待归档/释放 Draining：                                   │
 │   · store.requestClose()（拒绝新写；返回在途引用数）           │
 │   · 订阅 subscribeClose（归档完成＋草稿 flush＋锁释放后回调）   │
 │   · 任务"等待"：进度可见（TaskPresentationModel 仍读旧项目）   │
 │   · 有界等待（P-UI-8 阈值；超时→强制终止路径，不永久阻塞）      │
 └──────┬──────────────────────────────────────────────────┘
        │ closed()==true（subscribeClose 回调 / closed() 轮询）
 ┌──────▼──────┐   新路径：切换→回到 Opening（打开候选）
 │ 已关闭 Closed │   退出路径→应用退出（先 scheduler.shutdown(DrainPolicy)）
 └─────────────┘
```

状态表：

| 状态 | 进入条件 | 期间允许 | 期间禁止 |
| --- | --- | --- | --- |
| NoProject | 启动/关闭完成 | 新建/打开/最近项目；会话级命令（视图、帮助、关于、设置） | 一切项目作用域命令（PM-10） |
| Opening | 收到打开请求 | 取消（预检阶段） | 对旧项目（若有）的一切变更入口 |
| OpenWritable | 打开成功且 writable=true | 全部命令（按可执行条件） | — |
| OpenReadOnly | 打开结果 writable=false（含请求可写被降级） | 浏览、FK 预览、单位切换、会话命令；草稿查看 | 全部写命令（§7.6）；需写 results/ 的正式评估提交 |
| CloseConfirmed | 对话框确认 | 执行所选草稿/任务处置 | 新任务提交、新命令提交（Draining 前窗口内 project 亦拒绝新写） |
| Draining | requestClose() 已调用 | 只读显示（旧项目任务进度只读投影）；取消等待→（仅当尚未 abandon 时）恢复显示 | 任何写路径；销毁 ProjectStore 引用 |
| Closed | closed()==true | —（立即转 NoProject/Opening） | — |

不变量：

- **INV-SES-1**：`OpenStoreResult.writable` 是只读判定的唯一数据源（OS 排他锁持有状态）；ui 不以心跳、控件状态或"上次结果"推断写权限。
- **INV-SES-2**：切换＝"旧项目关闭流程完成（或进入 Draining 且候选验证通过）→ 新上下文激活"；不存在两个项目同时处于 Open\* 态的界面会话。
- **INV-SES-3**：Draining 期间 UI 会话已切换时（先切后等策略，见 §5.4 S2），旧 store 由 SessionController 的后台持有点保活，直到 subscribeClose 回调后释放。
- **INV-SES-4**：UI 关闭流程不得因归档失败永久阻塞（§5.6 强制终止路径）。

### 5.3 打开期错误与显示差异（PM-07/PM-02/PM-06）

| 场景 | 数据源 | 显示 |
| --- | --- | --- |
| 锁被持（第二实例） | OpenStoreResult.lockInfo（PID/host）＋诊断 `PRJ-LOCK-HELD`（category permission-or-lock） | 只读打开提示横幅："项目正被进程 PID=\<pid\>（\<host\>）编辑，已以只读打开"；actionKind=contact-holder（联系持有者/以只读继续） |
| 介质只读 | StoreError `media-read-only` | "(只读)"徽标＋横幅"存储介质为只读"；actionKind=retry-readonly |
| 权限不足 | `access-denied` | 横幅"无访问权限"；actionKind=contact-holder/inspect-resource |
| 上下文失效（迟到） | StoreError `context-closed`（对已关闭 store 的写请求） | `UI-SESSION-CONTEXT-INVALID` 对话框："项目上下文已释放，操作未执行"；建议重新打开；**不重试写** |
| 旧格式/未来版本 | `format-legacy`/`schema-future` | 稳定拒绝页（诊断码＋当前版本/项目版本＋升级工具入口引导，不自动升级——PM-06；入口编排归 workflow） |
| 打开校验失败 | OpenStoreResult.store 为空＋RecoveryReport.diagnostics | 错误页定位具体文件；**当前项目不动**（仍在原状态） |

心跳过期只产生诊断提示"持锁进程疑似无响应"（project §6.8），ui 不据此改变写权限显示，不提供接管按钮。

### 5.4 关闭/切换时序（与 project/execution 的交接）

```
S1 单项目关闭/退出：
用户→关闭请求 ─► 统一确认对话框（ui）
  数据装配：DraftService::list(branch)＋会话脏模块   → 草稿区（三选）
           ITaskScheduler::tasksByProject(pid) 过滤非终态 → 任务区（二选，9 态短标签）
  [保存草稿] → DraftController.saveAll(Manual)（可写会话）→ 继续
  [放弃]     → 会话脏数据丢弃（磁盘草稿保留策略见 §8.6）→ 继续
  [取消]     → 回到原状态（中止）
  [等待]     → 保持进度可见直到 store.closed()
  [协作取消] → 对每个非终态任务 ITaskController::requestCancel
              （2 s 进入 Canceling/10 s 收敛由 execution 保证，NFR-PERF-02）
确认通过 ─► store.requestClose() ─► Draining ─► subscribeClose 回调 ─► Closed
退出附加：scheduler.shutdown(DrainPolicy)（排空有界）→ 应用退出

S2 项目切换（A→B）：
用户→切换请求 ─► 对话框（同上，针对 A）──► 候选 B 验证：
  ProjectStoreFactory::open(B, 请求模式) 五步协议执行
    失败 → 错误页，A 界面会话不变（PM-03"候选验证成功才切"）
    成功 → A 进入 Draining（后台持有点保活，迟到结果照常归档，§5.7）
         → UI 会话立即绑定 B（不等 A 排空）
         → A 的 subscribeClose 回调后释放 store 引用与锁
```

**迟到任务与旧诊断/旧结果处置（切换时）**：UI 会话切换瞬间（epoch 递增，§6.2）：旧项目任务从"当前任务区"移入"后台任务"只读清单（仍显示进度与归档阶段，不可控制——控制按钮仅对当前会话项目开放，见 §9.4 P-UI-7）；旧诊断/旧结果投影全部失效（epoch 过滤丢弃），磁盘内容不动；B 的投影以 B 的查询端口重建。**A 的迟到结果归档由 execution RunRegistry 五元组核对与 project 归档端口完成，不经 UI，不可能污染 B（AT-10）**。

### 5.5 只读模式下写入口禁用清单

禁用手段＝命令层（CommandRegistry 只读条件，§7.6）＋控件层（TopBar/面板按钮置灰＋tooltip 说明）双保险；**判定源唯一＝writable()**：

| 写入口 | 只读时行为 |
| --- | --- |
| `draft.apply`（应用修改）/域编辑器应用 | 禁用；提示"项目以只读打开" |
| `draft.save`/`draft.discardAll`（DraftService.save/discard 为写操作） | 禁用（草稿可查看不可保存/放弃） |
| `project.undo`/`project.redo`/`scheme.*`/`project.saveAs`(向新目录写时按目标探测)/重关联 | 禁用或按目标上下文判断 |
| 正式评估任务提交（需写 results/） | 禁用；execution 侧同样拒绝（EX-STORE-READ-ONLY——双层防线，ui 预过滤避免无效提交） |
| 策略编辑适配器提交 | 禁用（经命令端口，project 拒绝 not-writable） |
| 报告导出到项目内 reports/ | 禁用；导出到项目外用户选择路径允许（不属项目写） |

会话态操作不受限：浏览、FK 预览、单位显示切换、命令面板会话命令（视图切换等）、三维查看（阶段 B）。

### 5.6 关闭流程的防永久阻塞

1. Draining 等待有界面反馈（在途引用数、任务归档进度）。
2. 协作取消路径受 NFR-PERF-02 有界协议约束（2 s/10 s）。
3. 等待超过阈值 T_force（默认 120 s，装配期可配；构成＝取消协议 10 s＋归档余量；**具体值待裁决 P-UI-8**）：对话框转为"强制结束并关闭"确认——执行 `requestForceTerminate`（任务记 Failed＋EX-FORCE-TERMINATED，最近检查点保留可续）＋ L5 关闭控制器 `abandonAll(ForceTerminated)` 兜底（project §9.7）；随后正常走 Closed。
4. 极端：subscribeClose 回调丢失（实现缺陷）→ 兜底轮询 `closed()`＋T_force2（默认 300 s）后放弃等待、记录 `UI-SESSION-CONTEXT-INVALID`（Dev 级）并退出——**数据损失限制在未归档结果（检查点已保留）**，绝不无限等待。
5. 等待确认（ICommandInteraction 挂起）期间收到关闭请求：project 侧主动取消等待（Aborted，project.md §5.3.4），ui 侧按 §9.2 关闭时序处置，无需特殊顺序。

### 5.7 在途运行、归档与资源释放的关系

- 存储上下文释放的前置＝本实例发起的全部在途运行接纳归档完成＋草稿落盘完成（project 引用计数：归档会话/在途事务/草稿落盘）。ui 触发的 `requestClose()` 只是进入 Draining 的信号，**不催促、不跳过**归档。
- 迟到结果继续归档：UI 会话已切换/已关闭不影响 execution 归档路径（RunRegistry 核对→按登记记录写 `results/<run-id>/`→ResultArchived 事件）；ui 收到该事件时按 epoch 过滤（§6.2）只更新"后台任务"只读清单。
- 已释放上下文的写请求：project 侧拒绝（context-closed）；ui 侧显示 `UI-SESSION-CONTEXT-INVALID`（§5.3），**不重试、不缓存待写**。
- 归档失败（archivePhase=ArchiveFailed）：不阻塞关闭（归档失败不改任务终态——execution §9）；ui 在后台任务清单标记"归档失败"＋诊断入口。

---

## 6. 状态投影、阶段导航与门控

### 6.1 投影管线与线程调度

```
权威方（project/execution/evidence/diagnostics/policy）          ui（全部只读）
┌────────────────────────────────────────┐   事件/通知（无数据，仅身份）   ┌──────────────────────────┐
│ project：RevisionCommitted /           │ ──► UiThreadEventRelay ──►     │ UiProjectionStore        │
│         DependencyInvalidated          │    （Marshal 至 UI 线程，       │  · epoch（会话纪元）       │
│ execution：TaskStatusChanged /         │     同发布者 FIFO 保持）        │  · 快照重建：经各查询端口   │
│           ResultArchived               │                                │    取值拷贝                │
│ diagnostics：目录订阅（IDiagObserver） │ ──► Marshal ──────────────►    │  · 订阅分发（UI 线程）      │
└────────────────────────────────────────┘                                └────────────┬─────────────┘
                                                                          五区/命令可见性/七态 ◄── 消费
```

- **UiThreadEventRelay**（ui 持有，实现 core `IDomainEventSink`）：订阅进程级总线（execution 参考实现 DomainEventBusImpl 或 L5 装配实例），把事件以 QueuedConnection 转投 UI 线程——这是 core.md §4.9"总线 UI 线程投递实现归 ui"义务的落地；同发布者 FIFO 依排队序天然保持。
- **刷新模型＝事件驱动重查（pull-on-event）**：事件不携带数据（core D-09），relay 收到事件后由 UiProjectionStore 在 UI 线程调用相应查询端口重建受影响快照（例：RevisionCommitted → 重查 head()/currentMetadata()/listDrafts()）。进度类走轮询（§9.4），不进事件。
- **快照一致性**：单次重建内的多端口查询非原子——UiProjectionStore 按"修订身份对齐"规则组装：同一次重建中所有快照都标注其取数时的 head RevisionId；重建期间若又收到新事件，则放弃未完成快照、以新事件重启（简单化：投影最终一致，读取方只见完整快照）。
- **旧投影丢弃**：每个快照携带构建时的 epoch；epoch 不等于当前会话 epoch 的快照在分发前丢弃（§6.2）。

### 6.2 会话纪元（epoch）与迟到事件过滤

```
UiSessionController 每次：打开成功 / 关闭完成 / 切换绑定新项目
        │ epoch++（单调 uint64）
        ▼
所有入站事件携带项目身份：
  DomainEvent.payload.project（RevisionCommitted/DependencyInvalidated 直接字段；
                              TaskStatusChanged/ResultArchived 取 task.project）
  DiagContext.project（诊断条目）
        │
        ├─ event.project == 当前会话 project 且 epoch 一致 ──► 更新当前投影
        ├─ event.project == 后台排空项目（Draining 中的旧项目）──► 只更新"后台任务"只读清单（§5.7）
        └─ 其他（无登记项目/更旧会话）──► 丢弃＋Dev 级日志（UI-SESSION-CONTEXT-INVALID 关联，
                                          不产生用户诊断——避免打扰）
```

**迟到事件过滤图**（项目切换后）：

```
项目A运行中任务T ──完成事件迟到──► [epoch 检查]
                                    │ event.project==A，当前会话==B
                                    ▼
                          归类为"后台排空项目事件"
                                    │
              ┌─────────────────────┴──────────────────────┐
              ▼                                            ▼
   UI 呈现：后台任务清单更新（A.T：Completed＋   归档照常（execution→project，不经 UI）：
   Archiving→Archived 徽标，只读）              results/<run-id>/ 写入 A；ResultArchived 事件
              │                                            │
              └──────────── B 的任何投影不更新 ◄────────────┘
   （B 的"当前结果"只来自 B 自己的查询；A 的结果绝不成为 B 的当前结果——TASK-03/AT-10）
```

### 6.3 UX-10 七态状态词表与映射（ui 冻结；P-UI-1 登记需求侧确认）

**七态词表（唯一所有者 ui，NFR-MNT-03）**——token 冻结（小写连字符，与 core 词表同风格）：

| token | 中文 | 触发数据源（权威方） |
| --- | --- | --- |
| `empty-project` | 空项目 | UiSessionState ∈ {NoProject} |
| `incomplete` | 未完成 | 就绪投影无效（任一启用 Must 条目非法——evidence §6.4.1 ①级：ReadinessSummary.invalid；REQ-06"输入未完成"） |
| `computing` | 计算中 | 当前作用域存在非终态任务（TaskState ∈ {Queued, Preparing, Running, Paused, Canceling}） |
| `results-stale` | 结果过期 | 当前性 CurrentnessResult.status==Superseded（附 InvalidationReason 清单）；NotEvaluable 的呈现见 P-UI-2 |
| `data-insufficient` | 数据不足 | 最近正式评估 EngineeringStatus==DataInsufficient（证据/工况缺失，非输入级——输入级归 `incomplete`）；EvidenceManifest 含 Missing/Invalid/Unverified |
| `failed` | 失败 | 最近任务 outcome==Failed（附对象定位与修复建议——诊断 DiagProjectionItem）；Error 级诊断活跃 |
| `computable` | 可计算 | 就绪有效 ∧ 无在途计算 ∧ 无更高优先状态（结果 Current 或尚无结果） |

**七态 × 权威词表映射表**（呈现映射，**不是新判定**——判定全部来自权威方数据）：

| 七态 | core::TaskState（九态） | core::TaskOutcome | core::EngineeringStatus | evidence 当前性 |
| --- | --- | --- | --- | --- |
| empty-project | —（无项目） | — | — | — |
| incomplete | — | — | （输入级 DataInsufficient 之外不使用） | — |
| computing | Queued/Preparing/Running/Paused/Canceling | （非终态无 outcome） | — | — |
| computable | 无活跃任务 | Completed（可选） | Feasible（可选） | Current（可选） |
| results-stale | 无活跃任务 | Completed | 任一 | Superseded（或 NotEvaluable→P-UI-2） |
| data-insufficient | 无活跃任务 | Completed | DataInsufficient | 任一 |
| failed | Failed（终态） | Failed | NotApplicable（表 3 约束） | — |

**求值优先级（自上而下首个命中）**：`empty-project` ＞ `computing` ＞ `incomplete` ＞ `failed` ＞ `data-insufficient` ＞ `results-stale` ＞ `computable`。理由：计算中必须最先呈现（用户提供取消入口 UX-10）；未完成阻断新正式运行；失败与数据不足次之；过期仍可查看历史；全无则可计算。**七态按评估域/阶段分别投影**（每阶段一个七态视图），工作台总徽标＝取各活跃阶段中优先级最高者；底栏"结果"页签提供分域明细。

**九态短标签**（PM-03，execution.md §5.1 语义源的 ui 文案）：排队中/准备中/计算中/已暂停/取消中/已取消/已完成/失败/已中断——文案键 `state.<token>.label`，值归 ui 文案资源。Completed 徽标恒附归档阶段子标（§9.4，"已完成≠已归档"）。

**P-UI-2（承接 evidence P-EV-4）**：当前性"不可判定"（status=空＋诊断：依赖无法解析/跨上下文/资源缺失）的呈现——本文建议：归入 `results-stale` 呈现，原因区显示"当前性无法判定（依赖缺失/上下文变化）"＋诊断列表，**不显示为 Current、不显示为通过**；与 evidence"无默认 Current"规则一致。待需求侧确认后冻结（§16.4）。

**正式通过结论的显示纪律**：`FormalPassEligibility` 五条件（evidence §7.2）为唯一放行数据源；不满足时禁止渲染"正式通过"字样（RPT-05 措辞冻结同样适用于界面——估算值/数据不足/外部验证未完成等限定语保留）。**ui 不得自行由 outcome/工程状态组合出"通过"结论。**

> **UI-T04 落位登记（v0.6，2026-09-21）**：本节词表/映射/优先级的 ui 侧承载随 WP-10-T04 首消费冻结（v0.4"首消费冻结"机制落点，登记于 §16.7 v0.6 行）——七态词表 `ui::StatusWord`（token/中文/键 `state.<token>.label` 经 `statusWordToken/statusWordLabelKey` 承载）、求值入口纯函数 `ui::evaluateStatusWord(StatusFacts)`（触发数据源聚合值：会话态 NoProject 位＋`ReadinessProjection`〔§6.4.1①锚点〕＋`TaskActivityProjection`〔core 词表直用〕＋`CurrentnessProjection`〔§8.1 锚点，NotEvaluable＝status 空＋成因，P-UI-2 口径〕＋`EvidenceManifestProjection`〔§6.2 锚点〕＋`FormalPassEligibilityProjection`〔§7.2 锚点〕）、伴随呈现数据随投影同行（计算中附进度阶段键＋取消位、过期附 `staleReasons`/`notEvaluableCause`、失败附 `failureDiagnostics`、数据不足附 `unsatisfiedEvidence`）、显示纪律唯一实现点 `formalPassRenderable()`、工作台总徽标聚合 `dominantStatusWord()`、九态短标签键 `taskStateLabelKey`（token 段与 core `toToken(TaskState)` 一致；中文值为 UI-T09 UiText 前过渡承载，UI-T03 文案表同案）。C-7 协作面经 ui 自有端口 `IUiCurrentnessSource`（UiPorts.hpp——currentness/evidenceManifest/formalPassEligibility 三只读方法，L5 适配 evidence）注入，产品面零对端链接/include（O-31）。词表与优先级未改一字（P-UI-1）。

### 6.4 阶段导航与门控（UX-12 投影侧）

七阶段（UX-12 顺序，StageId token 冻结）：

```
modeling（建模）→ requirements（需求）→ kinematics（运动学）
→ trajectory-dynamics（轨迹/动力学）→ selection（选型）→ optimization（优化）→ reporting（报告）
```

**阶段状态投影表**（ui 呈现状态；进入条件的业务判定归 workflow 门控，ui 不复制）：

| 阶段呈现状态 | 语义 | 数据源 |
| --- | --- | --- |
| `completed` 已完成 | 阶段就绪且关键产物当前（Current） | workflow 门控输出＋当前性投影 |
| `in-progress` 进行中 | 当前激活阶段（用户所在） | StageNavigationModel.currentStage（会话态） |
| `blocked` 阻塞 | 门控未通过且存在待办（附原因＋下一步建议） | workflow 门控输出（失败定位经诊断/缺项列表） |
| `unavailable` 不可用 | 上游阶段未完成（锁定入口，显示解锁条件摘要） | workflow 门控输出 |
| `not-started` 未开始 | 可进入但从未访问 | workflow 门控输出 |
| `view-only` 只读查看 | 只读项目：阶段可查看、不可执行 | writable=false（§5.5） |

补充呈现规则：

- **失败定位**：blocked/failed 阶段点击展开"缺项清单"（VerdictTrace/missingItems——evidence §13 交接行指定 ui 消费）与关联诊断（subjectObjectId 经名称端口呈现局部名）。
- **下一步建议**：文案数据由 workflow 提供（UX-01）；ui 只呈现与跳转，不生成建议文本。
- **只读模式的区别**：阶段导航全部可点击查看（历史结果/对象），但阶段内"运行/应用"类命令按 §5.5 禁用——"可查看"与"不可执行"以命令层禁用体现，导航不整体锁死（区别于 `unavailable`）。
- **草稿未应用时的导航行为**：切换阶段不强制应用草稿（草稿是模块级的、跨阶段保留）；但离开阶段时若该阶段存在未应用修改，导航条在源阶段标记 `*`（复用未保存标记投影）；进入依赖该阶段产物的下游阶段时，下游阶段若因此 blocked/过期，按门控输出提示"上游有未应用修改"。
- **结果过期但历史可查看**：阶段状态可为 completed＋results-stale 并存（产物在、已过期）：徽标显示"已完成（结果过期）"，历史结果面板可打开（Superseded 保留为原快照历史证据，CON-02），面板顶部固定过期横幅＋原因清单（InvalidationReason）＋"重新计算"命令入口。

**阶段切换时序图**：

```
用户        StageNavigationModel(ui)      workflow门控(未产出,P-UI-6)   UiProjectionStore
 │ 点击"运动学"  │                              │                          │
 │────────────►│ 请求进入 stage=kinematics      │                          │
 │             │──navigate(stage)────────────►│ 门控评估（消费 ui 的       │
 │             │                              │ StageStatusModel 汇聚投影） │
 │             │                             ◄┤ 允许｜拒绝(原因/解锁条件)   │
 │             │◄─结果─────────────────────────│                          │
 │             │ 允许：currentStage=kinematics（会话态）                    │
 │             │   CentralAreaHost 换激活面板（IPluginUiRegistrar 装配的面板）│
 │             │   顶部导航条更新；左栏任务列表切到该阶段就绪投影             │
 │             │ 拒绝：就地提示（不弹模态）：原因＋"查看缺项"＋下一步建议      │
```

> **UI-T09 落位登记（v1.1，2026-09-21）**：本节投影侧承载随 WP-10-T09 落位——①六态合成的实现口径：门控输出（`IUiStageGate::presentStage`，C-12 端口）给出四态之一＋原因/缺项/建议键；`view-only`（writable=false，会话级事实）与 `in-progress`（currentStage，会话态）由 `StageNavigationModel` 按会话事实合成覆盖，其余透传；门控键在一切状态下透传不加工（投影只消费）。②只读导航读法：`RejectedReadOnly`＝§6.4 补充规则"全部可点击查看"的承载——currentStage 更新＋事件照常（消费方装配只读面板），**不经门控评估**（门控判定执行准入，只读浏览不构成解锁，可执行性归 §5.5 命令层）；`Rejected` 则不改 currentStage、无事件，就地提示数据（原因＋缺项＋解锁/建议键）由门控判定值原样携带。③门控/域就绪经 ui 自有端口 `IUiStageGate`/`IUiDomainReadinessSource`（UiPorts.hpp）注入——workflow.md 未产出前以桩承载（P-UI-6 单侧冻结；WP-22-T03 接入时经端口复核），产品面零对端链接/include（O-31）。

### 6.5 StageStatusModel（汇聚投影，供 workflow 消费）

ARCH §3.4：workflow 消费"由 ui 的 StageStatusModel 汇聚"的各域就绪状态投影。本文定义其数据形状（单侧冻结；workflow.md 产出后核对）：

```cpp
struct DomainReadinessItem {          // 单个评估域的就绪投影（数据来自该域插件注册的只读投影）
    std::string domainKey;            // "kinematics"/"trajectory"/...（域注册）
    core::EngineeringStatus verdict;  // 最近正式判定（无则 NotApplicable）
    bool inputComplete;               // 就绪校验结论（REQ-06）
    std::vector<TextKey> missingItemKeys; // 缺项（缺项明细归域/VerdictTrace）
    bool hasActiveTask;
};
struct StageReadinessSnapshot {       // 每阶段一份
    StageId stage;
    std::vector<DomainReadinessItem> domains;
    std::uint64_t epoch;              // 构建纪元（§6.2）
};
```

红线：StageStatusModel **只汇聚**域插件经注册端口上报的只读投影，不计算门控、不判定就绪（输入完整性判定数据来自域就绪校验结果本身）；workflow 的门控结论回填 UI 显示（§6.4 表）——门控状态机唯一，无第二套（约束 N-11）。

> **UI-T09 落位登记（v1.1，2026-09-21）**：本节单侧冻结形状已按原文落位——`DomainReadinessItem`/`StageReadinessSnapshot`（含 epoch 标注）落 `UiTypes.hpp`（§3.3 公共值类型头；避免接口头互 include 成环），汇聚出口＝`IStageNavigationModel::readinessSnapshot`（任意线程拉取、值拷贝、现取现拼、注册序稳定）；汇聚输入＝域就绪源端口 `IUiDomainReadinessSource`（每域一源，多源按注册序拼接）。红线由结构自证：模型不持任何门控判定逻辑，`requestNavigate` 把汇聚快照现取现传给 `IUiStageGate::evaluate`（§6.4 时序数据流），对判定结果零加工（N-11 无第二套）。workflow.md 产出后按本形状核对，不兼容时按影响面增量同步、不私改对端（P-UI-6 处置维持）。

### 6.6 工程用语与对象定位（UX-02）

- 界面文本经 `UiText::resolve(TextKey, params)`；参数中的数值一律带单位显示（core Quantity::displayValueIn——唯一换算入口）；"不适用"字段显示"不适用"占位，不伪造 0。
- 对象定位统一 `subjectObjectId → IRuntimeNameResolver::resolveObjectId → localName`；哈希、Schema 版本、内容身份、内部插件名不进用户可见文本（诊断/版本对话框的组件版本除外——UX-14 关于框显示组件版本属合法场景）。

### 6.7 工程策略摘要入口（UX-08；P-POL-9 适配器宿主）

- 右栏"诊断与设置区"内嵌**策略摘要只读卡**：数据＝`IPolicyProvider::resolvePolicy` 返回的 EngineeringPolicySet 公开字段投影（碰撞域启用/安全间距/过滤对计数/行程上限阈值/判定阈值，含"显式不适用"态）；显示值经单位换算投影，**不改变策略内容身份**。
- "修改策略…"跳转策略编辑适配器（ui 宿主）：表单（阶段 B 详化）→ `IPolicyValidator::validate` 预校验（只校验不发布，全量诊断）→ 组装领域命令经①命令端口提交（新内容身份→新修订）；确认放行（如阈值确认类）由 project 命令边界触发 §9.2 流程。**ui 不持有判定权与计算开关权威；碰撞"计算开关"与"显示碰撞几何/高亮"分组异名（显示开关是会话态，policy 字段中不存在显示设置——POL-ID-3）。**

### 6.8 UI 状态 × 任务状态 × 工程状态 × 当前性正交关系表

四轴互不推导（CON-02 正交的界面表达）；表中"呈现"为组合显示规则，任何一轴不得覆盖另一轴：

| UI 会话态 | 任务状态（示例） | 工程判定 | 当前性 | 界面组合呈现 |
| --- | --- | --- | --- | --- |
| NoProject | — | — | — | 首页；任务区空 |
| OpenWritable | Running | —（非终态无） | — | computing；进度＋取消可用 |
| OpenWritable | Completed | Feasible | Current | computable＋"正式通过结论"可显示（须 FormalPassEligibility 五条件齐） |
| OpenWritable | Completed | EngineeringInfeasible | Current | 可显示"经验证的不可行结论"（正式评审记录口径，RPT-05 两类声明区分） |
| OpenWritable | Completed | DataInsufficient | Current | data-insufficient＋缺失项全量清单 |
| OpenWritable | Completed | Feasible | Superseded | results-stale＋原因＋历史可查看＋重算入口；**不得显示"正式通过"**（过期结论不得沿用） |
| OpenWritable | Canceled | NotApplicable | — | 已取消（正常取消无错误诊断，UX-03）；不显示任何通过/结论 |
| OpenWritable | Failed | NotApplicable | — | failed＋诊断定位＋修复建议＋重试 |
| OpenWritable | Interrupted | NotApplicable | — | 已中断＋"可重跑"（NFR-REL-03） |
| OpenReadOnly | 任一 | 任一 | 任一 | 全部可查看；执行/应用类命令禁用 |
| Draining（旧项目后台） | Archiving | — | — | 后台清单只读；归档阶段徽标 |

**明确禁止**：以 Canceled/Failed/Interrupted 结果显示为"完整通过"；以控件存在/按钮可点推断业务能力；以界面缓存的旧当前性覆盖 evidence 新计算。

---

## 7. 命令注册表、快捷键与命令面板（SA-16）

### 7.1 命令项模型

```cpp
enum class CommandScope { Session, Project, View };   // 会话态／项目作用域／视图作用域
enum class CommandCategory { Project, Edit, View, Stage, Analysis, Report, Workbench, Help };

struct CommandDescriptor {              // 注册时一次性给出，构造后不可变
    CommandId id;                       // "project.new"（点分小写，全局唯一）
    std::string ownerUnit;              // 注册者：ui 内部设施或白名单插件 id
    TextKey titleKey;                   // "cmd.project.new.title"
    std::vector<TextKey> keywordKeys;   // 模糊搜索关键字（UX-13）
    CommandCategory category;
    std::string iconKey;                // 图标资源键（可空）
    CommandScope scope;
    bool readOnlyAllowed;               // 只读会话是否可用（§7.6）
    bool bindable = true;               // 是否可绑定快捷键
    std::optional<QKeySequence> defaultShortcut;   // 默认绑定（可空＝默认无键）
    MenuPath menuPath;                  // 菜单与面板分组（"文件/新建"）
    ParameterSchema params;             // 可选参数模式（无参命令为空 schema）
};
```

注册要素对照（任务清单要求）：命令 ID＝`id`；所有者＝`ownerUnit`；文案键＝`titleKey/keywordKeys`；图标＝`iconKey`；默认快捷键＝`defaultShortcut`；可见条件与可执行条件＝§7.5 谓词；只读条件＝`readOnlyAllowed`；参数模式＝`params`；诊断/确认要求＝命令处理器声明（产生修订的命令在执行期由 project 触发确认流 §9.2；拒绝类结果携带诊断）；菜单与面板分组＝`menuPath`＋`category`；注册顺序＝白名单序（§7.2）。

**最小命令集（UX-13 验收底线，阶段 A 注册壳层命令，业务命令随插件装配）**：

| 命令 ID | scope | readOnlyAllowed | 默认快捷键 | 归属 |
| --- | --- | --- | --- | --- |
| `project.new` | Session | 是 | Ctrl+N | workflow 编排（ui 提供命令项） |
| `project.open` | Session | 是 | Ctrl+O | 同上 |
| `draft.save` | Project | 否 | Ctrl+S | ui DraftController |
| `draft.apply` | Project | 否 | Ctrl+Enter | 域编辑器（经命令服务） |
| `project.undo` | Project | 否 | Ctrl+Z | ProjectCommandService.undoRedo |
| `project.redo` | Project | 否 | Ctrl+Y | 同上 |
| `scheme.switch` | Project | 否 | —（面板可达） | workflow |
| `project.saveAs` | Project | 否 | — | workflow/project |
| `package.export` | Project | 否 | — | io（进度 UI §9.4） |
| `report.export` | Project | 是（导出到项目外） | — | reporting（阶段 B） |
| `analysis.collisionCheck` | Project | 否 | — | 编排归 workflow（P-UI-6/O-26） |
| `view.displayMode` | View | 是 | — | ui 会话态 |
| `view.resetHome` / `view.resetZero` | View | 是 | — | ui 会话姿态（KIN-06 语义：不产生修订） |
| `workbench.commandPalette` | Session | 是 | Ctrl+Shift+P | ui |
| `workbench.closeProject` | Project | 是 | Ctrl+W | ui SessionController |
| `view.resetLayout` | View | 是 | — | ui（恢复默认布局） |
| `help.about` / `help.contents` | Session | 是 | F1（contents） | ui（UX-14） |

### 7.2 注册协议与冲突处理

```
L5 应用壳启动（静态白名单顺序：ui 壳层命令 → 各业务插件命令）
   │ registerCommand(descriptor, handler)
   ▼
CommandRegistry：
  1. id 语法校验（点分小写）＋ owner 在白名单内
  2. 重复 id（含不同 owner）→ 拒绝注册 ＋ 诊断 UI-CMD-DUPLICATE（Dev，装配期）
  3. 写入注册表（registrationOrder＝白名单序，决定菜单内稳定排序）
运行期：注册表只读快照查询；无注销接口
  （SA-01 静态白名单：不存在运行时卸载，故"插件卸载"场景不适用——
   会话级命令不可用由可见/可执行谓词表达，不靠增删注册项）
```

- **重复注册**：一律拒绝（不覆盖、不静默），装配期失败进 AssemblyReport（§11.3）。
- **未知命令**：`submit()` 前查表，未命中→拒绝＋`UI-CMD-UNKNOWN`（含请求 id），不派发。
- **注册顺序**＝白名单装配顺序；同菜单内按 registrationOrder 稳定排序（NFR-COR-02 的界面延伸：同输入同排序）。

### 7.3 GlobalShortcutRegistry（全局快捷键唯一注册点）

```cpp
struct HotkeyBinding {
    QKeySequence key;                   // 规范化序列文本
    CommandId command;
    BindingOrigin origin;               // Default | User
};
// 注册/改绑规则：
//  · 默认绑定：registerDefault(cmd, key)——装配期；同键已被占用 → 拒绝＋UI-HOTKEY-CONFLICT
//    （context 携带冲突键与已占用命令 id；数值比较字段＝"不适用"）
//  · 用户改绑：rebind(cmd, key)——设置界面/命令面板内；同键冲突 → 拒绝＋同一诊断；
//    解绑（key=nullopt）总是允许（该命令退回面板可达——UX-13 兜底）
//  · 快捷键→命令的映射全局唯一；实际 Qt 快捷键对象只由本注册点创建（QShortcut，
//    WindowShortcut 上下文）——业务插件不得自建全局作用域 QShortcut（静态扫描项）
//  · 绑定持久化：用户级设置（PM-14，不入项目）；Default 集随产品版本冻结
```

**全局与局部快捷键的优先级**：局部（面板作用域，插件自持，WidgetWithChildrenShortcut）仅在焦点位于该面板时生效且**优先于全局**（Qt 焦点语义）；全局快捷键在无局部消费时触发；同一键允许"某面板局部＋另一命令全局"并存（焦点决定），但全局表内同键冲突仍拒绝。插件不得把本应局部的作用域键注册为全局（SA-16/NFR-MNT-07 静态检查延伸：扫描插件界面目标中 WindowShortcut/ApplicationShortcut 上下文的 QShortcut 创建——命中即违例）。

**默认绑定表**：见 §7.1（最小集）；未列默认键的命令 `bindable=true` 但无默认绑定——经命令面板模糊搜索可达（可达性兜底）。默认表变更视同接口变更（冻结基线登记）。

### 7.4 命令面板（CommandPalette）

- **面板是 CommandRegistry 的只读投影**：打开时构建快照（id/titleKey/keywords/category/当前可见性），不复制执行逻辑。
- **模糊搜索**：子序列匹配（大小写不敏感）＋词边界前缀加权＋关键字命中加权；得分并列按 registrationOrder 稳定排序；结果上限 50 条，超出显示"继续输入以缩小范围"。
- **键盘导航**：↑↓ 选择、Enter 执行、Esc 关闭、Tab 补全类别；全程无鼠标可达（UX-13）。
- **可见性**：按 §7.5 可见谓词过滤（如 NoProject 时项目命令隐藏或禁用——按 PM-10 采用"禁用＋说明"以保留发现性，首页三入口除外）。
- **历史**：最近使用（会话内，用户级持久化最近 20 条）置顶分组。

### 7.5 可见与可执行谓词

```cpp
struct UiContextSnapshot {              // UiProjectionStore 的只读快照（§6）
    UiSessionState session;
    bool writable;
    bool hasActiveProject;
    std::optional<core::BranchId> branch;
    UndoRedoStatus undoRedo;            // project UndoRedoService::status()
    DraftingSummary drafts;             // §8
    TaskSummary tasks;                  // §9.4
    StageReadinessSnapshot stages;      // §6.5
};
using VisibilityPredicate = bool(*)(const UiContextSnapshot&);
using EnablementPredicate = std::function<std::optional<DisableReason>(const UiContextSnapshot&)>;
// 注册时绑定；求值仅 consuming UiProjectionStore 快照（UI 线程），
// 禁止谓词内发起端口查询/IO（保证 <1 ms，NFR-PERF-01）。
```

- 可执行条件例：`project.undo` ⇔ `undoRedo.canUndo`（空历史稳定提示"没有可撤销的操作"——PM-18）；`draft.apply` ⇔ `writable ∧ drafts.hasApplicable ∧ !tasks.blockingApply`。
- 谓词结果是**界面使能态**，不是业务判定：命令真正执行仍由目标服务校验（project 命令边界断言、execution 提交校验）；界面使能态错误最多造成一次被拒绝的提交（有诊断），不会绕过权威校验。

### 7.6 只读条件

命令在 `readOnlyAllowed=false` 且 `UiContextSnapshot.writable==false` 时：菜单/面板/工具栏三处一致禁用＋原因文案；强行编程提交→`UI-CMD-NOT-EXECUTABLE`＋拒绝（双保险，权威拒绝在 project/execution 侧：not-writable／EX-STORE-READ-ONLY）。

### 7.7 命令注册、快捷键与命令执行时序

```
装配期：
L5壳 ─registerCommand(desc,handler)→ CommandRegistry（冲突拒绝＋诊断）
    ─registerDefault(cmd,key)→ GlobalShortcutRegistry（同键拒绝＋UI-HOTKEY-CONFLICT）
    （业务插件经 IPluginUiRegistrar 间接注册，§11）

运行期（产生修订的命令，例：draft.apply）：
用户 Ctrl+Enter ─► GlobalShortcutRegistry（键→命令映射）
  ─► CommandRegistry.submit("draft.apply")
       ① 未知/不可执行/只读 → 拒绝＋诊断（UI-CMD-*）
       ② handler：域编辑器以草稿负载组装 CommandEnvelope
          （expectedRevision=draft.baseRevisionId，PM-04）
       ③ ProjectCommandService.submit(envelope, CommandInteractionBridge)
            ├─ 硬断言失败 → Rejected(hard-assert-failed)＋诊断（就地定位）
            ├─ 策略校验超限 → ConfirmableFinding 集合
            │     └─ 命令执行线程回调 ICommandInteraction.requestConfirmations
            │          └─ Bridge Marshal→UI 线程确认对话（§9.2 时序）
            │               ├─ 用户确认 → credentials → 编译前复核四元组 → 放行
            │               └─ 拒绝/关闭 → Rejected(confirmations-rejected)，不产生修订
            ├─ StaleRevisionRejected → Rejected(stale-revision)＋冲突定位（§8.5）
            └─ 成功 → CommandResult.status=Committed＋newRevision
       ④ CommandResult → 命令完成呈现（工具栏状态/诊断/事件驱动投影刷新）
       ⑤ project 发 RevisionCommitted→DependencyInvalidated → UiThreadEventRelay
          → UiProjectionStore 重建 → 五区刷新（§6.1）

运行期（会话态命令，例：view.resetHome）：
用户 ─► CommandRegistry.submit("view.resetHome")
  ─► handler 只写 ui 会话态（SelectionModel 会话姿态）——不产生修订、不触发失效、
     不进缓存身份（ARCH §7.7 / KIN-06 / AT-04）
```

**结果回 UI 的统一口径**：命令执行的同步结果＝CommandResult（或会话命令的本地结果）；执行期确认请求＝经 Bridge 的对话；失败诊断＝经 IDiagnosticSink 进入诊断目录→DiagnosticPresentationModel（§9.1）——三条通道不混用、不丢失。

---

## 8. DraftController 与草稿交互

### 8.1 分工红线

| 职责 | 归属 |
| --- | --- |
| 草稿数据模型（DraftDocument：schemaVersion/三元组/baseRevisionId/payload/externalRefs/origin） | project |
| 落盘、原子替换＋.bak、损坏检测与恢复数据 | project DraftService |
| 应用时的命令组装（草稿负载→CommandEnvelope） | 各域编辑器（业务插件）＋project 处理器 |
| 定时器（默认 60 s）、手动保存入口、恢复编排、跨模块汇总视图、未保存标记、关闭对话数据装配 | **ui DraftController** |
| 保存草稿产生修订？ | **否**——save 只落 drafts/；应用经 ProjectCommandService 恰好产生一个新修订（PM-04） |

### 8.2 组件与数据流

```
域编辑器(插件)            DraftController(ui)              DraftService(project)
   │ 会话编辑态               │                                │
   │ dirty 通知/模块句柄 ───► │ ModuleDraftTable                │
   │                          │  · 定时器 QTimer（UI 线程，60 s） │
   │                          │  · 脏模块集合（会话态）           │
   │   buildDraftDocument()   │                                │
   │ ◄────────────────────────│ 触发（autosave/manual）          │
   │ ──DraftDocument────────► │ 转投 ui 后台落盘线程 ──────────► │ save(doc)
   │                          │  （UI 线程零阻塞）                │  · 原子替换＋.bak
   │                          │ ◄─ SaveResult ──────────────── │  · 不产生修订
   │                          │ 成功→清脏标记＋状态栏`*`更新       │
   │                          │ 失败→保留脏标记＋UI-DRAFT-AUTOSAVE-FAILED
```

- **多模块草稿汇总**：DraftController 维护模块表（moduleId→{会话脏, 最近保存 origin/时间}），磁盘侧以 `DraftService::summarize(branch)`（DraftProjection）为权威；汇总视图＝两者合并（会话脏标记 OR 磁盘 present），跨模块"未应用修改"总标记供标题 `*`（PM-11）与关闭对话框（PM-03）。
- **草稿与当前修订**：磁盘草稿的 baseRevisionId 由 project 校验（stale 标记来自 DraftProjection/DraftInfo）；ui 呈现"基线已前进"提示，不在 ui 侧判定冲突细节。

### 8.3 草稿恢复（打开时，PM-04/PM-08/PM-15）

打开成功后（OpenStoreResult.recovery 携带 orphanDraftFiles 等）：

1. `DraftService::list(branch)` → 各模块 `tryLoad(branch, moduleId, diags)`；
2. 损坏（DraftCorruptDetected/`draft-corrupt`）→ 尝试 `.bak` 旧版恢复；`.bak` 可用→以 `.bak` 内容恢复＋诊断提示"草稿已从备份恢复"；**旧草稿文件保留（不删除）**，供人工核查；
3. `.new` 残留（保存崩溃现场）→ project 侧已丢弃并报告；ui 在恢复横幅显示；
4. 孤儿草稿（RecoveryReport.orphanDraftFiles）→ 恢复横幅一句话汇总＋"查看详情/恢复草稿/放弃"（PM-15）；放弃＝显式 discard（写操作，只读模式不可用）；
5. 恢复的草稿进入域编辑器（阶段 A：仅登记机制，域编辑器随阶段 B 交付；阶段 A 测试用桩模块验证控制器协议）。

### 8.4 定时器与线程纪律

- QTimer 在 UI 线程触发（默认 60 s；R2 起用户可调 60–600 s，PM-04-S1，用户级设置 §4.6）；触发时只做"收集脏模块→向域编辑器要文档→投后台线程调用 save"，**UI 线程不做磁盘 IO**；
- 落盘线程串行（同一时刻至多一个 save 在途）；save 期间的新脏数据进入下一周期（不合并半成品文档）；
- 手动保存（Ctrl+S）与定时保存共用 DraftService 同一入口（origin=manual/autosave），即时反馈（保存中→完成/失败）；
- 关闭流程中的 `saveAll(Manual)` 在确认对话框上下文同步完成（有进度提示；失败→对话框内报告并允许改选"放弃/取消"）。

### 8.5 应用与 StaleRevisionRejected（RV-10）

```
用户 draft.apply →（§7.7 时序）
  Rejected(stale-revision)：
    ui 弹"草稿基线冲突"对话框：
      · 显示：当前分支 tip（RevisionView 摘要＋命令摘要）vs 草稿 baseRevisionId
        （冲突定位数据由 CommandResult 附带——project §6.2：当前 tip/前进修订摘要/涉事对象差异）
      · 选项：[基于当前版本重新编辑]（推荐）/ [查看差异对象] / [取消]
      · 草稿保留（origin=apply-retained，project 侧已处置）
    "重新编辑"＝域编辑器以当前 HEAD 重建编辑基线（用户迁移未应用修改），
    ui 重置该模块会话脏标记与 baseRevision 显示；不得自动重试提交
```

**StaleRevisionRejected 提示并允许继续编辑**：拒绝永远不销毁草稿；用户编辑不中断。撤销/重做跨分支与过期修订遵循同一命令契约（PM-18）。

### 8.6 关闭/切换/分支切换/只读的草稿处置

| 场景 | 处置 |
| --- | --- |
| 关闭项目（PM-03 草稿三选） | 保存（saveAll→可写会话）／放弃（会话脏数据丢弃；**磁盘草稿不删除**——下次打开仍可恢复）／取消 |
| 切换项目 | 同关闭（针对旧项目） |
| 分支切换（PM-12） | 切换前先处置未应用草稿（PM-04 规则同款三选）；草稿绑定三元组（project/branch/module），不跨分支迁移 |
| 只读打开 | 草稿可查看（list/tryLoad），save/discard 禁用（§5.5）；关闭对话框的草稿区只提供"放弃会话内修改（不落盘）/取消" |
| 应用成功后 | 磁盘草稿由 project 侧处置（应用即消费）；ui 清会话脏标记 |

### 8.7 草稿局部撤销与项目命令撤销的边界（N-10/PM-18）

- **草稿内编辑级局部撤销**：域编辑器＋DraftController 会话栈（REQ-11 需求集局部撤销同族），**不经命令服务、不产生修订**；
- **项目命令级撤销/重做**：`project.undo/redo` 经 UndoRedoService（逆命令→新修订，不改写历史）；
- 两栈独立、互不越界：局部撤销不能撤销已应用修订；项目撤销不触碰当前未应用草稿内容（草稿 baseRevision 冲突由 §8.5 流程处理）。

---

## 9. 诊断、确认和任务 UI 契约

### 9.1 诊断呈现（UX-03/PM-15/NFR-REL-05）

**输入面（diagnostics.md §12.2 交接行，全部只读值拷贝）**：`DiagProjectionItem`（entryId/code/titleKey/detailKey/category/severity/subject/localName/runtimeName/actionKind/comparison/occurrences/aggregatedUnder）、actionKind 动作族→呈现映射、确认对话数据（比较型三要素＋optionKeys）、Tier-U 日志面板数据。

| 呈现件 | 数据 | 规则 |
| --- | --- | --- |
| 诊断表（底栏"诊断"页） | DiagProjectionItem 过滤（severity/category/subject/task/revision——DiagQuery） | 只读；按 orderKey 稳定排序；同 dedupKey 折叠显示 occurrences（聚合导航 aggregatedUnder）；**ui 不得反写目录、不得以投影文本反查原文** |
| 诊断详情 | 原因链展开（causedBy 单父 DAG 逐级上溯）、relatedTo 导航 | 对象定位经名称端口局部名（UX-02） |
| 建议动作 | actionKind→动作映射表（ui 冻结呈现层映射）：fix-input→跳转编辑；contact-holder→显示 PID；rerun-interrupted→重跑入口；confirm-or-fix→确认对话（§9.2）；inspect-log→日志页；… | 动作执行全部路由 CommandRegistry 或跳转，不直调领域服务 |
| 比较型三要素 | comparison.actual/expected（SourcedValue 四态）＋unit | 数值＋单位同显；NotApplicable/Invalid 显示"不适用/无效"，不伪造数值 |
| 恢复横幅（PM-15） | RecoveryReport＋orphanDraftFiles＋中断任务 | 一句话汇总（未完成保存已忽略/任务已中断/检测到未保存草稿）＋查看详情/恢复草稿/放弃 |
| 日志面板 | Tier-U（用户诊断：无调用栈/内部哈希） | flush 仅关闭路径；Dev 级仅"打开开发日志文件"跳转（不内嵌显示） |
| 脱敏 | IRedactionService 配置入口（§4.6 pathPolicy） | 呈现层不自行脱敏——显示前文本已由 diagnostics 管线处理；ui 只提供配置界面（L5 会话变更 setPolicy） |

**呈现映射（分类→去向，diagnostics.md §4.4 UI 映射列的 ui 落地）**：输入非法→失败态＋对象定位；权限或锁→只读横幅；可确认→确认对话；取消→无错误呈现；中断→"已中断"可重跑；内部→开发日志/恢复横幅。**ui 不改变诊断事实、阈值、严重级别；正常用户取消不产生错误呈现（UX-03）。**

### 9.2 确认流（SA-15／P-PR-7 交叉核对结论）

**CommandInteractionBridge（ICommandInteraction 的 ui 实现）**：

```cpp
class CommandInteractionBridge final : public project::ICommandInteraction {
public:
    // isAlive()：绑定 UiSessionController 存活探针——UI 会话在拆除（窗口关闭/项目切换
    // 的 Draining 之后）时置 false；命令等待期收到关闭请求由 project 主动取消（§5.6-5）
    bool isAlive() const noexcept override;                 // 任意线程（原子读）
    std::optional<std::vector<core::ConfirmationCredential>>
    requestConfirmations(const std::vector<core::ConfirmableFinding>&) override;
private:
    // 调用发生在【命令执行线程】（project.md §5.3.3）：
    //  1) QMetaObject::invokeMethod(bridgeObject, Qt::QueuedConnection) 弹确认对话（UI 线程）
    //  2) 以 QEventLoop/QFuture 阻塞命令执行线程等待用户结果（占命令槽、零事务资源）
    //  3) 桥内禁止调用命令服务/查询端口以外任何写入口（防重入死锁）
    //  4) 会话关闭→isAlive 探针触发循环退出→返回 nullopt（Aborted(interaction-lost)）
};
```

**ConfirmableFinding UI 回调时序图**：

```
project命令线程                Bridge(ui)                UI线程确认对话           diagnostics
     │ requestConfirmations     │                           │                      │
     │ (findings,隐式token) ───►│ Marshal(queued) ────────► │                      │
     │ （阻塞等待，占命令槽、     │                           │ 对话装配：            │
     │   零事务资源，无超时自动   │                           │  · 每条 finding：     │
     │   确认）                  │                           │    比较型三要素        │
     │                          │                           │    （实际值/阈值/单位） │
     │                          │                           │    subjectScope 对象   │
     │                          │                           │    optionKeys→按钮文案 │
     │                          │                           │    baseRevision 显示   │
     │                          │                           │  · 批量一次呈现        │
     │                          │                           │    （逐条选择/全部确认）│
     │                          │ ◄── 用户结果 ────────────── │                      │
     │                          │                           │ （若等待期收到         │
     │                          │                           │  RevisionCommitted：  │
     │                          │                           │  对话标注"输入已变化"  │
     │                          │                           │  ——权威失效由 project  │
     │                          │                           │  编译前复核决定）       │
     │ ◄─ credentials/拒绝/nullopt                          │                      │
     │ submitConfirmations(token,credential) ──────────────────────────────────────►│
     │                          │                           │   复核绑定四元组        │
     │                          │                           │   {findingDigest,      │
     │                          │                           │    policyContentId,    │
     │                          │                           │    commandDigest,      │
     │                          │                           │    baseRevisionId}     │
     │ 编译前再复核一次（不符→凭据作废，按未确认处置 confirmations-unresolved）        │
     │ → 确认照常执行硬断言与双编译（确认≠成功）                                        │
     │ → Committed：CommandRecord.confirmations[] 留痕（credential+四元组）→ finding 清理
```

**规则明细**（对应 diagnostics §5.3～§5.6 契约）：

| 主题 | ui 侧行为 |
| --- | --- |
| 线程调度 | 命令执行线程同步调用；Bridge 内 Marshal＋阻塞等待（P-PR-7 起点＝project.md §5.3.3，本次交叉核对通过，结论登记 §16.3 D-4） |
| principal 采集 | `ConfirmationCredential.principal`＝Windows 用户名（会话启动时采集缓存，不逐次弹问）；confirmedAtUtc 由组装时刻填充 |
| 回调生命周期 | Bridge 强引用由命令服务持有至返回；isAlive==false/抛出→Aborted(interaction-lost)，不产生修订 |
| 确认记录返回 | ui 只交 credentials；submitConfirmations/留痕/清理全在 project＋diagnostics；**ui 不持有判定权、不自行放行命令** |
| 未确认不提交 | 对话取消/拒绝→空 optional 或含 rejected→Rejected(confirmations-rejected)，无修订 |
| 超时 | 对话无自动超时（findings 默认 expiresAt 空——P-DIAG-6）；等待可被关闭请求打断（project 主动取消）；**无永久等待** |
| 输入变化后失效 | 权威判定在 project（提交时＋编译前双复核）；ui 侧对话标注提示但不代替判定；凭据失效后 finding 回 Pending 可重新确认（新对话） |
| 多确认排队 | 无排队设施——一次命令一批 `requestConfirmations` 集合呈现；不同命令的确认不共享 callbackToken |
| 关闭窗口未完成确认 | 事件循环拆除→isAlive=false→Aborted＋finding Invalidated(interaction-lost)；不产生用户诊断；已完成输入且四元组有效→照常放行（用户意图已表达且输入未变） |
| 确认后重新校验失败 | 编译前复核不符→按未确认处置→Rejected(confirmations-unresolved)＋提示重新确认；硬断言失败→就地阻止＋定位（确认不豁免编译） |
| 非命令期 pending 查看 | `pendingFor(revision)` 投影只读显示（ui 经 project 间接读——不直接调 diagnostics 服务端接口） |
| worker 约束 | worker 不产生确认（接口不在 worker 装配暴露）；一切 Widgets 交互只发生在主进程 UI 线程 |

### 9.3 确认对话数据契约（ui 消费）

FindingRecord 投影字段（diagnostics.md §5.2）→ 对话呈现映射：finding.record（比较型三要素/subject/localName/context/cause/recommendedAction）→主文区；subjectScope→影响对象列表；confirmTextKey/optionKeys→按钮文案键（UiText 解析）；sourceCommandType/commandPayloadDigest→仅 Dev 折叠区（默认隐藏，UX-02）；policyContentId→"策略版本"折叠提示（不显示哈希原文，显示"已随当前策略版本绑定"）。

### 9.4 任务进度、取消与结果显示（execution §13 交接行落地）

**输入面**：TaskState 九态事件流（TaskStatusChanged）、ProgressReport 投影（查询式，调度器节流 ≤10 Hz）、TaskSnapshot（PM-03 任务清单数据）、EventBusImpl UI 线程 marshaling 扩展点（§6.1 Relay 承担）。

| 呈现件 | 数据/接口 | 规则 |
| --- | --- | --- |
| 任务清单（底栏"任务进度"＋关闭对话框） | `ITaskScheduler::tasksByProject(projectId)` 快照 | 9 态短标签（§6.3）；当前任务置顶；**UI 不阻塞等待任务**（提交即返回 TaskId，全部非阻塞——execution §6.1） |
| 进度 | UI 线程 200 ms 轮询 `ITaskController::progress(taskId)`＋状态事件即时刷新 | percent/phaseToken（进度阶段文案键）/batchesDone/Total；**乱序/重复处理**：同 (taskId, attemptId) 内 percent 单调（回退值丢弃＋Dev 日志）；新 attempt 重置基线；重复值幂等 |
| 队列位置/资源占用 | execution 未提供对外接口（§12 登记 UI-EXEC-1 观测限制） | 只显示"排队中（等待资源）"；资源不足经 EX-RESOURCE-INSUFFICIENT 诊断呈现；**不推测、不编造数值** |
| 暂停/继续 | requestPause/requestResume | 按任务能力声明（TaskSnapshot.capability）启用；不支持暂停的任务收到请求→StatusAck.feedback 显式提示（不静默） |
| 取消/强制终止 | requestCancel / requestForceTerminate | 取消＝协作（正常取消无错误诊断）；强杀＝独立高级操作（带确认＋后果说明：任务记 Failed、检查点保留）；**关闭对话框不提供强杀选项**（execution §7.4） |
| worker 崩溃 | Failed＋EX-WORKER-CRASHED | 主界面不退出（NFR-REL-02）；诊断＋"重跑"（新 TaskId——崩溃不自动重试 D-09） |
| 检查点可恢复 | listCompatible（ICheckpointCoordinator） | 恢复/续跑入口由编排方（workflow/域插件）提供，ui 呈现"可从检查点续跑"标记 |
| 结果分批到达 | 结果明细归域面板（阶段 B）；ui 不消费 ResultBatch（execution 内部通道） | 消费侧分页（NFR-PERF-03）：明细面板分页加载，不一次性装全量 |
| 归档中 | TaskSnapshot.archivePhase（NotApplicable/Reserved/Archiving/Archived/ArchiveFailed） | **任务完成≠归档完成**：Completed 徽标附归档子标；ArchiveFailed→警告标记＋诊断（EX-ARCHIVE-FAILED） |
| 结果当前性 | evidence CurrentnessIndex/CurrentnessResult | 徽标 Current/Superseded（附原因）——**由 evidence 提供，ui 不计算**；过期历史可查看（§6.8） |
| 历史结果查看 | project 查询端口 listRuns/runDir | 绑定修订浏览；不成为"当前结果"（TASK-03） |
| 关闭项目后旧任务 | 后台任务只读清单（§5.4/§6.2） | 显示进度与归档阶段；**控制按钮禁用**（取消/暂停只对当前会话项目开放——控制接口以 TaskId 可达，ui 按项目归属自限；P-UI-7 登记开放范围裁决） |
| 迟到结果不污染新会话 | epoch 过滤（§6.2） | 旧项目事件只进后台清单 |

**任务进度、取消和归档流程图**：

```
提交(命令面板/面板按钮) ─► ITaskScheduler.submit ─► TaskId(Queued)
   │ 事件：TaskStatusChanged(Queued→Preparing→Running)（Relay→UI 线程）
   │ 轮询：progress()≤10Hz 有效数据 ─► 进度条＋阶段文案
   │
   ├─ 用户[取消] ─► requestCancel ─► Canceling（2 s 内，停止派发新批次）
   │        ├─ 10 s 内批次自然结束 ─► Canceled（无错误诊断，UX-03）
   │        └─ 超时→用户另行[强制终止] ─► Failed(EX-FORCE-TERMINATED)＋检查点保留
   ├─ 用户[暂停]/[继续] ─► Paused⇄Running（新 attemptId；继续不重复统计）
   ├─ worker 崩溃 ─► Failed(EX-WORKER-CRASHED)＋重跑入口
   └─ 正常完成 ─► Completed（≠归档完成）
              └─ archivePhase: Reserved→Archiving→Archived
                    │ 事件：ResultArchived → 当前性计算(evidence)
                    │   ├─ Current ─► 徽标"当前"，可消费
                    │   └─ Superseded ─► 徽标"过期"＋原因＋历史可查看
                    └─ ArchiveFailed ─► 警告＋诊断（不改任务终态）
```

### 9.5 关闭对话框任务区与 DrainPolicy 对接

- 对话框任务清单＝tasksByProject(当前项目) 过滤非终态（9 态短标签，PM-03 内嵌清单）。
- "等待"＝进入 Draining 显示归档进度（§5.4 S1）；应用退出时由 L5/workflow 调 `scheduler.shutdown(DrainPolicy)`＋`drained()`（有界排空）。
- **P-UI-3（文档不一致登记，已销账）**：execution.md §7.5 原使用 `DrainPolicy::WaitForInFlight`（§10.1 枚举中不存在）——execution.md v0.2 已更正为 `CancelQueuedAndWait`（2026-09-10），与本文"关闭等待＝取消排队、在途归档后收口"的对接语义一致；ui 按 §9.5 对接。

---

## 10. 公共接口详细设计

> O-31 裁决注（2026-09-19）：本节及 §4～§9、§11 行文中出现的对端类型（§3.2 清单）自 v0.4 起一律经 §3.3 UiPorts/UiProjections 承载（ui 自有最小端口接口＋值投影，L5 装配适配）；对应接口签名随首个消费该面的 UI 任务实现冻结并做单元卡增量修订登记（CCP §5 载体），需求语义零变化。**v0.5 首消费冻结登记（WP-10-T03）**：①`UiPorts.hpp` 落位 C-10/C-11 两端口——`ui::IPolicySummarySource::summary()→PolicySummaryProjection`（计数形态：available/collisionDomainEnabled/thresholdCount/filterPairCount/explicitNotApplicableCount；逐字段细化随 UI-T07 首消费登记）、`ui::IUiNameResolver::resolveObjectId(core::ObjectId)→std::optional<std::string>`（runtime.md §7.3 localName 语义的 ui 侧投影，失败→nullopt——呈现占位，R-4 不变）；②`UiProjections.hpp` 落位 `ProjectMetadataProjection`（projectId/projectDisplayName/writable——ProjectMetadataView 语义锚点）、`DraftPresenceProjection`（present/sessionDirty，`*`＝present∨sessionDirty——§4.2 状态栏行原文）、`ProjectContextProjection`（元数据＋草稿位复合快照，nullopt＝无项目首页态）、`RecentProjectEntry`、`PolicySummaryProjection` 及 **PM-11 格式唯一权威纯函数** `formatProjectStatusText`（`<显示名>[*][（只读）]`；无项目→"未打开项目"）；③`WorkbenchShell` 实现随本任务落位（五区 Dock/布局记忆含损坏回退/无项目首页/状态栏/壳层命令门控），落位细节见 §10.1 增注。

本章九个接口全部位于 `sdurws::ird::ui`（§3.3 头文件）。通用约定：所有接口为抽象类（纯虚），实现类由 ui 库提供并经 `IWorkbenchShell` 装配门面获取；除非单独注明，**方法只允许在 UI 线程调用**（违规＝未定义行为＋DT 断言）；返回值一律值语义/只读引用，接口不交出内部可变状态。

### 10.1 IWorkbenchShell（工作台壳门面）

> **v0.5 增量冻结（WP-10-T03＝本门面与 wiring 的首消费任务；下述代码块即落位签名，与实现文件逐字一致）**：
> ①`ShellWiring` 增成员 `devLog`（`diagnostics::IDevLogSink`，**允许为空＝无日志测试场景，与 eventBus 同款"须显式声明"**）：UI-LAYOUT-RESTORE-FAILED 等 Dev 级码**不入目录**（diagnostics §6.2 append 拒绝 Dev），其唯一出线＝开发日志通道；v0.4 签名无该通道则 §4.5"布局损坏回退＋Dev 诊断"不可观测——增量为 diagnostics 表内边成员补全，不触及 O-31 裁决主体（零对端业务单元边），登记为本表"首消费冻结"机制的首个落点；
> ②`initialize` 后置条件行"壳层命令集（§7.1）与默认快捷键已注册"的阶段 A 落位口径：本任务落位**壳层命令板**（§7.1 表九行：project.new/open、draft.save/apply、project.undo/redo、workbench.commandPalette、workbench.closeProject、view.resetLayout——scope/readOnlyAllowed 逐行承载；登记与可用性门控先于派发，完整注册表/冲突处理/面板投影归 UI-T06 §10.3/§7.2/§7.4）；**默认快捷键不在此安装**（§7.3 唯一注册点归 GlobalShortcutRegistry——UI-T06），菜单/面板可达性即本阶段的"注册"语义；
> ③`shutdown` 的 `ShellTeardownReport`＝`{wasInitialized, layoutPersisted, completed}`；"有界"阶段 A 口径＝壳自身拆卸（布局落盘＋Widget 销毁）同步有界，项目 drain 协议（§5.6）归 UiSessionController（UI-T11）；
> ④阶段 A 增列门面 facets（首消费冻结，后继任务增量扩展）：`regionVisible/setRegionVisible/resetLayout`（§4.4 五区可见性与出厂复位——Top/Central 恒在）、`presentProjectContext(ProjectContextProjection)`（§4.2 状态栏/首页/门控的原子快照注入口；UI-T11 UiSessionController 落地前的唯一上下文入口）、`commandAvailability(commandId)→ShellCommandAvailability{registered,visible,enabled,reasonKey}`（§7.5 求值口径的壳层子集）、`recentProjects/noteRecentProject/removeRecentProject`（PM-10/PM-14）、工厂函数 `createWorkbenchShell()`（具体实现类封闭于库内——R-2）与 `uiDiagnosticCodeDescriptors()`（§3.5 九码描述符供体，L5 装配期与 builtinCodeDescriptors 同序注册——§10.1 副作用行"注册 UI-* 诊断码（经 diagnostics 码表）"的落地面：wiring 不携注册表句柄〔v0.4 裁决签名不扩〕，注册动作归 L5 装配序列）；
> ⑤实现细节口径：布局/设置写盘走 ui 自有后台落盘线程（§3.4"1 条"——值任务串行队列，UI 线程零写盘）；布局记忆格式版本化（version 键），损坏/版本不识别→出厂默认＋UI-LAYOUT-RESTORE-FAILED（Dev，经 devLog）＋损坏段整段丢弃、不阻塞启动（§4.5）；无项目首页三入口/各区占位文本为 UI-T09 UiText 落地前的壳内文案表过渡承载（UX-02 值随迁移，键体系不预建）；阶段占位面板/未接线命令触发的占位说明＝§4.1/§11.4 契约显式设计（不虚构业务能力），非未完成实现；零 Q_OBJECT 声明（虚函数＋既有信号＋lambda 接线），构建不开 AUTOMOC（UI-T06+ 引入 Q_OBJECT 时再评估）。
>
> **v1.2 增量冻结（WP-10-T10；UI-T10 为 `aboutSource` 成员的首消费任务）**：`ShellWiring` 增成员 `aboutSource`（`ui::IUiAboutDataSource`，UiPorts.hpp v1.2 增端口——§11.4 关于框数据＝装配报告半区〔§10.9 assemblyReports 查询语义，值形状 PluginAssemblyReport〕＋冻结版本基线半区〔NFR-DEP-05 呈现值面〕；**允许为空＝无关于数据测试场景，与 eventBus/devLog 同款"须显式声明"**——为空时 help.about 处理器照常打开关于框，清单＝白名单占位行、版本区「未装载」占位，零虚构数据）。L5 适配点：装配器（IPluginUiRegistrar 落位前报告为空集）与冻结基线记录（WP-24-T01 产出前 available=false）。help.about/help.contents 两命令自本版起为壳自持处理器（§7.1 归属"ui"行兑现——不再走占位说明处理器）。

```cpp
struct ShellWiring {                       // L5 应用壳装配期注入（全部运行时注入，零编译依赖）
    std::shared_ptr<core::IDomainEventBus> eventBus;         // 允许为空＝无事件测试场景（须显式声明）
    std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
    std::shared_ptr<diagnostics::IRedactionService> redaction;
    std::shared_ptr<diagnostics::IDevLogSink> devLog;        // v0.5 首消费冻结：Dev 码唯一出线（§6.2"Dev 走日志"）；允许为空＝无日志测试场景（须显式声明）
    std::shared_ptr<ui::IPolicySummarySource> policySource;   // O-31 裁决：ui 自有端口（§3.3 UiPorts），L5 适配 policy::IPolicyProvider——§6.7 策略摘要只读面语义不变
    std::shared_ptr<ui::IUiNameResolver> nameResolver;   // O-31 裁决：ui 自有端口，L5 适配 runtime::IRuntimeNameResolver——resolveObjectId 取 localName 语义不变（§3.1 红线）
    std::shared_ptr<ui::IUiAboutDataSource> aboutSource;  // v1.2 首消费冻结：关于框数据源端口（§11.4 装配报告＋冻结基线，L5 适配）；允许为空＝无关于数据测试场景（须显式声明——空时关于框以占位呈现零虚构）
    // project/execution 实例随打开流程注入 UiSessionController（§5.2）——project/execution 实例随打开流程经 ui 自有会话/命令/草稿/查询端口注入（O-31 裁决）
};

class IWorkbenchShell {
public:
    virtual ~IWorkbenchShell() = default;
    virtual bool initialize(const ShellWiring&) = 0;         // 装配：壳层命令注册、默认布局装载
    virtual IStageNavigationModel& stages() = 0;
    virtual ICommandRegistry& commands() = 0;
    virtual IGlobalShortcutRegistry& shortcuts() = 0;
    virtual IDraftController& drafts() = 0;
    virtual IUiProjectionStore& projections() = 0;
    virtual ITaskPresentationModel& tasks() = 0;
    virtual IDiagnosticPresentationModel& diagnosticsView() = 0;
    virtual IPluginUiRegistrar& pluginRegistrar() = 0;
    virtual UiSessionController& session() = 0;              // §5 状态机
    virtual QWidget* mainWindow() = 0;                       // 供 L5 壳挂接（唯一 Widget 出口）
    virtual ShellTeardownReport shutdown() = 0;              // 有界拆除（§5.6）
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | `initialize` 恰好一次、先于其余一切调用；wiring 各指针非空（eventBus 允许为空＝无事件测试场景，须显式声明） |
| 后置条件 | 壳层命令集（§7.1）与默认快捷键已注册；`mainWindow()` 可显示；`shutdown` 后 mainWindow 不可再用 |
| 错误类型 | 返回 bool/report，不抛；失败细节经 IDiagnosticSink（UI-PLUGIN-ASSEMBLY-FAILED 等） |
| 线程 | initialize/shutdown 可在 L5 装配线程调用（内部完成 UI 线程移交）；其余 UI 线程 |
| 生命周期/所有权 | 壳由 L5 独占持有（unique_ptr）；注入实例所有权仍在 L5/装配层，壳只持共享引用 |
| 副作用 | 布局读写用户级设置；注册 UI-\* 诊断码（经 diagnostics 码表） |
| 示例 | `shell->initialize(wiring); shell->pluginRegistrar().registerPluginUi(desc, module); shell->mainWindow()->show();` |
| 合法 | 装配期注册、查询子模型、显示窗口、shutdown |
| 非法 | 二次 initialize；从非 UI 线程触碰子模型；在 shutdown 后调用除 shutdown 外任何方法 |

### 10.2 IStageNavigationModel（阶段导航投影）

```cpp
enum class StageId { Modeling, Requirements, Kinematics,
                     TrajectoryDynamics, Selection, Optimization, Reporting };
enum class StageViewStatus { Completed, InProgress, Blocked, Unavailable,
                             NotStarted, ViewOnly };

struct StageView { StageId stage; StageViewStatus status;
                   std::optional<UiStateToken> sevenState;      // §6.3 该阶段七态
                   std::vector<TextKey> blockingReasonKeys;     // 门控/缺项（数据源 workflow/evidence）
                   std::optional<TextKey> nextStepKey;          // 下一步建议（workflow 提供）
                   std::uint64_t epoch; };

class IStageNavigationModel {
public:
    virtual ~IStageNavigationModel() = default;
    virtual std::vector<StageView> stageViews() const = 0;      // 七项，固定顺序
    virtual StageId currentStage() const = 0;                   // 会话态
    virtual NavigateResult requestNavigate(StageId) = 0;        // 门控经 workflow（图 §6.4）
    virtual std::unique_ptr<core::IEventSubscription>
        subscribe(IStageViewObserver&) = 0;                     // 投影变更通知（UI 线程分发）
    // ui 侧汇聚（供 workflow 消费，§6.5）：
    virtual StageReadinessSnapshot readinessSnapshot(StageId) const = 0;
};
// NavigateResult = Allowed{stage} | Rejected{reasonKeys, unlockHintKey} | RejectedReadOnly{viewOnlyStage}
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 有项目会话时才有非 NotStarted 投影；`requestNavigate` 需会话非 Opening/Closed |
| 后置条件 | Allowed 时 currentStage 更新＋面板切换事件；Rejected 不改变 currentStage |
| 错误类型 | NavigateResult 显式返回（无异常）；门控数据缺失→Blocked＋"门控数据不可用"（Dev 诊断） |
| 线程 | 全部 UI 线程；readinessSnapshot 供 workflow 在任意线程拉取（值拷贝） |
| 生命周期/所有权 | 模型归壳；订阅句柄 RAII（观察者弱引用，退订幂等） |
| 副作用 | requestNavigate 触发中央区面板装配；不改任何权威对象 |
| 示例 | `auto r = stages().requestNavigate(StageId::Kinematics);` |
| 合法 | 只读浏览 stageViews（含只读项目 ViewOnly）；订阅后长期观察 |
| 非法 | 以 stageViews 推断业务能力并绕过命令系统直改数据；在 workflow 门控之外自行"解锁"阶段（第二状态机，N-11） |

### 10.3 ICommandRegistry（命令注册表）

```cpp
using CommandHandler = std::function<CommandOutcome(std::span<const CommandParameter>)>;
struct CommandOutcome { bool accepted; std::optional<TextKey> messageKey;
                        std::optional<project::CommandResult> revisionResult; };

class ICommandRegistry {
public:
    virtual ~ICommandRegistry() = default;
    // —— 装配期（L5/插件经 IPluginUiRegistrar 间接使用）——
    virtual RegistrationResult registerCommand(const CommandDescriptor&, CommandHandler) = 0;
    virtual RegistrationResult registerCommandWithPredicates(const CommandDescriptor&,
        CommandHandler, VisibilityPredicate, EnablementPredicate) = 0;
    // —— 运行期（UI 线程；命令面板/菜单/快捷键共用）——
    virtual std::vector<CommandView> query(const CommandQuery&) const = 0;   // 只读投影
    virtual CommandAvailability availability(CommandId) const = 0;
        // {visible, enabled, disableReasonKey, readOnlyBlocked}
    virtual CommandOutcome submit(CommandId, std::vector<CommandParameter> = {}) = 0;
    virtual std::vector<CommandView> paletteSnapshot(const std::string& fuzzy,
                                                     std::size_t limit) const = 0;
};
// RegistrationResult = Ok | DuplicateId | InvalidDescriptor | OwnerNotWhitelisted
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | registerCommand：descriptor 合法（id 句法/owner 白名单/谓词非空可省）；submit：命令已注册 |
| 后置条件 | 注册成功后命令对查询/面板/快捷键可见；submit 后命令恰好执行一次（或被前置校验拒绝）；无注册表结构变更（运行期只读） |
| 错误类型 | RegistrationResult 枚举＋UI-CMD-DUPLICATE 诊断；submit 拒绝：UI-CMD-UNKNOWN／UI-CMD-NOT-EXECUTABLE（含只读阻断）；处理器内异常→捕获→UI-CMD-NOT-EXECUTABLE＋Dev 日志（不让异常穿透事件循环） |
| 线程 | 全部 UI 线程（处理器在 UI 线程启动；耗时工作由处理器转交后台/命令服务——处理器本身禁止长计算） |
| 生命周期/所有权 | handler 按值持有（std::function）；注册项在壳生命周期内不注销（静态白名单，§7.2） |
| 副作用 | submit 可产生修订（经①命令端口）、任务提交（经 ITaskScheduler）或会话态变更——取决于命令 |
| 示例（注册） | `commands().registerCommandWithPredicates(desc, handler, vis, en);` |
| 示例（提交） | `auto out = commands().submit("draft.apply"); if (out.revisionResult) …` |
| 合法 | 装配期注册；运行期查询/提交；面板快照搜索 |
| 非法 | 运行期 registerCommand（拒绝）；重复 id（拒绝＋诊断）；处理器内直接修改领域对象/项目文件（只准走命令端口）；以 availability 之外的方式隐藏写入口绕过只读（§7.6） |

### 10.4 IGlobalShortcutRegistry（全局快捷键唯一注册点）

```cpp
class IGlobalShortcutRegistry {
public:
    virtual ~IGlobalShortcutRegistry() = default;
    virtual HotkeyResult registerDefault(CommandId, const QKeySequence&) = 0;  // 装配期
    virtual HotkeyResult rebind(CommandId, std::optional<QKeySequence>) = 0;   // 用户级；nullopt=解绑
    virtual std::vector<HotkeyBinding> bindings() const = 0;                   // 只读（含 Default/User 来源）
    virtual std::optional<CommandId> lookup(const QKeySequence&) const = 0;
    virtual std::unique_ptr<core::IEventSubscription>
        subscribeConflictFeed(diagnostics::IDiagObserver&) = 0;                // 冲突诊断观察
};
// HotkeyResult = Ok | Conflict{existingCommand} | UnknownCommand | NotBindable
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | registerDefault：命令已注册且 bindable；rebind：用户会话（任意 UI 线程时刻） |
| 后置条件 | Ok 后该键唯一映射到该命令；实际 QShortcut 对象仅由本组件创建（§7.3）；rebind 持久化用户级设置 |
| 错误类型 | HotkeyResult；Conflict 附带 UI-HOTKEY-CONFLICT 诊断（冲突键＋已占用命令；比较数值字段＝不适用） |
| 线程 | UI 线程 |
| 生命周期/所有权 | 键表随壳；用户改绑持久化随用户设置 |
| 副作用 | 键按下将触发 `ICommandRegistry::submit(command)`（经命令统一路径，不经旁路） |
| 示例 | `shortcuts().registerDefault("workbench.commandPalette", QKeySequence("Ctrl+Shift+P"));` |
| 合法 | 装配期默认注册；设置界面 rebind/解绑；查询 |
| 非法 | 插件在自身面板目标内创建 WindowShortcut/ApplicationShortcut 作用域快捷键（静态扫描违例，SA-16）；重复键注册（拒绝，不覆盖不静默） |

### 10.5 IDraftController（草稿控制器）

```cpp
enum class SaveTrigger { Autosave, Manual, CloseDialog };
struct SaveOutcome { std::size_t savedCount, failedCount;
                     std::vector<std::string> failedModules;
                     std::optional<project::StoreError> firstError; };
struct DraftingSummary { bool anyDirty;                 // 会话脏 OR 磁盘 present（标题 `*`）
                         std::vector<ModuleDraftView> modules; };

class IDraftController {
public:
    virtual ~IDraftController() = default;
    virtual void attachModule(ModuleDraftHandle, IModuleDraftSource&) = 0;  // 域编辑器接入（阶段 B）
    virtual void detachModule(ModuleDraftHandle) = 0;
    virtual DraftingSummary summary() const = 0;
    virtual SaveOutcome saveAll(SaveTrigger) = 0;            // 单模块＝saveAll 的单元素路径
    virtual RestoreOutcome restoreOnOpen(project::ProjectStore&) = 0;   // §8.3（打开协议第⑤步后）
    virtual void setAutosaveInterval(std::chrono::seconds) = 0;         // 60s 默认；R2 用户可调 60–600
    virtual std::chrono::seconds autosaveInterval() const = 0;
    virtual void onCommandResult(const std::string& moduleId,
                                 const project::CommandResult&) = 0;    // 应用回执（§8.5 冲突处理入口）
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | attachModule：模块 id 未占用且会话可写（只读会话拒绝 attach 写源）；restoreOnOpen：store 处于 Open\* 态 |
| 后置条件 | saveAll 成功项清会话脏标记（磁盘 SaveResult.ok 为准）；失败项保留脏标记＋UI-DRAFT-AUTOSAVE-FAILED；**任何 save 不产生修订** |
| 错误类型 | project::StoreError 透传（context-closed/not-writable/draft-corrupt…）；RestoreOutcome 携带损坏/孤儿明细 |
| 线程 | 公共方法 UI 线程；save 的磁盘段在 ui 后台落盘线程（§8.4），DraftService 线程安全（writer 互斥） |
| 生命周期/所有权 | 模块源归域编辑器（弱引用＋显式 detach）；磁盘草稿归 project |
| 副作用 | DraftService::save（origin=autosave/manual）；关闭对话框数据装配读取 summary |
| 示例（保存） | `auto out = drafts().saveAll(SaveTrigger::Manual);` |
| 示例（恢复） | `auto ro = drafts().restoreOnOpen(store); if (ro.corruptRecovered) 恢复横幅…` |
| 合法 | 定时/手动保存；打开恢复；冲突回执处理 |
| 非法 | 在 DraftController 内组装领域命令载荷（归域编辑器）；删除/改写磁盘草稿文件；应用草稿（只协调，应用走命令系统 §7.7）；只读会话调用 saveAll（拒绝） |

> **UI-T12 实现冻结登记（v1.4，2026-09-21）**：本节签名随本任务实现冻结（O-31 首消费冻结机制，§16.7 v1.4）。与原文的形态差与处置：①restoreOnOpen(project) 的对端实参按 O-31 改为**会话绑定注入**（bindSession(DraftSessionBinding)——C-5 读/写半区端口随打开结果绑定，restoreOnOpen() 无参调用）；②SaveOutcome.firstError 的 project::StoreError 类型以值投影承载（SaveModuleError{errorToken,detail}——对端稳定 token 直用不镜像，NFR-MNT-03）；③ModuleDraftHandle＝模块 token 别名（注册表键——与 DraftDocumentProjection.moduleId 同词表）；④DraftingSummary.anyDirty 即 §8.2 合并规则（会话脏 OR 磁盘 present）的控制器唯一实现点，另设 discardDraft（§8.3-4 横幅[放弃]执行面——写操作，只读拒绝）；⑤新增 tickAutosave（§8.4 定时器心跳的控制器半区——L5 定时器按周期调用）与 notifySessionDirty（§8.2 会话编辑态通知入口）与 staleConflictData/reEditOnCurrentRevision（§8.5 冲突呈现与决议执行面）。原文语义零变化：attach/detach/summary/saveAll/restoreOnOpen/setAutosaveInterval/autosaveInterval/onCommandResult 的前置/后置/错误/线程/所有权各行逐条兑现；域源以桩模块验证控制器协议（§8.3-5 阶段 A 原文），域编辑器接入归阶段 B。

### 10.6 IUiProjectionStore（只读投影存储）

```cpp
struct UiContextSnapshot;                     // §7.5（值类型，深拷贝）
class IUiProjectionObserver { public: virtual ~IUiProjectionObserver() = default;
    virtual void onProjectionChanged(ProjectionKind, std::uint64_t epoch) = 0; };

class IUiProjectionStore {
public:
    virtual ~IUiProjectionStore() = default;
    virtual UiContextSnapshot snapshot() const = 0;                  // 单次一致快照（值拷贝）
    virtual std::uint64_t epoch() const = 0;                         // 会话纪元（§6.2）
    virtual void rebindSession(std::shared_ptr<project::ProjectStore>,
                               std::shared_ptr<execution::ITaskScheduler>,
                               std::shared_ptr<execution::ITaskController>) = 0;
        // 打开成功/切换绑定/关闭时由 UiSessionController 调用；epoch++；
        // 旧 store 引用交由会话后台持有点（Draining 保活，§5.1）
    virtual void handleEvent(const core::DomainEvent&) = 0;          // UiThreadEventRelay 投递入口
    virtual std::unique_ptr<core::IEventSubscription>
        subscribe(ProjectionKind, IUiProjectionObserver&) = 0;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | handleEvent：已 initialize（Relay 已接总线）；rebindSession：新 store 非 Draining 拒写态（或空指针＝关闭） |
| 后置条件 | rebind 后 snapshot 反映新会话（epoch 递增）；旧 epoch 快照不再分发；迟到事件按 §6.2 三分支路由 |
| 错误类型 | 查询失败→快照内对应字段为"不可用"＋诊断（不抛出、不崩溃）；端口 StoreError 捕获降级 |
| 线程 | snapshot/epoch 只读（任意线程，值拷贝）；handleEvent/subscribe/rebind UI 线程 |
| 生命周期/所有权 | 存储归壳；store/scheduler 引用共享持有（rebind 语义管理） |
| 副作用 | 端口重查（pull-on-event §6.1）；不回写任何权威对象（投影只读红线） |
| 示例（迟到事件处理） | `projections().handleEvent(ev);  // 内部：epoch/项目归属过滤→重查或路由后台清单` |
| 合法 | 五区/谓词/命令 availability 消费 snapshot；订阅刷新 |
| 非法 | 经由快照反查并修改权威对象；在快照里保存跨纪元累积的业务状态；UI 侧由快照数据"计算"当前性/工程判定（§6.3 红线） |

### 10.7 ITaskPresentationModel（任务呈现模型）

```cpp
struct TaskRow { execution::TaskSnapshot snap;       // 五元组/状态/能力/归档相位（execution 投影）
                 TextKey stateLabelKey;              // 九态短标签（§6.3）
                 std::optional<execution::ProgressReport> progress; };

class ITaskPresentationModel {
public:
    virtual ~ITaskPresentationModel() = default;
    virtual std::vector<TaskRow> activeTasks() const = 0;      // 当前会话项目（非终态＋近终态窗口）
    virtual std::vector<TaskRow> backgroundTasks() const = 0;  // 后台排空项目（只读，§5.7）
    virtual std::optional<TaskRow> task(core::RunId) const = 0;
    virtual execution::CancelAck requestCancel(execution::TaskId) = 0;        // 转发 ITaskController
    virtual execution::StatusAck requestPause(execution::TaskId) = 0;
    virtual execution::StatusAck requestResume(execution::TaskId) = 0;
    virtual execution::StatusAck requestForceTerminate(execution::TaskId) = 0; // 高级操作＋确认
    virtual void startProgressPolling(std::chrono::milliseconds) = 0;         // 默认 200ms
    virtual void stopProgressPolling() = 0;
    virtual std::unique_ptr<core::IEventSubscription> subscribe(ITaskViewObserver&) = 0;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 控制类调用：任务属于当前会话项目（后台清单任务→拒绝，DisableReason）；scheduler 已注入 |
| 后置条件 | 控制请求按到达序送达（execution 同 TaskId 串行保证）；轮询停止后无残余定时器事件 |
| 错误类型 | execution Ack 结构（accepted=false＋feedback 诊断）；ContextClosed→提示"执行服务已关闭" |
| 线程 | 全部 UI 线程（内部转发到 execution 任意线程入口——execution 接口线程安全） |
| 生命周期/所有权 | 快照值拷贝；近终态任务保留窗口（默认 30 min，对齐 execution D-07 会话内存态） |
| 副作用 | 控制请求改变任务状态（经 execution）；**无 UI 线程阻塞等待**（全部即发即忘＋事件/轮询回看） |
| 示例 | `tasks().requestCancel(row.snap.taskId);`／`for (auto& r : tasks().backgroundTasks()) 显示归档徽标;` |
| 合法 | 当前项目任务控制；后台任务只读查看；轮询启停 |
| 非法 | 阻塞等待任务完成（UI 线程）；把 Canceled/Failed/Interrupted 呈现为通过；后台任务控制；在 ui 侧重算九态/归档相位（只投影 execution 数据） |

### 10.8 IDiagnosticPresentationModel（诊断呈现模型）

```cpp
class IDiagnosticPresentationModel {
public:
    virtual ~IDiagnosticPresentationModel() = default;
    virtual std::vector<diagnostics::DiagProjectionItem>
        query(const diagnostics::DiagQuery&) const = 0;          // 过滤（任务/修订/类别/严重/对象）
    virtual std::vector<diagnostics::DiagProjectionItem>
        expandChain(std::uint64_t entryId) const = 0;            // causedBy 根因链上溯
    virtual std::vector<diagnostics::FindingRecord>
        pendingConfirmations() const = 0;                        // 经 project 间接读（§9.2）
    virtual std::unique_ptr<core::IEventSubscription>
        subscribe(diagnostics::IDiagObserver&) = 0;              // 目录变更→Marshal→UI 线程分发
    virtual std::string redactedPath(std::string_view raw) const = 0;  // 展示路径（IRedactionService 转发）
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | initialize 时 diagSink 已注入 |
| 后置条件 | 查询返回值拷贝（含去重计数/聚合导航字段）；订阅回调全部 UI 线程 |
| 错误类型 | sink 快照失败→空集＋Dev 日志；无异常穿透 |
| 线程 | 公共方法 UI 线程；底层 sink 订阅回调在目录通知线程→组件内 Marshal（diagnostics §9.8） |
| 生命周期/所有权 | 无自有数据（目录归 diagnostics）；订阅 RAII |
| 副作用 | 无（只读）；redactedPath 仅转换显示文本 |
| 示例 | `auto items = diagnosticsView().query(DiagQuery{.minSeverity=Warning});` |
| 合法 | 诊断表/日志面板/恢复横幅/确认对话数据装配 |
| 非法 | 反写/删除目录条目（无接口）；以投影文本反查原文；在 ui 修改诊断事实/严重级别/阈值；直接调 IConfirmableFindingService 提交确认（确认只经 §9.2 Bridge） |

### 10.9 IPluginUiRegistrar（插件界面注册端口）

```cpp
struct PanelRegistration { StageId stage; TextKey titleKey;
                           PanelFactory factory;                // 创建 QWidget 族面板（ui 线程）
                           bool advanced = false; };            // UX-04 高级面板标记
struct PluginCapabilities {                       // 能力声明（描述性，非判定性）
    bool providesStagePanel = false;
    bool providesReadonlyProjection = false;      // 参与StageStatusModel汇聚
    bool registersCommands = false; };

struct PluginUiDescriptor { std::string pluginId;          // 白名单 token
    TextKey titleKey; std::vector<StageId> stages;
    PluginCapabilities capabilities;
    std::vector<ui::CommandDescriptor> commands;          // 随装配注册（§7）
    std::vector<PanelRegistration> panels; };

class IPluginUiRegistrar {
public:
    virtual ~IPluginUiRegistrar() = default;
    virtual RegistrationOutcome registerPluginUi(const PluginUiDescriptor&,
                                                 IPluginUiModule&) = 0;   // 装配期一次
    virtual std::vector<PluginAssemblyReport> assemblyReports() const = 0; // 关于对话框数据（UX-14）
    virtual std::vector<std::string> whitelist() const = 0;               // 静态白名单
};
// RegistrationOutcome = Ok | DuplicatePlugin | NotWhitelisted | InvalidDescriptor
// PluginAssemblyReport = {pluginId, ok, panelsLoaded, commandsRegistered, failureDiagnostics}
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 装配期（initialize 后、mainWindow 显示前）；pluginId 在白名单；IPluginUiModule 存活至壳拆除 |
| 后置条件 | Ok：面板工厂入列（按 stage 装配位）、命令入注册表（冲突规则 §7.2）、能力入关于页数据源 |
| 错误类型 | RegistrationOutcome＋UI-PLUGIN-ASSEMBLY-FAILED（Error，用户可见——降级占位） |
| 线程 | 装配线程调用（内部 UI 线程移交面板工厂登记）；面板工厂只在 UI 线程调用 |
| 生命周期/所有权 | 插件模块归 L5/插件自身（ registrar 持弱引用＋id 索引）；无注销（静态白名单，SA-01） |
| 副作用 | 命令/面板/快捷键默认表注册；AssemblyReport 累积 |
| 示例 | `registrar.registerPluginUi(desc, kinematicsModule);` |
| 合法 | 白名单内一次性注册；查询报告/白名单 |
| 非法 | 运行期注册/注销（动态加载——SA-01/NFR-SEC-04 禁止）；白名单外注册；插件间经注册表互取面板指针（ARC-02） |

**跨单元调用示例索引**：注册命令→§10.3 示例；注册快捷键→§10.4 示例；查询可执行条件→`ICommandRegistry::availability`（§10.3）；提交命令→§7.7 时序；显示确认→§9.2 时序；更新任务投影→§10.7 订阅＋轮询；切换项目→§5.4 S2；保存/恢复草稿→§10.5 示例；注册阶段面板→§10.9 示例；关闭请求→§5.4 S1；处理迟到事件→§10.6 示例。

---

## 11. 插件装配与跨单元协作

### 11.1 静态白名单装配（SA-01）

- 白名单为**编译期/装配期常量**（`PluginUiRegistrar::whitelist()`），由 L5 应用壳固定：`modeling, requirements, kinematics, trajectory, dynamics, selection, optimization, workflow`（阶段 A 均未产出，白名单先行登记占位 token）。
- 装配顺序＝白名单顺序；每插件恰好一次 `registerPluginUi`；重复/白名单外注册拒绝（§10.9）。
- **插件计算库与界面目标分离**（ARCH §3.3）：`ird_<unit>`（零 Qt）与 `ird_<unit>_plugin`（Widgets）为不同构建目标；IPluginUiRegistrar 只面向插件界面目标；ui 与计算库无编译期关系。
- **注册时机**：L5 装配序列＝① shell.initialize（壳层命令/默认快捷键）→②按白名单逐插件 registerPluginUi（面板/命令/能力声明）→③ 首次 mainWindow 显示。运行期不再有装配动作。
- **命令注册/面板注册/投影注册**：命令→CommandRegistry（§7.2 冲突规则）；阶段面板→CentralAreaHost 按 StageId 挂位；只读业务投影→StageStatusModel 汇聚（§6.5，经 IPluginUiModule 的只读投影接口）。
- **禁止运行时动态加载/卸载**（NFR-SEC-04）：无 QPluginLoader 路径、无注册表注销接口；部署目录 plugins\ 仅承载框架自身组件（ARCH §5.4）。
- **业务插件彼此不直接依赖**（R-1）：插件界面之间不互链、不互读控件；跨插件协作只经端口（ARCH §7.2）。
- **ui 只经公共端口消费业务投影**：IPluginUiModule 暴露的只读投影接口＋命令处理句柄；ui 不 include 业务单元私有头。

### 11.2 IPluginUiModule（ui 定义的插件界面模块接口）

```cpp
class IPluginUiModule {                    // 各插件界面目标实现
public:
    virtual ~IPluginUiModule() = default;
    virtual void onShellReady(IWorkbenchShell&) = 0;      // 注册回调后初始化（订阅/面板内容）
    virtual std::vector<DomainReadinessItem> readonlyProjections() const = 0; // §6.5 汇聚源
    virtual std::optional<project::CommandEnvelope>
        buildDraftCommand(const std::string& moduleId) = 0; // §8.5 应用时命令组装（域侧）
    // 面板工厂经 PluginUiDescriptor::panels 提供；快捷键一律经 GlobalShortcutRegistry
};
```

### 11.3 插件缺失与装配失败的降级

- 白名单插件未注册（缺位）：对应阶段中央区显示占位面板（"该模块在当前构建中不可用"）＋阶段导航该阶段 `unavailable`＋Dev 诊断；**不虚构业务能力、不以占位控件推断能力存在**。
- 装配失败（面板工厂抛出/命令冲突等）：登记 AssemblyReport.failureDiagnostics＋`UI-PLUGIN-ASSEMBLY-FAILED`；失败插件的面板降级为错误占位；其余插件照常装配（失败隔离，不中止启动）。
- 命令注册失败（重复 id）：该命令不进注册表（其余照常）；AssemblyReport 记录。

### 11.4 关于页面与插件清单（UX-14）

> **UI-T10 落地登记（v1.2，2026-09-21）**：本节交付面已实现——契约头 `AboutDialog.hpp`（§3.3 增行）：`pluginUiWhitelist()`（§11.1 八 token 编译期词表的运行时只读载体）、`PluginAssemblyReport`（§10.9 冻结形状承载——注册端口 `IPluginUiRegistrar.hpp` 随装配任务落位时该形状**迁移**至其自有头，NFR-MNT-03 单点）、`AboutVersionBaseline`（NFR-DEP-05 呈现值投影，available=false＝「未装载」占位）、`aboutPluginRows`/`aboutVersionRows`（清单/版本行装配唯一权威——模型层与 GUI 同源互证，UI-PLG-2 数据源断言锚）、`createAboutDialog`（对话框工厂——QDialog::open 窗口模态非阻塞打开，不用 exec()：§7.7 提交时序不重入）；清单装配规则＝白名单全集行（行序＝§11.1 词表序）＋报告对位充实（ok/面板数/命令数/失败码）＋白名单外报告条目交集丢弃＋重复报告取首条确定性防御；插件版本列阶段 A 恒「不适用」占位（§10.9 报告形状无版本字段——不虚构，版本来源随装配任务增量登记）。数据注入＝`IUiAboutDataSource` 端口（UiPorts.hpp v1.2）＋`ShellWiring.aboutSource`（§10.1 v1.2 增量——O-31 处置：版本数据 L5 注入、插件清单静态白名单，交付物零对端类型）。帮助入口＝`userManualPath()`/`openUserManual()`（落位口径 `<appDir>/../share/ird/user-manual/index.html`——share\ 帮助文件；文件缺失时不启动打开动作，状态栏用户可见反馈＋Dev 日志排障明文——§3.5 码表无此事件，不臆造稳定码）。壳集成＝帮助菜单两实条目（help.contents/help.about，UI-T03 的禁用占位升级）＋壳自持处理器。

关于对话框数据＝静态白名单 ∩ AssemblyReport（逐插件：标题/版本/面板数/命令数/装配状态）＋产品版本与组件版本（与冻结基线 NFR-DEP-05 一致——版本数据由 L5 提供，ui 呈现）。帮助入口链接用户手册（share\ 帮助文件）。

### 11.5 与 L5 应用壳／workflow 的分工（PM-17/PM-03）

| 事项 | ui | L5 壳 / workflow |
| --- | --- | --- |
| 进程启动、静态装配、闪屏 | 提供壳窗口与装配入口 | 启动流程编排（PM-17 主责 WP-24） |
| 新建/打开/另存/导入导出向导 | 提供对话框基件与命令项 | 向导编排（PM-01~07，WP-22） |
| 关闭/切换统一确认 | 对话框＋SessionController＋关闭原语 | 触发时机编排（PM-03，WP-22-T06） |
| 七阶段门控 | StageStatusModel 汇聚＋导航呈现 | 门控规则与下一步建议（WP-22-T03） |
| 命令面板入口整合 | 注册表/面板/快捷键设施 | 业务命令注册与全局入口（WP-22-T11/T12） |
| 强制放弃兜底 | T_force 计时与确认对话 | abandonAll 调用（L5 关闭控制器） |

---

## 12. 验证方案及 Windows GUI 测试流程

### 12.1 测试目标与分层

| 目标 | 内容 | ctest 标签 |
| --- | --- | --- |
| `sdurws_ird_ui_test` | 无界面模型测试：命令注册表/快捷键表/七态映射/epoch 过滤/DraftController 协议（QCoreApplication 级——AGENTS 模型测试豁免，无需 GUI 平台插件） | `ird` |
| `sdurws_ird_ui_contract_test` | 跨单元契约：对接 project/execution/diagnostics 公共头与桩实现（testkit FaultInterceptor 伪造 ITaskScheduler/ICommandInteraction） | `ird` |
| `sdurws_ird_ui_gui_test` | Widgets/GUI 契约测试（五区创建/布局恢复/对话框/面板导航） | `ird_gui`（**串行**） |

`sdurws_ird_testkit_qt` 启用登记（testkit.md §3.5/§10.1）：本单元为首个消费者（WP-10-T11 需 Qt Test 辅助：事件循环驱动、控件探针），理由已在 UI-T13 任务卡登记；该目标禁止进入生产计算库依赖（testkit 红线）。

### 12.2 Windows GUI 测试执行流程（承接 `C:\Users\zgl18\.codex\AGENTS.md`；testkit.md §6.7 同口径）

1. 在 **Visual Studio x64 Developer Environment**（或等价 vcvars64 初始化的 shell）中运行；
2. 设置 `$env:QT_QPA_PLATFORM='windows'`；
3. **每次只启动一个**GUI 测试可执行文件，且使用**绝对路径**调用（例 `D:\build\...\sdurws_ird_ui_gui_test.exe`）；ctest 侧以 `LABELS ird_gui` 串行调度；
4. **不使用** `QT_QPA_PLATFORM='offscreen'`；
5. **不把 Widget 测试与 Meta/模型测试可执行文件合并到同一命令**（分开运行）；
6. 若 Qt 报平台插件初始化失败：**先停止受影响进程**→检查继承的 `QT_*`/`QML_*` 环境变量（清除冲突项）→按上述设置**重启**；
7. 仅使用 QCoreApplication 的模型测试不需要 GUI 平台插件；
8. **本节为流程设计，本版不启动任何 GUI 程序**——以下用例全部为"已设计、未执行"，不得记载为通过。

通用判据：所有用例断言经 testkit `IRD_EXPECT_*` 宏输出 TestRecord（ird-test-report.json）；fault 注入用 FaultInterceptor/FaultPlan；不使用固定 sleep 判据（事件等待用 EventWatch/信号量）。

### 12.3 测试用例清单

编号规则：`UI-<域>-<序号>`；每例给出需求/AT 依据、前置、操作、预期、观测点。**状态＝Designed（未执行）**。

| 编号 | 场景 | 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- |
| UI-WB-1 | 五区布局创建与恢复 | UX-09 | GUI 环境；干净用户设置 | 启动→拖动停靠/隐藏两区→重启 | 五区齐备；重启后位形还原；布局文件未写入任何项目目录 | DockWidget 可见性矩阵；用户设置文件；.rwdesign 目录 mtime |
| UI-WB-2 | 恢复默认布局 | UX-09 | UI-WB-1 后乱序 | 执行 `view.resetLayout` | 出厂位形恢复；会话浮窗清除 | 五区几何断言 |
| UI-WB-3 | 布局记忆损坏回退 | UX-09/§4.5 | 用户设置注入损坏段 | 启动 | 默认布局＋UI-LAYOUT-RESTORE-FAILED（Dev）；启动不阻塞 | 启动时长；诊断目录 |
| UI-SES-1 | 无项目首页与入口禁用 | PM-10 | 无项目 | 启动 | 三入口可见；项目作用域命令 availability.enabled=false；命令面板仍可搜索打开/新建 | CommandRegistry 查询；面板列表 |
| UI-SES-2 | 只读打开（锁竞争） | PM-07/AT-20 前置 | 两进程；A 持写锁 | 第二实例打开同项目 | 立即只读；横幅含 PID；写命令禁用；无阻塞等待 | writable=false；`PRJ-LOCK-HELD` 呈现；按钮态 |
| UI-SES-3 | 介质只读/权限不足显示差异 | PM-07 | 只读介质目录/ACL 拒绝 | 打开 | 横幅文案与 actionKind 区分（retry-readonly/inspect-resource） | 诊断 category/文案键 |
| UI-SES-4 | 项目切换与迟到事件过滤 | TASK-03/PM-13/AT-10 | 项目 A 运行中任务；项目 B 就绪 | 切换 A→B；注入 A 的迟到 TaskStatusChanged/ResultArchived | B 投影不变；A 任务进后台只读清单；A 归档照常写 A 的 results/；B 的 results/ 无新写入 | epoch 值；后台清单内容；两项目 results/ 目录 diff |
| UI-SES-5 | UI 关闭但归档未完成 | PM-03/§5.6 | A 有 Archiving 任务 | 关闭→选"等待" | Draining 显示归档进度；subscribeClose 回调后 Closed；等待有进度反馈 | store.closed() 时序；对话框状态流转 |
| UI-SES-6 | 关闭无永久等待 | §5.6/INV-SES-4 | 模拟归档挂起（FaultPlan） | 关闭→等待超过 T_force | 出现"强制结束并关闭"确认；执行后退出；检查点保留声明 | 计时器触发；诊断记录 |
| UI-SES-7 | 已释放上下文的迟到写请求 | §5.3/SA-17 | store 已 Closed | 触发写命令（伪造迟到提交） | UI-SESSION-CONTEXT-INVALID 对话框；不重试 | 诊断条目；无二次提交 |
| UI-DRF-1 | 未保存草稿关闭三选 | PM-03/PM-04 | 可写项目＋脏模块 | 关闭→分别验证保存/放弃/取消 | 保存→saveAll(Manual) 落盘；放弃→磁盘草稿保留；取消→回到原状态 | DraftProjection；脏标记；状态机未离开 OpenWritable |
| UI-DRF-2 | 定时草稿保存 | PM-04 | 可写项目；间隔设为最小 | 编辑后等待定时触发 | origin=autosave 落盘；UI 线程无磁盘 IO（落盘线程执行）；无修订产生 | DraftService 调用记录；修订数不变 |
| UI-DRF-3 | 草稿损坏恢复 | PM-08/AT-21 | 磁盘草稿损坏＋.bak 完好 | 打开→恢复流程 | .bak 内容恢复；原损坏文件保留；恢复横幅提示 | tryLoad 诊断；横幅数据 |
| UI-DRF-4 | StaleRevisionRejected | PM-04(RV-10)/AT-29 | 草稿 base≠tip（外部推进修订） | draft.apply | 冲突对话框（当前 tip/差异对象）；草稿保留（apply-retained）；可继续编辑；无修订 | CommandResult.status=Rejected(stale-revision)；DraftProjection |
| UI-CMD-1 | 命令重复注册拒绝 | SA-16/ARCH §11.2-1 | 装配期 | 两插件注册同 id | 第二次 RegistrationResult=DuplicateId＋UI-CMD-DUPLICATE；注册表仅一条 | AssemblyReport；诊断目录 |
| UI-CMD-2 | 未知命令提交拒绝 | §7.2 | 运行期 | submit("no.such.cmd") | 拒绝＋UI-CMD-UNKNOWN；无派发 | CommandOutcome；诊断 |
| UI-CMD-3 | 只读模式命令拒绝 | PM-07 | 只读会话 | 编程提交 `draft.apply` | UI-CMD-NOT-EXECUTABLE＋拒绝；三处入口一致禁用 | availability；诊断 |
| UI-CMD-4 | 未确认命令不得提交 | SA-15/MDL-06④/AT-01 | 命令带 ConfirmableFinding；对话选拒绝 | draft.apply | Rejected(confirmations-rejected)；无修订 | CommandResult；finding.state=Invalidated(user-rejected) |
| UI-CMD-5 | 确认凭据与留痕 | SA-15/AT-01 | 同上但确认 | 确认→完成 | Committed；CommandRecord.confirmations[] 含 credential＋四元组；principal＝当前用户 | 命令摘要读回（project 查询） |
| UI-CMD-6 | 输入变化后确认失效 | §9.2/P-DIAG-6 | 对话打开期推进 baseRevision（Fault 注入） | 确认旧对话框 | 编译前复核不符→按未确认处置；提示重新确认 | Rejected(confirmations-unresolved)＋对话重开 |
| UI-CMD-7 | 关闭窗口时未完成确认 | diagnostics §5.6 | 确认对话挂起 | 直接关闭主窗口 | isAlive=false→Aborted(interaction-lost)；finding Invalidated(interaction-lost)；无用户诊断；无修订 | 命令结果；finding 状态；诊断目录 |
| UI-HKY-1 | 快捷键冲突拒绝 | UX-13(M-13)/SA-16/ARCH §11.2-1 | 已注册 Ctrl+S | 再注册 Ctrl+S 到另一命令 | HotkeyResult=Conflict＋UI-HOTKEY-CONFLICT（含冲突键与占用命令）；不覆盖 | 注册表内容；诊断 comparison/context |
| UI-HKY-2 | 用户改绑与解绑 | UX-13/PM-14 | 运行期 | rebind→冲突键→解绑 | 冲突拒绝；解绑后命令仍可面板搜索到 | bindings()；面板快照 |
| UI-HKY-3 | 插件私占全局快捷键静态拒绝 | SA-16/NFR-MNT-07 | 构建扫描 | 静态扫描插件界面目标 QShortcut 作用域 | WindowShortcut/ApplicationShortcut 创建命中即违例 | 扫描报告（CI 门禁） |
| UI-STG-1 | 七阶段状态投影 | UX-12/UX-01 | 桩 workflow 门控输出 | 遍历注入各门控态 | stageViews 与注入一致；blocked 附原因与缺项；只读项目 ViewOnly | StageView 快照 |
| UI-STG-2 | 阶段切换时序 | UX-12 | 桩门控允许/拒绝 | requestNavigate | 允许→面板切换＋事件；拒绝→就地提示不改 currentStage | 观察者事件序 |
| UI-STG-3 | 七态×九态映射 | UX-10/AT 系（UX 家族） | 桩投影数据集（含 NotEvaluable） | 求值七态 | 映射表逐行断言；NotEvaluable→results-stale＋"无法判定"原因（P-UI-2 建议口径） | StatusWordProjection 输出 |
| UI-TSK-1 | 任务取消/暂停/继续/强杀 | TASK-01/OPT-06/AT-34 | 桩 scheduler＋长任务 | 依次请求 | Ack 结构透传；状态事件驱动 UI 更新；强杀带确认；正常取消无错误诊断 | TaskRow 序列；诊断目录无取消错误 |
| UI-TSK-2 | worker 崩溃呈现 | NFR-REL-02/AT-11 | 注入 Failed(EX-WORKER-CRASHED) | 事件到达 | failed 态＋诊断＋重跑入口；界面不退出 | 进程存活；诊断条目 |
| UI-TSK-3 | 结果归档中/归档失败 | §9.4/CON-04 | 注入 archivePhase 序列/ArchiveFailed | 观察 | Completed 徽标附归档子标；ArchiveFailed→警告＋诊断；不显示为通过 | TaskRow.archivePhase 断言 |
| UI-TSK-4 | 当前性过期但历史可查看 | CON-02/AT-05 | 桩 Currentness=Superseded＋原因 | 查看结果面板 | results-stale＋原因清单；历史面板可打开＋过期横幅＋重算入口；无"正式通过"字样 | 面板控件状态；文案断言 |
| UI-TSK-5 | 进度乱序/重复 | §9.4 | 注入回退/重复 ProgressReport | 轮询消费 | 回退丢弃（Dev 日志）；重复幂等；界面单调 | 轮询器日志；显示值序列 |
| UI-PLG-1 | 插件装配失败降级 | SA-01/§11.3 | 白名单插件工厂抛出 | 装配 | 错误占位＋UI-PLUGIN-ASSEMBLY-FAILED；其余插件正常 | AssemblyReport；占位面板存在 |
| UI-PLG-2 | 关于页插件清单 | UX-14 | 白名单＋报告 | 打开关于 | 清单与白名单∩报告一致；版本与注入基线一致 | 对话框数据源断言 |
| UI-HELP-1 | 帮助入口与关于对话框命令路径（UX-14——UI-T10，v1.2 增行） | 注入基线＋装配报告替身（IUiAboutDataSource） | 工作台壳已 initialize | 菜单"关于"触发 help.about；再触发"帮助手册"（help.contents） | 关于框经统一提交路径打开（QDialog::open 窗口模态非阻塞）；版本区行集与 aboutVersionRows 输出逐字同源、值＝注入基线原样（产品行在首、组件行序＝注入序、未登记项「不适用」）；插件清单八行＝白名单全集（行序＝§11.1 词表序），命中报告行充实（状态/面板数/命令数）、未命中行「未装配」占位、白名单外报告条目不进清单；无数据源（aboutSource 空）时版本区「未装载」单行＋清单全占位（零虚构）；手册文件缺失时不启动打开动作、状态栏用户可见反馈＋Dev 排障明文（诊断目录零产出） | 对话框 objectName=ird_about_dialog；版本行 objectName=ird_about_version_<key>；清单表 objectName=ird_about_plugins；模型层 AboutDialogModelTest 九用例同源互证 |
| UI-DIA-1 | 诊断脱敏显示 | NFR-SEC-07/UX-03 | pathPolicy=RootOnly；注入含路径诊断 | 打开诊断表/日志页 | 显示路径已脱敏（转发 IRedactionService）；三要素数值＋单位同显；"不适用"占位 | 呈现文本断言 |
| UI-PERF-1 | UI 线程不执行长时工作 | NFR-PERF-01/ARCH §4.2 | 桩慢端口（>1 s 查询） | 触发投影刷新 | UI 线程无 >200 ms 阻塞（事件循环心跳探针）；长工作转后台 | 心跳时间戳序列 |
| UI-LCY-1 | UI 关闭后迟到写请求被拒 | SA-17/§5.7 | Closed 后注入命令提交 | submit | 拒绝＋UI-SESSION-CONTEXT-INVALID；无崩溃 | CommandOutcome；进程存活 |
| UI-V3D-1 | 中央区三维视图占位与契约登记 | UX-11/KIN-06/AT-04（UI-T05 阶段 A——v0.7 增行） | 项目打开态 | presentProjectContext 切中央区 | 占位面板呈现"本阶段将在后续版本提供"＋UX-11 九项交互清单只读登记（行序/文案/view3dId 与 View3DContract.hpp 契约同源一致）；零可交互业务控件（不虚构业务能力）；KIN-06 语义钉住值（仅会话姿态/零模型写/零修订/零失效）；上下文往返面板稳定存活 | objectName 定位；清单行属性逐项对照；控件类型扫描；View3DSessionPoseContract 四位断言（模型层 View3DContractTest 同源互证） |
| UI-PALETTE-1 | 默认键触发命令面板与 Esc 关闭 | UX-13/§7.3/§7.4（UI-T06——v0.8 增行） | 工作台壳已 initialize | 主窗口注入 Ctrl+Shift+P；再注入 Esc | 面板呈现（workbench.commandPalette 经 registry.submit 打开）；Esc 后收起；QShortcut 对象仅存在于 GlobalShortcutRegistry（模型层 BuildRedLineTest 唯一创建点扫描互证） | ird_command_palette objectName 定位；isVisible 序列 |
| UI-PALETTE-2 | 面板模糊过滤＋键盘导航＋Enter 执行 | UX-13/§7.4（UI-T06——v0.8 增行） | 同上 | 打开面板→过滤"恢复"→Enter；对照 Esc | 过滤后 view.resetLayout 置顶；Enter 经 registry.submit 派发（壳自持语义执行后面板收起）；未绑定命令（scheme.switch 无默认键）在快照中可达；无项目态写命令行"禁用＋说明"呈现 | 列表行文本；regionVisible(Left) 复位观测；panel isVisible 序列 |
| UI-PALETTE-3 | 用户改绑跨会话持久化 | PM-14/§7.3（UI-T06——v0.8 增行） | 用户级设置注入 "workbench.commandPalette→Ctrl+J" 改绑 | 会话 A 验证改绑键生效/被覆盖默认键失效→关停；会话 B 复验 | User 绑定覆盖 Default（一键一命令双向唯一）；关停兜底落盘后跨会话仍生效（持久化闭环：模型层 ShortcutRegistryModel 回调半区＋GUI 载荷读写半区） | Ctrl+J/Ctrl+Shift+P 触发序列；设置文件 shortcuts 组 |
| UI-POL-1 | 右栏策略摘要只读卡（UX-08/POL-ID-3——UI-T07 阶段 A，v0.9 增行） | 注入已装载投影 | initialize 装配 | 检查右栏卡分组行集 | 两分组异名在位（"工程策略（计算权威）"／"会话显示设置（不属策略字段）"）；行文本与 policySummaryRows 模型输出逐行同源（碰撞启用含域 token／安全间距·行程上限经单位换算投影／判定阈值含「显式不适用」／必检·过滤计数）；卡面零可编辑策略控件（摘要只读）；未装载态呈"未装载"占位不虚构数值；修改策略入口点击＝延期提示（表单归阶段 B）；投影变更→presentProjectContext→行更新（端口重拉） | 卡 objectName=ird_policy_summary_card；行 objectName=ird_policy_row_<key>；分组标题 objectName；模型层 PolicySummaryCardTest 十用例同源互证 |
| UI-FRM-1 | 参数表与表单公共件（UX-04/05/07/KIN-12——UI-T08 阶段 B，v1.0 增行） | 模型注入基线＋替身编辑出口 | createParamTablePanel 装配（gui）；ParamEditModel 直构（模型） | 值列就地编辑/单位切换/筛选/剪贴板粘贴/应用/取消 | 数值＋单位同显（值｜单位分列与 displayValueText 合显同源；长度 mm→cm 切换显示随动、SI 真值不变、零脏零移交——KIN-12）；非法输入状态列就地显示原因（含原文＋"保留原值"）且值列回最后一次有效值；应用→内嵌非模态确认区（比较型明细，零 QDialog——静态扫描＋控件扫描双证）→确认→ParamEditSet 移交编辑出口（SI 真值；反馈明示"断言与放行归 project 命令边界"）；有错应用被阻断并定位首错行；批量粘贴逐行独立判定＋影响明细（接受/拒绝计数＋逐行原因＋影响条目）；取消恢复回基线（单位制式与筛选不回滚）；未接出口应用禁用（kNoOutletTooltip）；开发诊断开关只回调不进编辑集（UX-07 显示操作不触发确认）；高级面板字段集三键冻结（solver-max-iterations/sampling-points/random-seed——UX-04 承载，基线未设不虚构数值） | 根 objectName=ird_param_table_panel；表/筛选/单位切换/确认区/按钮 objectName 锚族；模型层 FormEditModelTest 十六用例＋GUI 层 ParamTablePanelGuiTest 十用例同源互证 |

**登记执行限制（UI-EXEC-1）**：队列位置与资源占用无对外接口（execution §A3）——测试只验证"排队中（等待资源）"文案与诊断呈现，不验证数值（不虚构数据源）。

### 12.4 通过标准与留痕

- 每例绑定 TestRecord（passed/failed/skipped/notRun/envUnavailable/datasetInvalid）；GUI 用例环境不满足记 `envUnavailable`（不得绿灯，testkit §7.5）。
- 本文档自审（§16.6）**不等同于**测试通过或正式验收；全部用例执行状态以 ctest/ird-test-report.json 留痕为准。

---

## 13. 阶段 A 实现任务拆分

与 development-task-breakdown §2.11（WP-10-T01～T11）逐条映射（≙）；阶段 A 落地 UI-T02～T06、T09～T14 的阶段 A 子集，其余按阶段 B/C 交付。

| 任务 | 内容 | 前置 | 验收要点（追溯） | ≙ WP 任务 |
| --- | --- | --- | --- | --- |
| UI-T01 | 编写 ui 单元任务卡（本文） | WP-09-T01、core 卡 | 本文 §2/§6.3/§7/§8 契约冻结；计算逻辑零入 ui；插件无私占全局快捷键 | WP-10-T01 |
| UI-T02 | 构建落位：`sdurws_ird_ui` INTERFACE→STATIC（链 core＋diagnostics＋Qt Widgets，R-3 例外登记）；ui 测试目标与 `ird_gui` 标签；`sdurws_ird_testkit_qt` 启用登记 | UI-T01、WP-03-T01 | 目标编译；红线门禁含 ui 例外登记文本；静态扫描（Widgets 范围/QShortcut 作用域/前缀拼接） | WP-10-T02 |
| UI-T03 | 工作台壳与五区布局（WorkbenchShell、五区 Dock、布局记忆与恢复、最小可用布局、无项目首页、状态栏格式） | UI-T02 | UI-WB-1/2/3、UI-SES-1 | WP-10-T03 |
| UI-T04 | 七态公共状态呈现（StatusWordProjection＋九态短标签；映射表 §6.3） | UI-T03、WP-05-T08、WP-08-T06 | UI-STG-3；不新增状态词；证据不足不得显示通过 | WP-10-T04 |
| UI-T05 | 三维视图交互（RWStudioView3D 集成、视图/拾取/截图/分组显隐）——**阶段 A 仅中央区占位与视图契约登记，交互实现归阶段 B** | UI-T03 | UX-11 清单登记；KIN-06 会话姿态语义钉住（AT-04） | WP-10-T05（阶段 B 交付） |
| UI-T06 | CommandRegistry＋GlobalShortcutRegistry＋命令面板 | UI-T03 | UI-CMD-1/2、UI-HKY-1/2/3；模糊搜索与键盘导航；未绑定命令面板可达 | WP-10-T06 |
| UI-T07 | 统一工程策略入口界面与策略编辑适配器（P-POL-9）——阶段 A 交付摘要只读卡；编辑表单归阶段 B | UI-T03、WP-07-T05、WP-04-T10/T11 | 摘要只读、显示开关与计算开关分组异名（POL-ID-3） | WP-10-T07（摘要卡阶段 A） |
| UI-T08 | 参数表与表单公共件——阶段 B 交付（本文仅登记 §14） | UI-T03、WP-03-T04 | UX-04/05/07 公共编辑规则 | WP-10-T08（阶段 B） |
| UI-T09 | 阶段状态投影与工程用语（StageNavigationModel＋StageStatusModel 汇聚＋UiText 体系） | UI-T04、WP-22-T03（数据源，未产出→桩） | UI-STG-1/2；界面零哈希/Schema/插件名；投影不拥有门控规则 | WP-10-T09 |
| UI-T10 | 帮助入口与关于对话框（插件清单） | UI-T03、WP-24-T01 | UI-PLG-2；版本与冻结基线一致 | WP-10-T10 |
| UI-T11 | UiSessionController＋关闭/切换对话框机制（§5） | UI-T03 | UI-SES-2~7、UI-LCY-1 | WP-22-T06 协作侧 |
| UI-T12 | DraftController（§8） | UI-T03、WP-04-T12 | UI-DRF-1~4 | WP-04-T12 协作侧 |
| UI-T13 | 诊断/确认/任务呈现模型与 CommandInteractionBridge（§9） | UI-T03、WP-09、WP-08 | UI-CMD-4~7、UI-TSK-1~5、UI-DIA-1；`sdurws_ird_testkit_qt` 首消费者理由登记 | WP-10-T04/T06 协作侧 |
| UI-T14 | ui 契约测试套件（§12 全用例） | UI-T03~T13、WP-02 | ctest `ird`/`ird_gui` 注册；ird-test-report.json 留痕 | WP-10-T11 |
| UI-T15 | 工作台验证 harness（开发期 L5 装配雏形＋可视化验证入口，`sdurws_ird_ui_app`） | UI-T03、UI-T06、UI-T07、UI-T10、UI-T11 | harness 启动冒烟＋`--open/--new` 真实打开协议留痕；ird_gates 命中集恰增 2 处已登记（ui->project/ui->ui）；截图与日志留痕 traceability/builds/wp10-t15/ | WP-13 建模开发的验证前置 |
| UI-T16 | 工作台宿主插件（RobWorkStudio 集成入口＋开发期动态加载验证通道，`sdurws_ird_ui_plugin`） | UI-T03、UI-T11、UI-T15 | 框架 RobWorkStudio.exe 经 Plugins→Load plugin 加载后五区工作台嵌入主窗口（命令面板/打开协议/状态栏/布局记忆可用）＋RWStudioView3D 共存；harness 行为回归零变化；ird_gates 命中集增量登记；留痕 traceability/builds/wp10-t16/ | O-38 承接——WP-13-T15 前的嵌入验证前置；插件零计算逻辑；动态加载仅限开发期（SA-01 不变） |

退出条件（阶段 A，对齐 DTB 里程碑 M3 的 ui 侧）：UI-T01～T04、T06、T09（桩门控）、T11～T14 完成且 §12 用例在桩环境下通过；T05/T07（表单部分）/T08 转阶段 B。

> **UI-T05 阶段 A 落地登记（v0.7，2026-09-21）**：本行阶段 A 子集已交付——中央区三维视图占位面板（含"本阶段将在后续版本提供"说明＋UX-11 交互清单只读登记，零可交互业务控件——§4.2/§11.3 不虚构业务能力红线）＋视图契约登记（`View3DContract.hpp`：九项交互清单词表＋KIN-06/AT-04 会话姿态语义四位钉住，§16.7 v0.7）；交互实现仍归阶段 B（本行其余内容），承接时以该契约头为范围/红线边界。

> **UI-T07 阶段 A 落地登记（v0.9，2026-09-21）**：本行阶段 A 子集（摘要只读卡）已交付——右栏"诊断与设置区"策略摘要只读卡（`ird_policy_summary_card`：分组异名行集渲染＋「显式不适用」/单位换算投影＋"修改策略…"入口延期提示，§6.7 语义不变）＋呈现契约登记（`PolicySummaryCard.hpp`：分组词表/行模型/行键/阈值换算纯函数/卡面文案单点）＋`PolicySummaryProjection` 逐字段细化（PolicyThresholdProjection——C-10 端口返回值首消费细化，UI-T03 计数形态改派生聚合）；编辑表单仍归阶段 B（本行其余内容——"修改策略…"入口以 kPolicyEditDeferredNotice 延期提示承载 §11.4 不虚构能力，编辑经①命令端口提交的路径说明以 kPolicyEditPathNote 卡面呈现），承接时以该契约头为范围/红线边界。

> **UI-T08 阶段 B 落地登记（v1.0，2026-09-21）**：本行阶段 B 交付面（表单公共件）已交付——①公共契约头 `FormEditCommon.hpp`（§3.3 增行）：`ParamEditModel` 编辑会话模型（基线/暂存/就地错误三层值语义；UX-04/05/07 公共编辑规则唯一规则承载——数值＋单位同显〔KIN-12 显示投影经 core Units 唯一换算入口〕、非法输入就地显示原因保留原值、表单级确认应用〔UX-07 应用前确认交互的规则半区：无错＋有修改两前置，确认过的 `ParamEditSet` 经 ui 自有 `IFormEditOutlet` 移交域编辑器——断言与放行归 project 命令边界 §9.2〕、批量粘贴〔逐行独立判定＋影响明细〕/筛选/错误定位〔firstErrorKey 注册序稳定〕/取消恢复；零 Qt——模型层可测）、`advancedPanelDefaultFields()` 高级面板字段集（UX-04 数值半区：求解器最大迭代次数/采样点数/随机种子——计数类宿主约束，基线未设由域消费者注入，零默认数值 §11.4）；②参数表面板 `src/ParamTablePanel.cpp`（`createParamTablePanel` 工厂）：四列表格（参数｜值｜单位｜状态——值列就地编辑为唯一编辑形态）＋筛选行＋显示单位切换（KIN-12 仅投影）＋非模态确认区＋剪贴板粘贴＋可选"开发诊断"开关（会话显示设置——只回调不进编辑集，UX-07 后半句）；③O-31 处置（acceptance 2）：交付物零持有 C-3/4/5/7/8/10/11 对端类型——单位显示走已链接的 core Units 显示投影，草稿接入经 ui 自有 IDraftController.attachModule 接口（`IFormEditOutlet` 为域编辑器实现的移交面；`NoCrossUnitInclude_O31_UI_BUILD` 对新增文件扫描零命中）；④门禁：ird_gates 与 wp10-t07 基线（40）逐码比对恰增 1 处——ParamTablePanel.cpp 的 R3 头包含面命中（F-259 同型机器面滞后，R-3 例外范围同型；FormEditCommon.hpp 零命中——QWidget 仅前置声明），登记提交件 traceability/wp10-t08-gate-registrations.md（责任方 WP-01-T03）；56＋3＋25 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t08/，集成＋冒烟双模式同口径——冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR）；域消费者（WP-13-T15/WP-14-T08/WP-15-T12 等插件界面）经本契约头接入，接入时按 §14.1 承接行与契约头注释复核。需求语义零变化：UX-04/05/07/KIN-12 承接语义按 REQUIREMENTS 原文与 §4.2/§4.5/§4.6/§6.6/§14.1 登记落位。

> **UI-T11 落地登记（v1.3，2026-09-21）**：本行已交付——①会话状态机编程入口 `UiSessionController.hpp/.cpp`（§3.3 增行；零 Qt 模型层）：§5.2 七态唯一执行者（NoProject→Opening→OpenWritable/OpenReadOnly→CloseConfirmed→Draining→Closed〔瞬态〕→NoProject）＋§6.2 epoch（打开成功/关闭完成/切换绑定新项目各递增一次——切换绑定恰一次递增，后台持有点排空不递增〔不改变当前会话身份轴〕）；②统一确认对话框机制（§5.4 S1/S2，PM-03）：`assembleCloseDialogData` 数据装配唯一实现点（草稿区＝C-5 清单直通＋会话脏位独立事实、任务区＝C-8 非终态行直通、saveDraftsAvailable＝writable——§5.5 只读禁用的呈现半区）＋`beginClose`/`beginSwitch`（对话框呈现不改状态、单槽纪律）＋`resolveCloseDialog`（[取消]回原状态/[保存]→saveAll(Manual) 接线点·失败 SaveFailed 中止/[放弃]清会话脏/[协作取消]重查集逐任务 requestCancel〔NFR-PERF-02 ui 侧只发请求〕/[等待]无额外动作；切换流＝候选验证"成功才切"——失败 CandidateRejected A 会话不变，成功则 A 入后台持有点＋B 绑定 epoch++）；③INV-SES-1~4 实现（INV-SES-1＝isReadOnlySession 只读打开结果直写位；INV-SES-2＝B 进 Open* 唯一路径前置＝A 已转持有点＋openProject 重入拒绝；INV-SES-3＝DrainingHold shared 保活直到回调/轮询后释放；INV-SES-4＝pollDrain 四级防线）；④§5.6 防线：兜底轮询 closed()→T_force 强制结束确认（默认 120 s——呈现一次不重复）→T_force2 放弃等待（默认 300 s——未确认时补行强杀序列＋UI-SESSION-CONTEXT-INVALID 双通道出线；INV-SES-4 不依赖用户在场）；两阈值经 UiSessionControllerDeps 装配期可配——**P-UI-8 处置维持**：按 §5.6 默认保守值实现，阈值裁决未闭合不私定终值；`cancelDrainWait`（尚未 abandon 时恢复显示，§5.2 状态表原文）；⑤迟到写提示（§5.3 行 4/§5.7，UI-SES-7/UI-LCY-1）：`makeContextInvalidNotice`＝目录 Warning＋Dev 日志双通道出线＋提示值零动作位（不重试、不缓存待写由数据形状保证）；⑥C-3/5/8 会话端口首消费冻结（§3.3 UiPorts.hpp 增注，O-31 注入面——§10.1 注释行"实例随打开流程注入"落位）：`IUiStoreFactoryPort`（五步协议入口＋SessionPortBundle 绑定集出参——打开失败零绑定泄漏）、`IUiProjectStorePort`＋`IUiStoreCloseObserver`（requestClose 在途计数/closed 兜底轮询/subscribeClose RAII 订阅）、`IUiDraftQueryPort`（只读清单——写面归 §8.1 DraftController）、`IUiSessionTaskPort`（非终态清单/requestCancel/requestForceTerminate）；会话投影族落位 UiProjections.hpp（§3.3 增注）；⑦测试落位：sdurws_ird_ui_test 增 SessionControllerModelTest.cpp 十九用例＋sdurws_ird_ui_contract_test 增 SessionContractTest.cpp 六用例（契约 verify 行承接——§12.1 第二层：关闭协议计数/时序/回调恰一次、任务端口逐任务寻址、切换保活、退出就绪位、T_force 装配可配触发面、O-31 虚派发结构自证），106＋9＋28 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t11/，集成＋冒烟双模式同口径）；⑧门禁：ird_gates 与 base（63e30071＝UI-T10 合入后）同引擎 detached worktree 基线归一化逐码比对**零变化**（新增产品面文件零 Qt 头——防线/对话框机制全模型层），ird_gates-integration/base/smoke 三日志＋hit-set/diff 同目录留痕；validate-task PASS。需求语义零变化：PM-03/PM-07/TASK-03 承接语义按 §5/§6.2 原文落位，仅登记接口"形状"与实现口径（对话框 GUI 呈现面归 §12.1 第三层随 §12.3 用例扩展）。

> **UI-T12 落位登记（v1.4，2026-09-21）**：本行已交付——①草稿控制器 `IDraftController.hpp`＋`src/DraftController.cpp` 落位（§3.3 增行；零 Qt 模型层）：§8.2 保存编排（收集脏模块→域源 buildDraftDocument→经 ui 串行落盘执行器调用写半区端口→完成回执 Marshal 回 UI 线程消费——UI 线程零磁盘 IO 的执行面由端口替身线程标记自证；脏代次守卫实现"save 期间的新脏数据进入下一周期"）；§8.4 定时保存（tickAutosave＝L5 定时器心跳的控制器半区——60 s 默认周期〔PM-04-S1〕＋双触发防御＋在途跳过＝落盘线程串行）；§8.3 打开恢复编排（restoreOnOpen：清单→逐模块 tryLoad→Loaded/RecoveredFromBackup 经 adoptRestoredDocument 交域侧→损坏/孤儿/残留汇总）＋recoveryBanner 恢复横幅值装配（孤儿以计数呈现——UX-02 零内部名）；§8.5 StaleRevisionRejected 呈现（onCommandResult＝应用回执消费入口：Committed＝清脏＋清局部栈；Rejected(stale-revision)＝草稿保留＋暂存冲突定位数据→staleConflictData 冲突对话框数据〔当前 tip vs 草稿基线＋三选项键〕→reEditOnCurrentRevision〔基于当前版本重新编辑——域源重建基线＋控制器重置会话脏〕；**不自动重试提交由结构保证——控制器不持有命令网关**）；§8.7 草稿局部撤销栈（pushLocalCheckpoint/undoLocalEdit/redoLocalEdit——不经命令服务零修订；应用成功清栈＝"局部撤销不能撤销已应用修订"）；②C-5 写半区端口首消费冻结（§3.3 UiPorts.hpp 增注，O-31 注入面）：`IUiDraftStorePort`（save/tryLoad/discard 三原语——语义锚 project.md §5.4：save 仅落 drafts/ 零修订〔PM-04〕、tryLoad 损坏回退 .bak、discard 幂等写门卫）；③草稿投影族落位 UiProjections.hpp（§3.3 增注）：DraftDocumentProjection/DraftOrigin〔§4.4.5 冻结三值 token〕/DraftSaveOutcome/DraftLoadOutcome/DraftDiscardOutcome〔O-31 值投影〕＋StaleRevisionDetailProjection（RV-10 冲突定位——随 CommandResultProjection.staleDetail 承载，§8.5"数据由 CommandResult 附带"）＋DraftRowProjection 增列 moduleId/stale/baseRevisionCanonical（§8.2 汇总合并关联键与"基线已前进"提示位——既有字段零变化）；④`UiSessionController` 增 reportSessionDirty 生产者接线点（v1.3 既定缺口闭合——标题 `*` 判定位＝present ∨ 会话脏的会话半区经 DraftControllerDeps.onSessionDirtyChanged 翻转回调驱动上下文重呈现）；⑤测试落位：sdurws_ird_ui_test 增 DraftControllerModelTest.cpp 十三用例（保存零修订与清脏〔UI-DRF-1/PM-04〕／未保存标记两源合并〔PM-11〕／跨模块汇总／默认周期与双触发防御／落盘线程纪律与 UI-DRAFT-AUTOSAVE-FAILED〔UI-DRF-2〕／在途串行代次守卫／同步结局与只读拒绝〔§5.5〕／.bak 恢复与横幅〔UI-DRF-3/PM-08/AT-21〕／损坏清单与显式 discard〔§8.3-4〕／Stale 保留与冲突呈现〔UI-DRF-4/RV-10/AT-29〕／重建基线／两栈边界〔PM-18〕／会话生命周期与生产者接线闭环）＋sdurws_ird_ui_contract_test 增 DraftContractTest.cpp 三用例（写半区端口与 DraftService 语义锚闭环＋来源 token 冻结三值＋真后台线程零 UI 线程落盘／Stale 逐字段传递＋O-31 虚派发／关闭语境同步完成＋只读拒绝——§12.1 第二层契约面），119＋12＋27/28 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t12/，集成＋冒烟双模式同口径——冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR；GUI 28 用例中 1 项环境失败如实登记：ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1 宿主剪贴板被占用 OpenClipboard 0x800401D0 三轮一致——UI-T08 既有面与本任务零耦合，模型层等价断言随 119 用例全绿，testkit §7.5 不记为通过）；⑥门禁：ird_gates 与 base（e071a0a7＝UI-T11 合入后）逐码比对**零变化**（IRD-GATE-LIB×8/R-1×1/R-3×33/R-4×3/R-5×1/SUB×39/T-1×5/T-2×1 两侧一致、目标集合 diff 空；新增产品面文件零 Qt 头包含零表外边），ird_gates-integration/smoke 日志＋hit-set/diff 同目录留痕；validate-task PASS。需求语义零变化：PM-04/PM-08/PM-11/PM-15/RV-10/PM-18 承接语义按 §8/§12.3 原文落位，仅登记接口"形状"与实现口径（§10.5 原文 restoreOnOpen(project) 的对端形态按 O-31 改会话绑定注入，随 §16.7 v1.4 增量修订登记）。

> **UI-T13 落位登记（v1.5，2026-09-21）**：本行已交付——①诊断呈现模型 `IDiagnosticPresentationModel.hpp`＋`src/DiagnosticPresentation.cpp` 落位（§3.3 增行；头文件零 Qt）：§10.8 五方法（query 转发〔过滤/去重折叠/稳定序全在 sink——模型零再加工，同 dedupKey 折叠以 occurrences 呈现〕、expandChain 原因链、pendingConfirmations〔经 IUiFindingQueryPort"经 project 间接读"，不直接调 diagnostics 服务端接口〕、subscribe〔目录通知→M-1 Marshal→UI 线程分发，退订丢在途通知〕、redactedPath 只读转发〔NFR-SEC-07——装配缺服务 fail-fast〕）＋§9.1 纯函数装配唯一权威：diagActionFor 动作映射表（§4.4 全动作族 totality——fix-input→跳转编辑/contact-holder→显示 PID/rerun-interrupted→重跑入口/confirm-or-fix→确认对话/inspect-log→日志页/retry-readonly→只读横幅族等，未知族兜底 None 不虚构）、diagnosticRouteFor 呈现映射（取消→无错误呈现——UX-03；不可行证明同为非错误面）、formatComparison 三要素同显（数值＋单位一串呈现〔KIN-12〕，NotApplicable→「不适用」/Invalid→「无效（原串）」/NotProvided→「未提供」——不伪造数值）、assembleRecoveryBanner 恢复横幅（PM-15 一句话汇总按紧迫度择一＋固定三动作 查看详情/恢复草稿/放弃）、assembleUserLogPanel 日志面板（Tier-U 只读快照；Dev 级仅"打开开发日志文件"跳转不内嵌；模型无 flush 面＝flush 仅关闭路径的结构承载）；**expandChain 链查询缝（单侧冻结）**：diagnostics §9.7 投影不携带 causedBy（链数据只在 DiagnosticEntry 信封——sink 四方法无逐跳面），deps.chainOf 由 L5 经 diagnostics `CauseLink::chainOf` 供给（严格契约），未注入＝退化"起点自身即根"零虚构——缝的吸收点登记（diagnostics 投影扩展链字段后并入投影直读）；②任务呈现模型 `ITaskPresentationModel.hpp`＋`src/TaskPresentation.cpp` 落位（零 Qt）：§10.7 接口（activeTasks/backgroundTasks/task 双区、控制四请求、startProgressPolling/stopProgressPolling/pollOnce〔默认 200 ms〕、subscribe、attachProject/demoteCurrentToBackground 会话联动）＋§9.4 纯函数唯一权威：mergeProgressDecision 进度三规则（同 attempt percent 单调〔回退丢弃＋Dev 日志〕/新 attempt 重置/重复幂等）、archiveBadgeFor 归档子标（Completed 徽标附归档相位；ArchiveFailed→警告位＋诊断不改终态；未知 token 不虚构）、taskRowActionsFor 行动作（Failed/Interrupted 重跑入口〔新 TaskId 不自动重试〕、终态历史可查看）、resultBadgeFor 当前性徽标（C-7 CurrentnessProjection 搬运不计算——Superseded 附原因逐条、词表只有"当前/已过期"零「正式通过」字样）；清单当前置顶稳定序（Running 最前＋身份字典序次级键）、后台只读自限（P-UI-7 建议口径——控制请求 backgroundRejected＋"项目已关闭，后台任务只读"，未达对端）、暂停反馈双层保证（端口 Ack 反馈优先透传；行已知不支持暂停且 Ack 无反馈→"该任务不支持暂停"兜底——显式提示不静默）、强杀独立确认（requestForceTerminate(userConfirmed) 前置＋forceTerminateConfirmation 后果说明数据"任务记 Failed、检查点保留"；未确认→"强制终止前需在确认对话中确认"不发对端）、即发即忘（接口无等待任务形态——NFR-PERF-02）；③确认交互桥 `CommandInteractionBridge.hpp`＋`src/CommandInteractionBridge.cpp` 落位（头文件零 Qt）：**C-6 外翻形态**——实现 ui 自有 `IUiCommandInteraction` 端口（§2.1.2 C-6 行：L5 适配器实现 project::ICommandInteraction 并委托本端口；端口形状逐字镜像 project.md §5.3.3 冻结点——P-PR-7"对端单侧冻结互为起点不私改"），§9.2 四步线程模型落位（命令执行线程同步回调→QMetaObject 排队 Marshal→呈现器模态打开〔条件变量阻塞等待，占命令槽零事务资源、P-DIAG-6 无超时——等待谓词＝决议∨拆除，决议优先＝"用户意图已表达→照常放行"〕→markSessionDismantled 探针翻转＋唤醒全部在等＝Aborted(interaction-lost) 无用户诊断无修订）；principal＝会话启动采集缓存（P-UI-4——GetUserNameA，采集失败降级 unknown-user，provider 可注入），confirmedAtUtc＝组装时刻（IClock 注入）；ui 只交 credentials（确认判定/留痕/编译前复核全在 project＋diagnostics——SA-15）；桥内零命令/查询端口调用（重入死锁禁令的结构承载）；noteRevisionCommitted 失效标注（代次判别→新对话"输入已变化"＋已开对话呈现器实时标注——UI-CMD-6）；§9.3 对话装配纯函数 assembleConfirmDialogData（码表键推导 diag.<code-lower>.title/detail＋冻结确认/拒绝二键；FindingRecord 补全回调双层——subjectScope/confirmTextKey/optionKeys/baseRevision 命中随行、缺席缺行零虚构；Dev 折叠区只带命令类型 token，载荷摘要零进用户文本——UX-02）；④C-6/C-8/C-9 呈现面端口首消费冻结（§3.3 UiPorts.hpp 增注，O-31 注入面，登记 §16.7 v1.5）：`IUiCommandInteraction`（C-6——§9.2）、`IUiTaskPresentationPort`（C-8 呈现面——tasksByProject/task/progress 轻查询半区/requestCancel/Pause/Resume/ForceTerminate；v1.3 冻结的 IUiSessionTaskPort 三原语面保持不动，分立端口＝首消费冻结机制标准动作）、`IUiFindingQueryPort`（C-9 确认投影呈现半区）、`IUiUserLogSource`（Tier-U 面板）、`IUiRedactionPolicyPort`（脱敏配置——L5 会话变更 setPolicy）；任务投影族落位 UiProjections.hpp（TaskProgressProjection〔ProgressReport 锚——phaseToken 以 phaseLabelKey 文案键投影〕/TaskViewProjection〔TaskSnapshot＋TaskCapability.supportsPause 直读＋termination token〕/UiTaskAck〔StatusAck/CancelAck 折叠＋backgroundRejected ui 自限位〕/UserLogEntry）；⑤UiText 键族⑧ 32 键登记（§3.5 增族：比较占位二键＋恢复横幅六键＋日志面板二键＋确认对话八键〔含"确认不豁免校验"MDL-06④〕＋任务呈现十四键）；⑥测试落位：sdurws_ird_ui_test 增 DiagPresentationModelTest.cpp 十一用例（查询透传/折叠 occurrences/原因链〔真实 CauseLink 经缝驱动＋无缝退化〕/映射表冻结＋全族 totality＋UX-03 取消无错误/三要素四态占位/横幅择一与三动作/日志面板与 Dev 跳转/pending 经端口＋缺省空集/脱敏转发与装配 fail-fast/订阅 Marshal UI 线程与退订）＋TaskPresentationModelTest.cpp 十一用例（进度三规则＋轮询回退丢弃 Dev 留痕＋幂等/置顶稳定序与九态键/控制即发即忘＋强杀确认链/暂停反馈双层/后台只读自限/崩溃重跑/归档子标/当前性徽标零「正式通过」/装配 fail-fast）＋CommandInteractionBridgeTest.cpp 九用例（装配 fail-fast＋principal 缓存/确认凭据一一对应〔注入时钟〕/拒绝 nullopt/空集短路/拆除唤醒〔旁路线程驱动＋嵌套泵〕/无超时等待〔挂起泵轮数观测〕/失效标注/补全双层与 Dev 折叠/缺省装配零虚构），151/151 通过；sdurws_ird_ui_contract_test 增 InteractionTaskContractTest.cpp 六用例（O-31 虚派发结构自证〔is_base_of＋任意线程探针〕/Marshal UI 线程契约/C-8 到达序＋轻查询半区/即发即忘/C-9 装配缝投影〔端口供 FindingRecord→补全消费〕），17/17 通过；GUI 回归 27/28（1 项既有环境失败＝ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1 宿主剪贴板占用，v1.4 已登记与本任务零耦合，testkit §7.5 不记为通过）；gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t13/，集成＋冒烟双模式同口径（冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR；双模式构建零 error）；⑦门禁：ird_gates 与 base（7b62d221＝UI-T12 合入后）同引擎 detached worktree 基线归一化逐码比对恰增 4 处 R3（LIB×8/R-1×1/R-3×33→37/R-4×3/R-5×1/SUB×39/T-1×5/T-2×1——其余七码零变化、目标集合 diff 空；+4＝2 个新实现 TU〔DiagnosticPresentation.cpp/CommandInteractionBridge.cpp 的 Marshal Qt 面〕F-259 同型机器面滞后，登记提交件 traceability/wp10-t13-gate-registrations.md〔责任方 WP-01-T03〕）；validate-task PASS。需求语义零变化：UX-03/ERR-01/PM-15/TASK-01/02/03 承接语义按 REQUIREMENTS 与 §9/§10.7/§10.8 原文落位（knownPitfalls O-31 已裁决 2026-09-19 随整链放行——产品面零对端链接/include，NoCrossUnitInclude 守卫对新增文件零命中；P-PR-7 按 project.md §5.3.3 冻结点实现；P-UI-4 建议口径〔principal 会话缓存〕、P-UI-7 建议口径〔后台只读〕、P-DIAG-6 承接维持〔无自动超时〕）；§10.7/§10.8 原文对端类型（execution::TaskSnapshot 等）按 O-31 机制以 ui 投影承载（TaskViewProjection 等），接口"形状"随首消费冻结并随本版增量修订登记。

> **UI-T14 落位登记（v1.6，2026-09-22）**：本行已交付——ui 契约测试套件（承接面＝契约 acceptance 三条：ctest `ird`/`ird_gui` 标签注册与 §12.3 承接族全用例具名落位＋ird-test-report.json 留痕＋UI-EXEC-1 登记执行限制／UI-HKY-1·UI-STG-3·UI-WB-1 用例通过留痕／O-31 测试面直链落地）：①**O-31 处置（acceptance 3，已裁决 2026-09-19 随整链放行）**：§3.1 contract_test 目标行 v0.4 原文"project/execution 直链随 UI-T14 契约测试套件落地登记"兑现——`sdurws_ird_ui_contract_test` 落位直链 `sdurws_ird_project`/`sdurws_ird_execution`/`sdurws_ird_diagnostics`＋testkit（F-228 一并消解；**产品面守卫不变**：`sdurws_ird_ui` 链接块仍仅 core/diagnostics＋Qt 三件套——CMake 配置期守卫＋LinkageContractTest 链接块文本钉＋BuildRedLineTest include 面扫描三道防线常驻）；契约测试以 testkit FaultInterceptor 伪造对端接口、经 ui 自有端口适配桥接，对端类型形状漂移由测试面捕获（override＋对端字段访问＋成员函数指针 static_assert 三重编译期钉）；②**契约测试增两 TU（sdurws_ird_ui_contract_test 17→22 用例）**：`PeerBridgeContractTest.cpp` 四用例（测试面直链登记钉〔链接块含被测目标＋对端三库＋testkit，产品库链接块零对端〕／C-6 外翻桥接〔InteractionL5Adapter 实现 project::ICommandInteraction、委托经 `FaultInterceptor<ui::IUiCommandInteraction>`——故障计划命中注入 nullopt 失效路径并留 FaultLog、未命中透传且凭据与 findings 一一对应；双侧 requestConfirmations 成员函数指针 static_assert 镜像钉——P-PR-7 形状漂移编译期捕获〕／ITaskScheduler 伪造＋FaultInterceptor 注入→SchedulerPresentationAdapter 值投影桥〔对端"真实现"替身实现 execution::ITaskScheduler 全部纯虚；适配器 TaskSnapshot→TaskViewProjection 逐字段映射＋run→TaskId 寻址侧表＋phaseToken→文案键映射表；故障计划在 tasksByProject 第 1 次命中抛 ExecutionError→适配器折叠空投影＝端口"纯查询不抛"契约；Queued 快照 run 留零值＝P-EX-6 投影形态〕／diagnostics::RedactionPolicy 值面直用钉〔IUiRedactionPolicyPort::applyPolicy 方法签名 static_assert——表内登记边 ui→diagnostics 值面的测试侧承载〕）、`Section12RegistryTest.cpp` 一用例（§12.3 逐例注册审计：承接族＝WB/SES/DRF/CMD/HKY/STG/TSK/PLG/DIA/PERF/LCY 十一族全部用例编号＋UI-EXEC-1 执行限制——三测试目标源码语料归一化〔下划线→连字符〕后存在性核对、审计文件自身排除防恒真、缺失编号全量列示；V3D/PALETTE/POL/FRM/HELP 族由 UI-T05/06/07/08/10 已合入文件承载——其 §12.3 行"同源互证"观测点即指向对应文件——不属本契约承接族清单）；③**模型测试增三具名用例（sdurws_ird_ui_test 151→154）**：`PluginAssemblyFailureDegradationIsolatesRow_UI_PLG_1`（§12.3 UI-PLG-1 模型半区：§11.3 失败降级隔离——失败行"装配失败"占位＋UI-PLUGIN-ASSEMBLY-FAILED 证据保留＋计数零虚构、成功行照常充实、未命中行照常"未装配"占位、行集恒等白名单）、`QueuePositionWordingOnlyWithoutNumbers_UI_EXEC_1`（UI-EXEC-1 具名化：排队呈现＝"排队中（等待资源）"固定文案、零数字字符——队列位置/资源占用无对外接口〔execution §A3〕，不验证数值不虚构数据源；行与轮询面零虚构进度）、`UiThreadHeartbeatUnderSlowPeerQuery_UI_PERF_1`（§12.3 UI-PERF-1/NFR-PERF-01：慢对端 >1 s 场景下事件循环心跳探针〔25 ms 采样〕无 >200 ms 间隙＋单次投影刷新 <50 ms＋后台慢查询结果经排队 Marshal 落地消费；§12.2 判据纪律——判定＝心跳时间戳与刷新耗时测量，后台 1.1 s 限时等待为场景构件非判据）；④**落位结果**：sdurws_ird_ui_test 154/154、sdurws_ird_ui_contract_test 22/22、GUI 回归 27/28（1 项既有环境失败＝v1.4 已登记 BatchPasteImpactDetail_UI_T08_ACC1 剪贴板占用 0x800401D0，本轮连续五次复现一致，与本任务零耦合，testkit §7.5 不记为通过）；ctest 标签双命令在 ui 叶子目录执行（`^ird$` 2/2 通过；`^ird_gui$` 1 例 exe 级失败＝同一环境例——`--test-dir build` 根空转系 F-251 已登记框架 CTestTestfile 链缺失〔顶层未 enable_testing〕，非本任务引入，叶子目录口径沿 RPT-T11 验收先例）；ird-test-report.json 五件〔集成 model/contract/gui＋冒烟 model/contract/gui〕＋gtest XML＋控制台日志留痕 traceability/builds/wp10-t14/，集成＋冒烟双模式同口径（冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR），双模式构建零 error；⑤**门禁**：ird_gates 集成/冒烟同面，与 base（11534184449b295252f346e460ced8306d935d0a＝UI-T13 合入收尾）按码比对七码零变化（LIB×8/R1×1/R3×37/R4×3/R5×1/T1×5/T2×1），SUB 39→45 原始命中＝恰增 3 条唯一命中（ui_contract_test 直链 project/execution/diagnostics——O-31 裁决测试面既定形态非违约增边；MSBuild 命令回显使每条命中在日志出现两次，拆行去重后恰为 3 条），登记提交件 traceability/wp10-t14-gate-registrations.md（责任方 WP-01-T03）；validate-task PASS。需求语义零变化：UX-03/09/10/13 承接语义按 §12.3 原文落位，本任务零产品行为变化（变更面＝测试面＋契约测试目标的测试侧链接面，产品库本体零改动）。

> **UI-T15 落位登记（v1.7，2026-09-22）**：本行已交付——工作台验证 harness `sdurws_ird_ui_app`（开发期 L5 应用壳雏形：建模 WP-13+ 开发期间的可视化验证手段，所有者于流水线熔断停等期间直接指派）：①**目标落位**（ui/CMakeLists.txt 增量块）：`sdurws_ird_ui_app` 可执行目标——引擎预留 `_app` 后缀形态首次启用（ird_gates 分类面 plugin/worker/app；Qt 链接合法不触发 R-3），源码在 `ui/app/`（非 include/、src/ 产品面——门禁产品扫描域零触碰，R-3 命中集零变化）；POST_BUILD 复制运行时 DLL 后生成 qt.conf 钉回真实 Qt 前缀（修复 VS 启动项/双击场景 "Could not find the Qt platform plugin windows" 启动失败——Qt6 可重定位前缀 DLL 从副本推导不出插件目录；qt.conf 为构建期生成物不入库，前缀自 Qt6_DIR 推导）；②**端口适配器族**（`app/PortAdapters.hpp/.cpp`，O-31"装配层同时看见两边写单行适配器"的落地）：`StoreFactoryPortAdapter`（IUiStoreFactoryPort→project::ProjectStoreFactory::open——路径 weakly_canonical、UiOpenMode 直映、StoreError 折叠为稳定 token＋detail 透传不吞错、成功装配 SessionPortBundle）、`StorePortAdapter`（IUiProjectStorePort→ProjectStore 关闭协议三原语；unique_ptr 独占持有＝INV-SES-3 shared 保活的适配器侧落点；ICloseObserver→IUiStoreCloseObserver 类型翻译；v0.1 边界＝subscribeClose 句柄为无操作实现〔project 关闭契约"回调至多一次、无退订"〕，转发观察者由适配器持有到析构——悬挂不可能）、`ProjectDiagnosticsBridge`（project::IDiagnosticsSink→diagnostics 管线：report 经 DiagnosticsFactory::create 唯一入口入目录〔未注册码 fail-fast 不吞〕、reportDev 直转 IDevLogSink——§6.2 双通道纪律）；③**v0.1 诚实桩**（零虚构纪律）：`UnloadedPolicySource`（策略未装载——真实装配需对象存储＋闭包查询＋碰撞评估器〔policy.md §6.5 完整装配线〕，available=false 合法形态）、`NullUiNameResolver`/`DraftQueryPortStub`/`SessionTaskPortStub`/`HarnessAboutSource`（恒 nullopt/恒空行集——关闭对话框与真实草稿/任务链路随其装配任务接入后替换，适配点单一 ui 侧零改动）；④**装配序列**（`app/HarnessMain.cpp`）：诊断栈（LoggingPipeline 双文件日志＋StableCodeRegistry 87 码收编＋ui 九码→seal→DiagnosticsFactory→seal＋DiagCatalog＋RedactionService 挂接）→ShellWiring（必填四端口非空；eventBus=nullptr 显式声明——产品总线归 execution·ui，测试参考总线不入装配面）→UiSessionController（storeFactory 适配注入；presentContext→壳上下文投影；saveAllDraftsManual/forceAbandonAll 不接线＝关闭链路 v0.1 不可达，误触发按契约 fail-fast）；⑤**交互验证路径**：无参＝无项目首页态（PM-10 三入口/命令门控/命令面板 Ctrl+Shift+P/布局记忆）；`--open <目录>`＝真实打开五步协议（PM-11 状态栏/项目作用域命令使能/最近项目登记；`--readonly` 显式只读）；双开同名项目可验 PM-07 锁竞争降级横幅（attempt 1 留痕场景误配——在库 lock2 日志实为 `--readonly` 第二实例所产、不构成锁竞争场景；attempt 2 返工以双可写意图实例复测——见下方返工登记注）；`--new <目录> <显示名>`＝createNew 后释放创建锁再经标准打开协议进入（同一路径锁干净）；强杀进程不落盘＝后台写线程未走关闭路径的预期行为；⑥**落位结果**：集成构建零 error（sdurws_ird_ui_app.exe 产出）；ui 叶子目录 ctest 回归 `^ird$` 2/2、`^ird_gui$` 28/28 全通过（本轮宿主剪贴板空闲，v1.4 已登记的 BatchPasteImpactDetail 环境例通过）；ird_gates 与 base（2b5a8335）命中集归一化比对恰增 2 条唯一 SUB 命中（`ui->project`＝装配层特权边〔ui 库产品面仍零对 project 链接〕＋`ui->ui`＝app 目标自边——同型于 ui_gui_test 既有 `->ui` 登记形态），登记提交件 traceability/wp10-t15-gate-registrations.md（责任方 WP-01-T03）；harness 启动日志＋项目态/首页态截图＋PM-07 双开日志留痕 traceability/builds/wp10-t15/。需求语义零变化：本任务零产品库行为变化（变更面＝新增开发工具目标，`sdurws_ird_ui` 链接块与产品面源码零改动；UX-09/PM-07/PM-10/PM-11 的交互验证为既有已验收行为的观测面，非新语义）。

> **UI-T15 返工登记（v1.8，2026-09-22）**：验收 attempt 1 判 fail（阻断 1 项 B-1——PM-07 第二写者场景第二实例进程异常终止 0xC0000409＝std::terminate；在库双开留痕实为 `--readonly` 第二实例所产，与声称场景不符）。根因链与返工处置：①**根因**＝project 打开协议经 `IDiagnosticsSink` 发出的 PRJ-LOCK-HELD 等项目级事件记录（subject=∅——project 侧引 diagnostics §7.7 映射表"lock-held-by-other，subject=∅"）与 diagnostics 工厂校验链在集成面首次相遇即被拒（用户级码缺 subject——core §4.8/DiagnosticsErrorCode::SubjectMissing；快捷 sink 形态亦无法携带 paramSchema 占位），而 `ProjectDiagnosticsBridge::report` 对 create 的契约内异常零处理，异常穿透控制器与 main 触发 std::terminate；发射面缺 subject 属跨单元契约缺口（验收记录 F-290 候选），裁决权在 project/diagnostics 所有者——本返工不越 allowedFiles 代修对端。②**桥侧显式纪律**（`app/PortAdapters.*`）：`report` 补齐 create 上下文宿主标识（sourceUnit="project"/sourceInterface="sink.report"——DiagnosticsSinkImpl §9.7 同款形态；params 保持空，不做文本反解＝零虚构）；create 拒绝按错误码两分组——装配/桥自身缺陷（CodeUnknown/Usage）原样上抛 fail-fast；对端记录契约缺口（CodeDeprecated/SubjectMissing/ComparisonMissing/ParamSchemaMismatch/ContextMissing）经 Dev 通道具名落盘（channel `ird.harness.diagbridge`，消息含码/拒绝 token/工厂 detail/缺口归属 F-290）后**返回使打开协议继续**——PM-07 降级只读是 open 的成功形态（UiPorts.hpp C-3"降级只读也是成功"），条目未入目录的事实可观测不静默（v0.1 诚实边界增补：**PRJ-\* 项目级事件用户级条目在缺口裁决前不入诊断目录，横幅数据源为打开结果自带锁事实 readOnlyCause/lockHolder，两通道独立**）。③**编排侧兜底**（`app/HarnessMain.cpp`）：打开编排段 try/catch 包裹——残存异常（装配类缺陷）转消息框＋控制台双通道留痕＋受控退出 exit 5，杜绝 std::terminate 进程死亡，也不静默吞。④**复测与留痕**：双可写意图实例复测 PM-07——实例 1 `--new` 持锁存活（可写：是）、实例 2 普通 `--open` 同目录存活进入会话且"可写：否（降级只读——见横幅）"（PID 横幅在位）；双开日志/启动日志/截图刷新 traceability/builds/wp10-t15/（ctest 日志按契约 note 文件名 ctest-ui-ird.log/ctest-ui-gui.log 落位，attempt 1 的 ct-ird-ui.log/ct-gui-ui.log 命名漂移随之消解；gtest XML＋ird-test-report.json＋集成构建日志＋validate-task.log 按 F-288 缺口清单补齐）。需求语义零变化：本返工零产品库行为变化（变更面仅 `ui/app/` harness 与单元卡登记；PM-07 需求原文"双实例第二写者不阻塞等待"的承载语义不变）。

> **UI-T16 立项登记（v1.9，2026-09-23）**：O-38 裁决（DTB §4.2，所有者 2026-09-23 融合方案评估）承接——工作台宿主插件 `sdurws_ird_ui_plugin`（开发期 RobWorkStudio 集成入口）：①**形态**＝rws::RobWorkStudioPlugin 派生薄插件（框架基类本体即 QDockWidget 子类，虚函数 initialize/open/close/setupMenu/setupToolBar＋getRobWorkStudio/getState 注入面；IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1"），插件零计算逻辑——项目打开协议的端口适配复用 harness PortAdapters 形态（§13 UI-T15 落位登记注②的同款适配面搬入插件目标）；②**壳拆分授权**＝IWorkbenchShell 现为独立顶层 QMainWindow 形态（§10.1 mainWindow 唯一 Widget 出口、五区 addDockWidget＋中央区 setCentralWidget），本任务把壳拆"内容装配层＋顶层窗口宿主层"两层——harness（`sdurws_ird_ui_app`）与插件共用同一内容装配面，五区交互/布局记忆/命令门控语义不变，harness 行为回归零变化为硬验收项；契约面增量修订随本任务实现落位登记（§10.1 增量注）；**禁止把顶层 QMainWindow 嵌套进宿主 Dock**（双菜单栏/双状态栏/双 Dock 管理反模式——布局保存/快捷键/关闭行为全部复杂化）；③**三维视图**＝宿主模式中央区与框架 RWStudioView3D 共存的最小接入（框架 RobWorkStudio::getView()/getWorkCellScene() 注入面；壳占位面板在宿主模式的让位形态随实现登记），完整三维交互仍归本表 UI-T05 阶段 B；④**通道边界**＝开发期经框架 Plugins→Load plugin 动态加载仅限验证用途，产品交付路径零新增动态加载入口（SA-01/NFR-SEC-04；正式静态装配归 WP-24-T03——产品装配层落位时本插件按同一契约转正式装配或由其替换）；⑤**构建口径**＝插件目标 Q_OBJECT/插件元数据（Q_PLUGIN_METADATA）所需 AUTOMOC 仅该目标开启，ui 库本体与既有目标维持关闭（§3.1 登记注口径不变）；ird_gates plugin 分类面（Qt 链接合法），新增命中随任务按 DTB §4.5 模式登记。

---

## 14. 后续阶段承接与接口交接清单

### 14.1 阶段 B/C/D 承接

| 事项 | 阶段 | 承接内容 |
| --- | --- | --- |
| 三维视图交互（UX-11 全量：标准/相机视图、拾取、截图、碰撞高亮组掩码） | B | UI-T05；中央区 RWStudioView3D 集成；会话姿态红线（KIN-06）已在本文 §2/§6.8 钉住，并自 v0.7 起有代码侧锚点 `View3DContract.hpp`（九项交互清单＋语义钉住常量——阶段 B 实现承接其范围与红线边界，C-11 复核随阶段 B 增量修订） |
| 参数表/表单公共件（UX-04/05/07：批量粘贴、筛选、就地错误、单位同显） | B | UI-T08；单位换算唯一入口 core::Quantity::displayValueIn 已锁定；**自 v1.0 起公共件已交付**（FormEditCommon.hpp 编辑契约＋ParamTablePanel 面板——§13 UI-T08 落地登记注），域消费者插件接入（WP-13-T15/WP-14-T08/WP-15-T12 等）为本行余项 |
| 策略编辑表单（UX-08 编辑侧） | B | UI-T07 余项；IPolicyValidator 预校验→命令端口路径已定（§6.7） |
| 新建/打开/另存/导入导出向导编排 | B | workflow（WP-22）；ui 对话框基件与命令项已注册（§7.1） |
| 业务阶段面板接入 | B/C/D | 各域 `ird_<unit>_plugin` 经 IPluginUiRegistrar（§11）；面板内应用修改走 draft.apply 语义（§8） |
| 报告预览宿主 | B/C | reporting 预览接口（units/reporting.md 未产出——接口待其详设冻结；P-UI-9 登记） |
| 撤销/重做入口的域摘要 | B | UndoRedoStatus.undoSummary/redoSummary 呈现（project 已提供） |
| 草稿周期用户可调（PM-04-S1）、项目属性面板（PM-11-S1~S3）、修订历史浏览（PM-12-S1） | R2 | §4.6 设置项已预留接口位 |

### 14.2 接口交接清单（ui ↔ 各单元）

| 方向 | 内容 | 状态 |
| --- | --- | --- |
| project→ui | ICommandInteraction 契约（P-PR-7）、requestClose/closed/subscribeClose、DraftService、UndoRedoStatus、listDrafts/listRuns | 已冻结（project v0.1）；本文交叉核对通过（§9.2、D-4） |
| execution→ui | TaskState 事件流、ProgressReport、TaskSnapshot（PM-03 清单）、EventBusImpl marshaling 扩展点 | 已冻结（execution v0.1）；DrainPolicy 枚举不一致待澄清（P-UI-3）；队列位置/资源占用无接口（UI-EXEC-1 登记） |
| evidence→ui | CurrentnessResult（过期原因）、VerdictTrace（缺项）、FormalPassEligibility、CurrentnessIndex 会话投影 | 已冻结；P-EV-4 呈现口径由本文建议＋待需求侧确认（P-UI-2） |
| diagnostics→ui | DiagProjectionItem/FindingRecord 投影、actionKind 族、确认对话数据、Tier-U 面板数据 | 已冻结；ui 承诺交付：文案资源解析、七态投影、ICommandInteraction 实现（本文全部承接） |
| core→ui | 事件端口、TaskState 词表、单位换算、诊断数据契约 | 已冻结；ui 承诺：总线 UI 线程投递实现（UiThreadEventRelay §6.1）、九态短标签文案、七态×九态映射（§6.3） |
| policy→ui | IPolicyProvider 摘要投影、IPolicyValidator 预校验、适配器宿主归属（P-POL-9） | 已冻结 |
| runtime→ui | IRuntimeModelView、IRuntimeNameResolver（局部名呈现） | 已冻结；禁跨快照存 Frame 指针、禁视图旋转写回（本文 §2/§6.6 承接） |
| workflow↔ui | 门控输出（ui 消费）、StageStatusModel 汇聚（workflow 消费）、关闭/切换编排、O-26 碰撞检查命令 | **workflow.md 未产出——单侧冻结**（P-UI-6）；数据形状见 §6.5/§6.4 |
| io/reporting↔ui | 导入导出进度 UI、报告预览接口 | **详设未产出**——仅登记（P-UI-9） |

---

## 15. 需求—设计—验证追踪矩阵

| 需求 | 设计位置 | 验证用例 | 主 WP |
| --- | --- | --- | --- |
| UX-01 | §6.4（下一步建议呈现） | UI-STG-1 | WP-10-T09＋WP-22-T03 |
| UX-02 | §6.6、§3.5 | UI-STG-1（零内部名断言）、UI-DIA-1 | WP-10-T09 |
| UX-03 | §9.1 | UI-DIA-1、UI-TSK-2 | WP-09-T04＋WP-10 消费 |
| UX-04 | §4.2（右区/高级面板位） | 阶段 B（UI-T08） | WP-10-T08 |
| UX-05 | §14.1 | 阶段 B | WP-10-T08 |
| UX-06 | §6.3 | UI-STG-3 | WP-10-T04 |
| UX-07 | §7.7、§9.2 | UI-CMD-4 | WP-10-T08 |
| UX-08 | §6.7 | UI-T07 验收（POL-ID-3） | WP-10-T07＋WP-07-T05 |
| UX-09 | §4 | UI-WB-1/2/3 | WP-10-T03 |
| UX-10 | §6.3、§6.8 | UI-STG-3、UI-TSK-4 | WP-10-T04 |
| UX-11 | §4.2（中区契约）、§14.1 | 阶段 B（UI-T05） | WP-10-T05 |
| UX-12 | §6.4/§6.5 | UI-STG-1/2 | WP-10-T09＋WP-22-T03 |
| UX-13 | §7 | UI-CMD-1/2、UI-HKY-1/2/3 | WP-22-T11/T12＋WP-10-T06 |
| UX-14 | §11.4 | UI-PLG-2、UI-HELP-1 | WP-10-T10 |
| PM-03 | §5.4/§5.6 | UI-SES-4/5/6、UI-DRF-1 | WP-22-T06（机制 ui） |
| PM-04 | §8 | UI-DRF-1~4 | WP-04-T12（界面侧 ui） |
| PM-07 | §5.3/§5.5 | UI-SES-2/3、UI-CMD-3 | WP-22＋ui |
| PM-08/15 | §8.3、§9.1 | UI-DRF-3 | WP-22-T09（呈现 ui） |
| PM-10 | §4.3 | UI-SES-1 | WP-22-T05（页面 ui） |
| PM-11 | §4.2（状态栏格式） | UI-WB-1（标题断言并入） | WP-22-T09 |
| PM-14 | §4.6、§7.3 | UI-HKY-2 | WP-22（设置项 ui 拥有） |
| PM-17 | §11.5、§3.5（UI-QT-PLATFORM-FAULT） | WP-24-T02/T03 承接 | WP-24 |
| PM-18 | §7.1/§7.5（undo/redo 命令与空历史提示） | UI-CMD-2（并入 availability 断言） | WP-04-T13（入口 ui） |
| ERR-01 | §9.1 | UI-DIA-1 | WP-09-T04＋WP-10 |
| EVI-01/02 | §6.3（FormalPassEligibility 显示纪律） | UI-TSK-4 | WP-05＋WP-10 |
| TASK-01 | §9.4 | UI-TSK-1 | WP-08＋WP-10 |
| TASK-02 | §6.8（失败/取消/中断不得显示通过） | UI-TSK-3/4 | WP-08＋WP-10 |
| TASK-03 | §5.4、§6.2 | UI-SES-4 | WP-08＋WP-22/ui |
| CON-02/05 | §6.8 | UI-TSK-4 | WP-05＋WP-10 |
| NFR-PERF-01 | §3.4 | UI-PERF-1 | WP-23＋WP-10 |
| NFR-MNT-01/02 | §3.1 | UI-T02 静态扫描 | WP-03/WP-01＋WP-10 |
| NFR-MNT-03 | §6.3、§3.5 | UI-STG-3 | WP-03/WP-09＋WP-10 |
| NFR-SEC-04 | §11.1 | UI-PLG-1（无动态路径断言并入） | WP-01/WP-24 |
| NFR-SEC-07 | §9.1、§4.6 | UI-DIA-1 | WP-09＋WP-10 |
| NFR-REL-02/03 | §9.4 | UI-TSK-2 | WP-08＋WP-10 |
| AT-01（确认放行呈现侧） | §9.2 | UI-CMD-4/5/6 | WP-13/WP-04＋WP-10 |
| AT-04 | §2.2（会话命令不产生修订）、§7.7 | UI-CMD 系（会话命令修订数断言） | WP-15/WP-10 |
| AT-05 | §6.8 | UI-TSK-4 | WP-05/WP-10 |
| AT-10 | §6.2 | UI-SES-4 | WP-08/WP-22 |
| AT-11（呈现侧） | §9.4 | UI-TSK-2 | WP-08/WP-24 |
| AT-12（差异呈现） | §14.1（阶段 B 承接登记） | 阶段 B | WP-20/WP-22 |
| AT-13（呈现侧） | §9.1（恢复横幅） | UI-DRF-3 | WP-04/WP-22 |
| AT-29 | §7.1/§8.7 | UI-DRF-4、UI-CMD-2 | WP-04/WP-10 |
| AT-30（提示呈现） | §6.8（过期不沿用通过） | UI-TSK-4 | WP-19/WP-05 |
| AT-34（进度/取消呈现） | §9.4 | UI-TSK-1/3 | WP-20/WP-08/WP-10 |

---

## 16. 设计决策、风险、待裁决项与变更记录

### 16.1 本单元设计决策（D-xx）

| # | 决策 | 理由 |
| --- | --- | --- |
| D-1 | 事件总线 UI 线程投递由 ui 的 UiThreadEventRelay 承担（订阅进程总线→QueuedConnection 转投） | core.md §4.9 指定实现归 execution·ui；execution 零 Qt（D-01），故 ui 侧落地；同发布者 FIFO 天然保持 |
| D-2 | 进度＝轮询（200 ms）＋状态事件；进度不进领域事件 | execution §6.1：进度查询式 ≤10 Hz、不入 core 事件（D-09） |
| D-3 | 七态词表与九态映射在 ui 冻结（§6.3），求值优先级空项目＞计算中＞未完成＞失败＞数据不足＞过期＞可计算 | core.md §10.3 ui 义务；evidence §13"七态投影与九态映射"归 ui；避免双权威 |
| D-4 | ICommandInteraction 实现采用"Marshal＋阻塞命令执行线程"的 Bridge；principal＝会话启动采集的 Windows 用户名 | project.md §5.3.3 起点；P-PR-7 交叉核对通过（占槽零资源、无超时自动确认、关闭主动取消均可满足） |
| D-5 | 会话纪元（epoch）＋事件项目归属三分支路由（当前/后台排空/丢弃）实现迟到事件过滤 | TASK-03/AT-10；避免"先判丢弃再接纳"歧义（对齐 S7 单次判定口径——接纳归 execution，ui 只管呈现路由） |
| D-6 | 草稿落盘在 ui 后台线程执行（定时器 UI 线程触发、磁盘 IO 不占 UI 线程） | NFR-PERF-01（>1 s 转后台、UI 线程零磁盘事务）；DraftService 线程安全 |
| D-7 | 布局/快捷键/设置全部用户级持久化，不入 `.rwdesign` | PM-14/ARCH §6.7；恢复失败回退默认＋Dev 诊断，不阻塞启动 |
| D-8 | 命令执行使能态（谓词）与权威校验双层：谓词只控界面使能，权威拒绝在 project/execution | 谓词错误最多一次被拒提交（有诊断），不可能绕过权威（§7.5） |
| D-9 | 关闭对话框不提供强杀；T_force 超时转"强制结束并关闭"确认；极端回调丢失走 T_force2 兜底退出 | execution §7.4（关闭确认不提供强杀）＋§5.6 防永久阻塞（project §9.7 abandonAll 兜底） |
| D-10 | 插件缺失/装配失败降级为占位面板＋诊断，失败隔离不中止启动 | SA-01 静态装配；"不以控件存在推断业务能力" |
| D-11 | 后台排空项目任务只读呈现、控制按钮禁用（控制接口技术上可达但 ui 按项目归属自限） | 切换后旧任务归属旧项目；避免跨项目控制混淆（P-UI-7 登记开放范围裁决） |
| D-12 | NotEvaluable 当前性呈现归入"结果过期＋无法判定原因"（建议口径） | evidence P-EV-4 指定 ui 定呈现；"无默认 Current"不被违反（P-UI-2 待确认） |

### 16.2 风险

| # | 风险 | 缓解 |
| --- | --- | --- |
| R-1 | workflow.md 未产出：门控数据形状单侧冻结，后续不兼容 | §6.5 数据形状最小化（域就绪项＋缺项键）；P-UI-6 裁决跟踪；阶段 A 用桩注入 |
| R-2 | 七态映射被误用为业务判定（下游拿呈现态做逻辑） | §6.3 红线声明＋UI-STG-3 只测呈现；评审检查消费方 |
| R-3 | Bridge 阻塞命令执行线程期间用户继续操作引发状态错位 | 确认期间相关命令 availability 显示"命令进行中"；project 串行槽保证无并发修订 |
| R-4 | 大量诊断/事件导致 UI 卡顿 | 目录容量护栏（diagnostics 10,000 条上限）＋增量订阅＋epoch 丢弃；UI-PERF-1 探针 |
| R-5 | GUI 测试环境依赖（Windows/平台插件）造成 CI 不稳定 | `ird_gui` 串行标签＋envUnavailable 口径（不得绿灯）；模型测试覆盖逻辑层 |
| R-6 | execution DrainPolicy 枚举不一致导致关闭集成返工 | P-UI-3 提前登记；ui 侧按语义对接不受枚举名影响 |
| R-7 | 关闭超时参数（T_force/T_force2）不当造成数据损失或久等 | 默认保守值＋装配期可配；强制路径保检查点（execution 协议）；P-UI-8 裁决 |

### 16.3 上游冲突与不一致登记（集中列出：依据、影响、建议、裁决者）

| # | 冲突/缺口 | 依据 | 影响 | 建议 | 裁决者 |
| --- | --- | --- | --- | --- | --- |
| CF-1 | 架构 §3.5"ui→core：命令注册表类型"与 core.md §2.3"命令注册表归 ui"不一致（core 无此类型） | ARCH §3.5 vs core.md §2.3 | 无实现影响（本文将注册表类型落位于 ui 自有头） | 架构侧将 §3.5 该行语义改为"ui 消费 core 契约类型"或删除"命令注册表类型"字样 | 架构所有者 |
| CF-2 | ~~execution §7.5 `DrainPolicy::WaitForInFlight` 与 §10.1 枚举 `{CancelQueuedAndWait, KeepQueuedTerminate}` 不一致~~ **已销账（2026-09-10）**：execution.md v0.2 将 §7.5 更正为 `CancelQueuedAndWait` | execution.md §7.5 vs §10.1 | 无遗留——ui 按"取消排队＋在途归档后收口"语义对接（§9.5），与更正后枚举一致 | 已由 execution.md v0.2 澄清 | execution 所有者（已处置） |
| CF-3 | evidence §13 要求 ui 做"七态投影与九态映射"，但七态词表任何上游未定义 | evidence.md §13 vs 全文 | ui 已定义（§6.3），需确认 | 需求侧确认词表与优先级（P-UI-1） | ui 详设所有者＋需求所有者 |
| CF-4 | PM-03"运行中任务清单"数据在 project 查询端口缺席（只有 execution tasksByProject） | project.md §A7 | 无实现影响（ui 直接消费 execution 接口） | project.md 后续修订时补一句指向 execution（登记即可） | project 所有者 |

### 16.4 待裁决项（P-UI-x）

| # | 事项 | 建议默认 | 裁决者 |
| --- | --- | --- | --- |
| P-UI-1 | UX-10 七态词表/优先级冻结确认（CF-3；core O-24 关联） | 本文 §6.3 | ui 详设所有者＋需求所有者 |
| P-UI-2 | 当前性 NotEvaluable 呈现口径（evidence P-EV-4 承接） | 过期＋"无法判定"原因（D-12） | ui＋evidence＋需求侧 |
| P-UI-3 | DrainPolicy 枚举澄清（CF-2） | 按语义对接 | execution 所有者 |
| P-UI-4 | principal 采集口径（Windows 用户名是否满足审计需要） | 会话启动采集缓存 | project 所有者＋需求侧 |
| P-UI-5 | 架构 §3.5 ui→core 行消歧（CF-1） | 见 CF-1 建议 | 架构所有者 |
| P-UI-6 | workflow 门控/关闭编排/碰撞检查命令（O-26）三方契约 | 本文 §6.5 数据形状为谈判起点 | workflow 详设所有者（WP-22-T01 启动时） |
| P-UI-7 | 后台排空项目任务是否开放控制（当前只读） | 保持只读（D-11） | 需求侧 |
| P-UI-8 | T_force/T_force2 关闭超时阈值 | 120 s/300 s 可配 | L5/execution＋需求侧 |
| P-UI-9 | reporting 预览接口（详设未产出） | 命令项已占位、预览宿主阶段 B | reporting 详设所有者 |
| P-UI-10 | UI-\* 诊断码表最终登记（本文建议值与 diagnostics 码表合并） | §3.5 表 | diagnostics 所有者（码值权威） |

### 16.5 与约束清单的对照（任务三、八节的逐条核验）

| 约束 | 落实 |
| --- | --- |
| 不新增第二套命令入口/快捷键注册表/阶段状态机 | §7（唯一注册点）、§6.4（门控唯一归 workflow）；N-11 |
| 不把 UI 文本/控件状态/窗口生命周期当业务真值 | §7.5（使能态≠判定）、INV-SES-1、§6.8 禁令 |
| UI 线程不执行长时计算/编译/文件事务 | §3.4、D-6、UI-PERF-1 |
| UI 不直接写 `.rwdesign` | §2.1.3 N-1；一切写经 DraftService/命令端口/归档端口 |
| 确认回调经 diagnostics/project 契约 | §9.2（ICommandInteraction＋IConfirmableFindingService 分工，ui 不自行放行） |
| 静态白名单插件、无动态加载 | §11.1 |
| 不恢复 old/ 界面 | §1.3 |

### 16.6 交付前自审记录（2026-09-10，v0.1）

| 检查项 | 结论 |
| --- | --- |
| 是否重复 project/execution/evidence/diagnostics/workflow 职责 | 通过——草稿数据/归档/判定/门控/码值均为消费或机制侧（§2.1.3、§8.1、§9、§6.5） |
| 第二套命令入口或快捷键注册点 | 通过——唯一 CommandRegistry/GlobalShortcutRegistry；插件私占全局键列为静态违例（UI-HKY-3） |
| UI 直接修改领域对象或项目文件 | 通过——写路径全部经命令端口/DraftService（§7.7、§8） |
| UI 线程长时计算 | 通过——§3.4 线程表＋D-6＋UI-PERF-1 探针设计 |
| 控件状态误当业务真值 | 通过——§7.5 双层校验、INV-SES-1 |
| 输入变化后继续使用旧确认 | 通过——权威双复核＋ui 标注（UI-CMD-6） |
| 任务完成误当归档完成 | 通过——archivePhase 子标（§9.4、UI-TSK-3） |
| 关闭流程永久等待 | 通过——§5.6 四级防线（INV-SES-4） |
| 只读模式保留写入口 | 通过——§5.5 清单＋§7.6 双保险（UI-CMD-3） |
| 插件反向依赖或动态加载 | 通过——§11.1（白名单静态、R-1/R-2 不变） |
| 未经上游批准的新状态/阈值/工程判定 | 通过——七态为呈现投影且已列 P-UI-1 待确认；NotEvaluable 呈现列 P-UI-2；无新阈值 |
| 越权修改需求/架构/其他单元机制 | 通过——上游问题仅登记（§16.3），未改动任何其他文档 |

**声明**：本自审为文档级检查，不等同于实现测试通过或正式验收；实现与测试证据以 UI-T02～T14 交付物及 §12 留痕为准。

### 16.7 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版草案（阶段 A 范围）：基于 REQUIREMENTS v1.16（Accepted）与 ARCHITECTURE v0.11（Draft）及 8 份已产出单元详设（core/testkit/project/evidence/runtime/policy/execution/diagnostics，均 v0.1 Draft）编写；新建 `units/ui.md`（此前不存在）；冻结 §6.3 七态映射、§7 命令/快捷键设施、§8 DraftController、§9 确认与任务契约、§10 九接口；登记待裁决 P-UI-1~10 与上游不一致 CF-1~4 |
| v0.2 | 2026-09-10 | 全链一致性审计消账：①§3.1/§11 目标 `ird_ui_gui_test` 更名为 `sdurws_ird_ui_gui_test`（ARCH §1.4 命名约定，三处同步）；②P-UI-3/CF-2 销账——execution.md v0.2 已将 DrainPolicy 更正为 `CancelQueuedAndWait`，与本文对接语义一致；③§1.2"io/reporting 未产出"过期表述由文档治理任务同步（reporting.md 已产出并双向交接）。依赖边三方矛盾（C 表"接口依赖"标注 vs ARCH §3.5 白名单）登记 DTB §4.2 O-31，待架构所有者裁决，本文不私裁 |
| v0.3 | 2026-09-19 | WP-10-T02（≙UI-T02）构建落位登记：①§3.1 目标表逐行落地——`sdurws_ird_ui` INTERFACE→STATIC（目标名不变，C++17，PUBLIC 链 core＋diagnostics＋Qt6::Core/Gui/Widgets），`sdurws_ird_ui_test`/`sdurws_ird_ui_contract_test`/`sdurws_ird_ui_gui_test` 三测试目标按 DTB §5.5 注册（LABELS ird/ird/ird_gui——ctest 精确过滤用 `^ird$`/`^ird_gui$`，-L 为正则匹配）；②R-3 例外登记：目标形态按本表 §3.1 行承载（sdurws_ird_ui 本体即例外目标——DTB §4.5"预登记（WP-10-T02 生效）"行兑现）；ird_gates 机器面数据（IRD_R3_EXCEPTION_TARGETS 条目＋`_gui_test` 形态解析词表）滞后于人工登记册，回填登记提交件 traceability/wp10-t02-gate-registrations.md（责任方 WP-01-T03），门禁命中集 diff＝基线 19＋新增 8（全部契约明文面，findings.json F-227）；③`sdurws_ird_testkit_qt` 启用落位（testkit.md §10.1 触发已成立——本单元 §12.1 消费者定位兑现，设施随 UI-T13/T14）；④落位期源码形态＝锚点翻译单元＋三测试目标最小构建冒烟用例（O-31 include 面边界/R-4/QShortcut/例外登记文本核对的常驻自证），公共头与业务实现随 UI-T03+（不预建占位）；⑤O-31/P-UI-5 处置按契约 acceptance 4/5：仅表内边（ui→core、ui→diagnostics）＋CF-1 口径（注册表类型归 ui 自有头），零对 project/evidence/execution/policy/runtime 的链接或 include，不构成消账 |
| v0.4 | 2026-09-19 | O-31 裁决落地（裁决来源：DTB §4 O-31，所有者 2026-09-19 裁决）：确认 ui 侧最小注入接口模式——ui.md 依赖声明三方矛盾（§2.1.2 C 表"接口依赖" vs §3.1"仅链 core,diagnostics" vs ARCHITECTURE §3.5 白名单仅 ui→core,diagnostics）以"不增边＋ui 自有最小端口接口＋值投影＋L5 装配适配"消解：ARCH §3.5 不动，ui 产品面（sdurws_ird_ui 目标源集）保持零对 project/evidence/execution/policy/runtime 的链接或 include（UI-T02 已交付的 `NoCrossUnitInclude_O31_UI_BUILD` 守卫即该裁决的常驻执行面）；对端类型不直接进入 ui 头文件（对齐 reporting.md §3.3／evidence.md §3.3／io.md IO-D02／EX ICompileCacheJudge 等 10 卡"消费方自定义最小注入接口＋值传递"惯例）。编辑落点：①文档头版本→v0.4、日期→2026-09-19（并补同步文档头版本行——v0.3 变更行登记时漏改，文档头此前仍标 v0.2）；②§2.1.2 C 表：C-3/C-4/C-5/C-7/C-8 形态列改"运行时注入（O-31 裁决：ui 自有最小端口接口＋值投影，L5 装配适配；产品面零对端链接/include）"，C-6 方向外翻（ui 不再直接实现 project::ICommandInteraction，由 L5 适配器实现并委托 ui 自有交互回调端口），C-10 改 ui 自有策略摘要端口（不直接 include policy::IPolicyProvider）、C-11 改 ui 自有名称解析端口（resolveObjectId 语义按 runtime.md §7.3 冻结；IRuntimeModelView 共同消费点归阶段 B 复核），表后增注入形态统一细则注（C 表"消费对象"列自 v0.4 起读作语义协作面对象）；③§3.1 依赖边括注重写（不增边、`NoCrossUnitInclude_O31_UI_BUILD` 常驻执行面、端口/投影值形态以 §3.2 对端锚点为冻结基准、L5 适配器归装配层任务、ui 测试以可控替身承载）；④§3.1 contract_test 行链接列改"被测目标＋sdurws_ird_testkit＋GTest（落位直链面，WP-10-T02；core/diagnostics 经被测目标 PUBLIC 传染可达；project/execution 直链随 UI-T14 契约测试套件落地登记——测试目标链接面按本表承载，不属 ARCH §3.5 产品边管辖）"；⑤§3.2 重框为"投影值形态冻结基准"（投影≠重定义，NFR-MNT-03 单一权威不变——reporting ModelSummary／runtime §13.2 同案；分组类型清单原样保留即基准本身）；⑥§3.3 公共头布局新增 UiPorts.hpp（C-3/4/5/7/8/10/11 最小端口接口）与 UiProjections.hpp（值投影承载）两行；⑦§10.1 ShellWiring：policyProvider 成员→policySource（ui::IPolicySummarySource）、nameResolver 成员→ui::IUiNameResolver（均为 ui 自有端口，L5 适配对端），project/execution 实例随打开流程经 ui 自有会话/命令/草稿/查询端口注入；⑧§10 章首增 O-31 裁决引导注（§10 及 §4～§9、§11 行文中对端类型自 v0.4 起一律经 UiPorts/UiProjections 承载，对应接口签名随首个消费该面的 UI 任务实现冻结并做单元卡增量修订登记，CCP §5 载体）。findings.json F-228（本文件 §3.1 sdurws_ird_ui_contract_test 行"最终形态 vs 落位形态"措辞差）随④按其 resolution 建议措辞消解。需求语义零变化：UX-*/PM-* 等需求承接与验收语义一律不动（§2.2、§12～§15 零改动），仅改依赖"形态"。后续触发点：UI-T03+ 各消费任务在其 §10 对应节逐任务冻结端口/投影形状并增量修订登记；UI-T14 契约测试套件落地时登记 contract_test 对 project/execution 直链；阶段 B 随增量修订复核 C-11（IRuntimeModelView 共同消费点）与 C-14（reporting 预览接口） |
| v0.5 | 2026-09-20 | WP-10-T03（≙UI-T03）工作台壳落位登记：①§10.1 增量冻结（本门面与 wiring 的首消费任务，v0.4"首消费冻结"机制的首个落点）——`ShellWiring` 增成员 `devLog`（diagnostics::IDevLogSink，允许为空＝无日志测试场景、须显式声明）：UI-LAYOUT-RESTORE-FAILED 等 Dev 级码不入目录（diagnostics §6.2），唯一出线＝开发日志通道，v0.4 签名缺该通道则 §4.5"损坏回退＋Dev 诊断"不可观测；增量为 diagnostics 表内边成员补全，不触及 O-31 裁决主体（零对端业务单元边），ARCH §3.5 不动；②§10.1 代码块与实现文件逐字对齐＋阶段 A 增列门面 facets（regionVisible/setRegionVisible/resetLayout、presentProjectContext、commandAvailability、recentProjects/note/remove、createWorkbenchShell 工厂、uiDiagnosticCodeDescriptors 九码描述符供体——§10.1 副作用行"注册 UI-* 诊断码"的落地面：wiring 不携注册表句柄〔v0.4 裁决签名不扩〕，注册动作归 L5 装配序列）；③§3.3 UiPorts.hpp/UiProjections.hpp 形状随首消费冻结（IPolicySummarySource 计数投影、IUiNameResolver localName 投影、ProjectMetadataProjection/DraftPresenceProjection/ProjectContextProjection/RecentProjectEntry/PolicySummaryProjection、PM-11 唯一权威纯函数 formatProjectStatusText——O-31 裁决注 v0.5 段逐条登记）；④§12.1 gui_test 行兑现：sdurws_ird_ui_gui_test 载入真实 GUI 用例（UI-WB-1/2/3、UI-SES-1 及最近项目/PM-11 控件端到端——§12.3 四例＋2 辅助例），main 升级 QApplication 形态，§12.2 落位期"不启动 GUI"钉子按其自述授权拆除；⑤实现细节口径登记：布局/设置写盘走 §3.4 ui 后台落盘线程（值任务串行队列）、布局记忆版本化＋损坏三件事（出厂默认＋Dev 诊断＋整段丢弃，§4.5）、壳内文案表为 UI-T09 UiText 过渡承载、阶段占位/未接线命令占位说明＝§4.1/§11.4 契约显式设计、零 Q_OBJECT 不开 AUTOMOC；⑥§3.3 公共头布局首批落位 IWorkbenchShell.hpp/UiPorts.hpp/UiProjections.hpp（其余十头随归属任务）；⑦门禁状态登记：ird_gates 基线（efed7256 detached worktree 脚本模式）29 处 vs 本分支 32 处，新增恰为 3×R3（新源文件 Qt 头包含——R-3 例外头包含半区机器面滞后），回填登记提交件 traceability/wp10-t03-gate-registrations.md（责任方 WP-01-T03，findings.json F-259）；gate-all GUI 环境供给/冒烟 Qt 前缀透传两缺口随该件 §2 登记。需求语义零变化：UX-09/PM-10/PM-11/PM-14 承接语义按 §2.2/§4/§12 原文落位（§4.1~§4.6/§7.1/§7.5 原文照实现），仅登记接口"形状"与阶段 A 实现口径 |
| v0.6 | 2026-09-21 | WP-10-T04（≙UI-T04）七态公共状态呈现落位登记：①§6.3 承载首消费冻结（v0.4"首消费冻结"机制落点，§6.3 表尾已加登记注）——七态词表 `ui::StatusWord`（七枚举＝§6.3 词表行序；token/中文经 `statusWordToken`/`statusWordTransitionalLabel` 静态表承载，键 `state.<token>.label` 经 `statusWordLabelKey`；词表与求值优先级**未改一字**——P-UI-1 处置）、求值入口纯函数 `evaluateStatusWord(StatusFacts)`（src/StatusProjection.cpp——优先级 empty-project＞computing＞incomplete＞failed＞data-insufficient＞results-stale＞computable 逐短路；NotEvaluable＝status 空＋成因归 results-stale＋「无法判定」原因，P-UI-2/P-EV-4 建议口径，D-12 同稿）、触发数据源聚合值 `StatusFacts`（会话 NoProject 位＋`ReadinessProjection`〔evidence §6.4.1① ReadinessSummary 锚点〕＋`TaskActivityProjection`〔core::TaskState/TaskOutcome/EngineeringStatus 词表直用——表内边〕＋Error 级诊断活跃位＋失败诊断投影项＋三个 C-7 值投影）、`CurrentnessProjection`/`EvidenceManifestProjection`（含 `EvidenceItemProjection` 五值状态）/`FormalPassEligibilityProjection`（evidence §8.1/§6.2/§7.2 锚点——投影≠重定义，§3.2 冻结基准）、伴随呈现数据随投影同行（计算中附 `progressStageKey`＋`cancelAvailable`、过期附 `staleReasons`/`notEvaluableCause`、失败附 `failureDiagnostics`〔diagnostics::DiagProjectionItem 表内边原样承载〕、数据不足附 `unsatisfiedEvidence` 全量清单〔`unsatisfiedEvidenceItems`——Missing/Invalid/Unverified，NotApplicable 不计缺失〕）、显示纪律唯一实现点 `formalPassRenderable()`（FormalPassEligibility 五条件唯一放行数据源）、工作台总徽标聚合 `dominantStatusWord()`＋`statusWordPriority()`；②C-7 端口 `IUiCurrentnessSource` 落位 UiPorts.hpp（currentness/evidenceManifest/formalPassEligibility 三只读方法，O-31 裁决注入面——L5 适配 evidence 只读面，ui 测试以可控替身承载）；③九态短标签键 `taskStateLabelKey(core::TaskState)` 就位（PM-03/PM-11——键 token 段与 core `toToken(TaskState)` 词表逐字一致〔测试对断防漂移〕；中文短标签九值为 UI-T09 UiText 资源化前过渡承载，UI-T03 文案表同案，迁移时键不变）；④测试落位：sdurws_ird_ui_test 增 StatusWordModelTest.cpp 八用例（UI-STG-3 映射表七行逐行＋优先级相邻对＋词表冻结集＋NotEvaluable 口径＋FormalPass 放行门＋伴随数据四面＋九态键＋C-7 替身端口链路），16/16 通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t04/）；⑤门禁：ird_gates 与 wp10-t03 基线逐码比对零变化、新增文件零命中（本文件 gate 记录同上目录）；⑥实施口径登记：ProgressReport 的 phaseToken 以 `progressStageKey` 文案键形态进入投影（§9.4 键值分离），任务轴控制面（取消/暂停请求）不在本任务——归 §10.7 ITaskPresentationModel（UI-T13），本任务只承载只读活动事实。需求语义零变化：UX-06/UX-10/PM-03/PM-11 承接语义按 §6.3/§6.8 原文落位，仅登记投影"形状" |
| v0.7 | 2026-09-21 | WP-10-T05 阶段 A（≙UI-T05 阶段 A）三维视图占位与契约登记落位：①中央区三维视图占位面板交付（src/View3DPlaceholder.cpp——工作台壳 buildCentralArea 页 1 由泛化 QLabel 占位升级为 `ird_view3d_placeholder` 面板：三维视图标题＋"本阶段将在后续版本提供"说明＋UX-11 交互清单只读登记〔每行以只读属性 view3dId 携带契约 id，id 不进文案——UX-02〕；纯 QLabel 呈现零可交互业务控件——§4.2/§11.3 不虚构业务能力红线，零 Q_OBJECT 维持不开 AUTOMOC）；②视图契约登记公共头 `View3DContract.hpp`（§3.3 增行——本任务为该头首消费任务，v0.4"首消费冻结"机制落点）：UX-11 九项交互清单词表 `View3DInteractionItem`（id 词法＝点分段＋小写蛇形段，稳定序＝需求条目序，view3DInteractionManifest 供给）＋KIN-06/AT-04 会话姿态语义钉住 `View3DSessionPoseContract` 四位常量（sessionStateOnly=true／writesDesignModel=false／producesRevision=false／invalidatesResults=false——"钉住"＝机器可断言：阶段 B 实现任一位翻转即测试失败，语义变更必须走需求修订而非代码私改）；③测试落位：sdurws_ird_ui_test 增 View3DContractTest.cpp 三用例（清单九项逐项字面冻结＋id 词法/唯一性＋语义四位钉住——契约头互证锚），sdurws_ird_ui_gui_test 增 UI-V3D-1（§12.3 增行：占位呈现/清单同源对照/零按钮控件扫描/语义复核/上下文往返存活），19＋7 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t05/）；④门禁：ird_gates 与 wp10-t04 基线（32）逐码比对恰增 1 处——新实现 TU 的 R3 头包含面命中（View3DPlaceholder.cpp，F-259 同型机器面滞后；私有头仅 QWidget 前置声明不增命中），登记提交件 traceability/wp10-t05-gate-registrations.md（责任方 WP-01-T03），其余 32 处零变化；⑤O-31 处置（acceptance 3）：本任务交付物（占位面板＋契约头）零持有 C-3/4/5/7/8/10/11 对端类型——清单与语义均为 ui 自有词表/常量值，`NoCrossUnitInclude_O31_UI_BUILD` 对新增文件扫描零命中；三维渲染与 IRuntimeModelView 共同消费点（C-11）归阶段 B，届时按 §2.1.2 C-11 注入口径随增量修订复核。需求语义零变化：UX-11/KIN-06 承接语义按 REQUIREMENTS 原文与 §4.1/§4.2/§14.1 登记落位（交互实现仍归阶段 B——本版仅登记"占位＋契约"），仅登记契约"形状"与阶段 A 实现口径 |
| v0.8 | 2026-09-21 | WP-10-T06（≙UI-T06）CommandRegistry＋GlobalShortcutRegistry＋命令面板落位登记：①三公共头落位（§3.3 增注）——`UiTypes.hpp`（CommandId/TextKey/MenuPath 别名）、`ICommandRegistry.hpp`（§10.3 接口＋CommandDescriptor/UiContextSnapshot/CommandView/CommandAvailability/CommandOutcome/RegistrationResult＋createCommandRegistry 工厂与 CommandRegistryDeps）、`IGlobalShortcutRegistry.hpp`（§10.4 接口＋HotkeyBinding/HotkeyResult＋createGlobalShortcutRegistry 工厂与 GlobalShortcutRegistryDeps——attach/detach QShortcut 承载面）；②O-31 处置（acceptance 3，C-4 首消费冻结）：UiPorts.hpp 增 `IUiCommandGateway` 命令网关端口（L5 适配 ProjectCommandService::submit——唯一写路径经 project、ui 零业务判定语义零变化），UiProjections.hpp 增 `CommandEnvelopeProjection`/`CommandResultProjection`（字段语义逐条锚定 project.md §5.3.1——投影≠重定义；§10.3 CommandOutcome.revisionResult 原文 project::CommandResult 以投影承载，产品面零对端 include，`NoCrossUnitInclude_O31_UI_BUILD` 常驻自证对新增文件零命中）；③§10.3/§10.4 实现冻结增量（v0.4 §10 引导注机制）：ICommandRegistry 增 `seal()`（装配收口——§7.2"运行期只读"的执行机制，seal 后注册拒绝＝§10.3"非法"行）、`presentContext(UiContextSnapshot)`（§7.5 谓词求值上下文注入）、`query()/availability()/paletteSnapshot()/recentUsed()/restoreRecentUsed()`；CommandAvailability 增 `registered` 位（存在性与可见性正交——§10.4 UnknownCommand 前置校验数据源，UI-T03 ShellCommandAvailability.registered 同语义）、CommandView 增 `bindable` 位（§7.3 快捷键表前置校验数据源）；CommandHandler 签名以 `const std::vector<CommandParameter>&` 承载 §10.3 原文 std::span 形态（std::span 为 C++20 设施——C++17 钉死 D-01，契约语义零变化）；CommandScope＝ShellCommandScope 类型别名（UI-T03 冻结词表复用，NFR-MNT-03）；④命令 id 词形措辞差登记：§7.1 注记"点分小写"与表内冻结 id（workbench.commandPalette/view.displayMode/view.resetHome/view.resetZero/help.contents 段含大写）不一致——词形权威＝§7.1 冻结表原文（UI-T03 壳板已按表登记同批 id），校验落位为"点分段＋段字符 [A-Za-z0-9-]＋必含点"，段内大小写不强制（UiTypes.hpp CommandId 注释同步）；⑤§7.1 最小命令集登记（19 命令项——kMinimalCommandRows 逐行：id/titleKey/keywordKeys/category/scope/readOnlyAllowed/defaultShortcut/menuPath 严格按表列）＋§7.3 默认绑定表（Ctrl+N/Ctrl+O/Ctrl+S/Ctrl+Return/Ctrl+Z/Ctrl+Y/Ctrl+Shift+P/Ctrl+W/F1）；壳集成：UI-T03 ShellCommandBoard 登记职能由注册表取代并移除（WorkbenchGateState 由 UiContextSnapshot 取代）——菜单/顶栏/首页/快捷键/面板五入口统一收口 registry.submit（SA-16 唯一入口，N-11 无第二套）；workbench.commandPalette/view.resetLayout 壳自持处理器完整实现，其余归属命令（workflow/project/io/reporting）以占位说明处理器承接（§11.4 阶段 A 显式形态——触发有状态栏反馈，接线随归属任务）；快捷键 User 绑定与面板近期使用（§7.4 最近 20 条）经 UiSettingsWriter 用户级持久化（PM-14——"shortcuts"/"palette" 设置组）；⑥命令面板实现（src/CommandPalette.cpp＋CommandPalette_p.hpp 私有头）：注册表只读投影消费＋键盘导航（↑↓/Enter/Esc/Tab 补全类别）＋近期置顶分组渲染＋上限 50 提示＋"禁用＋说明"行——零 Q_OBJECT 维持不开 AUTOMOC（keyPressEvent/eventFilter 虚函数覆盖）；形态为主窗口浮动子面板（Qt::Popup 抓取语义在事件泵中不确定——GUI 测试实测后改定，§12.3 UI-PALETTE 行观测面）；§7.4 模糊匹配唯一实现点在 CommandRegistry::paletteSnapshot（子序列＋词边界前缀加权＋关键字加权，并列按注册序稳定——模型层可断言）；⑦测试落位：sdurws_ird_ui_test 增 CommandRegistryModelTest.cpp 七用例（UI-CMD-1/2/3 逐条＋提交语义/异常捕获/C-4 投影/§7.4 模糊规则/稳定序上限近期/seal 拒绝——真实 DiagCatalog＋DiagnosticsFactory＋StableCodeRegistry 全链，九码 P-UI-10 收编实测）＋ShortcutRegistryModelTest.cpp 四用例（UI-HKY-1 冲突拒绝＋UI-HOTKEY-CONFLICT 比较型字段「不适用」断言＋冲突流订阅/前置违约/UI-HKY-2 改绑解绑持久化回调/回放坏条目丢弃）；BuildRedLineTest 的 SA-16 扫描升级为"唯一注册点豁免面"口径（QShortcut＋全局作用域仅允许 src/GlobalShortcutRegistry.cpp——UI-HKY-3 正向控制）；sdurws_ird_ui_gui_test 增 CommandPaletteGuiTest.cpp 三用例（UI-PALETTE-1/2/3——§12.3 增行）；sdurws_ird_ui_gui_test 链接面增 Qt6::Test（§3.1 表增量）；30＋3＋10 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t06/）；⑧门禁：ird_gates 与 wp10-t05 基线（33）逐码比对恰增 6 处——全部为本任务新增产品面文件（两公共头/两实现 TU/面板 TU 对）的 R3 头包含面命中（F-259 同型机器面滞后），登记提交件 traceability/wp10-t06-gate-registrations.md（责任方 WP-01-T03），其余 33 处零变化；validate-task PASS。需求语义零变化：UX-13/PM-14/SA-16 承接语义按 §7 原文落位，仅登记接口"形状"与实现口径增量（§10.3/§10.4 文本按 O-31 首消费冻结机制随本版兑现） |
| v0.9 | 2026-09-21 | WP-10-T07 阶段 A（≙UI-T07 阶段 A）统一工程策略入口落位登记——阶段 A 交付摘要只读卡（编辑表单归阶段 B，单元卡 §13 UI-T07 行原文）：①右栏策略摘要只读卡交付（src/PolicySummaryCard.cpp 的 createPolicySummaryCard——buildSideDocks 右栏"诊断与设置区"由占位标签升级为真实卡 `ird_policy_summary_card`：分组异名行集只读渲染＋"修改策略…"入口〔点击＝kPolicyEditDeferredNotice 延期提示，§11.4 不虚构能力〕；壳持刷新钩子 m_refreshPolicyCard，initialize 与 presentProjectContext 后重拉端口快照——阶段 A 刷新锚点＝唯一上下文入口，事件驱动刷新随 §6.1 投影管线/UI-T09 接入；卡内容清空重建式刷新，行 objectName=ird_policy_row_<key> 稳定可定位；零 Q_OBJECT 维持不开 AUTOMOC）；②呈现契约登记公共头 `PolicySummaryCard.hpp`（§3.3 增行——本任务首消费，v0.4 §10 引导注机制落点）：分组异名词表 `PolicySummaryGroup`（PolicyAuthority"工程策略（计算权威）"/SessionDisplay"会话显示设置（不属策略字段）"——POL-ID-3 两名及影响范围说明的代码级单点）＋行模型 `PolicySummaryRow{key,group,label,value}`（key 词表十值——GUI 定位与测试断言锚，表尾追加式扩展）＋阈值显示换算纯函数 `formatPolicyThreshold`（core::convert 一次乘除——policy.md §10.6"显示单位经 core 换算投影，不改身份"，KIN-12 同案；量纲违约 fail-fast）＋`PolicySummaryDisplayUnits`（§4.6 出厂默认 m/rad——siDefaults；用户级显示单位偏好的设置承载随用户设置任务）＋`policySummaryRows` 行装配唯一权威（未装载占位/五类字段行/编辑路径说明/显示设置边界说明——模型层与 GUI 同源，View3DContract 同案）＋卡面文案常量单点（「显式不适用」/未装载/编辑路径 kPolicyEditPathNote"修改策略经命令端口提交生成新修订（权威归策略，ui 只读）"/POL-ID-3 边界说明）；③C-10 首消费细化（acceptance 1）：UiProjections.hpp `PolicySummaryProjection` 逐字段展开＋`PolicyThresholdProjection{present,siValue}`（§3.2 policy 行冻结基准——EngineeringPolicySet 公开字段直读投影：碰撞域启用＋启用域 token〔policy.md §4.3 冻结 token self/environment/tool/scene 直用不镜像〕/安全间距〔SI m〕/近限位比·条件数〔无量纲判定阈值族〕/行程上限〔SI rad〕/必检·过滤对计数；present=false＝「显式不适用」直读——P-POL-2/O-10 不发明数值；origin 字段不投影——对端来源词表编码 token 未随 codec 冻结，不制造第二权威，NFR-MNT-03）；UI-T03 计数形态（thresholdCount/explicitNotApplicableCount 数据成员）改派生 const 方法（逐字段为唯一事实，聚合零漂移；filterPairCount 保留并补 mandatoryPairCount 对偶）——UI-T03 冻结的聚合语义可复现（测试钉住）；④O-31 处置（acceptance 3）：C-10 摘要面经 ShellWiring.policySource（ui::IPolicySummarySource）承载——卡消费面只见 ui 自有端口与值投影，L5 适配 policy::IPolicyProvider（装配层任务）；C-4 命令面：编辑路径说明钉住"经①命令端口提交生成新修订"（IUiCommandGateway 已随 UI-T06 冻结，表单随阶段 B 消费该端口——本任务零新 C-4 面）；产品面零对 policy 链接/include（`NoCrossUnitInclude_O31_UI_BUILD` 对新增文件扫描零命中）；POL-ID-3 分组异名与 §6.7 摘要只读语义零变化；⑤测试落位：sdurws_ird_ui_test 增 PolicySummaryCardTest.cpp 十用例（分组异名/五类字段行/单位换算〔mm/deg/无量纲/恒等/量纲违约〕/显式不适用无数字/未装载占位/聚合派生/编辑路径说明/POL-ID-3 边界文案/行键稳定唯一——模型层纯函数面）；sdurws_ird_ui_gui_test 增 PolicySummaryCardGuiTest.cpp 五用例（UI-POL-1——§12.3 增行：装载投影分组行渲染与模型逐行同源＋端口消费计数〔替身计数＝注入承载证据〕/未装载占位/卡面只读零可编辑控件扫描＋唯一按钮＝修改策略入口/入口点击延期提示且零端口触达/上下文注入触发端口重拉行更新）；40＋3＋15 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t07/，集成＋冒烟双模式同口径）；⑥门禁：ird_gates 与 wp10-t06 基线（39）逐码比对恰增 1 处——新实现 TU（PolicySummaryCard.cpp）的 R3 头包含面命中（F-259 同型机器面滞后，R-3 例外范围同型），登记提交件 traceability/wp10-t07-gate-registrations.md（责任方 WP-01-T03；同件如实登记 gate-all 环境面 3 项既有失败——testkit_test 之 project/win32 既有 #ifdef TEST 命中、GUI ctest 无 Qt 环境、gate-all 冒烟步缺 Qt6_DIR 透传，均先于本任务存在）；validate-task PASS；冒烟模式按 AGENTS §4.1（vcpkg toolchain＋Qt6_DIR）配置构建通过。需求语义零变化：UX-08/ARC-05/POL-ID-3 承接语义按 §6.7/§4.6 与 REQUIREMENTS/policy.md §10.6 原文落位，仅登记投影"形状"与阶段 A 实现口径（编辑表单归阶段 B 维持 §13 原文，不构成收窄） |
| v1.0 | 2026-09-21 | WP-10-T08 阶段 B（≙UI-T08 阶段 B）参数表与表单公共件落位登记（本行即阶段 B 交付面，§13 UI-T08 行原文）：①公共契约头 `FormEditCommon.hpp` 落位（§3.3 增行）——`ParamEditModel` 编辑会话模型（基线/暂存/就地错误三层值语义：基线由域消费者经 setBaseline 注入〔应用回执后同步〕、暂存为解析成功的待应用值、就地错误只描述文本不触碰值——"保留原值"结构保证；UX-04/05/07 公共编辑规则唯一规则承载，零 Qt 模型层可测）：数值＋单位同显（displayValueText 合显＋displayNumberText/displayUnit 分列——KIN-12 显示投影经 core Units 唯一换算入口〔SA-12〕，setDisplayUnit/setDisplayUnitForKind 仅动投影制式、零脏零移交〔UX-07"仅改变会话显示的操作不弹出保存提示"〕）、非法输入就地显示原因保留原值（parseFieldValueText 六步判定：空/非数值〔from_chars 整串消费＋前导 '+'〕/非有限/整数约束/范围〔SI 闭区间〕/tryConvert 换算——原因文案含"（保留原值）"后缀与原文回显，kField* 契约文案单点）、表单级确认应用（UX-07 应用前确认交互的规则半区——presentErrors/无修改两前置拒绝〔就地反馈不弹窗〕，确认过的 `ParamEditSet`（SI 真值、注册序）经 ui 自有 `IFormEditOutlet` 移交域编辑器，移交后基线推进、表单与权威对齐；**断言与放行归 project 命令边界**（§9.2——ui 不持有判定权不自行放行），kApplyHandedOff 反馈文案明示）、批量粘贴（batchPasteText 逐行独立判定：TAB/逗号分隔、空行跳过、未知键/非法值拒绝不影响他行、同键后行覆盖；BatchPasteReport 逐行结果＋接受/拒绝计数＋影响明细〔本次涉及键的最终态 pendingChanges——"批量影响明细"验收点〕）、筛选（键/标签 ASCII 大小写不敏感子串，纯可见性裁剪）、错误定位（errors()/firstErrorKey 注册序首个——定位行为确定）、取消恢复（cancelRestore 丢弃暂存层，显示回基线；单位制式与筛选是会话显示设置不随取消回滚）；装配契约 fail-fast 面（makeQuantityFieldSpec 单位量纲一致性/空键空标签/无效 token、模型空集/重复键、未知键 out_of_range、非有限基线拒绝）；②`advancedPanelDefaultFields()` 高级面板字段集（UX-04 数值半区承载：solver-max-iterations/sampling-points/random-seed 三键冻结——计数类〔无量纲＋仅整数〕宿主约束，基线未设由域消费者注入，零默认数值 §11.4；"开发诊断"为会话级显示开关归面板选项不属数值字段）；③参数表面板 `src/ParamTablePanel.cpp`（createParamTablePanel 工厂——表单公共件第一渲染面，阶段 B 域消费者通用宿主）：四列表格 `ird_param_table`（参数｜值｜单位｜状态——值列就地编辑为唯一编辑形态〔UX-05 不用模态对话框〕，单位列与值列同显，状态列就地错误原因）、筛选行＋显示单位切换 `ird_param_unit_switch`（KIN-12 仅投影——activated 信号接线，候选＝R1 冻结 token 子集 m/cm/mm、rad/deg）、影响明细区 `ird_param_impact`（粘贴报告/应用反馈/错误提示就地呈现——零对话框）、非模态确认区 `ird_param_confirm_region`（应用前确认交互：kConfirmPromptPrefix＋逐项"标签：旧 → 新"比较型明细；确认/再改改双向可退，UX-07）、操作行（粘贴/应用〔未接出口禁用＋kNoOutletTooltip §11.4〕/取消恢复＋可选"开发诊断"开关 `ird_param_dev_diag`——只回调 options 不进模型）；零 Q_OBJECT 维持不开 AUTOMOC（itemChanged 单发定时器排队刷新防悬垂＋updating 守卫防程序 setText 误判）；非模态红线常驻自证＝FormEditModelTest 静态扫描（QDialog/exec(/setModal 注释剥离后零命中＋objectName 正向控制）＋GUI 控件扫描零 QDialog 双证；④O-31 处置（acceptance 2）：交付物零持有 C-3/4/5/7/8/10/11 对端类型——单位显示走已链接的 core Units 显示投影（ui→core 表内边），草稿接入经 ui 自有 IDraftController.attachModule 接口（IFormEditOutlet＝域编辑器实现的移交面，L5/域侧适配）；`NoCrossUnitInclude_O31_UI_BUILD` 对新增产品面文件扫描零命中；⑤测试落位：sdurws_ird_ui_test 增 FormEditModelTest.cpp 十六用例（同显与切换仅投影/未设占位不伪造 0/非法输入就地谱系〔非数值/空/非有限/非整数/越界〕/解析语法边界/确认应用主链路与移交载荷/错误阻断与修复后重放/批量粘贴独立行与影响明细/空行逗号容错/筛选/错误定位稳定性/取消恢复/非模态静态红线/高级面板字段集/装配契约 fail-fast/基线注入清暂存/合显格式化——§12.3 UI-FRM-1 模型半区）；sdurws_ird_ui_gui_test 增 ParamTablePanelGuiTest.cpp 十用例（§12.3 UI-FRM-1 呈现半区：结构与同显/就地错误保留原值/非模态确认区应用流与"断言与放行归 project"反馈/确认可反悔/取消恢复/批量粘贴影响明细/未接出口禁用/开发诊断开关不进编辑集/单位切换仅显示/筛选——§12.3 增行）；56＋3＋25 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t08/，集成＋冒烟双模式同口径——冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR，双模式构建零错误；集成全量构建零 error）；⑥门禁：ird_gates 与 wp10-t07 基线（40）逐码比对恰增 1 处——新实现 TU（ParamTablePanel.cpp）的 R3 头包含面命中（F-259 同型机器面滞后，R-3 例外范围同型；FormEditCommon.hpp 零命中——QWidget 仅前置声明不拖入 Widgets），登记提交件 traceability/wp10-t08-gate-registrations.md（责任方 WP-01-T03；gate-all 20/24 与 wp10-t07 同型——ird_gates 既有命中面/testkit_test 既有命中/GUI ctest 无 Qt 环境/冒烟步缺 Qt6_DIR 透传四项环境面既有失败如实登记）；validate-task PASS。需求语义零变化：UX-04/05/07/KIN-12 承接语义按 REQUIREMENTS 原文与 §4.2/§4.5/§4.6/§6.6/§14.1 登记落位，仅登记契约"形状"与阶段 B 实现口径（§14.1 承接行同步"公共件已交付、域消费者插件接入为余项"） |
| v1.1 | 2026-09-21 | WP-10-T09（≙UI-T09）阶段状态投影与工程用语落位登记（§13 UI-T09 行：StageNavigationModel＋StageStatusModel 汇聚＋UiText 体系）：①公共契约头两件落位（§3.3 增注）——`IStageNavigationModel.hpp`（§10.2 接口原文逐条兑现：StageView 六字段〔sevenState 承载 §6.3 冻结 token，UiStateToken 别名不另立词表——P-UI-1 词表未改一字〕＋NavigateResult 三态〔Allowed/Rejected/RejectedReadOnly〕＋IStageViewObserver pull-on-event 订阅＋stageIdSequence/stageToken 序与 token 唯一映射＋createStageNavigationModel 工厂与 StageNavigationModelDeps 装配依赖——五必注入项缺失 fail-fast）、`UiText.hpp`（§3.5/§6.6 唯一解析出口：resolveText 键解析＋位置参数替换纪律〔占位越界/参数冗余/摘要形态参数全 fail-fast〕＋formatQuantityText 数值带单位〔core displayValueIn 唯一换算入口，6 位有效数字 general 同 FormEditCommon 口径〕＋notApplicableText 不适用占位＋ensureNoInternalIdentity 哈希形态守卫〔64 连续十六进制拒绝——UX-02 呈现边界执行点〕＋registeredTextKeys 键盘点面＋内建中文过渡文案表 26 键〔stage.<id>.title 七键＋state.<token>.label 七/九态键＋「无法判定」原因两键＋呈现辅助键——键冻结值过渡承载，UI-T03 同案〕）；②值类型落位 UiTypes.hpp（§3.3）：StageId/StageViewStatus/UiStateToken＋DomainReadinessItem/StageReadinessSnapshot（§6.5 单侧冻结形状逐字段原文，epoch 标注——P-UI-6 处置：workflow.md 产出后核对，不兼容按影响面增量同步不私改对端）；③C-12 端口冻结（UiPorts.hpp 增注）：`IUiStageGate`（presentStage 呈现输出＋evaluate 消费 StageReadinessSnapshot 的导航评估）与 `IUiDomainReadinessSource`（§6.5 汇聚输入）——WP-22-T03 未产出前以桩承载（契约卡行"未产出→桩"），产品面零对端链接/include（O-31 处置：自身接口面＝ui/core 类型＋桩门控，真实数据源接入时按 v0.4 §10 引导注经自有端口复核）；④实现 TU 两件（src/UiText.cpp、src/StageNavigationModel.cpp——零 Qt 模型层，§6.4 落位登记注：六态合成口径＝门控四态透传＋view-only/in-progress 会话合成、只读导航 RejectedReadOnly 不经门控〔§6.4 补充规则"全部可点击查看"承载〕、拒绝不改 currentStage 无事件〔就地提示三要素随结果值〕；§6.5 落位登记注：汇聚只搬运、快照现取现拷注册序稳定、门控评估消费汇聚投影）；⑤StatusProjection 过渡标签值源迁移：statusWordTransitionalLabel/taskStateTransitionalLabel/currentnessUnevaluableTransitionalLabel 改经 UiText::resolve 解析（键不变、值逐字同源＝§6.3 原文，StatusWordModelTest 全数回归通过）；⑥测试落位：sdurws_ird_ui_test 增 StageNavigationModelTest.cpp 二十一用例（UI-STG-1：固定七项顺序＋门控四态注入矩阵＋blocked 附原因缺项与下一步建议〔UX-01 透传〕＋只读全阶段 ViewOnly＋in-progress 会话合成＋七态 token 经唯一求值点＋epoch 标注；UI-STG-2：允许→currentStage 更新＋观察者事件〔事件序＝先更新后通知〕＋拒绝→就地提示数据原样＋只读查看路径门控零咨询＋前置违约 fail-fast＋幂等导航＋退订幂等/RAII；§6.5：注册序汇聚逐字段透传＋门控收到快照实证〔§6.4 时序数据流〕＋缺门控装配 fail-fast〔N-11〕；UiText：七键标题解析＋替换纪律与哈希拒绝＋数值带单位与量纲违约＋不适用占位＋内建表零哈希/Schema/插件名断言〔UI-STG-1 零内部名〕＋过渡标签值源同源回归）；77＋3＋25 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t09/，集成＋冒烟双模式同口径——冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR；集成全量构建零 error）；⑦门禁：ird_gates 与 wp10-t08 基线（41）归一化逐码比对 HIT-SET-IDENTICAL 零变化、新增文件零命中（ird_gates-integration.log／ird_gates-smoke.log／gates-hit-set-diff.txt 同目录留痕；41 处均为 DTB §4.5 登记册既有项，目标失败＝机器面滞后既有状态与 wp10-t08 同型）；validate-task PASS。需求语义零变化：UX-01/UX-02/UX-12 承接语义按 REQUIREMENTS 原文与 §3.5/§6.4/§6.5/§6.6/§10.2 登记落位，仅登记投影"形状"与实现口径（§3.3/§3.5/§6.4/§6.5 落地登记注随本版兑现） |
| v1.2 | 2026-09-21 | WP-10-T10（≙UI-T10）帮助入口与关于对话框（插件清单）落位登记（§13 UI-T10 行：UI-PLG-2；版本与冻结基线一致）：①公共契约头 `AboutDialog.hpp` 落位（§3.3 增行——本任务首消费，v0.4 §10 引导注机制落点）：`pluginUiWhitelist()`（§11.1 八 token 编译期词表〔modeling/requirements/kinematics/trajectory/dynamics/selection/optimization/workflow——占位 token 先行登记〕的运行时只读载体，顺序即装配序与清单呈现序）、`PluginAssemblyReport`（§10.9 冻结值形状 {pluginId, ok, panelsLoaded, commandsRegistered, failureDiagnostics} 的承载——注册端口 IPluginUiRegistrar.hpp 随装配任务落位时该形状**迁移**至其自有头，NFR-MNT-03 单点）、`AboutVersionBaseline`（NFR-DEP-05 呈现值投影：available=false＝「未装载」占位、组件序＝注入序——ui 不改写不补默认）、`PluginAssemblyStatus` 三态词表（NotAssembled/Ok/Failed）、`aboutPluginRows`/`aboutVersionRows` 行装配唯一权威（清单规则＝白名单全集行〔行序＝词表序〕＋报告对位充实＋白名单外报告条目交集丢弃＋重复报告取首条确定性防御；版本区规则＝占位单行或产品行＋组件行、空值「不适用」占位〔ERR-01〕、空组件名 fail-fast——模型层与 GUI 同源互证，UI-PLG-2 数据源断言锚；PolicySummaryCard 同案）、`pluginTitleKey`/`pluginAssemblyStatusLabelKey` 键族（§3.5 UI-T10 冻结登记：plugin.<id>.title 八键＋plugin.assembly.<state>.label 三键——token 只作键词根，用户见中文名，UX-02 零内部插件名）、`userManualPath()`/`openUserManual()`（帮助入口执行半区——落位口径 `<appDir>/../share/ird/user-manual/index.html`〔share\ 帮助文件；实现口径登记〕；文件缺失不启动打开动作、返回 false 由调用方给用户可见反馈）、`createAboutDialog` 对话框工厂（版本区只读文本行＋插件清单五列只读表〔标题/版本/面板数/命令数/装配状态＝§11.4 原文列序〕；QDialog::open 窗口模态**非阻塞**打开——不用 exec()：§7.7 提交时序不可重入〔§3.4〕；QDialog/QWidget 仅前置声明不拖入 Widgets；零 Q_OBJECT 维持不开 AUTOMOC）；②关于框数据源端口 `IUiAboutDataSource` 落位 UiPorts.hpp（§3.3 增注）＋`ShellWiring` 增成员 `aboutSource`（§10.1 v1.2 增量冻结——**允许为空＝无关于数据测试场景，须显式声明**；为空时关于框照常打开＝清单占位＋版本「未装载」，零虚构）；③壳集成：帮助菜单由 UI-T03 禁用占位升级为两实条目（help.contents「帮助手册」F1＋help.about「关于」——§7.1 壳层命令集行），help.about/help.contents 转为壳自持处理器（§7.1 归属"ui"行兑现；菜单动作→submitShellCommand→registry.submit 统一路径不变——SA-16）；手册缺失反馈＝状态栏用户可见说明＋Dev 日志排障明文（含解析路径；§3.5 码表无此事件——不臆造稳定码，诊断目录零产出）；④O-31 处置（acceptance 2）：自身交付物零持有对端类型——版本数据经 IUiAboutDataSource 端口 L5 注入（WP-24-T01 基线产出前 available=false 承载）、插件清单＝静态白名单编译期词表（§11.1）、装配报告值形状为 ui 自有类型；产品面零对 project/evidence/execution/policy/runtime 链接/include（`NoCrossUnitInclude_O31_UI_BUILD` 常驻扫描对新增文件零命中）；⑤测试落位：sdurws_ird_ui_test 增 AboutDialogModelTest.cpp 九用例（白名单词表逐字冻结与唯一性/空报告白名单占位行集〔UI-PLG-2〕/交集充实与白名单外丢弃与失败证据保留〔UI-PLG-2〕/重复报告取首条确定性/标题键族解析与零 token 泄漏〔UX-02〕/版本行与注入基线逐字一致〔NFR-DEP-05〕/未装载单行占位零数字/空值占位与空组件名 fail-fast〔ERR-01〕/手册路径部署布局——模型层纯函数面）；sdurws_ird_ui_gui_test 增 AboutDialogGuiTest.cpp 三用例（UI-PLG-2/UI-HELP-1——§12.3 增行 UI-HELP-1：帮助命令路径＋版本区/清单格与模型输出逐字同源＋端口消费计数〔替身计数＝注入承载证据〕/无数据源占位零虚构/手册缺失状态栏反馈＋Dev 留痕＋诊断目录零产出）；86＋3＋28 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t10/，集成＋冒烟双模式同口径——冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR，双模式构建零错误；集成全量构建零 error）；⑥门禁：ird_gates 与 base（f6968386＝wp10-t09 合入后）同引擎 detached worktree 基线（41）归一化逐码比对恰增 1 处——新实现 TU（AboutDialog.cpp）的 R3 头包含面命中（F-259 同型机器面滞后，R-3 例外范围同型；AboutDialog.hpp 零命中——QDialog/QWidget 仅前置声明），登记提交件 traceability/wp10-t10-gate-registrations.md（责任方 WP-01-T03），其余 41 处零变化（ird_gates-integration.log／ird_gates-smoke.log／gates-hit-set.txt／gates-hit-set-diff.txt 同目录留痕，冒烟与集成命中集一致）；validate-task PASS；§2.2 UX-14 行承接位置由 §11.5 修正为 §11.4（笔误——§11.4 即本节标题与 §15 矩阵既有指向，非语义变更）。需求语义零变化：UX-14/NFR-DEP-05 承接语义按 REQUIREMENTS 原文与 §7.1/§10.9/§11.1/§11.4 登记落位，仅登记契约"形状"与实现口径 |
| v1.3 | 2026-09-21 | WP-10-T11（≙UI-T11）UiSessionController＋关闭/切换对话框机制落位登记（§13 UI-T11 行＋§13 UI-T11 落地登记注；需求 PM-03/PM-07）：①公共契约头 `UiSessionController.hpp` 落位（§3.3 增行）＋实现 TU `src/UiSessionController.cpp`（零 Qt 模型层——§12.1 第一层可测）：§5.2 七态状态机唯一执行者（状态迁移表见头注释；Closed 瞬态立即转 NoProject，审计经 closeCompletedCount）＋§6.2 epoch 递增（打开成功/关闭完成/切换绑定新项目——切换绑定恰一次；后台持有点排空不递增〔不改变当前会话身份轴，迟到事件路由语义 §6.2 三分支维持〕）＋INV-SES-1~4 实现落位（INV-SES-1＝isReadOnlySession 唯一读出点只读打开结果直写位；INV-SES-2＝B 进 Open* 唯一路径前置＝A 已移出当前绑定转入持有点＋openProject 重入拒绝；INV-SES-3＝DrainingHold shared 保活直到 subscribeClose 回调/轮询后释放；INV-SES-4＝Draining 全程有界——防线 3→4 串联总预算＝T_force2）；②统一确认对话框机制（§5.4 S1/S2，PM-03）：`assembleCloseDialogData`（数据装配唯一实现点——纯函数：草稿区＝IUiDraftQueryPort 清单直通＋sessionDirty 独立事实位〔不虚构磁盘行〕、任务区＝IUiSessionTaskPort 非终态行直通、saveDraftsAvailable＝writable〔§5.5 呈现半区〕、noActiveTasks 派生）、`beginClose`/`beginSwitch`（呈现不改状态＋单槽纪律）、`resolveCloseDialog`（[取消]回原状态／[保存]→saveAllDraftsManual 接线点〔→IDraftController::saveAll(Manual)，UI-T12 落位后接线〕失败＝SaveFailed 关闭中止〔不静默丢草稿〕／[放弃]清会话脏〔磁盘行保留〕／[协作取消]重查集逐任务 requestCancel〔NFR-PERF-02 ui 侧只发请求〕／[等待]进 Draining 保持进度可见；切换流后段＝候选验证"成功才切"——失败 CandidateRejected＋A 会话不变〔PM-03〕，成功则 A 发 requestClose 入持有点＋B 绑定 epoch++）；③§5.6 四级防线（INV-SES-4）：`pollDrain` 轮询驱动（closed() 兜底轮询→持有点释放→T_force 到点 ForceConfirmDue〔呈现一次〕→T_force2 到点 GivenUp 自动放弃〔防线 3 未确认时补行强杀序列——"数据损失限于未归档结果"的前提承载；UI-SESSION-CONTEXT-INVALID 目录 Warning＋Dev 日志双通道〕）＋`forceCloseDialogData`（waitedText 内部计时渲染单位显式）＋`resolveForceCloseDialog`（确认＝重查集逐任务 requestForceTerminate＋forceAbandonAll 接线点〔→L5 abandonAll，§11.5 分工〕＋abandon 标记；拒绝＝继续等待不重复弹确认）＋`cancelDrainWait`（尚未 abandon 恢复显示——§5.2 状态表原文；abandon 后拒绝）；**P-UI-8 处置维持（acceptance 3）**：T_force/T_force2 按 §5.6 默认保守值 120 s/300 s 经 UiSessionControllerDeps 装配期可配（默认值冻结断言＋可配触发面双测试钉住），阈值裁决未闭合不私定终值；④迟到写提示（§5.3 行 4/§5.7，UI-SES-7/UI-LCY-1）：`makeContextInvalidNotice`＝提示值零动作位/零待写入口（"不重试、不缓存待写"由数据形状保证）＋诊断双通道出线（码表 Warning＋Dev 明文）；⑤C-3/5/8 会话端口首消费冻结（§3.3 UiPorts.hpp 增注——O-31 注入面，§10.1 注释行"实例随打开流程注入"落位）：`IUiStoreFactoryPort`（open＝五步协议入口，SessionPortBundle 出参——失败零绑定泄漏）、`IUiProjectStorePort`＋`IUiStoreCloseObserver`（requestClose 在途引用计数/closed 兜底轮询面/subscribeClose RAII 订阅——§5.1 分离原则 shared 保活语义见端口注释）、`IUiDraftQueryPort`（只读清单——草稿写面归 §8.1 DraftController 分工红线）、`IUiSessionTaskPort`（非终态清单/requestCancel/requestForceTerminate）；会话投影族落位 UiProjections.hpp（§3.3 增注：OpenStoreOutcome/OpenedProjectFacts〔INV-SES-1 唯一数据源〕/LockHolderProjection/ReadOnlyBannerProjection＋assembleReadOnlyBanner〔§5.3 显示差异唯一实现点——actionKind 逐场景词表〕/DraftRowProjection/TaskRowProjection/CloseDialogData/CloseDecision/CloseDialogResolution/ForceCloseDialogData/ContextInvalidNotice）；会话词表落位 UiTypes.hpp（§3.3 增注：UiSessionState 七态/UiOpenMode〔project::OpenMode 语义锚〕/UiCloseIntent）；⑥测试落位：sdurws_ird_ui_test 增 SessionControllerModelTest.cpp 十九用例（对话框装配与五分支／横幅含 PID 与 actionKind 区分〔UI-SES-2/3〕／打开失败错误页〔§5.3 行 6〕／等待覆盖在途归档＋epoch＋退出就绪〔UI-SES-5〕／切换绑定与持有点〔UI-SES-4 机制半区〕／候选拒绝／状态矩阵＋INV-SES-1／调用面契约＋INV-SES-2／INV-SES-3 回调释放／防线 3 确认链〔UI-SES-6〕／防线 4 放弃／P-UI-8 默认值冻结／缺接线 fail-fast／迟到写提示〔UI-LCY-1〕／[放弃]磁盘保留／构造 fail-fast）；sdurws_ird_ui_contract_test 增 SessionContractTest.cpp 六用例（契约 verify 行承接——§12.1 第二层：C-3 关闭协议计数/时序/回调恰一次、C-8 逐任务寻址＋常规路径零强杀、SA-17 切换保活〔weak 可锁定→释放后 expired〕、§9.5 退出就绪位、P-UI-8 装配可配触发面、O-31 虚派发结构自证），106＋9＋28 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t11/，集成＋冒烟双模式同口径——冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR，双模式构建零错误；集成全量构建零 error）；⑦门禁：ird_gates 与 base（63e30071＝UI-T10 合入后）同引擎 detached worktree 基线归一化逐码比对**零变化**（新增产品面文件零 Qt 头包含——防线/对话框机制全模型层，R-3 例外面零扩大；ird_gates-integration.log／ird_gates-base.log／ird_gates-smoke.log／gates-hit-set.txt／gates-hit-set-diff.txt 同目录留痕，冒烟与集成命中集一致）；validate-task PASS；O-31 处置（acceptance 3）：`NoCrossUnitInclude_O31_UI_BUILD` 对新增文件扫描零命中（BuildRedLineTest 回归通过），产品面零对 project/evidence/execution/policy/runtime 链接/include，INV-SES 会话不变量语义零变化。需求语义零变化：PM-03/PM-07/TASK-03/SA-17 承接语义按 REQUIREMENTS 与 §5/§6.2 原文落位，仅登记接口"形状"与实现口径（对话框 GUI 呈现面归 §12.1 第三层——§12.3 用例扩展随消费任务） |
| v1.4 | 2026-09-21 | WP-10-T12（≙UI-T12）DraftController＋草稿写半区端口落位登记（§13 UI-T12 行＋§13 UI-T12 落位登记注＋§10.5 实现冻结登记注；需求 PM-04/PM-08/PM-11/PM-15/RV-10/PM-18）：①公共契约头 `IDraftController.hpp` 落位（§3.3 增行）＋实现 TU `src/DraftController.cpp`（零 Qt 模型层——§12.1 第一层可测）：§8.2/§8.4 保存编排（收集脏模块→域源组文档→ui 串行落盘执行器调用写半区端口→完成回执 Marshal 回 UI 线程；脏代次守卫＝"save 期间的新脏数据进入下一周期"；Manual/CloseDialog 同步完成＝§8.4 关闭对话语境，等待≠执行 IO）、§8.3 打开恢复编排＋recoveryBanner 恢复横幅值装配（孤儿计数呈现——UX-02）、§8.5 StaleRevisionRejected 呈现（草稿保留＋staleConflictData 冲突对话框数据＋reEditOnCurrentRevision；不自动重试提交由"控制器无命令网关"结构保证）、§8.7 草稿局部撤销栈（应用成功清栈＝"局部撤销不能撤销已应用修订"）；②C-5 写半区端口首消费冻结（§3.3 UiPorts.hpp 增注，O-31 注入面）：`IUiDraftStorePort`（save/tryLoad/discard——语义锚 project.md §5.4）；③草稿投影族落位 UiProjections.hpp（§3.3 增注）：DraftDocumentProjection/DraftOrigin〔§4.4.5 冻结三值 token〕/DraftSaveOutcome/DraftLoadOutcome/DraftDiscardOutcome＋StaleRevisionDetailProjection（随 CommandResultProjection.staleDetail 承载——§8.5"数据由 CommandResult 附带"）＋DraftRowProjection 增列 moduleId/stale/baseRevisionCanonical（既有字段零变化）；④`UiSessionController` 增 reportSessionDirty 生产者接线点（v1.3 既定缺口闭合——`*` 判定位会话半区经 DraftControllerDeps.onSessionDirtyChanged 驱动上下文重呈现；m_sessionDirty 其余写点语义零变化）；⑤测试落位：sdurws_ird_ui_test 增 DraftControllerModelTest.cpp 十三用例＋sdurws_ird_ui_contract_test 增 DraftContractTest.cpp 三用例（写半区端口与 DraftService 语义锚闭环＋真后台落盘线程零 UI 线程落盘＋O-31 虚派发——§12.1 第二层），119＋12＋27/28 全通过（gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t12/，集成＋冒烟双模式同口径；GUI 1 项环境失败如实登记——BatchPasteImpactDetail_UI_T08_ACC1 宿主剪贴板占用 OpenClipboard 0x800401D0 三轮一致，UI-T08 既有面与本任务零耦合，testkit §7.5 不记为通过）；⑥门禁：ird_gates 与 base（e071a0a7＝UI-T11 合入后）逐码比对零变化（LIB×8/R-1×1/R-3×33/R-4×3/R-5×1/SUB×39/T-1×5/T-2×1 两侧一致、目标集合 diff 空；新增产品面文件零 Qt 头包含零表外边），ird_gates-integration/smoke 日志＋hit-set/diff 同目录留痕；validate-task PASS；O-31 处置（acceptance 3）：NoCrossUnitInclude_O31_UI_BUILD 对新增文件扫描零命中，产品面零对 project/evidence/execution/policy/runtime 链接/include，RV-10/§8.7 两栈边界语义零变化。需求语义零变化：PM-04/PM-08/PM-11/PM-15/RV-10/PM-18 承接语义按 §8/§12.3 原文落位，仅登记接口"形状"与实现口径 |
| v1.5 | 2026-09-21 | WP-10-T13（≙UI-T13）诊断/确认/任务呈现模型与 CommandInteractionBridge 落位登记（§13 UI-T13 行＋§13 UI-T13 落位登记注＋§3.3 头布局三行增注；需求 UX-03/ERR-01/PM-15/TASK-01/02/03；knownPitfalls O-31〔已裁决随整链放行〕/P-PR-7/P-UI-4/P-UI-7 处置见 §13 登记注）：①三公共契约头落位＋三实现 TU（详见 §13 登记注①②③）——IDiagnosticPresentationModel.hpp（§10.8 五方法＋§9.1 纯函数装配唯一权威；expandChain 链查询缝 chainOf **单侧冻结**〔diagnostics §9.7 投影不携带 causedBy——缝由 L5 经 CauseLink::chainOf 供给，未注入退化"自身即根"零虚构；吸收点＝投影扩展链字段〕）、ITaskPresentationModel.hpp（§10.7 接口＋进度三规则/归档子标/行动作/当前性徽标纯函数唯一权威；后台只读自限 P-UI-7；强杀独立确认；即发即忘）、CommandInteractionBridge.hpp（C-6 外翻——实现 ui 自有 IUiCommandInteraction，形状逐字镜像 project.md §5.3.3 冻结点；§9.2 四步线程模型＋isAlive 探针＋P-UI-4 principal 会话缓存＋P-DIAG-6 无超时；§9.3 装配纯函数码表键推导＋补全双层＋Dev 折叠区零哈希）；②C-6/C-8/C-9 呈现面端口首消费冻结（§3.3 UiPorts.hpp 增注，O-31 注入面）：IUiCommandInteraction/IUiTaskPresentationPort/IUiFindingQueryPort/IUiUserLogSource/IUiRedactionPolicyPort（v1.3 冻结的 IUiSessionTaskPort 三原语面保持不动——分立端口）；任务投影族落位 UiProjections.hpp（TaskProgressProjection/TaskViewProjection/UiTaskAck/UserLogEntry——execution ProgressReport/TaskSnapshot/TaskCapability/StatusAck 语义锚，投影≠重定义）；③UiText 键族⑧ 32 键登记（§3.3 增注）；④测试落位：sdurws_ird_ui_test 增 DiagPresentationModelTest/TaskPresentationModelTest/CommandInteractionBridgeTest 共 31 用例（151/151），sdurws_ird_ui_contract_test 增 InteractionTaskContractTest.cpp 六用例〔契约 verify 行承接——§12.1 第二层：O-31 虚派发结构自证/Marshal UI 线程契约/C-8 到达序与轻查询半区/即发即忘/C-9 装配缝投影〕（17/17），GUI 回归 27/28（1 项既有环境失败＝UI-T12 已登记剪贴板占用，与本任务零耦合）；gtest XML＋ird-test-report.json 留痕 traceability/builds/wp10-t13/，集成＋冒烟双模式同口径（冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR），双模式构建零 error；⑤门禁：ird_gates 与 base（7b62d221＝UI-T12 合入后）同引擎 detached worktree 基线归一化逐码比对恰增 4 处 R3（其余七码零变化、目标集合 diff 空——+4＝2 个新实现 TU 的 Marshal Qt 面，F-259 同型机器面滞后），登记提交件 traceability/wp10-t13-gate-registrations.md（责任方 WP-01-T03）；validate-task PASS；O-31 处置（acceptance 5）：NoCrossUnitInclude_O31_UI_BUILD 对新增文件扫描零命中，产品面零对 project/evidence/execution/policy/runtime 链接/include；sdurws_ird_testkit_qt 首消费者理由随本契约 acceptance 4 登记（§12.1 原文——WP-10-T11 契约测试需 Qt Test 辅助，testkit 红线不进生产计算库依赖维持）。需求语义零变化：UX-03/ERR-01/PM-15/TASK-01/02/03 承接语义按 REQUIREMENTS 与 §9/§10.7/§10.8 原文落位，仅登记接口"形状"与实现口径（§10.7/§10.8 原文对端类型按 O-31 机制以 ui 投影承载，随本版增量修订登记） |
| v1.7 | 2026-09-22 | WP-10-T15（≙UI-T15）工作台验证 harness `sdurws_ird_ui_app` 落位登记（§13 UI-T15 行＋§13 UI-T15 落位登记注）：开发期 L5 应用壳雏形（建模开发期间可视化验证手段，所有者直接指派）——app/PortAdapters 端口适配器族（StoreFactoryPortAdapter/StorePortAdapter/ProjectDiagnosticsBridge：对端契约原样翻译零吞错）＋v0.1 诚实桩（策略未装载/名称恒不解析/草稿任务恒空——零虚构）＋HarnessMain 装配序列（诊断栈两段式 seal＋ShellWiring 必填四端口＋UiSessionController 打开编排）；`_app` 后缀形态首次启用（ird_gates 分类面既有词）；ird_gates 与 base 比对恰增 2 条 SUB（ui->project 装配层特权边＋ui->ui 自边，登记 traceability/wp10-t15-gate-registrations.md）；ui ctest 回归 ird 2/2＋ird_gui 28/28；集成构建零 error；harness 交互验证路径与截图/日志留痕 traceability/builds/wp10-t15/。需求语义零变化：零产品库行为变化（详见 §13 UI-T15 登记注） |
| v1.8 | 2026-09-22 | WP-10-T15 返工登记（验收 attempt 1 fail——阻断项 B-1：PM-07 第二写者双开第二实例 std::terminate〔0xC0000409〕＋在库双开留痕场景误配）：①ProjectDiagnosticsBridge::report 补齐 create 上下文宿主标识（sourceUnit="project"/sourceInterface="sink.report"——§9.7 DiagnosticsSinkImpl 同款；params 保持空零虚构）并对 DiagnosticsError 建立两分组显式纪律——装配/桥自身缺陷（CodeUnknown/Usage）上抛 fail-fast，对端记录契约缺口（CodeDeprecated/SubjectMissing/ComparisonMissing/ParamSchemaMismatch/ContextMissing——已知面：PRJ-LOCK-HELD 项目级事件 subject=∅ 与用户级码 subject 强制的集成面冲突，发射面/工厂豁免裁决归 project/diagnostics 所有者〔F-290 候选〕）经 Dev 通道具名落盘（channel ird.harness.diagbridge）后协议继续——PM-07 降级只读是 open 成功形态；②HarnessMain 打开编排 try/catch 兜底（残存异常→消息框＋exit 5 可观测错误页，杜绝进程死亡）；③v0.1 诚实边界增补：PRJ-* 项目级事件条目在缺口裁决前不入诊断目录，PM-07 横幅数据源＝打开结果自带锁事实（readOnlyCause/lockHolder），与诊断目录两通道独立；④双可写意图实例复测 PM-07（第二实例存活＋"可写：否"横幅）＋留痕刷新与 F-288 缺口补齐（gtest XML＋ird-test-report.json＋集成构建日志＋validate-task.log；ctest 日志按契约 note 文件名落位消解命名漂移）。需求语义零变化：零产品库行为变化（变更面仅 ui/app/ 与单元卡登记——详见 §13 UI-T15 返工登记注） |
| v1.9 | 2026-09-23 | UI-T16 立项登记（O-38 裁决承接，DTB v0.17 §4.2/§2.11/§5.1 同日登记）：§13 新增 UI-T16 行＋立项登记注——工作台宿主插件 `sdurws_ird_ui_plugin`（开发期 RobWorkStudio 集成入口＋动态加载验证通道）：rws::RobWorkStudioPlugin 派生薄插件、零计算逻辑；壳拆内容装配层/顶层窗口宿主层两层（harness 与插件共用，harness 回归零变化为硬验收项，IWorkbenchShell 契约面增量随实现落位登记）；宿主模式 RWStudioView3D 共存最小接入（完整三维交互仍归 UI-T05 阶段 B）；禁止顶层 QMainWindow 嵌套宿主 Dock；AUTOMOC 仅插件目标开启；SA-01 产品静态白名单与 WP-24-T03 正式装配口径不变（动态加载仅限开发期）。需求语义零变化 |

> **UI-T14 落位登记（v1.6，2026-09-22）**：本行已交付——ui 契约测试套件（§12.3 承接族全用例具名落位＋ctest 双标签注册＋ird-test-report.json 留痕＋UI-EXEC-1 执行限制登记＋O-31 测试面直链落地）：①**O-31 处置（acceptance 3，已裁决 2026-09-19 随整链放行）**：§3.1 contract_test 目标行 v0.4 原文"project/execution 直链随 UI-T14 契约测试套件落地登记"兑现——`sdurws_ird_ui_contract_test` 落位直链 `sdurws_ird_project`/`sdurws_ird_execution`/`sdurws_ird_diagnostics`＋testkit（F-228 一并消解；**产品面守卫不变**：`sdurws_ird_ui` 链接块仍仅 core/diagnostics＋Qt 三件套——配置期守卫＋LinkageContractTest 链接块文本钉＋BuildRedLineTest include 面扫描三道防线常驻）；契约测试以 testkit FaultInterceptor 伪造对端接口、经 ui 自有端口适配桥接，对端类型形状漂移由测试面捕获（override＋字段访问＋成员函数指针 static_assert 三重编译期钉）；②**契约测试增两 TU**：PeerBridgeContractTest.cpp 四用例（测试面直链登记钉〔链接块含五库＋产品块零对端〕／C-6 外翻桥接〔InteractionL5Adapter 实现 project::ICommandInteraction 委托经 FaultInterceptor&lt;ui::IUiCommandInteraction&gt;——故障计划命中注入 nullopt 失效路径、未命中透传凭据一一对应；双侧 requestConfirmations 签名 static_assert 镜像钉——P-PR-7〕／ITaskScheduler 伪造＋FaultInterceptor 注入→SchedulerPresentationAdapter 值投影桥〔TaskSnapshot→TaskViewProjection 逐字段映射＋run→TaskId 寻址侧表；对端 ExecutionError 折叠空投影＝端口"纯查询不抛"契约〕／diagnostics::RedactionPolicy 值面直用钉〔IUiRedactionPolicyPort 方法签名 static_assert——表内登记边值面〕）、Section12RegistryTest.cpp 一用例（§12.3 承接族逐例注册审计：WB/SES/DRF/CMD/HKY/STG/TSK/PLG/DIA/PERF/LCY 十一族全部用例编号＋UI-EXEC-1——测试源码语料归一化〔下划线→连字符〕存在性审计、自身排除防恒真、缺失全量列示；V3D/PALETTE/POL/FRM/HELP 族由 UI-T05/06/07/08/10 已合入文件承载——其 §12.3 行"同源互证"观测点即指向对应文件——不属本契约承接族清单）；③**模型测试增三具名用例**：PluginAssemblyFailureDegradationIsolatesRow_UI_PLG_1（§11.3 失败降级隔离：失败行"装配失败"占位＋UI-PLUGIN-ASSEMBLY-FAILED 证据保留、成功行照常充实、未命中行照常占位、行集恒等白名单）、QueuePositionWordingOnlyWithoutNumbers_UI_EXEC_1（UI-EXEC-1 具名化："排队中（等待资源）"文案零数字字符＋行与轮询面零虚构进度——不验证数值不虚构数据源）、UiThreadHeartbeatUnderSlowPeerQuery_UI_PERF_1（NFR-PERF-01/ARCH §4.2：慢对端 >1 s 场景下事件循环心跳探针无 >200 ms 间隙＋单次刷新 <50 ms＋后台结果经排队 Marshal 落地；§12.2 判据纪律——判定＝心跳时间戳与刷新耗时，1.1 s 限时等待为场景构件非判据）；④**落位结果**：sdurws_ird_ui_test 154/154、sdurws_ird_ui_contract_test 22/22、GUI 回归 27/28（1 项既有环境失败＝v1.4 已登记 BatchPasteImpactDetail_UI_T08_ACC1 剪贴板占用 0x800401D0，本轮连续五次复现一致，与本任务零耦合，testkit §7.5 不记为通过）；ctest 标签双命令在 ui 叶子目录执行（`^ird$` 2/2 通过、`^ird_gui$` 1 例 exe 级失败＝同一环境例——`--test-dir build` 根空转系 F-251 已登记框架 CTestTestfile 链缺失，非本任务引入，叶子目录口径沿 RPT-T11 先例）；ird-test-report.json 五件＋gtest XML＋控制台日志留痕 traceability/builds/wp10-t14/，集成＋冒烟双模式同口径（冒烟按 AGENTS §4.1 vcpkg toolchain＋Qt6_DIR），双模式构建零 error；⑤**门禁**：ird_gates 集成/冒烟同面，与 base（11534184449b295252f346e460ced8306d935d0a）按码比对七码零变化（LIB×8/R1×1/R3×37/R4×3/R5×1/T1×5/T2×1），SUB 39→45 原始命中＝恰增 3 条唯一命中（ui_contract_test 直链 project/execution/diagnostics——O-31 裁决测试面既定形态非违约增边；MSBuild 命令回显使每条命中在日志出现两次），登记提交件 traceability/wp10-t14-gate-registrations.md（责任方 WP-01-T03）；validate-task PASS。需求语义零变化：UX-03/09/10/13 承接语义按 §12.3 原文落位，本任务零产品行为变化（变更面＝测试面＋契约测试目标的测试侧链接面，产品库本体零改动） |
