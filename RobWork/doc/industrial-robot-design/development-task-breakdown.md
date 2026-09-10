# 工业机械臂设计软件 · 实现任务拆分（development-task-breakdown）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v0.12（Draft；四轮流水线审查修复已登记；见 §7 变更记录） |
| 日期 | 2026-09-10 |
| 状态 | **`Draft`** |
| 文档代号 | DTB |
| 上游 | `REQUIREMENTS.md` **v1.16（Accepted）**（需求语义与验收唯一权威，RV-13）；`ARCHITECTURE.md` **v0.11（Draft）**（20 单元组成、分层与依赖红线唯一权威）；`units/*.md` 单元详设现状 **11/20 已编写**（core/testkit/project/evidence/runtime/policy/execution/diagnostics/ui/io/reporting；v0.1，保留各卡评审状态）；`DETAILED-DESIGN.md` 已建立为 20 单元详设总目录；详设正文由 units/*.md 承担，缺失单元仍先出"编写任务卡"任务 |
| 与 ARCHITECTURE §11.1 的关系 | ARCH §11.1 下游文档链第三环：`units/*.md`（单元详设）→ **本文（任务分配与构建约定）** → 实现。本文是 WPnn-Tkk 两级任务编号的唯一登记处；units 卡内局部编号（CORE-Txx 等）经 §2 各表"≙"映射对接，**不重排、不双轨** |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`（20 个 `sdurws_ird_*` INTERFACE 占位目标＋单元 README＋patches/，共 23 文件，无产品源码；每单元 INTERFACE→真实库的升级时机均为显式任务，见 §2） |
| 实现口径 | **从头构建**（REQUIREMENTS v1.9/v1.11）：一切实现按需求与设计文档新建，不继承、不恢复 `old/` 历史实现（`old/` 磁盘实测**不存在**于仓库根——REQUIREMENTS v1.10 声明与磁盘不符，登记 §4 O-01；仅作功能范围对照的地位亦不可现场行使） |
| 编号规则 | 任务包 WP-00~25 为 REQUIREMENTS §3 既有预留登记，本文全部映射、不新造平行编号；包内任务 `WPnn-Tkk`。**本版无超出 WP-25 的新增包申请** |

> **文档地位与红线**：本文只做任务拆分与编排，不定义需求语义与验收标准（一律回指 REQUIREMENTS 条目与 AT），不进行单元内详细设计（归 units/*.md）。分层红线（ARCH §3.2）：L2 计算内核零 Qt；跨单元协作只经六类稳定端口（ARCH §7.2）；权威唯一（PA-1 归属表）不得被任何任务绕过。任务需求追溯中的需求 ID 均以 v1.16 当前正文为准，不得收窄或扩大。

---

## 0. 主 WP 交付结构（能力型）

本节定义项目计划的主层级。WP-A～WP-I 表示可验收的交付能力，不等同于代码单元。原 WP-00～WP-25 保留为历史/实施任务编号，作为主 WP 的子任务来源；单元详设中的任务编号只描述单元内部实现。

| 主 WP | 交付目标 | 主要单元 | 退出条件 |
| --- | --- | --- | --- |
| WP-A | 治理与基线 | 治理、全部单元 | 需求/架构/追踪矩阵和变更流程冻结 |
| WP-B | 构建与测试基础设施 | core、testkit、构建 | 双模式构建、依赖门禁、测试数据和 CI 可用 |
| WP-C | 核心模型与项目闭环 | core、evidence、policy、runtime、project | 模型身份、证据、策略、项目保存/恢复闭环通过 |
| WP-D | 任务执行与诊断闭环 | execution、diagnostics、project、evidence | 任务状态、取消、诊断、确认放行、结果归档通过 |
| WP-E | 建模与需求定义 | modeling、requirements、io、policy | 导入、规范化编译、需求约束定义闭环通过 |
| WP-F | 运动学与轨迹分析 | kinematics、trajectory、execution、policy | FK/IK、轨迹、碰撞和证据链通过 |
| WP-G | 动力学、传动与选型 | dynamics、drivetrain、selection | 动力学、传动映射、选型回填和复算通过 |
| WP-H | 优化与报告 | optimization、reporting、evidence | 优化结果比较、证据汇总和报告导出通过 |
| WP-I | 产品化交付 | ui、workflow、性能、部署、试点 | 集成、性能、安装包和试点材料通过 |

### 0.1 主 WP 与实施任务映射

| 主 WP | 现有实施任务来源 |
| --- | --- |
| WP-A | WP-00、WP-24-T01、各单元治理任务 |
| WP-B | WP-01、WP-02、WP-03 |
| WP-C | WP-04、WP-05、WP-06、WP-07 |
| WP-D | WP-08、WP-09、WP-04-T10~T15 |
| WP-E | WP-11、WP-13、WP-14 |
| WP-F | WP-15、WP-16 |
| WP-G | WP-17、WP-18、WP-19 |
| WP-H | WP-12、WP-20、WP-21 |
| WP-I | WP-10、WP-22、WP-23、WP-24、WP-25 |

### 0.2 文档分工

- 本文只维护主 WP、实施任务编排、里程碑、跨单元集成和退出条件。
- `units/<unit>.md` 只维护该单元内部的接口、数据模型、状态机、错误语义和单元测试任务。
- `ARCHITECTURE.md` 只维护单元边界、端口和依赖白名单。
- `DETAILED-DESIGN.md` 只作为 20 个单元详设的索引和冻结状态目录。
- 需求到验收、单元任务和主 WP 的唯一映射见本文第 3 节追踪矩阵。

### 0.3 第一批详设完成与编码准入（2026-09-10）

第一批 11 单元详设已编写；本次“第一阶段”指设计批次，不替代 §1.2 的产品阶段 A～E。**当前编码实现入口仅为 CORE-T01**；TK/EV/RT/POL-T01 的 canonical 契约已编码 `dependsOn:["CORE-T01"]`（准入顺序机器化，CORE-T01 完成留痕后方可领取——单元卡内"平行可做"指无技术前置，不覆盖准入顺序），WP-01-T01 门禁随首个 STATIC 目标启用；其余任务按 ready 状态与真实前置逐项放行。不允许跳过前置同时实施全部 11 单元，也不宣布 M0～M3 完成。

| 文档任务 | 产物 | 当前状态 |
| --- | --- | --- |
| WP-08-T01 | execution.md | 已编写；EX-T01～T10 待实现 |
| WP-09-T01 / DIAG-T01 | diagnostics.md | 已编写；DIAG-T02～T11 待实现 |
| WP-10-T01 / UI-T01 | ui.md | 已编写；UI-T02～T14 按阶段子集实施 |
| WP-11-T01 | io.md | 已编写；IO-T01～T07 待实现 |
| WP-12-T01 | reporting.md | 已编写；RPT-T01～T13 为基础设施，T14～T16 按 B/C 前置实施 |

各卡任务拆分比历史 WP 更细（例如 DIAG-T05/06/09、EX-T10、UI-T11～T14、RPT-T01～T16）。完整映射按 [任务索引](traceability/phase-one-task-index.json) 的原始任务行回查，不能按数字后缀推定一一对应。57 份基础任务 JSON 中只有 5 份 T01 为 ready（其中 TK/EV/RT/POL 四份 `dependsOn=CORE-T01`）；其余 52 份基础 JSON 保持 planned；74 份服务单元任务契约中 DIAG-T01/UI-T01 两项编卡任务已 done，其余 72 份保持 planned，待补齐需求追溯与真实前置。详设编写完成不冒充评审 Accepted。

建议实施顺序：CORE-T01 → TK-T01（联同 WP-01-T01 门禁）→ core/testkit 基座 → evidence/runtime/policy/diagnostics → project → execution/io/ui → reporting 基础设施。五个既有 T01 的 ready 仅允许构建落位；服务单元先补任务契约及接口评审，再逐任务放行。workflow 详设仍缺失，不阻塞 M0，但必须在阶段 A 生命周期收口前补齐。

准入证据、局部阻塞和未执行项见 [同步与准入记录](traceability/phase-one-readiness.md)。§4 历史待办以该记录的当前事实补充为准；需求、依赖边与发布验收不在本次变更。

## 1. 单元依赖 DAG 与里程碑划分（实施任务视图）

### 1.1 单元级依赖 DAG（数据源：ARCH §3.5 有向边＋§2.3 分层规则；未新增任何边）

```
L1  框架基线（RobWork / RobWorkSim / RobWorkStudio / Qt / vcpkg 第三方；零源码修改 SA-02）
     ▲
L2  core ──────────────────────┬──────────────────┬──────────────────┐
     ▲（接口依赖）              ▲                  ▲                  ▲
    evidence ──┐               policy             runtime            drivetrain → core,evidence
     │         │                │                  │                  （testkit → core：仅测试目标，T-1/T-2 红线）
     ▼         ▼                ▼                  ▼
L3  execution（→core,evidence,diagnostics,project） project（→core,diagnostics） diagnostics（→core）
    io（→core,diagnostics）   ui（→core,diagnostics）   reporting（→core,evidence,diagnostics,project）
     ▲
L4  业务域（modeling│requirements│kinematics│trajectory│dynamics│selection│optimization）＋workflow（编排）
    ——彼此互链禁止（R-1）；跨单元协作只经六类端口（①命令②查询③评估器④策略⑤事件⑥名称）；
      业务单元消费的端口所有者公共头必须先冻结（§1.3 约束）
L5  应用壳装配（RobWorkStudioApp 壳＋静态白名单 SA-01；策略编辑命令适配器宿主＝ui，P-POL-9 定稿承接）
```

要点：
- **core 是唯一根**；evidence / policy / runtime 三路仅依赖 core，可全并行；drivetrain 依赖 core+evidence。
- **diagnostics 虽为 L3，但被 project/execution/io/ui 依赖**，其任务卡与 StableCodeRegistry 必须先于四者实现（码值收编 P-PR-6）。
- execution 依赖面最宽（core/evidence/diagnostics/project），是 L3 侧汇聚点；reporting 同理但阶段 B 才启用。
- L4 中 optimization 消费 kinematics 评估器（经③端口）与 policy 碰撞子集；trajectory 的段内 IK 连续性检查同样经③端口消费 kinematics 评估器（**不是**业务互链）；selection 消费 drivetrain 的 DriveTrainMappingEvaluator（③端口）；workflow 只消费端口与事件（ARCH §3.4）。
- drivetrain/dynamics 消费 runtime 快照视图的承载形态（ARCH §3.5 未登记该边；runtime.md §13.2 已列为交接）登记为开放问题 O-07，由两卡产出时裁决，本文不私设。

### 1.2 阶段映射（REQUIREMENTS §3 ＋ ARCH §10.1）

| 阶段 | 启用 WP | 单元 |
| --- | --- | --- |
| A 平台 | WP-01~11（＋WP-24 版本基线首版、WP-00 治理、WP-22 任务卡先行） | core/testkit/project/evidence/runtime/policy/execution/diagnostics/ui/io ＋ workflow 卡 |
| B 业务链一 | WP-13/14/15/20(OPT-B)/12(RPT-01-B)（＋WP-04 阶段 B 任务、WP-22 入口） | modeling/requirements/kinematics/optimization ＋ reporting B 级 |
| C 业务链二 | WP-16/17/18/19/12(RPT-01-C)（＋WP-23 C 部分） | trajectory/dynamics/drivetrain/selection ＋ reporting C 级 |
| D R2 纵深 | WP-21/23(R2)＋各 R2 子项任务（KIN-09~11、MDL-18/21、TRJ-08-S1~S3、SEL-09-S1、MDL-12-S1、PM 子项） | optimization 扩展＋execution/checkpoint 规模化 |
| E 整合交付 | WP-22 整合、WP-24 安装包/交付、WP-25 试点 | workflow 整合＋横切交付 |

### 1.3 关键路径、可并行组与里程碑

- **关键路径**：core（WP-03）→ evidence（WP-05）→ project＋execution（WP-04/WP-08，diagnostics 前置）→ kinematics 评估器（WP-15-T03~T07）→ optimization（WP-20 → WP-21）→ reporting 完整报告（WP-12-T07/T08）→ WP-24/25 交付。B 段另一条等长支线：runtime（WP-06）→ modeling（WP-13）。
- **可并行组**（同一批内任务互不依赖）：
  - 并行组 P0：WP-01-T01 门禁 ‖ WP-03-T01 ‖ WP-02-T01 落位（门禁随首个 STATIC 目标启用，对骨架零误报）；
  - 并行组 P1：core 主链（WP-03-T02~T09）‖ testkit 主链（WP-02-T02~T09）；
  - 并行组 P2：evidence（WP-05）‖ runtime（WP-06）‖ policy（WP-07）‖ diagnostics（WP-09）——四路只依赖 core；
  - 并行组 P3：project（WP-04）先行 → execution（WP-08）‖ io（WP-11）‖ ui（WP-10）；
  - 并行组 P4（阶段 B）：modeling ‖ requirements ‖ kinematics ‖ optimization ‖ reporting-B（各域互不依赖，端口契约已冻结为前提）；
  - 并行组 P5（阶段 C）：trajectory ‖ dynamics ‖ drivetrain ‖ selection（drivetrain 最先，selection 消费它）。
- **里程碑**：

| 里程碑 | 判据（全部任务满足 §5.2 DoD） |
| --- | --- |
| **M0 构建闭环** | WP-01-T01、WP-03-T01、WP-02-T01 完成；门禁对骨架现状零误报 |
| **M1 内核与测试基座** | WP-03（T01~T10）＋ WP-02（T01~T10）全绿；testkit 契约断言可被下游消费 |
| **M2 L2 内核四单元** | WP-05、WP-06、WP-07 全绿＋WP-09 diagnostics 全绿；evidence/runtime/policy 公共头冻结（业务域可起卡） |
| **M3 阶段 A 平台收口** | WP-04、WP-08、WP-10、WP-11 全绿；WP-24-T01 版本基线首版登记；契约测试基座运转（AT-10/11/13 载体就绪） |
| **M4 阶段 B 业务链** | WP-13/14/15/20/12(B)＋WP-22 入口任务＋WP-04-T17/T18 全绿（AT-01/02/03/09/20 B 段可验） |
| **M5 阶段 C 业务链** | WP-16/17/18/19＋WP-12(C)＋WP-23-T02~T05 全绿（AT-06/07/08/30/32 可验） |
| **M6 阶段 D 纵深** | WP-21＋WP-23 R2＋各 R2 子项全绿（AT-25/26/33/35/36/38 可验） |
| **M7 阶段 E 交付** | WP-22 整合＋WP-24 安装包＋WP-25 试点材料（AT-20/29 系统级、DEL 门禁） |

- **设计前置铁律**：任何单元首个实现任务（构建落位）启动前，其 `units/<unit>.md` 任务卡必须已存在且 ≥v0.1（禁止"边写代码边补卡"）；任何业务域实现任务启动前，其消费的端口所有者公共头必须已冻结（M2 判据）。

---

## 2. 实施任务卡（WP-00~25 历史编号，按单元分组）

**通用约定（适用于本节每个任务，行内不重复）**：

- **默认验收基线（DoD，§5.2）**：①双模式构建零错误（独立冒烟＋集成；纯文档任务免除构建项）②`ird_gates` 门禁零命中 ③行内"验收标准"所列用例全部通过并**留痕**（未执行不得标注通过）④文档同步（实现与任务卡零偏差，或偏差按 §5.4 登记）。
- **默认禁止项（§5.3 全局红线）**：不继承/恢复 old/；L2 目标零 Qt；业务域目标互链／跨单元私有头／RobWork 前缀拼接剥离（R-1/R-2/R-4）禁止；不修改其他单元公共头与上游文档正文（经增量修订除外）；框架零源码修改（SA-02）。行内"禁止项"列只列**附加**项。
- **"≙"标记**：该 WPnn-Tkk 与既有单元任务卡中的局部任务一一对应（如 WP-03-T02≙CORE-T02）；对应任务的详细输入/产物/验证方式以卡内该行为准，本表不复制、只补充需求追溯与全局编排信息。无"≙"标记的任务为本文新登记（其单元卡已预留该能力或尚待产出）。
- **规模**：S ≈ ≤150 行产品代码或纯文档小节；M ≈ 150~500 行；L ≈ 已按建议拆分点拆为多个提交粒度（≤500 行/提交）。文档型任务标 L 时按章节分会话推进。
- 输出列中 `ird/` 前缀指 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`。

### 2.1 WP-00 治理协调（横切，无单元归属；首用阶段 E）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-00-T01 | 建立治理协调与待裁决项流转登记 | 治理 | 无 | REQUIREMENTS §3/§24（附录 C）、ARCH §11.3；六卡 §10.2/§12.3/§15.3 待裁决表 | 新建 `doc/industrial-robot-design/governance-log.md`（P-* 项集中登记：编号/所有者/状态/消账留痕）＋本文 §4 初始化 | PILOT-01（支持）、DEL-02 | 登记表覆盖六卡全部 P-* 项（P-AR/P-D/P-ENV/P-TK/P-PR/P-EV/P-RT/P-POL）与本文 §4 O-* 项，每项有编号与所有者 | 不裁决任何 P-*/O-* 项（只登记流转） | S |
| WP-00-T02 | 冻结需求追踪表格式契约并重建追踪矩阵（P-02 消账） | 治理 | WP-00-T01 | REQUIREMENTS 附录 C P-02、§4 统计表；本文 §3 矩阵 | 追踪矩阵格式契约文档＋首版生成物（本文 §3 为前身） | P-02（附录 C） | 契约覆盖 180 条主需求＋12 分期子项＋2 分级子级；矩阵计数与 REQUIREMENTS §4 一致 | 不改动需求语义与 ID | M |

### 2.2 WP-01 构建边界与门禁（横切；编号已被骨架 CMakeLists 注释引用）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-01-T01 | 实现依赖红线与补丁门禁 ird_gates | 横切（构建） | 与 WP-03-T01 同批（随首个 STATIC 目标启用） | ARCH §3.2/§3.5/§5.2、§7.2；policy.md §3.4（R-5 扩展建议）；testkit.md §2.4（T-1/T-2） | `ird/cmake/ird_gates.cmake`＋`ird_gates` 自定义目标＋白名单数据文件（依赖边白名单、例外登记册） | ARC-02、NFR-MNT-01/02/04/05/07、NFR-SEC-04、SA-10 | R-1~R-5＋T-1/T-2＋SA-02 补丁核对全部实现；`cmake --build build --target ird_gates` 对骨架现状零命中（INTERFACE 目标无链接边） | 不拦截骨架既有 20 目标；例外只能经 §4.5 登记册 | M |
| WP-01-T02 | CI 常驻与本地一键门禁 | 横切 | WP-01-T01 | ARCH §11.2；本文 §5.5 | CI 配置＋一键脚本（门禁＋双模式构建＋全部 `_test`/`_contract_test` 目标） | NFR-MNT-05、ARCH §11.2 第 2 条 | 每次提交可一键执行全部门禁与测试；CI 建成前本地脚本为强制门槛 | 不引入 Python（本机无；用 cmake -P＋git） | S |
| WP-01-T03 | 例外与白名单登记册持续维护 | 横切 | WP-01-T01 | 本文 §4.5；ARCH §3.2（SA-10） | 登记册条目维护（R-4 runtime 名称解析器清单、R-4 policy 消费点〔O-12 措辞确认后〕、R-3 ui、R-5 policy 等） | NFR-SEC-04、SA-10 | 每条例外有范围/依据/日期；未登记例外被门禁检出即失败 | 不越权裁决架构语义（转呈架构所有者） | S（持续） |

### 2.3 WP-02 testkit（单元卡 units/testkit.md §9，已存在）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-02-T01 | 构建落位与 gtest 框架接入〔≙TK-T01〕 | testkit | 无（与 WP-03-T01 平行） | units/testkit.md §3.3/§3.4；本文 §5.5（gtest 定稿） | `ird/testkit/CMakeLists.txt`（新）＋`TestPaths.hpp`＋`testdata/` 根 | NFR-MNT-01/02、NFR-COR-01（载体） | 双模式构建零错误；数据根三态（env/默认/缺失）用例通过；vcpkg gtest 安装版本登记 §4.4 | 产品目标零 testkit 链接（T-1） | M |
| WP-02-T02 | 实现受限 JSON JsonLite〔≙TK-T02〕 | testkit | WP-02-T01 | testkit.md §4.1 | `testkit/{include,src}/.../JsonLite.*` | NFR-COR-01（数据载体） | TK-JSON 往返/拒绝用例全过；行列错误可定位 | 不用于产品格式（D-02 边界） | M |
| WP-02-T03 | 实现黄金数据集清单模型与完整性校验〔≙TK-T03〕 | testkit | WP-02-T02 | testkit.md §4.2/§4.5 | `Dataset.*`＋`tools/testdata` lint 工具＋`testdata/requirements-ids.json`（v1.16 字典） | NFR-COR-01、附录 D C4（edgeCases） | TK-MAN/TK-INT 全过；反例拒绝且消息含字段路径 | 字典不随本任务私自改需求语义 | M |
| WP-02-T04 | 实现容差档案 ToleranceProfile〔≙TK-T04〕 | testkit | WP-02-T02 | testkit.md §4.3；附录 D | `ToleranceProfile.*`＋示例档案 `testdata/tolerance/kin-fk/` | NFR-COR-01、附录 D C4/C7/第 4/5/9/11 项 | TK-TOL 命中/未命中/超限三态用例通过 | 档案不得放宽附录 D 固定值 | M |
| WP-02-T05 | 实现数值断言与比较详情〔≙TK-T05〕 | testkit | WP-02-T04 | testkit.md §5.3 | `Check.hpp`（＋宏） | NFR-COR-03、附录 D C4 | TK-CMP 全部反例（NaN/长度/空/类型规则）按表落位 | — | M |
| WP-02-T06 | 实现集合与顺序断言〔≙TK-T06〕 | testkit | WP-02-T05 | testkit.md §5.4 | `SetCheck.hpp` | NFR-COR-02（稳定排序）、附录 D 第 12 项 | TK-SET/TK-ORD 排序误判与重复匹配反例（元测试）通过 | — | M |
| WP-02-T07 | 实现契约断言〔≙TK-T07〕 | testkit | WP-02-T05、WP-03 卡（core 类型可用性，P-TK-2） | testkit.md §5.5 | `ContractCheck.hpp` | §8.1 表 3、TASK-03、ERR-01/UX-03（断言） | TK-CTR 正反例通过；envelope 谓词模板以 core 值类型编译 | — | M |
| WP-02-T08 | 实现夹具与确定性环境〔≙TK-T08〕 | testkit | WP-02-T03/T04 | testkit.md §6.1~§6.3 | `Fixture.*`（TempDir/ReproRecord/DeterministicEnv/GoldenFixture） | NFR-COR-02 | TK-FIX 生命周期六步＋keepOnFailure 用例通过 | — | M |
| WP-02-T09 | 实现故障注入原语〔≙TK-T09〕 | testkit | WP-02-T01 | testkit.md §6.4 | `Fault.*`＋`ProcessRunner.hpp`（仅头文件冻结） | NFR-REL-01/02、PM-08（测试支撑） | TK-FAULT occurrence 触发/命中记录用例通过；TK-BUILD 复验产品零链接 | 生产代码零 testkit 头、零 `#ifdef TEST` | M |
| WP-02-T10 | 实现测试报告与 core 接入示例〔≙TK-T10〕 | testkit | WP-02-T03~T09、WP-03-T01~T05 | testkit.md §7、附录 A.1；core.md §8 | `Report.*`＋`gtest/RecordListener.hpp`＋core 示例用例＋安装排除扫描脚本建议（交 WP-24-T05） | NFR-COR-02（复现要素）、NFR-SEC-05/DEP（分发边界） | TK-RPT 六类 outcome 聚合正确；示例数据集随 CI 跑通 | — | M |
| WP-02-T11 | 实现 TestProcessRunner 进程级注入原语〔触发式；原 ≙TK-T11 映射悬空（testkit.md §9 仅 TK-T01~T10），TK-T11 由 testkit 卡触发时补登——见 §4.2 O-33〕 | testkit | WP-02-T09＋触发条件：WP-04-T15/WP-08-T10 需要进程级崩溃/恢复场景时 | testkit.md §6.5/§10.1 | `ProcessRunner.*`（Job Object/事件等待） | NFR-REL-02/03、AT-11/13（自动化载体） | 进程崩溃/强杀/事件等待用例通过；PRJ-TX-7/8、TASK 契约场景可消费 | 不提前实现（无消费者不建目标） | M |

