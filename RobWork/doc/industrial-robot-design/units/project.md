# 工业机械臂设计软件 · project 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-09 |
| 状态 | **`Draft`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试或正式验收） |
| 文档代号 | UNIT-PROJECT |
| 单元 | project（平台服务，L3；ARCHITECTURE §2.3/§3.1：.rwdesign 存储、修订/分支/事务、草稿、命令服务〔含可确认诊断流〕、重关联、schema 升级） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md` **v0.1（`Draft`，未冻结）**、`units/testkit.md` **v0.1（`Draft`，未冻结）**——本文消费的 core 公共契约以 core.md v0.1 签名为基线并逐项标注状态（§3.2）；core/testkit 冻结版本如有变更，本文按影响面增量同步（待裁决 P-PR-1） |
| 上游下游链位置 | ARCHITECTURE §11.1：`DETAILED-DESIGN.md`（已建立）→ `units/*.md`（单元任务卡）。本文即 `units/project.md`，按任务卡深度编写（接口签名、数据类型在本文件冻结） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/`（骨架已建：目标 `sdurws_ird_project`〔INTERFACE 占位〕＋别名 `RWS::ird::project`＋公共头保留位 `include/sdurws/ird/project/README.md`；README 已引用本文 §9，与本文结构一致） |
| 任务包 | WP-04（持久化/草稿/撤销/恢复平台原语；ARC-01 主WP；REQUIREMENTS §3 阶段 A〔WP-00～12〕；PM-04/08/12/18 主WP） |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 历史实现仅功能范围对照且**当前磁盘缺失**（实测；core.md §1.2 R-5、testkit.md §1.2 同源登记）；历史实现不作为语义来源 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 project 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 project 的职责（§3.1 单元总表行；§6 数据视图；§6.4 事务协议；§6.8 写锁与存储上下文；§7.1 命令与可确认诊断放行），把 `.rwdesign` 持久化模型与格式契约、公共接口、命令/确认/撤销/重做机制、事务发布与崩溃恢复、草稿与项目生命周期、写锁与存储上下文生命周期、跨单元协作与归档协议、验证方案与阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` 条目为自足定义，本文不重定义、不收窄、不扩大；本文引用的需求 ID 与章节号均指 v1.16 当前正文。架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-03/SA-09/SA-15/SA-17 直接约束本单元）；本文对其含混或未登记事项的解释集中登记于 §15.3（P-PR-1～P-PR-8），不私自修改上游。

### 1.2 上游与磁盘现状登记（2026-09-09 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：ARC-01/02、CON-01/03/04、TASK-03、PM-01～09/12/13/18、NFR-REL-01/04、NFR-SEC-01（消费侧）、NFR-DEP-04、§17 表注（`.rwdesign` 唯一规范格式、磁盘写入唯一归属 project） |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§3.5 依赖表登记 project→core,diagnostics；execution/reporting→project；§6.1～6.8、§7.1、S1/S5/S7 走查直接约束本文。若评审产生 A9+ 处置按影响面同步（P-PR-2） |
| `units/core.md` | 存在，v0.1，`Draft`（**未冻结**） | 协作输入。本文消费其身份/摘要/诊断/事件契约（§3.2 逐项登记状态）；其 §10.3 已向 project 交接身份分配时机、修订记录结构、确认记录编码、对象 canonical 编码、事件发布时序——本文 §4/§5/§6 即该交接的承接答复 |
| `units/testkit.md` | 存在，v0.1，`Draft`（**未冻结**） | 协作输入。本文验证方案（§11）消费其 FaultInterceptor/TempDir/DeterministicEnv/契约断言；其 §10.2 已登记 project 侧义务（保存事务故障场景编写、AT-13 场景逻辑） |
| `DETAILED-DESIGN.md` | **不存在** | ARCHITECTURE §11.1 列为"后续产出"。本文按任务卡深度自足编写，不虚构其内容（core.md/testkit.md 同源登记） |
| `development-task-breakdown.md` | **不存在** | 同上；其 §5.1（构建约定）被骨架 CMakeLists 头注释引用。本文 §12 使用 project 局部任务编号 `PRJ-Txx`，不预填 WP 分配、不重排上游 WP 编号 |
| `units/project.md` | **不存在**（本文新建） | 其余 17 个单元任务卡均未产出；evidence/runtime/policy/execution/diagnostics/io/ui/workflow 详设缺失——本文需要其接口处给最小依赖契约并登记交接（§13），不代写对方详设 |
| 构建骨架 | 存在：`industrialrobot/CMakeLists.txt`＋20 单元目录＋`patches/`（2 文件），共 23 文件（实测一致） | `sdurws_ird_project` 为 INTERFACE 占位，无源码；`_test`/`_contract_test` 目标按任务卡逐个登记、不预建空目标 |
| 构建缓存 | `build/CMakeCache.txt` 实测：Visual Studio 17 2022（MSVC x64）、Qt 6.11.1（D:/software/QT/6.11.1/msvc2022_64） | 与 core.md §1.4/testkit.md §1.4 同源事实；Windows 文件 API 按 §7.2 逐一对照官方文档口径设计 |
| `old/` | **不存在于磁盘**（仓库根实测仅 `.git/.gitignore/RobWork/build/vcpkg/…`；git 亦未跟踪） | 与 REQUIREMENTS v1.10 声明不符（core.md R-5 已登记）；从头构建口径不受影响，本文不引用其任何机制 |

### 1.3 设计目标

1. **唯一写路径与唯一提交点**：一切持久化写入经 project 存储端口；一切修订产生经 ProjectCommandService；单一提交根＝HEAD（ARCH §6.4 A2）。不存在第二写路径、第二权威元数据或旁路事务（自审项 A-1/A-2）。
2. **崩溃一致性**：任一提交边界失败或崩溃，已提交修订与 HEAD 字节不变；未提交内容启动时忽略并给恢复诊断（NFR-REL-01、PM-08、AT-11/13）。"目录中存在"不等于"已提交"（自审项 A-6）。
3. **不可变历史**：修订只增不改；撤销/重做＝新修订（PM-18）；旧结果保留为原快照历史证据（CON-02）——归档不因 HEAD 前进而拒绝合法历史结果（当前性归 evidence，§10.1）。
4. **权限即锁**：写权限唯一依据＝OS 排他句柄（ARCH §6.8 SA-17）；PID/心跳仅诊断；失权写拒绝；存储上下文与界面会话分离。
5. **平台服务不吞业务**：project 编排命令/事务/持久化；业务校验、编译、判定由注入的领域处理器与端口执行（ARC-02、NFR-MNT-04）；不实现 RobotDesign 编辑、双编译算法、策略判定、RunRegistry、结果判定、编解码器、界面与报告渲染（§2.3）。
6. **可直接落地**：阶段 A 交付存储原语、锁、格式契约、对象库、修订/分支模型、事务引擎、打开/恢复、查询、命令、确认流、草稿、撤销/重做、归档端口（§12）；业务命令处理器、双编译真实实现、包/另存/固化/升级器按上游阶段经接口承接（§13）。

### 1.4 语言、标准库与构建约束（按仓库实测确认，与 core.md/testkit.md §1.4 同源）

| 项 | 实测事实 | 本文决定 |
| --- | --- | --- |
| 编译器/生成器 | VS 17 2022（MSVC x64） | 按 MSVC 设计；Windows 专属代码（锁、原子替换、路径规范化）隔离在 `src/win32/` 实现文件内，公共头零平台 API 泄漏 |
| C++ 标准 | 基线 RobWork `CMAKE_CXX_STANDARD 11`；industrialrobot 显式 C++17（core.md D-01） | 同口径：显式 `cxx_std_17`；`std::filesystem`/`optional`/`variant` 允许；不用 C++20；不引入 Boost 等第三方运行库 |
| Qt | Qt 6.11.1（msvc2022_64）；L3 允许 Qt Core/Gui、禁 Widgets | **project 目标零 Qt（连 Core 也不用）**（决策 D-01）：文件/线程/时间用 std + Win32；定时落盘的触发归 ui 会话层（ARCH §3.1 ui 行 DraftController），project 只提供保存服务——最大化模型测试直调能力（NFR-MNT-01 精神），避免 Qt 事件循环依赖渗入存储内核 |
| RobWork 数学 | `rw::math`（`sdurw_math`） | project 不直接消费（经 core 公共头传递即可）；不直接包含 Eigen |
| Windows API | kernel32（CreateFileW/MoveFileExW/FlushFileBuffers/GetFinalPathNameByHandleW 等） | MSVC 默认链接；行为保证逐项对照 Microsoft Learn 文档登记（§7.2），**不凭记忆宣称断电安全** |
| 异常 | RobWork 惯例异常 | project 用自有 `StoreError`（§5.0）；可恢复查询路径提供 `try*` 非抛出变体 |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**project 拥有（实现责任方）**：

| # | 能力 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | 项目存储上下文与写权限（打开/只读/关闭/排空） | ARCH §6.8、PM-07 | §5.1、§9 |
| O-2 | `.rwdesign` 全部磁盘布局与读写入口（磁盘写入唯一归属） | REQUIREMENTS §17 表注、ARCH §6.1 | §4.1 |
| O-3 | 对象、修订、分支与项目元数据持久化（不可变对象库、修订清单、ProjectMetadata） | CON-01、ARCH §6.2/6.3 | §4.6/§4.8 |
| O-4 | ProjectCommandService 与原子提交（唯一写路径、命令串行、恰好一个新修订） | ARC-01、ARCH §7.1 | §5.3、§6 |
| O-5 | 可确认诊断放行流的编排与确认留痕（SA-15） | ARCH §7.1、MDL-06④ | §6.7 |
| O-6 | 草稿落盘、加载、恢复、汇总投影与 StaleRevisionRejected | PM-04/08、ARCH §6.3 | §5.4、§8 |
| O-7 | 撤销/重做的提交机制（逆命令＝新修订） | PM-18 | §5.5、§6.9 |
| O-8 | 查询端口（②端口：只读快照/对象/修订/分支/草稿/结果清单） | ARCH §7.2 | §5.2 |
| O-9 | 多文件事务与崩溃恢复协议（七步、.staging、HEAD 原子切换、恢复扫描） | NFR-REL-01、PM-08、ARCH §6.4 | §7 |
| O-10 | 项目格式检查（打开协议服务侧五步中的目录形态/版本/读校验/恢复诊断） | PM-02、PM-06 | §8.7 |
| O-11 | schema 升级器（已发布新格式的前向升级） | NFR-DEP-04、ARCH §5.5 | §8.11 |
| O-12 | 结果/检查点/报告/目录工件的存储发布与引用保护（results/checkpoints/reports/catalog 写入口与完整性发布） | ARCH §6.5/§6.8 写入口清单、CON-03 | §5.6、§10.1 |
| O-13 | 外部资源固化的存储侧（项目内不可变副本发布、引用保护、重关联产生新修订的提交） | CON-03、PM-09、ARCH §6.6 | §5.7 |
| O-14 | 另存为/项目包导入导出的存储侧服务（暂存、验证、发布、取消清理、一致视图） | PM-05 | §5.8、§8.8～§8.10 |
| O-15 | 身份分配策略落地（ObjectId/ProjectId/BranchId/RevisionId 分配时机与单调修订序号） | core.md §4.1 分配列、§10.3 交接 | §4.4/§6.1 |

**project 消费（经依赖白名单）**：

| 消费方 → 被消费 | 形态 | 消费内容 | 上游依据 | 状态 |
| --- | --- | --- | --- | --- |
| project → core | 接口依赖（编译链接） | 全部身份类型、ContentVersion/ContentIdentity/ContentDigester、SourcedValue/ValueProvenance、DiagnosticRecord/ConfirmableFinding/ConfirmationCredential、IDomainEventBus（发布方）、CoreError | ARCH §3.5 表 | core.md v0.1 **Draft 未冻结**（P-PR-1） |
| project → diagnostics | 接口依赖（允许边，实现经注入适配） | 稳定诊断码注册（PRJ-\* 码值）、两级日志设施 | ARCH §3.5 表、§7.8 | diagnostics 详设未产出：码值与日志经适配器注入，码表建议见 §5.0（P-PR-6） |
| project → runtime | **运行时注入**（零编译依赖） | 双编译（WorkCell/DynamicWorkCell）经 `IModelCompilePort` 最小契约，由 L5 装配注入 runtime 实现 | ARCH §7.1 S1、MDL-06 | runtime 详设未产出；最小契约 §5.3.6（P-PR-7 之一） |
| project → policy | **运行时注入**（经领域处理器间接） | 行程上限等策略校验由处理器经④端口读取 EngineeringPolicySet 后产生 ConfirmableFinding | ARCH §7.1/§7.5 | 同上 |
| project → execution | 反向服务（execution→project 接口依赖） | project 提供 IResultArchivePort；RunRegistry/五元组校验/调度归 execution | ARCH §3.5、§4.5 | execution 详设未产出；端口契约 §5.6（P-PR-4） |
| project → io | **待裁决边**（P-PR-5） | CSV/JSON 编解码、SafePath/BudgetGuard、包编解码、外部源安全读取 | 任务约束§四（project 消费 io 的解析/资源导入/包处理能力）；ARCH §3.5 表**未登记** project→io 边 | 阶段 A 不需要；阶段 B（包/固化）落地前裁决 |

**project 不拥有（其他单元所有，本文不实现、不预建桩）**：

| # | 不拥有内容 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | RobotDesign 业务编辑算法、物理合法性断言的判定逻辑、物性公式 | modeling（处理器内） | MDL-01～22、ARCH §7.1 |
| N-2 | WorkCell/DynamicWorkCell 编译实现（CanonicalModel 确定性编译链） | runtime | ARC-03、ARCH §7.3 |
| N-3 | 碰撞算法与工程策略判定实现（阈值数值、行程上限 4π） | policy | ARC-05、ARCH §7.5 |
| N-4 | RunRegistry、任务调度、任务状态机、取消/强制终止 | execution | TASK-01～03、ARCH §4 |
| N-5 | 结果工程判定、RequiredEvidenceProfile、ResultEnvelope 构造校验、结果当前性计算 | evidence | EVI-01/02、CON-02/04/05、ARCH §7.6 |
| N-6 | CSV/URDF/网格编解码、ZIP 包编解码、路径/预算防护实现 | io | NFR-SEC-01～03、ARCH §7.9 |
| N-7 | 界面确认对话框、向导、阶段导航、命令面板、DraftController | ui/workflow | UX-13、PM-01～03、ARCH §3.1/§7.11 |
| N-8 | 报告渲染与证据包内容 | reporting | RPT-01～06 |
| N-9 | 证据对象的业务判断（project 只保存与提供证据对象） | evidence | 任务约束§五.2 |
| N-10 | 草稿局部撤销（需求集撤销等编辑级 undo） | ui＋各业务域 | REQ-11、PM-18（与项目命令撤销分离） |

### 2.2 直接承接的需求（project 为实现责任方或数据契约责任方之一）

| 需求 | project 承接的部分 | 不可越界的部分 |
| --- | --- | --- |
| ARC-01（ProjectRevision 聚合根＋领域命令原子产生修订） | ProjectRevision 持久化结构、命令服务、原子提交、并发契约（串行＋修订单调＋冲突在命令边界拒绝） | 领域命令的业务语义（各处理器） |
| ARC-02（端口协作） | ①命令端口、②查询端口的所有者；磁盘写入唯一归属 | 其余四端口所有者 |
| CON-01（身份/版本包络；分析从不可变快照运行） | 包络的持久化编址（objects/ 按 ID＋内容版本）与快照读取（修订闭包视图） | 包络值类型（core）；快照组装机制（evidence） |
| CON-03（Verified/正式报告外部资源项目内不可变副本） | 固化副本的存储发布（入 objects/）与引用保护；重关联命令提交 | 固化时机的判定（evidence/reporting 依 EVI-01 阻断）；外部源检测（io） |
| CON-04（缓存/检查点契约判定） | checkpoints/ 写入口与清单发布 | 命中与兼容判定（execution/evidence） |
| TASK-03/PM-13（迟到结果归属） | 归档端口（绑定原修订 results/<run-id>/；不因 HEAD 拒绝） | 五元组核对、RunRegistry（execution）；当前性（evidence） |
| PM-01（新建三步向导的存储侧） | 目录创建、初始修订/元数据、外部引用记录的持久化 | 向导 UI（workflow）；模板内容（modeling）；一次性读取（io） |
| PM-02（打开五步协议） | 步骤②目录形态与版本检查、③加载与读校验、⑤激活与恢复诊断（服务侧 API） | 路径预检入口与向导（workflow）；领域校验（各域处理器） |
| PM-03（关闭/切换/退出） | 存储上下文排空与释放语义（等待在途归档/草稿完成） | 确认对话框（ui/workflow） |
| PM-04（保存与应用分离；草稿基线冲突） | DraftService（定时落盘服务侧、60 s 默认）、应用＝恰好一个新修订、StaleRevisionRejected | 会话状态机与未保存标记（ui）；PM-04-S1 周期设置（R2，用户级设置归 ui/PM-14） |
| PM-05（另存为/项目包） | 目录复制＋新 projectId、包导入暂存/验证/发布/取消清理、导出一致视图（存储侧） | 勾选记忆（ui）；ZIP 编解码与预算防护（io）；后台进度 UI（ui） |
| PM-06（旧格式/未来版本） | 稳定只读拒绝＋诊断码＋升级指引数据；升级器（NFR-DEP-04） | 升级工具入口 UI（workflow） |
| PM-07（只读打开） | 写锁机制、writable 判定、持锁 PID 读取、全部写入口权限检查 | 提示呈现（ui） |
| PM-08（崩溃与恢复） | .staging 扫描、锁残留、孤儿草稿报告、HEAD 闭包完整性校验、恢复诊断数据 | 恢复横幅呈现（PM-15→ui）；PM-08-S1 只读报告内容（R2，格式本文预留字段） |
| PM-09（重关联用户流程存储侧） | 重关联命令提交产生新修订 | 重关联入口 UI（workflow）；缺失/变化检测（io，NFR-REL-04） |
| PM-12（方案分支） | 分支记录 baseRevisionId 不复制对象、可切换（会话选择）、分支表随 ProjectMetadata 提交 | 分支切换 UI（workflow/ui）；PM-12-S1 历史浏览（R2，读取本文修订清单） |
| PM-18（项目命令撤销/重做） | 撤销/重做＝新修订的提交机制、空历史稳定提示数据、与草稿局部撤销的边界 | 菜单/命令面板绑定（ui）；跨分支/过期口径按统一命令契约执行 |
| NFR-REL-01（多文件事务） | 全部事务协议（§7） | 单资源"暂存＋原子替换"的版本目录内使用（同协议内） |
| NFR-REL-04（外部源缺失/变化可检测） | ExternalResourceRecord 的持久化（路径＋内容哈希）供 io 检测 | 检测实现（io） |
| NFR-SEC-01（资源区不逃逸） | 项目内持久化引用的存储边界（objects/ 资源区）与解包成员发布边界 | SafePath 实现与导入向导（io/workflow） |
| NFR-DEP-04（schema 版本与升级器） | schemaVersion 契约、拒绝规则、升级器 | 安装包交付（WP-24） |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

对象垃圾回收/删除（PM 明确不做，恢复扫描只读报告残留，R2 子项 PM-08-S1）；`.rwproj` 兼容读取或双写（稳定拒绝）；修订 diff/回滚 UI 与数据（PM 明确不做；逆命令≠回滚）；分支改名（PM 明确不做；创建时 label 一次写入，P-PR-8）；第二持久化格式或数据库（任务约束§三）；分布式恰好一次语义（任务约束§五.3，§6.10 明确范围）；自动保存整项目快照（需求附录 B 裁决排除）；任何业务判定（§2.1 N 表）。

---

## 3. 单元组成、依赖与目录布局

### 3.1 组成（公共头模块）

| 头（`project/include/sdurws/ird/project/`） | 内容 | 详见 |
| --- | --- | --- |
| `StoreTypes.hpp` | OpenMode/OpenStoreRequest/OpenStoreResult、StoreError/StoreErrorCode、RecoveryReport、LockInfo、SchemaInfo、StorePath | §4、§5.0/§5.1 |
| `ProjectStore.hpp` | ProjectStore（存储上下文）、ProjectStoreFactory、ICloseObserver | §5.1、§9.7 |
| `QueryPort.hpp` | IProjectQueryPort、RevisionView/ProjectMetadataView/BranchTip/RunInfo/DraftInfo、ObjectCache 预算参数 | §5.2 |
| `CommandService.hpp` | CommandEnvelope/CommandResult/CommandStatus、ICommandHandler/HandlerContext/CommandPlan、ICommandInteraction、IModelCompilePort、ProjectCommandService、HandlerRegistry | §5.3、§6 |
| `UndoRedo.hpp` | UndoRedoService、UndoRedoStatus、InverseRecord | §5.5、§6.9 |
| `DraftService.hpp` | DraftDocument、DraftService、DraftProjection、SaveResult/DiscardResult | §5.4、§8 |
| `ArchivePort.hpp` | ArchiveRequest/ArchiveBatch/RunManifest/ArchiveStatus、IResultArchivePort、ArchiveSessionRef | §5.6、§10.1 |
| `ResourceRefs.hpp` | ExternalResourceRecord、SolidifyRequest/Result | §5.7 |
| `PackageService.hpp` | SaveAsOptions/PackageExportOptions/PackageImportResult、ISaveAsService/IPackageService（阶段 B 落地，接口先冻结） | §5.8、§8.8～§8.10 |
| `Upgrade.hpp` | SchemaUpgrader 注册与调用（阶段 B+） | §8.11 |
| `PersistenceFormat.hpp` | 磁盘格式读取类型：ProjectStaticIdentity、HeadRecord、RevisionManifest、ProjectMetadataRecord、CommandRecord（升级器/工具消费的稳定契约） | §4.2/§4.3/§4.4 |
| `README.md` | 既有保留位说明（不参与编译，已指向本文 §9） | — |
| `src/`（实现，随 §12 任务落地） | `win32/AtomicFile.*`、`win32/StoreLock.*`、`win32/PathCanonical.*`、事务引擎、对象库、编解码器、命令服务、查询/草稿/撤销/归档实现 | §6/§7/§9 |

### 3.2 依赖（含 core 契约消费状态登记）

```
sdurws_ird_project ──► RWS::ird::core（PUBLIC：身份/摘要/诊断数据/事件接口；sdurw_math 经其传递）
                   ──► C++17 标准库 ＋ Win32（kernel32；隔离于 src/win32/）
                   ──► RWS::ird::diagnostics（ARCH §3.5 已登记边；消费经 IDiagnosticsSink 适配器注入，
                        适配器接口定义于本文 §5.0，diagnostics 详设产出前以空实现占位于测试/装配侧）
                   ──✖ 零 Qt（D-01）、零业务单元链接、零 testkit（产品目标）、零 Eigen 直接包含
运行时注入（零编译依赖）：IModelCompilePort（runtime）、ICommandHandler 各实现（业务单元）、
                        ICommandInteraction（ui）、IDiagnosticsSink（diagnostics）、事件总线实例（execution·ui）
```

消费的 core 契约清单（全部来自 core.md v0.1 §4/§5；**状态：Draft 未冻结**，P-PR-1）：

| core 契约 | project 用途 | 状态锚点 |
| --- | --- | --- |
| ObjectId/ProjectId/BranchId/RevisionId/RunId/AttemptId/TaskIdentity | 全部持久化身份与归档绑定 | v0.1 §4.1/§5.1 |
| ContentVersion/ContentIdentity/ContentDigester | 对象编址（objects/<oid>/<cv>）、清单摘要、完整性校验 | v0.1 §4.2/§5.2 |
| SourcedValue/ValueProvenance | ExternalResourceRecord 与草稿负载中的来源标记透传（不解释） | v0.1 §4.3 |
| DiagnosticRecord/ComparativeFields/ConfirmableFinding/ConfirmationCredential | 确认放行流数据、恢复/只读诊断 | v0.1 §4.8/§5.7 |
| IDomainEventBus/DomainEvent（RevisionCommitted/DependencyInvalidated/ResultArchived） | 提交后事件发布、归档完成事件 | v0.1 §4.9/§5.8 |
| CoreError | core 抛错捕获与转发 | v0.1 §4.10 |

### 3.3 命名空间、目标与 CMake 集成

- 命名空间 `sdurws::ird::project`；目标 `sdurws_ird_project`（INTERFACE → PRJ-T01 升级 STATIC），别名 `RWS::ird::project`；`target_compile_features(... cxx_std_17)`；`target_link_libraries(sdurws_ird_project PUBLIC RWS::ird::core)`。
- 测试目标：`sdurws_ird_project_test`（单元内）、`sdurws_ird_project_contract_test`（跨单元契约：锁双实例、归档协作等；gtest 接入按 development-task-breakdown.md §5.5 定稿——vcpkg＋`find_package(GTest CONFIG REQUIRED)`，失败即停〔CR-07 同步更正，2026-09-10〕），随 PRJ-T01 登记；产品目标不链 testkit（T-1 红线）。
- 头包含形式 `#include <sdurws/ird/project/CommandService.hpp>`；私有实现头不入 `include/`（R-2 纪律）。

### 3.4 源码目录布局

```
industrialrobot/project/
  include/sdurws/ird/project/     # §3.1 公共头（契约权威）
  src/
    win32/AtomicFile.{hpp,cpp}    # 建临时文件→FlushFileBuffers→MoveFileExW 原子替换；只增发布；fault 注入接缝 IFileOps
    win32/StoreLock.{hpp,cpp}     # 独占句柄锁、心跳重写、PID 读取
    win32/PathCanonical.{hpp,cpp} # GetFinalPathNameByHandleW 规范化
    TxEngine.{hpp,cpp}            # 七步事务状态机（§7）
    ObjectStore.{hpp,cpp}         # 内容编址对象库＋LRU 缓存
    RevisionIndex.{hpp,cpp}       # 修订闭包/分支表/元数据谱系（§4.4/§4.5）
    Codec.{hpp,cpp}               # project 自有格式 canonical 编码（§4.8）
    ProjectStoreImpl.{hpp,cpp}    # 打开/恢复/查询/上下文生命周期
    CommandServiceImpl.{hpp,cpp}  # 命令服务/处理器注册/确认流/编译编排
    DraftServiceImpl.{hpp,cpp}    UndoRedoServiceImpl.{hpp,cpp}  ArchiveServiceImpl.{hpp,cpp}
  test/                           # PRJ-* 用例（§11）
```

---

## 4. 持久化模型与格式契约

### 4.1 `.rwdesign` 目录总表（全部条目及所有者）

磁盘写入唯一归属 project（REQUIREMENTS §17 表注）；所有条目的运行时读写入口均为 project 存储端口（§5），其他单元经端口请求。schemaVersion 契约见 §8.11。

| 条目 | 用途 | 命名规则 | 可变性 | 引用关系 | 读写入口 | 何时创建 | 何时发布 | 异常残留处理 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `HEAD` | 提交指针（唯一提交点） | 固定名 | 原子替换（内容不可变语义：每次整体替换） | 指向最新提交修订＋权威元数据摘要 | ProjectStore/事务引擎 | 项目创建首个修订时 | 事务第 5 步原子切换 | 损坏/缺失＝CorruptStoreDetected（PM-02 读校验）；切换前的旧 HEAD 即权威 |
| `project.json` | 项目静态标识（projectId、schemaVersion、格式标识、创建时间与工具版本） | 固定名；内容见 §4.2 | **创建期一次写入，不参与事务**（ARCH §6.1 A2）；仅另存为/升级时整体重写 | 被 openStore 读取；不被修订引用 | openStore/SaveAs/Upgrader | 项目创建时 | 创建即生效（静态） | 缺失＝非项目目录；projectId 与 HEAD 不一致＝CorruptStoreDetected |
| `revisions/<rev-id>/` | 修订清单 | `<rev-id>`＝core 规范文本（`rev-<32hex>`）；内含 `manifest.json`＋`command.json` | **只增不改** | manifest 引用 objects 与 ProjectMetadata；command 记录含逆命令 | 事务引擎发布；查询端口读取 | 事务暂存期建目录 | 事务第 4 步内容发布 | 在闭包外（§4.5 规则）＝未提交残留：忽略＋恢复诊断，不作历史展示 |
| `objects/<oid>/<cv>` | 不可变对象库（按对象 ID＋内容版本编址；负载为处理器交付的 canonical 字节） | `<oid>`＝`obj-<32hex>`；`<cv>`＝内容版本 64 hex（无 tag） | **只增不改不覆盖**（内容寻址） | 被修订清单/results 清单/外部引用记录引用 | 对象库（写）；查询端口＋归档会话（读） | 事务暂存期写入 `.staging`，发布期就位 | 事务第 4 步 | 闭包外对象＝悬挂对象：开发诊断计数（只读报告归 PM-08-S1/R2）；**不删除**（GC 明确不做） |
| `results/<run-id>/` | 任务运行结果与证据（绑定修订与任务身份） | `<run-id>`＝`run-<32hex>`；批次文件任意名，`manifest.json` 最后发布 | 批次文件只增；manifest 原子替换（重投递幂等） | manifest 声明批次文件＋所属 TaskIdentity 五元组＋修订 | IResultArchivePort（execution 调用） | 归档 begin 时 | finalize（manifest 原子发布）＝"完整" | 无 manifest 的目录＝未完成归档：查询不列为完整运行；恢复诊断列出；重投递可续写或冲突拒绝（§10.1） |
| `checkpoints/<task-key>/` | 长任务检查点 | `<task-key>`＝`<run-id>`＋可选序号 | 批次只增＋manifest 发布（同 results 模式） | 被 execution 缓存命中判定消费（CON-04） | 归档端口（同通道） | 检查点写出时 | finalize | 无 manifest＝不作为缓存命中（CON-04：部分/失败结果不得命中） |
| `reports/<report-id>/` | 报告工件（ReviewReport 渲染产物、证据包） | `<report-id>` 由 reporting 分配（稳定 token） | 工件只增；`report.json` 描述文件原子替换 | 引用修订/结果清单 | reporting 经存储端口（阶段 B 起接入） | 渲染时 | 描述文件发布 | 未完成工件不列入报告清单（幂等追加、冲突拒绝归 reporting 契约） |
| `drafts/<branch-id>/<module>.draft.json` | 未应用草稿（定时落盘） | 分支目录＋模块文件名（`<module>`＝注册的模块 token） | 覆盖写（原子替换）＋`.bak` 保留上一版 | 内容自带 projectId/branchId/baseRevision（加载校验） | DraftService | 首次保存 | 保存即生效（非修订） | `.new`/`.bak` 残留＝崩溃现场：恢复诊断＋旧版可恢复（§8.4） |
| `catalog/<catalog-id>/<version>/` | 器件目录锁定版本（SEL-08） | 目录 ID＋版本号 | 只增（锁定版本不可变） | 被选型命令的对象引用与 ValueProvenance.sourceVersion 引用 | io/selection 经存储端口（阶段 C） | 目录导入锁定时 | 写入即锁定 | 不完整导入＝删除重导（导入失败不留目标，PM-05 同口径） |
| `.staging/<tx-id>/` | 事务暂存区 | `<tx-id>`＝进程内单调＋随机后缀 | 事务私有；提交后整体清理 | 事务期内可见，不对外 | 事务引擎独占 | 事务第 1 步 | 第 4 步迁出（内容发布） | 残留＝未提交：启动扫描忽略＋恢复诊断（PM-08）；不作为内容来源 |
| `.staging/tmp/` | 编译临时资源与非事务临时文件 | — | 事务外亦可短期使用（如固化复制中转） | 无对外引用 | 事务引擎/固化服务 | 按需 | 随事务/操作清理 | 残留同上，启动清理 |
| `lock` | 写锁文件（持有进程 PID＋主机＋心跳时间戳） | 固定名 | 内容周期重写（固定宽度记录）；**文件本身不删除重建**（D-03） | 无（不被引用） | StoreLock（§9） | 首次获取写权限时 | 不"发布"（诊断用） | 残留（崩溃后）＝下次接管时读取作恢复诊断并重写内容（§9.4） |

对象/修订/结果等全部路径仅经规范路径访问；项目内持久化资源引用不得逃逸 `objects/` 资源区（NFR-SEC-01 消费侧——project 校验一切写入路径落在 §4.1 白名单内）。

### 4.2 project.json 静态标识与 ProjectMetadata 的区别（ARCH §6.1/§6.3 A2 承接）

| 维度 | `project.json`（静态标识） | `ProjectMetadata`（不可变元数据对象） |
| --- | --- | --- |
| 内容 | `{formatId:"rwdesign", schemaVersion, projectId, createdAtUtc, createdWithToolVersion}` | `{schemaVersion, committedBy, supersedes?, projectDisplayName, primaryBranchId, branches[], schemeLabels}`（全字段见 §4.4.3） |
| 承载信息 | **创建后不变的项目本体事实**：谁（projectId）、什么格式（schemaVersion）、何时创建 | **随演进的权威状态**：分支表（含各分支 tip）、方案清单与 label、项目显示名 |
| 事务参与 | **不参与**（创建期一次写入；另存为/升级整体重写） | **参与**：作为对象入 objects/，随修订引用、随 HEAD 原子切换提交 |
| 一致性责任 | 无中间态（无并发修改者） | 与修订同协议提交，无"project.json 与 HEAD 分别更新"的中间态（A2 原文口径） |
| 修改途径 | 无（projectId 永不改名换 ID；显示名在 ProjectMetadata） | 领域命令（创建分支/设默认方案/项目改名〔R2 PM-11-S2〕）→ 新版本＋新修订 |
| 消费者 | openStore、另存为、升级器 | 查询端口（分支 tips、方案清单、标题栏显示名 PM-11） |

PM-11-S1 验收口径按 ARCH §11.3 待同步项 4 执行：projectId/schemaVersion 取自 project.json，名称/修订数/对象数取自当前权威 ProjectMetadata 对象——本文即按此结构设计，该待同步项随本文件产出可消账。

### 4.3 对象/修订/元数据/HEAD 引用关系图

```
                ┌─────────────── project.json（静态：projectId/schemaVersion；不参与事务）
                │
 HEAD ──────────┼──► revision r_head（revisions/<rev-id>/manifest.json）
（原子替换，      │        │ parent ────────► 父修订（分支 tip 维度，可缺）
 唯一提交点）     │        │ metadataRef ───► ProjectMetadata M_head（objects/<oid>/<cv>）
                │        │ objectRefs[] ──► 该修订的完整对象引用集（全量状态闭包）
                │        │                {objectId, contentVersion, objectTypeToken, digest}
                │        │ command.json ──► CommandRecord（命令摘要/载荷/逆命令/确认留痕）
                │        │ introduced[] ──► 本次新引入对象（恢复/残留识别用）
                │        ▼
                │     ProjectMetadata M_head
                │        │ committedBy ───► r_head（本元数据由哪次修订提交）
                │        │ supersedes ────► M_prev（元数据谱系，见 §4.5 闭包规则）
                │        │ branches[] ────► {branchId, baseRevisionId, tipRevisionId, label}
                │        ▼
                ┋     （分支 tip 各自沿 parent 链构成修订 DAG；对象被多修订共享，不复制）
```

四个身份概念互不混用（core.md §4.1.1 承接复述）：**对象身份**（ObjectId，跨内容修改稳定）≠**对象内容版本**（ContentVersion，内容字节摘要）≠**修订身份**（RevisionId，一次命令提交）≠**内容摘要**（Digest256，完整性校验值）。修订清单中的 digest 用于读校验，不充当对象身份；HEAD 中的 manifestDigest 用于切换后自校验，不充当修订身份。

### 4.4 数据类型与格式契约（字段级）

约定：全部磁盘 JSON 为 project canonical 编码（§4.8）；身份字段一律 core 规范文本；时间 ISO-8601 UTC；数值十进制文本（round-trip 安全形式）。以下类型在 `PersistenceFormat.hpp` 冻结。

#### 4.4.1 HeadRecord（HEAD 文件内容）

| 字段 | 类型 | 必填 | 默认 | 约束与语义 |
| --- | --- | --- | --- | --- |
| `formatId` | string | 是 | — | 固定 `"rwdesign"` |
| `schemaVersion` | int | 是 | — | 与 project.json 一致（不一致＝CorruptStoreDetected） |
| `projectId` | string | 是 | — | `prj-<32hex>`；与 project.json 一致 |
| `revisionId` | string | 是 | — | 最新提交修订 |
| `revisionSeq` | uint64 | 是 | — | 修订单调序号（项目内递增；不进 RevisionId——core D-04） |
| `branchId` | string | 是 | — | 该修订所属分支（重开会话的默认分支；D-06） |
| `manifestDigest` | string | 是 | — | 指向修订 manifest 的字节摘要（切换后自校验） |

#### 4.4.2 RevisionManifest（revisions/<rev-id>/manifest.json）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `revisionId` | string | 是 | 与目录名一致 |
| `revisionSeq` | uint64 | 是 | 项目内单调；分配归命令服务（O-15） |
| `parentRevisionId` | string? | 否 | **分支 tip 维度的父修订**（§4.5）；首修订无 |
| `branchId` | string | 是 | 提交时活动分支 |
| `committedAtUtc` | string | 是 | 提交时间 |
| `metadataRef` | object | 是 | `{objectId, contentVersion}` 指向本修订引用的 ProjectMetadata |
| `objectRefs[]` | object[] | 是（≥1，含 metadata 对象） | 完整对象引用集：`{objectId, contentVersion, objectTypeToken, digest256}`；全量状态（ARCH §6.3"对象集引用"） |
| `introducedObjects[]` | string[] | 否 | 本次新就位对象的内容版本列表（残留识别/浏览辅助） |

#### 4.4.3 ProjectMetadataRecord（元数据对象负载）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `schemaVersion` | int | 是 | 元数据负载自身版本（随整体 schemaVersion 联动） |
| `committedBy` | string | 是 | 提交本元数据版本的修订 id（闭包规则第二通道，§4.5） |
| `supersedes` | object? | 否 | `{objectId, contentVersion}` 指向上一权威元数据（谱系链；首版无）。**发布时校验谱系无环** |
| `projectDisplayName` | string | 是 | 项目显示名（PM-11 标题栏；R2 PM-11-S2 经命令改名） |
| `primaryBranchId` | string | 是 | 主分支（初始分支） |
| `branches[]` | object[] | 是（≥1） | `{branchId, label, baseRevisionId, tipRevisionId, createdAtUtc}`；label 创建时一次写入（P-PR-8） |
| `schemeLabels` | map | 否 | 方案清单附加标签（阶段 B 随 workflow 需要启用，字段先冻结） |

不变量 INV-M1：`branches[]` 内 branchId 唯一；`tipRevisionId` 必须指向已存在修订（发布期校验）。INV-M2：`supersedes` 链全局无环、版本单调（发布期校验，自审项 A-7）。

#### 4.4.4 CommandRecord（revisions/<rev-id>/command.json）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `commandType` | string | 是 | 处理器注册 token（`^[a-z0-9-]{3,64}`） |
| `payloadFormatVersion` | uint32 | 是 | 处理器自有负载版本（§6.4） |
| `payloadCanonical` | string | 是 | canonical 编码负载（域所有；project 不解释） |
| `inverse` | object? | 否 | `{commandType, payloadFormatVersion, payloadCanonical}` 逆命令表达（§6.9）；不可逆命令无此字段 |
| `confirmations[]` | object[] | 否 | 确认留痕：`{findingDigest, policyContentId, commandDigest, baseRevisionId, credential{principal, confirmedAtUtc}}`（§6.7 绑定） |
| `summary` | string | 是 | 人读命令摘要（PM-12-S1 历史浏览展示；处理器生成，project 原样持久化） |

#### 4.4.5 DraftDocument（drafts/<branch-id>/<module>.draft.json）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `schemaVersion` | int | 是 | 草稿格式版本 |
| `projectId`/`branchId`/`moduleId` | string | 是 | 归属三元组（加载时与路径和 project.json 校验，不一致＝DraftCorruptDetected） |
| `baseRevisionId` | string | 是 | 草稿基线（≠分支 tip 时应用被拒，PM-04） |
| `payload` | string | 是 | 模块草稿负载（域所有 canonical 字节） |
| `externalRefs[]` | object[] | 否 | 草稿期外部引用记录：`{externalRefId, absolutePath, contentHash256, sizeBytes, recordedAtUtc, state}`（NFR-SEC-01：不以裸路径充当项目资源引用；PM-01 登记口径） |
| `savedAtUtc` | string | 是 | 落盘时间 |
| `origin` | enum | 是 | `autosave`/`manual`/`apply-retained`（应用被拒后保留，§8.3） |

#### 4.4.6 对象文件（objects/<oid>/<cv>）

内容＝处理器交付的 canonical 负载字节，**原样存储**（project 不加信封、不解释——D-10）；`<cv>`＝SHA-256(负载) 的 hex；对象类型 token 记录在引用方（修订清单 results manifest 等），不在对象文件内自述（避免同一字节因存放形态不同产生第二身份）。合法实例：某 `RobotDesign` 参数对象的 canonical JSON 字节。非法实例：负载摘要与文件名 `<cv>` 不符（读校验拒绝）。

#### 4.4.7 RunManifest（results/<run-id>/manifest.json）

| 字段 | 类型 | 必填 | 约束与语义 |
| --- | --- | --- | --- |
| `taskIdentity` | object | 是 | 五元组（projectId/branchId/revisionId/runId/attemptId）——原样记录 execution 登记信息 |
| `items[]` | object[] | 是（≥1） | `{relPath, sha256, sizeBytes}` 批次文件完整性清单 |
| `runKind`/`evaluationKey` | string | 是 | execution 登记的任务类型与评估键（透传） |
| `finalizedAtUtc` | string | 是 | finalize 时间 |
| `manifestDigest` | string | 是 | 自身规范化摘要（幂等重投递判据，§10.1） |

#### 4.4.8 StoreErrorCode（稳定 token，§5.0 详述）

`lock-held-by-other`、`media-read-only`、`access-denied`、`not-a-project`、`format-legacy`、`schema-future`、`store-corrupt`、`write-rejected`、`disk-full`、`context-closed`、`stale-revision-rejected`、`unknown-command`、`invalid-payload`、`confirmations-unresolved`、`interaction-lost`、`command-aborted`、`compile-failed`、`archive-conflict`、`archive-target-missing`、`draft-corrupt`、`branch-metadata-regression`（防御性，§4.5 不变量）。

### 4.5 修订 DAG、分支模型与提交闭包

**三种"修订指针"的区别**（任务约束§五.1 明确项）：

| 概念 | 存储 | 含义 | 谁修改 |
| --- | --- | --- | --- |
| 分支 baseRevision | ProjectMetadata.branches[].baseRevisionId | 分支创建时的起点修订（**不变**，创建时记录） | 创建分支命令一次写入 |
| 分支当前修订（tip） | ProjectMetadata.branches[].tipRevisionId | 该分支最新提交 | 该分支上的每次提交更新（经新元数据版本） |
| 项目提交根（HEAD） | HEAD 文件 | 全项目最新一次提交的修订（唯一提交点，事务切换的对象） | 每次成功提交切换 |

**提交闭包（committed closure）规则**——判定"哪些修订属于已提交历史"（供恢复扫描、历史浏览、残留识别共用）：

```
closure(r_head)：
  visit_rev(r)：标记 r；
    若 r.parentRevisionId 存在 → visit_rev(parent)          ── 通道一：修订 DAG（分支 tip 维度）
    visit_meta(r.metadataRef)
  visit_meta(M)：标记 M.committedBy 已由其所属修订覆盖；
    若 M.supersedes 存在 → visit_meta(supersedes 所指元数据)  ── 通道二：元数据谱系
  起点：visit_rev(HEAD.revisionId)
一切未被闭包覆盖的 revisions/ 目录与 objects/ 内容 ＝ 未提交残留或悬挂对象
```

双通道的必要性：`CreateBranch` 这类元数据命令产生的修订（如 §4.5.1 走查中的 r1）不再是任何分支的 tip、也无后续修订以它为 parent——仅靠修订 DAG 通道它将成为"孤岛"而被误判为未提交；元数据谱系通道（`supersedes`→`committedBy`）保证它可达。两条通道均为纯追加，**发布期校验无环与单调**（不变量 INV-M2，防引用循环——自审项 A-7）。

**防分支表回退不变量（INV-M3，本设计的关键不变量）**：每次提交生成的新 ProjectMetadata ＝ **以当前权威元数据（HEAD 引用版本）为底本拷贝更新**——仅更新活动分支的 tipRevisionId（及命令显式声明的分支表变更，如 CreateBranch 增加条目）；**绝不以父修订引用的元数据版本重建**。违反即抛 `branch-metadata-regression`（防御性检查：新元数据的分支表必须是权威版本的超集且既有分支 tip 不得回退）。

**切换分支是否改变持久化状态**：**否**。切换＝纯会话选择（活动分支记录在会话层，ARCH §6.3/PM-12 语义）；分支 tip 已在每次提交时随元数据持久化，切换不产生修订、不写任何文件（含 HEAD——HEAD 记录的是"最后一次提交所在分支"，不是"当前活动分支"）。会话重开时默认活动分支＝HEAD.branchId（D-06）。

#### 4.5.1 分支走查（创建分支 → A 修改 → 切换 B → 修改 B → 返回 A）

设初始修订 r0，主分支 main；权威元数据 M0{main→r0}，HEAD=r0。

| 步骤 | 操作 | 会话 | 新修订（parent／元数据） | 权威 ProjectMetadata（HEAD 引用） | HEAD |
| --- | --- | --- | --- | --- | --- |
| 1 | CreateBranch A（base=r0） | main→A | r1（parent=r0〔main tip〕；M1） | M1{main→r0；A(base r0, tip r0)} | r1 |
| 2 | 在 A 应用修改 | A | r2（parent=**r0**〔A.tip〕；M2） | M2{main→r0；A→r2} | r2 |
| 3 | CreateBranch B（base=r0，自 main） | A→（切换）→B | r3（parent=r0〔main tip〕；M3） | M3{main→r0；A→r2；B(base r0, tip r0)} | r3 |
| 4 | 切换 B（**仅会话**，零写入） | B | —（无修订） | 不变（M3） | r3 |
| 5 | 在 B 应用修改 | B | r4（parent=r0〔B.tip〕；M4） | M4{main→r0；A→**r2**；B→r4} | r4 |
| 6 | 返回 A（**仅会话**，零写入） | A | — | 不变（M4） | r4 |
| 7 | 查询 A 的最新 | A | — | A.tip ＝ **r2**（自 M4 读取） | r4 |

结论验证：①步骤 5 提交在 B 上，其元数据 M4 以权威 M3 为底本——**A 的 tip r2 被完整携带**（INV-M3），不因"B 的父修订是 r0、r0 引用的 M0 无 A/B 分支"而丢失或回退分支表；②步骤 6/7 读取 A.tip 一律取自 HEAD 引用的 M4（权威），**禁止**经 r2 引用的 M2 重建分支表（M2 无 B——若用它即发生"读取旧修订回退整个项目分支表"，正是 INV-M3 与闭包规则所禁止）；③修订闭包：r4→r0；M4→M3(committedBy r3)→r3→r0；M3→M2(r2)→r2→r0；M2→M1(r1)→r1→r0——r0～r4 全部可达，无孤岛；④若步骤 5 提交在"发布 r4 后、切换 HEAD 前"崩溃：重启后 HEAD=r3，闭包={r0,r1,r2,r3}，r4 目录存在但闭包外 → 按未提交残留忽略＋恢复诊断，**不会**（a）把 r4 当作已提交历史（自审项 A-6），（b）影响 M3 权威性。

> 上游模型足以唯一支持本流程（ARCH §6.3 A2"分支表/方案清单/项目名＝不可变元数据对象随修订引用"＋§6.4 单一提交根），无需架构裁决；本节将"权威元数据取 HEAD 引用版本、提交以权威为底本"细化为可执行不变量（INV-M3）。

### 4.6 对象库与内容编址

- **创建**：处理器在 `HandlerContext` 上申请新 ObjectId（project 分配，core.md §4.1 分配列：对象创建→project）或复用既有 ObjectId＋新内容版本；提交时对象以 canonical 字节进入暂存区，发布后就位 `objects/<oid>/<cv>`。
- **不可变保证**：内容寻址＋只增发布——目标文件已存在时发布动作退化为"校验既有文件摘要一致→共享，不一致→`store-corrupt`（磁盘内容被外部篡改）"；任何代码路径不以写模式打开已发布对象（§7.1 第 4 步的发布原语 `publishNew` 只允许"不存在则 rename、存在则校验"）。同内容自然共享（同 `<cv>` 文件名）。
- **读取与完整性**：`object(oid, cv)` 惰性加载；首次加载执行 size＋SHA-256 校验（core ContentDigester），摘要缓存后免复检；打开协议（§8.7）执行 HEAD 闭包全量校验（PM-02 读校验）。
- **缓存**：每存储上下文一个 LRU `ObjectCache`（默认预算 256 MiB，可配）；键＝(ObjectId, ContentVersion)；**跨项目隔离**——缓存挂在 ProjectStore 实例上，实例身份＝规范化路径（§9.3），不同项目（不同 projectId 或不同路径）实例互不共享；ObjectId 全局随机唯一（core），跨项目碰撞概率可忽略，缓存键仍含 oid 不含路径——因实例隔离而无串扰。
- **类型与引用存在性**：objectTypeToken 随引用登记；查询端口对"引用不存在于清单"的请求返回空 optional（`tryObject`），不抛；打开与恢复扫描报告悬挂引用（有引用无对象＝`store-corrupt`；有对象无引用＝悬挂对象开发诊断）。
- **查询快照与写入隔离**：修订视图一经返回不可变（值语义＋对象文件不可变）；写入只增新内容，既有读者持有的引用永不被覆盖——读写无共享可变状态（PA-2/PA-3）。
- **证据对象**：results/ 工件经归档端口存储发布，project 只保存与提供（清单/摘要/读取），不做任何 evidence 业务判断（N-9）。

### 4.7 只读查询的线程与生命周期契约

查询端口全部方法线程安全（并发只读）；返回视图为进程内值拷贝（无句柄泄漏）；生命周期随存储上下文（上下文关闭后查询端口进入 `context-closed` 拒绝）；只读打开的实例同样提供查询（PM-07 可查看）。线程模型与约束总表见 §9.8。

### 4.8 canonical 编码契约（ContentVersion 的"对什么做摘要"）

- **对象负载**：域处理器负责其负载的 canonical 编码（字段序固定、UTF-8、无空白、数值 round-trip 文本、键排序规则由域登记——core.md §6.3 分工："对什么做摘要归各所有者"）；project 对**收到的字节**计算 SHA-256 作为 ContentVersion 并原样存储（D-10）。跨单元可比的前提（如 evidence 切片引用对象）由各域任务卡登记其序列化约定（core.md §10.3 既有交接项，本文不代定）。
- **project 自有格式**（project.json/HEAD/manifest/metadata/command/draft/run manifest）：编码器归 project（`Codec`）；规则：ASCII、固定字段序、无浮点（全字符串身份与整数）、`parse(dump(x))==x`。JsonLite（testkit）不用于产品格式（testkit.md D-02 边界同精神）。
- **稳定性**：schemaVersion 主版本变更＝破坏性（走升级器）；追加可选字段＝次版本兼容。

---

## 5. 公共接口详细设计

约定（适用本章）：签名为实现建议（自洽契约片段，非完整实现，未编译验证）；除注明外方法线程安全；凡触发磁盘写入的方法均要求存储上下文 `Active && writable==true`（§9 失权检查），否则抛 `StoreError(context-closed|write-rejected|lock-held-by-other)`；错误一律 `StoreError`（携带稳定 code＋机器可读 detail），用户可见文案不在 project 生成（NFR-REL-05 口径，码表经 diagnostics 注册后由其供文案）。

### 5.0 错误类型、诊断码与 diagnostics 适配

```cpp
// StoreTypes.hpp
enum class StoreErrorCode { LockHeldByOther, MediaReadOnly, AccessDenied, NotAProject,
    FormatLegacy, SchemaFuture, StoreCorrupt, WriteRejected, DiskFull, ContextClosed,
    StaleRevisionRejected, UnknownCommand, InvalidPayload, ConfirmationsUnresolved,
    InteractionLost, CommandAborted, CompileFailed, ArchiveConflict, ArchiveTargetMissing,
    DraftCorrupt, BranchMetadataRegression };
class StoreError : public std::runtime_error {
public: StoreError(StoreErrorCode, std::string detail);
        StoreErrorCode code() const noexcept;   // detail 面向开发诊断，前缀 "project/<域>:"
};

// diagnostics 适配（ARCH §3.5 project→diagnostics 边的实现形态，P-PR-6）：
class IDiagnosticsSink {              // project 定义，diagnostics（或 L5 装配）提供实现
public: virtual ~IDiagnosticsSink() = default;
    virtual void report(const core::DiagnosticRecord&) = 0;   // 用户诊断（稳定码注册后）
    virtual void reportDev(const std::string& channel, const std::string& message) = 0;
};
```

project 侧稳定诊断码建议清单（**码值分配权威＝diagnostics StableCodeRegistry，未产出，以下为建议值，P-PR-6 交接**）：`PRJ-LOCK-HELD`（只读打开且含持有 PID）、`PRJ-STORE-CORRUPT`、`PRJ-RECOVERY-IGNORED-UNCOMMITTED`、`PRJ-RECOVERY-ORPHAN-DRAFT`、`PRJ-RECOVERY-DANGLING-OBJECTS`、`PRJ-SCHEMA-FUTURE`、`PRJ-FORMAT-LEGACY`、`PRJ-STALE-REVISION-REJECTED`、`PRJ-ARCHIVE-CONFLICT`、`PRJ-WRITE-AUTHORITY-LOST`。

### 5.1 打开与存储上下文（ProjectStore / ProjectStoreFactory）

```cpp
enum class OpenMode { Writable, ReadOnly };

struct OpenStoreRequest {
    std::filesystem::path path;        // .rwdesign 目录（或包文件——阶段 B 由 workflow 解包后转目录打开）
    OpenMode mode = OpenMode::Writable;
    std::shared_ptr<core::IDomainEventBus> eventBus;    // 装配注入（可空：测试/只读场景）
    std::shared_ptr<IDiagnosticsSink> diagnostics;      // 装配注入（可空：退化为开发诊断）
    ObjectCacheBudget cacheBudget;                     // 默认 256 MiB
};

struct RecoveryReport {                // PM-08 恢复诊断数据（呈现归 PM-15/ui）
    bool headIntegrityVerified = false;
    std::vector<std::string> ignoredStagingTxs;        // 未提交事务（忽略并报告）
    std::vector<std::string> orphanDraftFiles;         // 损坏/孤立草稿（含 .new/.bak 残留）
    std::uint64_t danglingObjectCount = 0;             // 悬挂对象（只读计数，不删）
    std::vector<core::DiagnosticRecord> diagnostics;
};

struct OpenStoreResult {
    std::unique_ptr<ProjectStore> store;               // 失败时为空
    bool writable = false;                             // 实际取得（请求 Writable 可能降级 ReadOnly）
    LockInfo lockInfo;                                 // 持有者 PID 等（PM-07）
    RecoveryReport recovery;
};

class ProjectStoreFactory {
public:
    // 打开协议服务侧（PM-02 五步中的②③⑤；①路径预检与④领域校验由调用方/workflow 编排）：
    //   ②目录形态与版本检查（formatId/schemaVersion/旧格式拒绝）
    //   ③加载与读校验（HEAD→闭包→对象完整性，惰性清单级）
    //   ⑤激活与恢复诊断（.staging/锁残留/孤儿草稿扫描 → RecoveryReport）
    // 前置：路径存在（不存在＝NotAProject；调用方新建项目走 createNew）
    // 错误：not-a-project/format-legacy/schema-future/store-corrupt/lock-held-by-other/
    //       media-read-only/access-denied
    static OpenStoreResult open(const OpenStoreRequest&);
    static OpenStoreResult createNew(const std::filesystem::path& dir,
                                     std::string_view displayName,
                                     /*同 eventBus/diagnostics 注入*/...);   // PM-01 存储侧：空项目骨架
};

