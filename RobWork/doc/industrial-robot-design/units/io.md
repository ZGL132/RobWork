# 工业机械臂设计软件 · io 单元详细设计（阶段 A）

> 2026-09-10 同步：本单元详设编写完成（第一批 11/20）；保留本文 Draft/Draft-Structured 评审状态，不代表接口已冻结或实现通过。当前任务与准入结论见 [阶段一同步记录](../traceability/phase-one-readiness.md)；历史磁盘调查仅表示编写时事实，现状以该记录为准。

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.1（首版草案） |
| 日期 | 2026-09-10 |
| 状态 | **`Draft`**（本文只做详细设计；不自行宣布 Accepted，不视任何自审为实现测试通过或正式验收） |
| 文档代号 | UNIT-IO |
| 单元 | io（平台服务，L3；ARCHITECTURE §2.3/§3.1：CSV/JSON 读写〔转义 roundtrip〕、目录包导入校验、资源导入服务、SafePath/BudgetGuard） |
| 上游 | `REQUIREMENTS.md` **v1.16（`Accepted`，2026-09-09 签署；签署后变更 C1～C8 已留痕）**；`ARCHITECTURE.md` **v0.11（`Draft`，待评审）** |
| 协作输入 | `units/core.md`、`units/testkit.md`、`units/project.md`、`units/evidence.md`、`units/runtime.md`、`units/policy.md`、`units/execution.md`、`units/diagnostics.md`、`units/ui.md`——均 v0.1（`Draft`，未冻结）；本文消费的 core 契约以 core.md v0.1 签名为基线逐项登记（§3.2），上游冻结版本变更时按影响面增量同步 |
| 上游下游链位置 | ARCHITECTURE §11.1 / `DETAILED-DESIGN.md`：`units/io.md` 为 20 单元任务卡之一；对应任务包 WP-11（任务卡 WP-11-T01～T07 已在 `development-task-breakdown.md` §2.12 登记）；主 WP 归属 WP-E、WP-I（DETAILED-DESIGN 20 单元状态表） |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/`（骨架已建：目标 `sdurws_ird_io`〔INTERFACE 占位〕＋别名 `RWS::ird::io`＋公共头保留位 `include/sdurws/ird/io/README.md`；README 引用本文 §9〔公共接口〕作为源码落位依据，与本文结构一致） |
| 适用 AGENTS.md | 仓库根 `AGENTS.md` 适用：产品代码详细中文注释、框架零源码修改、双模式构建与留痕、提交后推送。Windows Qt GUI 测试须在 VS x64 环境设 `QT_QPA_PLATFORM=windows`，逐个绝对路径启动。 |
| 实现口径 | 从头构建（REQUIREMENTS v1.9/v1.11、ARCHITECTURE 文档头）；`old/` 历史实现仅功能范围对照、不作语义来源（RV-13），本文不引用、不复制、不恢复其任何机制 |

---

## 1. 文档信息、上游基线与设计目标

### 1.1 文档定位

本文是 io 单元的唯一详细设计：依据 `ARCHITECTURE.md` 分配给 io 的职责（§3.1 单元总表 io 行；§7.9 导入安全 SA-14；§6.6 外部资源三段边界；§6.7 项目包），把路径安全与预算模型、CSV/JSON 解析与可逆编码、URDF/Xacro/网格/WorkCell 资源读取交接、`.rwpack` 导入导出、外部资源固化协作、公共接口、跨单元协作协议、验证方案与阶段 A 任务拆分写到可直接实现的深度。

需求语义与验收标准一律以 `REQUIREMENTS.md` v1.16 条目为自足定义，本文不重定义、不收窄、不扩大；架构归属以 `ARCHITECTURE.md` v0.11 为准（SA-10 构建红线、§2.3 分层规则、SA-12 单一权威、SA-14 导入安全三件套直接约束本单元）。本文对其含混或未登记事项的解释集中登记于 §15.3（P-IO-1～P-IO-7，含对 core.md P-AR-1、project.md P-PR-5、runtime.md P-RT-6 的承接答复），不私自修改上游、不越权修改需求、架构或其他单元机制。

### 1.2 上游与磁盘现状登记（2026-09-10 实测）

| 项 | 状态 | 说明与对本文的影响 |
| --- | --- | --- |
| `REQUIREMENTS.md` | 存在，v1.16，`Accepted` | 唯一需求权威源。本文承接：NFR-SEC-01～03（P0 核心）、NFR-REL-04、NFR-DEP-01/04、REQ-05/12（支撑）、SEL-01/02（支撑）、PM-01/05/06/09（支撑）、MDL-18/19/20（支撑）、CON-03（协作）、NFR-SEC-07（脱敏消费）、NFR-MNT-01/04（构建红线）、附录 D 第 12 项（名称精确等值）；AT-02/20/22/34（支撑） |
| `ARCHITECTURE.md` | 存在，v0.11，`Draft`（待评审） | 唯一架构权威源。§2.3 分层规则（L3 允许 L2/L1、同层按 §3.5 有向依赖、禁 Widgets）、§3.5 依赖表登记 **io→core,diagnostics（接口依赖）**、§6.6 外部资源三段边界、§6.7 `.rwpack` 传输封装、§7.9 导入安全三件套直接约束本文。§3.5 未登记 io→project / io→runtime / project→io 边——本文按注入式协作设计并登记待裁决（P-IO-1，关联 project.md P-PR-5） |
| `DETAILED-DESIGN.md` | 存在（2026-09-10 磁盘版） | 20 单元状态表登记 io 为"待产出"、主 WP WP-E/WP-I；本文产出后该表由治理流程同步，本文不代改 |
| `development-task-breakdown.md` | 存在（2026-09-10 磁盘版） | §2.12 登记 WP-11-T01（本卡）～T07；§4.2 登记 O-09（P-PR-5）、O-17（P-RT-6）——本文 §15.3 给出处置答复；§5.1/§5.5 构建与测试框架约定（CMake 双模式、googletest 经 vcpkg）为本文 §3.3/§11 的执行基线 |
| `units/core.md` | 存在，v0.1，`Draft`（未冻结） | 本文消费其 ObjectId/Digest256/ContentVersion/ContentDigester/SourcedValue/UnitToken/tryParse/DiagnosticRecord 等契约（§3.2 逐项登记）；其 §15.3 P-AR-1 已建议 SafePath/BudgetGuard 类型与实现均归 io——本文承接执行（IO-D01） |
| `units/testkit.md` | 存在，v0.1，`Draft`（未冻结） | 本文验证方案（§11）消费其 TempDir/FaultInterceptor/契约断言与 `ird-test-report.json` 追溯字段；其 §10.2 已登记 io 侧义务（写入口 fake、CSV 方言 roundtrip 用例）——本文 §11/§13 承接 |
| `units/project.md` | 存在，v0.1，`Draft`（未冻结） | §4.1 `.rwdesign` 目录总表（资源区/`.staging/tmp/`/lock 口径）、§5.7 固化服务接口（SolidifyRequest/SolidifyResult 已冻结形状）、§5.8 另存为/包/升级、§8.8～§8.11 生命周期、§13.2 io 行（本文须冻结的相邻契约：ZIP 编解码、SafePath/BudgetGuard 注入形态、外部源检测接口）——本文 §7/§8/§9/§13 逐项答复 |
| `units/runtime.md` | 存在，v0.1，`Draft`（未冻结） | §8.6 IRuntimeResourceProvider/ResourceBytes/ResourceRef 契约（io 实现，运行时注入）、§13.2 io 行（本文须冻结：资源读取/检测/预算实现、accessVersion 语义）、§15.3 P-RT-6（ResourceBytes 指针生命周期由本文裁决）——本文 §8.6/§9.7/§15.3 裁决关闭 |
| `units/diagnostics.md` | 存在，v0.1，`Draft`（未冻结） | §8.6 已登记 io 义务：SafePath 违规/BudgetGuard 超限（比较型三要素）/CSV 逐行错误的诊断形状与 IO-\* 码注册协议——本文 §5.8/§15.1 落实；码值收编随实现期注册冻结 |
| `units/execution.md` | 存在，v0.1，`Draft`（未冻结） | N-7 登记 io 拥有外部资源解析与包编解码；后台任务/取消/进度归 execution——io 不自建线程（§9.13 线程总则） |
| `units/evidence.md`、`units/policy.md`、`units/ui.md` | 存在，v0.1，`Draft`（未冻结） | evidence：CON-03 固化门禁（io 不判定证据资格）；policy：工程策略不含 io 预算（IO-D06 论证）；ui N-6：向导仅收集用户选择、外部资源解析归 io |
| `units/modeling.md` 等 10 个业务/编报单元 | **不存在**（待产出） | modeling/requirements/selection/reporting/optimization/workflow 详设未产出；本文需要其消费处给最小依赖契约并登记交接（§13），不代写对方详设 |
| io 构建骨架 | 存在：`industrialrobot/io/include/sdurws/ird/io/README.md`（占位，不参与编译）；`industrialrobot/CMakeLists.txt` 注册 `sdurws_ird_io` INTERFACE 目标 | 源码随本文任务卡（§12，对应 WP-11-T02 起）落地；INTERFACE→STATIC 升级、`_test`/`_contract_test` 目标随任务登记，不预建空目标 |
| 构建缓存 | `build/CMakeCache.txt` 实测：Visual Studio 17 2022（MSVC x64）、Qt 6.11.1 | 与 core.md §1.4 同源事实；Windows 文件/路径 API 按 Microsoft Learn 文档口径设计（§4.2），不凭记忆超诺 |
| `old/` | 不存在于磁盘（仓库根实测；与 REQUIREMENTS v1.10 声明不符，core.md R-5 已登记） | 从头构建口径不受影响；本文不引用其任何机制 |

### 1.3 设计目标

1. **统一入口防护（SA-14）**：所有外部输入通道（URDF/Xacro 展开、网格、CSV、JSON、目录包、项目包）统一经 io 的 SafePath＋BudgetGuard＋数据-only 解析防护；防护设施不得分散到各导入通道（WP-11-T01 禁止项）。
2. **可逆编码与单一实现（SA-12/NFR-SEC-03）**：CSV 转义只发生在文件层；io 的读写器是唯一转义/还原实现；本软件导出的 CSV 携带方言标识，任意原文（含自带 `=、+、-、@、'` 前缀）roundtrip 逐字符一致；无标识外部 CSV 原样读入。
3. **路径不作身份**：资源身份＝resourceId＋内容摘要（SHA-256）；路径（含包内路径、文件名、压缩条目名）仅作寻址与追溯提示，绝不充当对象身份或引用键（CON-01 语义在 io 侧的延伸）。
4. **失败不污染目标**：取消、校验失败、预算超限、清理失败后，目标项目目录与正式 `.rwpack` 文件不留可被误识别的半成品（PM-05：失败不留目标目录；取消即清理临时区）。
5. **职责不越界**：io 拥有解析与安全，不拥有项目事务/修订/锁/模型真值/运行时编译/工程策略/证据资格/界面；`.rwdesign` 唯一写权归 project，`.rwpack` 仅是 ZIP 传输封装、不是第二套项目权威格式（PM-05/§17 表注）。
6. **可直接落地**：阶段 A 交付 SafePath、BudgetGuard、CSV/JSON 读写器、资源读取与检测、ZIP 通道与包导入导出 io 侧设施、固化前读取契约、IO-\* 诊断、取消与清理、测试替身与故障注入用例（§12）；业务向导、工程判定、真实建模/选型/报告链按阶段经接口承接（§13）。

### 1.4 语言、标准库与构建约束（与 core.md §1.4/project.md §1.4 同源，按 io 需要收窄）

| 项 | 约束 | 说明 |
| --- | --- | --- |
| C++ 标准 | **C++17**（各目标显式 `CXX_STANDARD 17`，不用 C++20+ 特性） | 与 core.md 决策 D-01 一致；MSVC 2022 完整支持 |
| 编译器/生成器 | Visual Studio 17 2022（MSVC x64） | 按 MSVC 设计；Windows 路径语义（§4.2）按官方文档口径 |
| Qt | **io 零 Qt**（不包含任何 Qt 头） | L3 允许 Qt Core/Gui（ARCH §2.3），但 io 无界面职责；保持计算侧纯净、便于模型测试直接调用（NFR-MNT-01 精神） |
| 第三方库 | 压缩（ZIP）与 XML 解析库**经 vcpkg** 引入；候选与选型冻结登记于 P-IO-3；禁止引入 Boost 等额外运行库 | 依赖清单入安装包（NFR-SEC-05）、支持企业离线安装（NFR-DEP-03）为选型硬约束 |
| RobWork 基线 | 允许依赖 L1（`rw::common` 文件与工具设施；`sdurw_math` 等目标经 vcpkg/构建集成） | L3 允许 L1（ARCH §2.3）；io 不修改框架源码（SA-02），如需补丁走 `patches/PATCHES.md` 登记 |
| 文件系统访问 | `std::filesystem` ＋ Win32 API（reparse point/锁语义/sharing violation 区分） | `std::filesystem` 不暴露 reparse point 细节，Win32 补充（§4.2/§4.3） |
| 异常 | 对外接口一律**非抛出**（`IoResult<T>`/输出参数＋错误码）；异常仅允许在内部实现被捕获转换 | 承接 core try\* 惯例；跨单元边界零异常逃逸 |

---

## 2. 需求承接与职责边界

### 2.1 拥有／消费／不拥有总表（本节先行给出，全文以此为准）

**io 拥有**（实现责任归 io，其他单元不重复实现——NFR-MNT-04）：

| # | 拥有项 | 上游依据 | 设计落点 |
| --- | --- | --- | --- |
| O-1 | CSV 方言识别（含方言标识行）、规范解析、可逆编码与流式读写器（唯一转义/还原实现） | NFR-SEC-03（M-15＋R5）、REQ-05（支撑）、AT-02 | §5、§9.3/§9.4 |
| O-2 | JSON 受限读写器（结构安全校验、canonical 输出、版本字段执行侧） | REQ-12（支撑）、OPT-12（支撑）、PM-06（执行侧） | §6、§9.5 |
| O-3 | `SafePath`：路径规范化、路径穿越/reparse point 逃逸防护（管辖范围＝项目内持久化资源引用＋项目包解包成员） | NFR-SEC-01、ARCH §7.9 | §4.3、§9.1 |
| O-4 | `BudgetGuard`：大小/数量/深度/压缩展开/比例/行数/字段长度/网格规模/引用深度/临时空间预算 | NFR-SEC-02、ARCH §7.9 | §4.5、§9.2 |
| O-5 | 外部资源安全读取（用户显式选择源的一次性读取；含摘要计算与读取快照） | PM-01/NFR-SEC-01（R4 消歧）、ARCH §6.6 | §8.2、§9.6 |
| O-6 | 外部源缺失/变化检测（Missing/Changed）实现 | NFR-REL-04、PM-01、project.md §13.2 io 行 | §8.3、§9.8 |
| O-7 | 目录包（选型 CSV 目录包）导入的文件层校验：清单核对、文件间引用存在性、路径/预算防护 | SEL-01/02（支撑，字段字典归 selection） | §7.8、§13 |
| O-8 | `.rwpack` 导入导出的 ZIP 编解码、包内路径校验、展开预算、哈希全量校验、临时区管理与校验报告 | PM-05、NFR-SEC-01/02、ARCH §6.7 | §7、§9.9 |
| O-9 | URDF/Xacro/网格/WorkCell XML 的**文件层**读取：编码识别、依赖树枚举、循环检测、预算、快照摘要（XML 语义与业务解析归 modeling/runtime） | MDL-03/18/19（支撑）、runtime.md N-7 | §6（资源章）、§9.6 |
| O-10 | 临时导入/导出/固化中转目录的创建、RAII 清理与失败残留报告 | PM-05（取消即清理）、project.md §4.1 `.staging/tmp/` | §7.5、§8.4 |
| O-11 | 导入校验报告（结构化、可脱敏呈现）与取消/清理契约 | PM-05、ERR-01（支撑） | §7.4、§9.9/§9.10 |
| O-12 | 输入格式错误与安全违规的原始诊断（IO-\* 码族、逐行列定位、比较型预算三要素） | NFR-SEC-01~03、diagnostics.md §8.6 | §5.8、§15.1 |

**io 消费**（经登记边或注入，不拥有）：

| # | 消费项 | 提供方 | 形态 |
| --- | --- | --- | --- |
| C-1 | 身份/摘要值类型（ObjectId、Digest256、ContentVersion、ContentDigester）、SourcedValue、UnitToken、tryParse/tryConvert | core | 接口依赖（编译链接；ARCH §3.5 登记边） |
| C-2 | 诊断契约（DiagnosticRecord、DiagCode、比较型字段、ConfirmableFinding 工厂、脱敏设施、StableCodeRegistry 注册协议） | diagnostics | 接口依赖（编译链接；ARCH §3.5 登记边） |
| C-3 | `.rwdesign` 只读快照/字节源、发布入口（包导入 rename 发布、固化入对象库）、`.staging/tmp/` 使用授权 | project | 运行时注入（P-IO-1；project.md §13.2 io 行方向） |
| C-4 | IRuntimeResourceProvider 契约（io 实现该接口）、ResourceRef 记录契约、编译期资源复查时点 | runtime | 契约消费＋运行时注入（runtime.md §3.2/§8.6/§13.2） |
| C-5 | 后台任务执行、进度与取消令牌（io 不自建线程） | execution | 运行时注入/调用方驱动 |
| C-6 | 业务字段字典与业务解析契约（CSV 列映射、JSON profile、目录包字段、单位语义） | modeling/requirements/selection 等 | 装配期注册（JsonProfile/字段字典由格式所有者注册，io 执行校验） |
| C-7 | Windows 文件系统 API、vcpkg 压缩/XML 库（官方能力） | L1/系统 | 直接使用 |

**io 不拥有**（明确排除，防职责重复与越权）：

| # | 不拥有项 | 所有者 | 上游依据 |
| --- | --- | --- | --- |
| N-1 | `.rwdesign` 的 HEAD、修订、分支、草稿、事务与写锁（含锁文件——io 不创建/删除/重建 `lock`） | project | ARCH §6.4/§6.8、project.md D-03 |
| N-2 | RobotDesign、CanonicalModel、WorkCell/DynamicWorkCell 编译与适配 | modeling/runtime | ARCH §7.3、SA-04 |
| N-3 | RunRegistry、结果当前性、证据资格判定（资源缺失≠工程不可行；未固化阻断正式结论的**判定**归 evidence 门禁） | execution/evidence | ARCH §4.5/§6.6、CON-03 |
| N-4 | EngineeringPolicySet、碰撞算法、工程判定阈值 | policy | ARCH §7.5、SA-06 |
| N-5 | 业务语义校验的最终结论（单位合法性最终判定、必填字段语义、型号唯一性、数值工程范围） | requirements/selection/modeling | REQ-05/06、SEL-02 责任行 |
| N-6 | UI 对话框、向导步骤、进度呈现、报告渲染 | ui/workflow/reporting | ui.md N-6、RPT-02 |
| N-7 | 项目历史发布（包导入的最终发布＝rename 到目标目录；另存为整体复制流程） | project | PM-05、project.md §8.8/§8.10 |
| N-8 | schema 升级器（未来版本项目的升级执行） | project | NFR-DEP-04、PM-06、project.md §8.11 |
| N-9 | 旧代码 `old/` 历史实现的任何机制（含其 CSV/包/导入行为） | —（不继承） | RV-13、从头构建口径 |

### 2.2 直接承接的需求

| 需求 | 条目要点（摘要） | io 角色 | 设计落点 |
| --- | --- | --- | --- |
| NFR-SEC-01 | 路径穿越防护：**项目内持久化资源引用与项目包解包成员不得逃逸项目资源区**；用户显式选择的外部源一次性读取与 PM-01 外部引用记录不在此限；转正式固化见 PM-01/CON-03 | **实现责任方**（SafePath） | §4.3、§7.4、§9.1 |
| NFR-SEC-02 | 资源预算：单文件大小、XML/压缩包递归深度、解压总量上限，超限拒绝＋诊断 | **实现责任方**（BudgetGuard） | §4.5、§9.2 |
| NFR-SEC-03 | CSV 数据-only 解析；导出对 `=、+、-、@` 开头字段转义；方言标识＋可逆编码（`'` 自转义、剥离恰好一个前导 `'`）；无标识外部 CSV 原样读入；转义形式不入结构化层 | **实现责任方**（唯一转义/还原实现） | §5、§9.3/§9.4 |
| NFR-REL-04 | 外部网格/目录缺失或变化**可检测**（检测能力规范正文） | **实现责任方**（检测能力；用户流程归 PM-09） | §8.3、§9.8 |
| PM-05（支撑） | `.rwpack`＝ZIP 传输封装（解包逐字节还原目录与哈希）；导入预算/穿越防护/全量校验/失败不留目标目录＋校验报告；导出/导入后台进度可取消、取消即清理临时区 | **io 侧实现责任方**（发布/复制流程归 project） | §7、§9.9 |
| PM-06（支撑） | 旧格式稳定只读拒绝＋诊断码；未来 schemaVersion 只读拒绝＋升级指引数据 | **执行侧支撑**（判定与升级器归 project；io 提供格式探测数据） | §6.4、§7.4 |
| REQ-05（支撑） | CSV 坐标表导入：字段映射、单位预览、逐行错误；不执行公式语义 | **解析与逐行错误实现**（映射/预览/单位归 requirements/ui） | §5、§13 |
| REQ-12（支撑） | 需求定义 JSON 导入/导出（与 CSV 同一字段字典与安全规则），导出为副本 | 读写器＋结构校验实现 | §6.6、§9.5 |
| SEL-01/02（支撑） | 电机/减速器 CSV 目录包：清单、主表、能力曲线表、兼容关系表；导入校验文件清单/文件间引用/单位/必填/唯一性/数值范围 | **文件层校验实现**（字段字典/单位语义/唯一性归 selection） | §7.8、§13 |
| MDL-18（支撑，R2） | 任意外部 WorkCell（.wc.xml）反向导入（有损提取、草稿） | 文件层安全读取＋预算（提取与提取报告归 modeling） | §6.5、§13 |
| MDL-19（支撑） | Xacro 受控预处理（参数、展开环境、离线依赖）；展开失败可定位诊断；展开结果进同一安全边界 | 展开护栏（预算/循环/逃逸）＋文件层（展开引擎与语义归 modeling，阶段 B） | §6.2 |
| MDL-20（支撑） | 规范模型导出/导入自有工件 roundtrip；导出失败恢复先前输出 | 原子写出设施（导出格式与语义归 modeling） | §4.6、§9.10 |
| CON-03（协作） | Verified/正式报告引用的外部资源必须有项目内不可变副本并受引用保护 | 读取/检测/复制实现（发布与引用保护归 project） | §8 |
| NFR-DEP-01（约束） | 首版仅 Windows x64 正式验收 | 路径语义按 Windows 设计（§4.2） | §4.2 |
| NFR-DEP-04（协作） | 项目格式带 Schema 版本；拒绝重构前格式；前向升级器 | io 读写器携带/校验版本字段（权威格式归 project） | §6.4 |
| NFR-SEC-07（消费） | 崩溃转储与日志不记录凭据；用户本机路径按配置脱敏 | io 诊断遵守脱敏（路径参数经 diagnostics 脱敏设施） | §5.8、§15.1 |
| AT-02（支撑） | CSV 含错导入：正确行保留、错误定位到列与原文；转义 roundtrip 含自带前缀样例 | 验证对象（§11 IO-V01～V05） | §11 |
| AT-20（支撑） | 包导出/导入主线、取消不留半成品、URDF 外部引用转正式前固化 | 验证对象（§11 IO-V14～V22） | §11 |
| AT-22（支撑） | HTML/JSON/CSV 多格式逐字段一致 | CSV/JSON 读写器确定性为一致性基础 | §5.6、§6.3 |
| AT-34（支撑） | 一站式导出（研究结果 JSON/候选 CSV/任务明细/审计 CSV/Markdown/模型包） | JSON/CSV/ZIP 通道支撑 | §13 |