### 2.4 WP-03 core（单元卡 units/core.md §9，已存在）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-03-T01 | 构建落位：core 占位转真实库〔≙CORE-T01〕 | core | 无（与 WP-02-T01/WP-01-T01 同批） | units/core.md §3.3；骨架 CMakeLists | `ird/core/CMakeLists.txt`（新）；`sdurws_ird_core` STATIC（C++17、链 sdurw_math）；注册 `_test` | NFR-MNT-01 | 双模式构建零错误；UT-BUILD 扫描通过；C++17 与基线共存留痕（P-ENV-1 消账）；骨架头注释悬空引用（DETAILED-DESIGN §1.2/§1.5）修订为指向 units/*.md＋本文 | 不得引入 Qt 与 industrialrobot 其他单元链接边 | M |
| WP-03-T02 | 实现身份与摘要〔≙CORE-T02〕 | core | WP-03-T01 | core.md §4.1/§4.2、§5.1/§5.2 | `Identity.*`、`Digest.*`（SHA-256） | ARC-04、CON-05/06、TASK-03（五元组）、附录 D 第 12 项 | UT-ID-T/UT-ID-D 全过（含 FIPS 180-2 已知向量） | — | M |
| WP-03-T03 | 实现值来源与缺失值〔≙CORE-T03〕 | core | WP-03-T02 | core.md §4.3、§5.3 | `Provenance.*` | NFR-COR-03、MDL-05/16、DYN-06（来源标记依据） | UT-MISS 四态语义与"缺失不转零"不变量用例通过 | — | M |
| WP-03-T04 | 实现单位与量〔≙CORE-T04〕 | core | WP-03-T01 | core.md §4.4、§5.4 | `Units.*`（R1 单位表唯一换算实现） | NFR-COR-03、KIN-12（显示单位不改真值）、DYN-03（类型化力矩）、SEL-02 | UT-UNIT 往返/量纲/溢出/非有限用例全过 | 不预建无消费者单位（D-10） | M |
| WP-03-T05 | 实现容差比较〔≙CORE-T05〕 | core | WP-03-T04 | core.md §4.5、§5.5；附录 D C4/C7 | `Compare.*`（closeWithin/逐元素/runtimeAbsoluteTolerance） | 附录 D C4/C7（转写权威）、NFR-COR-02 | UT-TOL 零参考/正负抵消反例/量纲默认值逐一对照用例通过 | 不私自新增或修改默认值（修改走需求变更） | M |
| WP-03-T06 | 实现评估词表〔≙CORE-T06〕 | core | WP-03-T01 | core.md §4.7、§5.6 | `Evaluation.*`（EvaluationMode/TaskState 9/TaskOutcome/EngineeringStatus） | §8.1 表 1/表 3、EVI-01、TASK-02、PM-03 | UT-EVAL token 往返与 NotApplicable 存在性用例通过 | 不新增评估模式/状态（词表漂移） | S |
| WP-03-T07 | 实现诊断数据契约〔≙CORE-T07〕 | core | WP-03-T03/T04 | core.md §4.8、§5.7 | `DiagData.*`（DiagnosticRecord/ConfirmableFinding/ConfirmationCredential） | ERR-01、UX-03、SA-15 | UT-DIAG C-1~C-3 不变量（比较型三要素、invalid 保留原文、不适用显式标记）用例通过 | 文案与码值归 diagnostics（本文只做数据契约） | M |
| WP-03-T08 | 实现事件契约〔≙CORE-T08〕 | core | WP-03-T02/T06 | core.md §4.9、§5.8 | `Events.*`（⑤端口接口归 core） | TASK-03、ARCH §7.2⑤ | UT-EVT 载荷一致性＋参考总线 FIFO/退订用例通过 | 事件不携带数据快照（D-9） | M |
| WP-03-T09 | 实现约定锁定与值语义测试〔≙CORE-T09〕 | core | WP-03-T01 | core.md §4.6、§8 | `core/test/*`（UT-CONV/UT-VAL） | ARCH §7.3（位姿约定）、NFR-COR-02 | 已知算例（旋转复合/逆）与值语义（拷贝/移动/哈希）断言通过 | — | S |
| WP-03-T10 | 文档与门禁同步〔≙CORE-T10〕 | core | WP-03-T01~T09 | core.md 全文 | README 指向核对；UT-BUILD 并入 CI 建议（WP-01-T02）；§10.2 待裁决项状态更新 | NFR-MNT-05 | 本文与实现零偏差登记；P-AR/P-D/P-ENV 项状态最新 | — | S |

### 2.5 WP-04 project（单元卡 units/project.md §12，已存在；T17~T20 为本文新登记、卡 §13.1 已预留）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-04-T01 | 构建落位：project 占位转真实库〔≙PRJ-T01〕 | project | 无（WP-03-T01 后更佳，可平行） | units/project.md §3.3 | `ird/project/CMakeLists.txt`（新）；STATIC（链 core）；注册 `_test`/`_contract_test` | NFR-MNT-01/02 | 双模式构建零错误；零 Qt/零业务边扫描通过；project README 任务卡指向 §9→§12 修正 | — | S |
| WP-04-T02 | 实现存储原语〔≙PRJ-T02〕 | project | WP-04-T01 | project.md §7.1/§7.2 | `win32/AtomicFile.*`＋`IFileOps` 接缝 | NFR-REL-01（暂存＋原子替换） | 原子/持久性分离语义用例＋官方文档口径复核留痕 | — | M |
| WP-04-T03 | 实现写锁与路径〔≙PRJ-T03〕 | project | WP-04-T02 | project.md §9.1~§9.6；ARCH §6.8（SA-17） | `win32/StoreLock.*`、`PathCanonical.*` | PM-07、PM-08、SA-17 | PRJ-TX-7/13：双实例/卡顿/接管/失权防护/只读判定用例通过 | 心跳不得作写权限判据 | M |
| WP-04-T04 | 实现格式契约与编码〔≙PRJ-T04〕 | project | WP-04-T01 | project.md §4.2/§4.4/§4.8 | `PersistenceFormat.hpp`＋`Codec.*` | CON-01、NFR-DEP-04（schemaVersion） | `parse(dump(x))==x` 全类型 round-trip＋非法拒绝用例通过 | — | M |
| WP-04-T05 | 实现对象库〔≙PRJ-T05〕 | project | WP-04-T02/T04 | project.md §4.6 | `ObjectStore.*`（内容编址、只增、LRU） | CON-01 | 共享/篡改/缓存预算用例通过 | — | M |
| WP-04-T06 | 实现修订/分支/元数据〔≙PRJ-T06〕 | project | WP-04-T04/T05 | project.md §4.3/§4.5 | `RevisionIndex.*`（双通道闭包、ProjectMetadata） | ARC-01、PM-12、PA-2 | 走查七步断言；环/回退注入拒绝（INV-M2/M3） | — | M |
| WP-04-T07 | 实现事务引擎〔≙PRJ-T07〕 | project | WP-04-T02/T05/T06 | project.md §7；ARCH §6.4（SA-09） | `TxEngine.*`（七步状态机、.staging、恢复扫描） | NFR-REL-01、PM-08、AT-13 | 边界注入矩阵（F1~F8 进程内部分）通过；旧版本字节不变 | — | L（提交粒度：写入/恢复两段） |
| WP-04-T08 | 实现打开与恢复〔≙PRJ-T08〕 | project | WP-04-T03/T07 | project.md §8.7/§7.4 | `ProjectStoreFactory/ProjectStoreImpl` | PM-02、PM-06、PM-08、AT-11 | 激活前失败不影响当前项目；旧格式/未来版本拒绝码用例通过 | — | M |
| WP-04-T09 | 实现查询端口〔≙PRJ-T09〕 | project | WP-04-T06/T08 | project.md §5.2（②端口所有者） | `QueryPort` 实现（视图/闭包/缓存/隔离） | CON-01、PA-3 | 并发只读＋闭包外不可见用例通过 | — | M |
| WP-04-T10 | 实现命令服务与内置命令〔≙PRJ-T10〕 | project | WP-04-T08/T09、WP-09-T03（诊断码收编） | project.md §5.3、§6；ARCH §7.1（SA-03） | `CommandServiceImpl.*`＋内置元数据处理器＋注册表 | ARC-01、PM-18、AT-29 | 恰好一个修订/失败零修订/串行契约用例通过（PRJ-TX-1/3） | 断言判定由注入处理器执行（P-PR-3 读法） | M |
| WP-04-T11 | 实现确认放行流〔≙PRJ-T11〕 | project | WP-04-T10 | project.md §5.3.3/§5.3.4、§6.7；ARCH §7.1（SA-15） | 交互回调编排、确认绑定与留痕 | MDL-06④（M-10）、ERR-01、UX-03、SA-15 | PRJ-TX-2 四路拒绝/绑定失效用例通过；确认留痕入命令摘要 | project 不依赖 Widgets（回调纯接口） | M |
| WP-04-T12 | 实现草稿服务〔≙PRJ-T12〕 | project | WP-04-T08 | project.md §5.4、§8.1~§8.6 | `DraftServiceImpl.*` | PM-04（保存/应用分离、StaleRevisionRejected） | PRJ-TX-6 恢复与保留用例通过 | — | M |
| WP-04-T13 | 实现撤销/重做〔≙PRJ-T13〕 | project | WP-04-T10 | project.md §5.5、§6.9 | `UndoRedoServiceImpl.*` | PM-18、AT-29 | 新修订/空历史/跨分支用例通过（历史不改写） | — | M |
| WP-04-T14 | 实现归档端口〔≙PRJ-T14〕 | project | WP-04-T08 | project.md §5.6、§10.1 | `ArchiveServiceImpl.*`（begin/batch/finalize/abandon） | TASK-03、PM-13、CON-04 | manifest 发布/幂等/冲突/上下文存活时序用例通过（PRJ-TX-8、F9） | — | M |
| WP-04-T15 | 契约测试与崩溃注入落地〔≙PRJ-T15〕 | project | WP-04-T02~T14、WP-02-T09（T11 按需） | project.md §11 全表；testkit §6.4/§6.5 | `project/test/*`、`project/contract_test/*`（PRJ-TX-1~14） | ARC-01/02、CON-01/04、PM-04/07/08/12/18、NFR-REL-01、AT-13 | §11 表逐项执行通过并留痕（未执行不得标注通过） | — | L（按用例组分提交） |
| WP-04-T16 | 文档与门禁同步〔≙PRJ-T16〕 | project | WP-04-T01~T15 | project.md 全文 | README 核对；§15.3 状态更新；P-PR-5/P-PR-6 裁决申请提交 | NFR-MNT-05 | 零偏差登记；待裁决项最新 | — | S |
| WP-04-T17 | 实现外部资源固化服务与重关联命令注册（阶段 B） | project | WP-04-T10、WP-11-T06 | project.md §5.7/§13.1；ARCH §6.6 | `project/src/` 固化模块＋重关联命令处理器注册 | CON-03、PM-09、NFR-REL-04、AT-21 | 未固化阻断正式结论（EVI-01 证据不足口径）用例；重关联显式提交产生新修订 | 磁盘写入仍唯一归 project | M |
| WP-04-T18 | 实现另存为与项目包实体（阶段 B） | project | WP-04-T14、WP-11-T05（ZIP 编解码；P-PR-5 裁决后） | project.md §5.8/§8.8~§8.10 | 另存为复制＋`.rwpack` 导出/导入实体 | PM-05、NFR-SEC-01/02、AT-13/20 | 解包逐字节还原＋哈希校验＋预算/穿越防护＋失败不留目标目录用例通过 | — | L（导出/导入两提交） |
| WP-04-T19 | 实现 PM 分期子项：属性/体积/改名/历史浏览/可达性报告（R2） | project（ui 协作） | WP-04-T09、阶段 D 启用 | project.md §13.1；REQUIREMENTS §17.1 | ProjectMetadata 消费侧只读面板等 | PM-08-S1、PM-11-S1~S3、PM-12-S1 | §17.1 各子项独立验收（字段与 project.json/ProjectMetadata 一致〔A2 口径〕；改名 projectId 不变；体积与目录一致；历史与 revisions 一致） | 不混入 R1 交付范围 | M |
| WP-04-T20 | 实现 PM-04-S1 草稿落盘周期可调（R2） | project | WP-04-T12 | REQUIREMENTS §17.1 | 草稿服务参数化＋设置接入 | PM-04-S1 | 设置持久化并按新周期（60~600 s）生效 | — | S |

### 2.6 WP-05 evidence（单元卡 units/evidence.md §12，已存在）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-05-T01 | 构建落位：evidence 占位转真实库〔≙EV-T01〕 | evidence | 无（与 WP-03-T01/WP-02-T01 平行） | units/evidence.md §3.4 | `ird/evidence/CMakeLists.txt`（新）；STATIC（链 core）；注册 `_test`/`_contract_test` | NFR-MNT-01/02 | 双模式构建零错误；红线扫描零命中 | 仅 core 一条单元边（D-7） | S |
| WP-05-T02 | 实现错误与依赖类型〔≙EV-T02〕 | evidence | WP-05-T01 | evidence.md §3.5/§4.2 | `Errors.*`、`Dependency.hpp` | CON-05 | 声明闭包校验用例通过 | — | M |
| WP-05-T03 | 实现 AnalysisSnapshot〔≙EV-T03〕 | evidence | WP-05-T02 | evidence.md §4.1 | `Snapshot.*`（builder/校验/refs-only＋materialized） | CON-01、CON-06 | EV-ID-1/3＋修订闭包拒绝（混入反例）用例通过 | — | M |
| WP-05-T04 | 实现 InputSlice 与双层身份〔≙EV-T04〕 | evidence | WP-05-T03 | evidence.md §4.3/§5 | `Slice.*`（builder/SliceCodec） | CON-05/06、KIN-13、NFR-COR-02 | canonical 往返＋位模式＋NaN 拒绝用例通过 | — | M |
| WP-05-T05 | 实现证据 Profile 与证明契约承载〔≙EV-T05〕 | evidence | WP-05-T04 | evidence.md §6.1~§6.3；§8.1 表 4（P-05） | `Evidence.*`（Profile/证明/搜索未果/覆盖校验） | EVI-01（表 4）、CON-03（固化门禁） | EV-VER-6/7、EV-COV-2/3；validateProof 逐字段反例通过 | Profile 明细归需求表 4，不复制双账本（D-14） | M |
| WP-05-T06 | 实现五级汇总判定〔≙EV-T06〕 | evidence | WP-05-T05 | evidence.md §6.4~§6.6；§8.1 表 2（C2/C5/C6/C8） | `Verdict.*`（决策表/VerdictTrace/基准检查） | EVI-01/02、REQ-06、KIN-04（R8 口径） | EV-VER-1~8、EV-COV-1/4 五级优先级正反例全过；缺失全量列出 | 汇总不得凭状态字段采信证明（D-9） | M |
| WP-05-T07 | 实现 ResultEnvelope 构造校验〔≙EV-T07〕 | evidence | WP-05-T06 | evidence.md §7；§8.1 表 3 | `Envelope.*`（make/validateCombination） | TASK-02、CON-02、SA-13 | EV-ENV-1/2 表 3 全组合矩阵用例通过；Preview 构造边界拒绝 | — | M |
| WP-05-T08 | 实现 ResultCurrentness〔≙EV-T08〕 | evidence | WP-05-T04/T07 | evidence.md §8.1 | `Currentness.*` | CON-02/05、AT-05 | EV-CUR-1~4：内容身份/不可解析/跨上下文/不写回用例通过 | 当前性纯投影不写回 | M |
| WP-05-T09 | 实现缓存/检查点兼容判定〔≙EV-T09〕 | evidence | WP-05-T07 | evidence.md §8.2 | `Compatibility.*` | CON-04、OPT-06 | EV-CPA-1~3：Quick≠Verified/契约/检查点独立用例通过 | — | M |
| WP-05-T10 | 实现评估器接口与注册表〔≙EV-T10〕 | evidence | WP-05-T02/T05 | evidence.md §9（③端口所有者） | `Evaluator.*`（descriptor/两注册表/manifest） | EVI-01、ARCH §7.2③、OPT-03/05、SEL-05 | EV-REG-1/2：注册边界/并发/manifest 稳定用例通过 | 不建调度器（与 execution 分界，D-12） | M |
| WP-05-T11 | 测试替身与契约套件〔≙EV-T11〕 | evidence | WP-05-T03~T10、WP-02 可用 | evidence.md §11；testkit §10.2 | `ScriptedEvaluator`＋EV-* 用例体＋`testdata/golden/ev-slice-fixture/` | §8.1 表 3、CON 家族 | §11 矩阵逐条通过并留痕；替身边界声明（EV-REG-3） | 替身数据不得冒充真实证据 | M |
| WP-05-T12 | 文档与门禁同步〔≙EV-T12〕 | evidence | WP-05-T01~T11 | evidence.md 全文 | README 核对；§15.3 状态更新（含 P-RT-9 消账） | NFR-MNT-05 | 零偏差登记；待裁决项最新 | — | S |

### 2.7 WP-06 runtime（单元卡 units/runtime.md §12，已存在）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-06-T01 | 构建落位：runtime 占位转真实库〔≙RT-T01〕 | runtime | 无（WP-03-T01 后更佳，可平行） | units/runtime.md §3.4、P-RT-3 | `ird/runtime/CMakeLists.txt`（新）；STATIC（链 core＋rw/rwsim 零 Qt 基线目标，目标名本任务实测核对并回登 §4.6） | NFR-MNT-01、SA-02 | 双模式构建零错误；零 Qt/零单元边扫描；L1 链接清单留痕（P-RT-3 消账输入） | L1 依赖集按 P-RT-3 登记确认前不扩散 | S |
| WP-06-T02 | 实现错误与注入契约〔≙RT-T02〕 | runtime | WP-06-T01 | runtime.md §3.3/§3.4 | `Errors.*`、`Sources.hpp` | NFR-COR-03 | 错误码全表用例通过 | — | S |
| WP-06-T03 | 实现 Description 与校验器〔≙RT-T03〕 | runtime | WP-06-T02 | runtime.md §4.2、§5.2 S2/S3 | `Description.hpp`＋结构与单位校验器（含耦合矩阵校验） | MDL-21（承载）、NFR-COR-03 | RT-CPX-3/RT-CPL-1/RT-BW-6 校验器部分全过 | — | M |
| WP-06-T04 | 实现 CanonicalModel 与编码器〔≙RT-T04〕 | runtime | WP-06-T03 | runtime.md §4.3/§4.5 | `CanonicalModel.*`＋`Codec.*`（RT-Codec 三形态） | ARC-03（唯一规范模型）、CON-05 | RT-ID-1/2/3；`parse(encode(x))==x` 往返；身份域排除项（显示字段）用例通过 | CanonicalModel 不持久化（D-1） | M |
| WP-06-T05 | 实现 RuntimeNameMap〔≙RT-T05〕 | runtime | WP-06-T04 | runtime.md §7；ARCH §7.4（SA-05，⑥端口） | `NameMap.*`（生成/消歧/双射/内容身份） | ARC-04、MDL-14、CON-06、AT-18 | RT-NM-1~7 双向往返全量用例通过 | 前缀操作唯一例外点（R-4 例外清单随 T13 登记） | M |
| WP-06-T06 | 实现基座—世界变换规则〔≙RT-T06〕 | runtime | WP-06-T04 | runtime.md §6；ARCH §7.3（M-11） | `BaseWorldTransform.*`（预设/校验/正反解纯函数） | MDL-22、AT-37、DYN-01（重力投影） | RT-BW-1/2/3/6：四预设＋反例用例通过；P-RT-4 冻结留痕 | 单一写入点，禁止下游二次旋转 | S |
| WP-06-T07 | 实现 WorkCell 编译器〔≙RT-T07〕 | runtime | WP-06-T04/T06 | runtime.md §5.2 S6、§8.2/§8.4 | `Adapter.hpp`＋`WorkCellCompiler.*` | ARC-03、MDL-06 | RT-AD-1/2、RT-EQ-1（1e-9 m/rad，附录 D 第 4 项）、RT-BW-4 用例通过 | — | L（Frame 树/Device 两提交） |
| WP-06-T08 | 实现 DynamicWorkCell 编译器〔≙RT-T08〕 | runtime | WP-06-T07 | runtime.md §5.2 S7、§8.5 | `DynamicWorkCellCompiler.*`（能力门控） | MDL-06（原子性）、DYN-06（降级）、CON-04 | RT-CPX-1/2、RT-AD-3 门控/原子性/销毁顺序用例通过 | rwsim API 表达力缺项走补丁流程不私改 | M |
| WP-06-T09 | 实现 RuntimeSnapshot 与工厂〔≙RT-T09〕 | runtime | WP-06-T05/T07/T08 | runtime.md §9.1~§9.3 | `Snapshot.*`（并发只读/迟到归档支持） | CON-01/02、AT-10/19 | RT-SNAP-1~4：并发/隔离/迟到用例通过（TSAN） | — | M |
| WP-06-T10 | 实现编译缓存纯判定〔≙RT-T10〕 | runtime | WP-06-T04 | runtime.md §9.4 | `CacheKey.*`（分层键） | CON-04/05 | RT-CACHE-1~4 分层键与判定表全用例通过 | 存储归 execution（本文只做纯判定） | S |
| WP-06-T11 | 实现编译器集成与取消〔≙RT-T11〕 | runtime | WP-06-T03~T09 | runtime.md §5.1~§5.7 | `Compiler.*`（十段链/事务/取消/资源复查） | ARC-03、MDL-06（双编译原子性）、TASK-01（取消） | RT-CPX 全组＋RT-RES-1/2＋RT-CONT-1/2 事务状态机全转移用例通过 | 编译器无内部超时（D-11） | L（十段链/事务两提交） |
| WP-06-T12 | 测试替身与契约套件〔≙RT-T12〕 | runtime | WP-06-T01~T11、WP-02 可用 | runtime.md §11；testkit §10.2 | Scripted\*/Fake\*＋RT-\* 用例体＋`testdata/golden/rt-fk-equation/`、`rt-namemap-roundtrip/` | ARC-03/04、AT-16/18/37 | §11 矩阵逐条通过并留痕；RT-STUB-0 边界声明 | 替身不作 RobWork 算法正确性证明 | M |
| WP-06-T13 | 文档与门禁同步〔≙RT-T13〕 | runtime | WP-06-T01~T12 | runtime.md 全文 | README 核对；§15.3 状态更新；R-4 例外登记（名称解析器文件清单）提交 WP-01-T03 | NFR-MNT-07 | 零偏差登记；例外清单入登记册 | — | S |