class ProjectStore {                   // 存储上下文（ARCH §6.8 A7）——与界面会话分离
public:
    bool writable() const noexcept;                    // 唯一依据：本实例持有 OS 排他句柄（§9）
    LockInfo lockInfo() const noexcept;                // {pid, host, heartbeatUtc, isSelf}
    core::ProjectId projectId() const noexcept;
    SchemaInfo schema() const noexcept;                // {schemaVersion, formatId}
    std::filesystem::path canonicalPath() const noexcept;

    IProjectQueryPort&   query()    const noexcept;    // ②端口（含只读实例）
    ProjectCommandService& commands() const noexcept;  // ①端口（只读实例上提交即拒绝）
    DraftService&        drafts()   const noexcept;
    IResultArchivePort&  archive()  const noexcept;
    UndoRedoService&     undoRedo() const noexcept;

    // 关闭协议（PM-03"等待"选项的实现锚点）：
    std::uint32_t requestClose();        // 进入 Draining：拒绝新写；返回在途引用数（归档会话/在途事务/草稿落盘）
    bool closed() const noexcept;
    void subscribeClose(ICloseObserver&); // 全部在途完成并 flush 草稿、释放锁句柄后回调（一次性）
};
```

| 接口 | 前置条件 | 后置条件 | 副作用/线程 | 调用示例 |
| --- | --- | --- | --- | --- |
| `factory.open` | 进程内同规范路径未见 writable 打开 | 返回存储上下文或稳定错误；Writable 请求失败降级 ReadOnly 并携带 `PRJ-LOCK-HELD`（PM-07 不阻塞等待） | 线程安全（内部互斥）；读校验期间持读句柄 | workflow 打开向导第②③⑤步调用 |
| `createNew` | 目标目录不存在或为空 | 写入 project.json/lock/初始修订 r0＋M0＋HEAD | 同上；取消/失败不留半成品（PM-01：先在 `.staging` 组装再整体就位，失败清理目标目录） | 新建向导确认步 |
| `requestClose` | 上下文 Active | 状态→Draining；新写拒绝（`context-closing` 并入 ContextClosed 语义） | 幂等；可与在途归档并发 | ui 关闭对话框"等待"分支轮询 `closed()` |
| 析构/`closed` | — | 锁句柄释放（OS 层面写权限即时失效） | — | — |

**UI 会话与存储上下文生命周期图**（A7 承接；详细文字见 §9.7）：

```
 UI会话(项目A)────关闭/切换(确认对话框: 草稿三选+任务二选)────► 会话结束
      │ 持有                                              ▲
      ▼                                                    │ 全部在途完成+草稿flush+锁释放
 存储上下文A(写锁)──Active──┬─ 归档会话 r1,r2 (execution 注册的活跃运行持有引用)
                           ├─ 在途命令事务 (命令服务持有)
                           └─ 草稿落盘在途 (DraftService 持有)
      requestClose ──► Draining(拒绝新写) ──► pending==0 ──► Closed ──► 迟到写请求=拒绝+诊断
                                                                  │（防御性：若确有合法迟到归档，
                                                                  │ 必须先重新取得写权限 §6.8 A7）
