# drivetrain 单元详细设计（L2 共享计算服务）

| 字段 | 值 |
| --- | --- |
| 单元 | drivetrain（**L2 共享计算服务**——ARCHITECTURE §2.3/§3.1：非业务域单元、非 L4 插件；dynamics 与 selection 之间的唯一共享传动计算服务） |
| 文档版本 | v0.1（2026-10-06，首版草案，WP-18-T01 交付物） |
| 文档状态 | **`Draft`**（未冻结；本文只做详细设计，不自行宣布任何验收通过） |
| 主 WP | WP-18（DYN-04 唯一映射实现；DYN-04 映射不归 dynamics 自行实现） |
| 上游 | `REQUIREMENTS.md` v1.16（`Accepted`，唯一需求权威）、`ARCHITECTURE.md` v0.13（`Draft`）、`development-task-breakdown.md` v0.53 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/`（**当前仅公共头占位 README，尚未落位**——见 §1.2/§4.1） |
| 实现口径 | 从头构建（REQUIREMENTS v1.9 确立）：不继承、不恢复 `old/` 历史实现 |
| 任务编号 | 本卡决策 `D-DT-x`；待裁决项 `P-DT-x`；诊断码前缀 `DT-`（建议值，注册归 diagnostics StableCodeRegistry） |

> **文档地位**：本文是 drivetrain 单元的唯一详细设计。依据 ARCHITECTURE §3.1（drivetrain 行：DriveTrainMappingEvaluator 唯一实现——虚功映射/交叉耦合/工作点/效率/反射惯量/能量分项）、§3.2/§3.5（L2 层规则与依赖白名单：**drivetrain → core, evidence**）、§7.2③（评估器端口：选型消费传动映射）、§7.10（传动映射契约，M-12 口径）与 REQUIREMENTS DYN-04/SEL-05/MDL-21，把传动输入快照与身份、R1 对角映射、R2 耦合矩阵、虚功/功率一致性、反射惯量、效率/能量/四象限、电机工作点、公共接口、验证方案与任务拆分写到可直接实现的深度。本文不修改 REQUIREMENTS/ARCHITECTURE/其他单元卡；发现上游冲突一律登记待裁决（§18），不擅自解决。

## 目录

- §1 文档信息、输入版本与当前代码状态
- §2 需求承接、R1/R2 边界与单元定位
- §3 职责边界、依赖与端口
- §4 当前代码落位与目标布局
- §5 传动输入快照、身份与归一化模型
- §6 R1 旋转对角传动映射
- §7 R2 耦合矩阵映射（MDL-21/DYN-04/AT-38）
- §8 虚功一致性与功率一致性
- §9 反射惯量
- §10 效率、功率、能量与四象限工作制
- §11 电机工作点与多工况包络
- §12 与 dynamics、selection、evidence、execution、reporting 的协作
- §13 公共接口与线程生命周期
- §14 验证方案与故障注入矩阵
- §15 阶段 C 实现任务拆分（WP-18-T01～T05）
- §16 R2 阶段 D 承接与接口交接清单
- §17 需求—设计—验证追踪矩阵
- §18 设计决策、风险、待裁决项与变更记录
- §19 交付前自审
- §20 未执行的实现测试、构建与 GUI 测试

---

## 1. 文档信息、输入版本与当前代码状态

### 1.1 输入文件实测台账（2026-10-06 磁盘实况）

| 输入 | 磁盘版本/状态 | 本卡消费点 |
| --- | --- | --- |
| `REQUIREMENTS.md` | v1.16（2026-09-09 签署，`Accepted`；C1～C8 签署后变更已留痕） | DYN-03/04、SEL-05/09/10、MDL-12/16/21/22、CON-01～06、EVI-01/02（§8.1 表 1～4）、TASK-01～03、ERR-01、OPT-02/03/05/06/07、NFR-COR-01/02/03、NFR-MNT-01/03/04/07、NFR-PERF-02/04～06、NFR-REL-02/03、AT-07/08/19/27/30/34/36/38、附录 D（P-01 冻结产物） |
| `ARCHITECTURE.md` | v0.13（2026-09-27，`Draft`） | §2.3（L2 层规则、零 Qt）、§3.1（drivetrain＝共享计算服务）、§3.2（四红线）、§3.5（依赖表：drivetrain→core,evidence）、§4（进程/线程/任务状态机/迟到结果）、§7.2③（评估器端口）、§7.3（编译链）、§7.6（切片/包络/当前性）、§7.10（传动映射契约 M-12）、§10.2（R2 不新增架构机制） |
| `DETAILED-DESIGN.md` | 索引（2026-09-10/22 状态行）：drivetrain＝**L2 共享计算服务**（非业务域，2026-09-10 修正注记）、状态"待产出"、主 WP-G | 单元分类与"单元详设最低结构"要求 |
| `development-task-breakdown.md` | v0.53 | §2.19 WP-18-T01～T05（本卡交付物与后续任务）、§4.2 O-07/O-11、§5 执行约定 |
| `traceability/unit-status.json` | drivetrain：`status=planned`、`designCompletion=not-written`、`masterWps=[WP-G]` | 落位状态记录（本卡产出后其刷新归治理任务，本卡不代改） |
| `units/core.md` | v0.11（contract-review） | ObjectId/ContentIdentity/ContentDigester、TaskIdentity 五元组、EvaluationMode/TaskOutcome/EngineeringStatus 词表、DiagCode 句法承载、Tolerance/比较工具 |
| `units/evidence.md` | v1.3（contract-review） | AnalysisSnapshot/InputSlice（§4.2 字段表）、依赖七类与声明协议（§4.2.1/4.2.3）、失效矩阵（§5.3）、EvidenceItem/RequiredEvidenceProfile（§6）、ResultEnvelope 合法组合（§7）、IEngineeringEvaluator/EvaluatorDescriptor/IEvaluationContext/注册表（§9）、调用流程（§10） |
| `units/runtime.md` | v0.17（contract-review） | §4.3.4：CanonicalModel 承载 `drivetrain.ratioPerJoint`（合法：正有限值）与 `drivetrain.coupling`（R2；方阵/可逆/条件数 ≤1×10⁸＝P-RT-7 设计默认；病态→编译期 InputInvalid 阻止）；§10.8/§13.2：dynamics/drivetrain 交接行（tryDynamicWorkCell、coupling/ratio 视图）；§13.1 全链承接行 |
| `units/modeling.md` | v0.39（Draft） | §4.7 `robot-drivetrain` 对象（DrivetrainDesign：传动比、耦合矩阵（R2）、摩擦、力矩限值、选型回填字段）；I-MDL-11（ratio 有限＞0；R2 下 C 方阵/可逆/条件数）、I-MDL-12（R1 下 coupling 禁止配置，`MDL-21-COUPLING-STAGE-LOCKED`）；§8.1（映射算法归 drivetrain 唯一实现）；`apply-drivetrain-design` 命令 |
| `units/trajectory.md` | v0.1（2026-10-06，**未入 git**——工作区未跟踪文件，如实登记） | 同为阶段 C 单元卡先例：结构模板、评估器/执行接入形态（§15.10～15.12）、上游卡缺失时的最小依赖契约写法 |
| `units/execution.md`、`units/policy.md`、`units/diagnostics.md`、`units/reporting.md` | 均已产出（v0.1 起，个别已多轮落位登记） | execution：TaskCapability 能力声明（暂停/检查点粒度/强制终止代价）、九态状态机、RunRegistry 两段式接纳；diagnostics：码命名空间（首段＝单元短前缀）、StableCodeRegistry；reporting：§11 章节 `drivetrain-operating-points`（C 级） |
| **`units/dynamics.md`** | **不存在**（待产出，WP-17-T01） | dynamics 为本卡**上游输入**（关节侧序列）的提供者。本卡按 execution/reporting 卡先例：在 §12.1 给**最小依赖契约**（提议 DTO＋依赖声明）并登记交接，**不代写对方详设** |
| **`units/selection.md`** | **不存在**（待产出，WP-19-T01） | selection 为本卡**下游消费**方。本卡在 §12.2 给最小消费契约并登记交接（SEL-05 组合校核、O-11 惯量比阈值归属在 selection 卡裁决） |
| **`units/optimization.md`** | **不存在**（待产出） | OPT-D 消费形态在 §16 以架构口径登记（ARCH §7.10/§15.0），不私设 optimization 内部契约 |

### 1.2 代码落位实测与缺失项登记（2026-10-06 实测）

| 检查项 | 实测结果 | 结论 |
| --- | --- | --- |
| `drivetrain/include/sdurws/ird/drivetrain/` | 仅 `README.md` 占位（"本目录为 drivetrain 单元公共头保留位……源码随对应任务卡（见 units/drivetrain.md §9）落地；本文件不参与编译"） | **未落位**。README 文案指向"卡 §9"与本卡章节编号不符（本卡实现任务＝§15）——占位文案偏差随 WP-18-T02 落位时修正，登记于 §18.3 |
| `drivetrain/src/`、`drivetrain/test/`、`drivetrain/contract_test/` | 不存在 | 未落位（WP-18-T02～T04 动作） |
| `drivetrain/CMakeLists.txt` | 不存在 | 未落位（WP-18-T02 动作） |
| 目标 `sdurws_ird_drivetrain` 及 `_test`/`_contract_test` | 上级 `industrialrobot/CMakeLists.txt` 未注册 | 未落位（WP-18-T02 动作） |
| `plugin/`、`worker/` 目录 | 不存在 | **符合本卡设计**——本架构不要求 drivetrain plugin/worker（§2.3/§4.2 明文承诺），不是缺失项 |

> 如实声明：当前代码只有 README 保留位，**没有** drivetrain CMake、源码、测试或 worker 实现；本卡是文档交付物（WP-18-T01），不把任务计划或 README 占位当作实现状态。

### 1.3 本卡与上游文档的编号口径

- 本卡决策编号 `D-DT-x`、待裁决项 `P-DT-x`（登记格式按 DTB §4.2：编号｜问题｜影响｜来源｜建议裁决者｜状态）；诊断码 `DT-*` 为**建议值**——码值分配与合法性权威＝diagnostics 的 StableCodeRegistry（core 仅承载 DiagCode 句法），diagnostics.md §4.5 命名空间清单补登 `DT` 前缀随本卡收编（P-DT-7）。
- 需求语义一律以 REQUIREMENTS 条目为自足定义（RV-13）：本卡不重定义、不收窄、不扩大；单元内部任务编号 DT-Txx 不使用（阶段 C 任务以 DTB WP-18-T01～T05 为准，§15 只做对齐展开）。

---

## 2. 需求承接、R1/R2 边界与单元定位

### 2.1 需求条目承接（逐条：承接什么 / 不承接什么）

| 需求 | 本卡承接 | 明确不承接 |
| --- | --- | --- |
| **DYN-04**（主责） | 唯一 `DriveTrainMappingEvaluator`：电机侧工作点（τ/ω/P 序列）、方向相关效率 η⁺/η⁻、反射惯量 J·i²、惯量比（数值事实）、能量分项、四象限工作制统计；映射动力学口径 M-12（τ_motor＝Cᵀ·τ_joint＋J_rotor·θ̈，θ̈＝C⁺·q̈；无耦合链等价于对角传动比映射；不对角化/不准静态/不丢交叉耦合项；C 非常矩阵/病态→诊断并阻止） | 关节侧广义力计算（DYN-01 RNEA，归 dynamics）；转子等效惯量与壳体质量的合成规则（SEL-10 权威，本卡只消费"未计入合成"的转子字段并防重复计入） |
| DYN-03（上游口径） | 消费其输出的每轴转角/位移、转速、加速度、**类型化**广义力（转动 N·m／移动 N）、机械功率、峰值（含持续时间窗与所在段）与完整循环 RMS（含驻留）——电机侧统计按同口径生成 | 关节侧序列的产生、峰值/RMS 关节侧统计（WP-17） |
| SEL-05（支撑） | 经③评估器端口向 selection 提供同一口径电机侧工作点；selection 不重新实现映射 | 电机—减速器—负载组合校核、可行集与淘汰原因（WP-19）；**惯量比阈值**（可配置工程规则——归属 O-11 待裁决，本卡只输出惯量比数值，不做阈值判定） |
| SEL-09（边界） | 输入含移动关节轴时输出"范围外"稳定诊断（`DT-AXIS-TYPE-OUT-OF-SCOPE`），不静默套用旋转传动（与 §2.1 支持矩阵一致） | 直线传动目录模板与选型筛选（SEL-09-S1，R2，归 selection＋本卡 R2 扩展点，§16） |
| SEL-10（衔接） | 消费"区分壳体质量与转子等效惯性"后的转子字段（未计入 DWC 合成惯量）；映射结果绑定传动配置身份供回填复算提示 | 回填命令（①端口，selection/project）；合成规则本体 |
| MDL-21（R2 支撑） | R2 耦合矩阵的唯一映射消费实现（MDL-21 行：供运动学限位校验与 DYN-04/SEL 传动映射消费——映射侧归本卡） | 耦合矩阵的建模权威、编辑与持久化（modeling `robot-drivetrain.coupling`）；编译期校验（runtime，S3 编译门禁）；R1 成模阻断（modeling I-MDL-12） |
| MDL-12（边界） | R1 拒绝混合链输入（prismatic 轴→范围外诊断）；4/5 轴、mimic/planar/floating/闭环链不因本单元任何能力放开 | 链型判定与支持矩阵（modeling §2.1 持有）；MDL-12-S1 启用属跨单元任务（§16） |
| MDL-16（输入） | 消费关节额定/峰值力矩限值（工作点参考）、摩擦参数存在性标记（摩擦本身由 DYN-02 在关节侧计入，本卡不重复建模） | 摩擦参数的建模采集入口（modeling）；RNEA 内的摩擦力计算（dynamics） |
| MDL-22（间接） | 关节侧序列已按编译链重力投影产生（DYN-01 消费 R_world_base）——本卡不做任何二次重力/基座变换（M-11 单一不变量的下游纪律） | 基座—世界变换（runtime 编译权威） |
| CON-01～06 | 输入冻结经 AnalysisSnapshot/InputSlice（CON-01）；结果完整性/当前性/工程判定三态正交不自判（CON-02）；缓存/检查点兼容按切片身份与契约版本（CON-04）；失效与当前性基于切片内容身份而非整个项目修订（CON-05）；策略/名称身份经快照携带（CON-06——本卡不直接消费 CollisionPolicy；运行时名称必须先反解为对象 ID 才能接纳结果，反解由接纳层执行） | 快照/切片构造（evidence 所有）；归档写入（execution/project） |
| EVI-01/02 | 评估模式（Preview/Quick/Verified）与证据汇总消费全局唯一契约；映射评估声明自己的 RequiredEvidenceProfile；必验工况覆盖进入映射评估（逐工况计算，覆盖矩阵素材由本卡输出）；缺失输入 → DataInsufficient 素材，不伪造完整证据 | 证据等级、ResultEnvelope 当前性、正式工程判定（evidence 所有）；不新增证据等级/评估模式 |
| TASK-01～03 | 评估器声明能力（批处理/取消/检查点=不支持暂停）；取消后不发布完整结果；请求与完成事件携带五元组（projectId/branchId/revisionId/runId/attemptId）＋输入切片身份 | 任务状态机、调度、登记表核对（execution 所有） |
| ERR-01 | 稳定诊断码（`DT-*`）＋subjectObjectId＋局部/运行时名称＋原因＋建议动作；比较型校验含实际值/期望值/单位；不适用字段显式标记 | 码值注册表与文案权威（diagnostics）；显示口径（ui） |
| OPT-02/03/05/06/07（衔接） | `drivetrain.ratio` 为 StageB 连续变量（V12-02：可编辑并编译进候选）——本卡映射消费编译后的传动配置；OPT-D（R2）中映射作为内层评估服务被组合 | 候选搜索/Pareto/鲁棒性/误淘汰审计/暂停继续/检查点调度/缓存淘汰（optimization/execution）；StageB 不评估驱动性能（阶段锁） |
| NFR-COR-01/02/03 | 解析算例＋独立参考实现对照（黄金数据集，附录 D 第 9 项容差）；同输入同配置同种子→等价结果与稳定排序；非有限数/非法单位/引用缺失不得静默转 0 或默认通过 | 黄金数据集管理（testkit） |
| NFR-MNT-01/03/04/07 | 计算内核零 Qt、可由模型测试直接调用；单位换算唯一实现归 core（本卡不自建第二换算点）；不建无边界价值包装器/重复 DTO；不做名称前缀拼接/剥离 | — |
| NFR-PERF-02/04～06 | 取消协作（批次边界查询 cancellationRequested）；R2 大任务分批/流式；规模化达标口径归 WP-23 | 任务终止/进程治理（execution）；并行度与检查点调度（execution/optimization） |
| NFR-REL-02/03 | worker 崩溃只致当前任务失败（本卡计算内核在 worker 进程内被动执行）；中断结果不伪装完整 | worker 进程管理（execution） |
| AT-07/08/19/27/30/34/36/38 | 见 §17 追踪矩阵（逐 AT 设计落点） | 系统级验收执行（对应 WP 与验收会话） |

### 2.2 R1 / R2 边界表（阶段 C 承诺面）

**R1／阶段 C（本卡设计并承诺可实现的范围）**：

| # | R1 承诺 | 依据 |
| --- | --- | --- |
| R1-1 | 旋转传动；单轴传动比；多轴对角传动（等价于对角矩阵） | DYN-04"无耦合链等价于对角传动比映射"、SEL-09 |
| R1-2 | 关节侧位置/速度/加速度 → 电机侧映射；关节侧广义力 → 电机侧力矩映射（虚功对偶） | DYN-04、M-12、ARCH §7.10 |
| R1-3 | 理想虚功一致性检查与功率一致性检查（口径见 §8） | DYN-04 验收要点"传动映射黄金数据" |
| R1-4 | 正向/反向效率 η⁺/η⁻（方向相关）；再生制动；零功率分支 | DYN-04、AT-07"双向效率" |
| R1-5 | 反射惯量 J·i²；惯量比（数值事实，不含阈值判定） | DYN-04、SEL-05 |
| R1-6 | 电机侧转矩/速度/功率序列；峰值（带时间窗/轨迹段/工况）、RMS（完整循环含驻留）、工作点统计 | DYN-03 口径、DYN-04 |
| R1-7 | 能量分项（正功/再生/损耗，按工况与轨迹段）；四象限工作制统计 | DYN-04 |
| R1-8 | 负载惯量与组合工作点输入（值传递，来源标记） | SEL-05 |
| R1-9 | dynamics 与 selection 的稳定评估器端口消费（③端口注册，同一映射实现） | ARCH §7.2③/§7.10 |
| R1-10 | 映射版本、内容身份、诊断（`DT-*`）；必验工况覆盖素材输出 | CON-04/05、EVI-02 |

**R1 无耦合链的实现纪律**（对角形式不丢信息）：

- R1 的无耦合链表现为**对角传动矩阵**或等价的逐轴传动比，但必须：明确关节侧→电机侧方向（§5.3 全文唯一约定）；明确传动比定义与数值口径；明确正负号语义（§5.3——映射核心契约支持带符号传动比；经项目对象通道受 modeling I-MDL-11"ratio 有限＞0"约束，负号经 R2 矩阵或值传递通道表达，见 P-DT-5）；明确关节轴与电机轴排列（§5.3 轴序表）；明确旋转量单位（§5.4）；经虚功与功率黄金算例验证（§14.2 DT-G 组）。
- **不允许**把未定义的传动比方向默认为正；**不允许**因使用对角矩阵而丢失任何已定义的轴向符号（对角元素逐轴携带自身符号，见 D-DT-6）。

**R1 不启用（阻断面）**：

| 阻断项 | 阻断方式 | 依据 |
| --- | --- | --- |
| MDL-21 非对角耦合矩阵（含耦合链交叉项） | 映射入口结构检查：C 存在且非对角 → 稳定诊断 `DT-COUPLING-STAGE-LOCKED`＋`DT-MATRIX-NONDIAGONAL-LOCKED`，阻止映射（不静默拆成独立轴、不对角化绕过） | MDL-21（R2）、M-6、AT-38"R1 阻断反例" |
| mimic / 闭环链的静默等效 | 关节类型/链型输入检查 → `DT-AXIS-TYPE-OUT-OF-SCOPE`（mimic/闭环成模在 modeling 侧已被 R1 阻断，此处为映射侧第二道防线） | MDL-12、M-6 |
| R2 完整混合移动关节产品链 | prismatic 轴 → `DT-AXIS-TYPE-OUT-OF-SCOPE`（"范围外"），该轴不产出电机侧结果 | SEL-09、MDL-12 |
| 直线传动目录与工作点映射 | 不设计直线传动映射实现（扩展点见 §16.2） | SEL-09-S1（R2） |
| OPT-D 联合优化语义 | 不实现候选搜索/评估组合/暂停继续（本卡只是被组合的内层服务） | §15.0 OPT-B/D、OPT-05 |
| 大规模并行、暂停/继续、R2 性能达标 | R1 评估器能力声明：支持取消与分批流式，**不支持暂停**（能力声明显式反馈，不静默） | TASK-01、NFR-PERF-04~06（R2） |

**R2／阶段 D 承接**（详见 §16）：MDL-21 非对角常矩阵 C（含腕部窗口与块对角组合）、交叉耦合保留、高速多轴联动映射（AT-38）、θ̈_motor＝C⁺·q̈_joint 转子惯量项全量口径、SEL-09-S1/MDL-12-S1 直线传动与混合链扩展点、OPT-D 内层评估服务。**R2 不新增架构机制**（PA-7/ARCH §10.2：MDL-21＝"RobotDesign 权威参数扩展＋编译链＋drivetrain 虚功映射"；直线传动＝"drivetrain/selection 扩展"）。

### 2.3 单元定位（L2 共享计算服务，非 L4 业务插件）

```
L5  应用壳层        装配期注册（评估器工厂、注入接口）
L4  业务插件层      modeling │ requirements │ kinematics │ trajectory │ dynamics
                    selection │ optimization │ workflow          ← 互链禁止（R-1）