### 2.8 WP-07 policy（单元卡 units/policy.md §12，已存在）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-07-T01 | 构建落位：policy 占位转真实库〔≙POL-T01〕 | policy | 无（与 WP-03-T01/WP-05-T01 平行） | units/policy.md §3.4 | `ird/policy/CMakeLists.txt`（新）；STATIC（链 core＋sdurw_math/kinematics/models/proximity） | NFR-MNT-01/07、SA-10 | 双模式构建零错误；红线扫描零命中；R-5 扩展建议提交 WP-01（门禁于本任务前生效） | proximity 直链唯一产品例外（R-5） | S |
| WP-07-T02 | 实现错误与策略类型〔≙POL-T02〕 | policy | WP-07-T01 | policy.md §3.1/§4 | `Errors.*`、`PolicySet.hpp`（四态承载） | ARC-05、NFR-COR-03 | 字段约束与不可变性用例通过 | 阈值无冻结默认者以 nullopt 显式不适用（D-5） | M |
| WP-07-T03 | 实现策略编码与内容身份〔≙POL-T03〕 | policy | WP-07-T02 | policy.md §5.3 | `PolicyInput.*`（PolicyCodec/contentIdentity） | CON-05/06 | POL-ID-1~5：canonical 往返＋位模式＋排序无关性用例通过 | 近似相等禁入身份 | M |
| WP-07-T04 | 实现解析器与校验器〔≙POL-T04〕 | policy | WP-07-T03 | policy.md §5.1/§5.2/§9.2 | `PolicyParsing.*`（resolvePolicy/校验器） | ARC-05、ERR-01 | POL-PARSE-1~6 错误分类表全量反例通过（不短路） | — | M |
| WP-07-T05 | 实现策略端口与上下文〔≙POL-T05〕 | policy | WP-07-T04 | policy.md §3.3/§9.1/§9.7（④端口所有者） | `Contexts.hpp`、`PolicyPort.*`（注入＋记忆化） | ARC-05、UX-08、KIN-13（不得覆盖） | 注入接口与端口契约用例通过；缓存一致（POL-ID-1） | — | M |
| WP-07-T06 | 实现碰撞场景与会话〔≙POL-T06〕 | policy | WP-07-T02/T05 | policy.md §6.1/§6.4 | `CollisionEvaluator` 之场景/会话/作用域展开/检测器初始化 | MDL-04/15（碰撞配置/环境参与） | POL-SCOPE-1/2 作用域矩阵展开与冲突拒绝（排除∩必检=∅）用例通过 | — | M |
| WP-07-T07 | 实现碰撞评估唯一实现〔≙POL-T07〕 | policy | WP-07-T06 | policy.md §6.2/§6.3/§6.5/§6.6 | `CollisionQuery.*`＋RobWork 适配（SingleState/PathSequence/SampleSet、取消、异常） | NFR-COR-05、KIN-02/05、TRJ-04③、OPT-03、AT-19 | POL-EVAL-1~9/POL-EXC-1/POL-CONC-1 状态机/稳定排序/边界值决策表用例通过 | API 无阈值/模式参数（D-9 结构防覆盖） | L（评估核心/查询类型两提交） |
| WP-07-T08 | 实现关节限位与行程阈值评估〔≙POL-T08〕 | policy | WP-07-T04 | policy.md §7.4/§9.4；附录 D 第 11 项 | `JointLimits.*` | MDL-06④、MDL-12（工程工作范围） | POL-JNT-1/2：行程上限 4π 边界＋比较型三要素用例通过 | 检出非法为诊断级不阻断（判定权归命令处理器，D-13） | M |
| WP-07-T09 | 实现策略兼容判定〔≙POL-T09〕 | policy | WP-07-T04 | policy.md §8.1/§8.2/§9.5 | `Compatibility.*` | CON-04/05、NFR-DEP-05（后端版本） | POL-COMPAT-1/2 版本/后端/锚失配全量 reason 用例通过 | — | S |
| WP-07-T10 | 实现诊断构造〔≙POL-T10〕 | policy | WP-07-T02 | policy.md §9.6 | `Diagnostics.*`（建议码表＋IPolicyDiagnostics） | ERR-01、UX-03 | checkDiagnosticRecord 族（testkit）码表与不变量用例通过 | 码值权威归 diagnostics 注册表 | S |
| WP-07-T11 | 替身与契约套件〔≙POL-T11〕 | policy | WP-07-T05~T10、WP-02 可用 | policy.md §11；testkit §10 | `policy/test/testdouble/`＋POL-\* 用例体＋契约夹具 | ARC-05、AT-01/19/37 | §11 矩阵逐条通过并留痕；POL-TD-1 边界声明 | 替身不冒充碰撞算法验证 | M |
| WP-07-T12 | 文档与门禁同步〔≙POL-T12〕 | policy | WP-07-T01~T11 | policy.md 全文 | README 核对（§9→§12）；§15.3 状态更新（P-POL-1 随 runtime 卡已产出可消账） | NFR-MNT-05 | 零偏差登记；待裁决项最新 | — | S |

### 2.9 WP-08 execution（单元卡已产出 v0.1）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-08-T01 | 编写 execution 单元任务卡 | execution（文档） | WP-05/WP-04/WP-09 三卡（evidence/project 卡已存在；WP-09-T01） | ARCH §4（§4.1~§4.6）、§7.2⑤；REQUIREMENTS §7；evidence.md §13（execution 行）、project.md §13.2（execution 行，P-PR-4 在此冻结） | 新建 `doc/industrial-robot-design/units/execution.md`（≥v0.1） | TASK-01~03、CON-04、NFR-PERF-02/04、NFR-REL-02/03、PM-13 | 卡含：九态状态机逐转移矩阵＋强制终止矩阵（ARCH §11.2-6）、RunRegistry 登记项、通道 DTO、TaskState→Outcome 映射、公共头清单与 §12 任务拆分；自审记录 | 不改架构语义；P-PR-4 线程模型按 project §5.6/§9.8 冻结 | L（文档，按章节分会话） |
| WP-08-T02 | 构建落位：execution 占位转真实库 | execution | WP-08-T01、WP-03-T01 | execution.md §3（卡） | `ird/execution/CMakeLists.txt`（新）；STATIC；注册 `_test`/`_contract_test` | NFR-MNT-01 | 双模式构建零错误；红线扫描零命中 | 零 Qt | S |
| WP-08-T03 | 实现任务状态机与能力声明 | execution | WP-08-T02、WP-03-T06/T08 | execution.md 状态机章（卡） | 状态机实现＋能力声明注册 | TASK-01、NFR-REL-03 | 逐转移矩阵＋强制终止矩阵用例通过；Queued/Preparing/Running/Paused 四态取消出口覆盖（ARCH §11.2-6） | 不支持暂停的任务收到暂停须显式反馈 | M |
| WP-08-T04 | 实现取消与终止协议 | execution | WP-08-T03 | execution.md（卡）；ARCH §4.4 | 取消信号/协作窗/强杀 | NFR-PERF-02、UX-03（正常取消非错误） | 2 s 入 Canceling、10 s 收敛、超时终止保检查点用例通过 | 正常取消不产生错误诊断 | M |
| WP-08-T05 | 实现 RunRegistry 迟到结果两段式接纳 | execution | WP-08-T03、WP-05-T07、WP-04-T14 | ARCH §4.5（A1/A8）；evidence.md §8.1 | RunRegistry＋接纳/归档路径 | TASK-03、PM-13、CON-02/05、AT-10 | 未知 runId/任一字段不符/陈旧 attempt 拒绝；归档位置取登记记录；同 attempt 重投递幂等（ARCH §11.2-7 全表） | 不得按当前会话丢弃合法迟到结果 | M |
| WP-08-T06 | 实现调度线程与事件/进度分发 | execution | WP-08-T03、WP-03-T08 | execution.md（卡）；ARCH §4.2/§7.2⑤ | 调度器＋⑤端口分发 | TASK-01/03、UX-10（进度阶段） | 事件 FIFO 与投递线程契约用例通过 | UI 线程不做计算 | M |
| WP-08-T07 | 实现 WorkerLauncher 工作进程池与通道 | execution | WP-08-T05、WP-06-T09（快照物化） | execution.md（卡）；ARCH §4.1 | `sdurws_ird_execution_worker`＋请求/结果流/取消/心跳通道 | NFR-REL-02、NFR-PERF-03 | 工作进程崩溃只失败当前任务；通道消息全携五元组；流式分批回传用例通过 | — | L（进程管理/通道序列化两提交） |
| WP-08-T08 | 实现 ResourceController 基础节流 | execution | WP-08-T07 | execution.md（卡）；ARCH §4.6 | 内存汇总＋节流＋资源不足诊断 | NFR-PERF-04（基础形态；规模化归 WP-23-T06） | 接近 70% 先节流后诊断用例通过 | — | S |
| WP-08-T09 | 实现缓存与检查点存储治理 | execution | WP-08-T05、WP-05-T09、WP-06-T10 | execution.md（卡）；evidence §8.2、runtime §9.4 | 缓存/检查点存储（消费兼容判定） | CON-04、NFR-PERF-04 | 部分失败结果不作正式缓存命中；检查点兼容契约用例通过 | 判定逻辑归 evidence/runtime（不复制） | M |
| WP-08-T10 | 契约测试套件 | execution | WP-08-T02~T09、WP-02-T11（按需） | execution.md 验证章（卡） | `execution/test/*`、`contract_test/*` | TASK-01~03、AT-10/11/13（载体） | 全部用例通过并留痕 | — | M |

### 2.10 WP-09 diagnostics（单元卡已产出 v0.1）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-09-T01 | 编写 diagnostics 单元任务卡 | diagnostics（文档） | 无未满足前置（core 卡已存在） | ARCH §7.8/§3.5（diagnostics→core）；core.md §4.8/§10.3（diagnostics 行）；project.md §5.0/§13.2（P-PR-6 收编）；evidence.md §13（建议码）、runtime.md §13.2、policy.md §9.6 | 新建 `doc/industrial-robot-design/units/diagnostics.md`（≥v0.1） | ERR-01、UX-03（主）、NFR-SEC-07、NFR-REL-05、NFR-MNT-03 | 卡含：StableCodeRegistry 码值分配权威（收编 PRJ-\*/RT-\*/POL-\*/EVI-\* 建议清单）、两级日志接口（P-PR-6 消账）、脱敏、诊断目录、公共头与任务拆分 | 不裁决码值外的需求语义 | L（文档） |
| WP-09-T02 | 构建落位：diagnostics 占位转真实库 | diagnostics | WP-09-T01、WP-03-T01 | diagnostics.md §3（卡） | `ird/diagnostics/CMakeLists.txt`（新）；STATIC（链 core） | NFR-MNT-01 | 双模式构建零错误；红线扫描零命中 | 零 Qt | S |
| WP-09-T03 | 实现 StableCodeRegistry 稳定诊断码注册表 | diagnostics | WP-09-T02 | diagnostics.md（卡） | `StableCodeRegistry.*`＋码值清单 | ERR-01、NFR-MNT-03（文案单一权威） | 码值唯一性/冲突拒绝用例；各卡建议码全部收编登记 | 码值不重排已登记建议 | M |
| WP-09-T04 | 实现诊断目录与两级日志 | diagnostics | WP-09-T03 | diagnostics.md（卡） | 诊断目录＋用户/开发两级日志 | ERR-01、UX-03、NFR-REL-05 | 用户诊断无调用栈/内部哈希；比较型三要素呈现数据完备用例通过 | — | M |
| WP-09-T05 | 实现脱敏与崩溃诊断文件 | diagnostics | WP-09-T04 | diagnostics.md（卡） | 脱敏过滤器＋异常诊断文件写出 | NFR-SEC-07、PM-17（与 WP-24-T02 协作）、AT-11 | 凭据类不记录、路径按配置脱敏用例通过 | — | S |
| WP-09-T06 | 契约测试套件 | diagnostics | WP-09-T03~T05、WP-02 | diagnostics.md 验证章（卡） | `diagnostics/test/*` | ERR-01 | checkDiagnosticRecord 族字段完整性反例通过并留痕 | — | S |

### 2.11 WP-10 ui（单元卡已产出 v0.1；L3 界面支撑——Widgets 唯一例外 ARCH §3.2）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-10-T01 | 编写 ui 单元任务卡 | ui（文档） | WP-09-T01（状态词/呈现契约）、core 卡 | ARCH §7.11（SA-16）/§7.7；REQUIREMENTS §18；evidence.md §13（ui/workflow 行，P-EV-4 呈现口径）；project.md §13.2（ui 行，P-PR-7 ICommandInteraction 起点） | 新建 `doc/industrial-robot-design/units/ui.md`（≥v0.1） | UX-01~14（主）、PM-11/15 | 卡含：五区布局与停靠、UX-10 七态投影×core 九态映射、CommandRegistry/HotkeyBindingTable（SA-16）、DraftController、策略编辑命令适配器宿主（P-POL-9 定稿承接：归 ui）、三维视图交互清单、任务拆分 | 计算逻辑零入 ui；插件不得私占全局快捷键 | L（文档） |
| WP-10-T02 | 构建落位：ui 占位转真实库 | ui | WP-10-T01、WP-03-T01 | ui.md §3（卡） | `ird/ui/CMakeLists.txt`（新）；STATIC/实际类型（链 core＋diagnostics＋Qt；R-3 唯一例外登记） | NFR-MNT-01（ui 例外登记） | 双模式构建零错误；R-3 例外行入登记册 | 例外范围仅 ui 界面目标 | S |
| WP-10-T03 | 实现工作台壳与五区布局 | ui | WP-10-T02 | ui.md（卡） | 壳＋五区（顶/左/中/右/底）＋停靠/隐藏/恢复默认＋跨会话记忆 | UX-09、PM-14（布局记忆用户级） | 布局恢复默认与跨会话记忆用例通过 | — | M |
| WP-10-T04 | 实现七态公共状态呈现 | ui | WP-10-T03、WP-05-T08、WP-08-T06 | ui.md（卡）；evidence P-EV-4 口径 | 公共状态组件＋状态词/图例 | UX-06/10、PM-11（结果过期） | 未完成缺项列表/计算中进度阶段与取消/过期原因/失败定位与修复建议呈现用例通过 | 不新增状态词（七态投影） | M |
| WP-10-T05 | 实现三维视图交互 | ui | WP-10-T03 | ui.md（卡） | 三维视图（标准/相机视图、缩放、透视正交、线框透明、渲染分组显隐、碰撞高亮与组掩码、PNG 截图、ray-cast 拾取） | UX-11、KIN-07（协作）、AT-23（拾取载体） | 视图操作与拾取用例通过（RWStudioView3D 框架承载） | — | L（基础视图/交互拾取两提交） |
| WP-10-T06 | 实现 CommandRegistry＋HotkeyBindingTable＋命令面板 | ui | WP-10-T03 | ui.md（卡）；ARCH §7.11 | 命令注册表＋快捷键唯一注册点＋面板（模糊搜索/键盘导航） | UX-13（命令面板契约）、PM-14（绑定用户级）、SA-16 | 重复绑定注册边界拒绝并出比较型诊断（ARCH §11.2-1）；未绑定命令经面板可达用例通过 | 插件作用域快捷键不得注册为全局 | M |
| WP-10-T07 | 实现统一工程策略入口界面与策略编辑适配器 | ui | WP-10-T03、WP-07-T05、WP-04-T10/T11 | ui.md（卡）；policy.md §10.6、P-POL-9 定稿（适配器宿主＝ui） | 策略摘要＋跳转＋开关分组异名＋策略编辑命令适配器（L5 装配注入 policy 纯函数） | UX-08、ARC-05（策略权威呈现） | 摘要只读、计算开关与显示开关分组异名并说明影响范围用例通过；策略编辑经①命令端口提交 | ui 不持有判定权与计算开关权威 | M |
| WP-10-T08 | 实现参数表与表单公共件 | ui | WP-10-T03、WP-03-T04 | ui.md（卡） | 批量粘贴/筛选/单位显示/错误定位参数表＋表单确认规则＋高级面板 | UX-04/05/07、KIN-12（单位显示消费） | 数值＋单位同显、非法输入就地显示原因保留原值、取消恢复、批量影响明细用例通过 | 不用模态对话框做大量重复编辑 | M |
| WP-10-T09 | 实现阶段状态投影与工程用语 | ui | WP-10-T04、WP-22-T03（门控数据源） | ui.md（卡）；ARCH §3.4（StageStatusModel 汇聚） | StageStatusModel＋统一用语呈现 | UX-01/02/12（投影侧） | 七阶段就绪/锁定投影与级联失效提示数据用例通过；界面零哈希/Schema/插件名 | 投影只消费，不拥有门控规则（归 workflow） | M |
| WP-10-T10 | 实现帮助入口与关于对话框 | ui | WP-10-T03、WP-24-T01 | ui.md（卡） | 帮助入口＋About（产品/组件版本＋插件清单） | UX-14、NFR-DEP-05（与基线一致） | 版本显示与冻结基线一致；插件清单与白名单一致 | — | S |
| WP-10-T11 | ui 契约测试套件 | ui | WP-10-T03~T10、WP-02（testkit_qt 按需） | ui.md 验证章（卡） | `ui/test/*` | UX 家族、SA-16 | 快捷键冲突拒绝/状态词映射/布局记忆用例通过并留痕 | — | M |

### 2.12 WP-11 io（单元卡已产出 v0.1）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-11-T01 | 编写 io 单元任务卡 | io（文档） | WP-09-T01（诊断契约） | ARCH §7.9（SA-14）/§6.6；REQUIREMENTS §19.5；runtime.md §13.2（io 行，P-RT-6 ResourceBytes 生命周期）；project.md §13.2（io 行，P-PR-5 注入形态起点） | 新建 `doc/industrial-robot-design/units/io.md`（≥v0.1） | NFR-SEC-01~03、NFR-REL-04、REQ-05/12（支撑）、SEL-01/02（支撑）、PM-05（支撑） | 卡含：SafePath/BudgetGuard（P-AR-1 结论：归 io）、CSV 方言转义 roundtrip、JSON、资源导入与 Missing/Changed 检测、ZIP 编解码（P-PR-5 裁决后）、任务拆分 | 防护设施不得分散到各导入通道 | L（文档） |
| WP-11-T02 | 构建落位：io 占位转真实库 | io | WP-11-T01、WP-03-T01 | io.md §3（卡） | `ird/io/CMakeLists.txt`（新）；STATIC（链 core＋diagnostics） | NFR-MNT-01 | 双模式构建零错误；红线扫描零命中 | 零 Widgets | S |
| WP-11-T03 | 实现 SafePath 与 BudgetGuard | io | WP-11-T02 | io.md（卡）；ARCH §7.9 | `SafePath.*`＋`BudgetGuard.*` | NFR-SEC-01/02、PM-05（解包防护） | 路径穿越反例（项目资源区逃逸拒绝）；单文件/递归深度/解压总量超限拒绝＋诊断用例通过 | 例外仅 PM-01 一次性读取与外部引用记录 | M |
| WP-11-T04 | 实现 CSV 读写器（方言标识＋可逆编码） | io | WP-11-T02、WP-03-T04 | io.md（卡）；NFR-SEC-03 全文（M-15＋R5） | `Csv.*`（唯一转义/还原实现） | NFR-SEC-03、REQ-05、AT-02（roundtrip） | 导出自带前缀（=、+、-、@、'）roundtrip 逐字符一致；无标识外部 CSV 原样读入；转义形式不入结构化层用例通过 | 数据-only 解析（不执行公式/命令） | M |
| WP-11-T05 | 实现 JSON 读写器与 ZIP 编解码 | io | WP-11-T02 | io.md（卡） | `Json.*`（与 CSV 同字段字典）＋ZIP 通道（P-PR-5 形态） | REQ-12、OPT-12（JSON 工件）、PM-05（ZIP） | JSON roundtrip；ZIP 解包逐字节还原＋哈希校验用例通过 | JsonLite 不用于产品格式（测试侧边界） | M |
| WP-11-T06 | 实现资源导入服务与外部源检测 | io | WP-11-T03 | io.md（卡）；ARCH §6.6（三段边界 R4） | `ResourceImport.*`＋Missing/Changed 检测 | NFR-REL-04、PM-01（外部引用记录）、CON-03（固化协作） | 外部源缺失/变化检测用例；三段边界（一次性读取/引用记录/转正式固化）用例通过 | — | M |
| WP-11-T07 | io 契约测试套件 | io | WP-11-T03~T06、WP-02 | io.md 验证章（卡） | `io/test/*` | NFR-SEC-01~03、AT-02/20（支撑） | 全部用例通过并留痕 | — | S |