```

### 5.2 查询端口（IProjectQueryPort，②端口所有者）

```cpp
struct RevisionView {                  // 值语义快照（不可变）
    core::RevisionId id; std::uint64_t seq;
    std::optional<core::RevisionId> parent; core::BranchId branch;
    std::vector<ObjectRef> objectRefs;               // 全量引用集（含 digest）
    ObjectRef metadataRef; std::string commandSummary; std::optional<InverseRef> inverse;
    bool hasUnresolvedPayload = false;               // 未知命令类型时 payload 不解析（§6.3）
};
struct BranchTip { core::BranchId id; std::string label;
                   core::RevisionId base, tip; };

class IProjectQueryPort {
public:
    virtual RevisionView head() const = 0;                       // HEAD 修订（重读 HEAD 文件）
    virtual std::optional<RevisionView> tryRevision(core::RevisionId) const = 0;
    virtual RevisionView revision(core::RevisionId) const = 0;   // 不存在抛 StoreError(store-corrupt)
        // 仅闭包内修订可取（闭包外＝未提交残留，一律 nullopt/抛——防误认已提交，自审项 A-6）
    virtual ProjectMetadataView currentMetadata() const = 0;     // 权威＝HEAD 引用版本（INV-M3）
    virtual std::optional<ProjectMetadataView> metadataAt(core::RevisionId) const = 0; // 历史浏览（R2）
    virtual std::vector<BranchTip> branchTips() const = 0;       // 自权威元数据
    virtual std::vector<RevisionView> branchHistory(core::BranchId,
                                                    std::uint32_t maxCount) const = 0; // 沿 parent 链
    // 对象读取（惰性＋缓存＋首次摘要校验）：
    virtual std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId, core::ContentVersion) const noexcept = 0;
    virtual std::vector<std::uint8_t> object(core::ObjectId, core::ContentVersion) const = 0;
    virtual std::vector<DraftInfo> listDrafts(core::BranchId) const = 0;   // 含 baseRevision 与过期标记
    virtual std::vector<RunInfo> listRuns(core::RevisionId) const = 0;     // 仅 finalize（有 manifest）的运行
    virtual std::filesystem::path runDir(core::RunId) const = 0;           // 供 reporting/evidence 读工件
};
```

| 接口 | 前置 | 后置 | 错误 | 示例 |
| --- | --- | --- | --- | --- |
| `head/currentMetadata` | 上下文未 Closed | 返回值快照，与后续写入隔离（不可变底层数据） | `context-closed` | 各域组装输入（PA-3：读快照） |
| `object` | (oid,cv) 存在于权威闭包或历史修订引用 | 字节与磁盘一致（首次校验摘要） | 引用缺失＝`store-corrupt`；未引用对象不可经此读取（走恢复报告） | modeling 读 RobotDesign |
| `branchHistory` | 分支存在于权威元数据 | 自 tip 沿 parent 的修订序列（不含其他分支提交） | 未知分支抛 | PM-12-S1（R2）浏览 |
| `listRuns` | — | 只含完整运行（manifest 在） | — | reporting 章节缺项判定 |

线程：全部并发只读安全；缓存互斥内部实现。所有权：返回值均为深拷贝或不可变视图，调用方自由持有；生命周期随上下文（Closed 后拒绝）。

### 5.3 命令端口（ProjectCommandService，①端口所有者）

#### 5.3.1 提交接口

```cpp
struct CommandEnvelope {
    core::BranchId branch;                             // 目标分支
    std::optional<core::RevisionId> expectedRevision;  // 缺省＝该分支当前 tip（提交期解析）
    std::string commandType;                           // 处理器注册 token
    std::uint32_t payloadFormatVersion;
    std::vector<std::uint8_t> payloadCanonical;        // 域 canonical 字节（不透明）
};