### 2.3 明确不做（非目标，与 §2.1 不拥有表互为补充）

1. 不实现第二套项目持久化格式：`.rwpack` 是 ZIP 传输封装，包内树仅逐字节镜像 `.rwdesign` 布局（§7.1），不新增任何包级权威语义（分支状态、当前指针、锁、`.staging` 等不入包）。
2. 不把任何路径、文件名、压缩条目名当作对象身份或引用键；`sourcePathHint` 仅追溯提示（runtime.md §8.6 同源）。
3. 不以"已解析"代替安全边界：解析成功仅代表结构与安全检查通过，不代表业务输入合法（§2.5 正交表）；io 不产出业务可行性结论。
4. 不代行业务向导、界面布局、进度对话框与工程判定（含"是否允许正式报告"——归 evidence/project）。
5. 不在 io 内实现 RobWork 模型编译、碰撞、求解或其任何业务算法。
6. 不读取/写入 `.rwdesign` 内部条目绕过 project 存储端口（含对象库直读——§8.6 经 IProjectBytesSource 注入）。
7. 不复制、不恢复 `old/` 历史实现的 CSV/包/导入行为；其功能仅经 REQUIREMENTS 附录 A 台账作范围对照。
8. 不自带后台线程池与超时强杀（归 execution）；io 全部长操作为**可取消的同步库调用**（§9.13）。
9. 不支持读取无方言标识外部 CSV 时执行任何前缀剥离或字段改写（NFR-SEC-03 明文）。
10. 不对无 BOM 且非 UTF-8 的外部文本做编码猜测转码（仅按 §5.2 声明的编码集读取，超出即稳定拒绝）。

### 2.4 关键术语

| 术语 | 定义 | 来源 |
| --- | --- | --- |
| 方言标识行 | 本软件导出 CSV 的首行标记（`#rwcsv1 …`，含版本与方言参数；§5.1） | 本文（承接 NFR-SEC-03"文件头标记行，含版本"） |
| 可逆编码 | 转义仅发生在 CSV 文件层：导出对以 `=、+、-、@、'` 开头的文本前置一个 `'`；导入对带方言标识文件剥离恰好一个前导 `'`；roundtrip 逐字符一致 | NFR-SEC-03（M-15＋R5） |
| 一次性读取 | 导入向导对用户显式选择的外部源文件的读取（NFR-SEC-01 例外；仍受预算管辖） | PM-01/NFR-SEC-01（R4） |
| 外部引用记录 | 草稿期登记的 `{绝对路径＋内容哈希}` 记录（不以裸路径充当项目资源引用） | PM-01、ARCH §6.6 |
| 固化（Solidify） | 转正式前将外部源复制为项目内不可变副本（objects/）并受引用保护；经命令进入事务 | CON-03、ARCH §6.6、project.md §5.7 |
| 读取快照 | 某次读取时刻的 `{size, mtime, SHA-256}` 三元组（变化检测基准） | 本文 §8.2 |
| 一致数据视图 | 包导出开始时经 project 快照得到的文件清单＋字节源（HEAD 闭包＋所选树）；io 不扫描磁盘 | project.md §5.8/§8.10（D-17） |
| 已验证临时目录 | 包导入九步协议中通过全量校验、等待 project 发布的解包产物目录（§7.3） | 本文 §7 |

### 2.5 四类错误正交关系表（格式／安全／业务／事务）

io 的诊断与状态语义建立在四类错误的正交性上（ERR-01"执行状态、工程判定和诊断类别正交"的 io 侧落点）：任一类不得被当作另一类上报或推断。

| 维度 | 格式错误 | 安全错误 | 业务错误 | 事务错误 |
| --- | --- | --- | --- | --- |
| 定义 | 输入不满足声明格式的语法/结构约束 | 输入触发安全边界（穿越、预算、炸弹、注入防护） | 输入结构合法但违反业务语义（单位、必填、范围、唯一性） | 项目状态机/并发/发布层面的失败（修订过期、发布冲突、失权） |
| 首检方 | **io**（读写器） | **io**（SafePath/BudgetGuard/解包校验） | 业务单元（requirements/selection/modeling…） | project/execution |
| io 码族 | `IO-FORMAT-*` | `IO-SEC-*` | —（io 不产出） | —（io 不产出；io 自身操作失败用 `IO-RES-*`/`IO-PACK-*` 表达，不冒充事务错误） |
| 典型例 | CSV 引号不闭合；JSON 重复键；包 manifest 损坏 | `../` 逃逸；zip 条目为符号链接；解压超预算 | 力矩单位未知；Must 必填缺失；型号重复 | expectedRevision 过期；目标目录发布时被占用 |
| 用户后果 | 定位（行/列/路径）＋可修改输入后重试 | 拒绝＋建议动作；**不产生任何业务结论** | 就绪状态降级/阻断（REQ-06 口径） | 重试/恢复诊断（project 侧） |
| 与"工程不可行"关系 | 无关 | 无关 | 业务判定的输入之一 | 无关 |
| 部分成功 | 按 §5.8 粒度允许（正确行保留） | **不允许**——安全失败即整体拒绝 | 按业务规则 | 不允许（原子性） |
| 正交性规则 | 格式错误≠业务非法：io 不得以格式错误推断"输入未完成"或不可行 | 安全拒绝不得转化为业务诊断；不得以"已解析"绕过 | 业务单元不得把结构合法当业务合法 | io 不得以 IO 失败冒充事务失败（如清理失败≠发布失败，§7.6） |

---

## 3. 单元组成、依赖与公共头文件布局

### 3.1 组成（公共头模块）

公共头根：`io/include/sdurws/ird/io/`（命名空间 `sdurws::ird::io`，与骨架 README 一致）。

| 公共头 | 内容（接口/类型） | 章节落点 |
| --- | --- | --- |
| `IoFwd.hpp` | 前向声明与公共别名（`IoResult<T>`、`IoCancelToken`、版本常量） | §9.0 |
| `IoError.hpp` | `IoErrorCode`（稳定 token 枚举）、`IoError`（码＋上下文参数＋原始细节） | §9.0、§15.1 |
| `SafePath.hpp` | `PathRole`、`NormalizedPath`、`ISafePathResolver`、`SafePathRuleSet` | §4、§9.1 |
| `Budget.hpp` | `BudgetDimension`、`BudgetSpec`（默认＋硬上限）、`IBudgetGuard`、`BudgetLedger` | §4.5、§9.2 |
| `Csv.hpp` | `CsvDialect`、`CsvCell`、`RawTable`、`CsvParseReport`、`ICsvReader`、`ICsvWriter` | §5、§9.3/§9.4 |
| `Json.hpp` | `JsonDocument`（受限 DOM）、`JsonProfile`、`JsonParseReport`、`IStructuredDataReader`、`IJsonWriter` | §6、§9.5 |
| `ResourceIo.hpp` | `ResourceSnapshot`、`ResourceContentId`、`ExternalRefProbe`（Missing/Changed）、`IResourceReader`、`IResourceSnapshotter`、`IRuntimeResourceAdapter`（桥接说明） | §6.6、§8、§9.6/§9.7/§9.8 |
| `Package.hpp` | `PackFormat`（rwpack.json/manifest 契约）、`PackageImportReport`、`PackageExportReport`、`IPackageImporter`、`IPackageExporter`、`ISnapshotFileSource` | §7、§9.9 |
| `TempArea.hpp` | `TempAreaSpec`、`ITempAreaManager`（导入/导出/固化中转） | §7.5、§9.10 |
| `AtomicFile.hpp` | `IAtomicFileWriter`（单文件导出原子替换；MDL-20 支撑） | §4.6、§9.10 |
| `IoDiagnostics.hpp` | IO-\* 码表（建议值）、诊断构造助手（比较型预算三要素、逐行列定位、脱敏接入） | §5.8、§15.1 |
| `IoSession.hpp` | 装配入口 `IoRuntime`（工厂＋注册表：JsonProfile/字段字典/注入端口绑定） | §9.11 |

### 3.2 依赖（含 core/diagnostics 契约消费状态登记）

| 依赖边 | 形态 | 用途 | 上游登记状态 |
| --- | --- | --- | --- |
| io → core | 接口依赖（编译链接） | ObjectId/Digest256/ContentVersion/ContentDigester（摘要算法唯一，D-05：SHA-256）、SourcedValue/UnitToken/tryParse（数值与单位原文保留口径）、DiagnosticRecord 基类型 | ARCH §3.5 已登记；core.md v0.1 Draft 未冻结（消费项逐项见下表） |
| io → diagnostics | 接口依赖（编译链接） | DiagCode 注册协议、比较型字段、脱敏设施、IDiagnosticsSink | ARCH §3.5 已登记；diagnostics.md v0.1 Draft 未冻结 |
| io → L1（RobWork `rw::common`/文件设施） | 实现链接 | 文件读取辅助、XML 设施（选型见 P-IO-3）；零源码修改（SA-02） | ARCH §2.3 L3 允许 L1 |
| io ←（注入）project | 运行时注入 | IProjectBytesSource（对象库字节，§8.6）、发布入口（包导入 rename、固化入对象库，project 发起调用 io） | **未登记编译边**；P-IO-1（关联 P-PR-5）——本文按注入式设计，接口不因裁决结果变化 |
| io ←（注入）runtime | 运行时注入 | runtime 定义 `IRuntimeResourceProvider`（其 Resource.hpp），io 提供实现；桥接经 `IoRuntime` 装配（§9.7） | runtime.md §3.2 已按"运行时注入"登记；编译边未登记（P-IO-1） |
| io ←（注入）execution | 运行时注入/调用方驱动 | 取消令牌与进度回调（io 不自建线程） | execution.md N-7 |
| io ←（注册）业务单元 | 装配期注册 | JsonProfile/字段字典（格式所有者注册，io 执行校验） | §6.2、§13 |

core 契约消费状态（core.md v0.1 签名基线；其冻结版本变更时本文同步）：

| 消费项 | 用途 | 状态 |
| --- | --- | --- |
| `ObjectId` | 资源对象身份（ResourceRef.resourceId 同源） | core.md §4 已定义（Draft） |
| `Digest256`/`ContentDigester`（SHA-256） | 全部内容摘要（资源快照、包 manifest、固化比对）——**io 不引入第二种摘要算法**（SA-12/NFR-MNT-03） | core.md §4.2（Draft） |
| `ContentVersion` | 固化副本 materializedVersion（project 分配；io 透传） | core.md §4.2（Draft） |
| `SourcedValue`/`UnitToken`/`tryParse` | CSV/JSON 数值与单位原文保留与提示性解析（最终判定归业务单元） | core.md §5（Draft）；core.md §13.2 io 行 |
| `DiagnosticRecord`/比较型字段 | IO-\* 诊断构造 | diagnostics.md §4（Draft） |

### 3.3 命名空间、目标与 CMake 集成

| 项 | 约定 |
| --- | --- |
| 命名空间 | `sdurws::ird::io`；测试 `sdurws::ird::io::test` |
| 库目标 | `sdurws_ird_io`（骨架 INTERFACE 占位 → IO-T01 升级 STATIC），别名 `RWS::ird::io`；链接 `sdurws_ird_core`＋`sdurws_ird_diagnostics`＋L1 文件/XML 目标（选型 P-IO-3 冻结后登记）；**零 Qt、零 Widgets** |
| 测试目标 | `sdurws_ird_io_test`（单元/黄金）、`sdurws_ird_io_contract_test`（契约/故障注入）——googletest 经 vcpkg `find_package(GTest CONFIG REQUIRED)`（development-task-breakdown §5.5），随 IO-T06 登记，不预建 |
| 装配桥接 | 若 P-IO-1 裁决为"补登直连边"，project/runtime 侧直接链接 io；若维持注入式，L5 装配层持有 `IoRuntime` 实例并注入（§9.11）——两种形态下 io 公共头零改动 |
| 双模式构建 | 独立冒烟模式（骨架注释口径）与 RobWorkStudio 集成构建均零错误（WP-11-T02 验收） |

### 3.4 源码目录布局

```text
industrialrobot/io/
├─ include/sdurws/ird/io/          # §3.1 公共头（README.md 已存在，引用本文 §9）
├─ src/                            # 实现（Csv.cpp/Json.cpp/SafePath.cpp/Budget.cpp/
│                                  #   ResourceIo.cpp/Package.cpp/ZipChannel.cpp/TempArea.cpp/...）
├─ test/                           # 单元/黄金测试（IO-T06）
├─ contract_test/                  # 契约与故障注入测试（IO-T06；testkit FaultInterceptor/TempDir）
└─ CMakeLists.txt                  # IO-T01 新建（STATIC 升级＋测试目标）
```

---
## 4. 路径安全与预算模型

### 4.1 路径角色模型（io 视角的七类路径）

SafePath 的规则因路径**角色**而异——同一条物理路径在不同角色下适用不同校验。角色由调用方在获取 resolver 时声明（§9.1），io 不从路径形态推断角色。

| # | 角色（PathRole） | 来源 | 典型值 | 校验强度 | 说明 |
| --- | --- | --- | --- | --- | --- |
| P-1 | 用户提供的源路径（UserSource） | 用户在向导/对话框显式选择（打开文件、目录、包） | 任意本地/UNC 路径 | **准入校验**（存在、可读、类型匹配、预算入口；**不施加资源区逃逸检查**——NFR-SEC-01 例外） | 一次性读取与外部引用记录走此角色；仍受预算管辖 |
| P-2 | 项目根路径（ProjectRoot） | project 打开协议 | `<dir>\.rwdesign` 的 `<dir>` | 规范化＋等价类归一（§4.2.4）；所有权归 project，io 仅接收规范化结果 | 同一项目经不同大小写/斜杠路径打开时用于归一 |
| P-3 | 导入临时根（ImportStaging） | io 创建（TempArea） | `<发布目标父目录>\.rwpack-import-<8hex>\` | io 全权管理；**必须与发布目标同卷**（发布＝同卷 rename，§7.3）；隐藏前缀 `.` | 包导入九步协议的工作区 |
| P-4 | 包内相对路径（PackEntry） | zip 条目名/manifest 路径 | `payload/objects/obj-…/cv…` | **最严校验**：纯相对、正斜杠、无盘符/UNC/前导 `/`、无 `..`/`.` 段、无 NUL/控制字符、大小写折叠后无重复条目、非 reparse point（§7.4） | SafePath 管辖范围之二（NFR-SEC-01 明文） |
| P-5 | 项目内持久化资源引用（ProjectResourceRef） | 项目对象内的资源引用（相对项目根的持久化路径） | `objects/<oid>/<cv>`、`catalog/<id>/<ver>/…` | **不得逃逸项目资源区**（`objects/`、`catalog/`；含 reparse point 检查） | SafePath 管辖范围之一（NFR-SEC-01 明文） |
| P-6 | 固化/缓存中转路径（StagingTmp） | project 授权的 `.rwdesign\.staging\tmp\`（project.md §4.1） | `.staging/tmp/solidify-<id>/…` | 写入仅经 project 端口授权的中转位；io 不越界写其他 `.rwdesign` 条目 | 固化复制中转、编译临时物（runtime 侧约定同源） |
| P-7 | 导出目标路径（ExportTarget） | 用户选择（`.rwpack` 文件、报告/CSV/JSON 工件） | 任意可写位置 | 可写性预检＋原子替换（§4.6）；**不覆盖未经确认的既有文件**（ReplacePolicy，§9.10） | 报告导出、另存输出、包导出共用 |

**不在模型内的路径**：`old/` 历史路径（不读取）；`.rwdesign\lock`（io 永不创建/删除/重写——锁归 project SA-17，project.md D-03"锁文件不删除重建"）；用户配置目录（PM-14 用户设置归 ui/project，io 不持久化任何用户设置）。

### 4.2 Windows 路径规范化

以下按 Microsoft Learn 对 NTFS/ReFS 与 Win32 文件 API 的公开口径设计；实现期以官方文档复核（project.md §7.2 同源纪律），不凭记忆超诺。

#### 4.2.1 规范化算法（`normalizeForRole`）

1. **输入分类**：空/超长（>32,767 UTF-16 码元，拒绝 `IO-SEC-PATH-TOO-LONG`）；相对 vs 绝对（盘符 `C:\`、根相对 `\x`、UNC `\\server\share\…`、设备 `\\?\`/`\\.\` 前缀）；网络驱动器映射盘符（如 `Z:`→UNC）**不做展开**（映射是会话态；仅在等价类归一时用 `QueryDosDevice` 做提示性比对，失败不阻断）。
2. **统一分隔符**：`/` 与 `\` 均合法，规范化为 `\`（比较用）；保留原样副本（`display`）用于诊断脱敏呈现。
3. **词法规范化**：解析为段序列，消解 `.`（剥离）与 `..`（与前一非 `..` 段抵消；段序列被抵穿根＝**路径穿越尝试**→`IO-SEC-PATH-ESCAPE`）。绝对路径根成分（盘符/UNC share）不可被 `..` 抵穿。
4. **大小写归一（比较用）**：NTFS/ReFS 默认大小写不敏感——路径**等价类键**＝全串大小写折叠（Windows 上对 A-Z 折叠为 a-z；不做 Unicode 简单折叠之外的本地化折叠）。等价类键仅用于：缓存键、重复条目检测、同一项目多路径打开归一；**不写回、不替换用户路径**。
5. **长路径**：内部处理统一使用 `\\?\` 扩展路径前缀形式与 `std::filesystem::path`；对外返回/诊断使用去除 `\\?\` 的显示形式。支持的段长/总长上限：单段 ≤255 UTF-16 码元、总长 ≤4096（io 自设上限低于 OS 上限，P-IO-4 数值表内）。
6. **保留名**：段名等于 `CON/PRN/AUX/NUL/COM1-9/LPT1-9`（不含扩展名或等于含扩展名形式）→ 对 P-4/P-5 角色拒绝（`IO-SEC-PATH-RESERVED`）；P-1/P-7 角色放行（系统自身规则裁决）。
7. **尾随空白/点**：NTFS 剥离段尾 `.` 与空白——P-4/P-5 角色含此形态即拒绝（防止"写入名 ≠ 引用名"的等价分裂）；P-1/P-7 放行。

#### 4.2.2 UNC、盘符与 `.`/`..`

| 输入形态 | 处理 |
| --- | --- |
| `C:\x\y` | 绝对路径；角色校验后接受 |
| `\\server\share\x` | UNC 绝对路径；接受（P-4 禁绝——包内不得出现） |
| `\\?\C:\x`、`\\?\UNC\server\share\x` | 剥离 `\\?\` 后按上两条处理 |
| `x\..\y` | 词法消解为 `y`；消解后为相对路径 → P-4/P-5 拒绝（必须纯相对且无上溯） |
| `..\..\x`、`x\..\..\..\y` | 段序列抵穿起点 → `IO-SEC-PATH-ESCAPE`（P-4/P-5/P-6 角色） |
| `C:\x\..\..\y` | `..` 抵穿盘符根 → `IO-SEC-PATH-ESCAPE` |
| `.`、空串 | P-4 拒绝；P-5 视为项目根本身（仅允许作为解析基点，不作资源引用） |

#### 4.2.3 符号链接、junction 与 reparse point

- **检测**：对规范路径上**每一段**执行 `std::filesystem::symlink_status`（不跟踪）＋ Win32 `GetFileAttributes` 的 `FILE_ATTRIBUTE_REPARSE_POINT` 复核；识别 symlink（含符号链接目录/文件）、junction（挂载点）、其他 reparse point（如 OneDrive 占位符）。
- **规则**：
  - P-4（包内）：解包产物**任何成员**（含中间目录）为 reparse point → `IO-SEC-SYMLINK`，整体拒绝（zip 条目的 symlink 属性在展开前即拒绝，§7.4——双保险）。
  - P-5（项目内资源引用）：解析链上任何一段为 reparse point → `IO-SEC-SYMLINK` 拒绝（防"资源区内名字→区外实体"的逃逸；不区分目标是否仍在区内——**一律拒绝**，规则简单可审计）。
  - P-6（中转）：io 自己创建的中转区禁止 reparse point；发现即清理重建会话。
  - P-1/P-7（用户源/导出目标）：**放行**（用户显式选择；跟随由 OS 决定——`std::filesystem` 常规跟踪语义），但读取快照按**最终实体路径**记录（`weakly_canonical` 解析），供变化检测与追溯提示。
- **TOCTOU 窗口**：reparse point 检查与实际打开之间存在窗口——纵深防御为：P-4/P-5 打开一律以 `FILE_FLAG_OPEN_REPARSE_POINT` 拒绝打开 reparse point 本身，并在打开句柄上复核最终路径仍在管辖根内（final-path 复核，§4.4 步骤⑦）。

#### 4.2.4 大小写不同但指向同一对象／同一项目经不同路径打开

- io 的**等价类键**（大小写折叠＋分隔符归一＋词法规范化＋`weakly_canonical` 实路径）用于：TempArea 去重、导入会话互斥、资源摘要缓存键、包条目重复检测。
- 同一项目经 `D:\P` 与 `d:\p\` 打开：project 的 projectId 是项目身份（路径不入身份）；io 侧缓存/临时键按等价类归一，不产生双份会话。
- 等价键计算失败（如网络路径 canonical 不可得）：退化为折叠键＋记录提示性诊断（不阻断用户源角色）。

#### 4.2.5 路径错误四分类（不存在／权限不足／介质只读／锁竞争）

io 将 OS 错误映射为**互斥**的稳定错误（诊断建议动作各异；不把一切失败都报"找不到文件"）：

| IoErrorCode | Windows 信号（例） | 诊断要点 | 重试语义 |
| --- | --- | --- | --- |
| `IO-RES-NOT-FOUND` | `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND` | 定位到规范化路径（脱敏呈现） | 用户修正后重试 |
| `IO-RES-ACCESS-DENIED` | `ERROR_ACCESS_DENIED`/`ERROR_PRIVILEGE_NOT_HELD` | 区分读/写方向与所需权限 | 用户调整权限后重试 |
| `IO-RES-READONLY` | `ERROR_WRITE_PROTECT`／目标卷只读属性/写探测失败 | 提示介质只读（对应 project 只读打开路径） | 换介质/位置 |
| `IO-RES-LOCK-CONFLICT` | `ERROR_SHARING_VIOLATION`/`ERROR_LOCK_VIOLATION` | 提示被其他进程占用（含 PID 若可得）；**io 绝不通过删除/重建对方锁文件来"解决"**（锁归 project SA-17） | 调用方决定等待/放弃 |

### 4.3 SafePath 设计

#### 4.3.1 规则总表

| 规则 ID | 规则 | 适用角色 | 违规码 |
| --- | --- | --- | --- |
| SP-1 | 管辖范围＝P-4 包内成员＋P-5 项目内持久化资源引用（ARCH §6.6 原文口径）；P-1 一次性读取与外部引用记录为例外（仍受预算） | — | — |
| SP-2 | P-4 条目必须纯相对、正斜杠（解析时）、规范化后无 `..`/`.` 段、无根成分 | P-4 | `IO-SEC-PATH-ESCAPE` |
| SP-3 | P-5 引用解析结果（final path）必须落在资源区根（`objects/` 或 `catalog/`）内，逐段无 reparse point | P-5 | `IO-SEC-PATH-ESCAPE`/`IO-SEC-SYMLINK` |
| SP-4 | zip 条目属性含 symlink/设备标志 → 展开前拒绝 | P-3/P-4 | `IO-SEC-SYMLINK` |
| SP-5 | 路径不作身份：SafePath 输出仅含规范化路径与等价键，**不产出任何对象 ID**；身份由 core/project 分配 | 全部 | —（设计约束） |
| SP-6 | 打开句柄 final-path 复核（TOCTOU 纵深） | P-4/P-5 | `IO-SEC-PATH-ESCAPE` |
| SP-7 | 禁止 io 触碰 `lock`（读其内容作诊断亦不允许——锁文件读取归 project） | 全部 | —（设计约束） |
| SP-8 | 保留名/尾随点与空白段拒绝（P-4/P-5） | P-4/P-5 | `IO-SEC-PATH-RESERVED` |
| SP-9 | 长度上限（总长 4096、单段 255） | 全部 | `IO-SEC-PATH-TOO-LONG` |
| SP-10 | 等价类键仅作缓存/去重/互斥；不改变用户可见路径 | 全部 | — |

#### 4.3.2 解析结果（`NormalizedPath`）

```cpp
enum class PathRole { UserSource, ProjectRoot, ImportStaging, PackEntry,
                     ProjectResourceRef, StagingTmp, ExportTarget };