### 2.13 WP-12 reporting（单元卡已产出 v0.1；B 级先行、C 级补全）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-12-T01 | 编写 reporting 单元任务卡 | reporting（文档） | WP-05/WP-04 卡（已存在） | ARCH §3.1 reporting 行；REQUIREMENTS §16；evidence.md §13（reporting 行：FormalPass 资格、envelope→snapshot 追溯链、P-EV-5 参考值限定语）；project.md §13.2（reports/ 写入端口）；附录 B PDF 行（是否预留 PDF 接口为下游设计决策——本卡裁决并登记） | 新建 `doc/industrial-robot-design/units/reporting.md`（≥v0.1） | RPT-01~06、NFR-COR-04 | 卡含：ReviewReport B/C 两级章节模型、渲染单一数据源（多格式一致依据）、证据包要素、往返复算、缺正式结果章节默认不选、任务拆分 | 不弱化 RPT-05 冻结语 | L（文档） |
| WP-12-T02 | 构建落位：reporting 占位转真实库 | reporting | WP-12-T01、WP-03-T01 | reporting.md §3（卡） | `ird/reporting/CMakeLists.txt`（新）；STATIC（链 core＋evidence＋diagnostics＋project） | NFR-MNT-01 | 双模式构建零错误；红线扫描零命中 | 零 Widgets | S |
| WP-12-T03 | 实现 ReviewReport 对象模型（B 级） | reporting | WP-12-T02、WP-05-T06、WP-04-T09 | reporting.md（卡） | `ReviewReport.*`（B 级章节＋只读明确 ID 修订/结果＋目录幂等追加/冲突拒绝） | RPT-01-B、NFR-COR-04、AT-32（B 部分） | 只读明确 ID；追加幂等/冲突拒绝用例通过；缺正式结果章节默认不选并显示缺项 | — | M |
| WP-12-T04 | 实现 HTML 渲染与多格式一致 | reporting | WP-12-T03、WP-11-T04 | reporting.md（卡） | 渲染器（HTML＋JSON/CSV 附带） | RPT-02、AT-22 | 多格式逐字段一致用例通过；导出失败保留选择与路径可重试 | 不设 PDF 需求条目（附录 B 裁决） | M |
| WP-12-T05 | 实现证据包导出 | reporting | WP-12-T03 | reporting.md（卡）；evidence §4.1.2 复现块 | `EvidenceBundle.*` | RPT-03、NFR-COR-04、AT-14 | 输入快照＋结果引用＋复现要素（版本/种子/线程/容差）打包用例通过 | — | M |
| WP-12-T06 | 实现措辞冻结与两类声明渲染 | reporting | WP-12-T04 | reporting.md（卡）；§8.1 正式通过判定 | 渲染规则（限定语保留/两类声明） | RPT-05、EVI-01（消费）、AT-22 | 估算值/数据不足/外部验证未完成限定语保留；不可行结论可作正式评审记录（与正式通过两声明）用例通过 | 不得渲染未经判定的"正式通过" | S |
| WP-12-T07 | 实现 RPT-01-C 扩展与方案变体报告（阶段 C） | reporting | WP-12-T03~T06、阶段 C 各域结果对象就绪 | reporting.md（卡） | C 级追加章节（轨迹/节拍、动力学曲线与峰值/RMS、传动工作点、选型/BOM 与淘汰依据）＋签署元数据＋改型差异 | RPT-01-C、RPT-04、EVI-02（一致基准）、AT-32 | 完整工程案例逐章追溯到结果/工况/快照；必验工况覆盖核对入章节完整性用例通过 | 变体报告基准不一致即拒绝 | L（C 级章节/变体对比两提交） |
| WP-12-T08 | 实现报告往返复算 | reporting | WP-12-T07 | reporting.md（卡） | roundtrip 复算比对 | RPT-06、AT-22、NFR-COR-04 | 从报告数据源复算一致用例通过；不一致时报告不可用于正式结论 | — | M |
| WP-12-T09 | 契约测试套件 | reporting | WP-12-T03~T08、WP-02 | reporting.md 验证章（卡） | `reporting/test/*`、`contract_test/*` | AT-14/22/32 | 全部用例通过并留痕 | — | M |

### 2.14 WP-13 modeling（单元卡待产出；MDL-01~22）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-13-T01 | 编写 modeling 单元任务卡 | modeling（文档） | WP-06/WP-07/WP-04/WP-11/WP-09 各卡（runtime 卡已存在——P-RT-4/5 交叉核对冻结） | ARCH §7.3；REQUIREMENTS §9、§2.1；runtime.md §13.2（modeling 行）；policy.md §13（project 行行程上限协作） | 新建 `doc/industrial-robot-design/units/modeling.md`（≥v0.1） | MDL-01~22、§2.1 支持矩阵 | 卡含：RobotDesign 领域对象与双权威 schema、URDF/Xacro 导入边界、断言分域、DH 五状态、模板与 P-03 七轴数值登记、工具/场景/安装姿态、插件界面、任务拆分 | 不放松 URDF/Xacro 解析安全（MDL-19） | L（文档） |
| WP-13-T02 | 构建落位：modeling 占位转真实库＋插件目标 | modeling | WP-13-T01、WP-03-T01 | modeling.md §3（卡） | `ird/modeling/CMakeLists.txt`（新）；计算库 STATIC（零 Qt）＋`_plugin`/`_test`/`_contract_test` 注册 | NFR-MNT-01、ARCH §3.3（二分结构） | 双模式构建零错误；计算库零 Qt、插件零计算逻辑扫描通过 | — | S |
| WP-13-T03 | 实现 RobotDesign 领域对象与双权威参数化 | modeling | WP-13-T02 | modeling.md（卡） | `RobotDesign.*`（StandardDH/显式 Joint 权威互斥与切换规则；Axis 一等字段） | MDL-01/02/09 | 权威互斥/派生只读/切换规则用例通过；Axis 编辑权限随权威模式 | 权威模式归属按 MDL-02（S-R-S 锁定显式） | M |
| WP-13-T04 | 实现物性估算与动力学参数层 | modeling | WP-13-T03 | modeling.md（卡） | 几何物性估算（唯一公式表/质心惯量基准/平行轴确认）＋关节力限/摩擦参数层（来源标记） | MDL-05、MDL-16、DYN-06（降级闭环） | 估算对照解析算例；质心修改平行轴确认/强制覆盖完整张量用例通过；缺失标记 DataInsufficient | 不静默脱钩质心与惯量基准 | M |
| WP-13-T05 | 实现 URDF 导入识别与链型判定 | modeling | WP-13-T03、WP-11-T03 | modeling.md（卡）；§2.1 两维度 | `UrdfImport.*`（字段映射/默认补全/忽略与不支持报告）＋链型判定（分支报告＋显式选链、continuous 工程工作范围、mimic/planar/floating 阻断） | MDL-03/11/12、§2.1、AT-15/17 | 默认补全/待确认/忽略/不支持全程可观察；零轴仅报告不提交修订；prismatic R1 阻断＋诊断；不得静默降级关节类型 | 未经用户显式选择不排除可动分支 | L（解析映射/链型判定两提交） |
| WP-13-T06 | 实现 Xacro 受控预处理 | modeling | WP-13-T05、WP-11-T03 | modeling.md（卡） | `XacroExpand.*`（参数列表/展开环境/离线依赖） | MDL-19、AT-31 | 展开失败/依赖缺失诊断可定位；展开结果进 URDF 相同安全边界；来源记录用例通过 | — | M |
| WP-13-T07 | 实现模板创建与关节/几何编辑 | modeling | WP-13-T03 | modeling.md（卡） | 六轴模板（＋七轴模板——**P-03 数值冻结前仅登记不启用**）＋关节逐轴编辑＋网格/基座/法兰/默认 TCP/Home/Zero/自碰撞配置 | MDL-01/04 | 六轴模板创建→编辑→应用链路用例通过；4/5 轴与含 prismatic 创建入口阻止＋提示（§2.1） | 不使用未冻结的 P-03 数值 | M |
| WP-13-T08 | 实现物理合法性断言与建模命令处理器 | modeling | WP-13-T04、WP-04-T10/T11、WP-06-T11、WP-07-T08 | modeling.md（卡）；ARCH §7.1（SA-15） | 断言分域①~③④（物性硬断言/限位区间/行程上限策略校验）＋ApplyRobotDesign 处理器＋双编译触发 | MDL-06、MDL-12（工作范围有限性）、AT-01 | 断言分域反例（continuous 豁免/物性缺失走 DataInsufficient/超 4π 经确认放行）用例通过；双编译失败零修订 | 硬断言就地阻止＋精确定位；确认留痕入命令摘要 | M |
| WP-13-T09 | 实现 DH↔显式转换五状态 | modeling | WP-13-T03、WP-06-T04/T07 | modeling.md（卡）；附录 D 第 5 项（C3 逐关节逐项上界） | `DhConvert.*`（结构适用性检查先行＋数值求解＋等价验证） | MDL-10、MDL-02（切换条件）、AT-16 | 五状态各含样例；NotExpressible 终判不进求解；Exact 判定按逐关节上界；仅 Exact/ExactNonUnique 经等价验证可切权威 | Approximate 不得成为权威 DH | M |
| WP-13-T10 | 实现工具/场景固定帧/命名位姿编辑 | modeling | WP-13-T03 | modeling.md（卡） | `ToolDefinition.*`（法兰/TCP/几何/物性，引用不复制）＋环境几何（显式引用）＋命名位姿 | MDL-13/15/17、AT-28 | 工具引用使用；环境对象参与碰撞（经策略）；位姿不改权威模型用例通过 | — | M |
| WP-13-T11 | 实现基座安装姿态编辑 | modeling | WP-13-T03、WP-06-T06 | modeling.md（卡）；runtime.md §6（P-RT-4 预设轴向在此对齐冻结） | 安装姿态编辑（三预设＋任意欧拉角、原子持久化、默认地面） | MDL-22、AT-37 | 三预设＋任意角编辑原子持久化；未配置默认地面；编辑值编译进 R_world_base 单一字段用例通过 | 不在基座系参数化 g | S |
| WP-13-T12 | 实现编译协作与 reader（建模侧） | modeling | WP-13-T03、WP-06-T05/T07/T11 | modeling.md（卡）；runtime.md §4.2（IRobotDesignReader，P-RT-5 注入形态） | `RobotDesignReader.*`（RobotDesign→Description 映射与 SI 化）＋IModelCompilePort 适配协作 | MDL-14、MDL-06（编译触发）、ARC-03 | reader 映射往返一致；编译诊断定位到对象/字段用例通过 | 名称生成归 runtime（R-4） | M |
| WP-13-T13 | 实现规范模型导出/导入 roundtrip | modeling | WP-13-T03、WP-11 | modeling.md（卡） | 规范模型包导出/导入（自有工件保真回读）＋WorkCell/DWC XML 外供导出 | MDL-20、AT-28 | roundtrip 逐项一致（权威参数化/物性/资源引用/碰撞规则/命名位姿）；导入仅识别本软件工件；导出失败项目状态不变 | 不与 MDL-18 通道混淆 | M |
| WP-13-T14 | 实现 Model Diff 数据实体 | modeling | WP-13-T03 | modeling.md（卡） | `ModelDiff.*`（结构/参数（DH、轴线、限位）与物性差异增量表） | MDL-08、AT-12（数据侧） | 两 RobotDesign 差异逐项输出、不覆盖基线；供 UX-13 呈现（呈现归 WP-22-T11） | 只拥有数据实体与语义（M-5 分工） | S |
| WP-13-T15 | 实现 modeling 插件界面 | modeling | WP-13-T03~T11、WP-10-T08 | modeling.md（卡） | 关节树/参数表/三维选择联动（选中只显示相关属性） | MDL-07 | 联动选择与属性过滤用例通过 | 插件零计算逻辑 | M |
| WP-13-T16 | 契约测试与三套黄金模型数据集 | modeling | WP-13-T05~T14、WP-02 | modeling.md 验证章（卡） | `modeling/test/*`＋`testdata/golden/mdl-*`（DH/显式/不可表达三套） | AT-01/15/16/17/28/31/37（建模侧） | 三套黄金模型等价测试通过并留痕 | — | L（按 AT 分组提交） |
| WP-13-T17 | 实现 WorkCell 反向导入通道（R2/D） | modeling | WP-13-T05、WP-11-T06、阶段 D 启用 | modeling.md §13（卡预留） | `WorkCellReverseImport.*`（有损提取＋提取报告） | MDL-18、AT-33 | 不可表达/未保留内容逐项列出；导入为草稿；不可表达按 MDL-10/12 同一判定用例通过 | 草稿不直接形成正式模型 | M |
| WP-13-T18 | 实现传动耦合矩阵建模（R2/D） | modeling | WP-13-T03、WP-18-T05、阶段 D 启用 | modeling.md（卡）；MDL-21 | 腕部线性耦合矩阵 C 编辑＋R1 阻断反例 | MDL-21、AT-38 | C 常矩阵校验/非常矩阵病态诊断阻止；R1 阻断口径（mimic/闭环维持阻断）用例通过 | 不提前放开 R1 阻断 | M |
| WP-13-T19 | 实现 MDL-12-S1 混合链启用（R2；前置 SEL-09-S1） | modeling（跨域协作） | WP-19-T12＋WP-15/16/17/19 对应扩展 | modeling.md（卡）；§2.1 交付同步 | 六/七轴含 prismatic 正式计算/报告放开 | MDL-12-S1、AT-36 | 混合链端到端（建模→…→报告）验收；4/5 轴反例仍被阻止；§2.1 支持矩阵同步更新 | 4/5 轴不因本子项放开 | L（跨域端到端） |

### 2.15 WP-14 requirements（单元卡待产出；REQ-01~12）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-14-T01 | 编写 requirements 单元任务卡 | requirements（文档） | WP-11/WP-05/WP-04/WP-10 各卡（P-EV-9 必验工况 schema 在此冻结） | ARCH §3.1 requirements 行；REQUIREMENTS §10；evidence.md §4.1.3/§13（P-EV-9） | 新建 `doc/industrial-robot-design/units/requirements.md`（≥v0.1） | REQ-01~12 | 卡含：任务点/区域/工况对象 schema（enabled/mandatory 字段冻结并回接 evidence §4.1.3）、CSV/JSON 字段字典、姿态规则五种、镜像阵列、就绪校验、任务拆分 | 工况 schema 与 evidence 承载一致（不双账本） | L（文档） |
| WP-14-T02 | 构建落位：requirements 占位转真实库＋插件目标 | requirements | WP-14-T01、WP-03-T01 | requirements.md §3（卡） | `ird/requirements/CMakeLists.txt`（新）；计算库＋`_plugin` | NFR-MNT-01、ARCH §3.3 | 双模式构建零错误；二分结构扫描通过 | — | S |
| WP-14-T03 | 实现任务点/区域/工况领域对象 | requirements | WP-14-T02 | requirements.md（卡） | 任务点（位姿分量约束/容差/Must-Should）＋三段（接近/作业/撤离）＋Box 区域采样定义＋负载工况/碰撞要求/目标节拍 | REQ-01~04 | 领域对象校验与 canonical 编码登记用例通过（供 evidence 切片） | 采样计划确定性（KIN-04 冻结前提） | M |
| WP-14-T04 | 实现 CSV/JSON 需求导入 | requirements | WP-14-T03、WP-11-T04/T05 | requirements.md（卡） | CSV 坐标表导入（字段映射/单位预览/逐行错误）＋JSON 导入导出（副本不影响项目） | REQ-05/12、NFR-SEC-03、AT-02 | 错误行保留正确行、错误定位到列与原文；JSON roundtrip 副本语义用例通过 | CSV 按数据解析不执行公式 | M |
| WP-14-T05 | 实现就绪校验与预览/正式分离 | requirements | WP-14-T03、WP-05-T06 | requirements.md（卡）；§8.1 汇总①⑤级 | 就绪校验（ReadinessSummary 数据源；Must 非法→输入未完成；预览只查有效条目） | REQ-06、EVI-01（模式） | 预览不产生正式证据与结果对象；Must/Should 分级判定用例通过 | — | M |
| WP-14-T06 | 实现姿态规则与三维拾取/捕获 | requirements | WP-14-T03、WP-10-T05 | requirements.md（卡） | 五种姿态规则解析（来源记录/失败可定位）＋几何特征拾取＋TCP 位姿捕获入草稿 | REQ-08/09/10、AT-23 | 规则解析成功/失败均可定位；写回前必须确认；未应用不失效用例通过 | 三维编辑事务不绕过确认 | M |
| WP-14-T07 | 实现工艺模板/镜像/阵列/需求集撤销 | requirements | WP-14-T03 | requirements.md（卡） | 工艺任务模板＋工位镜像＋四类阵列＋需求集撤销/重做（批量整体回滚） | REQ-07/11、AT-24 | 镜像坐标正确、四类阵列生成、批量整体回滚用例通过；局部撤销与项目级撤销分离 | — | M |
| WP-14-T08 | 实现 requirements 插件界面 | requirements | WP-14-T03~T07、WP-10-T08 | requirements.md（卡） | 定义面板（工位/区域/校验） | UX-05（参数表消费）、REQ 家族呈现 | 编辑→草稿→应用链路用例通过 | 插件零计算逻辑 | M |
| WP-14-T09 | 契约测试套件 | requirements | WP-14-T03~T08、WP-02 | requirements.md 验证章（卡） | `requirements/test/*`＋黄金数据集（搬运任务/部分位姿约束/区域/负载事件） | AT-02/23/24 | 全部用例通过并留痕 | — | M |

### 2.16 WP-15 kinematics（单元卡待产出；KIN-01~14）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-15-T01 | 编写 kinematics 单元任务卡 | kinematics（文档） | WP-06/WP-07/WP-05/WP-08 各卡（均已存在或先行） | ARCH §3.1 kinematics 行；REQUIREMENTS §11、附录 D 第 1~3 项；runtime.md §13.2（kinematics 行） | 新建 `doc/industrial-robot-design/units/kinematics.md`（≥v0.1） | KIN-01~14、AT-03/04/27 | 卡含：FK/IK 评估器与依赖声明、去重口径（附录 D 第 3 项适配器）、覆盖率算法（R8）、AnalysisConfiguration schema 与 canonical 编码、插件界面、任务拆分 | 碰撞启用状态只读引用策略（KIN-13） | L（文档） |
| WP-15-T02 | 构建落位：kinematics 占位转真实库＋插件目标 | kinematics | WP-15-T01、WP-03-T01 | kinematics.md §3（卡） | `ird/kinematics/CMakeLists.txt`（新）；计算库＋`_plugin` | NFR-MNT-01、ARCH §3.3 | 双模式构建零错误；二分结构扫描通过 | — | S |
| WP-15-T03 | 实现 FK 与指标评估器 | kinematics | WP-15-T02、WP-06-T09（DeviceView） | kinematics.md（卡） | FK/TCP/Jacobian/奇异值/条件数/可操作度/关节裕量评估器（统一尺度规则） | KIN-01、NFR-COR-01 | 解析对照黄金算例（附录 D 第 4/9 项容差）通过 | — | M |
| WP-15-T04 | 实现多初值 IK 与去重排序 | kinematics | WP-15-T03、WP-07-T07（碰撞硬过滤） | kinematics.md（卡）；附录 D 第 1~3 项 | IK 评估器（硬过滤→关节空间逐轴去重→裕量/可操作度/距离/稳定编号排序；搜索未果记录） | KIN-02、§8.1 搜索未果口径（C5/C8）、AT-03 去重/换初值反例 | 同末端位姿不同构型均保留；全部解被过滤→DataInsufficient 附记录 | 不得输出不可行结论（搜索未果≠不可行） | L（求解/过滤去重排序两提交） |
| WP-15-T05 | 实现批量任务点验证 | kinematics | WP-15-T04、WP-08（后台运行通道） | kinematics.md（卡） | 批量验证评估器（三态报告） | KIN-03、EVI-01/02 | 可行/工程不可行/数据不足三态报告；必验工况覆盖用例通过 | — | M |
| WP-15-T06 | 实现区域覆盖率评估 | kinematics | WP-15-T04 | kinematics.md（卡）；KIN-04（R3/R8 口径） | 区域覆盖评估器（样本冻结/分母=计划总数/存在性与全局口径/零样本） | KIN-04、EVI-01（降级） | 固定计数样例（计划 100 可达 60→60%）；数据不足样本保留分母不计分子＋整体降级；零样本→DataInsufficient 用例通过 | 复评不得增删更换样本 | M |
| WP-15-T07 | 实现碰撞证据接入 | kinematics | WP-15-T04、WP-07-T07 | kinematics.md（卡） | ④端口碰撞证据消费（KIN-05 口径） | KIN-05、NFR-COR-05、AT-19 | 缺检测器→DataInsufficient 不视为无碰撞；三入口一致用例通过 | 不另存碰撞布尔开关 | S |
| WP-15-T08 | 实现会话姿态与设默认 | kinematics | WP-15-T03、WP-04-T10、WP-10 | kinematics.md（卡）；ARCH §7.7 | 会话姿态（双击任务/候选）＋设默认 TCP/设备（经命令） | KIN-06/14、AT-04/27 | 双击只改会话姿态不触发失效；设默认经命令产生新修订不破坏引用用例通过 | — | S |
| WP-15-T09 | 实现结果筛选/导出/批量复算 | kinematics | WP-15-T05 | kinematics.md（卡） | 结果筛选/最差项排序/JSON·CSV 导出/批量复算 | KIN-08、AT-04 | 导出不产生修订；筛选排序稳定用例通过 | — | M |
| WP-15-T10 | 实现显示单位与求解配置 | kinematics | WP-15-T03、WP-05-T04 | kinematics.md（卡）；KIN-13 | 显示单位（m/cm/mm、deg/rad 纯投影）＋AnalysisConfiguration（初值/迭代/容差/去重阈值/采样预算；独立持久化、入运行身份） | KIN-12/13、AT-27 | 单位切换不改 SI 真值不触发重算；配置修改按依赖提示重算；碰撞启用只读引用策略用例通过 | 配置不得覆盖策略 | M |
| WP-15-T11 | 实现失败点/薄弱区三维可视化 | kinematics | WP-15-T05、WP-10-T05 | kinematics.md（卡） | 失败点/薄弱区/碰撞对象/最差关节裕量渲染数据与入口 | KIN-07 | 渲染状态与统一状态词一致；回写仅会话姿态（KIN-06）用例通过 | — | M |
| WP-15-T12 | 实现 kinematics 插件界面 | kinematics | WP-15-T03~T11、WP-10-T08 | kinematics.md（卡） | 工作流页＋任务点表＋单位/配置对话框 | UX-04/05（高级面板/参数表消费） | 界面链路用例通过 | 插件零计算逻辑 | M |
| WP-15-T13 | 契约测试与运动学黄金数据集 | kinematics | WP-15-T03~T12、WP-02 | kinematics.md 验证章（卡） | `kinematics/test/*`＋`testdata/golden/kin-*` | AT-03（全部反例）/04/27 | 全部用例通过并留痕 | — | L（按 AT 反例分组提交） |
| WP-15-T14 | 实现工作空间采样与可视化（R2/D） | kinematics | WP-15-T06、WP-08、WP-10-T05、阶段 D 启用 | kinematics.md §13（卡预留） | 采样（随机/网格、可取消有进度）＋点云着色/投影/包络/PNG＋回写 | KIN-09、AT-25 | 采样计划确定性；着色/投影/图例/PNG；点回写仅会话姿态用例通过 | 碰撞证据经统一策略不另存布尔 | M |
| WP-15-T15 | 实现近似外包络估算（R2/D） | kinematics | WP-15-T14 | kinematics.md（卡预留） | 射线边界 Rmax（默认 180 方向、可取消） | KIN-10、AT-26 | 结果标注"近似、非精确"用例通过 | — | S |
| WP-15-T16 | 实现独立位姿可达性分析（R2/D） | kinematics | WP-15-T14 | kinematics.md（卡预留） | 位置×方向×滚转采样＋计划预览＋方向覆盖率报告 | KIN-11、AT-26 | 计划预览 IK 目标数；协作取消；覆盖率报告用例通过 | — | M |
| WP-15-T17 | 实现扩展显示单位（R2） | kinematics | WP-15-T10 | §17.1 子项 | inch/grad/turn 换算 | KIN-12-S1 | 换算正确且不影响 SI 真值 | — | S |