struct CommandStatus;   // 见下
struct CommandResult {
    CommandStatus status;                              // Committed | Rejected | Aborted | Failed
    std::optional<core::RevisionId> newRevision;       // Committed 时唯一非空
    std::optional<RevisionView> newHeadState;
    std::vector<core::ConfirmableFinding> findings;    // Rejected(确认相关) 时回传
    std::vector<core::DiagnosticRecord> diagnostics;   // 硬断言/编译失败定位（处理器产出）
    std::optional<StoreError> error;
};

class ProjectCommandService {
public:
    // 唯一写路径入口。全局串行（每存储上下文同时至多一个命令在执行——ARC-01）；
    // 编译类命令可与界面并行，但不与下一命令并发（ARCH §4.2 命令执行线程行）
    CommandResult submit(const CommandEnvelope&, ICommandInteraction* interaction = nullptr);
    std::vector<std::string> registeredCommandTypes() const noexcept;
};
// variant: Committed{revision} | Rejected{reason:
//   hard-assert-failed | confirmations-rejected | stale-revision | unknown-command |
//   invalid-payload | not-writable}
//  | Aborted{reason: canceled | interaction-lost | context-closing} | Failed{StoreError}
```

| 接口 | 前置 | 后置 | 错误 | 线程/权限 | 示例 |
| --- | --- | --- | --- | --- | --- |
| `submit` | 上下文 Active；type 已注册；payload 版本＝处理器受理版本（否则 `invalid-payload`） | **成功恰好产生一个新修订**（对象集/元数据/HEAD 一次提交）；失败/拒绝/中止**不产生任何修订**（暂存区可残留，恢复忽略） | 见 CommandStatus 全集 | 任意线程调用（内部转命令执行序列）；**要求 writable** | `ApplyRobotDesign`（S1）、`Undo`/`Redo`（§5.5）、`CreateBranch`、`RelinkExternalResource`（PM-09） |

#### 5.3.2 领域处理器（注入，防反向链接）

```cpp
struct ObjectWrite {                                   // 处理器声明的对象写入
    std::optional<core::ObjectId> objectId;            // 空＝申请新对象（project 分配）
    std::string objectTypeToken;
    std::vector<std::uint8_t> payloadCanonical;
    // ContentVersion 由 project 计算（SHA-256），处理器不自行申报
};
struct MetadataChange {                                // 分支表/显示名等显式变更（CreateBranch 等）
    std::optional<core::BranchId> createBranchWithBase; std::string label;
    std::optional<std::string> newDisplayName;         // R2 PM-11-S2
};
struct CommandPlan {
    std::vector<ObjectWrite> objectWrites;             // 变更集（新内容）
    std::optional<MetadataChange> metadataChange;
    std::vector<core::ConfirmableFinding> confirmableFindings;   // 待确认集（SA-15）
    bool requiresDualCompile = false;                  // 声明需要双编译（MDL-06 类命令）
    std::optional<std::string> inverseCommandType;     // 可逆性声明＋逆载荷（见下）
    std::optional<std::vector<std::uint8_t>> inversePayloadCanonical;
    std::string summary;                               // 命令摘要（随修订持久化）
};
enum class PrepareOutcome { Planned, RejectedHardAssert, RejectedInvalidInput };

class ICommandHandler {                                // L5 装配期注册（§6.5）；project 不链接业务单元
public:
    virtual ~ICommandHandler() = default;
    virtual std::string commandType() const = 0;
    virtual std::uint32_t currentPayloadVersion() const = 0;
    // prepare：基于 baseSnapshot（expectedRevision 解析的修订视图）做业务校验并产出计划。
    // 硬断言失败 → RejectedHardAssert ＋ diagnostics（就地阻止＋精确定位非法对象，MDL-06）。
    virtual PrepareOutcome prepare(HandlerContext& ctx, const CommandEnvelope&,
                                   const RevisionView& baseSnapshot, CommandPlan& out,
                                   std::vector<core::DiagnosticRecord>& diags) = 0;
};
```

`HandlerContext` 提供：`objectId()` 分配、`query()` 只读、策略/编译端口访问器（注入的 `IModelCompilePort` 与处理器自持的④端口句柄）、`interaction()`（确认回调）。处理器在 prepare 中完成策略校验（如行程上限：经④端口读 EngineeringPolicySet 阈值，超限产出 ConfirmableFinding——M-10；**project 不持有 4π 数值**）。

#### 5.3.3 确认交互回调（SA-15，纯接口防 Widgets 依赖）

```cpp
class ICommandInteraction {            // ui 实现；命令服务在命令执行线程调用
public:
    virtual ~ICommandInteraction() = default;
    virtual bool isAlive() const = 0;
    // 返回与输入一一对应的决策：confirmed(credential) 或 rejected；空 optional＝整体拒绝
    virtual std::optional<std::vector<core::ConfirmationCredential>>
        requestConfirmations(const std::vector<core::ConfirmableFinding>&) = 0;
};
```

线程调度契约：命令服务在**命令执行线程**同步调用；ui 实现内部 Marshal 到 UI 线程并等待结果（其详设冻结——P-PR-7 交接）；回调内**禁止**回调命令服务/查询端口以外任何写入口（防重入死锁：命令序列化槽被持有）。生命周期：服务持 `shared_ptr` 强引用直至调用返回；`isAlive()==false` 或调用抛出 → `Aborted(interaction-lost)`，不产生修订。拒绝行为：返回空或任一 rejected → `Rejected(confirmations-rejected)`，不产生修订（未确认阻止应用）。

#### 5.3.4 等待确认期间的资源占用（任务约束§五.4 明确项）

确认等待**占用命令执行槽**（保持全局串行：确认所针对的输入版本在提交前不得被其他命令改变——否则确认凭据失效）；**不占用任何事务资源**（尚未创建 .staging、未写任何文件、无额外句柄）。因此：等待期间草稿自动落盘不受影响（独立路径）；会话关闭请求到达时 → 命令服务主动取消等待（经 interaction 的 alive 检查＋取消通知，Aborted）——**不存在永久等待**（自审项 A-4；无超时自动确认，上游未定义超时语义）。

#### 5.3.5 处理器注册

```cpp
class HandlerRegistry {                // L5 装配期一次性注册，运行期只读
public:
    void registerHandler(std::unique_ptr<ICommandHandler>);      // 重复 commandType 注册边界拒绝
    ICommandHandler* find(std::string_view commandType) const noexcept;
};
```

#### 5.3.6 双编译最小契约（runtime 交接；P-PR-7）

```cpp
struct CompileRequest { const IProjectQueryPort& query;
                        const std::vector<ObjectWrite>& plannedWrites;   // 计划闭包
                        core::RevisionId baseRevision; };
struct CompileResult  { bool ok; std::vector<core::DiagnosticRecord> diagnostics; };
class IModelCompilePort {             // runtime 实现，L5 注入；project 侧仅编排
public: virtual ~IModelCompilePort() = default;
    virtual CompileResult compileWorkCellAndDwc(const CompileRequest&) = 0; };
```

语义：WorkCell 与 DynamicWorkCell **任一失败→`compile-failed`→不提交**（MDL-06 原子性）；编译产物为瞬态（不持久化，修订仅含权威参数化对象）；编译过程产生的临时文件只允许写入 `.staging/tmp/`，随事务清理（§6.6）。阶段 A 以测试桩实现验证编排；真实编译链随 runtime/modeling 交付（§13 交接）。

### 5.4 草稿服务（DraftService）

```cpp
struct SaveResult { bool ok; std::optional<StoreError> error; std::filesystem::path file; };

class DraftService {
public:
    // 保存草稿：不产生任何修订（PM-04"保存与应用分离"）。原子替换＋旧版保留：
    //   写 <module>.draft.json.new → flush → 旧文件改名为 .bak → 新文件就位 → 清理失败的 .new
    SaveResult save(const DraftDocument&);
    // 加载：路径归属三元组与内容校验；损坏→draft-corrupt 诊断＋返回 .bak 旧版（若有）
    std::optional<DraftDocument> tryLoad(core::BranchId, std::string_view moduleId,
                                         std::vector<core::DiagnosticRecord>& diags) const;
    DraftProjection summarize(core::BranchId) const;   // 多模块汇总投影（§8.5）
    SaveResult discard(core::BranchId, std::string_view moduleId);  // 放弃（写操作：只读模式拒绝）
    std::vector<DraftInfo> list(core::BranchId) const;
};
struct DraftProjectionItem { std::string moduleId; bool present;
    core::RevisionId baseRevision; bool stale;   // base ≠ 分支 tip
    std::string savedAtUtc; std::string origin; };
```

| 接口 | 前置 | 后置 | 权限 | 示例 |
| --- | --- | --- | --- | --- |
| `save` | 上下文 Active | 草稿落盘（autosave/manual 同一入口，origin 标记）；**无修订产生** | **要求 writable**（只读模式草稿不可保存——PM-07 禁编辑） | ui 定时器 60 s 触发（PM-04；定时器归 ui，本服务只提供落盘） |
| `tryLoad` | — | 完整草稿或空＋诊断 | 无（读） | 打开时草稿恢复（PM-04/PM-08） |
| `discard` | Active | 删除当前与 .bak | 要求 writable | 恢复横幅"放弃"（PM-15） |

### 5.5 撤销/重做服务（UndoRedoService）

```cpp
struct UndoRedoStatus { bool canUndo=false, canRedo=false;
    std::optional<std::string> undoSummary, redoSummary;   // 空历史稳定提示数据（PM-18）
    std::optional<StoreError> blockedReason; };            // 过期基线/不可逆/跨分支边界说明

class UndoRedoService {
public:
    UndoRedoStatus status(core::BranchId) const noexcept;
    // undo＝提交逆命令（envelope: type/payload 取自当前 tip 修订的 inverse 记录，
    // expectedRevision＝tip）→ 新修订；redo＝重新提交被撤销命令的原始载荷 → 新修订
    CommandResult undo(core::BranchId, ICommandInteraction* = nullptr);
    CommandResult redo(core::BranchId, ICommandInteraction* = nullptr);
};
```

撤销栈归属：**会话内按分支维护**（不持久化；重启后历史仍在——undo 能力由 tip 修订的 inverse 记录推导，redo 仅会话内有效）。语义详表见 §6.9。

### 5.6 归档端口（IResultArchivePort，供 execution）

```cpp
struct ArchiveRequest {
    core::TaskIdentity task;                 // execution 已按 RunRegistry 五元组核验后传入
    std::filesystem::path runDir;            // 归档位置：取自登记记录（不重新推导——§4.5 A8）
    std::string runKind, evaluationKey;      // 登记信息透传
};
struct ArchiveBatch  { std::vector<ArchiveItem> items; };   // {relPath, bytes}
struct ArchiveStatus { bool ok; std::optional<StoreError> error; };
class IResultArchivePort {
public:
    virtual ArchiveSessionRef begin(const ArchiveRequest&) = 0;
        // 校验：task.project == 本上下文 projectId（防跨项目写入，AT-10）；
        //       上下文 Active（Draining 拒绝新 begin，§9.7）；runDir 落在 results/ 白名单内；
        //       同 runId 已有 manifest：与本次请求摘要比对（幂等 §10.1）
    virtual ArchiveStatus writeBatch(ArchiveSessionRef, const ArchiveBatch&) = 0;  // 分批写（partial）
    virtual ArchiveStatus finalize(ArchiveSessionRef, const RunManifest&) = 0;    // manifest 原子发布=完整
    virtual void abandon(ArchiveSessionRef, ArchiveEndReason) = 0;
        // Completed | Canceled | Failed | ForceTerminated —— 一律结束归档责任（§10.1）
};
```

**project 不根据当前 HEAD 拒绝历史归档**：runDir 绑定原修订（登记记录），HEAD 前进不影响写入合法性（当前性归 evidence，CON-02/05——自审项 A-2 的反面保证）。**不把"运行完成"等同"归档完成"**：完成事件触发 begin→…→finalize 序列，任一环节失败按 §10.1 报告与结束责任。

### 5.7 外部资源固化服务（阶段 B 落地；接口先冻结）

```cpp
struct SolidifyRequest { std::string externalRefId;      // 草稿外部引用记录 id
    core::ObjectId targetResourceObject;                 // 固化副本对象（新分配或替换版本）
    std::uint64_t budgetBytes; };                        // 预算（io BudgetGuard 执行）
struct SolidifyResult  { bool ok; core::ContentVersion materializedVersion;
    std::optional<core::ContentHash256> sourceChangedDuringCopy;  // 复制期间源变化检测
    std::vector<core::DiagnosticRecord> diagnostics; };
// 流程：io 安全读取（SafePath/预算）→ 前后哈希比对（变化→ Changed 诊断，NFR-REL-04）
//   → canonical 字节入对象库（随下一次命令提交发布——固化经命令进入事务，CON-03）
//   → ExternalResourceRecord.state = materialized（引用保护：此后对象受不可变保证）
```

责任划分：**io**＝解析与安全读取、缺失/变化检测实现；**project**＝存储发布与引用保护（PM-01 固化口径）。缺失/变化/读取失败/预算超限分别由 io 产出诊断，project 透传并中止固化。

### 5.8 另存为/包/升级（ISaveAsService/IPackageService；阶段 B 落地，接口先冻结）

```cpp
struct SaveAsOptions { std::filesystem::path targetDir; bool copyResults, copyReports, copyDrafts;
                       std::string newDisplayName; };     // 勾选记忆归 ui（PM-05）
struct PackageExportOptions { std::filesystem::path targetFile;  // .rwpack
                              bool includeResults, includeReports, includeDrafts;
                              ProgressCallback onProgress; CancelCallback onCancel; };