struct NormalizedPath {
    std::wstring        native;      // 规范化原生形式（\\?\ 内部形式）
    std::string         display;     // 脱敏呈现形式（诊断用；UTF-8，去除 \\?\）
    std::string         relKey;      // P-4/P-5：管辖根内的相对键（正斜杠、小写折叠）
    std::string         equivKey;    // 等价类键（折叠＋canonical）
    bool                isUnc;
    std::optional<std::uint64_t> fileSize;   // 若目标存在（提示性；不作身份）
};
```

#### 4.3.3 合法/非法示例

| 输入（P-5，基＝项目根） | 判定 | 说明 |
| --- | --- | --- |
| `objects/obj-abc…/cv-64hex` | ✅ 合法 | 资源区内规范相对引用 |
| `objects/obj-a/../../objects/obj-b/x` | ✅ 词法消解后合法（`objects/obj-b/x`） | 消解后仍在区内；消解前不上报 |
| `objects/../../evil.dll` | ❌ `IO-SEC-PATH-ESCAPE` | 消解后逃逸资源区 |
| `objects/..\..\evil.dll` | ❌ `IO-SEC-PATH-ESCAPE` | 分隔符混用不影响段解析 |
| `OBJECTS/OBJ-A/X` | ✅ 合法（大小写折叠后命中资源区） | NTFS 大小写不敏感 |
| `objects/lnk`（`lnk` 为 symlink 指向区外） | ❌ `IO-SEC-SYMLINK` | 逐段 reparse point 检查 |
| `objects/nul` | ❌ `IO-SEC-PATH-RESERVED` | 保留名 |
| `objects/a./x` | ❌ `IO-SEC-PATH-RESERVED` | 尾随点 |
| `C:\proj\objects\x`（P-5 角色收到的绝对路径） | ❌ `IO-SEC-PATH-ESCAPE`（P-5 要求纯相对） | 绝对形式持久化引用非法 |
| `\\server\share\objects\x` | ❌ `IO-SEC-PATH-ESCAPE` | UNC 不得出现在项目内引用 |

| 输入（P-4，zip 条目） | 判定 |
| --- | --- |
| `payload/objects/obj-a/cv…` | ✅ |
| `payload/../evil` | ❌ `IO-SEC-PATH-ESCAPE` |
| `/etc/passwd`、`C:\evil`、`\\?\C:\evil` | ❌ `IO-SEC-PATH-ESCAPE`（绝对/根成分） |
| `payload/objects/a/b:`（段含 `:`） | ❌ `IO-FORMAT-PACK-ENTRY`（非法字符：NUL、`:`、`*`、`?`、`"`、`<`、`>`、`|`、控制字符） |
| `payload/objects/A` 与 `payload/objects/a`（两同名条目仅大小写异） | ❌ `IO-PACK-DUPLICATE-ENTRY`（折叠键重复——Windows 落盘冲突预防） |
| 条目 symlink（unix mode 位 S_IFLNK / Windows 属性） | ❌ `IO-SEC-SYMLINK` |

接口契约（前置/后置/线程/取消等）见 §9.1。

### 4.4 SafePath 与 BudgetGuard 防护流程

```text
                    ┌────────────────────────────────────────────────────────┐
  外部输入通道      │ ① 角色声明（调用方）                                    │
  (CSV/JSON/资源/  ├─► ② SafePath.normalizeForRole                           │
   目录包/.rwpack) │      词法规范化 → 段校验 → 保留名/长度 → 角色规则 SP-2~9 │
                    │      └─违规─► IO-SEC-PATH-*/IO-FORMAT-* 诊断（拒绝）    │
                    │ ③ 预算开户（IBudgetGuard::openScope）                   │
                    │      单文件大小/数量/深度先行检查 → 违规：IO-SEC-BUDGET-*│
                    │ ④ 打开（reparse 拒绝打开 + final-path 复核 SP-6）       │
                    │ ⑤ 读取循环（流式）                                      │
                    │      每块/每行/每条目：累计预算 → 超限即中止＋诊断       │
                    │      取消检查点（每块/每 1024 行/每条目）→ IO-CANCELLED  │
                    │ ⑥ 摘要（ContentDigester，SHA-256）                      │
                    │ ⑦ 结果 = 已验证字节 + ResourceSnapshot{size,mtime,digest}│
                    └────────────────────────────────────────────────────────┘
  失败/取消路径：⑤⑥⑦ 任一点失败 → 无已验证产物外泄；临时区按 §7.6/§8.4 清理
```

要点：**②在③前**（路径非法不消耗预算配额）；**③在④前**（先拒超限文件再打开，防大文件打开本身成为攻击面）；取消检查点与预算检查点合并（一次分支两查）。

### 4.5 BudgetGuard

#### 4.5.1 预算维度

| 维度（BudgetDimension） | 含义 | 默认值 | 硬上限（不可覆盖） | 超限码 |
| --- | --- | --- | --- | --- |
| SingleFileBytes | 单文件读取/展开大小 | 256 MiB | 2 GiB | `IO-SEC-BUDGET-FILE` |
| TotalBytes | 单次导入/读取会话累计字节 | 2 GiB | 8 GiB | `IO-SEC-BUDGET-TOTAL` |
| FileCount | 文件数（包/目录/依赖树） | 200,000 | 1,000,000 | `IO-SEC-BUDGET-COUNT` |
| DirDepth | 目录深度 | 32 | 64 | `IO-SEC-BUDGET-DEPTH` |
| ArchiveExpandedBytes | 压缩包展开总量（`.rwpack`/zip） | 4 GiB | 16 GiB | `IO-SEC-BUDGET-EXPAND` |
| ArchiveRatio | 压缩比（展开/压缩字节数） | 100:1 | 200:1 | `IO-SEC-BOMB-RATIO` |
| CsvRowCount | CSV 行数 | 5,000,000 | 50,000,000 | `IO-SEC-BUDGET-ROWS` |
| CsvFieldChars | CSV 单字段字符数 | 64 KiB | 1 MiB | `IO-SEC-BUDGET-FIELD` |
| JsonDocBytes | JSON 文档大小 | 64 MiB | 512 MiB | `IO-SEC-BUDGET-JSON` |
| JsonDepth | JSON 嵌套深度 | 64 | 128 | `IO-SEC-BUDGET-JSON-DEPTH` |
| JsonStringChars | JSON 单字符串长度 | 16 MiB | 64 MiB | `IO-SEC-BUDGET-JSON-STRING` |
| MeshVertexCount | 网格顶点数 | 20,000,000 | 100,000,000 | `IO-SEC-BUDGET-MESH` |
| MeshFaceCount | 网格面数 | 40,000,000 | 200,000,000 | `IO-SEC-BUDGET-MESH` |
| IncludeDepth | XML include/Xacro 递归深度 | 16 | 32 | `IO-SEC-BUDGET-INCLUDE` |
| RefGraphDepth | 资源引用图深度（循环检测上限） | 64 | 128 | `IO-SEC-BUDGET-REFDEPTH` |
| TempAreaBytes | 临时区占用（导入展开＋导出暂存） | 8 GiB | min(展开预算×2, 可用磁盘−1 GiB) | `IO-SEC-BUDGET-TEMP` |

**数值口径**：默认值与硬上限为本卡首次给出的**产品默认**（NFR-SEC-02 只要求"上限"存在、未定数值；上游无任何既定数值可冲突）。修订走本文变更记录（§15.5），并随 P-IO-4 提请架构评审可见。**调用方只能收紧（≤默认）或经显式 `OverridePolicy::AllowUpToHard` 放宽至硬上限**；硬上限编译期常量，修改即格式语义变更。

#### 4.5.2 语义细则

| 事项 | 设计 |
| --- | --- |
| 默认值来源 | io 常量表（`BudgetSpec::productDefault()`）；**不属于** EngineeringPolicySet（工程策略＝碰撞/判定阈值等工程语义，ARCH §7.5；预算是资源安全设施，SA-14 归 io） |
| 调用方覆盖 | `BudgetSpec::tighten(dim, value)`（只收紧，永远合法）；`relaxToHardLimit(dim)`（显式标记，导入报告记录放宽项）；包导入通道**禁用放宽**（P-IO-4 冻结口径） |
| 超预算诊断 | 比较型三要素：实际值/上限/单位（diagnostics.md §8.6）；附进度上下文（第几个文件/已展开字节）便于定位 |
| 预算统计可取消 | 是——检查点即取消点（§4.4⑤）；取消不计为超限 |
| 溢出防护 | 累计器为 `uint64_t`＋饱和加法；`ArchiveRatio` 计算在压缩侧与展开侧分别记录后比较（不做除法上溢路径）；条目声明大小（zip 头/manifest）与实际展开双重计数，以**较大者**入账 |
| 多阶段累计 | `BudgetLedger` 跨阶段传递（如 Xacro 展开→URDF 解析→网格读取共用一个 scope）；父 scope 关闭前子 scope 余额回收；**固化流程**的读取/复制/校验三段共用同一 ledger（§8.4） |
| 包导入 vs 普通读取 | 包导入强制 `ArchiveExpandedBytes/ArchiveRatio/FileCount/DirDepth` 四维全开且不可放宽（zip 炸弹主战场）；普通资源读取至少强制 `SingleFileBytes/TotalBytes`；CSV/JSON 通道叠加各自行/深度维 |
| 超限不留半成品 | 超限即中止→临时区清理（§7.6 状态机）；**目标项目目录/正式包文件在发布/原子替换前不存在任何写入**（§7.3/§4.6），结构性保证"不因预算超限留下半成品" |
| 处理时间 | 阶段 A 不设硬超时（避免误杀慢介质）；耗时治理经取消令牌（调用方/execution 拥有超时策略）——登记于 P-IO-4 |

### 4.6 导出文件原子写出（`IAtomicFileWriter`，MDL-20/报告导出支撑）

单文件导出（CSV/JSON/HTML 工件、WorkCell XML、`.rwpack` 文件本体）统一走：

```text
写 `<target>.<8hex>.tmp`（同目录→同卷）→ flush＋FlushFileBuffers →
ReplaceFile（保留 .tmp 失败回滚路径）→ 删除 .tmp 残留 → 报告
```

- 失败/取消在替换前发生：**先前输出完整保留**（MDL-20"导出失败恢复先前输出"）。
- 替换后失败（如残留清理失败）：目标已正确，残留清理失败仅开发级诊断。
- `ReplacePolicy`：`NeverOverwrite`（默认，目标存在→`IO-PACK-TARGET-EXISTS` 同族码）/`OverwriteAtomic`（显式确认后）。

---
## 5. CSV/JSON 解析与可逆编码

### 5.1 方言标识行（本软件导出 CSV 的文件头标记）

NFR-SEC-03（R5）要求：本软件导出的 CSV 携带**方言标识（文件头标记行，含版本）**；导入侧仅对携带标识的文件执行前缀剥离。v1 语法定义如下（io 权限内的格式细则，登记 IO-D05；语法变更＝方言版本升级）：

```text
#rwcsv1 delimiter=, quote=" eol=CRLF encoding=utf-8
<数据行…>
```

| 要素 | 规则 |
| --- | --- |
| 标记 | 首个物理行；以 `#rwcsv1` 开头（`rwcsv`＝格式标识，`1`＝方言版本；无空格分隔）；行内以单个空格分隔 `key=value` 键值对 |
| 键集（v1，封闭） | `delimiter`（`,` `;` `tab`）、`quote`（**仅 `"`**——`'` 与转义符冲突，声明 `quote='` 判格式错误 `IO-FORMAT-CSV-DIALECT`）、`eol`（`CRLF`/`LF`；声明值约束**写出**方，读取方对行尾保持容错，见 §5.2）、`encoding`（**仅 `utf-8`**） |
| 缺失键 | 取 v1 缺省（`delimiter=,`、`quote="`、`eol=CRLF`、`encoding=utf-8`） |
| 未知键/未知值 | `IO-FORMAT-CSV-DIALECT`（拒绝；不猜测） |
| 判定 | 仅当首行**完整匹配**上述语法才视为"携带方言标识"；否则该文件按无标识外部 CSV 处理（§5.2）——判定是字节级精确的，不存在"疑似本软件文件" |
| 与 BOM | 本软件写出的文件**不带 BOM**（UTF-8 无 BOM＋标识行）；读取带标识文件时容忍并剥离 UTF-8 BOM（若存在） |
| 与表头 | 标识行之后第一数据行可为业务表头（由业务列字典约定；io 不强制——§5.4 表头处置） |

### 5.2 外部 CSV（无方言标识）的方言探测与编码

| 项 | 规则 |
| --- | --- |
| 编码 | ① 有 BOM：UTF-8/UTF-16LE/UTF-16BE 按 BOM 判定并剥离；② 无 BOM：尝试严格 UTF-8 校验（全文或首 64 KiB 预检）——通过按 UTF-8，不通过 → `IO-FORMAT-CSV-ENCODING`（稳定拒绝；**不猜测 ANSI/GBK 等编码、不转码**——IO-D09） |
| 分隔符 | 从 `，`？否——候选 `,` `;` `tab`：对表头行与后续至多 100 行统计"引号外出现次数"，取**一致且次数最多**者；无一致结果 → `IO-FORMAT-CSV-DIALECT`（含候选统计摘要，供调用方提示用户显式选择——字段映射 UI 归 requirements/ui） |
| 引号 | `"`（RFC4180 语义：`""` 表字面引号）；无标识文件中 `'` **只是普通数据字符**（不剥离、不作引号——与 §5.3 可逆编码解耦） |
| 行尾 | LF/CRLF/CR 均接受；引号字段内换行按 RFC4180 属字段内容 |
| 小数点 | 读取**不改写**：单元格原文原样保留（`3,14` 与 `3.14` 均为原文；由业务字段字典＋core `UnitToken`/tryParse 判定合法性——io 不在文件层猜小数点） |
| 单位 | 单位不属 CSV 文件层语义：列单位由业务字段字典（表头名→UnitToken）声明；io 只保留原文（SA-12：单位权威归 core，语义归业务单元） |
| 空值 | 空字符串＝空字段原文；"空 vs 缺列"区分：行字段数 < 表头列数 → 尾部视为缺列（`IO-FORMAT-CSV-ARITY` 或按调用方策略截断补空，见 §5.4） |

### 5.3 可逆转义编码（导出转义／导入还原）

完全承接 NFR-SEC-03（M-15＋R5）原文语义，实现规则唯一化（SA-12：io 读写器是唯一实现）：

| 方向 | 规则 | 例 |
| --- | --- | --- |
| 导出（写） | 文本字段以 `=` `+` `-` `@` `'` 之一开头 → 前置**恰好一个** `'`；其余原文原样。数值/空字段不经此规则（由写出方先格式化为文本，§5.6） | `=SUM(A1)` → `'=SUM(A1)`；`-3.5` → `'-3.5`；`'quoted'` → `''quoted'`；`abc` → `abc` |
| 导入（读，带方言标识文件） | 字段以 `'` 开头 → 剥离**恰好一个**前导 `'`；其余原样 | `'=SUM(A1)` → `=SUM(A1)`；`''quoted'` → `'quoted'`；`'abc` → `abc`（原文自带前缀样例） |
| 导入（读，无标识外部文件） | **不执行任何剥离**——所有字段（含 `'` 开头）原样读入 | `'-3.5` → `'-3.5`（一字不改） |
| 结构化层 | 转义形式**绝不进入**结构化数据层（RawTable/RawCell 保存的是剥离后的原始字符串）；结构化证据保留原始值（NFR-SEC-03） | — |
| 自反性 | 对任意原文 s：`decode(encode(s)) == s` 逐字符成立（含 s 以 `'` 开头、空串、含换行/引号/分隔符）——契约测试断言（§11 IO-V03） | — |

**公式注入防护的完整口径**：数据-only 解析（不执行公式/命令）＋导出前缀转义。读取侧永不"清除"疑似公式字符——防护目标是**导出物被表格软件误执行**，不是修改用户数据。

### 5.4 结构问题处置（表头／列／字符）

| 问题 | 判定 | 处置 | 码 |
| --- | --- | --- | --- |
| 重复列名 | 表头行（若存在，由调用方指定哪一行是表头）中折叠大小写后同名 | **拒绝**（列映射歧义；诊断列出列号与名字，AT-02 定位口径） | `IO-FORMAT-CSV-DUPCOL` |
| 缺失列 | 数据行列数 < 表头列数 | 默认拒绝并定位（行号/期望/实际）；调用方可声明 `PadTrailing` 策略补空（策略记入 ParseReport） | `IO-FORMAT-CSV-ARITY` |
| 多余列 | 数据行列数 > 表头列数 | 同上（`TrimExcess` 策略可选，**默认拒绝**——多余列可能是映射错误信号） | `IO-FORMAT-CSV-ARITY` |
| 非法字符 | 文件层：UTF-8 有效性（§5.2）；字段层：无非法字符概念（CSV 字段允许任意 UTF-8 字符，含控制字符——仅 NUL 拒绝 `IO-FORMAT-CSV-CHAR`，因其无法往返于文本工具链） | 见左 | `IO-FORMAT-CSV-CHAR` |
| 引号不闭合 | 物理文件结束仍处引号内 | 拒绝，定位起始行 | `IO-FORMAT-CSV-QUOTE` |
| 表头不存在 | 调用方声明 `header=false` | 首行即数据；列名由调用方提供（业务字段字典） | — |
| 空行 | 完全空行（零字段） | 默认跳过并计数（报告记录跳过数）；`RejectBlank` 策略可改为拒绝 | — |
| 字段顺序 | io **不重排列**（roundtrip 前提）；列语义映射归业务单元 | — | — |

### 5.5 roundtrip 语义（导出→导入一致性）

| 概念 | 定义 | io 承诺 |
| --- | --- | --- |
| 逐字符一致（数据层） | 对本软件导出的 CSV 再导入：每个单元格字符串（剥离转义后）与导出前结构化值逐字符相同 | **承诺**（含原文自带 `= + - @ '` 前缀、空串、含引号/分隔符/换行字段——NFR-SEC-03 R5） |
| 字节一致（文件层） | 同一结构化数据两次导出的文件字节相同 | **承诺**（canonical 写出：固定方言行、CRLF、UTF-8 无 BOM、`std::to_chars` 最短双精度表示——确定性基础，AT-22 多格式一致） |
| 外部文件原样读入 | 无标识 CSV 导入后：单元格原文与文件内容（引号处理后）一字不差，转义形式与语义还原均不发生 | **承诺** |
| 语义一致 | 数值/单位语义在业务层的等价（如 `1.0` 与 `1`） | **不承诺于 io**——io 保留原文；语义归业务单元（数值规范化仅发生在业务解析，且原文经 SourcedValue 保留） |
| 数值规范化 | — | io 读：原文保留；io 写：调用方传入的数值以 `std::to_chars`（最短往返表示，`.` 小数点，无本地化）格式化——同一 double 两次写出相同 |

### 5.6 流式读取、错误定位与写出模型