### 2.17 WP-16 trajectory（单元卡待产出；TRJ-01~08）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-16-T01 | 编写 trajectory 单元任务卡 | trajectory（文档） | WP-06/WP-07/WP-05/WP-08 各卡；WP-15 卡（③端口 IK 协作） | ARCH §3.1 trajectory 行；REQUIREMENTS §12；policy.md §13（trajectory 行 pathParameters 约定） | 新建 `doc/industrial-robot-design/units/trajectory.md`（≥v0.1） | TRJ-01~08、AT-06 | 卡含：PTP/笛卡尔/避障、平滑复检协议（P-06 数值消费）、时间参数化可扩展维度（TRJ-08 接口约束）、诊断定位、任务拆分 | 不建空字段/空页面/占位模块（TRJ-08） | L（文档） |
| WP-16-T02 | 冻结 P-06 轨迹碰撞复检协议数值（与需求所有者协同） | 横切（需求侧＋WP-16） | WP-16-T01（卡内登记数值需求） | REQUIREMENTS 附录 C P-06、§12 TRJ-04（R2/R9） | P-06 冻结产物（关节/笛卡尔步长默认、细分预算、代表点集规则）并入 REQUIREMENTS 附录 C 修订 | P-06（阶段 C 启用硬前置） | 数值表冻结留痕；WP-16-T07 复检实现以冻结值为准 | 本文不私设默认值（未冻结不得启用复检） | S |
| WP-16-T03 | 构建落位：trajectory 占位转真实库＋插件目标 | trajectory | WP-16-T01、WP-03-T01 | trajectory.md §3（卡） | `ird/trajectory/CMakeLists.txt`（新）；计算库＋`_plugin` | NFR-MNT-01、ARCH §3.3 | 双模式构建零错误；二分结构扫描通过 | — | S |
| WP-16-T04 | 实现关节空间 PTP 与作业序列路径 | trajectory | WP-16-T03、WP-06-T09 | trajectory.md（卡） | PTP 点到点＋任务点有序序列 | TRJ-01 | PTP 基准用例通过 | — | M |
| WP-16-T05 | 实现笛卡尔段与段内 IK 连续性检查 | trajectory | WP-16-T04、WP-15-T04（③端口） | trajectory.md（卡） | 直线接近/撤离段＋沿途 IK 连续性（适用条件=含笛卡尔段，纯关节路径标记不适用） | TRJ-02、§8.1 C2 例 | 连续性检查适用/不适用双路用例通过 | 不直链 kinematics（R-1） | M |
| WP-16-T06 | 实现避障路径搜索接入 | trajectory | WP-16-T04、WP-07-T07 | trajectory.md（卡） | RobWork 规划器调用（规划器与参数可配置） | TRJ-03 | 避障基准用例通过；候选路径碰撞→淘汰并触发重规划（C8） | — | M |
| WP-16-T07 | 实现简化平滑与碰撞复检协议 | trajectory | WP-16-T05/T06、WP-16-T02、WP-07-T07 | trajectory.md（卡）；TRJ-04（R2/R9 要素）；P-06 冻结值 | 简化/平滑＋复检（端点＋段内采样、双上界细分、预算、代表点集、段级 DataInsufficient） | TRJ-04、EVI-01、AT-06（段内碰撞/预算耗尽/重规划反例） | 段内碰撞反例被检出；预算耗尽段判 DataInsufficient 附实际步长；间距阈值消费工程策略 | 复检无私有线宽/阈值 | L（平滑/复检两提交） |
| WP-16-T08 | 实现时间参数化 | trajectory | WP-16-T07 | trajectory.md（卡）；附录 D 第 10 项 | 加速度连续运动律＋关节速度/加速度限制校验＋总节拍/分段时间 | TRJ-05、DYN 消费（节拍） | 五次样条 C² 结点匹配；限速校验容差（附录 D 第 10 项）用例通过 | — | M |
| WP-16-T09 | 实现失败段诊断定位 | trajectory | WP-16-T04~T08 | trajectory.md（卡） | 无路径/分支跳变/奇异邻域/限制超标的段落与原因 | TRJ-06、ERR-01 | 失败段定位到具体段落与原因码用例通过 | — | S |
| WP-16-T10 | 实现动画/曲线/局部重规划/导出 | trajectory | WP-16-T08、WP-10 | trajectory.md（卡） | 轨迹动画、曲线查看、路点局部重规划、标准轨迹导出 | TRJ-07、AT-04 | 动画/导出不产生修订用例通过 | — | M |
| WP-16-T11 | 保持评估接口可扩展（接口约束） | trajectory | WP-16-T08 | trajectory.md（卡）；TRJ-08（V13-02） | 评估维度扩展点（jerk/工艺速度/连续工艺预留） | TRJ-08 | 接口约束可扩展性评审通过；零空字段/空页面 | 不实现 S1~S3 用户功能（R2 分期） | S |
| WP-16-T12 | 实现 trajectory 插件界面 | trajectory | WP-16-T04~T10、WP-10-T08 | trajectory.md（卡） | 轨迹工作流页＋曲线视图入口 | UX 家族 | 界面链路用例通过 | 插件零计算逻辑 | M |
| WP-16-T13 | 契约测试与轨迹黄金数据集 | trajectory | WP-16-T04~T12、WP-02 | trajectory.md 验证章（卡） | `trajectory/test/*`＋`testdata/golden/trj-*` | AT-06（全部反例） | 全部用例通过并留痕 | — | M |
| WP-16-T14 | 实现 jerk 上限约束（R2） | trajectory | WP-16-T08/T11 | §17.1 子项 | 时间参数化纳入关节 jerk 限值 | TRJ-08-S1 | 限值满足断言＋超限段定位诊断 | — | M |
| WP-16-T15 | 实现工艺速度约束（R2） | trajectory | WP-16-T08/T11 | §17.1 子项 | 指定段 TCP 速度受限/恒定 | TRJ-08-S2 | 工艺速度约束满足断言＋超限诊断 | — | S |
| WP-16-T16 | 实现连续工艺轨迹段（R2） | trajectory | WP-16-T08/T11 | §17.1 子项 | 连续（不停驻）段表达与段间边界行为 | TRJ-08-S3 | 段间速度连续、驻留/不停驻切换明确断言 | — | M |

### 2.18 WP-17 dynamics（单元卡待产出；DYN-01~03/05~08）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-17-T01 | 编写 dynamics 单元任务卡 | dynamics（文档） | WP-06/WP-05 各卡 | ARCH §3.1 dynamics 行、§7.10；REQUIREMENTS §13；runtime.md §13.2（dynamics/drivetrain 行——rwsim 消费方式、R_world_base 重力投影） | 新建 `doc/industrial-robot-design/units/dynamics.md`（≥v0.1） | DYN-01~08（DYN-04 归 WP-18）、AT-07 | 卡含：RNEA 评估器、输出序列与包络统计、正动力学一致性、可信等级、多工况、任务拆分 | DYN-04 映射不自行实现（归 drivetrain） | L（文档） |
| WP-17-T02 | 构建落位：dynamics 占位转真实库＋插件目标 | dynamics | WP-17-T01、WP-03-T01 | dynamics.md §3（卡） | `ird/dynamics/CMakeLists.txt`（新）；计算库（链 rwsim 零 Qt 基线）＋`_plugin` | NFR-MNT-01、ARCH §3.3 | 双模式构建零错误；二分结构扫描通过 | — | S |
| WP-17-T03 | 实现 RNEA 逆动力学评估器 | dynamics | WP-17-T02、WP-06-T08/T09（DWC/重力投影） | dynamics.md（卡） | 逆动力学评估器（重力/连杆惯性/末端负载/外部力/黏性+库仑摩擦；关节空间精确含全部耦合项） | DYN-01/02、MDL-16（摩擦输入链）、MDL-22（重力投影） | 二连杆解析算例＋静态重力矩（含倒挂 AT-37 动力侧）用例通过 | 关节侧结果与候选传动无关（DYN-04） | M |
| WP-17-T04 | 实现输出序列与峰值/RMS 包络 | dynamics | WP-17-T03 | dynamics.md（卡） | 转角/转速/加速度/类型化广义力/机械功率序列＋峰值（持续时间窗+所在段）与完整循环 RMS（含驻留） | DYN-03 | 峰值窗与所在段报告、RMS 含驻留用例通过 | 力矩/力类型化（转动 N·m/移动 N） | M |
| WP-17-T05 | 实现正动力学一致性检查 | dynamics | WP-17-T03 | dynamics.md（卡） | RobWorkSim 正动力学场景（控制输入/初始状态/步长/容差明确）＋异常检测 | DYN-05 | 响应一致性与异常检测用例通过 | — | M |
| WP-17-T06 | 实现数据不足降级与可信等级 | dynamics | WP-17-T03 | dynamics.md（卡）；evidence EvidenceItem 承载 | 物性/摩擦缺失可信等级标记（DYN-06 降级经证据表达） | DYN-06、MDL-05/16 | 缺失走 DataInsufficient 降级并列入缺失清单；不包装成精确结论用例通过 | — | S |
| WP-17-T07 | 实现多工况与包络合并 | dynamics | WP-17-T04 | dynamics.md（卡） | 多负载工况/急停保持设计工况＋结果包络合并 | DYN-07、EVI-02 | 包络合并不替代必验工况覆盖用例通过 | — | M |
| WP-17-T08 | 实现曲线联动与峰值定位/回放数据 | dynamics | WP-17-T04、WP-10 | dynamics.md（卡） | 各关节曲线联动、峰值定位、三维轨迹时刻回放数据 | DYN-08 | 联动与回放不产生修订用例通过 | — | M |
| WP-17-T09 | 实现 dynamics 插件界面 | dynamics | WP-17-T03~T08、WP-10-T08 | dynamics.md（卡） | 动力学工作流页＋曲线视图 | UX 家族 | 界面链路用例通过 | 插件零计算逻辑 | M |
| WP-17-T10 | 契约测试与动力学黄金数据集 | dynamics | WP-17-T03~T09、WP-02 | dynamics.md 验证章（卡） | `dynamics/test/*`＋`testdata/golden/dyn-*` | AT-07（完整循环包络/降级/多工况不漏验） | 全部用例通过并留痕 | — | M |

### 2.19 WP-18 drivetrain（单元卡待产出；DYN-04 唯一映射实现）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-18-T01 | 编写 drivetrain 单元任务卡 | drivetrain（文档） | WP-05 卡（已存在）；O-07（runtime 视图消费形态）在此裁决 | ARCH §3.1 drivetrain 行（L2 共享计算服务）、§7.10（虚功映射契约）；REQUIREMENTS §13 DYN-04；runtime.md §13.2（drivetrain 行） | 新建 `doc/industrial-robot-design/units/drivetrain.md`（≥v0.1） | DYN-04、SEL-05（支撑）、MDL-21（支撑）、AT-38 | 卡含：DriveTrainMappingEvaluator 接口（③端口注册）、映射契约（τ_motor=Cᵀτ_joint+J_rotor·θ̈）、runtime 消费形态（注入或值传递——O-07 裁决）、任务拆分 | 不采用对角/准静态近似、不丢交叉耦合项 | M（文档） |
| WP-18-T02 | 构建落位：drivetrain 占位转真实库 | drivetrain | WP-18-T01、WP-03-T01 | drivetrain.md §3（卡） | `ird/drivetrain/CMakeLists.txt`（新）；STATIC（链 core＋evidence） | NFR-MNT-01 | 双模式构建零错误；红线扫描零命中 | 零 Qt；依赖仅 core+evidence（§3.5） | S |
| WP-18-T03 | 实现 DriveTrainMappingEvaluator 唯一实现 | drivetrain | WP-18-T02、WP-05-T10（③端口） | drivetrain.md（卡）；ARCH §7.10（M-12 口径） | 映射评估器（电机侧工作点 τ/ω/P、η±、反射惯量 J·i²、惯量比、能量分项、四象限统计；C 非常矩阵/病态诊断阻止） | DYN-04、NFR-COR-01 | 传动映射黄金数据（双向效率/反射惯量/摩擦不重复计入）用例通过；常矩阵 C 下与对角传动比等价验证 | 禁止与壳体质量重复计入转子惯量 | L（映射核心/工作点统计两提交） |
| WP-18-T04 | 契约测试与传动映射黄金数据集 | drivetrain | WP-18-T03、WP-02 | drivetrain.md 验证章（卡） | `drivetrain/test/*`＋`testdata/golden/dt-*` | DYN-04 验收要点、AT-38（R1 可验部分） | 全部用例通过并留痕 | — | M |
| WP-18-T05 | 实现耦合矩阵 C 全链消费（R2/D） | drivetrain | WP-18-T03、WP-13-T18 | drivetrain.md §13（卡预留）；MDL-21/DYN-04 | C 矩阵消费（SEL-03/04/05 同一口径；限位经映射校验） | MDL-21、AT-38 | 高速多轴联动工况映射含交叉耦合项验证；R1 无耦合链等价对角映射用例通过 | — | M |

### 2.20 WP-19 selection（单元卡待产出；SEL-01~10）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-19-T01 | 编写 selection 单元任务卡 | selection（文档） | WP-11/WP-18/WP-04/WP-05 各卡；P-POL-3（惯量比阈值归属）在此裁决 | ARCH §3.1 selection 行；REQUIREMENTS §14；drivetrain.md 交接 | 新建 `doc/industrial-robot-design/units/selection.md`（≥v0.1） | SEL-01~10、AT-08/30 | 卡含：目录包 schema、硬筛选规则、组合校核（③端口）、回填命令（①端口）、P-POL-3 阈值归属落位、任务拆分 | 硬能力判定不受优选品牌影响（SEL-07） | L（文档） |
| WP-19-T02 | 构建落位：selection 占位转真实库＋插件目标 | selection | WP-19-T01、WP-03-T01 | selection.md §3（卡） | `ird/selection/CMakeLists.txt`（新）；计算库＋`_plugin` | NFR-MNT-01、ARCH §3.3 | 双模式构建零错误；二分结构扫描通过 | — | S |
| WP-19-T03 | 实现器件目录包模型与导入校验 | selection | WP-19-T02、WP-11-T03/T04 | selection.md（卡） | 版本化 CSV 目录包（清单/主表/能力曲线/兼容表）＋导入校验（引用/单位/必填/唯一性/范围；分段线性插值禁外推） | SEL-01/02、NFR-SEC（io 协作）、AT-08 | 目录导入/错误字段/版本锁定测试用例通过 | 默认禁止能力曲线外推 | M |
| WP-19-T04 | 实现电机/减速器硬筛选 | selection | WP-19-T03 | selection.md（卡） | 电机筛选（连续/峰值转矩、转速、功率、过载、工作制、电压、温度降额、制动/安全系数）＋减速器筛选（转矩/转速/速比/效率/回隙/寿命/安装方向/外载） | SEL-03/04 | 可行/不可行型号黄金表用例通过 | — | M |
| WP-19-T05 | 实现组合校核 | selection | WP-19-T04、WP-18-T03（③端口） | selection.md（卡） | 电机—减速器—负载惯量校核＋组合兼容＋每轴工作点（同一映射口径） | SEL-05、DYN-04（消费） | 经共享 DriveTrainMappingEvaluator 校核用例通过；惯量比阈值为可配置工程规则 | 不自建映射实现（R-5 精神） | M |
| WP-19-T06 | 实现可行集与淘汰原因输出 | selection | WP-19-T05 | selection.md（卡） | 可行组合＋裕量/质量/成本/来源＋逐项淘汰原因（实际值/阈值） | SEL-06、EVI-01（选型 Profile） | 每个淘汰项含实际值和阈值用例通过 | — | S |
| WP-19-T07 | 实现优选品牌与目录版本管理 | selection | WP-19-T03 | selection.md（卡） | 企业优选品牌/供应状态/系列限制＋目录差异比较＋项目锁定版本 | SEL-07/08 | 硬能力判定不变；目录更新不静默改变历史结果用例通过 | — | M |
| WP-19-T08 | 实现移动关节范围外诊断 | selection | WP-19-T04 | selection.md（卡） | 移动关节轴"范围外"诊断 | SEL-09、§2.1 | 含移动关节链阻断测试（DataInsufficient 语义）用例通过 | 不静默套用旋转传动 | S |
| WP-19-T09 | 实现选型回填命令与合成 | selection | WP-19-T06、WP-04-T10 | selection.md（卡）；ARCH S4 走查 | 回填各轴 DriveTrainDesign（目录版本＋安装关系；壳体/转子区分合成不重复）＋复算提示 | SEL-10、MDL-16（合成规则）、AT-30 | 回填产生新修订＋依赖失效提示；复核前不沿用原通过结论用例通过 | — | M |
| WP-19-T10 | 实现 selection 插件界面 | selection | WP-19-T03~T09、WP-10-T08 | selection.md（卡） | 选型工作流页＋目录管理＋候选表 | UX 家族 | 界面链路用例通过 | 插件零计算逻辑 | M |
| WP-19-T11 | 契约测试与选型黄金数据集 | selection | WP-19-T03~T10、WP-02 | selection.md 验证章（卡） | `selection/test/*`＋`testdata/golden/sel-*` | AT-08/30 | 全部用例通过并留痕 | — | M |
| WP-19-T12 | 实现直线传动目录与映射（R2） | selection（drivetrain 协作） | WP-19-T05、阶段 D 启用 | §17.1 子项 | 滚珠丝杠/齿条/同步带/直线电机目录模板＋工作点映射 | SEL-09-S1、AT-36（选型侧） | 目录模板导入校验通过；可行+不可行样例（含实际值/阈值）；不改变现有六/七轴全旋转链行为 | — | M |

### 2.21 WP-20 optimization·OPT-B（单元卡待产出；阶段 B 静态子集，§15.0 权威范围）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-20-T01 | 编写 optimization 单元任务卡（OPT-B 部分） | optimization（文档） | WP-05/WP-06/WP-07/WP-04/WP-15 各卡 | ARCH §7.10；REQUIREMENTS §15.0（OPT-B 权威范围）；evidence.md §13（optimization 行） | 新建 `doc/industrial-robot-design/units/optimization.md`（≥v0.1，含 OPT-D 扩展预留节） | OPT-01~04/06/07/11/12（B 子集）、AT-09/12/34 | 卡含：变量/补丁模型、静态硬约束编排（经③端口）、Pareto 与八项指标、Quick/Verified＋缓存＋种子、采用守卫、预检、一站式导出、任务拆分 | 阶段 B 不以联合硬约束作退出条件（OPT-03） | L（文档） |
| WP-20-T02 | 构建落位：optimization 占位转真实库＋插件目标 | optimization | WP-20-T01、WP-03-T01 | optimization.md §3（卡） | `ird/optimization/CMakeLists.txt`（新）；计算库＋`_plugin` | NFR-MNT-01、ARCH §3.3 | 双模式构建零错误；二分结构扫描通过 | — | S |
| WP-20-T03 | 实现变量与候选补丁模型 | optimization | WP-20-T02、WP-06-T04 | optimization.md（卡） | 连续/量化/离散变量＋CandidatePatch（drivetrain.ratio StageB 绑定）＋改型默认锁定＋两种初始化 | OPT-01/02 | 新机型/改型初始化用例；ratio 编译进候选（V12-02）用例通过 | 未授权参数默认锁定 | M |
| WP-20-T04 | 实现静态硬约束执行 | optimization | WP-20-T03、WP-15-T03~T07（③端口）、WP-07-T07 | optimization.md（卡）；§15.0 OPT-B 行 | 硬约束编排（拓扑/输入前置、Must 工位可达、Must 区域覆盖、碰撞静态子集、关节限位）＋阶段锁（IRD-OPT-STAGE-LOCKED） | OPT-03、EVI-01/02 | 硬约束先行；阶段 B 引用联合约束即拒；漏一个启用必验工况不得出正式通过（AT-09 反例）用例通过 | — | M |
| WP-20-T05 | 实现 Pareto 非支配与八项指标分层 | optimization | WP-20-T04 | optimization.md（卡） | 非支配排序（容差支配）＋八项指标全展示（可算项计算、不可算"—"）＋默认激活三项静态指标 | OPT-04/07 | 集合/排序满足容差支配；三项可算其余"—"用例通过 | 不以加权总分代替取舍 | M |
| WP-20-T06 | 实现 Quick/Verified 两层、缓存与种子 | optimization | WP-20-T04、WP-05-T09 | optimization.md（卡） | Quick 保守淘汰→Verified 复核＋会话内缓存＋确定性随机种子 | OPT-06（B 子集）、NFR-COR-02 | 同种子同线程等价集合与稳定排序（AT-09~11）用例通过 | 并行与检查点不作 B 期承诺 | M |
| WP-20-T07 | 实现 OptimizationRunResult 与采用守卫 | optimization | WP-20-T05、WP-04-T10 | optimization.md（卡）；ARCH S3 走查 | 运行结果归属（不产生修订）＋"设为当前方案"（创建分支＋新修订＋完整复算） | OPT-08、AT-12 | 基线对象字节不变；预览候选不改基线用例通过 | — | M |
| WP-20-T08 | 实现预检 Preflight 与基线评估 | optimization | WP-20-T03 | optimization.md（卡） | 预检（写集冲突/未登记绑定/缺失上游工件，阻塞/警告计数与定位）＋Evaluate Baseline | OPT-11、WP-05（依赖） | 预检阻塞项可定位；基线评估作比较基准用例通过 | — | M |
| WP-20-T09 | 实现优化证据一站式导出 | optimization | WP-20-T06、WP-12-T05、WP-11 | optimization.md（卡） | 研究结果 JSON/候选 CSV/任务明细 CSV/审计 CSV/Markdown 证据报告/候选模型包 | OPT-12、RPT-02/03（共用规则）、AT-34 | 全套工件导出且审计计数与重放一致；契约过期阻断正式导出用例通过 | — | M |
| WP-20-T10 | 实现 optimization 插件界面 | optimization | WP-20-T03~T09、WP-10-T08 | optimization.md（卡） | 变量表/约束页/运行控制（取消/进度漏斗）/候选表与对比 | UX 家族、TASK（取消） | 取消协作生效与进度显示（AT-34）用例通过 | 暂停/继续归 R2（AT-35） | M |
| WP-20-T11 | 契约测试与优化黄金数据集 | optimization | WP-20-T03~T10、WP-02 | optimization.md 验证章（卡） | `optimization/test/*`＋`testdata/golden/opt-*` | AT-09/12/34（OPT-B 部分） | 同种子确定性/不可行不入可行集/采用守卫用例通过并留痕 | — | M |