L3  平台服务层      project │ execution │ io │ reporting │ diagnostics │ ui
L2  计算内核层      core │ evidence │ policy │ runtime │ ★drivetrain(共享计算服务)
                    ＋ 各业务单元的"计算库"部分（零 Qt）
L1  框架基线        RobWork(rw) │ RobWorkSim(rwsim) │ RobWorkStudio(rws) │ Qt
```

- drivetrain 是 **L2 共享计算服务**：不是业务域插件，不进入 L4；不受 R-1"业务域互链禁令"的"业务域"一侧约束，但自身依赖白名单同样只有 core/evidence（ARCH §3.5，§3.2）。
- **零 Qt**（R-3/NFR-MNT-01）：不 include 任何 Qt 头；可被模型测试直接调用。
- **不要求 `drivetrain_plugin`**：drivetrain 无独立界面（工作点呈现由 dynamics/selection/reporting 的界面承载）。
- **不要求 `drivetrain_worker`**：独立 worker 由 execution 所有（ARCH §4.1：worker 进程复用产品代码基的计算内核；drivetrain 计算内核经评估器注册进入 worker 进程装配清单，worker 不属于 drivetrain）。若未来确需专用适配目标，必须登记为架构变更或 execution 装配决策（P-DT-8 通道），不在本卡默认创建。
- 不在 UI 线程执行大规模传动计算（评估必然经 execution 派发；单次映射为批量计算，无 <1 s 内联通道）。

---

## 3. 职责边界、依赖与端口

### 3.1 拥有／消费／不拥有表

| 类别 | 内容 |
| --- | --- |
| **拥有** | 传动映射算法（唯一实现）；已验证传动比的归一化计算视图（`DriveTrainModel` 规范化）；R1 无耦合/对角传动映射；R2 耦合矩阵映射（映射侧）；位置/速度/加速度映射；关节侧广义力→电机侧力矩映射（虚功对偶）；理想虚功一致性检查；功率一致性检查；交叉耦合保留（映射侧）；反射惯量计算；正向/反向效率模型的**消费与折算**（效率模型值本身归 modeling/目录，见下行"不拥有"）；电机侧工作点；机械功率与能量分项；四象限工作制统计；映射诊断（`DT-*` 建议码）；映射结果内容身份（评估器契约版本＋切片身份承载）；`DriveTrainMappingEvaluator` 注册描述（EvaluatorDescriptor）；传动事实结果 `DriveTrainEvidenceFacts`（事实 DTO，供 evidence/selection/reporting 消费） |
| **消费** | core 的单位、身份、版本包络、矩阵/容差比较与诊断契约；modeling/runtime 经稳定端口或**中立值对象**传入的传动配置（权威值在 `robot-drivetrain` 对象，经编译链与值传递进入切片）；dynamics 经上游结果传入的关节侧位置/速度/加速度/广义力/功率序列；evidence 的输入切片、评估器注册与结果包络契约；execution 的取消、批处理与 worker 上下文；diagnostics 的稳定诊断目录（码值注册）；selection 提供的器件能力引用与工作点消费契约（组合场景经配置条目值传递，§12.2）；必要的 RobWork 基础数学类型（零 Qt），不依赖 RobWorkStudio Widgets |
| **不拥有** | `RobotDesign` 编辑；传动参数的项目持久化（modeling/project）；`CanonicalModel` 编译（runtime）；`RuntimeSnapshot` 构造（runtime）；FK/IK/Jacobian（kinematics/runtime）；轨迹规划与时间参数化（trajectory）；逆动力学/正动力学（dynamics）；碰撞检测与 EngineeringPolicy（policy）；电机/减速器目录（selection/io）；器件硬筛选与组合校核（selection）；`DriveTrainDesign` 回填命令（selection→①端口）；optimization；evidence Profile 所有权之外的证据等级、结果当前性与正式工程判定（evidence）；TaskScheduler/worker 进程/缓存淘汰（execution）；项目事务/修订/结果归档（project）；CSV/JSON/XML 解析（io）；报告渲染（reporting）；Qt Widgets；全局命令与快捷键（ui）；R2 暂停/继续语义（execution/optimization） |

**旧提示词职责拆分说明（本卡正式口径）**：

- "传动参数规范"拆为两层：**modeling/runtime 拥有权威传动参数**（`robot-drivetrain` 对象与编译视图）；**drivetrain 只拥有归一化计算输入**（`DriveTrainMappingInput` 中立值对象）与映射结果。
- "传动结果证据明细"拆为两层：**evidence 拥有**证据等级、结果包络（ResultEnvelope）、当前性与工程判定；**drivetrain 只提供** `DriveTrainEvidenceFacts` 事实 DTO（映射事实＋输入身份＋诊断引用＋完整性信息）。drivetrain 内部**不新增**独立的 evidence Profile 权威、证据等级或正式判定。

### 3.2 依赖形态与红线自查（ARCH §3.5 口径）

**登记依赖边（dependency-graph.json 白名单，逐条核对）**：

| 边 | 形态 | 用途 |
| --- | --- | --- |
| drivetrain → core | 接口依赖 | ObjectId/ContentIdentity/ContentDigester、TaskIdentity、EvaluationMode/TaskOutcome/EngineeringStatus 词表、DiagCode 承载、Tolerance/比较工具、单位换算唯一实现 |
| drivetrain → evidence | 接口依赖 | IEngineeringEvaluator/EvaluatorDescriptor/IEvaluatorFactory、EvaluationRequest/Output、InputSlice/DependencyDeclaration、EvidenceItem/RequiredEvidenceProfile、ResultEnvelope 构造校验 |

**自查表**：

| 红线 | 自查结论 |
| --- | --- |
| R-1 业务目标互链禁止 | drivetrain 非 L4 业务单元；本卡不建立 drivetrain↔dynamics/selection/optimization/modeling 的任何编译链接边（消费全部经③端口/值传递/注入，§12） |
| R-2 跨单元私有头禁止 | 只 include core/evidence 公共头（`include/sdurws/ird/core|evidence/`）；不 include dynamics/selection/modeling/runtime 任何头（含公共头——依赖白名单未登记这些边，公共头也不行，A3 处置口径） |
| R-3 计算核心零 Qt | 零 Qt 模块；STATIC 库目标，模型测试直调（NFR-MNT-01） |
| R-4 名称前缀拼接/剥离禁止 | 不做任何 RobWork 名称前缀操作；对象名称呈现经⑥名称端口由消费方解析，本卡只携带 ObjectId |
| SA-02 框架零修改 | 不修改 RobWork/RWS/RWSim 源码，无 patch 登记 |
| 依赖白名单 | 表外同层边（含反向边 drivetrain→policy/runtime/diagnostics 与业务边）＝未登记依赖，构建失败（ARCH §3.5 门禁）——本卡全部消费通道据此设计为**值传递或注入**（§3.3/§12） |

**runtime 视图消费形态（O-07 承接，本卡推荐方案）**：ARCH §3.5 未登记 drivetrain→runtime 边，而 runtime.md §13.2 向 dynamics/drivetrain 交接快照视图（tryDynamicWorkCell、coupling/ratio 视图）。本卡推荐：**drivetrain 侧采用值传递形态**——传动配置从 `robot-drivetrain` 项目对象（切片 Object 条目）与组装方构造的中立值对象获得；runtime 供给身份要素（`runtime.model-identity`/`runtime.robwork-baseline`）按 evidence §4.2.1 既有机制经组装方值传递进入 Environment 条目（消费 CanonicalModel 编译产物的评估必填）；**不新增依赖边**。runtime 编译期对 C 的良态校验（方阵/可逆/条件数，P-RT-7）与映射入口的结构检查互为两道防线（§7.3），drivetrain 不重复实现编译。dynamics 侧对 tryDynamicWorkCell 的消费形态由 dynamics 卡（WP-17-T01）起草，与本卡推荐一并提交架构所有者裁决（P-DT-1，状态：登记待裁决）。

### 3.3 端口协作总图

```
 modeling(权威传动参数 robot-drivetrain 对象)              requirements(负载工况/必验工况)
        │ ①命令端口 apply-drivetrain-design                       │
        ▼                                                        ▼
 runtime 确定性编译(CanonicalModel: ratioPerJoint/coupling(R2)/良态校验 P-RT-7)
        │                                                  evidence(③端口/快照/切片/包络/当前性)
        ▼                                                        ▲
 ┌─ 组装方（消费域：dynamics 域评估 / selection 组合校核 / 优化候选编译链）──┐   │
 │  构造 DriveTrainMappingInput 中立值对象(5.2)：传动配置+轴序+零位偏置    │   │ 评估器注册
 │  ＋效率模型引用＋负载折算惯量(来源标记)                                │   │ （L5 装配，
 │  ＋关节侧序列引用（dynamics 上游结果 UpstreamResultRef）              │   │  主/worker 同清单）
 └──────────────────────────┬───────────────────────────────────┘   │
                            ▼ InputSlice（sliceId＝缓存键/失效判据）   │
              ┌────────────────────────────────┐                    │
              │ DriveTrainMappingEvaluator     │（实现 evidence::IEngineeringEvaluator）
              │  键 "dt.mapping"（③评估器端口） │────评估输出────────┘
              │  归一化→映射→一致性→工作点→事实  │ → EvaluationOutput{evidence/payload/diagnostics}
              └────────────────────────────────┘
                            │ 电机侧工作点/统计/映射事实（同一矩阵内容身份）
        ┌───────────────────┼───────────────────────┐
        ▼                   ▼                       ▼
  selection(SEL-05     dynamics 域结果链        reporting 章节 11
  组合校核/淘汰原因)    (DYN-04 消费链)          drivetrain-operating-points
  optimization(OPT-D   (R2)                     （限定语保留、逐字段一致）
  内层评估服务)
  execution：任务提交/派发/取消/登记表核对（五元组＋切片身份）；worker 进程装配本评估器；崩溃只致当前任务失败
```

---

## 4. 当前代码落位与目标布局

### 4.1 当前落位（如实登记，2026-10-06 实测）

| 项 | 状态 |
| --- | --- |
| `include/sdurws/ird/drivetrain/README.md` | 存在（公共头保留位；文案指向"卡 §9"与本卡 §15 实际任务章不符——§18.3 登记随 T02 修正） |
| `CMakeLists.txt` / `src/` / `test/` / `contract_test/` | 不存在 |
| 上级目标注册 | 无 `sdurws_ird_drivetrain` 及测试目标 |
| plugin/worker | 不存在（**符合设计**——本架构不要求，§2.3） |

### 4.2 目标布局（全部为设计；落位动作归 WP-18-T02 及后续任务）

```
drivetrain/
├── include/sdurws/ird/drivetrain/
│   ├── MappingTypes.hpp        # DriveTrainMappingInput/JointDriveAxis/MotorDriveAxis/
│   │                           # TransmissionRatio/CouplingMatrix/EfficiencyModel/
│   │                           # RotorInertiaModel/DriveTrainModel（归一化）——§5
│   ├── MappingCore.hpp         # IDriveTrainMappingEvaluator（映射核心注入契约）＋步骤接口
│   │                           # （ITransmissionInputValidator/ICouplingMatrixValidator/
│   │                           # IVirtualWorkConsistencyChecker/IReflectedInertiaEvaluator/
│   │                           # IEfficiencyEvaluator/IMotorOperatingPointEvaluator）——§13
│   ├── Series.hpp              # DriveTrainMappingSample/MotorOperatingPoint/DriveTrainSeries/
│   │                           # DriveTrainEnvelope/DriveTrainValidity——§10/§11
│   ├── Facts.hpp               # DriveTrainEvidenceFacts（事实 DTO）＋
│   │                           # IDriveTrainEvidenceFactsProvider——§12.3/§13
│   └── Evaluator.hpp           # DriveTrainMappingEvaluator final : evidence::IEngineeringEvaluator
│                               # （③端口注册面；评估键 dt.mapping）——§13.10
├── src/                        # 唯一实现（映射核心/校验器/统计/评估器适配）
├── test/                       # 单元测试（黄金数据集 dt-*，模型测试直调——NFR-MNT-01）
├── contract_test/              # 契约测试（③端口注册/切片依赖声明/与 evidence 契约联动）
└── CMakeLists.txt              # sdurws_ird_drivetrain（STATIC，C++17，PUBLIC 链 core＋evidence；
                                # 零 Qt；红线扫描守卫）；测试目标 _test/_contract_test（DTB §5.5 gtest 接入）