// export：一致数据视图＝开始时快照 HEAD 闭包＋所选树清单；对象/修订不可变＋drafts 单文件原子替换
//   ⇒ 后台写入并存期间导出仍一致（D-17）；ZIP 编解码与逐字节还原校验经 io（P-PR-5）
// import：解包到临时目录（io 校验：预算/路径穿越/哈希全量校验）→ 通过后整体发布到用户选择的目标
//   目录；失败/取消→不留目标目录、清理临时区（PM-05）
class ISchemaUpgrader { public: virtual ~ISchemaUpgrader() = default;
    // 输入源目录只读；输出写入新目录（升级失败不破坏源项目，§8.11）；
    // 产物按 PM-02 打开协议进入；升级器版本登记于新 project.json
    virtual UpgradeResult upgrade(const std::filesystem::path& sourceDir,
                                  const std::filesystem::path& targetDir) = 0; };
```

---

## 6. 命令、确认、撤销与重做

### 6.1 命令生命周期与串行化范围

```
submit(envelope)
 → [S1] 形式校验（type 已注册 / payload 版本受理 / branch 存在于权威元数据 / writable）
 → [S2] 基线解析与并发校验（expectedRevision vs 分支 tip，§6.2）
 → [S3] 处理器 prepare（业务校验＋计划：对象写入/元数据变更/待确认集/编译声明/逆命令）
 → [S4] 确认放行（待确认集非空且 interaction 有效 → 回调；拒绝/失效 → 终止，无修订）
 → [S5] 双编译（requiresDualCompile → IModelCompilePort；失败 → 终止，无修订，§6.6）
 → [S6] 事务提交（七步协议 §7.1：暂存→校验→发布→HEAD 切换）→ 恰好一个新修订
 → [S7] 事件发布（RevisionCommitted → DependencyInvalidated；失败不回滚，D-18）
 → 返回 CommandResult（Committed{newRevision}）
```

串行化范围：**每存储上下文一个命令执行槽**（互斥）。S3～S6 全程持槽；S4 等待期间持槽但无事务资源（§5.3.4）；编译（S5）可与 UI 并行但不能与下一命令并发（ARCH §4.2）。修订序号在 S6 内分配（权威 HEAD 的 seq+1），保证单调（ARC-01）。

### 6.2 expectedRevision 并发校验

- `expectedRevision` 缺省＝分支当前 tip（提交期解析后等同显式）；显式值 ≠ tip → `Rejected(stale-revision)`，附当前 tip 与差异定位数据（哪个对象/哪个分支前进）。
- 因全局串行，"解析→提交"之间无竞争窗口；该检查同时承接 PM-04 草稿基线冲突（draft.baseRevision 作为 expectedRevision 传入）与 PM-18 撤销的过期基线（§6.9）。
- **StaleRevisionRejected 触发**：应用草稿时 draft.baseRevision ≠ 分支 tip（PM-04 RV-10）。**用户可继续编辑的条件**：草稿完整保留（origin=apply-retained）、只读可编辑（编辑是内存态）、再次应用前基于当前版本重新调整（冲突定位数据＋域编辑器合并能力——归域，project 提供新旧修订引用）。

### 6.3 未知命令、非法输入、过期修订的处理

| 情形 | 处理 | 结果 |
| --- | --- | --- |
| 未知 commandType（未注册） | 提交边界拒绝 | `Rejected(unknown-command)`，无修订 |
| 非法输入（payload 版本不受理/结构性非法） | 提交边界或 prepare 拒绝 | `Rejected(invalid-payload)`，处理器在 prepare 内产出逐项诊断（域口径） |
| 硬断言失败（物性/限位区间/工作范围有限性） | prepare 返回 RejectedHardAssert | `Rejected(hard-assert-failed)`＋精确定位诊断（就地阻止，MDL-06） |
| 过期修订（expectedRevision≠tip） | S2 拒绝 | `Rejected(stale-revision)`（含 StaleRevisionRejected 稳定码 `PRJ-STALE-REVISION-REJECTED`） |
| 历史修订中的未知命令类型（加载侧） | 修订可读、payload 不解析 | RevisionView.hasUnresolvedPayload=true（历史浏览 PM-12-S1 不受影响；不阻断打开协议——升级/浏览仅消费 summary 与对象集） |

### 6.4 命令载荷序列化与版本策略

载荷＝处理器域内 canonical 字节，project **不解释**（透传存储）。版本三元组：`commandType`（注册 token）＋`payloadFormatVersion`（处理器演进）＋负载字节。规则：处理器受理版本集合显式声明（≥当前，可受理历史版本以保历史兼容）；不受理的历史载荷在浏览侧标记 unresolved（§6.3 末行）；负载格式演进归域任务卡登记（与 core §6.3 三层分工一致：project 存字节与版本戳，io 管外部编码，域管语义）。

### 6.5 领域处理器的注册与注入（防反向链接的实现）

L5 应用壳装配期：实例化各业务处理器（modeling 的 `ApplyRobotDesign`、selection 的 `ApplySelection`、project 自有的元数据命令处理器等）并注册进 HandlerRegistry；同时注入 `IModelCompilePort`（runtime 实现）。**project 编译期仅依赖 core**（＋经适配器的 diagnostics），业务单元零链接（ARC-02、NFR-MNT-04：命令服务不是转发包装器——它拥有事务、串行、确认编排、修订与事件这些处理器没有的平台语义）。project 自有的元数据命令（CreateBranch/SetPrimaryScheme/项目改名〔R2〕）由 project 内置处理器实现（type：`project.create-branch` 等），是唯一"project 即处理器"的命令族。

### 6.6 双编译事务编排

- 编译输入＝计划闭包（baseSnapshot 引用集＋plannedWrites 合成的对象集视图，经 CompileRequest 提供给端口）；runtime 只读快照语义（不写项目）。
- **任一失败→不提交**：CompileResult.ok==false → `Rejected(hard-assert-failed)` 不适用（非断言），归 `Failed(compile-failed)`＋诊断；无修订、暂存区未创建（编译先于 S6）。
- **临时资源隔离**：编译链需要落盘的中间产物只允许写 `.staging/tmp/`（CompileRequest 携带专用临时目录）；随事务/命令结束清理；失败残留由启动恢复扫描清除。**回滚语义**：因发布（S6 第 4 步）在编译成功之后才开始，失败时无任何已发布内容需要回退——"回滚"＝丢弃内存计划与临时目录（原子性来自顺序而非撤销，MDL-06 口径）。
- project 不重写物性公式、碰撞策略或编译算法（N-1/N-2/N-3）。

### 6.7 可确认诊断放行流（SA-15 详设）

| 要素 | 设计 |
| --- | --- |
| ConfirmableFinding 数据来源 | 处理器 prepare 产出（如 MDL-06④ 行程上限：处理器经④端口读 EngineeringPolicySet 阈值→超限→构造比较型诊断 `ConfirmableFinding`，core DiagData 类型；码值来自 diagnostics 注册表） |
| 确认绑定 | 确认记录四元组随命令摘要持久化（CommandRecord.confirmations[]）：`{findingDigest（ finding 内容摘要）, policyContentId（产生该 finding 的已解析策略内容身份——CON-06）, commandDigest（本次命令载荷摘要）, baseRevisionId（确认所针对的输入版本）}`——四者任一不符的确认凭据视为失效（编译前复核绑定） |
| 回调线程/生命周期/拒绝 | §5.3.3：命令执行线程同步调用＋ui Marshal；强引用；失效→Aborted(interaction-lost)；拒绝→Rejected(confirmations-rejected) |
| 等待期间资源 | §5.3.4：占执行槽、零事务资源、可被会话关闭取消 |
| 输入变化/会话关闭/取消/回调失效 | 输入变化不可能（槽内无并发写，§6.1）；会话关闭→Aborted(context-closing 路径)；用户取消→Aborted(canceled)；回调失效→Aborted(interaction-lost)。均无修订、无暂存残留 |
| 确认记录进入摘要 | S6 提交时写入 command.json（§4.4.4）；随修订永久留痕（PM-12-S1 可浏览） |
| 非交互提交 | interaction==nullptr 且有待确认集 → `Rejected(confirmations-unresolved)`（测试/后台通道，"未确认则阻止应用"） |

### 6.8 命令摘要、对象变更集与事件的关系

```
CommandPlan.objectWrites（变更集：新增/改版对象） ─┐
                                                  ├─► 修订引用集（全量）＝ base 引用集 −被替换版本
CommandPlan.metadataChange（元数据新版本）─────────┤        ＋新写入对象；ProjectMetadata 新版本入集
                                                  │      （修订＝命令摘要＋对象集引用，ARCH §6.3）
命令摘要（summary＋payload＋inverse＋confirmations）─┘
提交成功后事件（FIFO 同发布者，core Events 契约）：
  RevisionCommitted{project,branch,revision,parent} → DependencyInvalidated{...同修订}
  ——事件仅身份（core D-09）；失效范围由 evidence 经②查询端口按切片计算（CON-05）；
    事件发布失败不回滚已提交修订（重试一次＋开发诊断，D-18）
```

### 6.9 撤销/重做语义详表

| 问题 | 设计 | 依据 |
| --- | --- | --- |
| 可撤销命令的记录 | CommandRecord.inverse＝逆命令表达（处理器在 prepare 时基于 before/after 产出：类型＋负载；不可逆命令〔如 CreateBranch〕无 inverse——**可逆性是处理器声明的事实**，不由 project 推断） | PM-18 |
| 撤销＝新修订 | undo 提交逆命令（expectedRevision＝tip）→ 恰好一个新修订；历史不可变 | PM-18"不改写历史" |
| 撤销栈归属 | **分支**（会话内每分支一个栈；不持久化——重启后 canUndo 由 tip 的 inverse 记录推导，redo 仅会话内） | 本文决策 D-11 |
| 新命令对重做历史的影响 | 会话内该分支提交任何新命令（含 undo 之外的）→ redo 栈清空（标准语义；redo＝重放原始载荷产生新修订） | PM-18 统一命令契约 |
| 空历史 | tip 修订无 inverse（首修订/不可逆命令）→ canUndo=false＋稳定提示数据 | PM-18"空历史明确稳定提示" |
| 过期基线 | tip 与栈记录不一致（会话外变更）→ blockedReason=stale-revision | PM-04/18 同契约 |
| 跨分支 | 撤销只作用于**活动分支 tip**；栈按分支隔离；切换分支不迁移栈（各分支栈保留在会话） | PM-18"跨分支行为遵循统一命令契约" |
| 与草稿局部撤销的边界 | project 撤销＝项目命令级（产生修订）；未应用草稿内编辑的局部撤销（REQ-11 需求集撤销等）归 ui DraftController＋域编辑器，**不经命令服务、不产生修订**（N-10） | PM-18/REQ-11 |
| 复合修改整体撤销 | 器件回填（SEL-10）、模型应用（MDL-06）＝单命令单 inverse 记录 → 一次 undo 整体回退（对象引用集级） | SEL-10/MDL-06 |
| 可逆范围声明 | **引用级完全可逆**（对象引用集恢复到旧版本——物理字节不删除，GC 明确不做）；外部副作用：固化的资源副本为新增（无破坏性外部写）；不存在报告已发出等不可逆外部效应（报告渲染只读修订）。不可逆命令必须无 inverse（如 CreateBranch——分支删除不在范围） | PM 明确不做/附录 B |

### 6.10 命令应用与确认时序图

```
 调用方(ui/业务)   ProjectCommandService      ICommandHandler(域)   ICommandInteraction(ui)  IModelCompilePort   TxEngine/对象库
      │  submit(envelope) │                        │                     │                     │                  │
      │──────────────────►│ S1 形式校验             │                     │                     │                  │
      │                   │ S2 expectedRevision vs tip（不符→Rejected(stale)）              │                  │
      │                   │──prepare(ctx,envelope,baseSnapshot)──►│      │                     │                  │
      │                   │◄─ Planned{writes,findings,compile,inverse,summary} ──┘           │                  │
      │                   │ S4 findings 非空:                                        │                  │
      │                   │──requestConfirmations(findings)──────►│(Marshal UI,确认对话框)   │                  │
      │                   │◄─ credentials / 拒绝 / 失效 ───────────┘                        │                  │
      │                   │ S5 requiresDualCompile:                                  │                  │
      │                   │──compileWorkCellAndDwc(planned closure)────────────────────────►│                  │
      │                   │◄─ ok / fail+diagnostics ───────────────────────────────────────┘                  │
      │                   │ S6 七步事务: 暂存→校验→发布→HEAD 原子切换 ──────────────────────────────────────────►│
      │                   │◄─ committed(revId, seq) ────────────────────────────────────────────────────────────┘
      │                   │ S7 bus.publish(RevisionCommitted→DependencyInvalidated)（失败不回滚）
      │◄─ CommandResult ──│ Committed{newRevision}
```

---

## 7. 事务发布与崩溃恢复

### 7.1 七步协议（NFR-REL-01 / ARCH §6.4 SA-09 逐步化）

单一提交根＝HEAD（A2）：修订、ProjectMetadata、对象全部经同一提交点；project.json 不参与。

| 步 | 动作 | 读/写文件 | 持锁状态 | 可见性（其他读者） | 失败结果 | 恢复动作 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 准备 | 生成 tx-id；建 `.staging/<tx-id>/`；分配 RevisionId/seq（权威 HEAD seq+1）；处理器计划入内存 | 写 .staging 目录项 | 写权限已校验（事务开始时——§6.8 失权防护） | 无 | 建 目录失败（权限/磁盘）→ Failed(write-rejected/disk-full)，无残留或仅空目录 | 启动清理 |
| 2 暂存 | 对象字节、manifest.json、command.json、新 ProjectMetadata 全部写入 `.staging/<tx-id>/`；每文件 `FILE_FLAG_WRITE_THROUGH` 写＋`FlushFileBuffers` 后关闭 | 写 .staging 文件 | 同上 | 无（.staging 不对外） | 写中途失败/磁盘满 → Failed(disk-full)；残留部分文件于 .staging | 启动扫描忽略＋恢复诊断（PM-08） |
| 3 验证 | 重读暂存文件：size＋SHA-256 全量校验（含"对象字节→ContentVersion 名一致性"） | 读 .staging | 同上 | 无 | 校验不符（含注入位翻转）→ Failed(store-corrupt)＋开发诊断（磁盘/内存错误怀疑） | 残留同上 |
| 4 内容发布 | `.staging` 内容就位正式路径：`publishNew`（目标不存在→rename；已存在→摘要一致视为共享，不一致→store-corrupt）——revisions/<rev-id>/ 与 objects/ 逐项发布；**只增不改，部分就位不破坏旧版本**（HEAD 仍指旧内容） | rename（跨 .staging→正式目录，同卷） | 同上 | 新对象/新修订目录**存在但未提交**（闭包外，§4.5） | 某项 rename 失败 → Failed(write-rejected)；已就位项保留（悬挂，不回滚——旧版本完整性不受影响） | 启动扫描：闭包外内容＝未提交残留，忽略＋诊断；**不删除全部孤儿对象替代恢复**（GC 范围外） |
| 5 提交指针切换 | 写 `HEAD.new`（完整 HeadRecord）→ flush → `MoveFileExW(HEAD.new→HEAD, REPLACE_EXISTING|WRITE_THROUGH)`（§7.2）——**唯一提交点**；切换后重读 HEAD 自校验 manifestDigest | 写项目根 HEAD | 同上 | **此后本次修订视为已提交** | 切换失败 → Failed(write-rejected)；内容已发布但 HEAD 未变 → 同第 4 步残留语义 | 同上；崩溃于切换前后以 HEAD 实际内容为准（字节级判定） |
| 6 事件发布 | bus.publish(RevisionCommitted→DependencyInvalidated)（FIFO；失败重试一次＋开发诊断，**不回滚**——D-18） | 无文件写 | 同上 | 订阅方刷新 | 失败不影响提交事实 | 订阅方经②端口重读当前状态自愈（事件非数据源，core D-09） |
| 7 清理 | 删除 `.staging/<tx-id>/`（含 tmp）；清理失败仅开发诊断 | 删 .staging | 同上 | 无 | 删除失败 → 已提交状态不受影响（清理失败不得破坏已提交版本） | 下次启动清理；残留不参与任何读取路径 |

**暂存与正式目录之间的发布顺序**：先全部内容（对象→修订清单，顺序：对象先行、修订清单最后——清单是内容的引用者，反向会出现"清单在而对象缺"的中间可见态）后 HEAD；**对象、修订清单与 ProjectMetadata 如何共同提交**：三者同属第 4 步发布集合＋第 5 步单一 HEAD 切换——ProjectMetadata 是对象之一（引用者修订清单），无需独立提交动作。

### 7.2 Windows 文件操作保证（对照 Microsoft Learn 文档口径；不凭记忆超诺）

| 操作 | API 与标志 | 文档语义（摘述） | 本文使用与承诺边界 |
| --- | --- | --- | --- |
| 暂存写持久化 | `CreateFileW(FILE_FLAG_WRITE_THROUGH)` ＋ `FlushFileBuffers` | Write-Through：写直达介质未经系统缓存；FlushFileBuffers："将指定文件的缓冲区刷写到磁盘"（Microsoft Learn：FlushFileBuffers） | 暂存文件关闭前数据落盘；**这是写入持久性保证**（第 2 步） |
| 正式发布 | `MoveFileExW`（无 REPLACE，目标须不存在） | 同卷 rename 为文件系统元数据操作，对其他进程的打开/查询呈现原子可见（要么旧名要么新名，无中间态） | 只增发布原语 `publishNew`：目标已存在时不覆盖（校验共享）；**原子替换保证**（可见性层面，第 4 步） |
| HEAD 原子替换 | `MoveFileExW(MOVEFILE_REPLACE_EXISTING\|MOVEFILE_WRITE_THROUGH)` | MOVEFILE_WRITE_THROUGH："函数在文件实际移动到磁盘后才返回……保证以复制＋删除方式执行的移动在返回前刷盘"（Microsoft Learn：MoveFileEx） | HEAD.new 已 flush 后替换；对读者呈现旧/新完整 HEAD 之一；WRITE_THROUGH 覆盖"复制＋删除"路径的刷盘；**同卷 NTFS rename 本身的崩溃原子性依赖 NTFS 元数据日志**——本文不宣称超出文档声明的断电保证，残余窗口由恢复协议兜底（PM-08） |
| 路径规范化 | `GetFinalPathNameByHandleW` | 返回系统最终路径（解析符号链接/subst/盘符挂载） | 存储实例身份（§9.3） |
| 互斥 | `CreateFileW` 共享模式（dwShareMode=FILE_SHARE_READ） | 其他进程请求写访问（GENERIC_WRITE）→ `ERROR_SHARING_VIOLATION`；读访问（GENERIC_READ＋FILE_SHARE_WRITE 共享声明）成功 | 写锁机制（§9.1） |
| 不使用 | `ReplaceFileW` | 保留目标 ACL/属性语义 | HEAD/草稿无需保留属性，MoveFileEx 语义足够（少一个依赖面） |

**原子替换与写入持久性是不同保证**（任务约束§五.5 明确项）：原子替换＝并发可见性（其他进程见旧或新，无半文件）——覆盖 HEAD 切换、草稿/manifest 替换、results manifest 发布；写入持久性＝崩溃后内容仍在——覆盖暂存写（write-through＋flush）；**rename 自身的崩溃持久性**：NTFS 日志化元数据操作，`MOVEFILE_WRITE_THROUGH` 仅对"复制＋删除"形态承诺刷盘——本文对"断电后 HEAD 必为新值"不作承诺，仅承诺"断电后 HEAD 为旧值或新值之一且完整"（这正是 PM-08"崩溃后已提交修订与 HEAD 字节不变"的语义：已提交者完整，未提交者忽略）。以上口径以实现期对照官方文档复核为 PRJ-T02 完成条件之一，并以故障注入（§11、AT-13）实证。

### 7.3 已发布但未提交内容的识别

规则（§4.5 闭包）：HEAD 切换（第 5 步）完成前，一切第 4 步已就位的对象/修订均属"闭包外"＝未提交。识别与处置：打开/恢复扫描计算闭包，闭包外修订目录与对象计入残留清单（恢复诊断）；查询端口与历史浏览**拒绝**提供闭包外修订（自审项 A-6 反例测试 PRJ-TX-9）；悬挂对象不删除（GC 范围外，PM-08-S1 只读报告 R2）。

### 7.4 事务提交与恢复状态机

```
                       ┌──────────────────────────── 崩溃/失败（任一步）────────────────────────────┐
                       │                                                                          │
 Idle ─► Preparing ─► Staged ─► Verified ─► Published ─► Committed ─► EventsPublished ─► Cleaned   │
  │        tx-id/seq     写入+flush     全量哈希校验   只增就位        HEAD 原子切换     FIFO 事件      删 .staging
  │                                                                                              │
  │   提交点 = Committed（HEAD 切换完成）                                                          │
  ▼   Committed 之后的一切失败（事件/清理）不回滚：已提交修订字节不变（D-18）◄──────────────────────┘