### 2.22 WP-21 optimization·OPT-D（阶段 D/R2；OPT-01~10 全量语义）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-21-T01 | 冻结 P-04 扰动/鲁棒性协议（与需求所有者协同） | 横切（需求侧＋WP-21） | 无未满足前置（OPT-D 启用前必须完成——硬前置） | REQUIREMENTS 附录 C P-04（已登记延期）、OPT-09 | P-04 冻结产物（三模式采样规则/判定阈值/适用边界）并入 REQUIREMENTS 附录 C 修订 | P-04、OPT-09 | 冻结留痕；未冻结不得启用联合优化 | 本文不私设协议数值 | S |
| WP-21-T02 | 扩展 optimization 任务卡（OPT-D 部分） | optimization（文档） | WP-20-T01、WP-21-T01 | §15.0 OPT-D 行；ARCH §10.2（R2 无新机制核对） | units/optimization.md 增量修订（OPT-D 节） | OPT-05/06/09/10 全量、AT-35 | 卡含：分层联合策略、并行/检查点规模化、鲁棒性三模式、暂停/继续边界、误淘汰审计口径 | 不新增架构机制（PA-7） | M（文档） |
| WP-21-T03 | 实现分层联合策略 | optimization | WP-21-T02、WP-20-T04、WP-18（器件匹配内层） | optimization.md OPT-D 节（卡） | 结构/传动外层探索＋轨迹动力学校核与器件匹配内层反馈 | OPT-05、OPT-03（联合硬约束） | 内层 Quick→Verified→器件匹配反馈闭环用例通过 | — | L |
| WP-21-T04 | 实现联合硬约束与并行/检查点规模化 | optimization | WP-21-T03、WP-08-T07/T09、WP-16/WP-17 评估器（③端口） | optimization.md OPT-D 节（卡） | 轨迹/动力/器件联合硬约束＋完整并行评估与检查点恢复 | OPT-03/06（D 部分）、NFR-COR-02 | 并行与检查点恢复统计不重复（AT-35）；八项指标全量可算用例通过 | — | L |
| WP-21-T05 | 实现灵敏度与鲁棒性复核 | optimization | WP-21-T01/T03 | optimization.md OPT-D 节（卡）；P-04 冻结值 | 扰动协议三模式（有界公差/概率鲁棒性/敏感度抽查） | OPT-09（转必需证据） | 三模式按冻结协议执行用例通过 | — | M |
| WP-21-T06 | 实现搜索策略接口扩展 | optimization | WP-21-T02 | optimization.md OPT-D 节（卡） | 搜索策略接口（首版不要求一次引入全部算法） | OPT-10 | 接口扩展评审通过 | 不预建空算法占位 | S |
| WP-21-T07 | 实现暂停/继续与暂停中取消 | optimization＋execution | WP-21-T04、WP-08-T03/T04 | optimization.md OPT-D 节；ARCH §4.3（A5） | Paused 态消费（暂停确认边界/继续不重复统计/暂停中取消保检查点） | OPT-06（D 部分）、AT-35（V12-06） | 暂停确认边界明确、继续不重复统计、不改变输入快照、能力不支持时状态反馈用例通过 | — | M |
| WP-21-T08 | 契约测试（OPT-D） | optimization | WP-21-T03~T07、WP-02 | optimization.md 验证章（卡） | `optimization/test/*` 扩展 | AT-35 | 误淘汰审计达标（≥200 样本、误淘汰率 ≤1%、95% 置信上界 ≤3%）用例通过并留痕 | — | M |

### 2.23 WP-22 workflow（单元卡待产出；跨阶段编排，A~E）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-22-T01 | 编写 workflow 单元任务卡（含阶段 A 生命周期原形条款） | workflow（文档） | WP-10/WP-04/WP-08/WP-09 各卡 | ARCH §3.4（编排定位）；REQUIREMENTS §17 表注（RV-11 跨阶段）、§18 UX-12/13；project.md §13.2（workflow 行） | 新建 `doc/industrial-robot-design/units/workflow.md`（≥v0.1） | UX-12/13、PM-01~03/05~07/10/11/14/15、AT-20/21/29 | 卡含：七阶段门控规则与状态投影消费、生命周期入口流程（新建/打开/另存/包/重关联）、失败路径用户呈现、任务拆分 | workflow 不直链任何业务域单元（ARCH §3.4） | L（文档） |
| WP-22-T02 | 构建落位：workflow 占位转真实库＋插件目标 | workflow | WP-22-T01、WP-03-T01 | workflow.md §3（卡） | `ird/workflow/CMakeLists.txt`（新）；STATIC/插件目标 | NFR-MNT-01 | 双模式构建零错误；R-1 扫描（无业务域直链边）通过 | — | S |
| WP-22-T03 | 实现七阶段门控与下一步建议 | workflow | WP-22-T02、WP-10-T09（StageStatusModel） | workflow.md（卡） | 七阶段导航（建模→…→报告）就绪条件解锁/锁定＋级联失效提示＋下一步建议 | UX-01/12 | 上游完成态/结果失效时级联提示下游需重算用例通过 | 阶段状态投影归 ui（不双权威） | M |
| WP-22-T04 | 实现新建项目三步向导 | workflow | WP-22-T02、WP-13（模板/导入）、WP-11-T06、WP-04 | workflow.md（卡）；PM-01 | 向导（项目信息→初始来源（模板六/七轴×安装；URDF/Xacro；空白）→确认）＋右侧实时摘要 | PM-01、AT-20 | 取消/失败不留半成品；URDF 基线修订保存＋外部引用记录（复制入区或登记）用例通过 | — | M |
| WP-22-T05 | 实现打开向导与无项目首页 | workflow | WP-22-T02、WP-04-T08 | workflow.md（卡）；PM-02/10 | 五步打开协议 UI（命令行/拖放/对话框格式识别）＋无项目首页（新建/打开/最近三入口） | PM-02/10、AT-20 | 失败显示具体文件且不动当前项目；最近项目 10 上限/去重/失效提示＋重新选择＋移除；无项目禁用七阶段入口用例通过 | — | M |
| WP-22-T06 | 实现关闭/切换/退出统一确认 | workflow | WP-22-T02、WP-08-T06、WP-04-T12 | workflow.md（卡）；PM-03；ARCH §6.8（存储上下文） | 统一确认对话框（草稿三选＋任务二选＋任务清单 9 态短标签）＋切换=关闭后候选验证 | PM-03、AT-20/21 | 取消可中止；等待选项覆盖在途归档（A7）；退出复用同一流程用例通过 | — | M |
| WP-22-T07 | 实现另存为与包导出/导入向导 | workflow | WP-22-T02、WP-04-T18 | workflow.md（卡）；PM-05 | 另存向导（勾选记忆默认）＋包导出/导入（后台进度可取消、取消清理临时区） | PM-05、AT-20 | 进度可取消即清理；失败不留目标目录用例通过 | — | M |
| WP-22-T08 | 实现旧格式拒绝/只读/重关联流程 | workflow | WP-22-T02、WP-04-T03/T17 | workflow.md（卡）；PM-06/07/09 | 旧格式只读拒绝（诊断码＋升级指引不自动升级）＋只读打开提示（PID）＋重关联入口（显式提交） | PM-06/07/09、AT-20/21 | 原文件不动；只读禁编辑与应用提交；重关联经显式提交产生新修订用例通过 | — | M |
| WP-22-T09 | 实现标题栏/状态栏与恢复横幅 | workflow | WP-22-T02、WP-10-T04、WP-09 | workflow.md（卡）；PM-11/15 | 标题栏/状态栏（`<显示名>[*][（只读）]`、方案、结果状态）＋恢复横幅（一句话汇总＋详情/恢复/放弃） | PM-11/15、AT-21 | 格式与状态来源用例；横幅三场景（未完成保存忽略/任务中断/检测到草稿）用例通过 | — | M |
| WP-22-T10 | 实现用户设置持久化 | workflow | WP-22-T02、WP-10 | workflow.md（卡）；PM-14 | 用户级设置（最近项目/导出勾选/上次目录/快捷键绑定——不入 .rwdesign；求解配置独立不混入） | PM-14 | 用户设置与分析设置分离持久化用例通过 | — | S |
| WP-22-T11 | 实现方案比较视图 | workflow | WP-22-T02、WP-13-T14（Model Diff）、WP-10 | workflow.md（卡）；UX-13（V15-02 分工） | 2~4 方案八项比较指标差异高亮＋Model Diff 呈现（结构/参数/物性分组、点击定位） | UX-13、MDL-08（消费）、AT-12 | 差异高亮与点击定位对象用例通过 | 数据实体归 MDL-08（不重复实现） | M |
| WP-22-T12 | 实现命令集注册与工业高频命令 | workflow | WP-22-T02~T09、WP-10-T06 | workflow.md（卡）；UX-13 最小清单（F5） | 注册最小命令集（新建/打开/保存草稿/应用/撤销/重做/切换方案/另存为/包导出/报告导出＋运行碰撞检查/切换显示模式/复位关节 Home/Zero） | UX-13（命令清单部分）、KIN-06（复位会话语义） | ≥10 条常用命令注册并可经面板模糊搜索到达；复位关节不产生修订用例通过 | 全局快捷键经 ui 唯一注册点 | S |
| WP-22-T13 | 契约测试套件 | workflow | WP-22-T03~T12、WP-02 | workflow.md 验证章（卡） | `workflow/test/*` | AT-20/21/29（生命周期主线） | 新建→编辑→应用→撤销/重做→包导出导入→另存为主线用例通过并留痕 | — | M |

### 2.24 WP-23 性能规模化与确定性（横切；C 起 R1、D 起 R2）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-23-T01 | 编写性能基准清单与测量协议登记 | 横切（文档） | 无未满足前置（可在阶段 A 产出、阶段 C 启用） | REQUIREMENTS §19.3、PM-16（F-03/V12/V13 测量协议）；testkit.md §10.1（performance-baseline 类预留） | 新建 `doc/industrial-robot-design/performance-baseline.md`（基准清单＋黄金项目定义＋协议） | NFR-PERF-01~06、PM-16、NFR-COR-02 | 基准清单覆盖全部 PERF 条目；PM-16 协议（冷/热分组、≥15+15 样本、P95 线性插值）落位 | 不以 3 次中位数替代 P95 | M（文档） |
| WP-23-T02 | 交互响应基准达标（阶段 C） | 横切（ui/各域） | WP-23-T01、M5 各域 | performance-baseline.md | 基准执行与优化 | NFR-PERF-01 | 导航/选择/筛选/编辑/视图切换 P95 ≤200 ms；无 >2 s 无响应；>1 s 转后台用例通过 | — | M |
| WP-23-T03 | 取消时序达标（阶段 C） | 横切（execution） | WP-23-T01、WP-08-T04 | performance-baseline.md | 基准执行 | NFR-PERF-02 | 2 s/10 s 协议时序测量通过 | — | S |
| WP-23-T04 | 大规模数据加载与分页达标（阶段 C） | 横切（ui/各域） | WP-23-T01、M5 各域 | performance-baseline.md | 基准执行与分页优化 | NFR-PERF-03 | 5,000 任务点/100,000 采样结果/10,000 候选摘要加载保存分页通过；不整体装入界面 | — | M |
| WP-23-T05 | 项目打开热启动达标（阶段 C） | 横切（project） | WP-23-T01、WP-04-T08 | performance-baseline.md；PM-16 | 黄金项目（1,000 对象/100 修订）热启动 P95 ≤3 s | PM-16 | 热启动组 P95 ≤3 s（≥15 有效样本）；冷启动软指标 P95 ≤8 s 单独报告超限登记 | 冷启动不作门禁 | M |
| WP-23-T06 | 内存与节流规模化达标（R2/D） | 横切（execution） | WP-23-T01、WP-08-T08、WP-21-T04 | performance-baseline.md | 主进程+全部工作进程峰值 ≤70% 物理内存 | NFR-PERF-04 | 大任务分批/流式/检查点＋先节流后诊断用例与基准通过 | — | M |
| WP-23-T07 | 并行吞吐达标（R2/D） | 横切（execution/各域） | WP-23-T01、WP-21-T04 | performance-baseline.md | 固定候选评估基准 8 线程 ≥4× 中位吞吐 | NFR-PERF-05、NFR-COR-02 | 吞吐比测量通过（不含首次编译与导入） | 并行归约满足附录 D 第 8 项容差 | M |
| WP-23-T08 | 优化运行基准达标（R2/D） | 横切（optimization） | WP-23-T01、WP-21-T04 | performance-baseline.md | 10,000 Quick＋100 Verified 8 小时内结束＋检查点恢复 | NFR-PERF-06 | 基准时限内完成；运行期间满足 PERF-01；同版本检查点恢复用例通过 | — | M |
| WP-23-T09 | 确定性复现基准 | 横切（各域） | WP-23-T01、WP-02-T06/T08 | performance-baseline.md；附录 D 第 8 项 | 同输入＋版本＋配置＋线程＋种子等价集合与稳定排序验证集 | NFR-COR-02 | 各域抽样复现用例通过（不要求浮点逐字节相同） | — | M |

### 2.25 WP-24 交付与部署基础（横切；A/E）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-24-T01 | 冻结版本基线首版（阶段 A 收口登记、阶段 E 复核） | 横切 | WP-01-T01、M0（构建树现状登记） | NFR-DEP-05；ARCH §5.2；policy.md P-POL-5（碰撞后端版本回填） | `ird/share/baseline.md`（commit/tag、Qt/编译器版本、碰撞后端及版本、构建选项、已用 API 清单、许可证清单） | NFR-DEP-05、P-POL-5（消账） | 基线字段齐备并与构建树一致；policy CollisionBackendDescriptor 取值回填 | 基线变更走设计变更评审 | S |
| WP-24-T02 | 实现应用启动与异常诊断 | 横切（ui/壳） | WP-09-T05、WP-24-T03 | PM-17；ARCH §4.1（冷启动软指标） | 启动流程（设置/静态插件/闪屏可禁用/命令行参数）＋异常退出诊断文件 | PM-17、AT-11、NFR-SEC-07 | 异常退出诊断文件写出且脱敏；中断任务"已中断"可重跑用例通过 | — | M |
| WP-24-T03 | 实现应用壳装配与静态白名单 | 横切（L5） | M3（平台单元就绪）、WP-10-T03 | ARCH §2.4（SA-01）、§5.4；NFR-SEC-04 | 应用壳（RobWorkStudioApp 壳＋白名单静态装配＋About 插件清单 UX-14） | NFR-SEC-04、SA-01、UX-14（协作） | 白名单在装配期固定、无运行时动态加载入口；About 清单与白名单一致用例通过 | 不提供动态加载/卸载入口 | M |
| WP-24-T04 | 旧链路清理 | 横切 | 对应新能力替换完成的阶段 | NFR-MNT-06 | 各阶段替换后的旧实现/仅验证旧接口测试/无用依赖删除 | NFR-MNT-06 | 同阶段删除旧实现；无未引用目标与废弃构建选项残留（代码审查清单） | — | S |
| WP-24-T05 | 生成安装包依赖清单 | 横切 | WP-24-T01 | NFR-SEC-05；testkit.md §3.6（排除扫描建议——WP-02-T10 移交） | `share/` 依赖清单（组件/版本/许可证/哈希）＋安装树 testkit 排除复验 | NFR-SEC-05、T-1 红线 | 清单与安装树一致；安装树零 testkit 组件扫描通过 | — | M |
| WP-24-T06 | 制作离线安装包 | 横切 | WP-24-T03/T05、M7 前 | NFR-DEP-01~03；ARCH §5.4 | Windows x64 离线安装包（bin/plugins/share 结构、版本并存、卸载） | NFR-DEP-01/02/03 | 不依赖开发机路径；离线安装/卸载/版本并存验收通过 | — | L（打包脚本/验证两提交） |
| WP-24-T07 | 安装包签名与升级完整性校验 | 横切 | WP-24-T06 | NFR-SEC-06（按企业部署策略单独验收） | 签名与升级校验 | NFR-SEC-06 | 按企业策略验收留痕 | — | S |

### 2.26 WP-25 试点与交付材料（横切；阶段 E）

| 编号 | 标题 | 单元 | 前置依赖 | 输入 | 输出 | 需求追溯 | 验收标准 | 禁止项 | 规模 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WP-25-T01 | 试点范围与前置数据签署 | 横切 | WP-00-T01（治理机制） | PILOT-01 | 签署件（只读、版本化、锁定对照口径） | PILOT-01 | 清单逐项由业务数据负责人与产品负责人签署 | — | S |
| WP-25-T02 | 算法与实测对照 | 横切 | WP-25-T01、M5/M6（域结果可用） | PILOT-02 | 对照报告（按已签署容差逐指标对比；趋势/相关性证据边界） | PILOT-02 | 不得宣称超出数据支持的选型结论 | — | M |
| WP-25-T03 | 交付材料包齐备并复核 | 横切 | WP-24-T06、WP-25-T01 | DEL-01 | 发布检查表＋样例项目＋报告模板＋安装说明＋用户手册（关联 UX-14）＋培训材料 | DEL-01 | 检查表链接全部试点实际工件；签署复核通过 | — | M |
| WP-25-T04 | 试点缺陷门禁 | 横切 | WP-25-T02/T03 | DEL-02 | 缺陷门禁执行与登记 | DEL-02 | 开放 Blocker=0；遗留 Critical 均有负责人/影响说明/计划日期；不得降阈值或隐藏诊断关闭缺陷 | — | S |

---

## 3. 需求追踪矩阵（需求 → AT → 单元任务 → 主 WP）

> 覆盖判定：需求 ID 被至少一个实施任务的"需求追溯"列引用即为覆盖；每行同时必须能映射到 WP-A～WP-I 主 WP。（主 WP 任务为主责承载，支持 WP 任务为协作承载）。分期子项（§17.1）与分级子级（RPT-01-B/C）单列。全部 180 条主需求＋12 子项＋2 子级均被覆盖，**无"本期不实现/延期"的沉默遗漏项**；R2/D/E 条目按其发布标记随对应阶段任务交付（RV-04：全量范围、分期承接）。
>
> **缩写约定**：行内"裸编号→"沿用该行行首的需求家族前缀（如 MDL 行中"02→T03/T09"即 MDL-02→WP-13-T03/T09）；区间记法"S1~S3"指该父项的全部分期子项。

| 需求域 | 需求 ID → 覆盖任务（主责加粗优先） |
| --- | --- |
| ARC 架构（5） | ARC-01→**WP-04-T10/T13/T15**；ARC-02→**WP-01-T01**＋WP-04-T01/T15；ARC-03→**WP-06-T04/T07/T11**＋WP-13-T12；ARC-04→**WP-06-T05**＋WP-13-T12；ARC-05→**WP-07-T02~T09**＋WP-10-T07 |
| CON 快照/一致性（6） | CON-01→**WP-05-T03**＋WP-04-T04/T05；CON-02→**WP-05-T07/T08**；CON-03→**WP-04-T17**＋WP-11-T06＋WP-05-T05；CON-04→**WP-05-T09**＋WP-08-T09＋WP-06-T10；CON-05→**WP-05-T04/T08**；CON-06→**WP-05-T03/T04**＋WP-07-T03＋WP-06-T05 |
| TASK 任务执行（3） | TASK-01→**WP-08-T03**；TASK-02→**WP-05-T07**；TASK-03→**WP-08-T05**＋WP-03-T08 |
| ERR 诊断（1） | ERR-01→**WP-09-T03/T04**＋WP-03-T07＋各域诊断构造（WP-07-T10 等） |
| EVI 证据（2） | EVI-01→**WP-05-T05/T06**；EVI-02→**WP-05-T06**＋各域评估器（WP-15-T05/T06、WP-16-T07、WP-17-T07、WP-19-T06、WP-20-T04） |
| MDL 建模（22） | MDL-01→**WP-13-T03/T07**；02→T03/T09；03→T05；04→T07；05→T04；06→T08；07→T15；08→T14（呈现 WP-22-T11）；09→T03；10→T09；11→T05；12→T05/T08；13→T10；14→**T12＋WP-06-T05**；15→T10；16→T04；17→T10；18→T17（R2/D）；19→T06；20→T13；21→T18（R2/D）；22→**T11＋WP-06-T06** |
| REQ 需求定义（12） | REQ-01~04→**WP-14-T03**；05→T04；06→T05；07→T07；08→T06；09→T06；10→T06；11→T07；12→T04 |
| KIN 运动学（14） | KIN-01→**WP-15-T03**；02→T04；03→T05；04→T06；05→T07；06→T08；07→T11；08→T09；09→T14（R2/D）；10→T15（R2/D）；11→T16（R2/D）；12→T10；13→T10；14→T08 |
| TRJ 轨迹（8） | TRJ-01→**WP-16-T04**；02→T05；03→T06；04→T07（P-06 冻结 WP-16-T02）；05→T08；06→T09；07→T10；08→T11 |
| DYN 动力学/传动（8） | DYN-01→**WP-17-T03**；02→T03；03→T04；04→**WP-18-T03**（消费 WP-17-T03）；05→WP-17-T05；06→WP-17-T06；07→WP-17-T07；08→WP-17-T08 |
| SEL 选型（10） | SEL-01/02→**WP-19-T03**；03/04→T04；05→T05（阈值归属 P-POL-3/O-13）；06→T06；07→T07；08→T07；09→T08；10→T09 |
| OPT 优化（12） | OPT-01→**WP-20-T03**；02→T03；03→T04（D 全量 WP-21-T04）；04→T05；05→WP-21-T03；06→T06（D WP-21-T04/T07）；07→T05；08→T07；09→WP-21-T05；10→WP-21-T06；11→T08；12→T09 |
| RPT 报告（6＋2 子级） | RPT-01-B→**WP-12-T03**；RPT-01-C/RPT-04→WP-12-T07；RPT-02→T04；RPT-03→T05；RPT-05→T06；RPT-06→T08 |
| PM 项目管理（18） | PM-01→**WP-22-T04**＋WP-11-T06；02→WP-22-T05＋**WP-04-T08**；03→WP-22-T06；04→**WP-04-T12**；05→WP-22-T07＋**WP-04-T18**；06→WP-22-T08；07→WP-22-T08＋**WP-04-T03**；08→**WP-04-T07/T08**；09→WP-22-T08＋**WP-04-T17**；10→WP-22-T05；11→WP-22-T09；12→**WP-04-T06**；13→**WP-08-T05**；14→WP-22-T10；15→WP-22-T09；16→**WP-23-T05**；17→**WP-24-T02/T03**；18→**WP-04-T13** |
| UX 界面（14） | UX-01→**WP-10-T09**＋WP-22-T03；02→WP-10-T09；03→**WP-09-T04**＋WP-10 消费；04→WP-10-T08；05→WP-10-T08；06→WP-10-T04；07→WP-10-T08；08→**WP-10-T07**＋WP-07-T05；09→WP-10-T03；10→WP-10-T04；11→WP-10-T05；12→WP-10-T09＋**WP-22-T03**；13→**WP-22-T11/T12**＋WP-10-T06；14→WP-10-T10 |
| NFR-COR（5） | 01→**WP-02-T03~T05**＋各域解析算例（WP-15-T13/WP-17-T10/WP-18-T04）；02→**WP-23-T09**＋WP-05-T04；03→**WP-03-T03~T05**；04→**WP-12-T03/T05**；05→**WP-07-T07** |
| NFR-MNT（7） | 01→**WP-03-T01**＋WP-01-T01（R-3）；02→**WP-01-T01**；03→**WP-03/WP-09**（分域权威）；04/05→**WP-01-T02**；06→**WP-24-T04**；07→**WP-01-T01**（R-4/R-5） |
| NFR-PERF（6） | 01→**WP-23-T02**；02→WP-23-T03＋**WP-08-T04**；03→WP-23-T04；04→WP-23-T06＋**WP-08-T08**；05→WP-23-T07；06→WP-23-T08 |
| NFR-REL（5） | 01→**WP-04-T07**；02→**WP-08-T07**；03→**WP-08-T03/T05**；04→**WP-11-T06**；05→**WP-09-T04** |
| NFR-SEC（7） | 01/02→**WP-11-T03**；03→**WP-11-T04**；04→**WP-24-T03**＋WP-01-T03；05→**WP-24-T05**；06→**WP-24-T07**；07→**WP-09-T05** |
| NFR-DEP（5） | 01/02/03→**WP-24-T06**；04→**WP-04-T04/T08**；05→**WP-24-T01** |
| PILOT/DEL（4） | PILOT-01→**WP-25-T01**；PILOT-02→WP-25-T02；DEL-01→WP-25-T03；DEL-02→WP-25-T04 |
| 分期子项（12） | PM-04-S1→WP-04-T20；PM-08-S1/PM-11-S1~S3/PM-12-S1→WP-04-T19；KIN-12-S1→WP-15-T17；SEL-09-S1→WP-19-T12；MDL-12-S1→WP-13-T19；TRJ-08-S1/S2/S3→WP-16-T14/T15/T16 |
| 分级子级（2） | RPT-01-B→WP-12-T03/T04/T06；RPT-01-C→WP-12-T07/T08 |