```

| 目标 | 形态 | 依据 |
| --- | --- | --- |
| `sdurws_ird_drivetrain` | STATIC（别名 `RWS::ird::drivetrain`）；PUBLIC 链接 `sdurws_ird_core`＋`sdurws_ird_evidence`；CXX_STANDARD 17 | ARCH §3.5（依赖仅 core+evidence）、DTB WP-18-T02、NFR-MNT-01 |
| `sdurws_ird_drivetrain_test` | gtest 单元测试（黄金数据集 `testdata/golden/dt-*`） | DTB WP-18-T04 |
| `sdurws_ird_drivetrain_contract_test` | 契约测试（评估器注册/依赖闭包/切片联动） | 同上 |
| ~~`sdurws_ird_drivetrain_plugin`~~ | **不创建**（默认禁止） | §2.3；如需 → P-DT-8 架构变更通道 |
| ~~`sdurws_ird_drivetrain_worker`~~ | **不创建**（默认禁止；worker 归 execution 所有） | §2.3；同上 |

---

## 5. 传动输入快照、身份与归一化模型

### 5.1 输入来源与冻结链

| 输入 | 来源（权威所有者） | 进入方式 | 冻结点 |
| --- | --- | --- | --- |
| 权威传动配置（传动比、R2 耦合矩阵、效率、转子惯量、力矩限值） | modeling（`robot-drivetrain` 对象，MDL-16/21）；runtime（编译视图与良态校验） | 切片 **Object 条目**（`model.drivetrain`，ObjectId+ContentVersion）＋组装方构造的中立值对象（值传递——O-07 推荐形态，§3.2） | 快照组装时（AnalysisSnapshot 冻结）→ 切片派发前（SliceBuilder 冻结 sliceId） |
| 关节侧时间序列（q/q̇/q̈/τ_joint/P_joint、时间戳、轨迹段、工况、工具与负载身份） | dynamics（DYN-01/03，RNEA 关节侧精确计算） | 切片 **UpstreamResult 条目**（dynamics 评估结果切片身份，跨运行依赖、当前性传播消费——evidence §4.2.1） | dynamics 运行终结归档时；本卡评估派发前冻结引用 |
| 工况与负载身份（必验工况、负载工况、工具/负载对象） | requirements/evidence（REQ-04、EVI-02） | 关节侧序列样本携带（逐工况分组）＋负载折算惯量值对象（来源标记） | 随上游结果与切片冻结 |
| 任务与运行身份 | execution（TASK-03） | TaskIdentity 五元组（projectId/branchId/revisionId/runId/attemptId，派发时绑定） | 派发时（evaluationRequest.task） |
| 结果归档身份 | project/evidence（CON-01/05） | sliceId（缓存键与失效判据）＋runId 归档位置（登记表核对取自登记记录，ARCH §4.5） | 接纳时 |
| 求解/统计配置（采样对齐策略、统计开关等，如有） | 消费域 AnalysisConfiguration 子集 | 切片 **Configuration 条目**（`config.dt-mapping`，canonical 字节＋内容身份） | 切片冻结时 |

冻结链复用 evidence 既有机制（SnapshotBuilder→SliceBuilder→descriptor.inputs 声明→注册期闭包校验→冻结期子集校验），**不新建第二套快照/切片设施**。

### 5.2 核心类型设计（签名＝设计基线；实现任务允许按 DTB §5.4 微调并登记偏差）

```cpp
// 归一化传动模型：映射唯一消费形态（权威参数的规范化计算视图，区别于 modeling 权威存储）。
// 零 Qt；纯值语义；构造入口完成结构校验（非法即 fail-fast，见 §13.0）。
struct DriveTrainModel {
    std::vector<JointDriveAxis>  jointAxes;    // 关节轴表（按关节串联序，见 §5.3 轴序表）
    std::vector<MotorDriveAxis>  motorAxes;    // 电机轴表（排列规则同 §5.3）
    // 矩阵/向量承载：采用已登记的 RobWork 零 Qt 数学类型（L1 基线；不放任何第三方矩阵库头进公共契约）。
    // 归一化满矩阵 C_hat（n_joints × n_motors）：R1＝对角（n_motors==n_joints）；
    // R2＝块对角组合（自由轴 diag(c) ⊕ 耦合窗口 C_w），见 §7.2
    Matrix  chat;                              // 无量纲（rad/rad 或 m/m——逐轴类型一致，§5.4）
    std::vector<TransmissionRatio> ratios;     // 对角口径逐轴视图（c_j＝chat(j,j)，含符号）
    std::optional<CouplingWindow>  window;     // R2 耦合窗口（R1 禁止出现——存在即 DT-COUPLING-STAGE-LOCKED）
    std::vector<EfficiencyModel>   efficiency; // 逐电机轴 η⁺/η⁻（来源标记；缺失→DataInsufficient 素材）
    std::vector<RotorInertiaModel> rotor;      // 逐电机轴转子等效惯量（未计入 DWC 合成，§9.4）
    std::vector<double> zeroOffsetMotor;       // 电机零位偏置 θ_off（rad；q_joint=0 对应的电机角）
    DriveTrainIdentity identity;               // 内容身份（§5.5）
};
```

```cpp
struct JointDriveAxis {                        // 关节轴（身份与类型，权威来自建模链）
    core::ObjectId jointId;                    // 关节对象 ID（ARC-04 稳定 ID，不经名称匹配）
    JointKind kind;                            // Revolute / Continuous(已确认工作范围) / Prismatic
                                               // —— Prismatic 在 R1 触发 DT-AXIS-TYPE-OUT-OF-SCOPE
    std::string localName;                     // 运行时局部名（诊断呈现用；经⑥端口反解由接纳层保证）
};
struct MotorDriveAxis {                        // 电机轴（R1：与旋转关节一一对应）
    core::ObjectId motorId;                    // 电机轴对象 ID（建模侧分配；R1 可与 jointId 同源派生）
    std::size_t jointIndex;                    // 对应关节轴下标（一一对应；R2 窗口内经 C 结构表达）
};
struct TransmissionRatio {                     // ★带符号传动比（c 口径，§5.3 全文唯一约定）
    double c;                                  // c＝Δq_joint/Δθ_motor（无量纲 rad/rad；0 非法）
    SourcedValueTag source;                    // 来源标记：ModelingField/CatalogBackfill/ConfigEntry
};
struct CouplingMatrix {                        // R2（MDL-21）：常矩阵 C
    Matrix C;                                  // rows＝适用关节（窗口内串联序）；cols＝对应电机轴
    std::vector<core::ObjectId> jointRange;    // 适用关节窗口（有序；与 rows 一致）
    double conditionNumber;                    // 编译期/映射期检查用（阈值来源 P-RT-7 对齐，§7.3）
};
struct EfficiencyModel {                       // 方向相关效率（值对象；权威值归 modeling/目录）
    double etaForward;                         // η⁺ 驱动方向（电机→负载），合法 (0,1]
    double etaBackward;                        // η⁻ 再生方向（负载→电机），合法 (0,1]
    SourcedValueTag source;                    // 含 Estimated（估算）标记——进入结果限定语
};
struct RotorInertiaModel {                     // 电机转子等效惯量（**未计入** DWC 合成惯量，§9.4）
    double rotorInertia;                       // kg·m²（电机轴系）；必须＞0 且有限
    SourcedValueTag source;                    // SEL-10 回填区分壳体/转子字段（CatalogBackfill 等）
};
struct DriveTrainIdentity {                    // 传动配置内容身份（进入映射结果身份链）
    core::ContentIdentity drivetrainObjectCv;  // robot-drivetrain 对象内容版本身份
    std::uint32_t algorithmVersion;            // 映射算法版本（与评估器 contractVersion 联动，§5.5）
    std::uint32_t contractVersion;             // 输入/输出契约版本（进入 sliceId，CON-04）
};
```

> 实现注记：矩阵与向量类型采用已登记的 RobWork 零 Qt 数学类型（L1 基线，经 DTB §4.6 L1 基线库目标名实测表核对后引用）；上表 `Matrix` 为占位记号，实现任务落位时按 L1 实测登记替换，属 DTB §5.4 允许的实现微调。

### 5.3 方向、符号与轴序（★全文唯一约定，禁止混用第二种约定）

**映射方向与传动比定义（唯一权威公式，与 ARCH §7.10/MDL-21 冻结口径一致）**：

```
位置/速度/加速度（常矩阵 C）：   Δq_joint = C · Δθ_motor        q̇ = C·θ̇   q̈ = C·θ̈
力矩（理想虚功对偶）：           τ_motor = Cᵀ · τ_joint
含转子惯量（映射动力学）：       τ_motor = Cᵀ·τ_joint + J_rotor·θ̈_motor，θ̈_motor = C⁺·q̈_joint
```

- **传动比 c 的定义**：`c ＝ Δq_joint / Δθ_motor`（关节位移增量／电机位移增量，无量纲）。减速器 n:1（n 为常规工程减速比，电机快、关节慢）对应 **c＝1/n**。对角情形 `C＝diag(c_1..c_n)`。
- **符号**：c＞0 表示电机与关节同向；c＜0 表示反向。**映射核心契约支持带符号 c**（值传递通道可达；c＝0 非法——`DT-RATIO-ZERO`，除零无意义）。经项目对象通道（R1）受 modeling I-MDL-11"ratio 有限＞0"约束——负传动比在 R1 项目数据中不可达，需要时经 R2 矩阵（C 元素可负）或值传递通道表达，登记 P-DT-5。
- **与 "J·i²" 的关系（DYN-04 原文记法）**：i＝减速比＝1/c（c 口径换算），故对角反射惯量 `J_ref＝J_rotor/c²`（§9.2）——避免出现两套未说明的约定；正文中出现 "i" 处均按此换算。
- **零位偏置**：矩阵映射作用于**增量**（MDL-21 口径）。电机绝对角＝`θ_off ＋ (C⁻¹·(q−q_ref))_motor`（q_ref 为关节参考位，默认 0；θ_off 为 q=q_ref 对应电机角，rad）。偏置只影响电机**绝对位置**序列，不影响速度/加速度/力矩/功率；θ_off 进入归一化模型身份（保守方向，宁可多算不可错复用——evidence §5.1 等价纪律）。
- **轴排序**：关节轴按**关节串联序**（基座→法兰，modeling 关节表权威序）；电机轴按"对应关节串联序"排列（R1 一一对应；R2 窗口内电机轴按窗口内关节序）。两侧顺序不一致的输入 → `DT-INPUT-AXIS-ORDER-MISMATCH`（不允许静默重排——重排等价于改输入，必须由组装方显式完成并通过身份体现）。

| 表 | 行 | 列 | 维度规则 |
| --- | --- | --- | --- |
| C（及归一化 Ĉ） | 适用关节（窗口内串联序；R1＝全部关节） | 对应电机轴（同序） | 必须方阵（n_joints×n_motors，n 相等）——非方阵 `DT-MATRIX-NONSQUARE` 阻止（R2；MDL-21/runtime 口径） |
| τ_motor、θ_motor | 电机轴 | — | 与 C 列一致 |
| τ_joint、q_joint | 关节轴（串联序） | — | 与 C 行一致 |
| J_rotor | 电机轴（对角） | — | 逐轴标量；R2 交叉惯量项经 Ĉ⁻¹ 映射呈现（§9.3） |

### 5.4 物理量单位表（ERR-01/AT-27：单位进入诊断比较字段；显示单位不进入计算身份）

| 物理量 | 单位 | 说明 |
| --- | --- | --- |
| 旋转关节位置 / 电机位置 | rad | 关节 q、电机 θ（绝对角含零位偏置；增量映射见 §5.3） |
| 旋转速度 / 电机速度 | rad/s | — |
| 旋转加速度 / 电机加速度 | rad/s² | — |
| 关节力矩 / 电机力矩 | N·m | 类型化广义力（DYN-03：转动 N·m） |
| 移动关节位移 / 速度 / 加速度 | m / m/s / m/s² | R1 范围外（诊断）；R2 扩展点（§16.2） |
| 移动关节广义力 | N | 类型化（DYN-03：移动 N）；R2 直线传动承接 |
| 转动惯量（转子/反射/负载折算） | kg·m² | 电机轴系或关节轴系按字段显式标注（§9） |
| 功率 | W | 机械功率（关节侧 P_joint、电机侧 P_motor） |
| 能量 | J | 分项积分（§10.5） |
| 效率 η⁺/η⁻ | 无量纲 | 合法 (0,1]（§10.2） |
| 传动比 c、惯量比 | 无量纲 | c＝Δq_joint/Δθ_motor；惯量比定义见 §9.5 |

单位换算唯一实现归 core（SA-12/NFR-MNT-03）：本卡不自建第二换算点；显示单位（deg/mm 等）只是投影（KIN-12/AT-27），**不进入**计算、身份与缓存键。

### 5.5 身份、缓存键与结果失效

- **身份链**（全部字节等值比较，evidence §5.1"三种等价"纪律；数值容差**严禁**作为身份等价关系）：
  - `evaluatorContractVersion`（EvaluatorDescriptor）：映射输入/输出契约版本——**同输入不同契约版本＝不同切片**（CON-04）。
  - 映射算法版本（`algorithmVersion`）：随实现演进登记于本卡 §18.4 变更记录；变更＝装配清单变更（新版本新清单），运行期无热替换。
  - 矩阵版本：`robot-drivetrain` 对象 ContentVersion（对象不可变＋内容寻址——任一字段变更必然新版本）。
  - 效率模型版本：效率值来源对象的 ContentVersion（或 Configuration 条目内容身份）。
  - 切片条目：Object（`model.drivetrain`）＋UpstreamResult（dynamics 关节侧结果切片身份）＋Configuration（`config.dt-mapping`）＋Environment（评估契约版本等复现要素；`runtime.model-identity`/`runtime.robwork-baseline` 经组装方值传递取得——evidence §4.2.1）。
  - `sliceId`＝全部冻结条目的规范编码摘要（SHA-256，core ContentDigester 唯一哈希路径）＝**缓存键与失效判据**；`inputBaselineId`＝基准子集（"同一冻结输入复评"凭据，C5）。
- **迟到结果**：完成事件携带五元组＋输入切片身份；登记表核对（execution 两段式）通过后按登记记录归档，当前性由 evidence 对比结果切片身份 vs 当前 HEAD 切片身份独立判定（Current/Superseded）——**不以整个项目修订判断当前性**（CON-05）。
- **失效语义（对齐 evidence §5.3 失效矩阵）**：传动配置（ratio/coupling/效率/转子）变更 → 凡声明消费 `model.drivetrain` 的切片失效（本卡评估、消费其结果的选型/优化条目）；目录能力曲线变更 → 动力学侧关节侧结果不失效（DYN-04"关节侧与传动无关"），选型侧工作点/淘汰条目失效；dynamics 上游结果失效 → 传动映射结果经 UpstreamResult 依赖**传播失效**（当前性传播消费）。
- **显示单位、界面切换、会话姿态不进入任何切片条目**（EV-CUR-1 同款纪律）。

### 5.6 关节空间与电机空间的内容身份（输入身份分离）

关节侧序列身份（dynamics 上游切片身份）与传动配置身份（`model.drivetrain` ContentVersion）是**两个独立条目**：任一变化 → sliceId 变化 → 本卡评估结果失效；只变其一时另一侧结果保留为历史证据（CON-02）。同一项目不同分支天然隔离（projectId/branchId 在五元组与归档位置中）；不同工具/负载/轨迹/工况经上游序列的工况分组与负载身份携带——任一变化使依赖其切片的结果失效。

---

## 6. R1 旋转对角传动映射

### 6.1 R1 对角映射与 R2 耦合矩阵对比图

```
            R1（阶段 C，本卡承诺）                     R2（阶段 D，§7/§16 承接）
 关节侧                电机侧                    关节侧                  电机侧
 q_j(t) ── ×(1/c_j) ──► θ_j(t)              q_w(t) ── C_w⁻¹ ──► θ_w(t)   （腕部窗口，交叉耦合）
 q̇ ────── ×(1/c_j) ──► θ̇                    q̇_w  ── C_w⁻¹ ──► θ̇_w
 q̈ ────── ×(1/c_j) ──► θ̈                    q̈_w  ── C_w⁺ ───► θ̈_w（结构检查通过后，§7.4）
 τ_j ───── ×c_j ─────► τ_m（虚功对偶）       τ_w ── C_wᵀ ───► τ_w,m ＋ J_rotor·θ̈_w,m
        （逐轴独立，无交叉项）                          （轴间交叉耦合保留，不对角化）
 阻断面：非对角输入→DT-MATRIX-NONDIAGONAL-LOCKED；mimic/闭环/prismatic→DT-AXIS-TYPE-OUT-OF-SCOPE