- **流式**：读取为单遍流式（行迭代器/回调），峰值内存＝当前行＋配置窗口；行数预算（§4.5）在流中检查；大文件（NFR-PERF-03 量级）不整体载入。
- **逐行错误定位**：`RowError{rowNo(1 起，含标识行偏移), colNo(1 起), fieldName(若表头), rawSnippet(脱敏), reason}`；**部分成功**：默认 `StopOnError`？否——CSV 通道默认 **继续解析并收集全部行错误**（上限 1000 条，超出截断＋计数），正确行照常产出（AT-02"正确行保留、错误定位到列与原文"）；调用方可选 `StopOnFirstError`。错误行**不进入**业务数据模型（由调用方按行过滤）。
- **写出**：`ICsvWriter` 接受行序列（`CsvCell`＝`string`｜`int64`｜`double`｜空标记），缓冲写出至 `IAtomicFileWriter`（§4.6）；写出过程可取消，取消即中止（目标不受影响——替换未发生）。

### 5.7 CSV roundtrip 流程图与错误矩阵

```text
┌─────────────┐   ┌──────────────────────────┐   ┌────────────────────────┐
│ 结构化数据   │   │ ICsvWriter（唯一转义点）  │   │ CSV 文件（带方言标识行）│
│ (业务值原文) │──►│ 格式化(to_chars, '.')     │──►│ #rwcsv1 … + CRLF + UTF8│
└─────────────┘   │ 转义(=+-@' → 前置 ')      │   └───────────┬────────────┘
                  │ 引号/分隔符 RFC4180        │               │  (文件层：转义形式)
                  └──────────────────────────┘               ▼
                                                  ┌────────────────────────┐
                                                  │ ICsvReader（唯一还原点） │
                                                  │ 首行方言判定（字节精确） │
                                                  │  ├ 带标识 → 剥离恰一个 ' │
                                                  │  └ 无标识 → 原样读入     │
                                                  │ 预算/流式/逐行错误定位   │
                                                  └───────────┬────────────┘
                                                              ▼
┌────────────────────────────────────────────────────────────────────┐
│ 断言（契约测试 IO-V02/V03）：cellwise  decode(encode(x)) == x        │
│  · 带前缀原文（=+-@'）逐字符还原   · 转义形式不入结构化层            │
│  · 无标识文件任何字段零改写        · 文件层二次导出字节一致           │
└────────────────────────────────────────────────────────────────────┘
```

**CSV 错误矩阵**（行＝文件特征，列＝处置；全部为**格式错误**类，§2.5——与业务合法性无关）：

| # | 文件特征 | 读带标识文件 | 读无标识外部文件 | 对导出 |
| --- | --- | --- | --- | --- |
| E1 | 首行方言行语法错（未知键/quote='/encoding≠utf-8） | `IO-FORMAT-CSV-DIALECT` 拒绝 | （首行非标识语法→按数据/表头处理，不报此项） | —（io 导出恒合法） |
| E2 | 非 UTF-8 且无 BOM | `IO-FORMAT-CSV-ENCODING` | 同左 | — |
| E3 | UTF-16（带 BOM） | 接受（标识行按解码后判定，通常非标识语法→按无标识处理） | 接受 | —（io 只写 UTF-8） |
| E4 | 引号不闭合 | `IO-FORMAT-CSV-QUOTE`（定位起始行） | 同左 | — |
| E5 | 行字段数与表头不符 | `IO-FORMAT-CSV-ARITY`（或声明策略） | 同左 | — |
| E6 | 表头重复列名 | `IO-FORMAT-CSV-DUPCOL` | 同左 | — |
| E7 | 字段含 NUL | `IO-FORMAT-CSV-CHAR` | 同左 | 写入侧拒绝 NUL 入 CSV |
| E8 | 单字段超长 | `IO-SEC-BUDGET-FIELD`（安全类，§2.5） | 同左 | 同左 |
| E9 | 行数超预算 | `IO-SEC-BUDGET-ROWS` | 同左 | — |
| E10 | 空行 | 跳过＋计数（或策略拒绝） | 同左 | 不产生 |
| E11 | `'` 开头字段 | 剥离恰一个 `'` | **原样保留** | 前置一个 `'` |
| E12 | 单元格内换行/分隔符/引号 | RFC4180 引号处理，原文保留 | 同左 | RFC4180 引号写出 |

### 5.8 数据模型归属、部分成功与项目影响

| 事项 | 设计 |
| --- | --- |
| 解析后数据模型 | `RawTable`（方言＋表头＋行集＋行错误集）归 **io 产出、调用方所有**；业务数据模型（任务点表、目录表…）归 requirements/selection/modeling——io 不生成业务对象 |
| 原文保留 | RawCell 保留单元格原文（剥离转义后）；业务解析经 core `tryParse`/`UnitToken` 产出 SourcedValue（原文始终可溯）——与 core.md §13.2 io 行一致 |
| 部分成功 | 允许（行粒度，§5.6）；调用方决定"正确行进预览/草稿"；**预览不产生正式证据**（REQ-06，AT-02）由调用方保证 |
| 导入失败与项目修订 | CSV 解析/校验失败**不产生任何项目修订**：io 不触达命令端口；草稿写入归 DraftService（调用方），失败时 io 无任何落盘产物（RawTable 纯内存） |
| 诊断脱敏 | rawSnippet 经 diagnostics 脱敏（用户级仅定位；开发级保留片段——diagnostics.md §8.6 口径）；路径参数同 |

### 5.9 JSON 与结构化数据（受限读写器）

io 提供产品级 JSON 读写器（`Json.*`；testkit JsonLite **不用于产品格式**——WP-11-T05 禁止项）。JSON 通道服务对象：REQ-12 需求 JSON、OPT-12 研究结果/审计 JSON、`manifest.json`/`rwpack.json`（§7）、future 工件。

#### 5.9.1 受限 DOM 与安全限制

| 限制 | 规则 | 超限码 |
| --- | --- | --- |
| 文档大小 | JsonDocBytes 预算（§4.5）流式预检（先 stat/首块） | `IO-SEC-BUDGET-JSON` |
| 嵌套深度 | 解析器深度计数器，超 JsonDepth 拒绝 | `IO-SEC-BUDGET-JSON-DEPTH` |
| 重复键 | **同层同名键第二次出现即拒绝**（定位路径；不"后者覆盖"——静默覆盖即数据丢失） | `IO-FORMAT-JSON-DUPKEY` |
| NaN/Infinity | 语法层拒绝（`NaN`/`Infinity`/`-Infinity` 非法 JSON；也拒 `1e999` 溢出→`IO-FORMAT-JSON-NUMBER`）；写出侧 `std::isfinite` 断言，非有限值拒绝写出 | `IO-FORMAT-JSON-NUMBER` |
| 大字符串 | 单字符串长度 JsonStringChars；拼接跨块检查 | `IO-SEC-BUDGET-JSON-STRING` |
| 数值 | IEEE754 double／int64（|i|≤2^63−1 范围整数无损；超范围整数按原文保留为 string 透传＋提示——不静默截断） | `IO-FORMAT-JSON-NUMBER` |
| 编码 | 仅 UTF-8（BOM 容忍剥离）；无效序列拒绝 | `IO-FORMAT-JSON-ENCODING` |

#### 5.9.2 版本字段、schema 校验与 JsonProfile

| 项 | 设计 |
| --- | --- |
| 版本字段 | 产品 JSON 文档**必须**顶层携带 `"schemaVersion": <int>`（缺省＝`IO-FORMAT-JSON-VERSION-MISSING`；类型错＝`IO-FORMAT-JSON-VERSION-TYPE`）。`schemaVersion > io 已注册 profile 支持上限` → `IO-FORMAT-VERSION-FUTURE`（稳定只读拒绝；诊断含当前支持版本/文件版本/升级指引数据——PM-06 执行侧）；`< 下限` → `IO-FORMAT-VERSION-LEGACY`（同口径）。**版本判定先于一切 schema 校验** |
| JsonProfile | 声明式结构契约，由**格式所有者单元**装配期注册（io 持注册表）：`{profileId, supportedVersions[], rootShape(type/required/optional keys, per-key type|enum|range|array bounds|递归 shape), unknownKeyPolicy}`。io 执行校验、产出逐路径诊断；**字段语义**（单位、业务范围）归格式所有者 |
| 未知字段 | 默认 `Reject`（`IO-FORMAT-JSON-UNKNOWN`，定位路径）——schema 演进必须走版本升级（NFR-DEP-04），不允许静默吞字段；profile 可对指定子树声明 `Preserve`（透传保留，用于前向兼容的扩展块——扩展块名单属格式所有者契约） |
| 必填/默认值 | required 缺失→`IO-FORMAT-JSON-REQUIRED`（路径）；**io 不注入默认值**——默认值语义归业务单元（io 只报结构事实，防"结构补全"变成业务决策） |
| 类型/范围 | 类型不符→`IO-FORMAT-JSON-TYPE`；数值范围（profile 声明的 [min,max]）→`IO-FORMAT-JSON-RANGE`（**比较型**：实际/期望/单位） |

#### 5.9.3 顺序规范化与内容身份

- **读**：DOM 保留对象键的**文件出现序**（诊断定位需要）；不排序。
- **写（canonical）**：键按 profile 声明序（无 profile 的通用文档按字典序）写出；数值 `std::to_chars` 最短表示；缩进固定 2 空格、LF 行尾——**同一 DOM 两次写出字节相同**（canonical 编码）。
- **内容身份**：对 canonical 字节计算 `Digest256`（core ContentDigester）；包 manifest、固化副本、缓存键（如需）统一用此口径。JSON 文件本身**不是**项目权威数据（权威归 `.rwdesign` 对象库；REQ-12 导出为副本）。

#### 5.9.4 格式校验与业务校验边界

io 负责：语法、安全限制、版本、结构（profile 驱动）。**不负责**：单位合法性最终判定（core UnitToken＋业务字典）、字段业务语义、引用对象存在性判定（业务/查询端口）、任何工程结论。失败时返回**稳定诊断**（码＋路径＋行/列位置——解析器记录每个值的位置区间）＋已解析部分的**不产出**（结构校验失败则整个文档不进入业务层——JSON 通道无"部分成功"，与 CSV 行粒度不同，登记为设计决策 IO-D10：JSON 用于整档结构化数据，行级容错无业务场景）。

---
## 6. URDF/Xacro、网格和 WorkCell 资源读取

### 6.1 交接总则（io／runtime／modeling 三分）

| 关注点 | 所有者 | 内容 |
| --- | --- | --- |
| 文件层读取 | **io** | 编码识别、XML 良构检查、include/依赖树枚举、循环检测、预算、读取快照（size/mtime/SHA-256）、格式识别（魔数/扩展名） |
| 受控预处理护栏 | **io**（机制）＋**modeling**（语义） | Xacro 参数代入与宏展开的**执行机制**（展开深度、展开总大小、include 解析、循环拒绝）归 io；参数合法性、展开结果的业务意义归 modeling（MDL-19；阶段 B 落地，§13） |
| 业务模型语义 | **modeling** | URDF 字段映射、默认补全、忽略/不支持项报告、MDL-18 WorkCell 有损提取与提取报告 |
| 编译与适配 | **runtime** | CanonicalModel 编译、rw::loaders 消费、名称映射；runtime 只接收 io 已验证资源字节（IRuntimeResourceProvider，§9.7） |
| 不做 | io | io 不创建正式 CanonicalModel、不产出 RobotDesign、不判定"模型不可用/工程不可行" |

**边界强调**：展开结果进入与 URDF 相同的安全解析边界（MDL-19/ARCH §7.9）——Xacro 预处理**不放松**任何 SafePath/BudgetGuard 规则。

### 6.2 URDF/Xacro 读取与 include／宏展开边界

| 项 | 设计 |
| --- | --- |
| 读取入口 | P-1 角色（用户显式选择的 .urdf/.xacro 文件或目录）；一次性读取例外（NFR-SEC-01），预算照常 |
| include 解析 | Xacro/URDF 的 `<xacro:include>`/自定义 include 以**导入根**（用户选择的文件所在目录或显式声明的搜索根）为基解析；include 目标必须落在导入根内（SafePath：越界＝`IO-SEC-PATH-ESCAPE`——导入根自身即临时管辖根）；`package://` 等 ROS URI **不支持**（R1）——出现即诊断引导用户改用相对路径（不猜测映射） |
| 宏展开边界 | 展开递归深度 ≤ IncludeDepth（16）；展开产物总字节 ≤ TotalBytes；未定义宏/参数 → 定位诊断（行/列/宏名）；**展开绝不执行任意代码**（无条件执行、无 shell-out、无 eval 类设施——Xacro 语言子集按声明式处理） |
| 循环引用 | include 图有环 → `IO-FORMAT-XML-CYCLE`（列出环路径）；循环与深度双保险（深度超限先触发也拒绝） |
| 离线依赖 | 展开所需全部文件必须已在本机（导入向导声明/已复制入项目资源区）；缺文件 → `IO-RES-MISSING`＋缺失清单（MDL-19"依赖缺失给出可定位诊断"） |
| 快照 | 每个读取文件记录 ResourceSnapshot（§8.2）；导入结果为草稿（PM-01），外部资源由用户选择复制入项目资源区或登记外部引用记录（PM-01 三段边界） |

### 6.3 网格资源（STL/OBJ/DAE）与纹理材质

| 项 | 设计 |
| --- | --- |
| 格式识别 | 二进制 STL（魔数后 84 字节头含三角数→可即时计数）、ASCII STL、OBJ（行前缀统计）、DAE（COLLADA，XML）；识别失败→`IO-FORMAT-MESH-UNKNOWN`（附首 16 字节十六进制摘要，脱敏无虞） |
| 规模预检 | 二进制 STL：84 字节头三角形数×50 字节与预算比对（超限即拒，不读体）；OBJ/DAE：流式统计 `v`/`f`/`<triangles>` 计数至 MeshVertexCount/MeshFaceCount——超限即中止 |
| 几何解析 | 完整解析（顶点/法向/索引装配）**归 modeling/runtime 消费的 rw::loaders 或 modeling 自有解析**——io 只保证"已验证字节＋规模安全" |
| 纹理与材质 | 纹理文件（PNG/JPG/…）按字节资源处理（大小预算＋摘要）；材质引用（MTL/DAE 内嵌）作为依赖树节点参与 include/循环检查 |
| 顶点/面预算归属 | 计数预检归 io（预算入口）；真实计数与预检不符（声明谎报）→ 以实际计数触发预算（读取中复核） |

### 6.4 WorkCell XML（MDL-18，R2 通道）

io 阶段 A 不实现（R2 分期）；接口预留：`IResourceReader::openXml(...)` 同一通道（P-1 角色＋预算＋快照＋依赖树）。有损提取、提取报告、不可表达内容判定（MDL-10/12 规则）归 modeling——**io 不因资源缺失/不可解析判定工程不可行**（§6.6）。

### 6.5 资源依赖树

```text
导入根（用户选择）
└─ robot.xacro ── include ──► macros.xacro
   │  └─ <mesh filename="base.stl"/>        ──依赖──► base.stl   ──材质──► base.mtl ──纹理──► tex.png
   └─ <mesh filename="arm.dae"/>            ──依赖──► arm.dae（内嵌纹理依赖）
节点 = ResourceSnapshot{path(导入根相对), size, mtime, digest}
边   = {include | mesh | material | texture | workcell-include}
约束：所有节点落在导入根内（SafePath）；边数 ≤ FileCount；路径深度 ≤ DirDepth/RefGraphDepth；
     有环 → IO-FORMAT-XML-CYCLE；节点缺席 → IO-RES-MISSING（缺失清单）
产物：ResourceDependencyTree（io 产出）→ modeling 消费做字段映射与草稿模型
```

### 6.6 源文件变化检测与"缺失≠不可行"

- **检测时机**（io 责任）：读取时快照；重读时比对（digest 为主判据，size/mtime 为快速预筛——digest 不同即 Changed；digest 相同视为未变〔size/mtime 不同仅提示，防碰撞性极低且哈希是权威〕）；固化复制期间前后双读比对（§8.4）。
- **上报形态**：`ExternalRefProbe::Ok | Missing | Changed {recorded, actual}`——纯事实；**用户重关联流程归 PM-09/project 命令**。
- **缺失不得判工程不可行**：io 产 `IO-RES-MISSING`（资源事实）→ evidence 门禁按 CON-03/§8.1 判"未固化→阻断正式结论（证据不足）"；工程不可行是业务判定（需求 §8.1 五级优先级），io 的资源诊断只进证据链、不进结论。

---

## 7. `.rwpack` 导入导出

### 7.1 包格式契约（传输封装，非权威格式）

```text
<name>.rwpack  （ZIP；zip64 支持必开；条目名 UTF-8 正斜杠；DEFLATE 允许）
├─ rwpack.json     # {formatId:"rwpack", schemaVersion:1, createdAtUtc,
│                  #  createdWithToolVersion, sourceProjectId,
│                  #  content:{headRevisionId, fileCount, totalBytes, totalDigest}}
├─ manifest.json   # {schemaVersion:"ird-pack-manifest/1",
│                  #  entries:[{path(相对,payload/ 前缀), size, sha256}]}（按 path 字典序）
└─ payload/        # 逐字节镜像 .rwdesign 布局：payload/HEAD、payload/project.json、
                   # payload/revisions/…、payload/objects/…、(可选)payload/results/…、
                   # payload/reports/…、payload/drafts/…
```

| 规则 | 内容 |
| --- | --- |
| 不是第二权威格式 | 包内树仅镜像；**不含** `lock`、`.staging/`、任何"当前状态"语义；导入后须经 project 打开协议全量校验并发布，才成为项目（§7.3 步骤⑧） |
| 版本 | `rwpack.json.schemaVersion`＞当前支持 → `IO-FORMAT-VERSION-FUTURE`（只读拒绝＋升级指引数据，PM-06 口径）；旧版本包按注册的包升级步距处理（未注册→`IO-FORMAT-VERSION-LEGACY`；升级器归 project/project 侧，io 给数据） |
| 哈希 | manifest 每条目 SHA-256（core ContentDigester）；`content.totalDigest`＝对 manifest canonical 字节的摘要（防 manifest 自身被改） |
| 条目约束 | SP-2/SP-4/SP-8/SP-9（§4.3）；无目录条目（仅文件）；加密条目拒绝（`IO-FORMAT-PACK-ENCRYPTED`）；数据描述符/压缩方法仅 STORED/DEFLATE |
| 可选树 | `results/`、`reports/`、`drafts/` 按导出勾选（PM-05；勾选记忆归 ui）；`objects/`、`revisions/`、`HEAD`、`project.json` 恒在 |

### 7.2 导出协议（一致数据视图＋后台＋取消＋清理）

**发起**：project 的 PackageService（PM-05 存储侧）调用 io `IPackageExporter`；ui/workflow 只做向导与进度呈现（N-6）。

**一致数据视图的取得（关键——不读半提交内容、不把文件存在当已提交）**：

```text
┌────────────────────────── project 侧（快照所有者）──────────────────────────┐
│ export 开始：锁定快照视图 = HEAD 闭包文件清单 ＋ 勾选树清单（D-17）           │
│   · objects/revisions：不可变 → 枚举自当前 HEAD 闭包（不扫描目录！）          │
│   · drafts/manifest：单文件原子替换 → 读到的是完整旧版或新版                  │
│   · .staging/lock：永不入清单 ——"文件存在≠已提交"的结构性保证                │
└───────────────┬─────────────────────────────────────────────────────────────┘
                ▼ ISnapshotFileSource（io 定义接口，project 实现，注入）
┌────────────────────────── io 侧（导出执行者）───────────────────────────────┐
│ ① TempArea（目标文件同卷同目录旁 .<name>.<8hex>.tmp/）                       │
│ ② 逐文件：source.read(path) → 摘要 → 写入临时区（预算 TempAreaBytes）        │
│    （清单外文件一概不读；source 枚举与 read 的一致性由 project 快照保证）      │
│ ③ manifest.json ＋ rwpack.json（canonical 写出，§5.9.3）                     │
│ ④ 压缩为 .rwpack（zip64；进度回调每文件；取消检查点每文件）                   │
│ ⑤ 完整性自检：重新打开包 → 全量条目哈希复算比对（导出完整性报告）             │
│ ⑥ IAtomicFileWriter 原子替换到用户目标路径（§4.6；取消/失败发生在⑥前→        │
│    目标不存在先前包被破坏；替换成功后清理临时区）                             │
└────────────────────────────────────────────────────────────────────────────┘
```

| 场景 | 行为 |
| --- | --- |
| 导出期间后台写入并存 | 快照清单保证一致性（D-17）；io 单文件读取期间 source 报"已变化"（drafts 被替换的极窄窗口）→ 该文件按快照重读一次（source 语义：同 key 重读返回稳定版本）；仍失败→导出失败清理，**不产出混合版本包** |
| 取消 | 任一检查点取消 → 立即中止 → 清理临时区；**目标路径无任何产出**（无"可被误识别的正式包"） |
| 失败 | 同取消；若失败发生在⑥替换后（残留清理失败）→ 包已正确，仅开发级诊断 |
| 源项目影响 | 导出全过程**零写源项目**（只读）；导出失败不修改源项目（结构性保证） |

### 7.3 导入协议（九步，逐步可取消/可失败）

```text
① 选择包      用户选择 .rwpack（P-1 角色：存在/可读/扩展名与魔数预检）
② 建立临时目录 TempArea：目标父目录旁 .rwpack-import-<8hex>/（同卷！发布=rename）
③ 验证包版本与路径  rwpack.json 读取（JSON 通道§5.9）→ 版本判定（未来/旧）；
                    全条目名 SP-2/4/8/9 预检（展开前！折叠键查重复条目、绝对路径、..、symlink 属性）
④ 展开并执行预算检查  逐条目流式展开：每条目声明大小 vs 预算先行；展开累计 vs
                    ArchiveExpandedBytes/ArchiveRatio/FileCount/DirDepth/TempAreaBytes；
                    磁盘可用空间检查（不足→IO-PACK-DISK-FULL）
⑤ 校验哈希与目录结构  每文件展开后即算 SHA-256 vs manifest（不等全部展开完才验——
                    早失败早清理）；rwpack.json.content.totalDigest 验 manifest；
                    目录结构 vs §7.1 镜像规则（必备条目齐全；多余顶层条目拒绝）
⑥ 检查引用完整性    清单内文件间引用（对象存在性/修订闭包）——文件层核对；
                    领域校验（打开协议②③⑤）归 project 于临时目录执行
⑦ 生成校验报告     PackageImportReport（结构化：条目数/字节/哈希结果/警告/
                    诊断全量；可脱敏呈现）
⑧ 等待 project 发布  project：目标目录占用二次预检 → rename（同卷原子）→
                    按打开协议进入 —— io 不发布（N-7）
⑨ 成功后清理临时目录  发布成功 → TempArea.cleanup()；失败→§7.6
```