恢复（启动，PM-08 顺序）：
  ①扫描 .staging 残留 ──► 忽略未提交内容＋恢复诊断（PRJ-RECOVERY-IGNORED-UNCOMMITTED）
  ②锁文件残留读取（接管路径，§9.4）──► 恢复诊断（不据此判活）
  ③孤儿草稿（.new/.bak/损坏）──► 报告（PRJ-RECOVERY-ORPHAN-DRAFT；可恢复旧版）
  ④校验 HEAD 引用闭包完整性（对象/清单哈希）──► 失败＝store-corrupt（定位到文件）
  ⑤闭包外对象计数 ──► 开发诊断（只读，不删）
```

### 7.5 崩溃一致性要点复述

崩溃后已提交修订与 HEAD **字节不变**（AT-11/13 验收）：HEAD 未切换即未提交（第 4 步残留无害且可识别）；已切换即已提交（第 6/7 步失败不影响字节）。清理失败不破坏已提交版本（第 7 步独立于提交点）。恢复不依赖"目录存在"判定提交（§7.3）。"未完成任务显示已中断"的数据由 execution 状态持久化/RunRegistry 恢复提供（project 提供扫描与诊断通道——NFR-REL-03 归 execution）。

### 7.6 故障注入矩阵（提交边界全覆盖；实现载体 §11）

| # | 边界 | 注入方式（testkit FaultInterceptor 经 `IFileOps` 接缝） | 前置 | 预期 | 观测点 | 追踪 |
| --- | --- | --- | --- | --- | --- | --- |
| F1 | 准备失败 | `.staging` 目录创建失败（权限/只读） | writable 项目 | Failed(write-rejected)；无修订；HEAD 不变 | StoreError.code；HEAD 字节比对 | NFR-REL-01 |
| F2 | 暂存写中途失败 | 第 N 字节写失败（write-chunk occurrence） | 同上 | Failed(disk-full/write-rejected)；旧版本完整 | HEAD 字节不变；.staging 残留部分文件 | AT-13 |
| F3 | 验证失败 | 暂存文件位翻转（写后篡改） | 暂存完成 | Failed(store-corrupt)；无发布 | 校验诊断含文件路径 | NFR-REL-01 |
| F4 | 发布部分失败 | 第 k 个 rename 失败 | 验证通过 | Failed；已就位项悬挂；旧版本完整 | 闭包外计数＞0；HEAD 不变 | AT-13 |
| F5 | HEAD 切换失败 | MoveFileEx 失败（占位目标句柄等） | 发布完成 | Failed；内容已发布未提交（可识别） | 重开后历史不含该修订（PRJ-TX-9） | NFR-REL-01/PM-08 |
| F6 | 事件发布失败 | 总线 fake 抛出 | 已提交 | 修订保留；重试一次＋开发诊断 | 修订仍在；查询可见 | ARCH §6.4 |
| F7 | 清理失败 | 删除 .staging 失败 | 已提交 | 已提交状态完好；残留仅诊断 | HEAD/修订字节比对 | 同上 |
| F8 | 各步进程崩溃 | TestProcessRunner kill 于 T1～T7 每步后 | 各步前置 | 重启：已提交字节不变；未提交忽略＋恢复诊断 | 恢复报告字段逐项断言 | AT-11/13 |
| F9 | 归档边界 | 批次写失败/manifest 发布失败/同 run 重投递 | 归档会话 | §10.1 表 | manifest 缺失＝不完整；幂等/冲突分支 | TASK-03/CON-04 |
| F10 | 草稿边界 | 草稿写中途崩溃/旧版存在 | 有草稿 | .new 残留；.bak 可恢复 | tryLoad 诊断＋旧版恢复 | PM-04/08 |

---

## 8. 草稿与项目生命周期

### 8.1 草稿与已应用修订的数据结构区别

| 维度 | 草稿（DraftDocument，§4.4.5） | 已应用修订（RevisionManifest，§4.4.2） |
| --- | --- | --- |
| 身份 | (branchId, moduleId) 二元组（同分支同模块仅一份） | RevisionId（全局唯一、单调 seq） |
| 状态 | 可变（覆盖写）；携带 baseRevisionId 声明基线 | 不可变（只增不改） |
| 内容 | 模块负载＋外部引用记录（可含非法中间态——编辑中） | 完整对象引用集（已通过断言/确认/编译的权威状态） |
| 事务 | 单文件原子替换（不走七步协议；PM-04"保存仅落 drafts/"） | 七步事务＋HEAD 切换 |
| 撤销 | 局部撤销归 ui DraftController/域编辑器（N-10） | 命令级撤销＝新修订（§6.9） |

### 8.2 定时落盘与手动保存

默认 **60 s** 定时落盘（PM-04；周期可调 60–600 s 为 R2 子项 PM-04-S1，设置项归用户级——ui/PM-14，project 仅接收调用）；定时器归 ui 会话层（DraftController），触发 `drafts().save(origin=autosave)`。手动保存同一入口（origin=manual）。两者均不产生修订（PM-04"保存与应用分离"）。落盘失败（磁盘满/只读）→ 返回错误＋诊断；会话"未保存"标记的数据依据＝save 成功与否（ui 消费）。

### 8.3 应用与 StaleRevisionRejected

应用＝以草稿负载构造命令（域组装）→ `submit(expectedRevision=draft.baseRevisionId)`。基线冲突路径见 §6.2：拒绝后草稿保留（origin=apply-retained），用户可继续编辑（数据完整），重新应用前需基于当前 tip 调整；冲突定位数据（当前 tip、前进的修订摘要、涉事对象差异）随 Rejected 返回（域负责字段级合并呈现）。

### 8.4 草稿恢复、损坏诊断与旧草稿保留

打开时 `listDrafts`＋`tryLoad`（PM-04 恢复；PM-15 横幅呈现归 ui）。损坏处置：JSON 不可解析/归属三元组不符/路径与内容不一致 → `draft-corrupt` 诊断；**旧草稿保留**：`.bak`（上一版）优先恢复；`.new` 残留＝保存崩溃现场→丢弃 `.new`、保留 current/.bak 并报告（F10）。孤儿草稿（无对应分支）→ RecoveryReport.orphanDraftFiles（PM-08）。

### 8.5 多模块草稿汇总投影

`DraftProjection`（§5.4）：按分支列出各模块——present/baseRevision/stale（base≠tip）/savedAtUtc/origin。会话级"未应用修改"标记（标题 `*`，PM-11）由 ui 基于投影中 present 项计算；"跨模块草稿汇总视图"（PM-04）＝该投影＋各域摘要（域提供模块级描述符，注册 token 对应）。project 不解释模块内容。

### 8.6 放弃草稿、只读打开与项目切换

放弃＝`discard`（写操作，只读模式拒绝＋诊断——PM-07 禁编辑的存储侧落实）；只读打开下草稿可查看（list/tryLoad）不可保存/放弃。项目切换（PM-03）：切换前草稿三选（保存/放弃/取消——ui 编排）；确认后旧项目上下文进入 Draining（§9.7）——在途草稿保存完成、在途归档完成后释放；新项目上下文独立打开。

### 8.7 打开协议的服务侧（PM-02 五步的 project 部分）

| 步 | 归属 | project 提供 |
| --- | --- | --- |
| ① 路径预检 | workflow/ui | `factory.open` 的入口参数（不存在→not-a-project） |
| ② 目录形态与版本检查 | **project** | formatId/schemaVersion 校验；旧格式→`format-legacy`（稳定码＋原文件不动）；未来版本→`schema-future`（含当前/项目版本数据，PM-06） |
| ③ 加载与读校验 | **project** | HEAD→闭包→（清单级＋按需对象级）哈希校验；失败定位到具体文件（PM-02"失败显示具体文件"） |
| ④ 领域校验 | 各域处理器/workflow | project 不做（N-1） |
| ⑤ 激活与恢复诊断 | **project**（数据）/ui（呈现） | RecoveryReport（§7.4 恢复顺序） |

**激活前失败不影响当前项目**：`factory.open` 完整构造候选上下文（含恢复扫描）后才返回；失败时当前已打开项目的存储上下文未被触碰（open 从不写当前项目；新项目只读探测＋锁获取仅作用于目标目录）。**正常打开与崩溃恢复的区别**：正常＝.staging 空、锁无残留，恢复报告全空、直接激活；崩溃恢复＝发现残留（staging/锁/孤儿草稿/悬挂对象）→ 先产出恢复诊断与可恢复项（草稿恢复入口）再激活；锁接管路径见 §9.4。

### 8.8 另存为（PM-05 存储侧；阶段 B 落地）

协议：目标目录在 `.staging` 同卷临时位完整复制 → 校验闭包 → **换新 projectId**（core 生成新 ProjectId；对象/修订/分支 ID 全部原样保留——随机全局唯一，跨副本引用语义不变）→ 改写点仅三处：`project.json.projectId`、`HEAD.projectId`（HeadRecord 字段）、drafts 的 `projectId`（逐文件重写）→ 按 PM-02 打开协议进入新目录（D-16）。**results 中已归档 RunManifest 的 taskIdentity 保持原五元组**——它是历史运行事实的记录，不因复制改写（读取侧不校验 manifest 的 projectId 归属；新归档仍以新 projectId 校验）。取消/失败清理临时位，不留半成品。

### 8.9 选择性复制（results/reports/drafts 勾选）与引用完整性

复制前扫描被勾选 reports 的工件引用（runId 清单）：引用的 run 不在勾选集 → 该报告**不复制**＋诊断列出（防止"报告在而证据缺"的悬空引用）；drafts 独立可复制（重写 projectId）；results 复制保持完整性（整 run 目录＋manifest 原子出现）。对象库与修订历史**无条件全量复制**（它们是项目状态本体，勾选项不影响）。

### 8.10 项目包导入导出（PM-05 存储侧；阶段 B 落地）

**导入**：解包到临时目录（io：预算/路径穿越防护/逐字节哈希还原校验，NFR-SEC-01/02）→ 于临时目录执行打开协议全量校验（②③⑤）→ 通过后整体发布（rename 到用户选择的目标目录）→ 按打开协议进入；**失败或取消 → 不留目标目录、清理临时区**（PM-05）。**导出**：一致数据视图＝开始时快照（HEAD 闭包＋所选树清单）——对象/修订不可变＋drafts/manifest 单文件原子替换 ⇒ 与后台写入并存时仍一致（D-17）；后台进度可取消（取消即清理临时输出）。ZIP 编解码归 io（P-PR-5）。

### 8.11 格式升级（PM-06 / NFR-DEP-04）

| 情形 | 判定 | 行为 |
| --- | --- | --- |
| 旧格式（.rwproj 等） | 扩展名/魔数不符 `rwdesign` | 稳定只读拒绝 `format-legacy`＋诊断码；**原文件不动**；不提供读取 |
| 未来 schemaVersion | `schemaVersion > 当前支持` | 只读拒绝 `schema-future`；诊断含当前支持版本/项目版本/升级工具入口数据（不自动升级，PM-06） |
| 已发布新格式的旧项目 | 升级器已注册该步距 | `ISchemaUpgrader::upgrade(sourceDir→targetDir)`：**源目录只读打开**、输出新目录、失败不破坏源（D-15）；产物按打开协议进入；project.json 登记 `upgradedFrom` 信息 |

---

## 9. 写锁、线程与存储上下文

### 9.1 Windows 锁机制与句柄封装（SA-17 落地）

- **机制选择（D-02）**：写权限＝对 `lock` 文件的**独占打开句柄**——`CreateFileW(lock, GENERIC_READ|GENERIC_WRITE, dwShareMode=FILE_SHARE_READ, OPEN_ALWAYS)`。共享模式不含 FILE_SHARE_WRITE ⇒ 其他进程任何写访问请求得到 `ERROR_SHARING_VIOLATION`（内核裁决，获取动作本身原子）。**弃用** LockFileEx 范围锁备选：两机制并存易造成"锁对象分裂"；独占打开满足 ARCH §6.8"独占范围锁**或**独占打开句柄"二选一口径。
- **崩溃释放**：进程退出/崩溃 → OS 关闭句柄 → 排他性即时消失（ARCH：崩溃后由 OS 释放锁）。
- **句柄封装**：`StoreLock`（RAII）独占持有 HANDLE：构造＝获取＋写 PID 记录；析构＝关闭（关闭即失权，状态机同步置 LostWrite/Closed）；内部心跳线程（§9.4）。
- **持锁范围**：存储上下文存续期间持续持有（获取于 `factory.open`/`createNew`，释放于 Closed）。

### 9.2 第二实例：读取 PID 而不破坏互斥

第二实例流程：①以写意图 `CreateFileW(GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ)` → 共享冲突 → **立即只读**（PM-07 不阻塞等待）；②以读意图 `CreateFileW(GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE)`（共享声明允许持有者的写访问共存）→ 成功 → 读取 PID/host/heartbeat（固定宽度记录，容忍撕裂读——心跳仅诊断）→ 提示"项目被 PID=<n> 持有"；③打开为 ReadOnly 上下文（可查看、禁编辑与应用提交）。读句柄随即关闭（不长期持有；PID 信息已取）。

### 9.3 路径规范化与同项目多路径打开

存储实例身份＝`GetFinalPathNameByHandleW` 解析的最终路径（消除 subst/符号链接/盘符别名），Windows 默认大小写不敏感比较（NTFS）。同一规范路径的第二次 writable 打开（含本进程内）：获取锁失败 → 只读（进程外）／直接拒绝 `lock-held-by-other`（进程内重复打开，防自我双写）。8.3 短名/相对路径经规范层统一后不再歧义。

### 9.4 锁文件生命周期（防锁对象分裂）

**文件本身永不删除重建**（D-03）：删除会在"旧锁对象（已删除路径的句柄）与新锁对象（重建后的文件）"间产生分裂窗口。持有期内容更新＝**原地重写固定宽度记录**（truncate→write→flush；心跳周期 30 s，由 StoreLock 内心跳线程执行——纯诊断，非权限判据）。接管（原持有者已退出）：新持有者获取成功后读取残留内容作恢复诊断（PM-08），再重写为自己的 PID。残留心跳过期**不**触发任何接管（卡顿不接管——ARCH §6.8 原文；接管唯一途径＝OS 锁可被获取）。

### 9.5 三类失败的不同诊断

| 情形 | 检测点 | 诊断 |
| --- | --- | --- |
| 介质只读 | 打开期写探测（createNew 试写/或属性检查） | `media-read-only`＋`PRJ-*` 稳定码（PM-07 writable=false 同路径） |
| 权限不足 | CreateFile/rename 的 `ERROR_ACCESS_DENIED`（非共享冲突） | `access-denied`（含路径） |
| 锁竞争 | `ERROR_SHARING_VIOLATION`（对 lock 或发布目标） | `lock-held-by-other`＋持有者 PID（PRJ-LOCK-HELD） |

### 9.6 失权写入防护与句柄关闭/在途事务同步

**不能只检查句柄非空**（任务约束§五.8）：三道防线——①**状态机**：写路径入口校验上下文 `Active`（Draining/Closed/LostWrite 一律拒绝 `context-closed`/`write-rejected`）；句柄由 RAII 独占，非空即"我方持有且未释放"（关闭路径同步置位状态，消除"句柄悬空"态）；②**权威探测**：每笔写事务开始时对 lock 句柄执行轻量有效性探测（GetHandleInformation；失败即失权）；③**I/O 分类**：任何写操作遇到 `ERROR_SHARING_VIOLATION/ERROR_INVALID_HANDLE/ERROR_ACCESS_DENIED` → 上下文转 LostWrite → 后续写全拒＋`PRJ-WRITE-AUTHORITY-LOST`（纵深防御覆盖实现层边缘：句柄异常失效、上下文结束后的迟到写）。**句柄关闭与在途事务同步**：requestClose 先置 Draining（新写拒绝），再等待在途事务/归档/草稿落盘归零（writer 互斥保证无事务处于中间态），最后关闭句柄——关闭动作与事务串行化于同一 writer 通道。

### 9.7 存储上下文的引用持有与最终释放

上下文引用计数持有者：命令服务（在途命令）、归档会话（execution 注册的活跃运行，§10.1）、DraftService（在途保存）、L5 关闭控制器。`requestClose()` → Draining → pending==0 → 末次草稿 flush 确认 → 关闭锁句柄 → Closed（一次性回调 ICloseObserver）。**防永久等待**（自审项 A-4）：pending 的清零由 execution 的结束路径保证（成功/取消/失败/强杀均 abandon，§10.1）；L5 关闭控制器在超阈值（如取消协议 10 s + 余量）后可强制 `abandonAll(ForceTerminated)` 收尾——残留会话记开发诊断，不阻塞进程退出。

### 9.8 线程模型总表

| 线程/通道 | 归属 | 职责与约束 |
| --- | --- | --- |
| 命令执行线程（每可写上下文 1 条） | project | submit 的 S1～S7 串行执行；关闭时 join（在途命令完成或被 Draining 取消） |
| writer 互斥（非线程） | project | 全部变更性文件操作（事务/归档/草稿/目录/锁重写）的串行化点——命令线程、execution 调度线程、UI 触发的保存在此汇合 |
| 调用方线程 | 任意 | 查询端口并发只读；submit/archive/save 可从任意线程进入（内部转串行） |
| 心跳线程（每持锁上下文 1 条） | project | 30 s 原地重写心跳；纯诊断 |
| 事件总线投递线程 | execution·ui（总线实现承诺） | project 只 publish，不做投递线程假设（core Events 契约） |

### 9.9 双实例锁竞争流程图

```
 实例1                                   实例2
   │ CreateFileW(lock, RW, share=READ)     │
   │ OPEN_ALWAYS ──成功──► 写 PID 记录      │ CreateFileW(lock, RW, share=READ)
   │ 持有至存储上下文 Closed                │  └─ERROR_SHARING_VIOLATION（内核原子裁决）
   │                                       │ CreateFileW(lock, R, share=R|W)──读 PID/host/心跳──关闭读句柄
   │                                       │ → writable=false 只读打开＋PRJ-LOCK-HELD(pid)（不阻塞、不推测活性）
   │ （实例1 仅卡顿——心跳停滞）              │ 不接管：写请求仍被共享冲突拒绝（卡顿不触发接管）
   │ 实例1 崩溃/退出                        │
   │  OS 关闭句柄                           │ 用户重开（或显式重试获取）
   │                                       │ CreateFileW(RW)──成功（原子获取；并发竞争仅一胜者）
   │                                       │ 读取残留记录→恢复诊断→重写 PID→恢复扫描→可写激活