```

### 6.2 R1 映射定义（逐样本、逐轴）

对归一化模型 `Ĉ＝diag(c_1..c_n)`（c 带符号，§5.3；R1 项目通道 c＞0），对每个采样时刻 t 与每轴 j：

| 映射 | 公式 | 说明 |
| --- | --- | --- |
| 电机位置 | `θ_j(t) ＝ θ_off,j ＋ q_j(t)/c_j` | 含零位偏置（§5.3）；只影响绝对位置 |
| 电机速度 | `θ̇_j(t) ＝ q̇_j(t)/c_j` | — |
| 电机加速度 | `θ̈_j(t) ＝ q̈_j(t)/c_j` | — |
| 电机力矩（理想） | `τ_m,j(t) ＝ c_j · τ_j(t)` | 虚功对偶 diag 情形（τ_m＝Ĉᵀτ_j） |
| 电机力矩（含转子项） | `τ_m,j(t) ＝ c_j·τ_j(t) ＋ J_rotor,j·θ̈_j(t)` | M-12 全量口径（转子项 R1 即计入——"无耦合链等价于对角传动比映射"指含转子项的完整口径；J_rotor 缺失→`DT-ROTOR-MISSING`→该轴力矩按理想口径输出并标 DataInsufficient 素材，见 §10.7） |
| 电机机械功率 | `P_m,j(t) ＝ τ_m,j(t)·θ̇_j(t)` | 含转子项贡献；效率折算见 §10.3 |
| 关节侧机械功率 | `P_j(t) ＝ τ_j(t)·q̇_j(t)` | 上游提供或由 τ·q̇ 复核（逐元素，§8.3） |

- **映射误差**：映射本身是封闭代数运算（无迭代、无近似），不存在算法截断误差；黄金算例对照容差按附录 D 第 9 项（标量相对 1×10⁻⁹，黄金算例可自带更严值并声明）。
- **数值容差来源**：运行时输入校验使用"非有限即拒绝"（NFR-COR-03，精确判据，无需阈值）；矩阵良态阈值见 §7.3（来源 P-RT-7）；一致性检查容差口径见 §8.4。
- **限位和工作范围交接**：关节限位（权威模型）与电机工作范围（如电机最大转速）的**校验归消费域**（运动学限位校验经映射执行属 R2/MDL-21 消费面，§16.1；selection 器件硬筛选消费工作点，§12.2）——本卡只产出电机侧序列与统计，不判定限位通过/超限（避免越权 second-guessing）。
- **结果身份**：映射输出绑定 `DriveTrainIdentity`＋上游切片身份＋评估器契约版本（§5.5）。

### 6.3 R1 阻断面（实现为映射入口结构检查的第一步，先于一切数值计算）

| 检查（顺序执行，首个命中即阻止） | 条件 | 稳定诊断（建议码） | 依据 |
| --- | --- | --- | --- |
| 能力门控 | 归一化模型含耦合窗口（R1 能力未启用，含"UI/配置中出现 R2 标签"的情形——**显示/配置标签不改变计算能力**，能力由装配清单与算法版本决定） | `DT-COUPLING-STAGE-LOCKED` | MDL-21（R2）、M-6、AT-38"R1 阻断反例" |
| 非对角矩阵 | Ĉ 任一非对角元素非零 | `DT-MATRIX-NONDIAGONAL-LOCKED` | 同上；不得对角化绕过、不得静默拆成独立轴 |
| 链型/关节类型 | mimic/闭环/planar/floating 关节、prismatic 轴 | `DT-AXIS-TYPE-OUT-OF-SCOPE` | SEL-09、MDL-12、§2.1 支持矩阵 |
| 结构有效性 | Ĉ 对角元素含 0/非有限；维度不匹配；轴序不一致 | `DT-RATIO-ZERO`/`DT-MATRIX-NONFINITE`/`DT-INPUT-DIMENSION-MISMATCH`/`DT-INPUT-AXIS-ORDER-MISMATCH` | NFR-COR-03 |
| 空输入 | 关节轴表空 / 上游序列空 | `DT-INPUT-EMPTY` | 空模型没有评估意义，fail-fast；**空输入不得发布完整结果**（EVI/TASK-02） |

阻断结果**不伪装成"传动不可行"**：阻断是能力/输入问题（调用方或阶段边界），产出比较型/定位型诊断并使该次评估按证据规则表达（数据不足或输入未完成，由消费域按 §8.1 表 2 汇总优先级判定）——工程不可行判定权在 evidence/各域（§12.3），本卡不自判。

---

## 7. R2 耦合矩阵映射（MDL-21/DYN-04/AT-38）

### 7.1 公式组（MDL-21/DYN-04/ARCH §7.10 冻结口径，本节为映射侧承接）

```
位置（增量）：      Δq_joint = C · Δθ_motor          （常矩阵 → q̇ = C·θ̇，q̈ = C·θ̈）
理想虚功对偶：      τ_motor = Cᵀ · τ_joint
含转子惯量项：      τ_motor = Cᵀ·τ_joint + J_rotor·θ̈_motor
其中：              θ̈_motor = C⁺ · q̈_joint
```

- **C 是常矩阵**：建模期不可含时变/工况项（modeling §8.1）；映射期发现时变结构（按工况变化的矩阵）→ `DT-MATRIX-TIME-VARYING-UNSUPPORTED`，**明确列为不适用，不得当作常矩阵计算**。
- **行列语义**：行＝适用关节（窗口内串联序）；列＝对应电机轴；C 须为**方阵**（维度＝适用关节数＝对应电机轴数）、有限、可逆、良态。
- **块对角组合（腕部窗口＋自由轴并存，AT-38 高速多轴联动场景）**：归一化 Ĉ＝diag(c_free) ⊕ C_w（自由轴逐轴对角，窗口内交叉耦合）。窗口与自由轴集合**不得重叠**（重叠 → `DT-INPUT-DIMENSION-MISMATCH`）；同一矩阵身份（ĉ 内容身份）覆盖位置/速度/加速度/力矩/功率全部映射。

### 7.2 必须阻止的矩阵形态（全部给出稳定诊断并阻止映射，不静默降级）

| 形态 | 诊断（建议码） | 说明 |
| --- | --- | --- |
| 非方矩阵 | `DT-MATRIX-NONSQUARE` | 电机轴数≠适用关节数（如差动/冗余驱动）——R2 按 MDL-21/runtime 口径不支持 |
| 奇异矩阵 | `DT-MATRIX-SINGULAR` | det≈0/不可逆——**不得以伪逆放行** |
| 病态矩阵 | `DT-MATRIX-ILL-CONDITIONED`（比较型：实际条件数/阈值/无量纲） | 条件数超限——阈值来源见 §7.3 |
| 非有限矩阵 | `DT-MATRIX-NONFINITE` | NaN/±Inf 任一元素（NFR-COR-03） |
| 维度不匹配 | `DT-INPUT-DIMENSION-MISMATCH` | 行列数与适用关节/电机轴集合不一致 |
| 时变矩阵 | `DT-MATRIX-TIME-VARYING-UNSUPPORTED` | 常矩阵前提破坏（§7.1） |
| 非对角（R1 收到） | `DT-MATRIX-NONDIAGONAL-LOCKED` | R1 阻断面（§6.3） |

### 7.3 矩阵良态检查与阈值来源（P-RT-7 对齐）

- 结构与维度检查：映射入口执行（精确判据，无阈值）。
- 可逆性与条件数：阈值复用 runtime 编译校验同一来源——**条件数 ≤1×10⁸（P-RT-7 设计默认，待策略侧确认归属）**。runtime 编译已阻止非法 C 进入 CanonicalModel（InputInvalid）；映射入口的检查是**第二道防线**（防御值传递通道与未来扩展），阈值必须与编译侧同源（若 P-RT-7 裁决改归 EngineeringPolicySet，本卡改经 Policy 条目声明消费——登记 P-DT-2，在裁决前按同一设计默认执行并留痕）。
- **伪逆的使用边界（M-12/提示词红线）**：只有在矩阵**已经通过**结构、维度、可逆、秩与条件性检查后，才允许以 `C⁺` 作为已批准公式（θ̈_motor＝C⁺·q̈_joint）中的**计算表示**（方阵良态下 C⁺≡C⁻¹）；**不得**以伪逆把非方/奇异/病态矩阵变成"可计算"，不得静默降级。
- **交叉耦合纪律**：不丢弃交叉项、不使用独立轴近似、不使用准静态近似、不得将耦合链转换为独立轴链绕过 R2 门禁；高速多轴联动必须保留交叉耦合（AT-38）。

### 7.4 θ̈_motor＝C⁺·q̈_joint 的使用前提（逐条）

1. C 已通过 §7.2 全部结构检查（方阵/有限/可逆/良态/常矩阵）；
2. 窗口内 q̈_joint 来自 dynamics 关节侧序列（RNEA 精确输出的派生量，非独立再计算）；
3. 转子惯量项 `J_rotor·θ̈_motor` 只加在**电机侧**力矩（§9.4 单一计入位置），关节侧 RNEA 输入不含电机转子反射效应（§9.4 防重复计入）；
4. 位置/速度/加速度/力矩/功率使用**同一矩阵内容身份**；dynamics、drivetrain、selection 三方消费**同一矩阵身份**（MDL-21 行/DYN-04/AT-38）——三方切片各自携带 `model.drivetrain` 对象 ContentVersion，接纳层核对该身份一致（契约测试 §14.2 DT-R2-13）。
5. R2 能力未启用（装配清单/算法版本为 R1）时同样拒绝耦合输入（§6.3 能力门控）。

---

## 8. 虚功一致性与功率一致性

### 8.1 理想几何/力矩映射的验证口径（虚功原理）

```
虚功：      δW_joint ＝ τ_jointᵀ · δq_joint        δW_motor ＝ τ_motorᵀ · δθ_motor
理想无损映射（常矩阵 C）：   δq_joint ＝ C·δθ_motor 且 τ_motor ＝ Cᵀ·τ_joint
            ⟹ δW_motor ＝ τ_jointᵀ·C·δθ_motor ＝ τ_jointᵀ·δq_joint ＝ δW_joint   （逐样本恒等）
```

理想映射（**不含效率、不含转子惯量项**的几何/力矩关系）下虚功严格相等——这是映射公式 τ_m＝Cᵀτ_j 的推导依据与黄金算例验证口径。

### 8.2 必须区分的四个口径（不得混用）

| 口径 | 内容 | 检查方式 |
| --- | --- | --- |
| ①理想几何/力矩映射 | τ_m＝Cᵀτ_j（不含损耗、不含转子项） | 黄金算例逐样本逐元素断言（测试对照容差，附录 D 第 9 项） |
| ②理想机械功率一致性 | P_motor_ideal ＝ τ_mᵀθ̇ ＝ τ_jᵀCθ̇ ＝ τ_jᵀq̇ ＝ P_joint（逐样本恒等） | 同上；**逐元素**（逐样本逐轴），不以总和/均值替代——防正负抵消（附录 D C4） |
| ③含效率后的功率平衡 | P_motor ＝ f_dir(P_joint)（§10.3 方向折算）；损耗 E_loss＝∫(P_motor−P_joint)dt＞0（§10.5 恒等式） | 能量守恒黄金算例（双向效率样例：正功段/再生段/混合循环） |
| ④含转子惯量项的电机侧力矩 | τ_m＝Cᵀτ_j＋J_rotor·θ̈（此时 P_m−P_joint＝转子功率＋传动损耗，不再逐样本等于零） | 转子项单独分项（§9.4/§10.5），一致性断言按"理想部分＋转子部分"分解核对 |

**不得把效率损耗引入理想虚功公式后再声称仍然严格相等**：①/②的恒等性只在理想口径成立；③/④是有损/含惯量口径，按能量分项核对，两者表述不得混写。

### 8.3 功率符号分类与机械功率范围

- **正功率（驱动）**：P_joint＞0——功率自电机流向负载，经正向效率 η⁺（§10.3）。
- **负功率（再生制动）**：P_joint＜0——功率自负载经传动流回电机侧，经反向效率 η⁻；再生功率/能量分项单独统计（§10.5）。
- **零功率**：P_joint＝0（精确符号判据，不需要阈值）——效率不适用（显式标记"不适用"，ERR-01 口径，不伪造数值），P_motor＝P_joint＝0，该样本不计入能量分项与四象限统计的有效样本集合（单独计数）。
- **机械功率与电气功率**：本卡只计算**机械**功率（关节侧/电机侧轴功率）。**电气功率（母线/电机电气端口）不属于本需求语义（REQUIREMENTS 未定义），明确不计算**；再生能量按机械再生功报告，不声明为可回馈电能——限定语在报告侧保留（RPT-05）。
- **摩擦**：关节侧摩擦（黏性+库仑，DYN-02）已由 RNEA 计入 τ_joint（dynamics 权威）；传动箱损耗仅经 η 折算（§10.3）。**两者是不同物理环节，摩擦不重复计入**（AT-07 验收要点）；本卡不叠加第二套摩擦模型（MDL-16 边界）。

### 8.4 一致性检查的实现形态与容差来源（诚实边界）

- 检查器 `IVirtualWorkConsistencyChecker`（§13.5）提供两类能力：**黄金数据集对照**（测试侧，附录 D 第 9 项：标量相对 1×10⁻⁹，逐例声明更严值；导出量 ε_abs 未声明即判"容差未定义"并报错——NFR-COR-03）与**映射自检**（实现内部对"独立供给的电机侧数据 vs 映射公式期望值"的逐元素对照——用于契约测试与诊断定位）。
- **产品运行侧不私设功率/虚功阈值**：附录 D 未冻结功率量纲的运行校验 ε_abs，且 C7 规定"无默认且无配置来源的量不得引入产品侧相对校验（新增须走需求变更）"。因此运行时的一致性异常只以两类形式出现：①精确判据（非有限/维度/结构——无需阈值）；②黄金算例对照（测试侧）。若未来需要运行时数值一致性校验阈值，走需求变更（P-DT-3 登记）。
- 检查失败输出：实际值、期望值、单位（ERR-01 比较型字段），逐元素定位（样本时刻＋轴）。

---

## 9. 反射惯量

### 9.1 反射惯量与转子惯量关系图

```
 电机轴系（motor side）                 关节轴系（joint side）
 ┌──────────────────┐   ×(1/c²)        ┌──────────────────────┐
 │ J_rotor（转子）   │ ───────────────► │ J_reflected ＝ J_rotor/c²   （对角 R1：J·i²，i＝1/c）
 │ （逐轴 kg·m²）    │ ◄─────────────── │                      │
 └──────────────────┘   ×c²            └──────────────────────┘
        ▲                                       ▲
        │ R2：θ̇＝C⁻¹q̇ ⟹ J_ref,joint ＝ (C⁻¹)ᵀ·J_rotor_diag·(C⁻¹)   （对称正定，含交叉项）
        │
  负载折算惯量 J_load@joint（组装方提供，来源标记；权威产生者 P-DT-4 待 dynamics 卡对齐）
        │ 折算到电机轴：J_load@motor ＝ c²·J_load@joint（对角）；R2 ＝ C⁻¹ᵀ·J_load·C⁻¹
        ▼
  惯量比（数值事实，逐轴）＝ J_load@motor / J_rotor ＝ c²·J_load@joint / J_rotor   （阈值判定归 selection，O-11）