**导入事务状态图**：

```text
                 ┌─────────┐  ②成功   ┌──────────┐ ③④⑤⑥通过 ┌──────────┐
  ①Selected ───► │Staging  ├────────►│Extracting├──────────►│Verified  │
                 └────┬────┘         └────┬─────┘           └────┬─────┘
                      │ 任意步失败/取消        │ 失败/取消           │ ⑧ project 发布
                      ▼                      ▼                   ▼
                 ┌──────────────────────────────┐         ┌──────────┐
                 │ Cleanup（清理临时区）          │         │Publishing│（project 侧）
                 │ 成功→Canceled/Failed（终态）   │         └───┬──────┘
                 │ 失败→CleanupFailed（残留登记） │     发布成功│      发布失败
                 └──────────────────────────────┘          ▼        ▼
                                                    ⑨ Cleaned   清理临时区
                                                    （终态）    ＋诊断（project）
```

### 7.4 导入威胁处置矩阵

| 威胁/异常 | 检测点 | 处置 | 码 |
| --- | --- | --- | --- |
| 路径穿越（`../`、绝对路径、盘符/UNC） | ③条目名预检＋SP-2 | 整体拒绝 | `IO-SEC-PATH-ESCAPE` |
| 符号链接条目（属性位）/展开产物 reparse point | ③预检＋④展开时 SP-4 双检 | 整体拒绝 | `IO-SEC-SYMLINK` |
| 压缩炸弹（高比例/超展开量） | ④逐条目累计＋比例 | 中止＋清理 | `IO-SEC-BOMB-RATIO`/`IO-SEC-BUDGET-EXPAND` |
| 重复条目（含仅大小写异） | ③折叠键集合 | 整体拒绝（防"后写覆盖先写"歧义） | `IO-PACK-DUPLICATE-ENTRY` |
| 未来版本包 | ③rwpack.json | 稳定拒绝＋当前/包版本/升级指引数据 | `IO-FORMAT-VERSION-FUTURE` |
| 旧版本包 | ③ | 未注册步距→拒绝；已注册→project 升级器流程 | `IO-FORMAT-VERSION-LEGACY` |
| 缺失文件（manifest 声明无对应条目/镜像必备缺失） | ⑤ | 拒绝＋缺失清单 | `IO-PACK-REF-INCOMPLETE` |
| 哈希不匹配 | ⑤（展开即验） | 拒绝＋条目定位 | `IO-PACK-HASH-MISMATCH` |
| 引用不完整（文件间引用断裂） | ⑥ | 拒绝＋引用链定位 | `IO-PACK-REF-INCOMPLETE` |
| 目标目录已存在 | ⑧前预检（io）＋发布时二次校验（project，TOCTOU 收口） | 拒绝（不覆盖既有目录；`ReplacePolicy` 对目录不适用） | `IO-PACK-TARGET-EXISTS` |
| 取消 | 全步骤检查点 | 中止→清理（正常取消非错误，UX-03） | 状态 `Canceled`（非诊断） |
| 磁盘不足 | ②④预检＋写入失败兜底 | 拒绝＋建议（所需/可用） | `IO-PACK-DISK-FULL` |
| 清理失败 | Cleanup 阶段 | 见 §7.6；**不自动删除任何非本会话文件** | `IO-PACK-CLEANUP-FAILED` |
| 加密条目 | ③ | 拒绝 | `IO-FORMAT-PACK-ENCRYPTED` |
| zip 结构损坏（中心目录错/截断） | ③ | 拒绝＋定位 | `IO-FORMAT-PACK-ZIP` |

### 7.5 临时区管理（TempArea）

| 项 | 设计 |
| --- | --- |
| 创建 | `.rwpack-import-<8hex>/`（导入）、`.<name>.<8hex>.tmp/`＋`.tmp` 文件（导出）、`.staging/tmp/solidify-<id>/`（固化——project 授权位，P-6 角色） |
| 互斥 | 同卷同级同会话前缀互斥（等价键匹配到未清理残留 → 尝试清理：**仅当**残留目录内含本 io 会话标记文件（`io-session.json`：pid＋时间戳）才清——绝不误删用户目录） |
| 清理 | RAII＋显式 `cleanup()`：仅删除本会话根（递归；每层预算无关、深度受限 64）；删除顺序自底向上 |
| 残留报告 | 清理失败→列出残留绝对路径（脱敏）＋重试入口（`cleanup()` 幂等） |
| 崩溃残留 | 下次同前缀会话创建时按标记文件识别回收（§7.6）；启动全局扫描归 project/宿主（io 无进程生命周期） |

### 7.6 导入失败与清理状态图

```text
            失败/取消（②~⑥、⑧发布失败）
   ┌────────────────────────────────────────────┐
   │ Failed / Canceled                           │
   │  目标项目目录：从未写入（发布未发生）——结构性 │
   │  保证"失败不留目标目录"（PM-05）             │
   └───────────────────┬────────────────────────┘
                       ▼
              ┌────────────────┐   成功   ┌──────────────┐
              │ Cleanup        ├────────►│ 终态          │
              │ 删本会话临时区  │         │（Failed 或    │
              └───────┬────────┘         │  Canceled）   │
                      │ 删除失败          └──────────────┘
                      ▼
              ┌────────────────┐
              │ CleanupFailed  │  · 残留路径登记＋诊断（IO-PACK-CLEANUP-FAILED）
              │ （终态，可重试  │  · 不自动删除任何非本会话文件
              │  cleanup()）   │  · 目标目录仍为零写入——残留不可被误识别为项目
              └────────────────┘  · 与"正式包/项目"可区分：残留均在隐藏临时前缀内
```

### 7.7 导出导入与 project 的责任切分

| 步骤 | io | project |
| --- | --- | --- |
| 导出 | ②~⑥执行＋报告 | 快照清单/ISnapshotFileSource；发布无 |
| 导入 | ①~⑦＋⑨清理 | ⑥领域校验（打开协议②③⑤于临时目录）、⑧发布（rename）＋目标二次校验、失败路径呈现 |
| 失败语义 | 临时区与校验报告归 io | "不留目标目录"由发布时序共同保证（发布只在全量校验后） |

### 7.8 目录包（选型 CSV 目录包）导入校验（SEL-01/02 支撑）

| 层 | 归属 | 内容 |
| --- | --- | --- |
| 文件层 | **io** | 目录结构（清单 manifest、型号主表、能力曲线表、兼容关系表——**文件名与清单结构契约由 selection 注册**）；清单 vs 实际文件核对；文件间引用存在性（主表↔曲线表↔兼容表的键引用）；每文件为 CSV 通道（§5）；路径/预算防护（导入根内） |
| 业务层 | **selection** | 字段字典、单位合法性、必填字段、型号唯一性、数值范围、分段线性插值与外推禁止（SEL-02） |
| 失败处理 | 共同 | 不完整导入＝删除重导、不留目标（project.md §4.1 catalog 行同口径）；io 临时区先建、校验通过后经 project 存储端口落 `catalog/<id>/<ver>/`（阶段 C 接入，§13） |

### 7.9 与 `.rwdesign` 打开/升级的边界

包导入后于临时目录的"打开协议②③⑤"（目录形态/版本/读校验/恢复诊断）由 project 执行——io 已保证字节完整与结构镜像，project 的 `schemaVersion` 判定（PM-06 三情形表）在包版本判定之后独立发生（包版本≠项目 schemaVersion，两层版本各自判定，诊断各自给出）。

---

## 8. 外部资源固化与引用保护（三段边界协作）

### 8.1 三段边界承接（PM-01/NFR-SEC-01，R4；ARCH §6.6）

```text
段① 一次性读取        段② 草稿期外部引用记录              段③ 转正式固化（CON-03）
导入向导，用户显式     草稿内登记 {绝对路径＋内容哈希}；     进入 Verified 评估/正式报告前：
选择外部源文件；io 安   io 持有缺失/变化检测（§8.3）；      必须固化为项目内不可变副本
全读取（预算管辖区，    不以裸路径充当项目资源引用；        （objects/）并受引用保护；未固化
SafePath 例外区）      runtime 消费走 Recorded 警告        阻断正式结论（evidence 门禁，
                                                          EVI-01 证据不足口径）
```

### 8.2 读取快照（ResourceSnapshot）与资源摘要

```cpp
struct ResourceSnapshot {
    std::filesystem::path  finalPath;     // 实体路径（weakly_canonical；仅追溯提示，不作身份）
    std::uint64_t          sizeBytes;
    std::uint64_t          mtimeUtc;      // 快速预筛（非权威）
    core::Digest256        contentDigest; // SHA-256（权威判据；core ContentDigester）
};
```

- 摘要算法唯一：SHA-256 via core（SA-12；io 不引入第二种算法）。
- 资源内容身份＝`contentDigest`（对资源字节的摘要）；**不含路径、不含 mtime**——同内容不同路径＝同身份（runtime.md §8.6"路径变化而内容不变→身份不变"同源）。
- 快照稳定性：同一未变文件重复读取，digest 必须相同（确定性；含并发写场景的边界——若读取期间文件被写，则本次读取的 digest 不稳定 → 复制期间变化检测兜底，§8.4）。

### 8.3 外部源缺失/变化检测（NFR-REL-04 实现责任）

| 检测 | 算法 | 产出 |
| --- | --- | --- |
| Missing | 实体路径 `finalPath` 不可达（NOT_FOUND/ACCESS_DENIED 细分记录） | `IO-RES-MISSING`＋recorded 摘要/大小 |
| Changed | 快照比对：size 或 mtime 不同→必疑→**重算 digest 比对**；digest 不同→Changed | `IO-RES-CHANGED`（比较型：recorded digest/actual digest/时间）＋`ExternalRefProbe::Changed` |
| 未变 | digest 相同（size/mtime 不同仅提示性诊断，不判 Changed——防 touch 不改内容的误报） | `ExternalRefProbe::Ok` |

检测由 project/evidence/modeling 在需要时调用（草稿打开、就绪校验、编译前复查）；**重关联用户流程与命令归 PM-09/project**（新修订）。

### 8.4 固化流程（io 执行侧；project 编排）

承接 project.md §5.7 已冻结接口（SolidifyRequest/SolidifyResult）：

```text
┌─ project：SolidifyCommand（经命令端口，CON-03：固化随命令入事务）──────────┐
│   SolidifyRequest{externalRefId, targetResourceObject, budgetBytes}        │
└──────────────────────────────┬─────────────────────────────────────────────┘
                               ▼ io（IResourceSnapshotter::solidifyToStaging）
 1 预算开户（budgetBytes 与产品默认取小；三段共用 ledger）
 2 前置快照 A＝snapshot(finalPath)          ── stat＋SHA-256 全读（P-1 例外区）
 3 复制到 .staging/tmp/solidify-<id>/（P-6；io 只写 project 授权中转位）
 4 复制后快照 B＝snapshot(finalPath)（重读源文件）
 5 比对 A/B：digest 或 size 不同 → sourceChangedDuringCopy 置位 →
   产出 Changed 诊断 → 清理中转 → 固化失败（NFR-REL-04 复制期间变化检测）
 6 副本字节复算 digest（与 A 一致——传输完整性）＋ canonical 字节就绪
 7 交 project：入对象库（对象负载 canonical 要求由域侧保证）→
   随下一次命令提交发布（materializedVersion）→ ExternalResourceRecord.state
   =materialized → 引用保护生效（此后不可变，runtime S10 免复查）
 8 成功 → 清理中转；失败（任何步）→ 清理中转；清理失败 → 残留报告（§7.5）
```

**失败责任划分**：

| 失败 | 责任方 | 处置 |
| --- | --- | --- |
| 缺失（段②/③读取时） | io 检测 | `IO-RES-MISSING`；project 透传中止；evidence 判证据不足 |
| 复制期间源变化 | io 检测（§8.4 步骤 5） | `sourceChangedDuringCopy`＋Changed 诊断；中止固化；**不产生部分副本** |
| 读取失败（IO 错误） | io 检测 | 四分类（§4.2.5）；中止＋诊断 |
| 预算超限 | io（BudgetGuard） | 中止＋比较型诊断；无半成品 |
| 发布/引用保护 | project | 事务/对象库/不可变保证 |
| 是否允许正式报告 | evidence（FormalPass 资格）＋project | io 不判定（N-3） |

### 8.5 资源内容身份与 project/runtime 引用关系图

```text
        ┌─────────── project（.rwdesign 唯一写权）───────────┐
        │  objects/obj-<oid>/<cv>   ←——固化副本（不可变）     │
        │  drafts/…ExternalResourceRecord{extRefId, absPath, │
        │          recordedDigest, state=recorded|materialized}│
        │  ResourceRef{resourceId, contentDigest,            │
        │             sourcePathHint(仅提示), state,          │
        │             accessVersion}（runtime.md §8.6 同源）   │
        └───────┬───────────────────────────────┬────────────┘
   固化发布（命令）│                               │ IProjectBytesSource（注入）
                ▼                               ▼
        ┌─────────── io（IRuntimeResourceAdapter，§9.7）─────┐
        │ resourceId ─► Solidified：读项目对象字节（缓存不可变）│
        │              Recorded：读外部引用记录路径（每次重读＋│
        │              重算 digest，绝不跨调用缓存——保证 S10   │
        │              复查能发现替换，runtime.md §5.4 口径）    │
        └───────┬────────────────────────────────────────────┘
                ▼ IRuntimeResourceProvider::tryResourceBytes（runtime 定义）
        ┌─────────── runtime（编译链 S4 读取/S10 复查）────────┐
        │  ResourceBytes{data,size,digest}（生命周期 §8.6）     │
        │  digest 入 CanonicalModel 身份 → 内容变化＝新身份＝   │
        │  缓存不命中（CON-05）                                 │
        └──────────────────────────────────────────────────────┘
  身份规则：resourceId（core ObjectId，project 分配）＋contentDigest（SHA-256）
           为引用键；路径（absPath/sourcePathHint/PackEntry）一律不作身份、
           不作引用键、不参与缓存/失效判定
```

### 8.6 accessVersion 语义与 ResourceBytes 生命周期（P-RT-6 裁决）

**accessVersion**＝io 资源读取契约版本（`ResourceRef.accessVersion`，runtime.md §8.6 字段）。当前＝**1**。升版条件：ResourceBytes 语义/缓冲规则/digest 算法任一变更（digest 算法固定 SHA-256，升版即格式语义变更，走本文变更记录＋runtime 同步）。语义：调用方可据 accessVersion 判定读取契约兼容性（不透明版本戳）。

**ResourceBytes 指针生命周期（对 runtime.md P-RT-6 的裁决，本文为登记裁决者）**：

1. **采纳 runtime 提议并加强**：`ResourceBytes.data` 指向的缓冲区自 `tryResourceBytes` 返回起**至少**保持有效至：① 调用方对**同一 provider 实例**发起下一次调用；或 ② provider 析构。io 实际保证：缓冲区由 provider 内部稳定区持有，**直至 provider 析构均有效**（强于最低要求，调用方可放心同步消费）。
2. **调用方纪律**（与 runtime 提议一致）：同步消费、不跨调用长期持有指针——即便 io 保证更长的有效期，跨调用持有不构成契约。
3. **并发只读安全**：provider 满足并发 `tryResourceBytes`（runtime.md §8.6 注明）；实现上每次调用返回独立稳定缓冲（内部池化，绝不复用仍在暴露中的缓冲），无数据竞争。
4. **不跨调用缓存 Recorded 资源**：外部（未固化）资源每次调用重新读取＋重算 digest（§8.5）——保证 runtime S10 摘要复查语义成立；Solidified 资源（项目内不可变对象）允许以 `{resourceId, accessVersion}` 为键缓存字节＋digest（不可变保证缓存安全）。
5. 若未来改值拷贝（小资源）语义兼容本裁决（更强不弱）——无需 runtime 变更。

---
## 9. 公共接口、线程、取消与生命周期

### 9.0 公共约定（本章全部接口适用）

```cpp
// IoFwd.hpp
namespace sdurws::ird::io {

using IoString = std::string;                       // UTF-8
inline constexpr std::uint32_t kAccessVersion = 1;  // §8.6 读取契约版本

// 协作取消令牌：io 定义、调用方（execution/命令侧）实现驱动；
// 取消是协作式检查点（§4.4），非抢占、非强杀
class IoCancelToken {
public:
    virtual ~IoCancelToken() = default;
    virtual bool isCancelled() const = 0;           // 幂等；一经真值不再复位
};

// 进度：已完成单位数/总量（总量未知=0）＋阶段标签（稳定英文短语）
struct IoProgress { std::uint64_t done, total; const char* stage; };
using IoProgressCallback = std::function<void(const IoProgress&)>;

template <typename T> struct IoResult {             // 非抛出返回（§1.4）
    T value{};                                       // 仅 ok 时有效
    IoError error;                                   // ok 时 error.code == Ok
    explicit operator bool() const;                  // ok 判定
};
}
```

- **签名均为实现建议**（自洽契约片段，未编译验证——与兄弟单元卡同口径）；实现期允许等价调整，语义不变。
- **错误一律 `IoError{IoErrorCode, params, detail}`**；跨单元上报时映射为 `DiagnosticRecord`（diagnostics 工厂，IO-\* 码表 §15.1）。
- **取消**：所有长操作接受 `IoCancelToken*`（null＝不可取消）；取消返回 `IoError{IO-CANCELLED}`——**取消不是错误诊断**（UX-03），调用方据此转 Canceled 状态。
- **线程**：除会话型对象（§9.3 Reader、§9.9 ImportSession 注明）外，接口实现并发安全（内部无共享可变状态或加锁）；io 不创建线程。
- **确定性**：同输入同输出（含诊断文本参数）；不依赖时钟（时间戳仅在"快照"类数据中作记录不作判定）、不依赖迭代顺序（集合输出按稳定序——字典序或声明序）。
- **生命周期/所有权**：接口对象由 `IoRuntime`（§9.11）装配创建，进程级或会话级逐接口注明；返回的值类型由调用方所有；`shared_ptr` 仅用于明确共享的不可变产物。
- **副作用**：仅 §各接口注明（临时区/输出文件/诊断）；失败路径副作用＝清理自身临时产物，不改任何调用方数据。

### 9.1 `ISafePathResolver`（SafePath）