```

并发接管（两个实例同时重试）：内核对共享模式的裁决天然原子——仅一个成功，失败方立即只读（ARCH §6.8"获取动作本身原子"的实现落点）。

---

## 10. 跨单元协作与归档协议

### 10.1 与 execution 的归档协作（TASK-03/PM-13/CON-02/04/05；A7/A8 承接）

RunRegistry 与完整五元组校验归 execution；project 接收**已核验**的存储请求，但仍验证项目上下文与写权限。要素逐项：

| 要素 | 设计 |
| --- | --- |
| 归档请求绑定原项目与原修订 | ArchiveRequest 携带五元组＋登记记录中的 runDir；project 校验 `task.project == 本上下文 projectId`（不符＝拒绝＋开发诊断，AT-10 反例）＋runDir 位于本存储 results/ 白名单内；**归档位置不重新推导**（A8） |
| UI 关闭与上下文释放分离 | 界面会话结束≠上下文结束：在途运行（execution 注册）持有引用，上下文存活至归档完成（§9.7；PM-03"等待"选项的实现锚点） |
| 在途运行获取/释放使用权 | execution 派发时向 project 请求归档预留（begin 或预登记引用）→ 归档结束（任一终结路径）释放 |
| 成功/取消/失败/强制终止的结束 | Completed→finalize（完整发布）；Canceled/Failed→abandon（批次残留目录保留为"未完成"——不作为正式缓存命中 CON-04，恢复诊断列出；**取消/失败结果不得进入正式报告**的判定归 evidence/reporting，project 只如实存档 ResultEnvelope 工件）；ForceTerminated（进程被杀、无完成事件）→ execution 侧以 Interrupted 终结登记→abandon 或最小信封归档（其内容构成归 execution/evidence） |
| 分批写入与最终完整发布 | writeBatch 追加批次文件（目录内可见但**不完整**）→ finalize 原子发布 manifest＝唯一"完整"标志；消费者（查询/报告）只认 manifest 在的 run（D-13） |
| 重投递幂等与内容冲突 | 同 (runId, attempt) finalize 重投递：manifest 摘要一致→幂等成功（不重写）；不一致→`archive-conflict` 拒绝＋诊断（D-14）。批次级重投递：目标文件已存在且摘要一致→跳过；不一致→冲突拒绝 |
| 写入失败/磁盘不足/进程异常报告 | writeBatch/finalize 失败→ArchiveStatus.error（disk-full/write-rejected/store-corrupt）＋稳定诊断；调用方（execution）按任务状态机处置；**不因失败结果不进正式报告而使关闭流程永久等待**——失败即 abandon 释放引用（A-4） |
| 运行完成 ≠ 归档完成 | 完成事件后仍有 begin→batches→finalize 序列；"归档完成"＝finalize 成功或 abandon（二者均为责任终结） |
| 当前性与历史归档 | project 不按当前 HEAD 拒绝归档：runDir 绑定原修订，HEAD 前进无关（当前性 Current/Superseded 归 evidence，CON-02/05——自审项 A-2） |
| 重投递/去重范围声明 | 幂等仅覆盖**进程内、单存储上下文**的归档 finalize/批次（由 execution 完成事件可能的重复投递引起）；命令路径无重投递机制（用户/程序内同步调用，无 at-least-once 通道）；**不承诺任何分布式恰好一次语义**（任务约束§五.3） |

### 10.2 项目切换后的迟到结果归档流程图（S7/A7/A8 的 project 侧）

```
 项目A运行中(run r1, 五元组已登记, runDir=项目A/results/<run-id>)
   │ 用户切换项目（PM-03 对话框：草稿三选＋任务二选"等待/协作取消"）
   ▼
 UI会话──► 项目B；项目A存储上下文保持（r1 引用未释放；Draining 由 requestClose 触发或保持 Active 视关闭策略）
 r1 完成事件迟到 ──► execution: RunRegistry 五元组核对（一次性判定；无匹配→丢弃＋开发诊断，绝不写项目B）
   │ 匹配（属项目A）
   ▼
 project(项目A上下文).archive.begin(task, runDir)   ── 上下文已 Closed？→ 拒绝归档＋诊断（须先重新取得写权限，A7 防御规则）
   │ Active ──► writeBatch*（分批）──► finalize（manifest 原子发布）──► ResultArchived 事件
   ▼
 当前性独立判定（evidence）：结果切片内容身份 vs 项目A当前 HEAD 切片内容身份（CON-05）
   ├─ 输入未变（如仅电机成本变更）→ Current（可消费）
   └─ 输入已变 → Superseded（保留为原快照历史证据，CON-02）
   ▼
 无论何种当前性：不进入当前界面会话、不成为"当前结果"（TASK-03）
 r1 引用释放 ──► 项目A pending==0 ──► 上下文 Closed、写锁释放