```

### 9.2 R1 报告口径

- `J_reflected,j ＝ J_rotor,j / c_j²`（kg·m²，关节轴系）——即 DYN-04 记法 J·i²（i＝1/c，§5.3 换算）。
- 输入要求：J_rotor＞0 且有限（`DT-INERTIA-INVALID`：负值/零/非有限）；c≠0（§6.3）。

### 9.3 R2 耦合矩阵惯量口径

- 关节侧反射惯量矩阵：`J_ref ＝ (C⁻¹)ᵀ·diag(J_rotor)·(C⁻¹)`——对称正定（J_rotor≻0、C 可逆）；**输出完整矩阵（含交叉惯量项）**，另附逐轴对角视图供单轴消费。
- 对称性校验：‖J_ref−J_refᵀ‖ 精确为零（构造保证——按对称式计算）；正定性经 Cholesky 判定（失败 → `DT-INERTIA-NOT-POSITIVE-DEFINITE`，属输入/矩阵非法，阻止并诊断）。
- **反射惯量不默认对角化**：selection 只需单轴数值时消费对角视图，但交叉项保留在完整矩阵中随结果归档（**不因单轴消费丢弃交叉项**）。
- 电机侧合成功当量惯量（工作点统计用）：`J_eff,motor ＝ diag(J_rotor) ＋ C⁻¹ᵀ·J_load@joint·C⁻¹`（转子与反射负载之和，电机轴系）——用于加速度力矩分项（§10.4），不作为选型结论。

### 9.4 防重复计入（SEL-10/MDL-05/MDL-16 边界，单一计入位置）

| 纪律 | 内容 |
| --- | --- |
| 转子惯量唯一计入位置 | 电机侧映射公式显式项 `J_rotor·θ̈_motor`（M-12）。关节侧 RNEA 消费的 DWC 合成惯量**不得包含**电机转子反射效应——组装方按 SEL-10 合成规则取"转子等效惯性"**独立字段**（未计入合成），本卡消费该字段并在结果中携带其来源 ContentVersion |
| 壳体质量区分 | 壳体质量/惯量属连杆（MDL-05/MDL-16 合成，dynamics 消费）；本卡不消费、不折算壳体惯量 |
| 电机转子 ≠ 电机壳体 | 转子等效惯量是电机轴系转子值；禁止与电机壳体质量（经安装折入连杆物性）重复计入 |
| 一致性提示 | 组装方提供的 J_rotor 来源标记为 CatalogBackfill 时，结果携带"转子惯量按 SEL-10 回填字段独立计入"限定素材；无法验证 DWC 侧是否已含转子（跨单元事实）→ 登记为风险 R-DT-2，契约测试以"组装方契约"口径验证（黄金数据集以不含转子的 DWC 输入为前提） |

### 9.5 惯量比（数值事实，阈值判定不在本卡）

- 定义（对角）：`惯量比_j ＝ J_load@motor,j / J_rotor,j ＝ c_j²·J_load@joint,j / J_rotor,j`；R2 窗口轴：`（C⁻¹ᵀ·J_load·C⁻¹)_jj / J_rotor,j` 并附"窗口内存在交叉项、单轴比值为投影值"的限定标记。
- **本卡只输出数值**；惯量比阈值是可配置工程规则（SEL-05），归属待 O-11 裁决（EngineeringPolicySet vs selection 域配置）——本卡不定义、不内嵌任何阈值（P-DT-2 关联）。
- **反射惯量不是选型结论**：本卡输出的是映射事实；"电机是否合适"由 selection 按目录能力与工程规则判定（§12.2）。
- 反射惯量结果绑定：矩阵内容身份＋传动比＋转子模型 ContentVersion＋输入切片身份（任一变更即失效）。
- **新增惯量等效公式的门槛**：任何本节之外的等效公式（如含减速器自身惯量、丝杠折算）必须有需求或架构依据，否则列为待裁决项（P-DT 通道），不得实现。

---

## 10. 效率、功率、能量与四象限工作制

### 10.1 效率模型输入与合法性

- η⁺/η⁻ 逐电机轴值对象（§5.2 EfficiencyModel），来源标记 UserProvided/CatalogBackfill/ConfigEntry/**Estimated**。
- 合法域 (0,1]：η≤0 或 η＞1 → `DT-EFFICIENCY-INVALID`（比较型：实际值/期望范围/无量纲）；缺失 → 该轴效率不可用 → 电机侧功率/能量分项按**数据不足**处理（DataInsufficient 素材，§10.7），理想力矩映射（τ_m＝Cᵀτ_j）不受影响仍可输出（标注"未含效率"）。
- 效率模型**缺失时的结果状态**：映射事实（力矩/速度/位置）完整可用，功率/能量/四象限标记 DataInsufficient 素材——不伪造数值、不以 η＝1 静默替代。
- **不把传动损耗结果直接当作器件正式通过结论**（损耗小≠器件合格；筛选归 selection）。

### 10.2 方向选择（零功率与负功率的处理）

```
逐样本逐轴（P_joint＝τ_joint·q̇，关节侧机械功率）：
  P_joint ＞ 0  → 驱动方向：P_motor ＝ P_joint / η⁺        （电机侧输出更多机械功率，传动箱消耗 ΔP＞0）
  P_joint ＜ 0  → 再生方向：P_motor ＝ P_joint · η⁻        （|P_motor|＜|P_joint|，传动箱消耗 ΔP＞0）
  P_joint ＝ 0  → 精确零：效率不适用（显式标记），P_motor ＝ 0
```

- 方向判据是**精确符号测试**（不需要阈值；浮点意义下的"应零非零"由理想功率一致性黄金样例覆盖——附录 D C4"黄金数据集必含零值/近零值/正负抵消样例"）。
- η⁺/η⁻ 的选择按**能量流向**（P_joint 符号）而非转向符号（ω 的符号）——倒转运行（ω＜0）时电动/再生仍由功率符号决定，与四象限统计（§10.6）正交。
- 驻留段（q̇＝0，τ≠0 保持力矩）：P＝0，效率不适用；能量分项不计入正/负功，但计入时间与"保持"统计（RMS 输入含驻留，§10.4）。

### 10.3 电机侧机械功率（效率折算后）

`P_motor,j(t) ＝ f_dir(P_joint,j(t))`（上式）＋ 转子功率项 `P_rotor,j(t) ＝ J_rotor,j·θ̈_j(t)·θ̇_j(t)`（含转子项时电机侧总机械功率＝效率折算的传动功率＋转子加减速功率；分项分别报告，不混写）。

### 10.4 峰值、RMS 与负载率

| 统计 | 口径 |
| --- | --- |
| 峰值（τ/ω/P 逐轴） | 逐电机轴取 |x|(t) 最大者；**必须携带发生时刻、所在轨迹段、所属工况**（DYN-03 峰值窗口径的电机侧镜像）；正/负峰值分别报告（最大驱动转矩与最大再生转矩不同象限，不混取绝对值） |
| RMS（逐轴） | 基于完整任务循环（含驻留）——`RMS(x)＝√(∫x²dt/T)`，时间边界＝上游序列完整循环区间（DYN-03 口径）；驻留段计入口（力矩保持计入热负载） |
| 负载率 | RMS 转矩 / 额定转矩（额定值来自传动配置力矩限值字段，MDL-16；**参考值呈现**，硬筛选归 selection） |
| 加速/减速/再生区间 | 按逐样本符号组合标记区间（θ̈·θ̇＞0 加速、＜0 减速；P_joint＜0 再生），区间时长/能量分项输出 |
| 数据不足 | 循环不完整（缺样本/时间倒退）→ 该统计标 DataInsufficient 素材并给 `DT-INPUT-SAMPLE-MISSING`/`DT-INPUT-TIME-NONMONOTONIC`，不输出"部分 RMS 冒充完整循环 RMS" |

### 10.5 能量分项（逐轴、逐工况、逐轨迹段）

```
E_motor,j   ＝ ∫ P_motor,j dt       （梯形积分；非均匀采样按逐对区间积分，缺样本区间不外推）
E_joint,j   ＝ ∫ P_joint,j dt
E_loss,j    ＝ ∫ (P_motor,j − P_joint,j) dt    （＞0 恒成立——§10.2 两方向折算均消耗；传动箱损耗）
E_regen,j   ＝ ∫ max(0, −P_joint,j) dt          （再生功，机械口径；不声明为可回馈电能）
E_pos,j     ＝ ∫ max(0,  P_joint,j) dt          （正功）
E_rotor,j   ＝ ∫ J_rotor·θ̈·θ̇ dt                 （转子动能往返，往返净额≈0，分项呈现）
```

- 工况分项与轨迹段分项并行输出（分组键＝工况 ID＋段 ID，随上游序列携带）；"完整任务循环"能量＝循环边界内的分项之和。
- 缺样本区间：不外推、标记覆盖缺口（时长）；估算能量（如效率为 Estimated 来源）→ 结果携带估算限定语（DYN-06 可信等级的素材，等级判定归 dynamics/evidence）。

### 10.6 四象限工作制统计（逐电机轴）

| 象限 | 条件（ω＝θ̇，P＝τ_m·θ̇） | 物理含义 | 统计输出 |
| --- | --- | --- | --- |
| Q1 | ω＞0 且 P＞0 | 正转电动 | 时间占比、能量、峰值 |
| Q2 | ω＞0 且 P＜0 | 正转再生 | 同上 |
| Q3 | ω＜0 且 P＜0 | 反转电动 | 同上 |
| Q4 | ω＜0 且 P＞0 | 反转再生 | 同上 |
| 零速/零功率 | ω＝0 或 P＝0 | 驻留/保持 | 时间占比（不计入象限能量） |

### 10.7 数据不足与结果质量（映射侧素材，判定归 evidence/消费域）

| 情形 | 映射侧行为 |
| --- | --- |
| 效率缺失/非法 | 功率/能量/四象限＝DataInsufficient 素材（缺失项全量列出）；力矩/速度/位置照常（标注未含效率） |
| 转子惯量缺失 | 含转子项力矩不可得 → 力矩按理想口径输出＋`DT-ROTOR-MISSING`＋限定标记 |
| 负载折算惯量缺失 | 惯量比＝不适用（显式标记）；反射惯量（转子侧）照常 |
| 上游序列部分工况缺失 | 缺失工况列入覆盖矩阵缺口（EVI-02 素材）；不混入其他工况数据 |
| 估算来源 | Estimated 标记贯穿映射结果 → 证据/报告保留限定语（RPT-05） |

---

## 11. 电机工作点与多工况包络

### 11.1 MotorOperatingPoint 字段表（selection 消费口径＝reporting 展示口径）

| 字段 | 类型/单位 | 说明 |
| --- | --- | --- |
| axisId | MotorDriveAxis | 电机轴身份（ObjectId） |
| jointId / jointIndex | ObjectId / size_t | 对应关节 |
| tauPeakPos/Neg, omegaPeak, powerPeak | N·m, rad/s, W | 峰值（正/负转矩分列）＋发生时刻＋轨迹段＋工况（§10.4） |
| tauRms, omegaRms | N·m, rad/s | 完整循环 RMS（含驻留） |
| loadRatio | 无量纲 | 负载率（参考值，§10.4） |
| etaForward/Backward | 无量纲 | 本次计算使用的 η⁺/η⁻（含来源标记） |
| reflectedInertia, inertiaRatio | kg·m², 无量纲 | 反射惯量（关节轴系对角视图）＋惯量比（数值事实） |
| quadrants | 四象限统计 | §10.6 |
| energyBreakdown | 分项 | §10.5（按工况×段） |
| caseId / segmentId | CaseId / 段 ID | 所属工况与段（多工况逐条输出，不合并成无来源最大值） |
| quality | 完整性状态 | Complete / Partial（附缺失清单）/ Estimated（附来源） |
| constraintRefs | 引用 | 消费的力矩限值等参考字段的对象引用（不复制值语义） |
| identity | 身份块 | 传动配置身份＋矩阵内容身份＋上游切片身份＋算法/契约版本（§5.5） |

### 11.2 多工况包络（DYN-07 衔接）

- 每工况独立计算工作点与统计（不同工况的负载/轨迹不得混算）；**包络合并仅为结果呈现方式，不替代必验工况覆盖规则**（EVI-02）——合并包络条目必须保留来源工况清单，缺一工况即整包络降级（DataInsufficient 素材）。
- **不同工况的工作点不直接混成无来源的最大值**：跨工况"最坏值"必须携带来源工况/时刻/段（可定位）；峰值不带来源即非法输出（构造边界拒绝）。
- 包络不替代必验工况覆盖：漏验任一启用必验工况 → 不得输出正式通过结论（判定在 evidence 汇总，映射侧提供覆盖矩阵素材）。

### 11.3 消费边界

- 工作点**由 drivetrain 计算**（唯一实现）；电机/减速器能力硬筛选、可行集、淘汰原因归 selection（§12.2）；**工作点超限不直接等于整机工程不可行**（超限是候选淘汰/复算提示的输入，工程判定权在 evidence/各域汇总规则）。
- selection 消费的工作点与 reporting 展示的工作点**同源同身份**（同一结果对象，逐字段一致）；历史工作点不被目录更新静默改变（历史结果保持原切片身份，CON-02/evidence §5.3 目录行）。

---

## 12. 与 dynamics、selection、evidence、execution、reporting 的协作

### 12.1 与 dynamics（上游输入；dynamics 卡未产出——最小依赖契约＋交接登记）

**交接事实**：`units/dynamics.md` 不存在（WP-17-T01 待产出）。本卡按下表给出**最小依赖契约（提议）**，登记供 dynamics 卡对齐；**不代写 dynamics 详设**（execution/reporting 卡对缺失卡的同款先例）。

- dynamics 通过稳定端口提供（经上游结果切片消费）：关节位置、关节速度、关节加速度、关节广义力（类型化）、关节侧功率、时间戳、轨迹段、工况、工具和负载身份、其输入切片身份、运行身份。
- **提议 DTO：DynamicsJointSeries**（dynamics 域 payload 契约，drivetrain 消费面）：

| 字段 | 类型/单位 | 约束 |
| --- | --- | --- |
| jointIds / jointKinds | ObjectId[] / JointKind[] | 串联序（§5.3）；Kinds 含 Revolute/Continuous/Prismatic——Prismatic 触发范围外诊断 |
| caseGroups | 工况分组（CaseId→样本区间） | 必验工况逐组；组间不混算 |
| samples | 逐时刻：t(s)、q/q̇/q̈（rad,m 系）、τ_joint（N·m/N 类型化）、P_joint(W)、segmentId、驻留标记 | 时间严格单调递增；等长数组；非有限拒绝 |
| loadRef | 负载/工具对象引用＋负载折算惯量引用 | 身份引用（不复制语义） |
| identity | dynamics 切片身份＋运行五元组 | UpstreamResult 条目载荷 |

- **处理规则**：采样**逐时间戳对齐**（本卡不重采样、不插值——上游序列即对齐基准）；输入长度不一致 → `DT-SERIES-LENGTH-MISMATCH`；时间非单调 → `DT-INPUT-TIME-NONMONOTONIC`（不排序吞错）；轨迹段/工况分组原样保持；dynamics 失败或部分结果 → 上游结果不成立（无归档 envelope），本卡评估根本不被派发（UpstreamResult 依赖未满足 → 派发前失败，任务按 execution 状态机处理）——**不消费残缺上游**。
- **不同快照、不同轨迹或不同传动配置不得混合**（切片身份隔离）；不得让 drivetrain 直接 include dynamics 私有头/公共头（白名单未登记边）——经 UpstreamResult 切片条目＋payload 契约传递。
- **取消**：本卡取消后不发布完整结果（§12.4）；dynamics 侧取消语义归其卡。
- **登记**：P-DT-6——DynamicsJointSeries 键名/字段与 dynamics 卡（WP-17-T01）对齐；对齐前本卡评估器的 UpstreamResult 依赖声明使用本表提议键 `dyn.joint-series`（域键词表跨域唯一性由注册表校验，dynamics 卡产出时如更名按注册清单同步）。

### 12.2 与 selection（下游消费；selection 卡未产出——最小消费契约＋交接登记）

- selection 消费电机侧工作点（§11.1 字段表＝消费口径）；**不重新实现传动映射**（NFR-MNT-07 无重复算法；R-5 精神）；使用**同一传动配置与同一映射版本**（矩阵内容身份一致性，§7.4-4）。
- **消费通道（③端口，ARCH §7.2③/§7.10"选型消费传动映射"）**：selection 组合校核评估器（WP-19-T05）经注册表消费 `dt.mapping` 输出；组合候选的器件值（目录行派生的 ratio/η/J_rotor）由 selection 组装为 Configuration 条目（`config.dt-mapping`，canonical 字节进 sliceId）或经编译链候选模型（OPT-D 路径）——**目录 schema 知识留在 selection**，drivetrain 只见归一化值对象（不消费目录对象语义）。
- 惯量比阈值归属按 O-11 登记（本卡只出数值）；selection 输出可行组合、裕量、淘汰原因（实际值/阈值）；drivetrain 只提供工作点与映射事实。
- SEL-09：目标链含移动关节 → selection 输出"范围外"（DataInsufficient）；映射侧同口径诊断（§6.3）。

### 12.3 与 evidence（事实与判定正交）

| 正交轴 | 所有者 | drivetrain 行为 |
| --- | --- | --- |
| 映射事实（Facts DTO） | drivetrain | `DriveTrainEvidenceFacts`：映射结果＋输入身份＋工况覆盖素材＋诊断引用＋完整性状态（Complete/Partial/Estimated）——**只含事实** |
| EvidenceItem/Profile/证据等级 | evidence | drivetrain 注册自己的 RequiredEvidenceProfile（profileId 建议值 `dt`，版本对齐需求 §8.1 表 4：动力学域"机械功率/能量分项"建议项、选型域"每组合电机侧工作点——DriveTrainMappingEvaluator 同一口径"必需项的证据素材由本卡产出）；缺失输入由 evidence 表达为数据不足 |
| ResultEnvelope/当前性/正式判定 | evidence | drivetrain 不拥有、不判定、不改写；"有工作点数据"≠完整证据（必需项缺失仍 DataInsufficient） |

- 映射结果绑定：模型（上游切片）、矩阵、效率、转子惯量、工况、算法身份（§5.5）——全部进入 Facts 身份块，供证据追溯（NFR-COR-04）。

### 12.4 与 execution（调度/取消/检查点/worker）

| 项 | 契约 |
| --- | --- |
| 任务提交 | 消费域组装快照/切片后经 execution 提交（评估键 `dt.mapping`、契约版本、能力已注册前提校验——trajectory §15.12 同款薄适配，不建逐域包装器） |
| 能力声明（TASK-01） | descriptor 声明：支持取消（批次边界查询 cancellationRequested，NFR-PERF-02 协作取消）；支持分批流式（逐工况批次，caseSubset）；**不支持暂停**（R1；R2 大规模批处理暂停/继续归 execution/optimization） |
| 检查点 | R1 不声明检查点能力（单次映射为流式短批次；恢复＝重跑该工况批次）；**检查点（如未来声明）只保存可恢复中间状态，不构成正式证据/结论**（CON-04：部分结果不得作为正式缓存命中） |
| worker | L5 装配在 worker 进程注册同一评估器清单（manifest 摘要握手，evidence §9.4）；**worker 不属于 drivetrain**；worker 崩溃只致当前任务失败（NFR-REL-02），主进程不受影响 |
| 批次与结果 | 批次边界＝工况边界；**取消/失败/中断不得发布完整结果**（TASK-02：Canceled/Failed/Interrupted → NotApplicable，不得进入正式报告或可行集）；迟到结果经登记表核对归档（§5.5） |
| 版本兼容 | 检查点/缓存按契约判兼容（契约版本＋切片身份）；版本不兼容 → 不命中并诊断（CON-04） |

### 12.5 与 reporting（呈现一致性）

- reporting 消费电机侧工作点、效率、能量、反射惯量与四象限统计（reporting.md §11 章节 `drivetrain-operating-points`，C 级验收）：**报告结果与 selection 结果逐字段一致**（同一结果对象同源）；保留估算、数据不足、外部验证限定语（RPT-05）；不把传动工作点直接渲染为器件通过结论；显示单位转换发生在呈现层、不进入计算身份（§5.4）。

### 12.6 责任矩阵（drivetrain 视角）

| 事项 | modeling | runtime | dynamics | **drivetrain** | selection | evidence | execution | reporting |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 传动参数权威/持久化 | **拥有** | 编译视图 | — | 消费 | 目录值来源 | — | — | — |
| C 良态编译校验 | 编辑约束 | **拥有**（P-RT-7） | — | 映射入口第二道防线 | — | — | — | — |
| 关节侧序列（RNEA） | 物性来源 | DWC | **拥有** | 消费 | — | — | — | — |
| 虚功映射/工作点/效率折算/能量/四象限 | — | — | — | **拥有（唯一）** | 消费 | — | — | 消费 |
| 组合校核/淘汰/回填命令 | 回填写入口 | — | — | 供给工作点 | **拥有** | — | — | 呈现 |
| 证据等级/包络/当前性/正式判定 | — | — | 域判定 | 事实 DTO | 域判定 | **拥有** | 接纳 | 措辞 |
| 任务/worker/取消/检查点/缓存 | — | — | — | 能力声明 | — | — | **拥有** | — |
| 归档/修订/事务 | 命令产生 | — | — | — | — | — | 写 results | 写 reports（经 project） |

---

## 13. 公共接口与线程生命周期

### 13.0 通用约定（适用于 13.1～13.10，各接口不再重复）

| 约定 | 内容 |
| --- | --- |
| 语言/标准 | C++17；命名空间 `sdurws::ird::drivetrain`；头文件 `include/sdurws/ird/drivetrain/` |
| 零 Qt | 全部头与实现不 include 任何 Qt 模块（R-3）；不暴露 UI 类型 |
| 依赖纪律 | 只 include core/evidence 公共头与 L1 已登记数学类型；不 include 业务单元任何头（§3.2） |
| 错误语义 | 调用方错误（契约违约：空输入、维度不匹配、非法矩阵、非有限值）＝**fail-fast**（构造校验抛 `std::invalid_argument`/`std::out_of_range`，语义见各签名）——不返回半结果；环境/数据类情形（效率缺失、样本缺失、工况缺口）＝**返回诊断＋完整性降级素材**（DataInsufficient 路径），不抛异常、不吞错；稳定诊断码 `DT-*`（建议值，注册归 diagnostics） |
| 确定性（NFR-COR-02） | 纯函数：同输入＋同契约版本 → 等价输出；无隐藏状态、无时钟/随机源；并行归约（如分批能量汇总）满足附录 D 第 8 项相对容差 1×10⁻¹² |
| 线程安全 | 映射核心纯函数＝可重入（ConcurrentReadOnly）；评估器实例非线程安全（每 worker 每任务一实例，trajectory §15.10 同款）；归一化模型构造不可变共享 |
| 所有权/生命周期 | 输入值对象由调用方持有（本卡不接管）；输出值对象按值返回；评估器实例由 IEvaluatorFactory::create() 产生、调用方独占 |
| 单位/坐标系 | 全部按 §5.4 单位表；关节轴系/电机轴系逐字段标注（§5.3/§9）；无世界系量（本卡不做基座变换——MDL-22 纪律） |
| 取消/超时 | 映射核心无阻塞等待（纯计算）；评估器适配层在工况批次边界查询 cancellationRequested（协作取消，NFR-PERF-02）；超时与强制终止由 execution 状态机处置（本卡无超时语义） |
| 副作用 | 零副作用（不写项目、不派发任务、不自我注册、不产生修订） |
| 缓存身份 | 经 InputSlice（§5.5）；本卡不自建缓存键 |
| R1/R2 适用 | 逐接口标注 |

### 13.1 `IDriveTrainMappingEvaluator`（映射核心注入契约）

```cpp
/// @brief 传动映射核心的稳定契约：归一化输入 → 电机侧序列/工作点/统计/事实。
///        唯一实现 DriveTrainMappingCore；模型测试直调（NFR-MNT-01）；
///        评估器适配层（13.10）经本接口调用核心，保证③端口形态与注入形态同一算法。
class IDriveTrainMappingEvaluator {
public:
    virtual ~IDriveTrainMappingEvaluator() = default;
    /// @brief 执行一次完整映射评估。
    /// @param[in] model  归一化传动模型（构造入口已过结构校验；调用方持有）
    /// @param[in] series 关节侧序列（12.1 契约；调用方持有；本函数不修改）
    /// @param[in] ctx    取消查询回调（可为 nullptr＝不可取消；批次边界查询）
    /// @return 映射输出（§11 工作点序列＋统计＋事实 DTO；逐工况分组）
    /// @throws std::invalid_argument 结构非法（§6.3 阻断面/§7.2 矩阵形态——附 DT-* 码语义）
    /// @pre  model.identity 与 series.identity 无冲突（不同传动配置/不同序列混用＝调用方错误）
    /// @post 纯函数零副作用；同输入等价输出（NFR-COR-02）
    /// @note R1/R2：能力由 model.window 是否存在与装配能力版本共同决定（§6.3 能力门控）
    virtual DriveTrainMappingOutput evaluate(const DriveTrainModel& model,
                                             const JointSeriesView& series,
                                             ICancellation* ctx) = 0;
};
```

### 13.2 `ITransmissionInputValidator`（输入快照校验）

```cpp
/// @brief 归一化输入结构校验：轴表/序列/单位/有限性/轴序一致性（§6.3 后三行＋12.1 规则）。
///        精确判据（非有限/维度/顺序），无数值阈值；校验失败经 result 返回逐项诊断（比较型字段带单位），
///        调用方错误类（契约违约形态）另以异常 fail-fast。
struct TransmissionValidationResult {
    bool ok = false;
    std::vector<core::DiagnosticRecord> diagnostics;   // DT-INPUT-*/DT-RATIO-*/DT-SERIES-*
};
struct ITransmissionInputValidator {
    virtual ~ITransmissionInputValidator() = default;
    virtual TransmissionValidationResult validate(const DriveTrainModel& model,
                                                 const JointSeriesView& series) const = 0;
};
```

### 13.3 `ICouplingMatrixValidator`（矩阵良态校验）

```cpp
/// @brief 耦合矩阵/归一化 Ĉ 校验：方阵/有限/可逆/条件数/时变结构/R1 非对角阻断（§6.3/§7.2 全表）。
///        条件数阈值来源＝P-RT-7 对齐（§7.3；裁决前用同一设计默认 1×10⁸ 并留痕）。
///        阻断结果不伪装成"传动不可行"——返回能力/输入类诊断，判定权在消费域证据规则。
struct CouplingValidationResult {
    bool accepted = false;                     // false＝阻止映射（诊断含 DT-MATRIX-*/DT-COUPLING-*）
    double conditionNumber = 0.0;              // 实测条件数（无量纲；比较型诊断素材）
    std::vector<core::DiagnosticRecord> diagnostics;
};
struct ICouplingMatrixValidator {
    virtual ~ICouplingMatrixValidator() = default;
    virtual CouplingValidationResult validate(const DriveTrainModel& model,
                                              StageCapability stage) const = 0;
    // stage：R1Capability / R2Capability——能力门控第一检查（§6.3；UI/配置标签不改变能力）
};
```

### 13.4 虚功/功率一致性检查、反射惯量、效率、工作点（管线步骤接口）

```cpp
/// @brief 虚功/功率一致性检查器（§8）：黄金对照（测试侧，附录 D 第 9 项容差）与逐元素自检。
///        产品运行侧不私设数值阈值（§8.4）；失败输出实际值/期望值/单位（ERR-01）。
struct IVirtualWorkConsistencyChecker {
    virtual ~IVirtualWorkConsistencyChecker() = default;
    /// @param[in] ideal 期望理想映射值（τ_m=Cᵀτ_j 派生）；@param[in] actual 待核对值（独立供给）
    /// @return 逐元素报告（样本时刻＋轴＋实际/期望/单位）；全通过＝空报告
    virtual std::vector<ConsistencyFinding> checkVirtualWork(
        const JointSeriesView& series, const std::vector<double>& actual) const = 0;
    virtual std::vector<ConsistencyFinding> checkPowerBalance(
        const JointSeriesView& series, const std::vector<double>& actualMotorPower) const = 0;
};