**未覆盖项**：无。（R2/D/E 标记条目均为"明确分期承接"，非遗漏——RV-04；P-03/P-04/P-06 三个待冻结前置各有显式承载任务 WP-13-T07／WP-21-T01／WP-16-T02，冻结前对应能力不启用。）

## 4. 遗留与开放问题（只登记、不裁决；格式参照单元任务卡 P-AR-n 登记）

> 登记项分四类：上游文档与磁盘事实（§4.1）、跨卡契约与裁决（§4.2）、待冻结前置（§4.3）、本文辖域已定稿/消账项（§4.4，构建约定属本文所有者权限——ARCH §11.1，不构成对架构/需求语义的裁决）。每项：编号｜问题｜影响｜来源｜建议裁决者｜状态。
>
> **集中流转视图（WP-00-T01 初始化，2026-09-10）**：本节 O-\* 与 11 张单元卡全部 P-\* 待裁决项的集中登记见 [governance-log.md](governance-log.md)（编号/所有者/状态/消账留痕一表可查）——权威本体仍在本节与各卡，该文件是流转台账；消账后两处同步翻转。

### 4.1 上游文档与磁盘事实（2026-09-10 实测）

| # | 问题 | 影响 | 来源 | 建议裁决者 | 状态 |
| --- | --- | --- | --- | --- | --- |
| O-01 | `old/` **不存在于仓库根**（git 未跟踪、目录缺失），REQUIREMENTS v1.10 起头表声明"位于仓库根、523 文件可读"与磁盘不符 | 附录 A 旧功能台账不可现场查证；"仅作功能范围对照"地位不可行使（从头构建口径不受影响） | core.md §10.1 R-5；本文实测 | 需求所有者（勘误留痕） | 登记 |
| O-02 | 单元详设总目录已建立，仍需补齐剩余单元正文 | 20 单元中 11 个详设已编写；其余 9 个先完成卡片；全部单元按各自门禁冻结接口 | 本文 §0；DETAILED-DESIGN.md | 各单元所有者 | 持续 |
| O-03 | ARCHITECTURE v0.11 为 `Draft` 待评审；评审（A9+）可能调整单元/依赖表/端口 | 全部单元卡与本文 §1/§2 可能需增量同步 | 各卡 P-AR-2/P-PR-2/P-EV-2/P-RT-2/P-POL-4（同源） | 架构所有者 | 登记（各卡已锚定明文，影响面可控） |
| O-04 | 上游设计原则实际为 **PA-1~PA-7**（v0.10 起含 PA-7"架构一次成型、分期启用"），任务指令口径"PA-1~PA-6"少一项 | 无实质影响——本文按 ARCH 现行正文 PA-1~7 执行 | ARCH §1.3 | —（事实登记） | 登记 |
| O-05 | gtest 现状：构建树 `USE_gtest=OFF`、`RobWork/cmake/gtestTargets.cmake` 从未生成、vcpkg installed 无 GTest（ports 有 gtest port）、本机无 Python | 测试目标接入方式悬空——本文 §5.5 已定稿（vcpkg 安装＋`find_package(GTest CONFIG REQUIRED)` 唯一机制），安装动作随 WP-02-T01/WP-03-T01 执行并回登版本 | core.md P-ENV-2、testkit.md P-TK-1/D-01（失实记载） | 构建约定所有者（本文，已定稿） | 已定稿（两卡下次增量修订正式关闭） |
| O-06 | project README 任务卡指向已同步 §12；11 单元 README 均回接当前任务章节 | 文档入口已一致 | 本次同步 | —（执行项） | 已关闭（文档级） |

**缺失详设清单（9 单元）**：modeling、requirements、kinematics、trajectory、dynamics、drivetrain、selection、optimization、workflow。execution、diagnostics、ui、io、reporting 的编写任务已产出；接口冻结和实现验收仍待执行。

### 4.2 跨卡契约与待裁决项（承接六卡 P-* 登记，不裁决）

| # | 问题 | 影响 | 来源 | 建议裁决者 | 状态 |
| --- | --- | --- | --- | --- | --- |
| O-07 | ARCH §3.5 未登记 drivetrain/dynamics→runtime 依赖边，而 runtime.md §13.2 向两者交接快照视图（tryDynamicWorkCell/重力投影） | 两单元消费 runtime 视图的承载形态（注入/值传递/补登边）未定 | ARCH §3.5 vs runtime.md §13.2 | 架构所有者＋drivetrain/dynamics 卡（WP-17-T01/WP-18-T01 起草时） | 登记 |
| O-08 | P-PR-3：ARCH §7.1 图示将"前置断言"画在 project 命令服务框内，断言判定逻辑属业务与策略 | 若按字面读法 project 须实现业务断言（违反分层）；project.md 已按"编排归 project、判定归注入处理器＋④端口"设计 | project.md §15.3 P-PR-3 | 架构所有者 | 登记 |
| O-09 | P-PR-5：ARCH §3.5 未登记 project→io 边，PM-05/CON-03 需要 project 消费 io（ZIP/固化） | 阶段 B 包导入导出/固化的编译期依赖不合法（表外边＝构建失败） | project.md §15.3 P-PR-5 | 架构所有者（补登边或确认注入形态） | 登记（阻塞 WP-04-T18 编译形态，不阻塞注入式先行设计） |
| O-10 | P-POL-2：安全间距/近限位比/条件数警告阈值无冻结默认（附录 D 仅第 11 项行程上限） | 开箱默认行为与验收解释分歧；裁决前 policy 以 nullopt＝显式不适用 | policy.md §15.3 P-POL-2 | 需求所有者＋WP-07 | 登记 |
| O-11 | P-POL-3：SEL-05 惯量比阈值归属（EngineeringPolicySet vs selection 域配置）未定 | 阶段 C 选型切片的 Policy 条目声明与失效矩阵 | policy.md §15.3 P-POL-3 | 需求所有者＋selection 卡（WP-19-T01） | 登记 |
| O-12 | P-POL-8：R-4 例外登记措辞——policy 只读消费完整设备作用域名不构成"拼接/剥离"，例外清单原文仅列 runtime 名称解析器 | 静态检查可能误报 policy 实现 | policy.md §15.3 P-POL-8 | 架构所有者 | 登记（§4.5 预登记行随确认转生效） |
| O-13 | P-EV-3/P-EV-7/P-EV-8：跨域汇总顺序（任务级不可行证明 vs 他域证据缺失并存）、空必验工况集口径、EvidenceItemStatus 词表（Unverified 为实现扩展）上游未明文 | 整体判定验收解释与下游呈现词表 | evidence.md §15.3 | 需求所有者 | 登记（evidence 卡按保守字面顺序实现） |
| O-14 | P-EV-9：必验工况标记的权威 schema（负载工况对象 enabled/mandatory 字段）归 requirements，其卡未产出 | RequiredCaseSet 解析口径 | evidence.md §15.3 | requirements 卡（WP-14-T01，冻结后回接 evidence §4.1.3） | 登记 |
| O-15 | P-RT-3：ARCH §2.3 L2 行"L1 的 RobWork 数学/运动学类型"与 §7.3 要求 runtime 构造 WC/DWC（rw::models/rwsim）措辞差异 | runtime 合法 L1 依赖集需确认；WP-06-T01 已按 §7.3 明文执行并留痕 | runtime.md §15.3 P-RT-3 | 架构所有者 | 登记 |
| O-16 | P-RT-4：安装预设轴向（倒挂＝R_x(π)、壁装＝R_y(π/2)）为 runtime.md 单侧选择，MDL-22 未指明 | 预设矩阵与建模编辑器/用户预期对齐 | runtime.md §15.3 P-RT-4 | modeling 卡（WP-13-T11 交叉核对冻结） | 登记 |
| O-17 | ResourceBytes 生命周期由 io §8.6 / P-IO-2 裁决并回接 runtime | 调用方同步消费；provider 稳定缓冲至析构；Recorded 每次重读 | io.md §8.6；runtime P-RT-6 | io 详设所有者 | 已关闭（设计级）；实现测试待执行 |
| O-18 | P-RT-7：耦合矩阵"病态"阈值（条件数 ≤1×10⁸）为 runtime 设计默认，建议并入 EngineeringPolicySet | 阻止边界的权威归属 | runtime.md §15.3 P-RT-7 | 需求所有者＋policy 卡 | 登记 |
| O-19 | P-PR-8：分支 label 创建时一次写入、不可改名是否满足 PM-11"当前方案"显示需要 | 若需可编辑方案名则走需求变更 | project.md §15.3 P-PR-8 | 需求所有者 | 登记 |
| O-20 | P-POL-5：碰撞后端版本取值依赖 NFR-DEP-05 冻结版本基线（未产出）；内置 ProximityStrategyRW 版本串暂取 RobWork 构建版本 | 复现块与兼容判定版本口径 | policy.md §15.3 P-POL-5 | 版本基线所有者（WP-24-T01 消账） | 登记 |
| O-21 | P-TK-4：ARCH §3.5 未列 testkit（测试单元）；建议补登"testkit→core（仅测试目标）"边及 T-1/T-2 红线 | 依赖门禁数据源不含测试侧红线时 T-1 仅靠 testkit 卡自律 | testkit.md §12.3 P-TK-4 | 架构所有者 | 已消账（2026-09-10，WP-01-T01：ARCH §3.5 补登该行〔契约 allowedFiles 专门授权〕＋门禁白名单收录该边并登记出处；dependency-graph.json 补行使随 traceability 维护承接） |
| O-22 | P-TK-5：大体绩黄金数据（点云/大规模采样快照）存储与 CI 获取策略（git 内 vs LFS vs 制品）未定 | 数据集交付与 CI 时效；阶段 A 数据小（源内即可） | testkit.md §12.3 P-TK-5 | 构建负责人＋WP-02（首个大数据集出现时裁决） | 登记 |
| O-23 | P-TK-7：`requirements-ids.json` lint 字典维护责任 | 字典随 REQUIREMENTS 修订漂移则 lint 失效 | testkit.md §12.3 P-TK-7 | 需求所有者＋WP-02（随需求变更流程更新） | 登记 |
| O-24 | P-D-1：EvaluationMode/TaskOutcome/EngineeringStatus/TaskState 四词表置 core 的跨卡确认 | execution/ui 卡若另作安排则 core 需迁移词表（evidence/runtime/policy 卡已默认承接） | core.md §10.2 P-D-1 | execution/ui 卡（WP-08-T01/WP-10-T01） | 登记 |
| O-25 | reporting 已裁决不留 PDF 专用接口桩 | RPT-02 仍交付 HTML＋JSON/CSV | reporting.md §14.3 P-RPT-6 | reporting 详设所有者 | 已关闭（设计级） |
| O-26 | UX-13 工业高频命令"运行碰撞检查"的执行编排（经④端口触发后台任务、结果呈现）需 workflow/ui/execution 三方契约对齐 | 高频命令语义与任务清单呈现 | UX-13（M-13）；ARCH §7.11 | workflow 卡＋ui 卡（WP-22-T12 落地） | 登记 |
| O-31 | ui.md 依赖声明三方矛盾：§2.1.2 C 表把 project/evidence/execution/policy/runtime 协作（C-3/4/5/7/8/10/11）标为"接口依赖"（ARCH §3.5 定义＝链接库目标），而 §3.1 声明"仅链 core,diagnostics、其余零编译依赖"，ARCH §3.5 白名单亦只有 ui→core,diagnostics 两条；ShellWiring（§11）直接持有 policy::IPolicyProvider、runtime::IRuntimeNameResolver 等对端类型 | 按 §3.1 实现则 ShellWiring 无法编译；按 C 表实现则 5 条边违反 SA-10 门禁（表外边＝构建失败）；WP-10 落位前必须裁决 | 全链一致性审计 2026-09-10；ui.md §2.1.2/§3.1 vs ARCHITECTURE.md §3.5 | 架构所有者（增边或确认 ui 侧最小注入接口模式，对齐其余 10 卡"自定义最小注入接口"惯例）＋ui 卡 | 登记 |
| O-32 | PILOT-01/02、DEL-01/02 四条 P0/R1 需求在 ARCHITECTURE 无 ID 级落点（仅 §10.1 阶段 E 文字性描述；§3.1 单元表"关键需求族"列未登记） | 阶段 E 交付时需求覆盖矩阵（§3）无法回指承载单元与任务；试点/交付验收追溯断链 | 全链一致性审计 2026-09-10；REQUIREMENTS §（PILOT/DEL 条目）vs ARCHITECTURE §3.1/§10.1 | 架构所有者（WP-22/WP-24 任务细化前补登落点） | 登记 |
| O-33 | WP-02-T11 原 ≙TK-T11 映射悬空：testkit.md §9 任务表仅 TK-T01~T10，§6.5 冻结的 TestProcessRunner 实现任务约定"届时补登 TK-T11"，映射已从 §2 该行移除以保证 ≙ 计数（73）与"六卡 73 任务"声明一致 | 触发条件满足时 WP-02-T11 无卡内编号可对接 | testkit.md §6.5/§9 vs 本文 §2 WP-02-T11 行 | testkit 卡（触发时补登 TK-T11 并恢复 ≙ 映射） | 登记 |
| O-34 | CI 常驻激活前置（WP-01-T02 登记）：CI 模板已固化于 `scripts/industrialrobot/ci/`（与 eb777e2 逐字节一致），但落位目标路径在框架树/仓库根（`RobWork/gitlab-ci/`、`.github/workflows/`）——SA-02 须先补丁登记；且模板引用的入口脚本为 `scripts/industrial-robot/`（连字符）与现行 `scripts/industrialrobot/` 命名不一致，需裁决统一 | CI 六步门禁（ARCH §11.2-2 常驻）无法实际接入 Runner；本地 gate-all.ps1 为过渡强制门槛 | WP-01-T02 产物 ci/README.md 差异表 | 构建约定所有者＋所有者（扩契约＋补丁登记＋命名裁决后复制落位） | 登记 |

### 4.3 待冻结前置（REQUIREMENTS 附录 C 状态同步）

| # | 前置项 | 承载任务 | 冻结前约束 | 状态 |
| --- | --- | --- | --- | --- |
| O-27 | P-03 七轴模板工程数值 | WP-13-T07（仅登记不启用；模板启用前冻结） | 七轴模板不进入 R1 交付 | 未冻结（附录 C） |
| O-28 | P-04 扰动/鲁棒性协议（已正式延期至 OPT-D 启用前） | WP-21-T01（硬前置） | 未冻结不得启用联合优化（v1.13 决议） | 已登记延期 |
| O-29 | P-06 轨迹碰撞复检协议数值（步长/细分预算/代表点集） | WP-16-T02（阶段 C 启用前硬前置） | 未冻结不得启用 TRJ-04 复检 | 未冻结（附录 C） |
| O-30 | P-02 需求追踪表格式契约 | WP-00-T02 | 追踪矩阵以本文 §3 为前身 | 未冻结（附录 C） |

### 4.4 本文辖域已定稿/消账项（构建约定所有者＝本文，ARCH §11.1）

| 事项 | 定稿结论 | 消账对象 |
| --- | --- | --- |
| `_test` 目标 gtest 接入机制 | §5.5：vcpkg 安装＋`find_package(GTest CONFIG REQUIRED)` 唯一机制；失败即停不回落。**首次安装执行：2026-09-10 随 WP-03-T01（≙CORE-T01）安装 `gtest:x64-windows@1.18.0`（installed/x64-windows，动态库）并两模式接入成功** | core.md P-ENV-2、testkit.md P-TK-1（及 D-01 失实记载更正）——两卡下次增量修订正式关闭 |
| 策略编辑命令处理器适配器宿主（P-POL-9） | **ui 单元承载**（L5 应用壳装配期装配注入；policy 仅供纯函数解析/校验/编码）——WP-10-T07 落地 | policy.md §15.3、project.md §6.5 引用处；ui 卡产出时承接冻结 |
| 局部任务号与 WP 分配对齐（P-TK-3 及同型） | §2"≙"映射登记制：`WPnn-Tkk ≙ <UNIT>-Txx`，不重排卡内编号、不双轨 | testkit.md §12.2 及各卡同型条目 |
| testkit 红线扩展建议接收（policy.md §3.4 P-POL） | R-5（proximity 直链禁止）入 §4.5 例外登记册＋门禁（WP-01-T01）；ARCH §3.2 补登行归架构所有者（O-03 同批） | policy.md POL-T01 引用处 |
| project.md 完整性争议（P-EV-6/P-POL-6） | 2026-09-10 实测 project.md §1~§15 完整（runtime.md P-RT-9 同结论）——两卡登记已过时 | evidence.md/policy.md 下次增量修订消账 |

### 4.5 红线例外登记册（WP-01-T03 维护；未登记例外被门禁检出即构建失败）

| 红线 | 例外范围 | 依据 | 状态 | 登记日期 |
| --- | --- | --- | --- | --- |
| R-3（Qt 禁入计算核心） | ui 单元界面目标（Widgets 唯一例外） | ARCH §3.2（SA-10 既有） | 预登记（WP-10-T02 生效） | 2026-09-10 |
| R-4（前缀拼接/剥离禁止） | runtime 名称解析器实现文件清单 | ARCH §3.2、§7.4 | 预登记（清单随 WP-06-T13 提交转生效） | 2026-09-10 |
| R-4（同上） | policy 策略消费点（只读消费整名，禁拼接/剥离） | P-POL-8（O-12 措辞确认后） | 待确认 | 2026-09-10 |
| R-5（proximity 直链禁止） | policy 产品实现（碰撞唯一实现） | policy.md §3.4；AT-19 | 生效（WP-07-T01 起） | 2026-09-10 |
| R-5（同上） | 各 `_test`/`_contract_test` 经 testkit/policy 替身消费 | §5.3；POL-T11 | 生效 | 2026-09-10 |
| T-1/T-2（testkit 分发/依赖） | 无例外 | testkit.md §2.4 | — | 2026-09-10 |

### 4.6 L1 基线库目标名实测表（各单元落位任务核对后回登；零 Qt 库方可被 L2 引用）

| 目标 | 用途 | 引用单元（落位任务核对） |
| --- | --- | --- |
| `sdurw_math` | rw::math 数学类型 | core（WP-03-T01）、policy（WP-07-T01） |
| `sdurw_kinematics` | Frame/State/StateStructure | runtime（WP-06-T01）、policy |
| `sdurw_models` | WorkCell/Device/Joint | runtime、policy |
| `sdurw_proximity` | 碰撞后端接口 | policy（R-5 例外唯一产品引用方） |
| `sdurwsim` | DynamicWorkCell/刚体动力学 | runtime（WP-06-T01）、dynamics（WP-17-T02） |
| `sdurw_pathplanners` | 路径规划 | trajectory（WP-16-T06；业务域经各自计算库消费） |
| `sdurw_loaders` | XML 解析 | 原则不使用（io/modeling 导入通道自行实现安全解析；若启用须登记） |

### 4.7 验收发现跟踪登记册（v0.9；findings.json 建册，配合 PIPE v1.3 §7 发现闭环）

验收产出的**建议级问题**一律逐条转登 `traceability/findings.json`（编号 F-xxx 顺延；字段：来源、severity、状态 open/fixed、处置、责任方）——本表（§4.1~4.6）继续承载待裁决/例外/定稿类登记，findings.json 专管"有人提出、需有人销"的修复类发现；每份验收记录 4.8 核对未关闭条目。先例补登：CORE-T01 验收 G-1~G-4 → F-001~F-004（F-001~003 随 v0.9 批次修复，F-004 留构建所有者）。