```cpp
class ISafePathResolver {                       // 进程级；并发安全；无状态（规则集不可变）
public:
    virtual ~ISafePathResolver() = default;
    // role 决定规则集（§4.1/§4.3.1）；base 仅 P-5（项目资源区根）/P-6 使用
    virtual IoResult<NormalizedPath>
        normalize(PathRole role, const std::wstring& rawPath,
                  const std::filesystem::path& base = {}) const = 0;
    // P-4 批量（包条目/manifest）：折叠键查重；任一非法即整批拒绝（errors 全量列出）
    virtual IoResult<void>
        normalizePackEntries(std::size_t entryCount,
                             const std::function<IoString(std::size_t)>& entryAt) const = 0;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | `rawPath` 非 null；P-4/P-5 角色时 `base` 必须提供（P-5＝项目资源区根） |
| 后置条件 | 成功：`NormalizedPath` 各字段填充（§4.3.2）；失败：无部分产物 |
| 错误类型 | `IO-SEC-PATH-ESCAPE/SYMLINK/RESERVED/TOO-LONG`、`IO-RES-NOT-FOUND`（P-1 实体校验时） |
| 线程约束 | 并发只读安全；规则集构造后不可变 |
| 确定性 | 同输入同结果；等价键仅依赖路径字符串与 canonical（canonical 失败时降级口径一致） |
| 取消行为 | 纯计算，无取消点（耗时上界＝路径长度） |
| 生命周期 | 进程级单例（`IoRuntime` 创建） |
| 所有权 | 无状态；结果值归调用方 |
| 副作用 | 访问文件系统元数据（symlink_status/attributes/canonical）——只读 |
| 调用示例 | `resolver.normalize(PathRole::ProjectResourceRef, L"objects/obj-a/cv-1", projRoot)` |
| 合法调用 | 见 §4.3.3 合法行 |
| 非法调用 | P-4/P-5 未传 base（实现返回 `IO-FORMAT-INTERNAL` 防御性拒绝）；以 resolver 结果当作对象身份（规约禁止，SP-5） |

### 9.2 `IBudgetGuard`（BudgetGuard）

```cpp
struct BudgetSpec {                             // §4.5 数值表的运行时形态
    static BudgetSpec productDefault();         // io 产品默认（不可变来源）
    BudgetSpec& tighten(BudgetDimension, std::uint64_t v);          // 只收紧
    BudgetSpec& relaxToHardLimit(BudgetDimension);                  // 显式放宽至硬上限
    bool isHardLimited(BudgetDimension) const;  // 包导入通道禁用放宽的判定源
};
class IBudgetGuard {                            // 会话级（scope 树）；单 scope 非并发
public:
    virtual ~IBudgetGuard() = default;
    virtual IoResult<BudgetScopeId> openScope(const BudgetSpec&) = 0;
    virtual IoResult<void> charge(BudgetScopeId, BudgetDimension,
                                  std::uint64_t amount) = 0;   // 饱和加法；超限即拒
    virtual IoResult<void> closeScope(BudgetScopeId) = 0;      // 余额回收至父
    virtual BudgetLedgerSnapshot ledger(BudgetScopeId) const = 0; // 诊断用快照
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | amount≥0；scope 处于打开态；多阶段累计时子 scope 先于父关闭 |
| 后置条件 | charge 成功＝该维度累计入账；失败＝**状态不变**（本笔不入账，整体由调用方中止） |
| 错误类型 | `IO-SEC-BUDGET-*`/`IO-SEC-BOMB-RATIO`（比较型三要素：actual/limit/unit）；`IO-FORMAT-INTERNAL`（非法 scope） |
| 线程约束 | 单 scope 内操作非并发（单线程读取流程）；ledger() 并发只读 |
| 确定性 | 饱和算法（§4.5.2）；同序列 charge 同判定 |
| 取消行为 | 无内建取消（检查点由读取流程在 charge 前后插入，§4.4） |
| 生命周期 | 会话级（随导入/固化会话创建销毁） |
| 所有权 | guard 归 io 会话对象；BudgetSpec 值拷贝归调用方 |
| 副作用 | 无 I/O；超限诊断经 sink 上报 |
| 调用示例 | 固化三段共用：`scope=budget.openScope(spec); budget.charge(scope,SingleFileBytes,st.sizeBytes); …` |
| 合法调用 | tighten 任意维度；包导入 spec `isHardLimited` 四维全真 |
| 非法调用 | charge 超过硬上限（即便 relax 也不可逾越）；在多线程共享同一 scope 并发 charge |

### 9.3 `ICsvReader`

```cpp
struct CsvReadOptions {
    std::optional<std::uint64_t> headerRow;     // 1 起数据行号（nullopt=无表头）
    CsvArityPolicy arity = CsvArityPolicy::Reject;      // Reject|PadTrailing|TrimExcess
    CsvBlankPolicy  blank = CsvBlankPolicy::Skip;       // Skip|Reject
    std::uint64_t   maxRowErrors = 1000;        // 超出截断＋计数
};
class ICsvReader {                              // 会话型：一个实例绑定一个文件一次读取
public:
    virtual ~ICsvReader() = default;
    virtual IoResult<CsvDialect>
        probe(const std::filesystem::path& file,     // P-1/P-7 角色；BOM/标识行/分隔符探测
              IBudgetGuard*, IoCancelToken*) = 0;
    // 单遍流式：逐行回调；返回 RawTable 元数据＋行错误全量（≤maxRowErrors）
    virtual IoResult<RawTable>
        read(const std::filesystem::path& file, const CsvReadOptions&,
             const std::function<bool(std::uintsize /*row*/, CsvRowView&&)>& onRow,
             IBudgetGuard*, IoCancelToken*, IoProgressCallback = {}) = 0;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 文件存在可读（P-1 角色规范通过）；budget scope 已开 SingleFileBytes/TotalBytes/CsvRowCount/CsvFieldChars |
| 后置条件 | 成功：RawTable 含方言、表头（若有）、错误清单；行数据已经回调交付（流式，不在表内重复存储——RawTable 可配置保留行）；失败：无部分业务产物（回调已交付的行由调用方丢弃） |
| 错误类型 | `IO-FORMAT-CSV-*`（E1~E12 矩阵）；`IO-SEC-BUDGET-*`；`IO-RES-*`；`IO-CANCELLED` |
| 线程约束 | 实例单线程使用（会话型）；probe 结果可跨线程只读 |
| 确定性 | 同文件同选项同输出；行错误顺序按行号列号稳定 |
| 取消行为 | 每行检查点；取消即停，返回 `IO-CANCELLED`（已回调行数在 progress 中） |
| 生命周期 | 会话级（每次 read 一个实例或复用实例串行多次 read） |
| 所有权 | CsvRowView 为零拷贝视图，仅在回调内有效；RawTable 归调用方 |
| 副作用 | 读文件；诊断上报；无写 |
| 调用示例 | §13 requirements 消费样例（字段映射前置 probe→read） |
| 合法调用 | 带标识文件（§5.3 剥离语义）；无标识文件（原样） |
| 非法调用 | 以 reader 输出直接当业务模型（规约：业务解析归业务单元）；同一实例并发 read |

### 9.4 `ICsvWriter`

```cpp
struct CsvWriteOptions {
    CsvDialect dialect = CsvDialect::rwDefault();   // §5.1 标识行参数（quote 恒为 "）
    bool emitDialectMarker = true;                  // 本软件导出恒 true（false 仅测试）
};
class ICsvWriter {                              // 会话型：一个实例一个目标文件
public:
    virtual ~ICsvWriter() = default;
    virtual IoResult<void> open(IOutputTarget&&,        // 原子目标（§4.6）或内存缓冲
                                const CsvWriteOptions&) = 0;
    virtual IoResult<void> writeHeader(const std::vector<IoString>&) = 0;  // 不查重——列名重复由调用方/读侧管
    virtual IoResult<void> writeRow(const std::vector<CsvCell>&) = 0;      // 转义+RFC4180（§5.3/§5.6）
    virtual IoResult<void> finish() = 0;                 // flush＋原子替换＋完整性回读校验（可选）
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | open 成功后 write\*；单元格无 NUL（E7 写侧）；数值有限（isfinite 断言） |
| 后置条件 | finish 成功＝目标原子就位（先前输出或完整保留或被确认替换）；finish 前失败＝**目标不变** |
| 错误类型 | `IO-FORMAT-CSV-CHAR`（NUL）；`IO-RES-*`（写失败四分类）；`IO-PACK-TARGET-EXISTS`（NeverOverwrite 命中） |
| 线程约束 | 实例单线程；写出内部缓冲无锁 |
| 确定性 | canonical：同数据字节一致（§5.5——AT-22 基础） |
| 取消行为 | finish 前调用方废弃实例 → 析构清理临时（目标不变）；write 无检查点（行级耗时极小），取消在 finish 层生效 |
| 生命周期 | 会话级（RAII：未 finish 析构＝放弃＝清理） |
| 所有权 | 输出目标归调用方（成功后）；转义仅存在于 io 内部缓冲与文件层 |
| 副作用 | 写目标文件（原子协议）；无项目写 |
| 调用示例 | reporting 多格式导出：`writer.writeRow({std::string("=SUM(...)")}) → 文件层 "'=SUM(...)"` |
| 合法调用 | 任意含 `=+-@'` 前缀/引号/分隔符/换行原文 |
| 非法调用 | 期望 writer"清除"公式字符（规约：转义≠清除，§5.3）；未 open 即 write |

### 9.5 `IStructuredDataReader`（JSON）＋ `IJsonWriter`

```cpp
struct JsonReadOptions { const IoString* profileId; };  // null=仅语法/安全层（manifest 等内部件）
class IStructuredDataReader {                  // 无状态服务；并发安全
public:
    virtual ~IStructuredDataReader() = default;
    virtual IoResult<JsonDocument>
        parse(const std::filesystem::path&, const JsonReadOptions&,
              IBudgetGuard*, IoCancelToken*) = 0;
    virtual IoResult<JsonDocument> parseBytes(std::string_view utf8,
              const JsonReadOptions&, IBudgetGuard*, IoCancelToken*) = 0;
    // 结构校验（profile 驱动，§5.9.2）——可与 parse 合并（profileId 给定时）
    virtual IoResult<void> validate(const JsonDocument&, const IoString& profileId) const = 0;
};
class IJsonWriter {                            // 会话型（同 ICsvWriter 生命周期模型）
public:
    virtual IoResult<void> write(IOutputTarget&&, const JsonDocument&,
                                 const JsonWriteOptions& /*canonical 固定*/) = 0;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 文件/字节为 UTF-8（BOM 容忍）；profileId 已注册（未注册＝`IO-FORMAT-INTERNAL`，装配期防漏） |
| 后置条件 | 成功：受限 DOM（含每值位置区间）；版本判定先于 schema 校验（§5.9.2）；失败：无部分 DOM 外泄 |
| 错误类型 | `IO-FORMAT-JSON-*`（编码/重复键/NaN/数值/版本/schema 族）；`IO-SEC-BUDGET-JSON*`；`IO-RES-*` |
| 线程约束 | reader 并发安全；writer 会话型单线程 |
| 确定性 | canonical 写出字节一致（§5.9.3）；诊断路径稳定 |
| 取消行为 | 流式解析每块检查点 |
| 生命周期 | reader 进程级；writer 会话级（RAII 同 §9.4） |
| 所有权 | DOM 归调用方；Preserve 块原样透传 |
| 副作用 | 读文件；无写（writer 副作用＝原子输出） |
| 调用示例 | manifest 解析（profile=null）；REQ-12 需求 JSON（profile="ird-requirements/1"，requirements 注册） |
| 合法调用 | 带扩展块的 profile（Preserve 名单内） |
| 非法调用 | 以 parse 成功当业务合法（§2.5）；对同一 writer 并发 write；io 侧注入默认值（IO 不做，§5.9.2） |

### 9.6 `IResourceReader`（外部/项目内资源文件层读取）

```cpp
struct ResourceOpenSpec {
    PathRole role;                              // P-1（用户源）或 P-5/P-6（区内/中转）
    std::filesystem::path base;                 // P-5/P-6 必填
};
class IResourceReader {                         // 无状态服务；并发安全
public:
    virtual ~IResourceReader() = default;
    // 打开＋final-path 复核（SP-6）：返回流式读取句柄（预算挂钩）
    virtual IoResult<ResourceStreamHandle>
        open(const std::filesystem::path&, const ResourceOpenSpec&,
             IBudgetGuard*, IoCancelToken*) = 0;
    virtual IoResult<ResourceSnapshot>          // §8.2 快照（全读＋摘要）
        snapshot(const std::filesystem::path&, const ResourceOpenSpec&,
                 IBudgetGuard*, IoCancelToken*) = 0;
    virtual IoResult<ResourceKind>              // 魔数/扩展名识别（§6.3）
        identify(const std::filesystem::path&) const = 0;
    // XML 依赖树（§6.5）：include/mesh/material/texture 边＋循环检测
    virtual IoResult<ResourceDependencyTree>
        dependencyTree(const std::filesystem::path& importRoot, IBudgetGuard*,
                       IoCancelToken*, IoProgressCallback = {}) = 0;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 角色与 base 齐备（§9.1 同）；预算 scope 覆盖 SingleFileBytes/TotalBytes/（树场景）FileCount/DirDepth/IncludeDepth |
| 后置条件 | open 成功＝句柄通过 SP-4/SP-6（reparse 拒绝＋final-path 复核）；snapshot 成功＝三元组完整 |
| 错误类型 | `IO-SEC-PATH-*`/`IO-SEC-SYMLINK`/`IO-SEC-BUDGET-*`/`IO-RES-{NOT-FOUND,ACCESS-DENIED,READONLY,LOCK-CONFLICT}`/`IO-FORMAT-XML-CYCLE`/`IO-CANCELLED` |
| 线程约束 | 并发安全（句柄各自独立） |
| 确定性 | 同文件同快照（未变时 digest 相同）；依赖树遍历序＝字典序稳定 |
| 取消行为 | 流读取每块检查点；dependencyTree 每节点检查点＋进度 |
| 生命周期 | 服务进程级；句柄会话级（RAII 关闭） |
| 所有权 | 字节流归句柄；snapshot/树归调用方；**摘要经 core ContentDigester** |
| 副作用 | 只读文件系统；诊断上报 |
| 调用示例 | URDF 导入：`depTree = reader.dependencyTree(userDir, …)` → modeling 消费 |
| 合法调用 | P-1 用户源（含 UNC/网络盘，跟随链接但快照按实体路径） |
| 非法调用 | 以 snapshot.digest 之外的任何东西（路径/mtime）作内容身份；用本接口读取 `.rwdesign` 内部对象（须经 §9.7 项目字节源） |

### 9.7 `IRuntimeResourceAdapter`（实现 runtime `IRuntimeResourceProvider` 的 io 桥；P-RT-6 落点）

```cpp
// io 侧依赖注入源（project 实现，L5 装配期绑定——P-IO-1 注入形态）
class IProjectBytesSource {                    // 对象库只读字节（含摘要校验后的不可变副本）
public:
    virtual ~IProjectBytesSource() = default;
    virtual IoResult<ProjectBytesView> tryObjectBytes(core::ObjectId,
                                                     core::ContentVersion) const = 0;
};
class IExternalRefSource {                     // 草稿外部引用记录（project DraftService 持久层）
public:
    virtual ~IExternalRefSource() = default;
    virtual IoResult<ExternalRefRecord> tryRecord(const IoString& externalRefId) const = 0;
};
// 桥接实现：class RuntimeResourceAdapter : public runtime::IRuntimeResourceProvider
//   （runtime.hpp 定义接口；adapter 由 L5 装配或（裁决后）project/runtime 侧直连编译——§3.2/§15.3 P-IO-1）
//   tryResourceBytes(resourceId)：
//     ├─ Solidified → IProjectBytesSource（缓存 {resourceId,accessVersion}→字节）
//     └─ Recorded  → IExternalRefSource → P-1 读取（每次重读＋重算 digest，不缓存）
//   返回 runtime::ResourceBytes{data,size,digest}（生命周期=§8.6 裁决）
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | 两个注入源已绑定；resourceId 为 project 已分配对象 |
| 后置条件 | 成功＝字节已过 SafePath（Recorded 路径）/来源不可变（Solidified）＋预算＋digest 复算一致；失败＝ResourceReadError（runtime 码，io 原始诊断随 detail） |
| 错误类型 | runtime `ResourceMissing/ResourceChanged/ResourceBudget`（runtime.md §8.6 四分）＋ io 原始码嵌 detail |
| 线程约束 | 并发只读安全（§8.6 第 3 条） |
| 确定性 | 同资源未变→同 digest；Recorded 每读必现检（不缓存） |
| 取消行为 | 不接受取消令牌（runtime 契约）；读取耗时受 SingleFileBytes 上界约束 |
| 生命周期 | 会话级（随项目存储上下文）；析构释放全部暴露缓冲（§8.6 生效域终点） |
| 所有权 | 缓冲归 adapter（池化）；调用方仅视图 |
| 副作用 | 读文件/读对象库；Solidified 缓存（内存） |
| 调用示例 | runtime 编译 S4/S10（runtime.md §5.4） |
| 合法调用 | 同步消费返回视图 |
| 非法调用 | 跨调用持有 data 指针（纪律）；经 adapter 写任何数据（只读） |

### 9.8 `IResourceSnapshotter`（快照/探测/固化执行）

```cpp
enum class ExternalRefState { Ok, Missing, Changed };
class IResourceSnapshotter {                   // 无状态服务（固化中转经 TempArea）
public:
    virtual ~IResourceSnapshotter() = default;
    // 段②检测（§8.3）：record 来自 IExternalRefSource
    virtual ExternalRefState probe(const ExternalRefRecord&,
                                   IBudgetGuard*, IoCancelToken*,
                                   std::optional<ProbeDetail>* out = nullptr) const = 0;
    // 段③固化执行（§8.4；project 编排/发布）：返回中转副本路径＋前后快照
    virtual IoResult<SolidifyStagingResult>
        solidifyToStaging(const std::filesystem::path& source,       // P-1
                          const std::filesystem::path& stagingDir,   // P-6（.staging/tmp/solidify-<id>）
                          std::uint64_t budgetBytes, IBudgetGuard*, IoCancelToken*,
                          IoProgressCallback = {}) = 0;
};
struct SolidifyStagingResult {                 // project 侧组装 SolidifyResult（§5.7 project.md）
    ResourceSnapshot before, after;            // A/B 快照（变化检测判据）
    core::Digest256  copiedDigest;             // 副本复算摘要
    bool             sourceChangedDuringCopy;
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | probe：record.recordedDigest 非全零；solidify：stagingDir 为 project 授权中转位（P-6 规范通过） |
| 后置条件 | solidify 成功＝中转副本完整＋`sourceChangedDuringCopy=false`＋digests 一致；变化检出＝中转已清理＋失败返回 |
| 错误类型 | `IO-RES-MISSING/CHANGED`（探测事实，非内部错误）；`IO-SEC-BUDGET-*`；`IO-RES-*` 四分类；`IO-CANCELLED` |
| 线程约束 | probe 并发安全；solidify 会话级（中转区互斥） |
| 确定性 | 同源同快照；变化检测判据唯一（digest，§8.3） |
| 取消行为 | 复制流每块检查点；取消清理中转 |
| 生命周期 | 服务进程级；中转目录会话级 |
| 所有权 | 结果值归调用方（project 组装发布） |
| 副作用 | 读源＋写中转（P-6 唯一写点）＋清理；**不写 objects/**（发布归 project） |
| 调用示例 | project 固化命令：`stg=snapshotter.solidifyToStaging(rec.absPath,tmp,req.budgetBytes,…)` → 入库 |
| 合法调用 | 用户重关联后的新源路径（P-1，PM-09 流程内） |
| 非法调用 | 期待 io 判定"是否允许正式报告"（归 evidence）；跳过 staging 直接写 objects/ |

### 9.9 `IPackageImporter` / `IPackageExporter`

```cpp
struct PackageImportOptions {
    std::filesystem::path targetDir;           // 发布目标（project 二次校验后 rename）
    BudgetSpec budget;                          // 四维强制硬限（§4.5.2 包通道）
    ReplacePolicy replace = ReplacePolicy::NeverOverwrite;
};
class IPackageImporter {                       // 会话型（ImportSession 独占一个临时区）
public:
    virtual ~IPackageImporter() = default;
    virtual IoResult<PackageImportSession>
        begin(const std::filesystem::path& packFile, const PackageImportOptions&,
              IoCancelToken*, IoProgressCallback) = 0;      // 步骤①②
    // 步骤③~⑦：全部通过→Verified；任一失败/取消→会话终态（§7.3/§7.6 状态图）
    virtual IoResult<PackageImportReport> verifyThrough(PackageImportSession&) = 0;
    virtual IoResult<void> cleanup(PackageImportSession&) = 0;  // ⑨/失败清理（幂等）
};
struct PackageExportOptions {
    std::filesystem::path targetFile;          // .rwpack 目标（P-7）
    bool includeResults, includeReports, includeDrafts;      // 勾选记忆归 ui（PM-05）
    ReplacePolicy replace = ReplacePolicy::OverwriteAtomic;  // 导出默认原子覆盖（用户已选目标）
};
class IPackageExporter {                       // 无状态服务（每次 export 自建临时区）
public:
    virtual ~IPackageExporter() = default;
    virtual IoResult<PackageExportReport>
        export_(ISnapshotFileSource& consistentView,         // project 实现（§7.2 一致视图）
                const PackageExportOptions&, IBudgetGuard*,
                IoCancelToken*, IoProgressCallback) = 0;
};
// io 定义、project 实现（注入）：快照视图枚举＋字节读取（§7.2 图）
class ISnapshotFileSource {
public:
    virtual ~ISnapshotFileSource() = default;
    virtual IoResult<std::vector<PackFileEntry>> enumerate() const = 0;   // 快照清单（path,size）
    virtual IoResult<std::string> read(const IoString& packPath) = 0;     // 稳定版本语义
};
```

| 契约项 | IPackageImporter | IPackageExporter |
| --- | --- | --- |
| 前置条件 | 包文件存在可读（P-1）；targetDir 父目录存在可写且与临时区同卷 | consistentView 为 project 快照（非目录扫描）；目标父目录可写 |
| 后置条件 | Verified＝临时区含已验证树＋报告全绿；任何失败/取消→临时区按状态图清理、**targetDir 零写入** | 成功＝目标文件原子就位＋完整性自检通过＋报告；失败/取消→目标不变 |
| 错误类型 | §7.4 威胁矩阵全表＋`IO-CANCELLED`＋`IO-PACK-CLEANUP-FAILED` | `IO-SEC-BUDGET-TEMP`、`IO-RES-*`、`IO-PACK-TARGET-EXISTS`、`IO-CANCELLED` |
| 线程约束 | 会话单线程；不同会话并行（各自临时区） | 单次调用单线程；多导出并行（不同目标） |
| 确定性 | 同包同判定；哈希全量复算 | 同快照同字节（zip 时间戳固定为 rwpack.json.createdAtUtc——文件层可复现，IO-D11） |
| 取消行为 | 每条目/每文件检查点（§7.3 步注） | 每文件检查点 |
| 生命周期 | 会话级（session RAII 兜底清理） | 调用级（内部临时区 RAII） |
| 所有权 | 临时区归 io（至 project 发布完成）；报告归调用方 | 输出文件归调用方 |
| 副作用 | 临时区创建/删除；发布由 project 执行（⑧） | 目标文件写出＋临时区清理；**零写源项目** |
| 调用示例 | project PackageService：`begin→verifyThrough→(打开协议校验)→rename→cleanup` | project：`export_(snapshotView, opts, …)` |
| 合法调用 | Verified 后由 project 发布；发布成功后 cleanup | 勾选任意组合（objects/revisions 恒在清单——project 侧保证） |
| 非法调用 | io 侧 rename 到 targetDir（发布归 project N-7）；未 verifyThrough 即要求发布数据；cleanup 中删除非本会话文件 | 传入目录扫描型 source（违反一致视图契约）；导出中改 targetFile |

### 9.10 `ITempAreaManager` / `IAtomicFileWriter`（支撑设施）

```cpp
class ITempAreaManager {                       // 进程级；并发安全（会话前缀互斥）
public:
    virtual ~ITempAreaManager() = default;
    virtual IoResult<TempAreaSession>
        create(const TempAreaSpec& /*role,baseDir,name*/, IoCancelToken*) = 0;
    virtual IoResult<void> cleanup(TempAreaSession&) = 0;        // 幂等；仅删本会话根
};
class IAtomicFileWriter {                      // §4.6 协议；会话型
public:
    virtual ~IAtomicFileWriter() = default;
    virtual IoResult<AtomicTarget>
        prepare(const std::filesystem::path& target, ReplacePolicy) = 0;
    // prepare→[写入]→commit(替换)／abort(清理)；commit 前目标不变
    virtual IoResult<void> commit(AtomicTarget&) = 0;
    virtual IoResult<void> abort(AtomicTarget&) = 0;
};
```

（契约同上文规则；两设施并发约束：TempArea 会话互斥经等价键；AtomicWriter 实例单线程。）

### 9.11 装配入口 `IoRuntime`

```cpp
class IoRuntime {                              // L5 装配期创建；进程级；随后只读
public:
    struct Injection { ISafePathResolverPtr safePath; /*…全部服务*/ };
    static IoResult<std::shared_ptr<IoRuntime>>
        create(IoDiagnosticsSinkPtr,           // diagnostics 注入（码表注册在此完成）
               IProjectBytesSourcePtr, IExternalRefSourcePtr);   // project 注入（§9.7）
    // 注册（装配期）：JSON profile（格式所有者单元）
    IoResult<void> registerJsonProfile(JsonProfile&&);
    // 访问器：resolver()/budgetFactory()/csvReader()/jsonReader()/resourceReader()/
    //         snapshotter()/packageImporter()/packageExporter()/tempArea()/atomicWriter()
    // 桥接产出：makeRuntimeResourceAdapter() → shared_ptr<runtime::IRuntimeResourceProvider>
};
```

| 契约项 | 内容 |
| --- | --- |
| 前置条件 | diagnostics sink 非空（码表注册必需）；create 后、start 前完成全部注册 |
| 后置条件 | 注册完成后服务只读；IO-\* 码表已向 diagnostics 注册（§15.1） |
| 线程约束 | 装配期单线程；运行期访问器并发安全 |
| 确定性 | 注册顺序不影响行为（profile 按 id 查找） |
| 取消行为 | 无 |
| 生命周期 | 进程级（L5 持有）；其派生会话对象生命周期见各接口 |
| 所有权 | L5 所有；注入源所有权留驻 project |
| 副作用 | 码表注册；无文件副作用 |
| 调用示例 | L5：`ioRuntime = IoRuntime::create(diagSink, projBytes, extRefs)`；runtime 编译器经 `ioRuntime->makeRuntimeResourceAdapter()` 获得 provider |
| 合法调用 | 装配期注册、运行期只读 |
| 非法调用 | 运行期 registerProfile（拒绝——防行为漂移）；未注入 sink 即 create |

### 9.12 错误码汇总（IoErrorCode，建议值——随 IO-T02 注册冻结）

| 族 | 码 |
| --- | --- |
| Ok/取消 | `Ok`；`IO-CANCELLED`（状态，非诊断） |
| 格式-CSV | `IO-FORMAT-CSV-DIALECT/-ENCODING/-QUOTE/-ARITY/-DUPCOL/-CHAR` |
| 格式-JSON | `IO-FORMAT-JSON-ENCODING/-DUPKEY/-NUMBER/-VERSION-MISSING/-VERSION-TYPE/-VERSION-FUTURE/-VERSION-LEGACY/-UNKNOWN/-REQUIRED/-TYPE/-RANGE` |
| 格式-包 | `IO-FORMAT-PACK-ZIP/-ENCRYPTED/-ENTRY/-MANIFEST` |
| 格式-XML/网格 | `IO-FORMAT-XML-CYCLE`；`IO-FORMAT-MESH-UNKNOWN` |
| 安全 | `IO-SEC-PATH-ESCAPE/-SYMLINK/-RESERVED/-TOO-LONG`；`IO-SEC-BUDGET-FILE/-TOTAL/-COUNT/-DEPTH/-EXPAND/-ROWS/-FIELD/-JSON/-JSON-DEPTH/-JSON-STRING/-MESH/-INCLUDE/-REFDEPTH/-TEMP`；`IO-SEC-BOMB-RATIO` |
| 资源 | `IO-RES-NOT-FOUND/-ACCESS-DENIED/-READONLY/-LOCK-CONFLICT/-MISSING/-CHANGED` |
| 包过程 | `IO-PACK-DUPLICATE-ENTRY/-HASH-MISMATCH/-REF-INCOMPLETE/-TARGET-EXISTS/-DISK-FULL/-CLEANUP-FAILED` |
| 内部 | `IO-FORMAT-INTERNAL`（防御性；开发级） |

### 9.13 线程与取消总则

1. io **不创建线程**：长操作由调用方线程驱动（execution 任务线程/命令线程）；io 只保证这些操作可取消、流式、预算受控。
2. 服务级对象并发安全；会话级对象（Reader/Writer/ImportSession/TempArea 会话）单线程。
3. 取消＝协作检查点（§4.4/§9.0）；取消后的清理同样接受检查点（清理不挂起）。
4. 进度回调在**驱动线程**内同步调用（不做跨线程投递——ui 侧自行 Marshal，与 diagnostics sink 投影同口径）。

---

## 10. 跨单元协作与事务边界

> 通用事务边界声明：io 的所有操作**不在任何 project 事务内**（不打开事务、不写 HEAD/objects/drafts）；唯一例外路径是"project 命令（如固化、包导入发布）**调用** io 的读取/校验/中转设施，事务归 project 命令所有"。io 失败＝命令失败＝project 事务回滚，io 无需也无力补偿项目状态。

### 10.1 与 project

| 维度 | 设计 |
| --- | --- |
| 谁发起 | 双向：project 发起（另存为/包导出导入/固化/升级器读旧格式、对象负载 canonical 读写）；io 发起（仅经注入源读对象字节/引用记录） |
| 输入/输出 | project→io：SolidifyRequest、快照视图（ISnapshotFileSource）、授权中转位、发布目标；io→project：已验证临时目录＋报告、固化中转副本＋前后快照、ZIP 编解码结果、外部源检测（Missing/Changed） |
| 安全校验 | io 侧全部防护（SP/Budget）；project 侧写入路径白名单（其 §4.1 注）与发布时序（§7.3⑧） |
| 事务边界 | 固化/包导入发布均在 project 命令/发布协议内；io 产物是命令输入（N-7：发布归 project） |
| 线程 | project 命令线程驱动 io 调用；导入导出经 execution 后台任务时由任务线程驱动 |
| 取消 | 调用方令牌贯通（PM-05：取消即清理临时区） |
| 失败/清理 | io 清理自身临时区；project 回滚事务/不动当前项目（PM-02） |
| 重试 | 允许：导入/导出/固化均为无副作用可重试（目标未污染）；重试创建新会话 |
| 禁止反向 | io 不链接 project 头、不调用命令端口、不读 `.rwdesign` 内部（经注入）；project 不实现 CSV/JSON/ZIP 解析（N-6，project.md） |

### 10.2 与 runtime

| 维度 | 设计 |
| --- | --- |
| 谁发起 | runtime 编译链（S4 读取/S10 复查）调用 provider；provider 由 io 实现（桥接 §9.7，L5 装配注入——runtime.md §3.2 登记形态） |
| 输入/输出 | runtime→io：resourceId；io→runtime：`ResourceBytes{data,size,digest}`（已验证：SafePath/预算已过、digest 复算一致） |
| 安全校验 | io 侧完成（provider 语义即"已验证内容"）；runtime 不重复防护、不放宽 |
| 事务边界 | 编译期内存活（runtime.md §8.6：缓存仅编译期）；io provider 会话随存储上下文 |
| 线程 | 并发只读（§8.6.3） |
| 取消 | provider 不接受令牌（runtime 编译令牌在编译器层检查；单资源读取上界受预算约束） |
| 失败/清理 | 四类错误严格区分（ResourceMissing/Changed/Budget——runtime 转译、io 原始诊断）；Recorded 每读现检 → S10 复查语义成立 |
| 重试 | 重关联/固化后重编译（runtime.md §5.4"是"） |
| 禁止反向 | runtime 不自行读外部文件（N-7，runtime.md）；io 不触碰 CanonicalModel/编译器 |

### 10.3 与 diagnostics

| 维度 | 设计 |
| --- | --- |
| 谁发起 | io 全程产码（装配期注册＋运行期构造） |
| 输入/输出 | io→diagnostics：IO-\* CodeDescriptor（paramSchema：预算三要素/行列定位/包条目）、DiagnosticRecord 流；diagnostics→io：工厂/脱敏/两级日志设施 |
| 安全校验 | 脱敏：路径参数与 CSV 原文片段经 diagnostics 脱敏设施（NFR-SEC-07）；报告 exportSafeSummary 不含原始文本 |
| 事务边界 | 诊断是会话态，不写 `.rwdesign`（diagnostics D-10 同源） |
| 线程/取消 | sink 投影线程模型随 diagnostics；io 长操作诊断不因取消重复上报 |
| 失败 | 码未注册→`IO-FORMAT-INTERNAL` 拒绝构造（工厂口径） |
| 重试 | 无状态，天然幂等 |
| 禁止反向 | diagnostics 不解析文件（§8.6 仅登记接口义务）；io 不复制日志/脱敏设施（NFR-MNT-04） |

### 10.4 与 execution

| 维度 | 设计 |
| --- | --- |
| 谁发起 | execution 任务（包导入导出后台化、大 CSV 导入）在其工作线程调用 io；io 不注册任务类型 |
| 输入/输出 | execution→io：IoCancelToken 实现（挂接其 2s/10s 协作协议 ARCH §4.4）、驱动线程；io→execution：进度/取消响应 |
| 安全校验 | io 防护不因后台化减弱 |
| 事务边界 | 归档写 results/ 走 project 归档端口（execution↔project，io 不在场） |
| 线程 | 任务线程驱动；io 会话对象不跨任务共享 |
| 取消 | 令牌贯通；io 检查点密度（每块/每行/每条目）满足 2s 进入 Canceling 的响应性 |
| 失败/清理 | io 失败→任务 Failed（含诊断）；临时区清理在任务收尾前完成 |
| 重试 | 新任务新会话 |
| 禁止反向 | io 不依赖 execution 头、不自建调度；execution 不做文件解析（其 N-7） |

### 10.5 与 modeling（阶段 B 接入）

io 提供：URDF/Xacro 文件层读取＋依赖树＋预算＋快照（§6.2）；Xacro 展开机制护栏；MDL-20 导出的原子写出与自有工件读取（带来源标识校验——profile 由 modeling 注册）。modeling 提供：字段映射/默认补全/忽略与不支持项报告（MDL-03）、WorkCell 有损提取（MDL-18，R2）、展开语义（MDL-19）。禁止反向：modeling 不自行读文件（其消费 io 通道）；io 不建 RobotDesign/不判"不可表达"。失败：展开/依赖缺失给定位诊断，**导入结果为草稿**，不产生正式模型修订（PM-01）。

### 10.6 与 requirements（阶段 B）

io 提供：CSV 通道（probe→read＋逐行列错误——REQ-05/AT-02）、需求 JSON 通道（REQ-12，与 CSV 同字段字典——字典由 requirements 注册为 JsonProfile 与列映射表）。requirements 提供：字段映射、单位预览、姿态规则、就绪校验。边界：**预览不产生正式证据**（REQ-06）由 requirements 就绪状态承载；CSV 行错误到"Must 非法→输入未完成"的映射是业务判定（§2.5：格式错误≠业务非法）。禁止反向：requirements 不解析文件字节。

### 10.7 与 selection（阶段 C）

io 提供：目录包文件层校验（§7.8：清单核对/引用存在性/CSV 通道）；目录落位经 project 存储端口（`catalog/<id>/<ver>/`）。selection 提供：字段字典/单位/唯一性/范围/插值语义（SEL-01/02）。失败：不完整导入删除重导不留目标（project.md §4.1 同口径）；锁定版本不可变（SEL-08）由 project 引用保护承载。

### 10.8 与 evidence

io 提供：外部源 Missing/Changed 事实（§8.3）、资源快照摘要（进复现/证据要素——经 reporting/evidence 契约）。evidence 提供：CON-03 固化门禁与 DataInsufficient 判定（未固化→阻断正式结论）。边界：**io 不判定证据资格、不判定是否允许正式报告**（N-3）；资源缺失≠工程不可行（§6.6）。

### 10.9 与 ui / workflow

ui/workflow 发起用户交互（向导收集选择、进度对话框、勾选记忆——PM-05/PM-14 勾选记忆归 ui）；io 不持有界面对象、不呈现进度（回调纯数据）。向导"取消或失败不留半成品"由 io 临时区契约＋project 创建协议共同保证。禁止反向：ui 不解析文件（N-6，ui.md）。

### 10.10 与 reporting / optimization（阶段 B/C，输出通道消费）

reporting（RPT-02/AT-22）与 optimization 一站式导出（OPT-12/AT-34）消费：ICsvWriter/IJsonWriter（canonical 确定性＝多格式逐字段一致基础）、IAtomicFileWriter（导出失败保留先前输出/选择与路径可重试——RPT 验收要点）、IPackageExporter（证据包/模型包，经 project 快照）。禁止反向：io 不渲染章节、不组装导出清单。

---
## 11. 验证方案及故障注入矩阵

### 11.1 测试基础设施与命名

| 项 | 约定 |
| --- | --- |
| 框架/目标 | googletest（vcpkg，`find_package(GTest CONFIG REQUIRED)`）；`sdurws_ird_io_test`＋`sdurws_ird_io_contract_test`（§3.3；development-task-breakdown §5.5） |
| 用例命名与追溯 | 用例名携带需求/AT 追溯字段（`ird-test-report.json` 承载——testkit §3.6 约定）；组名 `IoSec/IoCsv/IoJson/IoRes/IoPack` |
| 替身 | 可控文件/包测试替身：CSV/JSON 字节生成器、恶意 zip 构造器（穿越/炸弹/重复/链接条目）、FaultInterceptor（写失败第 N 次注入——testkit §6.4）、TempDir（每用例隔离临时根） |
| 故障注入原则 | 文件系统故障（磁盘满/写失败/清理失败）经 `ITempAreaManager`/`IAtomicFileWriter` 的 fake 适配层注入（testkit §10.2 io 行义务）；**不**依赖真实磁盘满 |
| Windows GUI 规则 | 涉及 GUI（进度对话框/向导）的验证流程设计遵循用户级 `C:\Users\zgl18\.codex\AGENTS.md`：VS x64 开发环境、`QT_QPA_PLATFORM='windows'`、一次一个可执行文件、绝对路径启动、不 offscreen。**本阶段仅设计流程，不启动 GUI 程序**（§11.4） |
| 通过判定 | 下表"预期结果"全部可程序化断言；**任何未执行用例不得标记"通过"**；本卡产出时全部状态＝已设计未执行 |

### 11.2 故障注入与契约验证矩阵

| # | 用例（组/名） | 需求/AT 依据 | 前置 | 操作 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- | --- | --- |
| IO-V01 | IoCsv/RoundtripBasic | NFR-SEC-03、AT-02 | 生成含引号/分隔符/换行/中文/空串字段的表 | write→read→逐字段比对；再 write 比对字节 | 数据层逐字符一致；文件层字节一致 | 断言宏逐字段；文件 SHA-256 二次比较 |
| IO-V02 | IoCsv/RoundtripPrefixed | NFR-SEC-03（R5）、AT-02 | 原文含 `= + - @ '` 前缀样例集 | 同 V01 | `'=x`→`x`、`''q`→`'q`、`-3.5`→`'-3.5`→`-3.5`；自反性 decode(encode(s))==s 全样例成立 | 逐样例断言；RawTable 无转义形式残留 |
| IO-V03 | IoCsv/UnmarkedNoRewrite | NFR-SEC-03 | 无标识行外部 CSV（含 `'`、`=` 开头字段） | read | 全字段原样读入（`'=x` 保持 `'=x`） | 逐字段原文比对 |
| IO-V04 | IoCsv/BomQuotEol | NFR-SEC-03、AT-02 | UTF-8 BOM/UTF-16LE/BE、CRLF/LF/CR、RFC4180 引号嵌套样例 | read | BOM 剥离、编码识别、行尾容错、引号语义正确；非 UTF-8 无 BOM 拒绝 | 方言报告字段；错误码 `IO-FORMAT-CSV-ENCODING` |
| IO-V05 | IoCsv/ColumnAnomalies | REQ-05、AT-02 | 重复列名/缺列/多列/空行样例 | read（默认策略＋显式策略两轮） | 默认拒绝且定位（列号/名字/行号）；策略模式下按声明处置并记录 | RowError 集合内容与顺序 |
| IO-V06 | IoCsv/StreamBudget | NFR-SEC-02、NFR-PERF-03 | 生成 10⁶ 行 CSV（流式生成器） | read＋行预算设 10⁵ | 第 10⁵+1 行触发 `IO-SEC-BUDGET-ROWS`；峰值内存有界 | 预算诊断三要素；内存计数器 |
| IO-V07 | IoJson/SchemaUnknownFuture | NFR-DEP-04、PM-06、REQ-12 | 注册测试 profile；构造未知字段/`schemaVersion=999`/重复键/缺必填样例 | parse＋validate | 未知字段拒（Preserve 名单外）；未来版本只读拒绝（含当前/文件版本数据）；重复键/必填定位 | 错误码＋JSON 路径＋行列区间 |
| IO-V08 | IoJson/NaNInfLimits | NFR-SEC-02 | `NaN/1e999`、超长字符串、深嵌套构造器 | parse | NaN/Inf/溢出拒；字符串/深度超限给 `IO-SEC-BUDGET-JSON*` | 错误码与实际/上限值 |
| IO-V09 | IoSec/PathTraversal | NFR-SEC-01 | 构造 §4.3.3 非法表（`..`、绝对、盘符、UNC、保留名、尾点） | normalize(P-4/P-5) | 逐条拒绝＋正确码；消解后合法者放行 | 表驱动断言全集 |
| IO-V10 | IoSec/SameProjectAltPaths | NFR-SEC-01（§4.2.4） | 同目录以 `D:\P`、`d:\p\`、`D:/P` 打开 | 等价键计算＋TempArea 互斥 | 等价键相同；单会话；无重复临时区 | 等价键相等断言；临时区计数 |
| IO-V11 | IoSec/SymlinkJunction | NFR-SEC-01 | 建 symlink（文件/目录）与 junction 指向资源区外 | P-5 引用解析；P-4 展开产物 | 两者均 `IO-SEC-SYMLINK`；区内名字指向区外同样拒 | 码＋final-path 记录 |
| IO-V12 | IoPack/ZipBomb | NFR-SEC-02、PM-05 | 构造高压缩比包（比例>100:1）与超展开量包 | begin→verifyThrough | `IO-SEC-BOMB-RATIO`/`IO-SEC-BUDGET-EXPAND` 中止；临时区清理；目标零写入 | ledger 快照；临时区存在性检查 |
| IO-V13 | IoPack/DuplicateEntries | NFR-SEC-01 | 同名条目×2、仅大小写异条目对 | 步骤③预检 | `IO-PACK-DUPLICATE-ENTRY`；展开前拒绝（无任何落盘） | 展开前断言临时区空 |
| IO-V14 | IoPack/HashMismatch | PM-05 | 篡改 manifest 一条目哈希/篡改 rwpack.json | 导入 | `IO-PACK-HASH-MISMATCH`（定位条目）；整体拒绝 | 报告条目级结果 |
| IO-V15 | IoRes/CyclicIncludes | NFR-SEC-02、MDL-19 | A→B→A include；自包含；深度链 17 层 | dependencyTree | `IO-FORMAT-XML-CYCLE`（环路径）；深度链 `IO-SEC-BUDGET-INCLUDE` | 环清单内容 |
| IO-V16 | IoRes/MeshBudget | NFR-SEC-02 | 二进制 STL 谎报三角形数；真实超限 STL | identify＋预检 | 头声明即拒或读中复核触发 `IO-SEC-BUDGET-MESH` | 触发时机（读前/读中） |
| IO-V17 | IoRes/SourceChangedDuringCopy | NFR-REL-04、CON-03 | 快照源文件；复制中途（FaultInterceptor 定点）改写源内容 | solidifyToStaging | `sourceChangedDuringCopy=true`＋Changed 诊断；中转清理；无部分副本 | before/after digest 不等断言；中转目录不存在 |
| IO-V18 | IoPack/ExportConcurrentWrite | PM-05（D-17） | fake ISnapshotFileSource：读取中某 drafts 键版本切换 | export_ | 按快照重读稳定版本；产出包自洽（无混合版本）；哈希自检通过 | 包内 drafts 内容＝两个完整版本之一 |
| IO-V19 | IoPack/ImportCancel | PM-05、NFR-PERF-02 | 大包导入（慢速 fake 解压） | 各阶段注入取消令牌 | ≤检查点间隔即中止；临时区清理；**无 Canceled 诊断**（状态非错误，UX-03）；目标零写入 | 会话终态；诊断计数=0 |
| IO-V20 | IoPack/DiskFull | PM-05 | TempArea fake 注入剩余空间不足 | 展开至超限 | `IO-PACK-DISK-FULL`（所需/可用比较型）；清理成功 | 报告建议字段 |
| IO-V21 | IoPack/CleanupFailure | PM-05 | fake 删除失败（第 2 次删除调用） | 失败后 cleanup | `IO-PACK-CLEANUP-FAILED`＋残留路径清单（脱敏）；重试 cleanup 幂等可成功；残留均在隐藏前缀临时区 | 残留列表内容；二次 cleanup 结果 |
| IO-V22 | IoPack/FailureNoTarget | PM-05、AT-20 | 全部失败样例（V12~V14/V19~V21）跑后检查 | 目标目录枚举 | **目标项目目录从未创建/写入**（含发布前取消、校验失败、清理失败各分支） | 目录存在性断言（结构性） |
| IO-V23 | IoPack/ExportStagingOnly | PM-05、AT-20 | 导出中途失败注入（压缩阶段） | export_ 失败 | 目标 .rwpack 不存在或为先前完整版本（原子替换未发生） | 目标路径状态 |
| IO-V24 | IoRes/ReadOnlyMedia | NFR-REL（project 只读口径） | 只读属性目录/卷（或 ACL 模拟） | 写目标/固化中转 | `IO-RES-READONLY`（与 ACCESS-DENIED 区分） | 码区分断言 |
| IO-V25 | IoRes/AccessDenied | NFR-SEC（§4.2.5） | ACL 拒读文件 | 读取 | `IO-RES-ACCESS-DENIED`；不降级为 NOT-FOUND | 码＋方向参数 |
| IO-V26 | IoRes/MissingResource | NFR-REL-04、MDL-19 | 依赖树缺叶；外部引用记录指向已删文件 | dependencyTree/probe | `IO-RES-MISSING`＋缺失清单；`ExternalRefProbe::Missing`；**无任何工程不可行结论产出** | 诊断类别＝资源事实（§2.5） |
| IO-V27 | IoRes/DigestStability | CON-05（内容身份） | 同文件 100 次快照；改名/移动后快照 | snapshot 循环 | digest 恒定；路径变化不改变 digest（路径不作身份） | 逐次比对 |
| IO-V28 | IoRes/ReassociateNewRevision | PM-09、CON-03、AT-20 | 固化后外部源变化→用户重关联新源→再固化 | project 命令编排（契约级：命令 fake） | 新固化产生新 materializedVersion＋新修订（project 断言）；io 侧仅提供前后快照与检测 | 命令调用序列记录（io 不产生修订的负断言） |
| IO-V29 | IoPack/ImportRefIntegrity | PM-05、SEL-02（文件层） | 构造 manifest 引用不存在条目/镜像缺必备文件 | verifyThrough⑥ | `IO-PACK-REF-INCOMPLETE`＋断裂链定位 | 报告引用链字段 |
| IO-V30 | IoDiag/Sanitization | NFR-SEC-07、ERR-01 | 含本机路径与长原文片段的诊断 | 产码→diagnostics sink fake | 用户级仅定位（无完整路径/原文）；开发级片段保留；报告 exportSafeSummary 无原始文本 | 两级日志内容比对 |
| IO-V31 | IoPack/BackgroundNonBlocking | NFR-PERF-01/02、AT-34 | 大导入运行于后台任务（驱动线程 fake UI 线程消息循环计数） | 导入中 UI 线程空转打点 | UI 线程无 >2s 无响应窗口（io 同步库调用在任务线程）；进度回调频率合理 | 打点时间戳序列 |
| IO-V32 | IoPack/CommittedVsStaged | PM-05、NFR-REL-01（D-17 口径） | fake source 在快照外投放 `.staging` 残留文件/多余文件 | export_ | 清单外文件**不出现在包内**（存在≠已提交） | 包条目枚举比对 |

### 11.3 GUI 相关验证流程（设计，不启动）

进度/取消/向导类验收（AT-20 包主线、AT-34 一站式导出）需要 GUI 驱动；流程：① 构造样例项目与包；② 按 AGENTS.md 规则以独立进程启动被测可执行文件（VS x64 环境、`QT_QPA_PLATFORM=windows`、单实例）；③ 脚本化触发导出→中途取消→检查目标与临时区；④ 记录 `ird-test-report.json`。该流程随 WP-10/WP-22 集成任务执行；本卡只冻结步骤与断言点。

### 11.4 覆盖核对

§11.2 覆盖任务清单 28 项：CSV roundtrip（V01~03）、BOM/编码/引号/换行（V04）、列异常（V05）、JSON schema/未知/未来（V07）、NaN/超长/深度（V08）、穿越（V09）、UNC/大小写/多路径（V10）、symlink/junction（V11）、炸弹（V12）、重复条目（V13）、哈希（V14）、循环（V15）、复制期变化（V17）、导出并发写（V18/V32）、导入取消（V19）、磁盘不足（V20）、清理失败（V21）、失败不建目标（V22/V23）、只读（V24）、权限（V25）、缺失（V26）、摘要稳定（V27）、重关联新修订（V28）、引用完整性（V29）、脱敏（V30）、后台不阻塞（V31）、预算超限（V06/V16）。**全部为设计，未执行——不得在任何登记中标"通过"**。

---

## 12. 阶段 A 实现任务拆分

本卡局部编号 `IO-Txx`，与 development-task-breakdown §2.12 的 WP-11 任务一一对应（不重排上游编号）；规模/DoD 遵循该文 §5.2 统一完成定义。

| 编号 | 对应 WP 任务 | 标题 | 前置 | 产出 | 验收（含 §11 用例） | 禁止项 |
| --- | --- | --- | --- | --- | --- | --- |
| IO-T01 | WP-11-T02 | 构建落位：io 占位转真实库 | 本卡 v0.1 | `io/CMakeLists.txt`（STATIC，链 core＋diagnostics＋选定 L1/vcpkg 目标）；`IoFwd/IoError` 头 | 双模式构建零错误；红线扫描零命中（§3.3） | 零 Widgets/Qt；预建空测试目标 |
| IO-T02 | WP-11-T03 | SafePath＋BudgetGuard | IO-T01 | `SafePath.hpp/.cpp`、`Budget.hpp/.cpp` | V09/V10/V11/V06 全绿；穿越反例（项目资源区逃逸拒绝）；预算三要素诊断 | 例外扩大化（P-1 例外仅限显式用户源） |
| IO-T03 | WP-11-T04 | CSV 读写器（方言标识＋可逆编码） | IO-T01 | `Csv.hpp/.cpp` | V01~V05 全绿；自带前缀 roundtrip 逐字符一致；无标识原样读入；转义不入结构层 | 数据-only（无公式/命令执行路径）；方言语法超出 §5.1 |
| IO-T04 | WP-11-T05 | JSON 读写器＋ZIP 通道 | IO-T01（P-IO-3 冻结后） | `Json.hpp/.cpp`、`ZipChannel.cpp` | V07/V08 全绿；JSON canonical 字节一致；zip 解包逐字节还原＋哈希（V14 前置） | JsonLite 用于产品格式；第二摘要算法 |
| IO-T05 | WP-11-T06 | 资源导入服务与外部源检测 | IO-T02 | `ResourceIo.hpp/.cpp`（reader/snapshotter/probe/依赖树） | V15~V17、V24~V27 全绿；三段边界用例（一次性读取/引用记录/固化前契约） | 直接创建 CanonicalModel/objects 写入 |
| IO-T06 | WP-11-T05/T07 | 包导入导出 io 侧设施＋固化中转 | IO-T02/T04 | `Package.hpp/.cpp`、`TempArea.cpp`、`AtomicFile.cpp`、`IRuntimeResourceAdapter` 桥 | V12~V14、V18~V23、V29/V31/V32 全绿（发布用 project fake）；失败不留目标 | io 侧 rename 发布；读 `.staging` |
| IO-T07 | WP-11-T07 | 契约测试套件整备与留痕 | IO-T01~T06、WP-02 | `io/test/*`、`io/contract_test/*` 全量 | §11.2 全用例可执行且结果登记（不预设通过）；`ird-test-report.json` 追溯字段齐备 | 用例跳过不报；伪造通过状态 |

阶段 A 不做（登记防抢跑）：Xacro 展开引擎本体（阶段 B 随 modeling）；目录包业务校验（阶段 C selection）；真实 project 发布集成（WP-04-T18，阶段 B）；GUI 进度（WP-10/22）；报告/优化导出编排（WP-12/20/21）。

---

## 13. 后续阶段承接与接口交接清单

### 13.1 对兄弟单元"须在 io 侧冻结"义务的答复（承接 project.md §13.2 / runtime.md §13.2 / diagnostics.md §12 / testkit.md §10.2 io 行）

| 来源 | 义务 | 本文落点 | 状态 |
| --- | --- | --- | --- |
| project.md §13.2 | ZIP 编解码（P-PR-5 裁决后） | §7（IPackageExporter/Importer＋ZipChannel；注入/直连双形态兼容——P-IO-1） | 已冻结契约（实现阶段 B 随 WP-04-T18） |
| project.md §13.2 | SafePath/BudgetGuard 对 project 的注入形态 | §3.2/§9.11（IoRuntime 注入；project 消费形态＝调用 io 服务或经 L5 桥） | 同上 |
| project.md §13.2 | 外部源检测接口（Missing/Changed） | §8.3/§9.8（IResourceSnapshotter::probe） | 已冻结 |
| runtime.md §13.2 | 资源读取/检测/预算实现 | §6/§8/§9.6~9.8 | 已冻结契约（实现 IO-T05，阶段 A） |
| runtime.md §13.2 | accessVersion 语义 | §8.6（＝1；升版条件） | 已冻结（**P-RT-6 关闭**） |
| runtime.md P-RT-6 | ResourceBytes 指针生命周期 | §8.6 五条裁决 | **本文裁决关闭**（裁决者＝io 详设所有者） |
| diagnostics.md §12 | IO-\* 码值与 paramSchema | §9.12/§15.1（建议值；随 IO-T02 注册冻结） | 建议值已给 |
| diagnostics.md §8.6 | SafePath/BudgetGuard/CSV 行错误诊断形状 | §4.5.2/§5.6/§9（比较型三要素、行列定位、脱敏两级） | 已冻结 |
| testkit.md §10.2 | 写入口 fake 适配模板；CSV 方言 roundtrip 用例 | §11.1/§11.2（V01~V05＋FaultInterceptor 适配层） | 已设计 |
| core.md P-AR-1 | SafePath/BudgetGuard 归属 | §2.1 O-3/O-4（类型与实现均归 io；core 不预建） | 承接其建议执行 |

### 13.2 阶段 B/C 消费方向（接口预冻结，不代写对方详设）

| 消费方 | 消费接口/契约 | 接入点 |
| --- | --- | --- |
| modeling | dependencyTree/reader（URDF/Xacro 文件层）、AtomicWriter（MDL-20 导出）、Xacro 展开护栏（IO 阶段 B 补展开引擎机制，语义归 modeling）、WorkCell XML 通道（R2） | WP-13 |
| requirements | CsvReader（probe→read＋行错误）、JsonProfile 注册（ird-requirements）、REQ-12 roundtrip | WP-14 |
| selection | 目录包文件层校验（§7.8 契约：清单/引用/CSV 通道）、catalog 落位经 project 端口 | WP-19 |
| reporting | CsvWriter/JsonWriter（canonical＝AT-22 多格式一致）、AtomicWriter（导出失败保留可重试）、证据包经 IPackageExporter | WP-12 |
| optimization | 一站式导出通道（JSON/CSV/audit/模型包——OPT-12/AT-34） | WP-20/21 |
| workflow/ui | 向导收集选择→io 执行；进度/取消回调；勾选记忆归 ui | WP-22/WP-10 |
| project | 包导入发布集成（WP-04-T18）、固化命令集成、升级器读旧格式探测数据 | WP-04（阶段 B） |

### 13.3 本单元留待阶段 B/C 的自身增量

Xacro 展开机制实现（护栏已定）；`ResourceKind` 扩展（DAE 完整/新格式）；包升级步距数据对接；TempArea 崩溃残留的宿主启动扫描对接（project/壳）；目录包落位端到端。

---

## 14. 需求—设计—验证追踪矩阵

| 需求/上游条款 | io 角色 | 设计落点 | 验证（§11） | 状态 |
| --- | --- | --- | --- | --- |
| NFR-SEC-01 | 实现责任方 | §4.1~4.4、§7.4、§9.1/§9.6 | V09~V11、V13 | 设计完成，未执行 |
| NFR-SEC-02 | 实现责任方 | §4.5、§9.2 | V06、V08、V12、V16 | 同上 |
| NFR-SEC-03 | 实现责任方（唯一转义/还原） | §5.1~5.8、§9.3/§9.4 | V01~V05 | 同上 |
| NFR-REL-04 | 检测实现责任方 | §8.2~8.4、§9.8 | V17、V26、V27 | 同上 |
| PM-05（io 侧） | io 侧实现责任方 | §7、§9.9、§9.10 | V12~V14、V18~V23、V29、V31、V32 | 同上 |
| PM-06（执行侧） | 支撑 | §5.9.2、§7.1 版本规则 | V07 | 同上 |
| REQ-05（支撑） | 解析与逐行错误 | §5、§10.6 | V04、V05、V30 | 同上 |
| REQ-12（支撑） | 读写器＋结构校验 | §5.9、§10.6 | V07 | 同上 |
| SEL-01/02（文件层支撑） | 文件层校验 | §7.8、§10.7 | V29（文件层） | 设计完成（阶段 C 接入） |
| MDL-19（支撑） | 展开护栏＋文件层 | §6.2 | V15、V26 | 设计完成（阶段 B 接入） |
| MDL-18（支撑，R2） | 文件层通道预留 | §6.4 | —（R2） | 预留 |
| MDL-20（支撑） | 原子写出 | §4.6、§9.10 | V23 | 设计完成 |
| CON-03（协作） | 读取/检测/复制 | §8.1、§8.4、§8.5 | V17、V28 | 设计完成 |
| PM-01（三段边界） | 段①③执行＋段②检测 | §8.1~8.4 | V17、V26~V28 | 设计完成 |
| PM-09（协作） | 检测提供方 | §8.3、§10.1 | V28 | 设计完成 |
| NFR-DEP-01 | Windows 路径口径 | §4.2 | V09~V11 | 设计完成 |
| NFR-DEP-04（协作） | 版本字段执行 | §5.9.2、§7.1 | V07 | 设计完成 |
| NFR-SEC-07（消费） | 脱敏 | §5.8、§10.3 | V30 | 设计完成 |
| NFR-MNT-01/04 | 零 Qt/零重复包装 | §1.4、§3.3 | 构建门禁 | 设计完成 |
| ARCH §6.6/§6.7/§7.9、SA-12/SA-14 | 架构承接 | 全文 | 全部 | 设计完成 |
| AT-02（支撑） | — | §5 | V01~V05、V30 | 设计完成，未执行 |
| AT-20（io 部分） | — | §7/§8 | V12~V14、V17~V23、V28 | 设计完成，未执行 |
| AT-22（支撑） | — | §5.5/§5.9.3 | V01、V07 | 设计完成，未执行 |
| AT-34（支撑） | — | §10.10 | V31 | 设计完成，未执行 |
| core.md P-AR-1 | 承接执行 | §2.1、§3.2 | — | 关闭（按其建议） |
| runtime.md P-RT-6 | 裁决者 | §8.6 | — | **本文裁决关闭** |
| project.md P-PR-5 | 关联方 | §3.2、§7、§15.3 P-IO-1 | — | 部分关闭（io 侧免依赖：注入式先行；补边与否待架构） |

---

## 15. 设计决策、风险、待裁决项与变更记录

### 15.1 设计决策登记（IO-Dxx）

| ID | 决策 | 依据/论证 | 影响面 |
| --- | --- | --- | --- |
| IO-D01 | SafePath/BudgetGuard 类型与实现均归 io（core 不预建） | core.md P-AR-1 建议；仅 io 消费无共享必要 | core 零新增；§3.2 |
| IO-D02 | io 编译期依赖严格限定 core＋diagnostics（＋L1）；project/runtime 协作经 io 自有接口＋L5 注入（`IoRuntime`/`IRuntimeResourceAdapter`/`ISnapshotFileSource`） | ARCH §3.5 白名单未登记 io→project/io→runtime/project→io；注入式与 runtime.md §3.2 登记、project.md P-PR-5 备选一致；接口契约与补边结果无关（双形态兼容） | P-IO-1；构建图 |
| IO-D03 | CSV 方言标识行 v1 语法（§5.1：`#rwcsv1`＋封闭键集；quote 恒 `"`、encoding 恒 utf-8） | NFR-SEC-03 只约束"标记行＋版本＋可逆"；语法细则属 io 权限；`'` 引号与转义符冲突故禁止 | 文件格式（新方言＝版本升级） |
| IO-D04 | CSV 读取保留单元格原文，数值/单位不在文件层转换 | roundtrip 逐字符一致的前提；SA-12（单位权威 core、语义归业务）；SourcedValue 原文保留 | §5.2/§5.5 |
| IO-D05 | CSV 部分成功＝行粒度（错误行定位并剔除、正确行保留）；JSON 无部分成功（整档拒绝） | AT-02"正确行保留"；JSON 用于整档结构化数据无行级容错场景 | §5.6/§5.9.4 |
| IO-D06 | 预算默认值/硬上限数值表（§4.5.1）为 io 产品默认；包导入通道四维强制不可放宽 | NFR-SEC-02 要求机制未定数值；工程策略不含预算（ARCH §7.5） | P-IO-4 冻结口径 |
| IO-D07 | `.rwpack` 包格式（rwpack.json＋manifest＋payload/ 镜像；zip64；无目录条目；加密拒） | PM-05"ZIP 传输封装＋逐字节还原＋哈希"；不承载任何权威语义（非第二格式） | §7.1 |
| IO-D08 | 导入临时区与发布目标同卷（发布＝project 同卷 rename）；导出临时区与目标同目录 | 原子性要求（rename 跨卷不可用）；与 project.md §8.10"整体发布（rename）"对齐 | §7.5/§7.3 |
| IO-D09 | 无 BOM 非 UTF-8 文本稳定拒绝（不猜测转码）；编码集＝UTF-8/UTF-16(BOM) | 防静默数据损坏；R5"原样读入"精神；Windows 现实兼容（UTF-16 Excel 导出） | §5.2 |
| IO-D10 | 变化检测权威判据＝digest（size/mtime 仅预筛）；复制期间前后双快照比对 | NFR-REL-04 可检测性；touch 不改内容不应误报 Changed；复制窗口检测为 PM-05/CON-03 协作关键 | §8.3/§8.4 |
| IO-D11 | 包导出确定性：zip 条目时间戳统一取 rwpack.json.createdAtUtc、manifest 字典序、canonical JSON | 同快照同字节（可复现导出；审计友好） | §7.2/§9.9 |
| IO-D12 | ResourceBytes 缓冲有效期＝provider 析构（强于"至下次调用"）；Recorded 资源绝不跨调用缓存 | §8.6 裁决；S10 复查语义依赖每次现读 | P-RT-6 关闭 |

### 15.2 风险登记

| ID | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R-IO-1 | ZIP/XML 第三方库选型（P-IO-3）未冻结，IO-T04 阻塞 | 进度 | 候选已列（libzip/miniz；expat/pugixml），vcpkg 可用性验证前置到 IO-T01 |
| R-IO-2 | Windows reparse point/长路径行为与文档偏差 | 安全边界 | SP-6 句柄级复核（final-path）纵深；V11 用例覆盖 symlink/junction；实现期对照官方文档复核 |
| R-IO-3 | 注入式协作（P-IO-1）增加 L5 装配代码 | 装配复杂度 | IoRuntime 单一入口收口；若架构补边可零改动直连 |
| R-IO-4 | CSV 方言嗅针（无标识文件分隔符统计）在边界样本上不稳定 | 用户体验 | 探测失败→稳定诊断＋显式选择路径（REQ-05 映射 UI）；永不静默猜测 |
| R-IO-5 | 大包导入在慢速网络盘（P-1 UNC 源）耗时 | 响应性 | 检查点密度＋进度；取消即时；时间治理归 execution（§4.5.2） |
| R-IO-6 | 崩溃残留临时区依赖下次同前缀会话回收 | 磁盘占用 | 隐藏前缀＋标记文件（pid/时间）；宿主启动扫描对接登记 §13.3 |

### 15.3 待裁决项（P-IO-x；对上游已登记项的处置一并列出）

| ID | 事项 | 依据 | 影响 | 本文处置/建议 | 裁决者 |
| --- | --- | --- | --- | --- | --- |
| P-IO-1 | ARCH §3.5 未登记 io↔project、io↔runtime 边，而 PM-05/CON-03/runtime §8.6 需要协作 | ARCH §3.5"表外边＝构建失败"；project.md P-PR-5；runtime.md §3.2 注入登记 | 编译形态 | **io 侧已免依赖**：注入式（IO-D02）先行可用、接口契约与裁决无关；建议架构统一处置：a) 补登 project→io（接口依赖，无环）＋确认 runtime 注入为正式形态（本文推荐）；或 b) 全注入式 | 架构所有者（P-PR-5 同案合并裁决） |
| P-IO-2（=P-RT-6 关闭） | ResourceBytes 指针生命周期 | runtime.md §15.3 P-RT-6（裁决者＝io 详设所有者） | runtime/io 实现 | **本文裁决**（§8.6 五条：最低至下次调用、实际至析构、并发安全、Recorded 不缓存、值拷贝兼容） | 已裁决（io）；runtime 侧按此同步其文档表述 |
| P-IO-3 | ZIP 与 XML 解析库选型（vcpkg：libzip/miniz；expat/pugixml 或 RobWork 自带 XML） | NFR-DEP-03（离线）、NFR-SEC-05（依赖清单）、确定性导出 | IO-T04 | 建议：ZIP＝libzip（成熟/zip64）；XML＝expat（流式/小）；随 IO-T01 验证 vcpkg 可用性后冻结 | WP-11 评审（实现期） |
| P-IO-4 | §4.5.1 预算默认数值表冻结 | NFR-SEC-02 未定数值 | 产品行为 | 数值＝本卡建议默认（Draft）；包通道四维强制不可放宽已定；请架构/评审确认数值档位 | 架构评审（数值可见性） |
| P-IO-5 | CSV 方言标识行 v1 语法（`#rwcsv1`）冻结 | NFR-SEC-03 未定语法 | 导出文件格式 | 语法已给全（§5.1）；请确认后视为 v1 冻结（此后变更走方言版本升级） | 需求/架构评审（格式可见性） |
| P-IO-6 | 诊断码 IO-\* 建议值收编 | diagnostics.md §12（码值随 io 卡注册） | 诊断稳定性 | §9.12 建议值；随 IO-T02 向 StableCodeRegistry 注册时与 diagnostics 收编确认 | diagnostics 所有者 |
| P-IO-7 | 目录包文件清单/文件名结构契约（selection 注册的形态） | SEL-01"模板（清单/主表/曲线/兼容）" | §7.8 校验 | io 已定义校验执行框架；具体文件名/清单 schema 由 selection 卡注册（本文不预写） | selection 详设所有者 |

### 15.4 交付前自审记录（任务清单 12 项，2026-09-10）

| # | 检查项 | 结论 | 证据/修正 |
| --- | --- | --- | --- |
| 1 | 是否重复 project/runtime/modeling/evidence 职责 | 通过 | §2.1 不拥有表 N-1~N-9；发布归 project（§7.3⑧）、编译归 runtime（§6.1）、语义归业务（§5.9.4/§10.5~10.8） |
| 2 | 是否存在第二套持久化格式 | 通过 | `.rwpack`＝传输封装（§7.1"不是第二权威格式"规则行）；RawTable/JsonDocument 均内存态 |
| 3 | 是否将路径当作对象身份 | 通过 | SP-5；§8.2 身份＝digest；§8.5 图"路径不作身份"；V27 断言 |
| 4 | 是否遗漏穿越/符号链接/压缩炸弹防护 | 通过 | SP-2~4/SP-6；V09/V11/V12/V13；双保险（预检＋展开检） |
| 5 | 是否把格式解析成功误当业务合法 | 通过 | §2.5 正交表；§5.9.4 边界；io 不注入默认值、不出业务结论 |
| 6 | 是否把文件存在误当正式提交 | 通过 | §7.2 快照清单结构性保证（不扫描目录）；V32 |
| 7 | 失败/取消/清理失败后是否留可误识别目录 | 通过 | §7.6 状态图（隐藏前缀＋零目标写入）；V19~V23；CleanupFailed 残留不可被误识别（标记文件区分） |
| 8 | 是否未检测复制期间源变化 | 通过 | §8.4 步骤 5 前后双快照；V17 |
| 9 | 是否将资源读取失败升级为工程不可行 | 通过 | §6.6/§10.8：Missing＝资源事实→evidence 判证据不足；V26 |
| 10 | 是否泄露敏感路径或完整资源内容 | 通过 | §10.3/V30：两级脱敏；exportSafeSummary 无原始文本 |
| 11 | 是否引入未经上游批准的新状态/阈值/格式语义 | 有登记 | 新增项均为 io 权限内（预算数值 P-IO-4、方言语法 P-IO-5、accessVersion=1 §8.6）并提请评审可见；无需求级语义新增 |
| 12 | 是否越权修改需求/架构/其他单元机制 | 通过 | 本文仅新增 units/io.md；上游文件零修改；冲突全部走 §15.3 登记（P-IO-1/2 等） |

自审结论：12 项中 11 项通过、1 项（#11）为"有登记"（新增参数均已显式登记待评审，无未声明语义）。**本自审不等同实现测试通过或正式验收**（§11 全部用例状态＝已设计未执行）。

### 15.5 变更记录

| 版本 | 日期 | 摘要 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版草案：承接 REQUIREMENTS v1.16／ARCHITECTURE v0.11 与九份兄弟单元卡（v0.1 Draft）的 io 行义务；冻结路径安全/预算模型、CSV/JSON 可逆编码、资源读取与三段边界固化协作、`.rwpack` 导入导出协议、公共接口与线程/取消契约、验证矩阵与阶段 A 任务拆分；裁决关闭 P-RT-6；登记 P-IO-1~P-IO-7。状态 `Draft`。 |

---

**本文档结束**（io 单元详细设计 v0.1，状态 `Draft`；对应任务 WP-11-T01。）