/// @brief 反射惯量评估（§9）：R1 对角 J/c²；R2 (C⁻¹)ᵀ·J_rotor·C⁻¹（完整矩阵＋对角视图）。
///        绑定矩阵/转子模型/切片身份；交叉项保留（不对角化输出）。
struct IReflectedInertiaEvaluator {
    virtual ~IReflectedInertiaEvaluator() = default;
    /// @return 每关节轴：J_reflected（kg·m²，关节轴系）＋惯量比（数值事实，§9.5）
    /// @throws std::invalid_argument J_rotor 非法（DT-INERTIA-INVALID）或矩阵未过 §7.2 校验
    virtual ReflectedInertiaResult evaluate(const DriveTrainModel& model,
                                            const LoadInertiaInput& load) const = 0;
};

/// @brief 效率折算（§10.2/10.3）：方向选择（P_joint 符号精确判据）＋η⁺/η⁻折算＋零功率分支。
///        η 缺失/非法 → 结果标 DataInsufficient 素材（不伪造、不以 1 替代）。
struct IEfficiencyEvaluator {
    virtual ~IEfficiencyEvaluator() = default;
    virtual EfficiencyResult apply(const DriveTrainModel& model,
                                   const JointSeriesView& series) const = 0;
};

/// @brief 电机工作点统计（§10.4/§10.6/§11）：峰值（带时刻/段/工况）、RMS（完整循环含驻留）、
///        负载率（参考值）、四象限统计、能量分项（§10.5 恒等式核对）。
///        循环不完整 → 统计标 DataInsufficient 素材（不输出部分 RMS 冒充完整循环）。
struct IMotorOperatingPointEvaluator {
    virtual ~IMotorOperatingPointEvaluator() = default;
    virtual OperatingPointResult summarize(const DriveTrainModel& model,
                                           const MotorSeriesView& motorSeries) const = 0;
};
```

### 13.5 接口的边界价值声明（NFR-MNT-04 自查）

上述步骤接口各为**映射管线的独立对照点**（黄金数据集按步骤独立断言：输入校验/矩阵校验/虚功/反射惯量/效率/工作点），并构成 R2 扩展（如直线传动映射）的替换缝——非转发包装器；单实现但有多验证面与演进面（D-DT-9）。

### 13.6 `IDriveTrainEvidenceFactsProvider`（事实 DTO 提取；替代旧提示词 `IDriveTrainEvidenceBuilder`）

```cpp
/// @brief 从映射输出提取传动事实 DTO（§12.3）——只提供映射事实、输入身份、诊断引用与完整性信息。
///        本接口【不拥有】RequiredEvidenceProfile、证据等级、ResultEnvelope、结果当前性、
///        正式工程判定、项目归档（全部归 evidence/接纳层）。旧名称 IDriveTrainEvidenceBuilder 弃用：
///        "Builder"暗示拥有证据组装权，与 evidence 所有权边界冲突（D-DT-10）。
struct IDriveTrainEvidenceFactsProvider {
    virtual ~IDriveTrainEvidenceFactsProvider() = default;
    /// @return 事实 DTO（含身份块＋工况覆盖素材＋缺失清单＋诊断引用；canonical 形态供证据消费）
    virtual DriveTrainEvidenceFacts facts(const DriveTrainMappingOutput& output) const = 0;
};
```

### 13.7 公共头纪律（机器可断言清单）

零 Qt；不 include dynamics/selection/modeling/runtime/policy/execution/evaluation 域任何头；不直接拥有 `RuntimeSnapshot` 生命周期；不暴露 UI 类型；显示单位不进入计算身份；**不允许非法矩阵通过伪逆静默继续**（§7.3）；**不允许空输入发布完整结果**（§6.3）。

### 13.8 合法/非法调用示例

```cpp
// 合法（R1）：对角模型＋完整序列 → 输出含每轴工作点与四象限统计
auto out = core.evaluate(diagModel, jointSeries, &cancel);
assert(out.points.size() == diagModel.motorAxes.size());

// 非法（调用方错误，fail-fast）：R1 能力下传入含耦合窗口的模型
expectThrows<std::invalid_argument>([&]{ core.evaluate(coupledModel, jointSeries, nullptr); });
// → std::invalid_argument("DT-COUPLING-STAGE-LOCKED")（诊断码语义；同型记录进 validator 路径）