---

## 5. 执行约定

### 5.1 构建与目标约定

| 项 | 约定 |
| --- | --- |
| 双模式 | 集成模式＝仓库根 `build/` 构建树＋`RWS_BUILD_INDUSTRIALROBOT=ON`（patch 0001 守卫，唯一交付口径）；独立冒烟模式＝`industrialrobot/` 单独配置（仅验证目标注册与 include 路径）。两模式结论分别留痕，仅冒烟通过不构成完成 |
| 目标命名 | 库 `sdurws_ird_<unit>`、别名 `RWS::ird::<unit>`（冒烟模式无别名）；`_test`/`_contract_test`/`_plugin`/`_worker` 随单元任务登记，不预建空目标；INTERFACE→真实库升级时目标名不变 |
| C++ 标准 | 各目标显式 `CXX_STANDARD 17`；不用 C++20（core.md D-01；C++11 基线混链安全性由各 T01 双模式验证——P-ENV-1，失败回落 C++14 走设计变更） |
| 第三方依赖 | 一律经 vcpkg（仓库根、经典模式、无 manifest）；禁源码 vendor 与第二渠道；新增依赖先在本文增量修订登记 |
| Qt | 仅 L3 及以上按层规则允许（ui 界面目标 Widgets 唯一例外，§4.5）；L2 计算内核与业务计算库零 Qt（R-3） |
| 语言/工具链 | MSVC 2022 x64（现有构建树）；门禁脚本用 CMake 脚本模式（`cmake -P`）＋git，零 Python |
| 治理脚本口径（v0.9） | 调用方式 `pwsh -File`（pwsh 7.6.5 已装）或 `powershell -File`（5.1 系统自带）均可——脚本对仓库根做三段自检解析（显式 -RepoRoot > $PSScriptRoot > MyInvocation），与调用方式无关（findings F-001 修复口径）；脚本清单：validate-docs / validate-task / verify-task（含 -DryRun）/ validate-state（流水线状态，PIPE §0.2）/ pipeline-lock（流水线 tick 原子锁，PIPE §0.1，v0.10） |

### 5.2 统一完成定义（DoD，§2 全部任务的默认验收基线）

1. **构建**：双模式配置构建零错误（纯文档任务免除）。
2. **门禁**：`ird_gates` 对本次变更零命中（含新启用红线扩展）。
3. **验证**：任务"验收标准"列全部用例通过并**留痕**（gtest XML＋`ird-test-report.json`＋构建日志）；**任何未执行测试不得标注通过**；失败如实登记失败状态与原因。
4. **同步**：实现与任务卡（或本文任务行）零偏差，或偏差已按 §5.4 登记；待裁决项状态更新。

### 5.3 全局禁止项（每个任务的默认红线）

① 不继承、不恢复 `old/` 历史实现（且 old/ 磁盘不存在，O-01）；② L2 计算内核与业务计算库零 Qt；③ 业务域目标互链（R-1）、跨单元私有头（R-2）、RobWork 名称前缀拼接/剥离（R-4，例外见 §4.5）、proximity 直链（R-5）；④ 不修改其他单元公共头与上游文档正文（增量修订除外）；⑤ 框架零源码修改，先登记后改动（SA-02）；⑥ testkit 不随产品分发（T-1）、testkit 仅依赖 core＋标准库（T-2）；⑦ 不新建 WP-25 外编号；需求语义不得被任务收窄或扩大（验收一律回指 REQUIREMENTS/AT）。

### 5.4 文档-代码同步与偏差登记

| 情形 | 处置 |
| --- | --- |
| 实现与任务卡零偏差 | 照常登记完成 |
| 实现细节偏差（签名微调/文件名变化，不改语义） | 单元卡增量修订＋变更记录行 |
| 卡间/卡与上游冲突 | 在对应卡 §10/§12/§15 类待裁决表登记（带编号），所有者裁决后消账；本文 §4 同步跟踪 |
| 构建约定变化（gtest/门禁/命名等本文辖域） | 本文增量修订先行，实现随新版执行 |

### 5.5 测试框架接入（定稿）

googletest **经 vcpkg 安装**（`vcpkg install gtest:x64-windows`，经典模式），industrialrobot 全部 `_test`/`_contract_test` 目标统一经 `find_package(GTest CONFIG REQUIRED)` 接入：失败即停（REQUIRED），不静默跳过；不要求框架开启 `USE_gtest`（保持 OFF）、不消费从未生成的 `RW::gtest`、不源码 vendor、不混用两份 gtest；测试 main 自持（`GTest::gtest_main` 或 testkit RecordListener 适配）；沿用框架 `ADD_RW_GTEST` 宏语义（`add_test`＋`<target>_report` XML 报告目标），宏定义放各单元 CMake；首次安装的版本登记于 §4.4 定稿行（随增量修订更新）。测试用例命名带需求/AT 追溯字段（`ird-test-report.json` 承载）。

### 5.6 提交与分支约定

| 项 | 约定 |
| --- | --- |
| 分支 | 一切工作基于 `redesign-main`——**main 为旧主分支，已冻结，禁止一切新提交**（分支红线全文见仓库根 `AGENTS.md` §6.2）；实现任务用短生命周期分支 `wp<nn>-t<kk>`（如 `wp03-t02`），DoD 达成后合入 `redesign-main`；文档修订可直接在 `redesign-main` 小步提交 |
| 提交信息 | `[WP-nn-Tkk] <摘要>`；文档修订 `[DTB] v0.x: <摘要>`；代码与其测试同一提交 |
| 完成登记 | 任务完成状态随实现提交或紧随提交登记（本表 §2 状态或治理日志），不积压 |
| 框架改动 | 先更新 `patches/PATCHES.md`＋出 patch 文件，后改动（SA-02 顺序）；门禁按登记核对 |

### 5.7 会话上下文交接最小信息（AI 实现工作流）

一次实现会话的输入固定为：①本文该任务行（含 DoD/禁止项）；②其"输入"列指向的文档章节（任务卡行/上游条款）；③其前置任务的产出清单（公共头/目标/数据集）。**会话流程为三段式（v0.6，详见 acceptance-protocol.md）**：实施段——实施者单会话领取单份 ready 契约，在 `wp<nn>-t<kk>` 分支完成实现→双模式构建→`ird_gates`→用例执行留痕→文档同步→commit 并推送，发出验收请求，**不得自行合入 redesign-main**；验收段——独立上下文的验收者按 acceptance-protocol.md 对抗式核查（重跑构建与 verify、红线与注释规范、测试真实失败能力），产出 pass/fail＋证据写入 traceability/acceptance/；合入段——pass 后由所有者决策合入，分歧与语义拿不准按 §4 登记待裁决。任何一步失败停在原地登记，不带病前进；发现上游缺陷按 §5.4 登记，不私自裁决。所有持久状态落文档与代码，不依赖会话间记忆。

---

## 6. 自检清单核对结果（v0.2 交付时逐项核对）

- [x] **WP-00~25 全部有映射**：26/26 包均有任务（WP-00:2、WP-01:3、WP-02:11、WP-03:10、WP-04:20、WP-05:12、WP-06:13、WP-07:12、WP-08:10、WP-09:6、WP-10:11、WP-11:7、WP-12:9、WP-13:19、WP-14:9、WP-15:17、WP-16:16、WP-17:10、WP-18:5、WP-19:12、WP-20:11、WP-21:8、WP-22:13、WP-23:9、WP-24:7、WP-25:4）；无"不产出实现任务"的包；无超出 WP-25 的新增申请。
- [x] **需求追溯非空且 ID 真实**：§3 覆盖矩阵对账 180 条主需求＋12 分期子项＋2 分级子级全部覆盖、零沉默遗漏（R2/D/E 条目按发布标记分期承接，RV-04）；各任务追溯列引用的 ID 均存在于 v1.16 正文（AT 引用限于 §21 定义 38 条）。
- [x] **任务独立可验证、接口先于消费**：六卡单元沿用卡内依赖闭包（各卡自审记录确认）；无卡单元 T01（卡）→T02（落位）→实现的显式链；平台端口冻结（M2）先于业务域；优化/轨迹经③端口消费 kinematics 评估器的依赖已显式（WP-20-T04、WP-16-T05 依赖 WP-15-T04）。
- [x] **L2 零 Qt**：core/testkit/evidence/runtime/policy/drivetrain 及全部业务计算库的落位任务验收含 R-3 扫描（WP-03-T01 等 20 个落位任务）；ui 唯一例外经 §4.5 预登记。
- [x] **缺详设单元均有"编写任务卡"前置**：14/14（§4.1 缺失清单 ↔ §2 各 T01 一一对应）；另 WP-21-T02（optimization OPT-D 扩展）补位。
- [x] **20 单元 INTERFACE→真实库均有显式任务**：core WP-03-T01｜testkit WP-02-T01｜project WP-04-T01｜evidence WP-05-T01｜runtime WP-06-T01｜policy WP-07-T01｜execution WP-08-T02｜diagnostics WP-09-T02｜ui WP-10-T02｜io WP-11-T02｜reporting WP-12-T02｜modeling WP-13-T02｜requirements WP-14-T02｜kinematics WP-15-T02｜trajectory WP-16-T03｜dynamics WP-17-T02｜drivetrain WP-18-T02｜selection WP-19-T02｜optimization WP-20-T02｜workflow WP-22-T02——20/20。
- [x] **无任务要求继承/恢复 old/**：全局禁止项 §5.3-①；old/ 磁盘不存在（O-01）已登记勘误请求。

**统计**：任务总数 **266**（其中单元任务卡映射 ≙ 73 项、新增登记 193 项：含 14 张卡编写＋3 项冻结/登记文档任务）；WP 覆盖 26/26；需求 ID 覆盖率 **180/180＋12/12＋2/2**；无前置可立即启动任务：**WP-00-T01、WP-00-T02、WP-01-T01、WP-02-T01、WP-03-T01、WP-04-T01、WP-05-T01、WP-06-T01、WP-07-T01、WP-09-T01**（10 个起点，另六卡内链式任务随各自起点解锁）；开放问题 **33 项登记**（O-01~O-33）＋5 项本文辖域定稿消账（§4.4）。

---

## 7. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-09-10 | 首版（WP 登记汇总＋构建约定结构）：WP-00~25 登记、WP-01 横切任务、DoD、阶段 A 批次、gtest/门禁定稿、登记册与实测登记 |
| v0.2 | 2026-09-10 | 重写为全量任务卡结构：§1 单元依赖 DAG＋里程碑 M0~M7；§2 266 张任务卡（WPnn-Tkk 两级编号，六卡 73 任务经"≙"映射、14 缺卡单元各设编写卡任务、20 单元落位任务全覆盖）；§3 需求覆盖矩阵（180＋12＋2 全覆盖零沉默遗漏）；§4 开放问题 30 项（O-01~O-30，只登记不裁决）＋例外登记册＋L1 目标实测表；§5 执行约定（DoD/禁止项/同步规则/gtest 定稿/提交与会话交接）；§6 自检核对留痕。收编 v0.1 的构建约定与实测结论，v0.1 结构性内容（WP 登记表、批 0~4 顺序）由 §1/§2 取代 |
| v0.3 | 2026-09-10 | 同步 11/20 详设完成、五项编写任务产物、全量单元任务索引、编码分批准入；不改变需求或宣布实现通过。 |
| v0.4 | 2026-09-10 | §5.6 分支约定增量同步：main 冻结、唯一开发主线为 `redesign-main`，任务分支 DoD 后合入 `redesign-main`——消除与仓库根 AGENTS.md §6.2 分支红线的矛盾；不改变其他章节语义。 |
| v0.5 | 2026-09-10 | 全链一致性审计消账：①§0.3 准入顺序机器化——TK/EV/RT/POL-T01 canonical 契约编码 dependsOn=["CORE-T01"]（配套 readiness 同步），§0.3 表述消除"仅为 CORE-T01"与"ready＋空前置可领取"的自相矛盾；②§0.3 服务侧计数修正（DIAG-T01/UI-T01 已 done，非"74 份均 planned"）；③WP-02-T11 悬空 ≙TK-T11 映射移除并登记 O-33（≙ 计数回归 73，与"六卡 73 任务"声明一致）；④O-31（ui.md 依赖三方矛盾）、O-32（PILOT/DEL 四条需求无架构 ID 级落点）登记入 §4.2；§6 统计更新为 O-01~O-33。不裁决 O-31/O-32/O-33，留对应所有者。 |
| v0.6 | 2026-09-10 | 三段式实施流程确立：§5.7 会话流程由单会话循环改为**实施段（实施者，不得自行合入）→验收段（独立上下文验收者，对抗式核查）→合入段（所有者决策）**；新建 acceptance-protocol.md（文档代号 ACC，10 项验收清单、独立性要求、pass/fail 判据与 traceability/acceptance/ 记录格式）；AGENTS.md §6.4 同步改写。目的：实施/验收/裁决三权分立，消除"自实现自验证自宣布完成"的确认偏差。 |
| v0.7 | 2026-09-10 | §4.4 gtest 定稿行回填首次安装版本：随 WP-03-T01（≙CORE-T01）实施段安装 `gtest:x64-windows@1.18.0` 并两模式接入成功（留痕 traceability/builds/wp03-t01/）。仅版本登记，不改 §5.5 机制本身。 |
| v0.7 | 2026-09-10 | 横切契约补建与契约家族归属登记：①新建 tasks/ 根目录四份 canonical 契约——WP-00-T01（governance-log，ready）、WP-00-T02（追踪矩阵格式契约，ready＋dependsOn）、WP-01-T01（ird_gates 门禁，ready＋dependsOn CORE-T01，验收含 O-21 消账/O-12 处置）、WP-01-T02（CI 一键门禁，ready＋dependsOn）——消除 §6"无前置可启动"四任务与机器无契约的失配；②§8 登记契约家族归属（foundation/＝单元任务、tasks/ 根＝治理 DOC 族＋横切 WP 族，编号以本文为唯一来源不双轨）；③validate-task/validate-docs 的 taskId 句法扩展接受 WP-nn-Tkk；④foundation-tasks.json 入口索引 4 份 T01 的 dependsOn 同步为 ["CORE-T01"]（与 canonical 一致）。编码轨 CORE-T01 进行中，本修订与其 allowedFiles 零交集。〔与同日另一 v0.7 行（gtest 版本登记）为并行小步修订，合称 v0.7 批次〕 |
| v0.8 | 2026-09-10 | 自动化流水线登记：新建 automation-pipeline.md（文档代号 PIPE v1.0）——六态状态机（paused/idle/implementing/awaiting_acceptance/awaiting_merge/blocked）、五守卫（避让进行中会话、禁触 main、契约前置校验、异常即停）、tick 流程（实施/验收分别用全新子代理，验收者只给产物不给叙述）、返工熔断（maxFixCycles=3）与所有者授权分级（autoMerge 默认关闭，预授权须记录于 state.json policy）；状态载体 traceability/pipeline/state.json（初始 paused——CORE-T01 手动会话占用工作树）。§8 登记其地位：三段式流程的编排自动化，不改变任何纪律。 |
| v0.9 | 2026-09-10 | 流水线审核修复登记（配合 PIPE v1.3/ACC v1.2/新建 CCP v1.0）：①§8 契约编译协议与发现闭环登记（ready 契约唯一产出通道＝CCP；findings.json 强制转登）；②§4.7 新建验收发现跟踪登记册（CORE-T01 G-1~G-4 补登为 F-001~F-004）；③§5.1 增治理脚本调用口径行（调用方式无关＋脚本清单含 validate-state）；④配套数据修复：6 份契约 designRefs 锚点补全/修正（DOC-T01、DOC-T03、FOUNDATION-CR-01、WP-00-T02 #附录C→#24、DIAG-T01、UI-T01），state.json 豁免收编 policy 字段＋心跳 ISO 格式。不改变任何需求语义与任务范围。 |
| v0.10 | 2026-09-10 | 二轮审查修复登记（配合 PIPE v1.4/ACC v1.3/CCP v1.1）：①§8 增"并发与合入安全"段（原子锁/run 租约/SHA 冻结链/结构化 queue）；②§5.1 脚本清单补 pipeline-lock.ps1；③配套数据：10 份 ready/done 契约补 branch/interUnit/knownPitfalls（跨单元判定留痕于 traceability/contract-compile-log.md 补录批次），state.json queue 结构化＋leaseMinutes/firstUnitCheckpointExempted/tickCount 落位。流水线维持 paused，等所有者复查后恢复。不改变任何需求语义与任务范围。 |
| v0.11 | 2026-09-10 | 三轮审查修复登记（配合 PIPE v1.5/ACC v1.4）：①锁 fencing token（release/renew 必须 -Token 核对，旧 tick 超时被接管后无法释放新锁）；②state 增 tickToken 侧写＋心跳时钟健全性（≤now+5min，防未来时间戳）与单调性（≥run.startedAt）校验——2026-09-10 实录两类事故（无锁心跳写入/未来时间戳）均被检出；③queue 真读校验（契约本体 taskId/branch 与条目一致＋逐份过 validate-task）；④evidence 分支按尝试隔离 acc/<taskId>/<attempt>（attempts.accept 计数），acceptanceRecord.commit 为 40 位不可变 SHA，收尾双漂移检测（任务分支＋evidence 分支）后按 SHA 合并；⑤in-flight 三态统一强制 branch/base/队首一致。另登记：审查期间发现并行会话无锁写心跳（分钟精度格式溯源）——该会话须停止直写 state 或改走 tick 协议，恢复流水线前必须 retire。维持 paused。 |
| v0.12 | 2026-09-10 | 四轮审查修复登记（配合 PIPE v1.6/ACC v1.5）：①pipeline-lock 的 status/acquire/renew/release 全过程由按绝对仓库路径派生的 Windows 命名 mutex 串行，fencing 核对与修改锁文件不再存在 TOCTOU；②编排者在每次阻塞等待返回后再次 renew，失败即停止，禁止过期 tick 处理完成事件或写 state；③state.json 与 validate-state 增 lastFailureRecord{branch,path,commit,attempt}，验收 fail 的不可变 evidence 记录只在返工 implementing 态传给实施者，首次实施/其他阶段强制为空；④ACC §5 遗留 acc/<taskId> 表述修正为带 attempt 的分支。配套 test-pipeline-pipeline.ps1 覆盖 mutex、旧 token 接管和失败证据 schema 回归。维持 paused。 |
| v0.13 | 2026-09-10 | WP-00-T01 执行（governance-log 建立）：新建 doc/industrial-robot-design/governance-log.md——11 张单元卡全部 93 项 P-\* 待裁决项与本文 §4 O-01~O-33 的集中流转登记（编号/所有者/状态/消账留痕；逐卡零丢失矩阵；与 phase-one-readiness §2 口径核对一致；只登记不裁决）；本文 §4 头部初始化集中流转视图指向。不裁决任何 P-\*/O-\* 项，需求/架构语义零修改。 |
| v0.14 | 2026-09-10 | WP-01-T02 执行（本地一键门禁＋CI 模板）：①新建 `scripts/industrialrobot/gate-all.ps1`——ird_gates＋双模式构建＋全部 `_test`/`_contract_test` 目标一键执行（CTestTestfile 递归发现；零 Python；CI 建成前为强制门槛），首跑 9/9 全绿；②新建 `scripts/industrialrobot/ci/`——eb777e2 双 CI 文件逐字节固化（缓存回退键/双 ini 模板供给/六条门禁行）＋差异说明 README；③§4.2 登记 O-34（CI 激活前置：框架文件落位须补丁登记＋入口脚本命名裁决，归构建约定所有者）。F-007 的 ini 模板半边已由 CI 模板内嵌供给消解（AGENTS §4.1 文档同步项仍开放）。 |








## 8. AI 子任务执行约定

AI 只能领取 `doc/industrial-robot-design/tasks/` 下（含 foundation/ 子目录）状态为 `ready` 的单份执行契约；索引 JSON 不是执行契约。**契约家族与编号归属（v0.7）**：单元任务契约位于 `tasks/foundation/`（taskId＝卡内编号，经 §2 ≙ 映射对应 WPnn-Tkk）；治理/横切任务契约位于 `tasks/` 根目录——治理文档族（DOC-T01~T03、FOUNDATION-CR-01，主 WP-A 的过程任务，无 §2 独立行）与横切任务族（WP-00-T01/T02、WP-01-T01/T02，2026-09-10 补建，taskId 即 §2 登记 WPnn-Tkk 编号）——两族均以本文 §2/§0.1 的登记为唯一编号来源，不双轨。单元任务行与 canonical 的前置必须同时满足。任务必须先通过 `scripts/industrialrobot/validate-task.ps1`，完成后运行 `verify-task.ps1`，并同步 `traceability/` 中的状态和映射。需求或架构语义发生变化时，任务必须转为 `blocked`，不得在代码中自行解释或修改上游文档。**自动化流水线（v0.8）**：三段式流程可经 automation-pipeline.md 编排自动化（定时 tick＋子代理实施/验收，状态载体 `traceability/pipeline/state.json`）——守卫避让进行中会话、返工熔断、合并决策默认所有者门控（预授权须记录于 state.json 的 policy）；流水线不改变本节任何纪律。

**契约编译与发现闭环（v0.9）**：ready 契约的唯一产出通道是 `contract-compilation.md`（文档代号 CCP）——单元卡任务行按其 §2 逐字段门槛与 §3 七步编译放行（requirements 非空、designRefs 带真实锚点、acceptance 逐条可验证、`branch`/`interUnit` 必填、跨单元任务 `knownPitfalls` 显式携带已登记陷阱），占位契约不得领取；validate-task.ps1 三查（前置引用/设计锚点/knownPitfalls）是其机器执行面；编译评审留痕于 `traceability/contract-compile-log.md`。验收建议级问题强制转登 `traceability/findings.json`（§4.7），发现必须闭环不得只留档。

**并发与合入安全（v0.12，PIPE v1.6）**：tick 并发的唯一裁判是 `.git/ird-pipeline-tick.lock` 原子锁；pipeline-lock.ps1 以绝对仓库路径派生 Windows 命名 mutex，将 status/acquire/renew/release 的读判写完整串行，再以 O_EXCL 创建作文件层兜底，旧 tick 的 fencing 核对与修改锁文件之间不存在 TOCTOU。实施/验收工作者以 run 租约标识，未超时只汇报不续派；阻塞等待返回后必须再次 renew 才能处理完成事件或写 state。验收对象以 40 位 SHA 冻结（base/headSha/acceptedHead 链），合入按 acceptedHead 执行且先验证远端分支未漂移；验收 fail 的不可变记录以 lastFailureRecord{branch,path,commit,attempt} 只传给返工 implementing 工作者。state.queue 为结构化条目 {taskId,contractPath,branch}（branch 唯一来源在契约）。