```

### 10.3 与 diagnostics

project 消费（经 IDiagnosticsSink 注入，P-PR-6）：稳定码注册（§5.0 建议清单）、用户/开发两级日志输出、恢复诊断的脱敏（路径脱敏配置归 diagnostics，NFR-SEC-07）。project 产出：全部 DiagnosticRecord（含只读/恢复/权限/归档类）；ConfirmableFinding 的**构造**在域处理器（码值/文案权威在 diagnostics/ui）。

### 10.4 与 io

分工：io＝CSV/JSON 编解码、SafePath/BudgetGuard、包 ZIP 编解码、外部源安全读取与缺失/变化检测；project＝一切磁盘写入与发布、格式契约、引用保护。**依赖边待裁决（P-PR-5）**：ARCH §3.5 未登记 project→io；本文建议补登（无环：io→core,diagnostics），否则阶段 B 以"project 定义 IPackageCodec/IResourceReader 接口＋L5 注入 io 实现"落地（零编译边）。阶段 A 无 io 依赖。

### 10.5 与 ui / workflow

ui：ICommandInteraction 实现（确认对话，P-PR-7）、DraftController（定时器与未保存标记）、关闭对话框（消费 requestClose/closed）。workflow：生命周期入口编排（新建/打开/另存/导入导出向导调用 project 服务；PM-01～07 主 WP-22）；七阶段门控消费修订事件（经事件总线）。project 不呈现任何 UI（N-7）。

### 10.6 与 evidence / reporting

evidence：经②查询端口读修订/对象组装快照；失效计算消费 DependencyInvalidated 事件＋查询（core D-09：事件不携数据）；当前性判定读 HEAD。reporting：经②端口只读明确 ID 的修订与结果（RPT-01）；报告工件写 reports/ 经存储端口（阶段 B 接入；幂等追加/冲突拒绝归其契约，project 提供同 D-13/D-14 模式）。project 保存与提供证据对象，不做证据判断（N-9）。

### 10.7 与 runtime / policy / 各业务域

全部经注入处理器间接协作（§6.5）：modeling 处理器持④⑥端口句柄做断言/策略校验/双编译；selection 回填、optimization 采用（OPT-08 创建分支＋新修订＝`project.create-branch`＋域命令组合）等均经①端口提交。project 编译期零业务/runtime/policy 链接。

---

## 11. 验证方案及故障注入矩阵（设计；未实现未运行，实现状态随 §12 登记）

测试目标 `sdurws_ird_project_test`（单元）与 `sdurws_ird_project_contract_test`（跨单元：锁/归档/命令契约）。消费 testkit：FaultInterceptor（经 `src/win32/IFileOps` 接缝——生产窄接口，测试侧 fake，testkit.md D-10 形态）、TempDir、DeterministicEnv、IRD_TEST_INFO/IRD_EXPECT_*、TestProcessRunner（崩溃类，按 testkit §6.5/§10.2 交付节奏）。产品目标不链 testkit（T-1）。每用例注明需求/AT、前置、操作、预期、观测点：

| 组 | 用例（正/反例/故障注入） | 需求/AT | 前置 | 操作 | 预期 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- |
| PRJ-TX-1 并发与过期基线 | 双线程并发 submit；expectedRevision=旧 tip | ARC-01、PM-04 | 可写上下文、两个合法命令 | 并发提交；随后以过期基线提交 | 全局串行（第二个等待）；各恰好一个新修订；过期者 Rejected(stale-revision) 且无修订 | seq 单调无重复；rejected 者无 revisions 目录；稳定码 PRJ-STALE-REVISION-REJECTED |
| PRJ-TX-2 断言与确认 | 硬断言失败／无 interaction 待确认集／interaction 拒绝／**确认凭据绑定失效**（findingDigest/policyContentId/commandDigest/baseRevisionId 任一不符） | MDL-06、SA-15、AT-01 | 测试处理器产出断言/finding | 四路提交 | 均无修订；confirmations-unresolved/confirmations-rejected/绑定失效拒绝 | command.json 无新记录；诊断字段完整（ContractCheck.checkDiagnosticRecord） |
| PRJ-TX-3 双编译 | IModelCompilePort 桩：WC 失败／DWC 失败／双成功 | MDL-06、AT-01 | requiresDualCompile 命令 | 三路 | 任一失败→无修订无发布（对象库无新对象）；成功→恰好一个新修订 | objects/ 计数；.staging/tmp 清理 |
| PRJ-TX-4 事务边界 | F1～F8（§7.6）逐项 | NFR-REL-01、AT-13 | 健康修订 r1 | 注入/kill 于每步 | 旧版本字节不变；未提交忽略＋恢复诊断 | HEAD/r1 字节哈希；RecoveryReport.ignoredStagingTxs |
| PRJ-TX-5 分支与撤销/重做 | §4.5.1 走查七步；随后 undo/redo；跨分支提交后再回 | PM-12/18、AT-29 | 项目含 A/B 分支 | 走查＋undo/redo | 分支 tip 不丢失/不回退（INV-M3）；undo/redo 产生新修订、历史不改写；空历史稳定提示 | branchTips 逐断言；修订只增；revisionSeq 递增 |
| PRJ-TX-6 草稿 | 保存/恢复/损坏（截断 JSON）/.new 残留/放弃 | PM-04/08、AT-21 | 有分支与模块 | 落盘→破坏→重开 | 损坏诊断＋.bak 恢复；保存不产生修订 | drafts 文件状态；修订计数不变；tryLoad 诊断 |
| PRJ-TX-7 锁 | 第二实例只读；持锁进程卡顿（心跳停滞）；崩溃后并发接管（两实例同时获取） | PM-07、SA-17 | 实例1 可写打开 | 起第二实例；实例1 挂起；kill 后双实例竞争 | 只读＋PID 提示；卡顿不接管；接管仅一胜者 | writable 标志；lock 内容 PID；共享冲突错误码 |
| PRJ-TX-8 UI 关闭归档未完 | requestClose 后迟到完成事件到达 | PM-03/13、TASK-03、AT-10 | 项目 A 在途运行＋已切 B | 关闭请求→事件迟到 | 上下文存活至归档 finalize；完成后才 Closed；跨项目事件不写入 | closed() 时序；项目 B results 目录不含外来 run |
| PRJ-TX-9 迟到写与误认 | Closed 后 archive.begin/submit/draft.save；F5 后重开浏览"已发布未提交"修订 | NFR-REL-01、PM-08 | Closed 上下文／崩溃残留 | 各写入口调用；tryRevision(闭包外) | 全部拒绝＋诊断；闭包外修订不可见 | StoreError code 集合；查询返回空 |
| PRJ-TX-10 磁盘与清理 | 磁盘满（写 N 字节后失败）；rename 拒绝；清理失败 | NFR-REL-01 | 健康项目 | 注入 | Failed(disk-full/write-rejected)；已提交状态完好 | 字节比对；残留仅诊断 |
| PRJ-TX-11 复制一致性 | 另存为（换 projectId）后全链引用走查；包导入后哈希与引用一致 | PM-05、AT-20 | 健康项目 | 另存/导出导入 | 对象/修订/分支引用可解析；results 保持原五元组；报告-结果引用缺失时报告不复制 | 新旧 projectId 比对；闭包校验通过 |
| PRJ-TX-12 元数据不变量 | 闭包双通道（CreateBranch 修订可达）；谱系成环注入；分支表回退注入 | PM-12、自审 A-6/A-7 | 各型项目 | 构造与注入 | 孤岛判定不出现；环/回退在发布边界拒绝（branch-metadata-regression） | 闭包覆盖集断言；发布错误码 |
| PRJ-TX-13 只读全入口 | 只读上下文遍历全部写 API | PM-07 | 只读打开 | 逐个调用 | 一律拒绝＋稳定诊断 | 错误码集合＝设计清单 |
| PRJ-TX-14 构建红线 | 目标无 Qt 头、无私有头出 include、CMake 无业务单元边、无 testkit 边 | NFR-MNT-01/02、T-1 | — | 脚本扫描 | 零命中 | 扫描报告 |

### 11.3 Windows GUI 测试规则承接

依据用户级 AGENTS.md（`C:\Users\zgl18\.codex\AGENTS.md` 实测已读）：project 全部测试为无界面模型测试（无 Qt 平台插件需求）；未来 workflow/ui 的 GUI 测试按该规则执行（`QT_QPA_PLATFORM=windows`、单实例绝对路径启动、禁 offscreen、ctest 以 `ird_gui` 标签串行）——本阶段仅登记流程，不启动 GUI。任何未执行测试不得标注"通过"。

---

## 12. 阶段 A 实现任务拆分

局部编号 `PRJ-Txx`（project＝**WP-04**，REQUIREMENTS §3/ARCH §3.1 既有登记；不重排任何上游 WP 编号；DETAILED-DESIGN/development-task-breakdown 产出后如需对齐，以映射表增量登记——同 testkit P-TK-3 口径）。依赖自上而下；阶段 A 聚焦可独立落地的平台能力，业务命令处理器、真实双编译、向导、包/另存/固化/升级器实体、R2 子项按上游阶段经 §13 交接承接，不提前实现。

| 任务 | 输入 | 产物 | 依赖 | 涉及文件 | 验证方式 | 完成条件 |
| --- | --- | --- | --- | --- | --- | --- |
| PRJ-T01 构建落位 | 骨架 CMakeLists、§3.3 | `sdurws_ird_project` STATIC（C++17、链 core）；注册 `_test`/`_contract_test`（gtest 按 P-TK-1 结论） | 无（CORE-T01 后更佳，可平行） | `industrialrobot/CMakeLists.txt`、`project/CMakeLists.txt`（新）、`src/`（空起步） | 冒烟＋集成双模式配置构建；PRJ-TX-14 前两项 | 两模式零错误；零 Qt/零业务边扫描通过 |
| PRJ-T02 存储原语 | §7.1/§7.2 | `win32/AtomicFile.*`＋`IFileOps` 接缝；`publishNew`/原子替换/write-through 写 | PRJ-T01 | 同名文件 | 单测＋官方文档口径复核记录（§7.2 表逐行） | 原子/持久性分离语义用例过；文档复核留痕 |
| PRJ-T03 写锁与路径 | §9.1～§9.6 | `win32/StoreLock.*`（独占句柄/心跳/PID 读取/失权防护）、`PathCanonical.*` | PRJ-T02 | 同名文件 | PRJ-TX-7、PRJ-TX-13（锁部分） | 双实例/卡顿/接管/只读判定用例过 |
| PRJ-T04 格式契约与编码 | §4.2/§4.4/§4.8 | `PersistenceFormat.hpp`＋`Codec.*`（canonical 编码、round-trip） | PRJ-T01 | 同名文件 | 编码 round-trip/非法拒绝单测 | `parse(dump(x))==x` 全类型过 |
| PRJ-T05 对象库 | §4.6 | `ObjectStore.*`（内容编址、只增发布、共享、LRU、摘要校验） | PRJ-T02/T04 | 同名文件 | 单测（共享/篡改/缓存预算） | 不可变与共享用例过 |
| PRJ-T06 修订/分支/元数据 | §4.3/§4.5 | `RevisionIndex.*`（闭包双通道、分支表、防回退校验、seq 分配） | PRJ-T04/T05 | 同名文件 | PRJ-TX-5（模型部分）、PRJ-TX-12 | 走查七步断言过；环/回退注入拒绝 |
| PRJ-T07 事务引擎 | §7 | `TxEngine.*`（七步状态机、.staging、恢复扫描） | PRJ-T02/T05/T06 | 同名文件 | PRJ-TX-4（F1～F8 进程内部分） | 边界注入矩阵过；旧版本字节不变 |
| PRJ-T08 打开与恢复 | §8.7、§7.4 | `ProjectStoreFactory/ProjectStoreImpl`（open/createNew/RecoveryReport/生命周期） | PRJ-T03/T07 | 同名文件 | PRJ-TX-9、恢复报告断言 | 激活前失败不影响当前项目用例过 |
| PRJ-T09 查询端口 | §4.6/§4.7、§5.2 | `QueryPort` 实现（视图/闭包约束/缓存/隔离） | PRJ-T06/T08 | 同名文件 | 并发只读单测；闭包外不可见 | 隔离与不可见用例过 |
| PRJ-T10 命令服务与内置命令 | §5.3、§6 | `CommandServiceImpl.*`（串行槽/S1-S7/注册表/编译编排）＋内置 `project.create-branch` 等元数据处理器＋测试处理器 | PRJ-T08/T09 | 同名文件＋`test/StubHandlers.*` | PRJ-TX-1、PRJ-TX-3（桩编译） | 恰好一个修订/失败零修订用例过 |
| PRJ-T11 确认放行流 | §5.3.3/§5.3.4、§6.7 | 交互回调编排、确认绑定与留痕、取消/失效路径 | PRJ-T10 | 同名文件 | PRJ-TX-2 | 四路拒绝/绑定失效用例过；留痕入 command.json |
| PRJ-T12 草稿服务 | §5.4、§8.1～§8.6 | `DraftServiceImpl.*`（原子替换+.bak/损坏恢复/投影/放弃） | PRJ-T08 | 同名文件 | PRJ-TX-6 | 恢复与保留用例过 |
| PRJ-T13 撤销/重做 | §5.5、§6.9 | `UndoRedoServiceImpl.*`（逆命令提交、会话栈、边界） | PRJ-T10 | 同名文件 | PRJ-TX-5（undo/redo 部分） | 新修订/空历史/跨分支用例过 |
| PRJ-T14 归档端口 | §5.6、§10.1 | `ArchiveServiceImpl.*`（begin/batch/finalize/abandon、幂等/冲突、引用持有） | PRJ-T08 | 同名文件 | PRJ-TX-8（进程内模拟）、F9 | manifest 发布/幂等/冲突/上下文存活时序过 |
| PRJ-T15 契约测试与崩溃注入落地 | §11 全表、testkit §6.4/§6.5 | PRJ-TX-1～14 用例体（含 TestProcessRunner 接入的 F8/PRJ-TX-7/8） | PRJ-T02～T14、TK-T09（及 TestProcessRunner 按 testkit §10.1 节奏） | `project/test/*`、`project/contract_test/*` | §11 表逐项 | 全部设计用例执行通过并留痕（**未执行不得标注通过**） |
| PRJ-T16 文档与门禁同步 | 全文 | README 核对；§11/§15.3 状态更新；门禁建议（含 P-PR-5/P-PR-6 裁决申请）提交 | T01～T15 | 本文、`project/include/.../README.md` | 评审 | 本文与实现零偏差登记；待裁决项最新状态 |

---

## 13. 后续阶段承接与接口交接清单

### 13.1 project 在后续阶段的交付（本文已设计、随阶段落地）

| 能力 | 阶段 | 任务锚点 |
| --- | --- | --- |
| 固化服务实体（io 协作）与重关联命令注册 | B（PM-01/09、CON-03） | §5.7；PRJ 系列新增任务届时登记 |
| 另存为/包导入导出实体（io 编解码依赖，P-PR-5 裁决后） | B（PM-05） | §5.8/§8.8～§8.10 |
| schema 升级器首个实例（随已发布格式演进） | 按需（NFR-DEP-04） | §8.11 |
| PM-04-S1 落盘周期可调（消费方传参即可，无 project 侧新开发）／PM-08-S1 只读报告／PM-11-S1～S3／PM-12-S1 | R2 | §4.1/§5.2 字段已预留 |
| 真实领域处理器（modeling/selection/optimization 注册） | B/C | §6.5 注册协议 |

### 13.2 接口交接清单（各单元详设/实现的直接输入）

| 单元 | 从 project 接收 | 须在其侧冻结/提供的相邻契约 |
| --- | --- | --- |
| core | 身份分配落地点（对象/项目/分支/修订分配时机与 seq 规则）、事件发布时序（Committed→Invalidated FIFO）、确认留痕编码（CommandRecord.confirmations 结构） | 冻结 v0.1 契约（P-PR-1）；ContentDigester 稳定性 |
| execution | IResultArchivePort、上下文引用协议（begin/abandon 语义）、Closed 后迟到归档的拒绝与重取权规则 | RunRegistry 登记项（runDir/runKind）、调度线程调用约定（P-PR-4）、完成事件重复投递上限、强杀后的 abandon 调用时机 |
| diagnostics | PRJ-\* 稳定码建议清单（§5.0）、DiagnosticRecord 产出点、脱敏需求（路径） | StableCodeRegistry 码值分配、两级日志接口（P-PR-6）、IDiagnosticsSink 实现 |
| io | 磁盘写入收口（catalog/包临时位经 project 端口）、对象负载 canonical 要求（域侧） | ZIP 编解码、SafePath/BudgetGuard 对 project 的注入形态（P-PR-5 裁决）、外部源检测接口（Missing/Changed） |
| ui | ICommandInteraction 实现（线程 Marshal 与取消）、DraftController 定时器（60 s 默认）、关闭对话框与 requestClose/closed 对接 | 确认对话凭据采集（principal）、interaction 生命周期（P-PR-7）、未保存标记投影 |
| workflow | open/createNew/saveAs/package 服务调用序列（向导编排）、恢复报告呈现 | 生命周期入口流程（PM-01～03/05～07）、失败路径的用户呈现 |
| evidence | ②查询端口（快照组装输入）、runDir 读取、DependencyInvalidated 事件 | 切片计算与当前性判定（消费 HEAD 与对象）；results 工件格式（经 execution 归档） |
| reporting | ②端口只读修订/结果、reports/ 写入端口（D-13/D-14 模式） | 报告工件引用结构与幂等追加规则 |
| runtime | IModelCompilePort 最小契约（§5.3.6） | compileWorkCellAndDwc 实现、`.staging/tmp` 临时物约定、诊断口径（P-PR-7） |
| policy | （无直接边）处理器经④端口读策略产出 finding 的模式 | EngineeringPolicySet 读取接口与内容身份（CON-06） |
| modeling 等业务域 | ①命令提交协议、HandlerContext（对象分配/查询）、draft 模块注册 token、inverse 记录产出义务 | 各命令 payload canonical 序列化登记（core §4.2 责任边界）、prepare 断言实现、逆命令表达 |
| testkit | IFileOps 接缝、PRJ-TX 用例对 FaultInterceptor/TestProcessRunner 的消费 | TestProcessRunner 实现节奏（TK-T11，testkit §10.1） |

---

## 14. 需求—设计—验证追踪矩阵

| 需求/上游条款 | 设计落点 | 验证 |
| --- | --- | --- |
| ARC-01（聚合根＋命令原子产生修订） | §5.3、§6.1、§7.1 | PRJ-TX-1/3/4 |
| ARC-02（端口协作/无私有互访） | §3.2、§5（①②端口）、§6.5 | PRJ-TX-14 |
| CON-01（身份/版本包络、不可变快照） | §4.3/§4.6/§4.7 | PRJ-TX-4/9 |
| CON-03（外部资源不可变副本与引用保护） | §4.1（objects 资源区）、§5.7（阶段 B） | 阶段 B 契约测试（登记） |
| CON-04（部分/失败不作缓存命中） | §4.1 checkpoints 行、§5.6（manifest 才完整） | PRJ-TX-14/F9 |
| TASK-03/PM-13（迟到结果归属） | §5.6、§10.1/§10.2 | PRJ-TX-8/9 |
| PM-01（新建与外部引用记录） | §5.1 createNew、§4.4.5 externalRefs | 阶段 B（PM-01 主 WP-22）；记录结构 PRJ-T04 单测 |
| PM-02（打开五步协议） | §8.7 | PRJ-TX-9（激活前失败）；向导阶段 B |
| PM-03（关闭/切换统一流程支撑） | §9.7、§10.2 | PRJ-TX-8 |
| PM-04（保存/应用分离、草稿冲突） | §5.4、§6.2、§8.1～§8.3 | PRJ-TX-1/6 |
| PM-05（另存为/项目包存储侧） | §5.8、§8.8～§8.10 | PRJ-TX-11（阶段 B 实体落地后复验） |
| PM-06（旧格式/未来版本） | §8.11 | PRJ-T08 单测（拒绝码） |
| PM-07（只读打开、双实例） | §9 全节 | PRJ-TX-7/13 |
| PM-08（崩溃与恢复） | §7.4、§8.4、§8.7 | PRJ-TX-4/6/9 |
| PM-09（重关联存储侧） | §2.2、§5.7 | 阶段 B |
| PM-12（方案分支） | §4.5、§6.5（内置命令） | PRJ-TX-5/12 |
| PM-18（撤销/重做＝新修订） | §5.5、§6.9 | PRJ-TX-5 |
| NFR-REL-01（多文件事务） | §7 全节 | PRJ-TX-4/10 |
| NFR-REL-04（外部源检测数据源） | §4.4.5、§5.7 | io 侧检测测试（交接） |
| NFR-SEC-01（资源区不逃逸） | §4.1 白名单、§5.6 runDir 校验 | PRJ-TX-13 |
| NFR-DEP-04（schema 版本与升级器） | §4.2、§8.11 | PRJ-T08 单测 |
| NFR-MNT-01/02（内核零 Qt/模型测试直调） | §1.4、§3.2 | PRJ-TX-14 |
| SA-15（可确认诊断放行） | §5.3.3/§5.3.4、§6.7 | PRJ-TX-2 |
| SA-17（写锁/存储上下文/只读） | §9 全节 | PRJ-TX-7/13 |
| ARCH §6.4（事务协议）/§4.5（归档两段式） | §7、§10.1 | PRJ-TX-4/8 |

---

## 15. 设计决策、风险、待裁决项与变更记录

### 15.1 设计决策登记

| ID | 决策 | 理由与备选 |
| --- | --- | --- |
| D-01 | project 目标零 Qt（含 Core） | L3 允许但不必需；std+Win32 足够；定时器归 ui；最大化模型测试直调（NFR-MNT-01 精神）。备选（QTimer/QFile）引入事件循环与 Qt 依赖面，被否 |
| D-02 | 写锁＝lock 文件独占打开句柄（share=FILE_SHARE_READ） | 内核原子裁决获取、崩溃自动释放、第二实例可读 PID；备选 LockFileEx 范围锁（ARCH 允许二选一）因双机制分裂风险被否 |
| D-03 | 锁文件永不删除重建；心跳原地重写固定宽度记录 | 删除重建产生锁对象分裂窗口；重写仅诊断用途容忍极小撕裂窗口 |
| D-04 | 提交闭包＝修订 DAG（parent 链）∪元数据谱系（supersedes→committedBy）双通道 | 单通道会把 CreateBranch 修订判为孤岛；谱系通道以常量元数据开销修复（§4.5） |
| D-05 | 新元数据一律以 HEAD 引用的权威版本为底本拷贝更新 | INV-M3 防回退不变量的机制化；绝不经父修订元数据重建 |
| D-06 | HEAD 内容含 branchId 与 revisionSeq（超出 ARCH"修订 id＋对象清单摘要"最低要求） | 服务重开默认分支与会话恢复；仍为单一提交点，不构成第二权威 |
| D-07 | 确认等待占用命令执行槽、零事务资源 | 保确认所针对输入版本在提交前不可变；等待无文件/句柄残留，可被关闭取消 |
| D-08 | 业务处理器注册表＋L5 装配注入 | project 零业务链接（ARC-02）；命令服务拥有平台语义非转发包装（NFR-MNT-04） |
| D-09 | 双编译经 IModelCompilePort 注入；临时产物限 .staging/tmp | 编译归 runtime（SA-04）；失败时零已发布内容需回退（顺序即原子性） |
| D-10 | 对象文件＝负载原样字节，摘要即内容版本 | project 不解释域负载；避免信封造成第二身份；域序列化登记归域（core §6.3 分工） |
| D-11 | 撤销＝逆命令；栈＝会话内按分支、不持久化 | PM-18"新修订不改写历史"；redo 语义仅会话内有意义；重启后 canUndo 由 tip 推导 |
| D-12 | 草稿＝模块粒度单文件＋.bak 轮换 | 部分写影响面最小；旧草稿保留（任务约束§五.6） |
| D-13 | 结果/检查点完整＝manifest 原子发布 | 消费者判据单一（CON-04）；分批写与最终发布明确区分 |
| D-14 | 归档幂等＝manifest 摘要比对；内容不一致＝冲突拒绝 | 重投递安全且不掩盖数据分歧；范围限进程内单上下文（§10.1） |
| D-15 | 升级器源只读、输出新目录 | 升级失败不破坏源项目（任务约束§五.11） |
| D-16 | 另存为＝全量复制＋三处改写（project.json/HEAD/drafts 的 projectId） | 对象/修订/分支 ID 全局随机唯一，无需映射表；results 保持原五元组（历史事实） |
| D-17 | 导出一致视图＝HEAD 闭包快照＋不可变对象＋单文件原子替换 | 与后台写入并存无需暂停写入（PM-05） |
| D-18 | 事件发布失败不回滚已提交修订 | 提交点在 HEAD 切换；事件非数据源（core D-09），订阅方可自愈 |

### 15.2 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-1 | core.md v0.1 未冻结（身份/摘要/事件签名可能变） | PRJ-T04 起返工 | §3.2 消费清单逐项锚定；core 冻结 diff 后增量同步（P-PR-1） |
| R-2 | ARCHITECTURE v0.11 Draft 待评审（A9+ 可能调整依赖表/端口） | §3.2/§13 交接返工 | 严格锚定 §6/§7.1 明文；影响面集中于 §2.1/§3.2/§10，可增量修订（P-PR-2） |
| R-3 | diagnostics/ui/execution 详设未产出（码表/回调/RunRegistry 对接悬空） | 确认流与归档契约需二次对齐 | 最小契约＋注入适配已定（§5.0/§5.3.3/§5.6）；P-PR-4/6/7 集中登记 |
| R-4 | io 未产出且 project→io 边未登记 | 阶段 B 包/固化阻塞 | P-PR-5 提前裁决；阶段 A 无依赖 |
| R-5 | Windows API 崩溃一致性行为与文档口径偏差（断电残余窗口） | 极端崩溃下 HEAD 指向不完整 | 恢复协议兜底（PM-08）＋故障注入实证（PRJ-T15/F8）；不超诺（§7.2） |
| R-6 | 热启动性能（1,000 对象/100 修订 P95≤3 s，PM-16） | 打开协议全量哈希校验可能超时 | 打开仅清单级校验＋对象惰性校验（§4.6）；预算与分级校验已设计；性能验证归 WP-23/PM-16 主线 |
| R-7 | C++17 与 C++11 基线混链（同 core P-ENV-1） | 集成构建失败 | PRJ-T01 双模式验证；不使用 C++20 |
| R-8 | 长事务期间用户感知卡顿（命令槽串行） | 交互体验 | 编译在命令线程与 UI 并行（ARCH §4.2）；>1 s 工作转后台归 execution 域 |

### 15.3 待裁决项（问题—依据—影响—建议—需要裁决者）

| # | 问题 | 依据 | 影响 | 建议 | 需要裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-PR-1 | core.md v0.1（Draft）契约可能随冻结调整 | core.md 文档头状态行 | 本文 §3.2 消费清单相关任务返工 | core 冻结时出 diff，project 按影响面增量修订并留痕；不私改 core 语义 | core 详设所有者 |
| P-PR-2 | ARCHITECTURE v0.11 状态 Draft 待评审 | 架构文档头 | 评审结论可能要求同步（依赖表/端口/锁机制表述） | 评审后按影响面增量修订 | 架构所有者 |
| P-PR-3 | ARCH §7.1/S1 走查将"前置断言"画在 project 命令服务框内，而断言逻辑（物性/限位/策略）属业务与策略 | ARCH §7.1 图示 vs §3.5 依赖边（project 仅依赖 core,diagnostics）与 N-1 边界 | 若按字面读法 project 须实现业务断言——违反分层 | 本文解释：命令服务**编排**断言阶段，判定由注入处理器＋④端口执行（§6.5/§6.6）；请架构侧确认该读法（不改依赖方向） | 架构所有者 |
| P-PR-4 | 归档端口调用线程与 RunRegistry 对接细节未定（execution 详设未产出） | ARCH §4.2"调度线程……写 results/"与本文 writer 互斥（§9.8） | 契约测试线程模型需二次对齐 | execution 详设起草时按 §5.6/§9.8 冻结调用线程与重复投递上限 | execution 详设所有者 |
| P-PR-5 | ARCH §3.5 依赖表未登记 project→io 边，而任务约束与 PM-05/CON-03 需要 project 消费 io 能力 | ARCH §3.5 表（表外同层边＝构建失败） | 阶段 B 包导入导出/固化的编译期依赖不合法 | 建议架构修订补登 project→io（接口依赖，无环）；备选：project 定义 IPackageCodec/IResourceReader、L5 注入 io 实现（零编译边） | 架构所有者 |
| P-PR-6 | PRJ-\* 稳定诊断码值与 IDiagnosticsSink 形态待 diagnostics 冻结 | diagnostics 详设未产出；ARCH §3.5 project→diagnostics 边已登记 | 码表冲突或适配器返工 | diagnostics.md 起草时收编 §5.0 建议清单；sink 接口以其详设为准 | diagnostics 详设所有者 |
| P-PR-7 | ICommandInteraction（ui Marshal/取消）与 IModelCompilePort（runtime）最小契约需对端确认 | 本文 §5.3.3/§5.3.6 为单侧冻结 | 对端详设若另立形态需本文增量修订 | ui/runtime 详设起草时以本文契约为起点交叉核对 | ui/runtime 详设所有者 |
| P-PR-8 | 分支 label 创建时一次写入、不可改名是否满足 PM-11"当前方案"显示需要 | PM 明确不做"分支改名"；PM-11 标题栏需显示方案名 | 若需可编辑方案名则走需求变更 | 维持"创建时 label＋不可改名"；P-PR-8 登记请需求侧确认口径 | 需求所有者 |

### 15.4 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-09 | 首版：基于 REQUIREMENTS v1.16（Accepted）、ARCHITECTURE v0.11（Draft）、core.md/testkit.md v0.1（Draft，协作输入）完成 15 章详细设计；登记磁盘现状（DETAILED-DESIGN/development-task-breakdown/其余 units 卡缺失、old/ 缺失）；冻结 .rwdesign 全目录契约、双通道提交闭包与防回退不变量、七步事务协议与故障注入矩阵、Windows 锁与存储上下文生命周期、命令/确认/撤销/归档接口；实现任务 PRJ-T01～T16；待裁决 8 项（P-PR-1～8） |

### 15.5 交付前自审记录（v0.1；自审≠实现测试≠正式验收）

| 检查项（任务约束§九） | 结论 | 证据位置 |
| --- | --- | --- |
| 是否出现第二写路径或第二权威元数据 | ✔ 未出现：磁盘写入唯一归 project（§2.1 O 表/§4.1）；project.json 静态化、分支表唯一权威＝HEAD 引用的 ProjectMetadata（§4.2/INV-M3） | §2.1、§4.2、§7.1 |
| 是否将 HEAD 前进误当作历史结果非法 | ✔ 归档绑定原修订、不按当前 HEAD 拒绝（§5.6/§10.1）；当前性归 evidence | §10.1 |
| 是否在锁释放后继续写入 | ✔ 三道失权防线＋Draining 排空（§9.6/§9.7）；迟到写拒绝用例 PRJ-TX-9 | §9.6 |
| 是否对崩溃/取消/失败出现永久等待 | ✔ 关闭排空有界、abandon 全路径终结、确认等待可取消（§5.3.4/§9.7/§10.1） | §9.7、§10.1 |
| 是否将文件存在误当作提交成功 | ✔ 闭包规则＋manifest 完整性判据＋闭包外不可见（§4.5/§7.3/D-13）；反例 PRJ-TX-9 | §7.3 |
| 是否存在分支元数据回退或引用循环 | ✔ INV-M3 防回退不变量＋发布期无环校验（INV-M2）＋注入用例 PRJ-TX-12 | §4.5 |
| 是否发生上层业务依赖倒挂 | ✔ project 编译期仅 core；业务/runtime/policy/execution 全部注入或反向（§3.2/§6.5） | §3.2 |
| 是否越权重定义需求或其他单元机制 | ✔ 断言/编译/判定/调度/编解码/UI 全部排除（§2.1 N 表/§2.2）；8 项冲突集中登记待裁决（§15.3） | §2.1、§15.3 |
| 未参与开发者可直接实现 | ✔ 数据类型字段表（§4.4）、接口签名/前置/错误/线程/示例（§5）、七步逐步表（§7.1）、任务卡（§12） | 全文 |
| 上游输入版本如实登记 | ✔ §1.2 磁盘实测表（含缺失项，未虚构） | §1.2 |