// 非法（数据类，返回降级素材而非异常）：效率缺失 → out.facts.completeness==Partial，
// 缺失清单含 "efficiency[j=3]"；功率/能量/四象限标 DataInsufficient 素材
```

### 13.9 线程与生命周期汇总

| 对象 | 线程约束 | 生命周期 |
| --- | --- | --- |
| 映射核心（纯函数） | 可重入（ConcurrentReadOnly） | 进程静态 |
| 评估器实例 | 单线程（每 worker 每任务一实例） | 任务期；不跨任务复用 |
| 归一化模型 | 构造后不可变（跨线程只读共享） | 调用方持有 |
| 输出值对象 | 按值返回 | 调用方持有；归档由接纳层执行 |

### 13.10 `DriveTrainMappingEvaluator`（③端口注册面）

```cpp
/// @brief evidence::IEngineeringEvaluator 实现（唯一映射实现；评估键 "dt.mapping"）。
///        descriptor：contractVersion（进入 sliceId）；inputs＝{model.drivetrain(Object,Required),
///        dyn.joint-series(UpstreamResult,Required——P-DT-6 待 dynamics 卡对齐),
///        config.dt-mapping(Configuration,Optional——组合场景效率/统计配置值传递)}；
///        supportedModes＝{Quick, Verified}（Preview 待消费需求登记，暂不含——保守）；
///        stateless＝true；threadSafety＝SingleThread（实例级）。
///        evaluate()＝校验（13.2/13.3）→ 归一化 → 映射（13.1）→ 一致性自检 → 工作点 → Facts；
///        输出 EvaluationOutput{EvidenceItem*（Facts 素材）, payload（canonical：MotorSeries+
///        OperatingPoints+Facts）, diagnostics}——envelope 由接纳层经 aggregateVerdict+
///        ResultEnvelope::make 组装（evidence §9.3，本卡不自建 envelope）。
class DriveTrainMappingEvaluator final : public evidence::IEngineeringEvaluator { /* … */ };
```

---

## 14. 验证方案与故障注入矩阵

### 14.1 验证总体口径（只设计，不宣称已执行）

- **黄金数据集**：`testdata/golden/dt-*`（对齐 DTB WP-18-T04 交付物登记）；解析算例组：①单级齿轮（已知 c、η⁺/η⁻、J_rotor）正弦轨迹的解析工作点；②双向效率能量守恒（正功段＋再生段混合循环，E_loss＞0 恒等式核对）；③虚功对偶随机矩阵性质测试（良态 C 下 ‖τ_m−Cᵀτ_j‖＝0 构造性核对＋独立参考实现对照——NFR-COR-01，附录 D 第 9 项容差，黄金算例逐例声明）；④摩擦不重复（关节侧含摩擦的 τ_joint 与传动损耗分项分离核对）；⑤转子惯量不重复（DWC 输入不含转子的前提契约，§9.4）。
- **独立参考实现**：映射公式以逐元素标量参考实现（非矩阵库路径）交叉对照（NFR-COR-01）。
- **确定性**：同输入同种子同线程数多轮等价＋稳定排序（工况/轴按 ID 字典序）；并行归约容差按附录 D 第 8 项。
- 全部用例标注：需求/架构依据、AT 编号、R1/R2 范围、前置、输入身份、操作、预期、失败分类、稳定诊断、evidence 影响、execution 状态影响、观测点、是否需真实执行。**不得把未执行测试写成"通过"**（§20 如实声明）。

### 14.2 故障注入矩阵（核心组；全部为设计）

| 组# | 用例 | 输入/操作 | 预期结果 | 失败分类 | 稳定诊断（建议） | 依据/AT | 范围 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DT-G1 | 单轴正传动比 | c=0.01，正弦 q(t) | θ̇/θ̈/τ_m/P 解析一致 | — | — | DYN-04、NFR-COR-01 | R1 |
| DT-G2 | 单轴负传动比 | c=−0.01（值传递通道） | 符号逐轴保留，虚功恒等 | — | — | §5.3、§6.2 | R1 |
| DT-G3 | 多轴对角 | c 各异 | 逐轴独立、轴序不串 | — | — | DYN-04 | R1 |
| DT-G4 | 位置/速度/加速度映射 | 偏置 θ_off≠0 | 绝对位置含偏置，θ̇/θ̈ 不受影响 | — | — | §5.3 | R1 |
| DT-G5 | 虚功/理想功率一致性 | 良态对角 | 逐样本逐元素恒等 | — | — | §8.1/8.2 | R1 |
| DT-G6 | 双向效率与再生 | η⁺≠η⁻ 混合循环 | 方向折算正确、E_loss＞0、E_regen 分项 | — | — | AT-07"双向效率" | R1 |
| DT-G7 | 零功率/驻留 | q̇=0、τ≠0 段 | 效率不适用标记、RMS 含驻留 | — | — | §10.2/10.4 | R1 |
| DT-G8 | 反射惯量与惯量比 | 已知 J_rotor/J_load | J/c² 与比值解析一致；不重复计入 | — | — | AT-07"反射惯量"、§9 | R1 |
| DT-G9 | 峰值窗与 RMS | 多段循环 | 峰值带时刻/段/工况；RMS 完整循环 | — | — | DYN-03 口径 | R1 |
| DT-G10 | dynamics 输入缺失 | UpstreamResult 不满足 | 派发前失败（评估不运行） | 调用方/上游 | —（execution 状态） | §12.1 | R1 |
| DT-G11 | selection 消费一致性 | 同一结果对象双消费 | 逐字段一致 | — | — | §12.2/12.5 | R1 |
| DT-G12 | 空序列/缺样本/非均匀/时间倒退 | 变异序列 | 非均匀可积分；倒退→阻止；缺样本→降级素材 | 数据 | DT-INPUT-TIME-NONMONOTONIC / DT-INPUT-SAMPLE-MISSING | NFR-COR-03 | R1 |
| DT-G13 | 非有限值/维度不匹配/零传动比/非法效率/非法惯量 | 变异输入 | fail-fast 或比较型诊断（实际/期望/单位） | 调用方 | DT-MATRIX-NONFINITE / DT-INPUT-DIMENSION-MISMATCH / DT-RATIO-ZERO / DT-EFFICIENCY-INVALID / DT-INERTIA-INVALID | NFR-COR-03 | R1 |
| DT-B1 | R1 非对角矩阵被阻断 | 含非对角 Ĉ | 阻止＋诊断；不对角化、不拆轴 | 能力/输入 | DT-MATRIX-NONDIAGONAL-LOCKED | AT-38"R1 阻断反例" | R1 |
| DT-B2 | R1 mimic/闭环/线性耦合链被阻断 | 链型变异 | 阻止＋诊断（不伪装"传动不可行"） | 能力/输入 | DT-AXIS-TYPE-OUT-OF-SCOPE | MDL-12、M-6 | R1 |
| DT-B3 | R2 未启用时拒绝耦合输入；UI R2 标签不改能力 | 能力门控变异 | 同 DT-B1（标签无关性） | 能力 | DT-COUPLING-STAGE-LOCKED | §6.3 | R1 |
| DT-R2-1～12 | 合法常矩阵/非方/奇异/病态/非有限/维度/交叉项保留/高速联动/同矩阵身份/力矩对偶/转子项/功率一致性 | §7 全表 | 阻断或精确映射；交叉项逐元素非零保留；三方矩阵身份一致 | 调用方或无 | DT-MATRIX-* 族 | MDL-21、AT-38 | R2 |
| DT-R2-13 | 矩阵版本变化使缓存失效 | robot-drivetrain 新版本 | sliceId 变化→缓存不命中→Superseded/重算 | — | —（evidence 机制） | CON-04/05、AT-05 | R1/R2 |
| DT-R2-14 | 映射失败不自动判定整机不可行 | 病态矩阵注入 | 输出输入类诊断；不可行判定由证据规则 | 能力/输入 | DT-MATRIX-ILL-CONDITIONED | §8.1 表 2、§6.3 | R1/R2 |
| DT-E1～16 | 效率/能量/工作点组：η 缺失/估算/多工况包络/漏验工况/工作点超限交给 selection/不转工程判定 | §10/§11 变异 | 降级素材/限定语/覆盖缺口/超限仅报告 | 数据 | DT-EFFICIENCY-* / 覆盖素材 | EVI-02、AT-07/08 | R1 |
| DT-X1～14 | execution 可靠性：取消/超时/worker 崩溃/检查点（无）/版本不兼容/切片不兼容/算法版本不兼容/迟到结果/项目切换/历史当前性/失败不发布/取消不进可行集/检查点不构成证据/主进程不退出 | §12.4 变异 | 状态机语义正确 | — | EX-* 族（execution 所有） | TASK-01~03、NFR-REL-02/03、AT-10/34 | R1 |
| DT-R1B1～8 | R2 直线传动/混合链扩展（§16.2）：单位/接口/目录归属/混合链端到端/4-5 轴仍阻断/不影响全旋转链 | 设计验证预登记 | 阻断或（R2 启用后）端到端 | 能力 | DT-AXIS-TYPE-OUT-OF-SCOPE | SEL-09-S1、MDL-12-S1、AT-36 | R2 |

### 14.3 需真实动力学数据的清单（不得替身替代送验）

- 与 dynamics 黄金二连杆解析算例联动（AT-07：关节侧 τ 序列）→ 传动映射端到端（AT-07"传动映射黄金数据"）；多工况必验覆盖联动（EVI-02）；selection 组合校核联动（WP-19-T05 后，AT-08）。R1 单元测试可用模型测试直调（NFR-MNT-01），无需 GUI/RobWorkSim 实例；端到端组待 dynamics/selection 落位后执行。

### 14.4 GUI/插件流程（只设计；当前无 GUI 目标，不虚构测试）

drivetrain 无 plugin 目标（§2.3），**无 GUI 测试**；Windows GUI 测试约定（VS x64 DevEnv、`QT_QPA_PLATFORM='windows'`、单实例）不适用于本卡，登记为消费域（dynamics/selection/reporting）注意事项。

---

## 15. 阶段 C 实现任务拆分（对齐 DTB §2.19 WP-18-T01～T05）

| 任务 | 交付物 | 依赖 | 验收（DoD 口径） | 红线 |
| --- | --- | --- | --- | --- |
| **WP-18-T01**（本卡） | `units/drivetrain.md`（≥v0.1）——DriveTrainMappingEvaluator 接口（③端口注册）、映射契约（τ_motor＝Cᵀτ_joint＋J_rotor·θ̈）、runtime 消费形态（值传递推荐——O-07 起草）、任务拆分 | WP-05 卡（已存在） | 文档自审（§19）；登记 DYN-04/SEL-05/MDL-21/AT-38 追溯 | 不采用对角/准静态近似、不丢交叉耦合项 |
| **WP-18-T02** | `ird/drivetrain/CMakeLists.txt`（新）：STATIC（链 core＋evidence）；注册 `_test`/`_contract_test`；占位 README 文案修正 | T01、WP-03-T01 | 双模式构建零错误；红线扫描零命中（ird_gates） | 零 Qt；依赖仅 core+evidence（§3.5） |
| **WP-18-T03** | DriveTrainMappingEvaluator 唯一实现：电机侧工作点 τ/ω/P、η⁺/η⁻、反射惯量 J·i²、惯量比、能量分项、四象限统计；R1 对角传动；R2 矩阵扩展接口（§7 设计落位为编译期隔离的能力门控）；非法矩阵诊断；不重复计入转子惯量 | T02、WP-05-T10（③端口） | 传动映射黄金数据（双向效率/反射惯量/摩擦不重复计入）用例通过；常矩阵 C 下与对角传动比等价验证 | 禁止与壳体质量重复计入转子惯量 |
| **WP-18-T04** | `drivetrain/test/*`＋`contract_test/*`＋`testdata/golden/dt-*`（§14.2 矩阵 R1 组） | T03、WP-02 | 全部用例通过并留痕（gtest XML＋ird-test-report.json）；ird_gates 零命中 | 未执行测试不得标注通过 |
| **WP-18-T05**（R2/D） | 耦合矩阵 C 全链消费：SEL-03/04/05 同一口径；限位经映射校验（MDL-21 消费面）；AT-38 高速多轴联动交叉耦合验证；R1 无耦合链等价对角映射用例 | T03、WP-13-T18 | R2 用例通过并留痕 | 不提前放开 R1 阻断 |

**跨单元同步说明（必须随任务登记）**：

- DYN-04 的唯一映射实现归 drivetrain（dynamics 卡红线：映射不自行实现）；SEL-05 只消费 drivetrain，不重写映射（WP-19-T05）。
- WP-17-T03 提供关节侧动力学（本卡 §12.1 最小依赖契约待其对齐——P-DT-6）；WP-19-T05 消费电机侧工作点；WP-19-T12 直线传动属 R2 跨单元扩展（§16.2）；WP-21（OPT-D）只组合 drivetrain，不由 drivetrain 实现优化语义。
- T03 体量 L（映射核心/工作点统计两提交，DTB 登记）——两提交均独立可构建、可回溯（§6.3 粒度）。

---

## 16. R2 阶段 D 承接与接口交接清单

### 16.1 MDL-21 耦合矩阵全链（R2；WP-18-T05）

| 上游 | 交接内容 |
| --- | --- |
| modeling → drivetrain | 传动配置（`robot-drivetrain` 对象）；轴身份与轴序；传动比；耦合矩阵 C＋适用关节窗口；矩阵版本（对象 ContentVersion）；转子惯量；效率模型；R1/R2 能力声明；输入内容身份；诊断引用 |
| runtime → drivetrain | 编译能力声明（hasCouplingMatrix——runtime §4.3.4 能力位）；编译期良态校验结论（不重复校验语义，映射侧保留第二道防线）；`runtime.model-identity`/`runtime.robwork-baseline` 身份要素（值传递） |
| drivetrain → selection | 电机侧位置/速度/加速度/力矩/功率；η⁺/η⁻；反射惯量（完整矩阵＋对角视图）；惯量比；峰值/RMS/负载率；四象限；能量分项；工况包络；映射诊断；数据完整性；传动配置身份（§11.1） |
| drivetrain → evidence/reporting | Facts DTO（映射事实/工作点/统计包络/输入身份/工况覆盖/算法版本/数值状态/数据不足/估算来源/诊断引用/当前性依赖清单） |
| execution → drivetrain | 任务身份五元组；取消；超时（状态机处置）；worker 上下文；批次边界；资源限制；缓存兼容性判定要素 |
| drivetrain → 运动学限位校验（MDL-21 消费面，经 kinematics 域） | 同一矩阵身份的映射关系（限位经映射执行——消费接口由消费域声明，本卡只保证映射关系与身份可复用，不实现限位判定） |

R2 矩阵检查前提重申（§7.4）：常矩阵、方阵、可逆、良态、有限；非方/奇异/病态阻止不降级；交叉项保留；dynamics/selection 同一矩阵内容身份；R1 阻断反例保留；R2 启用前非对角矩阵不进正式计算。

### 16.2 SEL-09-S1／MDL-12-S1 直线传动与混合链（R2；跨单元扩展，本卡只提供扩展点）

- 明确边界：直线传动**目录和器件筛选属 selection**（SEL-09-S1 选型层）；混合链正式产品链放开属 modeling/kinematics/trajectory/dynamics/selection 跨单元任务（MDL-12-S1 主责 WP-13，支持 WP-15/16/17/19）；**drivetrain 只提供统一的传动映射扩展点和类型化广义量接口**——本卡不提前宣称 SEL-09-S1/MDL-12-S1 已实现（§2.2 R1 不启用面）。
- 扩展点设计预留（接口形状，非 R1 实现）：`JointKind::Prismatic` 通道（移动广义量：位移 m、速度 m/s、加速度 m/s²、力 N）；直线传动映射接口（丝杠：θ→x＝p·θ/(2π)；齿条；同步带；直线电机——公式族待 R2 需求细化，**不在本卡冻结数值**）；能力曲线与工作点映射接口对齐 selection 目录模板。4/5 轴不因 R2 子项自动放开；planar/floating/闭环链维持阻断。
- 端到端启用条件＝MDL-12-S1/SEL-09-S1 验收（AT-36：混合链端到端，4/5 轴反例仍被阻止）；本卡对应任务随 R2 拆分登记（T05 之后的增量，不占阶段 C DoD）。

### 16.3 R2 优化消费（OPT-D；本卡为内层服务）

结构/传动外层生成候选 → dynamics 提供关节侧轨迹动力学 → drivetrain 生成电机侧工作点 → selection 执行器件能力筛选 → optimization 汇总联合硬约束与指标（OPT-05）。drivetrain **不实现**：候选搜索、Pareto、鲁棒性、灵敏度、误淘汰审计、暂停/继续状态机、检查点调度、任务缓存淘汰（§2.2 R1 不启用面；execution/optimization 承担）。

---

## 17. 需求—设计—验证追踪矩阵

| 需求/架构项 | 设计位置 | 验证位置 |
| --- | --- | --- |
| DYN-04 | 唯一映射器（§13.10）、关节侧与电机侧分离（§2.1/§5.1）、工作点（§11） | DT-G1～G9 黄金组；AT-07"传动映射黄金数据" |
| SEL-05 | selection 消费契约、共享映射身份（§12.2） | DT-G11；组合校核一致性（WP-19-T05 后，AT-08） |
| SEL-09 | 移动关节范围外诊断（§6.3/§12.2） | DT-B2；AT-08"移动关节链输出范围外" |
| SEL-10 | 转子字段独立消费、回填身份衔接（§9.4） | DT-G8（不重复计入）；AT-30 回填交接 |
| MDL-21 | R2 耦合矩阵、矩阵校验、交叉耦合（§7/§16.1） | DT-R2-1～13；AT-38 |
| MDL-12 | R1 混合链阻断、R2 扩展边界（§2.2/§16.2） | DT-B2、DT-R1B 组；AT-36 |
| MDL-16 | 力矩限值/摩擦来源消费、缺摩擦走 DYN-06 降级素材（§2.1/§8.3/§10.7） | DT-E 组；数据来源和缺失测试 |
| MDL-22 | 基座变换由 runtime 编译，本卡不重复变换（§2.1/§13.0） | AT-37 交接测试（重力矩已在关节侧 τ 内） |
| CON-01～06 | 输入冻结（§5.1）、身份/缓存键（§5.5）、历史结果（§5.6/§11.3） | DT-R2-13、DT-X 组；快照和切片测试 |
| EVI-01～02 | 事实输出与 evidence 汇总边界（§12.3）、覆盖素材（§11.2） | DT-E 组（数据不足/工况覆盖）；AT-07/08 |
| TASK-01～03 | 能力声明/取消/失败/迟到结果（§12.4、§5.5） | DT-X 组；execution 契约测试 |
| AT-19 | 不复制碰撞策略、无本地碰撞布尔开关（§3.2 红线自查——本卡零策略消费） | 依赖和红线检查（ird_gates） |
| AT-27 | 单位和配置不改变计算真值（§5.4/§5.5） | 单位/配置测试（DT-G 组身份断言） |
| AT-30 | selection 应用后的传动回填和复算提示（§9.4/§5.6） | 回填交接测试（DT-G8＋WP-19 联动） |
| AT-34 | 取消、进度和结果导出消费链（§12.4/§12.5） | execution/selection 集成测试（DT-X 组） |
| AT-36 | 直线传动和混合链 R2 边界（§16.2） | R2 端到端测试（DT-R1B 组） |
| AT-38 | 交叉耦合、位置/力矩对偶和统一矩阵（§7） | R2 矩阵测试（DT-R2 组） |
| NFR-COR-01 | 解析算例和独立参考实现（§14.1） | 黄金数据集（dt-*） |
| NFR-COR-02 | 确定性和稳定排序（§13.0） | 多线程/同输入等价测试 |
| NFR-COR-03 | 非有限数、非法单位和缺失引用（§6.3/§13.0） | 输入拒绝测试（DT-G13） |
| NFR-PERF-02/04～06 | 后台、取消、规模化（§12.4/§2.3） | execution/performance 测试（DT-X 组；WP-23） |
| NFR-REL-02/03 | worker 崩溃和中断结果（§12.4） | 故障注入测试（DT-X 组） |

---

## 18. 设计决策、风险、待裁决项与变更记录

### 18.1 已采用决策（D-DT-x）

| # | 决策 | 依据/理由 |
| --- | --- | --- |
| D-DT-1 | drivetrain 是 L2 共享计算服务；零 Qt；STATIC 库（链 core＋evidence）；不创建 plugin/worker | ARCH §2.3/§3.1/§3.5；worker 归 execution（ARCH §4.1） |
| D-DT-2 | `DriveTrainMappingEvaluator` 是唯一映射实现；dynamics/selection 不各自实现 | DYN-04、ARCH §7.10、NFR-MNT-07 |
| D-DT-3 | R1 只支持旋转无耦合/对角传动；R2 才启用 MDL-21 非对角耦合；能力门控先行（§6.3 顺序检查） | MDL-21（R2）、M-6、AT-38 |
| D-DT-4 | 全文唯一传动比口径 c＝Δq_joint/Δθ_motor（对偶 τ_m＝Cᵀτ_j；i＝1/c 换算记法），禁用第二种未声明约定 | ARCH §7.10/MDL-21 冻结公式；§5.3 |
| D-DT-5 | 映射核心契约支持带符号 c 与零位偏置；项目对象通道受 modeling I-MDL-11（＞0）约束 | §5.3；负号经 R2 矩阵或值传递通道（P-DT-5） |
| D-DT-6 | 对角映射逐轴携带自身符号（diag 元素即带符号 c），对角形式不丢轴符号 | §2.2 R1 纪律 |
| D-DT-7 | 转子惯量唯一计入位置＝电机侧映射公式显式项；组装方按 SEL-10 取"未计入合成"的转子字段 | M-12、SEL-10、§9.4 |
| D-DT-8 | 一致性检查分四口径（§8.2）；运行时不私设功率/虚功阈值（无需求变更不引入） | 附录 D C7、§8.4 |
| D-DT-9 | 管线步骤接口（校验/矩阵/虚功/惯量/效率/工作点）＝黄金对照点＋R2 替换缝（非转发包装器） | NFR-MNT-04 自查、§13.5 |
| D-DT-10 | 事实接口命名 `IDriveTrainEvidenceFactsProvider`（弃用 `IDriveTrainEvidenceBuilder`）；drivetrain 无证据等级/判定/当前性 | §13.6、evidence 所有权 |
| D-DT-11 | dynamics 上游经 UpstreamResult 切片消费（两段评估管线）；不做进程内评估器互调 | evidence §4.2.1 UpstreamResult、§10.1 流程、R-1/R-2 |
| D-DT-12 | selection 组合候选器件值经 Configuration 条目值传递进切片（目录 schema 留在 selection） | §12.2；CON-05 切片身份 |
| D-DT-13 | O-07 推荐值传递形态（drivetrain 侧）；不新增依赖边 | §3.2；P-DT-1 |
| D-DT-14 | 评估键 `dt.mapping`；supportedModes＝{Quick, Verified}；stateless=true；实例 SingleThread | evidence §9 语法与注册表行为 |
| D-DT-15 | 诊断码前缀 `DT-`（建议值；注册归 diagnostics；命名空间清单补登 P-DT-7） | diagnostics §4.5 前缀＝所有权 |

### 18.2 待裁决项（P-DT-x；格式按 DTB §4.2——编号｜问题｜影响｜来源｜建议裁决者｜状态）

| # | 问题 | 影响 | 来源 | 建议裁决者 | 状态 |
| --- | --- | --- | --- | --- | --- |
| P-DT-1 | **O-07**：runtime 视图消费形态（ARCH §3.5 未登记 drivetrain/dynamics→runtime 边 vs runtime.md §13.2 交接） | 本卡已按值传递设计（D-DT-13）；若裁决补登边或注入形态，§3.2/§5.1 微调 | DTB §4.2 O-07 | 架构所有者＋drivetrain/dynamics 卡（本卡已起草推荐） | 登记（推荐值传递，未裁决前不落编译边） |
| P-DT-2 | **O-11/P-RT-7**：惯量比阈值归属（policy vs selection 配置）与矩阵条件数阈值（1×10⁸）归属 | 本卡只出惯量比数值；条件数阈值暂按 P-RT-7 设计默认对齐；若归 policy → 切片加 Policy 条目声明 | DTB §4.2 O-11、runtime.md P-RT-7、policy.md §15.3 P-POL-3 | 需求所有者＋selection 卡（WP-19-T01）＋policy 侧 | 登记 |
| P-DT-3 | 运行时数值一致性校验阈值（功率/虚功）未冻结——附录 D 无功率量纲运行校验 ε_abs | 运行侧只做精确判据与黄金对照；如需运行阈值须走需求变更 | 附录 D C7 | 需求所有者 | 登记（保守：不引入） |
| P-DT-4 | 负载折算惯量（J_load@joint）的权威产生者与参考系口径（dynamics？工况组装？） | 惯量比计算的输入来源；未对齐前按"组装方提供＋来源标记"保守消费 | §9.1/§9.5 | dynamics 卡（WP-17-T01）＋selection 卡 | 登记 |
| P-DT-5 | R1 项目通道负传动比：modeling I-MDL-11 冻结 ratio＞0，映射核心带符号契约与 R1 项目数据不可达负值并存 | 若工程需要 R1 负传动比（电机反装），须 modeling 侧修订；当前经 R2 矩阵/值传递通道表达 | §5.3、modeling I-MDL-11 | 需求所有者＋modeling 卡 | 登记（R1 不放开） |
| P-DT-6 | `DynamicsJointSeries` 键名与字段（本卡提议 `dyn.joint-series`）待 dynamics 卡对齐 | UpstreamResult 依赖声明与 payload 解码；跨域依赖键唯一性 | §12.1 | dynamics 卡（WP-17-T01） | 登记（提议契约） |
| P-DT-7 | diagnostics §4.5 命名空间清单补登 `DT` 前缀（同 RPT 先例） | 码值注册放行 | §1.3、diagnostics.md §4.5 | diagnostics 卡（WP-09 侧） | 登记（随本卡收编） |
| P-DT-8 | 若未来需要 `drivetrain_plugin`/`_worker` 专用目标 | 与 L2 共享服务、execution worker、零 Qt 红线的关系须架构变更登记 | §2.3/§4.2 | 架构所有者 | 登记（默认不创建） |
| P-DT-9 | 效率模型（η⁺/η⁻）的建模侧 schema 尚未登记（modeling.md §4.7 DrivetrainDesign 字段未列效率） | R1 效率输入通道：未落位前效率只能经 ConfigEntry 值传递或按缺失降级；需 modeling 卡增量登记字段或 EfficiencyModelRef | §5.2/§10.1、modeling.md §4.7 | modeling 卡（WP-13 侧）＋需求所有者 | 登记（R1 映射在效率缺失时按 §10.7 降级，不阻塞其余映射事实） |
| P-DT-10 | `drivetrain.ratioPerJoint` 数值口径（c＝q/θ 或 n＝θ/q）在 modeling/runtime/optimization 卡间的统一表述 | 本卡按 ARCH 公式口径取 c 并在归一化入口单点换算；若上游卡冻结为 n 口径，换算规则随组装方契约登记（映射核心只见 c，身份不受呈现口径影响） | §5.3、runtime.md `ratioPerJoint`、OPT-02 | modeling/runtime/optimization 卡产出时 | 登记 |

### 18.3 偏差与待同步登记（DTB §5.4 精神；本卡不改他文）

| 项 | 内容 |
| --- | --- |
| 占位 README 指向 | README 文案"见 units/drivetrain.md §9"与本卡章节编号不符（实现任务＝§15）——随 WP-18-T02 落位时修正 |
| DTB 锚点 | DTB WP-18-T05 引用"drivetrain.md §13（卡预留）"——本卡 R2 承接章为 §16（§13 为公共接口）；DTB 侧锚点修订随其下次修订登记 |
| 索引状态 | DETAILED-DESIGN.md §20 与 unit-status.json 的 drivetrain 行（"待产出/planned"）刷新归治理任务（与 trajectory 卡同批待登记），本卡不代改 |
| diagnostics 命名空间 | `DT` 前缀补登（P-DT-7） |

### 18.4 风险登记（R-DT-x）

| # | 风险 | 缓解 |
| --- | --- | --- |
| R-DT-1 | runtime 视图交给 drivetrain 的承载形态未裁决（O-07）期间，组装方实现可能出现两种形态并存 | 本卡冻结值传递推荐（D-DT-13）；契约测试断言"无编译边"（红线扫描）——形态漂移在门禁可见 |
| R-DT-2 | 转子惯量与 DWC 合成惯量的防重复计入依赖组装方契约（跨单元事实无法在本卡强校验） | §9.4 契约口径＋黄金算例前提声明＋SEL-10 回填字段来源标记；风险在 selection/modeling 联动验收（AT-30）复核 |
| R-DT-3 | R2 矩阵条件性阈值来源未冻结（P-RT-7） | 映射侧与编译侧同源对齐；裁决前按设计默认留痕，不私设第二阈值 |
| R-DT-4 | 效率/损耗/再生能量的精确口径（机械 vs 电气边界） | §8.3/§10.5 冻结机械口径；电气功率明确不计算；报告限定语保留（RPT-05） |
| R-DT-5 | 直线传动/混合链跨单元启用顺序（SEL-09-S1 前置 MDL-12-S1 主责交叉） | §16.2 扩展点设计；阶段 C 不提前宣称；4/5 轴阻断不受影响 |
| R-DT-6 | R2 检查点与大规模批处理由 execution 承担的边界漂移 | 能力声明显式（不支持暂停）；R2 承接按 §16.3 组合口径，不含调度语义 |
| R-DT-7 | dynamics 卡未产出期间，§12.1 提议契约为单方面约定 | P-DT-6 登记；WP-18-T03 的 UpstreamResult 依赖声明以注册表校验兜底（键冲突/缺失在装配期可见） |

### 18.5 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-10-06 | 首版草案（WP-18-T01 交付物）：基于 REQUIREMENTS v1.16、ARCHITECTURE v0.13、DTB v0.53 与 core/evidence/runtime/modeling 卡既有契约编写；R1/R2 边界、归一化模型与身份、R1 对角映射与阻断面、R2 耦合矩阵、虚功/功率一致性四口径、反射惯量与防重复计入、效率/能量/四象限、工作点与包络、五单元协作与最小依赖契约（dynamics/selection 卡未产出）、公共接口、验证方案与故障注入矩阵、任务拆分对齐、追踪矩阵、决策/风险/待裁决登记、自审与未执行声明 |

---

## 19. 交付前自审

> 本节为**文档自审**（对照提示词检查清单逐项），不构成实现测试、构建或正式验收结论。

| # | 自审项 | 结论 |
| --- | --- | --- |
| 1 | 是否把 drivetrain 错写成 L4 业务插件 | 否——§2.3/§3.1 明确 L2 共享计算服务 |
| 2 | 是否强制创建不属于当前架构的 Qt plugin 或 worker | 否——§2.3/§4.2 明文禁止默认创建；例外走 P-DT-8 |
| 3 | 是否违反零 Qt 计算服务约束 | 否——§13.0/§13.7 纪律；无任何 Qt 依赖设计 |
| 4 | 是否直接依赖 dynamics/selection/modeling/runtime 私有头 | 否——依赖白名单仅 core/evidence（§3.2）；runtime 视图经值传递（P-DT-1） |
| 5 | 是否让业务单元之间形成直接依赖 | 否——消费全部经③端口/值传递/注入（§3.3/§12）；drivetrain 自身亦无业务边 |
| 6 | 是否把传动参数持久化错误归给 drivetrain | 否——modeling/project 拥有（§3.1"不拥有"） |
| 7 | 是否把 DriveTrainMappingEvaluator 重复实现于 dynamics 或 selection | 否——唯一实现（D-DT-2）；消费域只消费 |
| 8 | 是否把 R1 的对角传动误写成 R2 耦合矩阵已启用 | 否——§2.2 边界表＋§6.3 能力门控；T05 归 R2 |
| 9 | 是否在 R1 静默接受非对角矩阵 | 否——DT-MATRIX-NONDIAGONAL-LOCKED 阻断（§6.3） |
| 10 | 是否使用伪逆放行非方或病态矩阵 | 否——§7.3 使用前提；C⁺ 仅作已批准公式的计算表示 |
| 11 | 是否丢弃交叉耦合项 | 否——§7.1/§9.3 交叉项保留；AT-38 断言 |
| 12 | 是否使用准静态近似 | 否——§2.2/§7.3 明文禁止 |
| 13 | 是否混淆关节侧和电机侧量 | 否——§5.3/§5.4 轴系与单位逐字段标注 |
| 14 | 是否混淆位置映射和力矩对偶映射 | 否——§5.3 公式组分行给出（q=Cθ 与 τ_m=Cᵀτ_j 方向相反，全文一致） |
| 15 | 是否遗漏效率方向、再生制动和功率符号 | 否——§10.2/§10.5/§10.6（η⁺/η⁻、E_regen、四象限） |
| 16 | 是否重复计算转子惯量 | 否——§9.4 唯一计入位置＋壳体区分 |
| 17 | 是否把工作点超限直接判为整机工程不可行 | 否——§11.3（超限仅报告，判定权在证据规则） |
| 18 | 是否把反射惯量直接当成器件选型结论 | 否——§9.5（数值事实，筛选归 selection） |
| 19 | 是否把 evidence 等级、当前性或正式判定放进 drivetrain | 否——§12.3 正交表；Facts 只含事实（D-DT-10） |
| 20 | 是否把 execution 的 worker、取消、检查点和缓存淘汰写成 drivetrain 所有 | 否——§12.4（能力声明＋execution 所有） |
| 21 | 是否把 selection 的目录和筛选写成 drivetrain 所有 | 否——§3.1/§12.2（目录 schema 留在 selection） |
| 22 | 是否把 SEL-09-S1、MDL-12-S1 或 OPT-D 提前写成阶段 C 已实现 | 否——§2.2 R1 不启用面＋§16.2"不提前宣称" |
| 23 | 是否遗漏矩阵、传动比、效率模型和转子惯量的内容身份 | 否——§5.5 身份链逐项覆盖 |
| 24 | 是否在取消、失败、崩溃或数据不足后发布完整传动结果 | 否——§12.4（TASK-02 合法组合）＋§10.7 降级素材 |
| 25 | 是否遗漏历史结果不可变和迟到结果隔离 | 否——§5.5/§5.6（CON-02/05、五元组＋切片身份） |
| 26 | 是否允许显示单位影响计算身份 | 否——§5.4/§5.5 |
| 27 | 是否引入未经上游批准的新状态、阈值、证据等级或诊断码 | 否——DT-* 全部为建议值（注册归 diagnostics）；阈值仅复用 P-RT-7 对齐；无新状态/证据等级（P-DT-3 保守不引入运行阈值） |
| 28 | 是否越权修改需求、架构或其他单元机制 | 否——本文只新建 units/drivetrain.md；冲突全部登记（§18.2/§18.3） |
| 29 | 是否把当前只有 README 的代码保留位写成源码已实现 | 否——§1.2/§4.1 如实登记"尚未落位" |

**上游冲突集中清单**：本卡未发现 REQUIREMENTS/ARCHITECTURE 与本设计的**语义级冲突**；存在四项**待对齐**（非冲突）：O-07 消费形态（P-DT-1，已推荐值传递）、效率 schema 缺位（P-DT-9）、负传动比通道（P-DT-5）、ratio 口径表述统一（P-DT-10）——各项均已登记影响面、当前保守行为、裁决者与未裁决前的阻断范围（不阻塞阶段 C 其余设计；P-DT-9 未裁决前效率输入走 §10.7 降级路径，不影响已完成设计的其余部分）。

---

## 20. 未执行的实现测试、构建与 GUI 测试（如实声明）

| 类别 | 状态 |
| --- | --- |
| 产品源码/CMake/测试实现 | **未实现**（本卡为 WP-18-T01 文档交付物；落位动作归 WP-18-T02～T04） |
| 集成模式构建 / 独立冒烟构建 | **未执行**（无构建目标可构建） |
| 单元测试 / 契约测试 / 黄金数据集 | **未执行**（§14 全部为设计；"全部为设计"≠"已通过"） |
| ird_gates 门禁 | **未执行**（无源码改动；门禁随实现任务执行） |
| GUI 测试 | **不适用**（本单元无 plugin/GUI 目标，§14.4） |
| 文档验证脚本 | **未执行**（validate-docs.ps1 等治理脚本运行随 WP-18-T01 验收会话执行） |

以上任何一项在后续任务中执行后，须按 DTB §5.4 与本卡 §18.5 登记真实结果；未执行项不得标注"通过"。
