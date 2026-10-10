# selection 单元详细设计（L4 业务域）

| 字段 | 值 |
| --- | --- |
| 单元 | selection（**L4 业务域单元**——ARCHITECTURE §3.1/§3.3：计算库＋Qt Widgets 插件二分结构；从动力学与传动结果形成电机—减速器方案） |
| 文档版本 | v0.1（2026-10-06，首版草案，WP-19-T01 交付物） |
| 文档状态 | **`Draft`**（未冻结；本文只做详细设计，不自行宣布任何验收通过） |
| 主 WP | WP-19（SEL-01～10；组合校核经③端口消费唯一映射，不自建映射实现） |
| 上游 | `REQUIREMENTS.md` v1.16（`Accepted`，唯一需求权威）、`ARCHITECTURE.md` v0.13（`Draft`）、`development-task-breakdown.md` v0.53 |
| 构建落位 | `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/selection/`（**WP-19-T02 已落位**：计算库 STATIC＋_plugin 最小可注册实现＋_test/_contract_test 随落位登记；业务实现随 WP-19-T03～T11——见 §1.2/§3.5） |
| 实现口径 | 从头构建（REQUIREMENTS v1.9 确立）：不继承、不恢复 `old/` 历史实现 |
| 任务编号 | 本卡决策 `D-SEL-x`；待裁决项 `P-SEL-x`；诊断码前缀 `SEL-`（**已在** diagnostics.md §4.5 业务域命名空间清单登记，码值注册归 diagnostics StableCodeRegistry） |

> **文档地位**：本文是 selection 单元的唯一详细设计。依据 ARCHITECTURE §3.1（selection 行：目录管理、硬筛选、组合校核、可行集与淘汰原因、回填命令）、§3.3（业务域二分：`ird_selection` 计算库零 Qt ＋ `ird_selection_plugin` 界面）、§7.2①③（命令端口/评估器端口）、§7.10（选型消费传动映射）与 REQUIREMENTS SEL-01～10，把目录包数据模型与版本身份、导入与 io 交接、能力曲线插值、电机/减速器硬筛选、组合校核（消费唯一 `DriveTrainMappingEvaluator`）、可行集与逐项淘汰原因、Quick/Verified 证据边界、器件回填命令（经 ProjectCommandService）、公共接口与验证方案写到可直接实现的深度。本文不修改 REQUIREMENTS/ARCHITECTURE/其他单元卡；发现上游冲突或待对齐项一律登记（§19），不擅自解决。

## 目录

- §1 文档信息、输入版本与当前代码状态
- §2 需求承接、R1/R2 边界与单元定位
- §3 单元组成、依赖关系、目标布局和公共头文件
- §4 目录包、目录快照和版本身份
- §5 目录导入、io 交接、安全和业务校验
- §6 能力曲线、插值和外推边界
- §7 电机硬筛选
- §8 减速器硬筛选
- §9 drivetrain/dynamics 工作点和组合校核
- §10 可行集、稳定排序和逐项淘汰原因
- §11 Quick/Verified、evidence 和当前性边界
- §12 器件回填、project 命令、事务和复算
- §13 execution、project、evidence、drivetrain、dynamics、reporting 协作
- §14 公共接口、线程安全和生命周期
- §15 验证方案及故障注入矩阵
- §16 阶段 C 实现任务拆分（WP-19-T01～T12）
- §17 R2 阶段 D 承接和接口交接清单
- §18 需求—设计—验证追踪矩阵
- §19 设计决策、风险、待裁决项与变更记录
- §20 交付前自审
- §21 未执行的实现测试、构建和 GUI 测试

---

## 1. 文档信息、输入版本与当前代码状态

### 1.1 输入文件实测台账（2026-10-06 磁盘实况）

| 输入 | 磁盘版本/状态 | 本卡消费点 |
| --- | --- | --- |
| `REQUIREMENTS.md` | v1.16（2026-09-09 签署，`Accepted`） | SEL-01～10、SEL-09-S1、DYN-03/04、MDL-12/12-S1/16/21、CON-01～06、EVI-01/02（§8.1 表 1～4）、TASK-01～03、PM-12、OPT-03/05/06/07/08、NFR-COR/PERF/REL 家族、AT-08/09/10/19/27/30/34/36/38、附录 D（P-01 冻结产物） |
| `ARCHITECTURE.md` | v0.13（2026-09-27，`Draft`） | §3.1（selection 行）、§3.2（四红线）、§3.3（业务域二分结构）、§3.5（依赖表）、§4（任务/迟到结果）、§6.1（`catalog/` 存储）、§7.1（命令与可确认诊断流）、§7.2①③（端口）、§7.6（切片/包络/当前性）、§7.10（选型消费传动映射）、S4 走查（选型回填与复算提示）、§10.2（R2 不新增机制） |
| `DETAILED-DESIGN.md` | 索引（2026-09-22 状态行）：selection＝业务域、状态"待产出"、主 WP-G | 单元分类与"单元详设最低结构"要求 |
| `development-task-breakdown.md` | v0.53 | §2.20 WP-19-T01～T12（全量）、§4.2 O-07/O-11、§5 执行约定 |
| `traceability/unit-status.json` | selection：`status=planned`、`designCompletion=not-written`、`implementationStatus=not-verified` | 落位状态记录（本卡产出后其刷新归治理任务，本卡不代改） |
| `units/core.md` | v0.11（contract-review） | ObjectId/ContentIdentity/ContentDigester、TaskIdentity、EvaluationMode/TaskOutcome/EngineeringStatus、DiagCode 句法、Tolerance/比较工具、SourcedValue/ValueProvenance |
| `units/evidence.md` | v1.3（contract-review） | InputSlice/依赖七类/失效矩阵（§5.3 选型列）、IEngineeringEvaluator/注册表（§9）、§8.1 表 4 选型域 Profile、ResultEnvelope 合法组合 |
| `units/io.md` | 已产出（v0.9+ 多轮落位登记） | §7.8 目录包导入校验（SEL-01/02 支撑）：**文件层＝io**（清单核对/文件间引用存在性/路径与预算防护/CSV 通道），**业务层＝selection**（字段字典/单位/必填/唯一性/范围/插值语义）；P-IO-7（目录包文件清单/文件名结构契约由 selection 卡注册）；C-6（字段字典由格式所有者注册）；`catalog/<id>/<ver>/` 落位经 project 存储端口 |
| `units/project.md` | 已产出（v0.22 裁决销案登记） | `CommandService.hpp`（ICommandHandler/HandlerContext/CommandPlan/HandlerRegistry/ProjectCommandService——PRJ-T10 已落位）；StaleRevisionRejected＝`PRJ-STALE-REVISION-REJECTED`；S1～S6 命令编排（双编译、串行槽、修订身份单一分配点）；`catalog/<catalog-id>/<version>/` 行（只增、写入即锁定、不完整导入＝删除重导）；P-PR-9 已裁决销案（2026-10-10 RUL-TOK 批——commandType 无点词形终态，O-35 同案） |
| `units/policy.md` | 已产出 | P-POL-3（SEL-05 惯量比阈值归属**待裁决**；policy 侧预留"策略阈值条目扩展承载"schema 通道，不预填数值）；§4.5 未设置/无效/冲突/不适用四态；§7.4 阈值比较边界语义先例（`＞` 才超限、等于不超限） |
| `units/drivetrain.md` | v0.1（2026-10-06，**未入 git**——工作区未跟踪文件，如实登记） | 唯一映射实现 `DriveTrainMappingEvaluator`（键 `dt.mapping`）；§11 MotorOperatingPoint 字段表（selection 消费口径）；§12.2 selection 消费契约（③端口＋Configuration 值传递）；§9 反射惯量/惯量比数值事实；§16.2 SEL-09-S1 扩展点 |
| `units/modeling.md` | v0.39 | `robot-drivetrain` 对象（§4.7，回填目标）、`apply-drivetrain-design` 命令、I-MDL-11/12 传动合法性、MDL-05/06 物性断言与平行轴规则、四消费者分工 |
| `units/runtime.md`、`units/execution.md`、`units/diagnostics.md`、`units/reporting.md`、`units/ui.md` | 均已产出 | runtime：coupling/ratio 视图与能力声明；execution：TaskCapability/九态/RunRegistry；diagnostics：`SEL` 前缀已登记＋StableCodeRegistry；reporting：选型/BOM/淘汰依据章节（C 级）；ui：IUiTreeNodesProvider/IUiPropertyPagesProvider（UI-T21/T22 形状已冻结）、CommandRegistry（SA-16） |
| **`units/dynamics.md`**、**`units/optimization.md`** | **不存在**（待产出，WP-17-T01／WP-20 侧） | dynamics 为本卡上游（关节侧事实）提供者；optimization 为 OPT-D 消费方。本卡按既有先例给最小依赖契约并登记交接（§9.1/§17），不代写对方详设 |

### 1.2 代码落位实测与缺失项登记（2026-10-06 实测）

| 检查项 | 实测结果 | 结论 |
| --- | --- | --- |
| `selection/include/sdurws/ird/selection/` | 仅 `README.md` 占位（"源码随对应任务卡（见 units/selection.md §9）落地；本文件不参与编译"） | **未落位**。README 文案指向"卡 §9"与本卡章节编号不符（实现任务＝§16）——占位文案偏差随 WP-19-T02 落位时修正，登记于 §19.3 |
| `selection/src/`、`selection/plugin/`、`selection/test/`、`selection/contract_test/` | 不存在 | 未落位（WP-19-T02～T11 动作） |
| `selection/CMakeLists.txt` | 不存在 | 未落位（WP-19-T02 动作） |
| 目标 `sdurws_ird_selection`／`_plugin`／`_test`／`_contract_test` | 上级 `industrialrobot/CMakeLists.txt` 未注册 | 未落位（WP-19-T02 动作） |
| `worker/` 目录 | 不存在 | **符合本卡设计**——不默认创建 selection 专属 worker（§3.4），不是缺失项 |

> 如实声明：当前代码只有 README 保留位，**没有** selection 计算库、CMake、插件、worker 或测试实现；本卡是文档交付物（WP-19-T01），不把 README、任务登记、接口名称、UI 标签或目录占位描述为已实现功能。
>
> **WP-19-T02 落位登记（2026-10-06，构建落位任务——上表为 T01 时点实测，本注为落位后状态）**：`selection/CMakeLists.txt` 已建成——`sdurws_ird_selection` 由上级 INTERFACE 占位升级为真实 STATIC 库（C++17、PUBLIC 链 core＋evidence 两条 §3.2 编译链接边，ird_gates 白名单 "selection->…" 两行随任务登记并同步刷新 dependency-graph.json）；`sdurws_ird_selection_plugin` 最小可注册实现（自持描述符装配门面 `assembly/SelectionPluginAssembly.hpp`——pluginId/titleKey 均有 ui.md §11.1/§3.5 登记出处，零 ui 编译边；与 modeling/requirements/kinematics/workflow 插件门面的差异论证见单元 CMakeLists 头注）；`sdurws_ird_selection_test`/`_contract_test` 随落位登记（ctest LABELS ird）；`src/DiagCodes.cpp`＋`include/.../DiagCodes.hpp`＝SEL-* 17 码登记表（§2.2/§5.3/§6.2/§6.3/§9.3 登记值物化，L5 装配期注册数据源——依赖白名单无 diagnostics 编译边，kinematics 注册函数形态不适用，drivetrain WP-18-T02 同款承载；§10.3 词表逐 token 稳定码映射随 WP-19-T06）；占位 README 文案修正为落位说明版（§19.3 登记项闭环）；worker 目录仍不创建（§3.5/D-SEL-13，符合设计）。T03+ 源码集（目录模型/校验/插值/筛选/组合校核/可行集/回填）尚未落地——§3.5 布局表任务列为权威。
>
> **WP-19-T03 落位登记（2026-10-07，目录包模型与导入校验——SEL-01/02/AT-08）**：§3.5 布局表八头中的三公共头已落地——`CatalogTypes.hpp`（§4.1 数据模型全量：CatalogIdentity/CatalogVersion/CatalogSource/CatalogManifest＋FieldDictionary/FieldSpec＋Motor/GearboxCatalogEntry＋CompatibilityRecord＋§6.1 PerformanceCurve/CapabilityPoint＋§4.2 CatalogPackageSnapshot＋ParsedCatalogInput/CatalogValidationReport——io 解析产物的纯 std 承载形态＋规范序列化与内容身份）；`CatalogProvider.hpp`（§14.1 ICatalogProvider/§14.2 ICatalogValidator 接口＋CatalogImporter 校验装配器＋InMemoryCatalogProvider 内存锁定供给＋`catalogPackageFileSchema()` P-IO-7 注册面）；`Curve.hpp`（§14.3 IPerformanceCurveEvaluator/CurveQueryResult＋LinearCurveEvaluator）。实现翻译单元四件：`src/CatalogTypes.cpp`（规范序列化/SHA-256 内容身份——to_chars 最短 round-trip＋确定性排序）、`src/CatalogValidation.cpp`（§5.3 八码＋§6.3 四码全表＋v1 列契约注册表＋assemble 快照装配）、`src/CapabilityCurve.cpp`（§6.2 分段线性插值/默认禁止外推）、`src/CatalogProvider.cpp`（锁定只增/快照冻结/摘要不符 fail-fast）。落位细化七项登记于 §19.3（T03 ①~⑦）。§3.5 布局表的 Screening/Combination/FeasibleSet/ResultFacts/Backfill 五头与对应实现随 WP-19-T04~T09（§3.5 任务列为权威）；`catalog/<id>/<ver>/` 的 project 端口落位随 WP-19-T07（§14.1 头注已登记注入形态）。
>
> **WP-19-T04 落位登记（2026-10-07，电机/减速器硬筛选——SEL-03/04）**：§3.5 布局表第四公共头 `Screening.hpp` 已落地——§10.3 ReasonToken 封闭词表全表 34 token 物化（枚举序＝词表序＝§10.4 稳定排序键；`reasonTokenText()` 唯一映射点）＋§10.1 记录类型候选级承载（VerdictKind/DataGap/RejectionReason/FeasibilityRecord）＋筛选输入形态（ScreeningCriteria/JointMountRequirement/ExternalLoadFacts/AxisWorkpointFacts）＋§14.4 IHardConstraintSelector 接口与 HardConstraintSelector 唯一实现（ctx 复用 `evidence::IEvaluationContext` 取消查询形态——§14.9"已有形态则复用"）。实现翻译单元 `src/Screening.cpp`：§7 电机硬筛选（安装/连续转矩/峰值转矩/转速双子项/功率双口径/过载持续时间触发式/工作制/电压/温度降额/制动/保持/安全系数——逐维独立不短路）＋§8 减速器硬筛选（安装接口与方向/速比闭区间/额定与峰值输出转矩/输入转速（映射事实优先，ω_m＝ω_j/c 为唯一自算——§8.2）/效率/回程间隙/寿命/外载荷力臂核算）＋§10.2 校验边界 fail-fast（候选能力筛选不短路）＋§10.4 原因稳定排序＋批次边界取消截断。落位细化九项登记于 §19.3（T04 ①~⑨）。**惯量比维度不在 §7/§8 筛选范围**（O-11/P-POL-3 未裁决——§11.3 保守口径，实现未内嵌任何阈值数字；组合校核与 `inertia-ratio-policy-unsettled` 标记归 WP-19-T05）。§3.5 布局表余下 Combination/FeasibleSet/ResultFacts/Backfill 四头随 WP-19-T05~T09。
>
> **WP-19-T05 落位登记（2026-10-08，组合校核——SEL-05/DYN-04 消费）**：§3.5 布局表第五公共头 `Combination.hpp` 已落地（§3.1 组成表 CombinationCheck 组件的公共面）——组合身份与构造（§9.5 `makeDeviceCombinationId` 规范序列化 SHA-256＋§14.5 IDeviceCombinationBuilder/DeviceCombinationBuilder：兼容过滤〔零行语义〕/去重/批预算切分/组合键确定性）＋候选传动参数构造（§9.2 `makeCombinationDriveInputs`/`AxisDriveInput`/`CombinationDriveInput`——c＝1/n 单点换算）＋③端口消费的映射事实承载（P-SEL-1 提议契约 v1：`MappingBatchFacts`/`MappingCombinationFact`/`MappingAxisFact`/`AxisFactsBundle`/`CatalogLockPayload`——组合指派表随批结果值传递，管线②自足）＋惯量比可配置工程规则（`InertiaRatioRule`/`InertiaRatioCheck`——参考语义，未配置即"未判定"）＋多工况资格矩阵素材（`CaseCoverageEntry`）＋组合校核核心（`checkCombinations`——§9.3 清单十步）＋§9.6 `SelectionCheckResult` payload＋域内 canonical 编解码六面（IRDSLKV1/IRDSLBV1/IRDSAFV1/IRDSMBV1/IRDSCCV1/IRDSDCV1——drivetrain Codec 同款协议风格）＋`selUpstreamAnchor` 物化锚单点＋组合校核评估器（`makeCombinationCheckDescriptor`/`CombinationCheckEvaluator`/`CombinationCheckEvaluatorFactory`——评估键 `sel-combination-check` kebab 词形偏差与 drivetrain 同款先例，§9.2 管线②四条目声明）。实现翻译单元 `src/CombinationCheck.cpp`。Screening.hpp 仅表尾追加 `DeviceKind::Combination`（枚举追加纪律）＋头注边界 4 状态刷新。落位细化八项登记于 §19.3（T05 ①~⑧）。**SEL-05 红线机器可断言**：产品面零他单元 include＋零映射公式词表（契约测试扫描）＋c＝1/n 单点书写核对。§3.5 布局表余下 FeasibleSet/ResultFacts/Backfill 三头随 WP-19-T06/T09。

> **WP-19-T06 落位登记（2026-10-08，可行集与淘汰原因输出——SEL-06/EVI-01/EVI-02）**：§3.5 布局表第六公共头 `FeasibleSet.hpp` 已落地（§3.1 组成表 FeasibleSet 组件的公共面）——结果身份块 `IdentityBlock`（§4.3/§10.1/§11.2——目录/映射切片/评估切片/契约版本/评估模式）＋SEL-06 指标素材（`MarginFact`/`FeasibleCombinationMetrics`——§17.3 裕量含来源工况＋质量＋成本显式缺失）＋结果类型（§10.1 `FeasibleSet`/`SelectionRunResult`——metrics 为落位表尾扩展）＋§14.6 `IFeasibleSetBuilder`/`FeasibleSetBuilder`（§10.2 分层资格＋§10.4 稳定排序＋EVI-02 覆盖复核＋diagRef 输出回填）＋`IRejectionReasonProvider`/`RejectionReasonProvider`（词表唯一实现点——`ReasonContext` 落位承载＋diagRef 回填）＋SEL-06 指标计算 `computeFeasibleCombinationMetrics`（六驱动维裕量纯函数——指标面不参与判定）＋EVI-01 sel 域 Profile 注册面（`makeSelRequiredEvidenceProfile`——表 4 选型行必需 4 项/建议 2 项逐行实例化＋item 词表常量 `kSelProfileItem*`）。实现翻译单元 `src/FeasibleSet.cpp`。**DiagCodes 表尾追加 28 码**（§10.3"SEL-* 稳定码建议值随 WP-19-T06 注册"的兑现——电机族 11＋减速器族 9〔安装码共用〕＋惯量比未判定 1＋上游/数据 5＋边界/偏好 2；全表 45 码）＋`reasonTokenDiagCode()` 词表→稳定码唯一映射（34 token 全覆盖；复用码 6 token 不新造同义码）。**T05 评估器证据项对齐**：`CombinationCheckEvaluator` 证据产出由 1 项扩为 4 项（总证据项保留＋必需项①③④随 payload 登记；必需项②"每组合电机侧工作点"归 dt.mapping 评估器——项级分工不伪造上游证据）。落位细化八项登记于 §19.3（T06 ①~⑧）。§3.5 布局表余下 ResultFacts/Backfill 两头随 WP-19-T09 及后续任务。

> **WP-19-T07 落位登记（2026-10-08，优选品牌与目录版本管理——SEL-07/08）**：两公共头落地——`Preference.hpp`（第七公共头；§10.2/§10.4 分轨执行面：`EnterprisePreference` 企业偏好配置（优选品牌/供应状态/系列三维度白名单式）＋`CandidateEnterpriseFacts` 候选企业侧标注（供应状态/系列——v1 目录 schema 不扩大，vendor 商业权威＝目录条目）＋`PreferenceFilterOutcome` 偏好过滤结果（preferredVendorHit 标注面＋passesPreference＋preferenceReasons 分轨原因）＋`PreferenceFilter::apply` 五步语义——user-preference-filtered 独立 token 分轨、对硬筛选记录零改写、偏好原因唯一经 IRejectionReasonProvider::make 接口构造）＋`CatalogDiff.hpp`（§3.1 组成表 CatalogDiff 组件的公共面；§13.6 结构化差异：`CatalogDiff`/`ModelDiff`/`FieldChange`/`CurveDiff`/`CompatibilityDiff`＋`CatalogDiffer::compare` 纯函数——型号新增/移除/修改三态＋字段级增量（电机 20 组/减速器 18 组固定注册表序）＋曲线点集增量（同 x 变 y＝旧值移除＋新值加入）＋兼容关系全键增量；D-SEL-14 会话工具——static_assert 不适配 IEngineeringEvaluator 不进③端口）。实现翻译单元两件：`src/Preference.cpp`、`src/CatalogDiff.cpp`（数值文本＝to_chars 最短 round-trip 与 canonicalPackageText 同源）。SEL-08 锁定面：锁定语义执行面仍为 T03 InMemoryCatalogProvider（ICatalogProvider 注入接口不变——project 对象库真实存储适配随 L5 装配收编，selection 零 project 编译边为 §3.2 硬约束）；本任务交付"目录更新不静默改变历史结果"的锁定×比较组合用例钉扎。落位细化八项登记于 §19.3（T07 ①~⑧）。§3.5 布局表余下 ResultFacts/Backfill 两头随 WP-19-T09 及后续任务。
>
> **WP-19-T08 落位登记（2026-10-09，移动关节范围外诊断——SEL-09/D-SEL-15）**：零新公共头/零新翻译单元——阻断语义在既有两单元内落位：`Screening.hpp` 表尾追加 `JointKind` 枚举（Revolute/Prismatic——§17.1 modeling/runtime→selection 交接行"关节类型；旋转/移动能力"的轴侧值传递承载；权威判定与链型支持矩阵归 modeling MDL-12，selection 只消费声明）＋`AxisWorkpointFacts.jointKind` 字段（默认 Revolute——R1 产品管线中 modeling MDL-12 已在链型入口阻断混合链，能流转到选型的轴事实缺省即全旋转链成员；显式 Prismatic 是上游范围外信号；T04~T07 既有构造零破坏）＋`makeAxisOutOfScopeGap` 内联构造器（dimension="axis-out-of-scope"＋detail 文本唯一书写点——筛选器与组合校核共用，diagCode 以参数注入避免与 DiagCodes.hpp 循环 include）；`src/Screening.cpp`：§7/§8 两筛选入口的候选×轴遍历内对 Prismatic 轴先于全部旋转传动维度输出"范围外"记录（verdict＝DataInsufficient、reasons 恒空、gaps 恰一条 axis-out-of-scope 缺口〔diagCode＝SEL-INPUT-AXIS-OUT-OF-SCOPE——T02 批已登记码〕；记录数不变量＝候选数×轴数保持——截断感知计数不受影响；速比换算 ω_m＝ω_joint/c 对直线轴不执行）＋validateFacts 追加同轴 jointKind 一致性校验（矛盾＝物理矛盾 fail-fast——§10.2 校验边界）；`src/CombinationCheck.cpp`：checkCombinations ④ 维度（§14.5 契约注释 ①~⑩ 扩为 ①~⑪）——组合入口同轴一致性校验＋逐范围外轴恰一条缺口（轴级事实与工况无关，caseId 空串；不放入格循环防多工况重复计入）＋该轴跳过工作点/映射事实查询与 T04 筛选调用（workpoint-missing 覆盖缺口语义不适用）＋格级兜底（comboHasOutOfScopeAxis 且格聚合无轴记录可依〔全移动链〕时强制 DataInsufficient＋note="axis-out-of-scope"；混合链旋转轴既有 Rejected 保持原样——范围外不覆盖淘汰原因、不升级整机不可行）＋AxisFactsBundle 编码表尾追加 jointKind 字段（u8 枚举，kAxisFactsCodecVersion 1→2——字段面演进版本递增纪律；解码侧词表外枚举值严格拒绝，静默归默认会把范围外轴误降级为旋转轴）。落位细化五项登记于 §19.3（T08 ①~⑤）。**SEL-09 红线机器可断言**：移动关节轴零 RejectionReason（超限工作点反证——若套用旋转传动必产生 SEL-MOTOR-/SEL-GEARBOX- 前缀原因；测试内嵌该反证面）＋DataInsufficient 语义＋SEL-INPUT-AXIS-OUT-OF-SCOPE 稳定码（契约测试 OutOfScopeAxisNeverYieldsRejectionReason 三红线断言）。

> **WP-19-T09 落位登记（2026-10-09，选型回填命令与合成——SEL-10/MDL-16/AT-30）**：§3.5 布局表第八公共头 `Backfill.hpp` 已落地（§3.1 组成表 Backfill 组件的公共面）——§12.3 命令形态（token `apply-device-backfill` 无点冻结常量＋payload 版本 1＋参考系词表 `link-frame`）＋回填域值模型（`AxisBackfillEntry`/`DeviceBackfillRequest`——目录版本/型号/安装关系/锁定引用/传动比/连杆原值/两壳体物性与锚点/转子独立字段）＋AT-30 复算提示（`RecalcDomain` 四域封闭词表＋`BackfillRecalcNotice`——恒四域全量＋retainPriorConclusion 恒 false）＋§12.4 物性合成内核（`synthesizeAxisBodyProperties`——输入卫生→平行轴迁移原点→求和→回迁合成质心→MDL-06①~③同语义断言；壳体进合成、**转子独立登记不参与**）＋命令/记录载荷 canonical 编解码（IRDSBFP1/IRDSBFV1——小端定宽 IEEE754 位模式，严格解码分结构/版本两轨）＋§3.1 回填数据组装（`assembleDeviceBackfill`——目录取值/安装核对/数据缺失整体失败〔P-SEL-6 保守口径〕/合成预演＋`makeBackfillEnvelope`）＋§14.8 处理器（`IDeviceBackfillCommandHandler` 接口＋`DeviceBackfillCommandHandler` 实现——`planFromEnvelope` 计划内核〔版本受理→解码→域校验→逐轴合成断言→恰一 `sel-device-backfill` 对象写＋中文摘要〕＋`prepare` 直通壳）。实现翻译单元 `src/Backfill.cpp`。**DiagCodes 表尾追加 9 码**（SEL-BACKFILL- 族——§12.1 S3 判定序；全表 54 码）。**依赖形态登记（§3.2 增量修订）**：产品面 include 集合增列 project（include `CommandService.hpp` 实现 ICommandHandler 接口——**零编译链接边不变**：产品库不链 sdurws_ird_project、全文件零 project 实现符号引用〔HandlerContext 零调用——对象 oid 走 ObjectWrite 空值语义由 project S6 分配〕；两红线测试白名单同步——BuildRedLineTest kAllowedUnits/BuildGraphContractTest IncludeFace 集合）。落位细化九项登记于 §19.3（T09 ①~⑨）；跨命令编排对齐登记 P-SEL-9。**prepare 端到端边界如实登记**：HandlerContext 构造符号在 project 库内（selection 零链接边不可达）——prepare 直通壳的端到端验证（真实 ProjectCommandService submit→S1~S6→恰一新修订）随 L5 装配集成面承载，单元内以计划内核三态全覆盖＋壳转发代码面承载（不虚称端到端已执行）。

> **WP-19-T10 落位登记（2026-10-09，selection 插件界面——UX-02/UX-10）**：插件界面面在 T02 最小可注册形态上扩展落位——**assembly 门面扩展**：`SelectionPluginDescriptor` 增列 `stageToken`/`readinessDomainKey`/`commands`/`panels` 四个**字段同构自持**登记面字段（T02 两字段 pluginId/titleKey 逐字保留）；新增登记值类型 `SelCommandDescriptor`/`SelPanelRegistration` 与挂位/域键词表常量（`kSelStageToken`＝"selection"——ui.md §6.4 七阶段第 5；`kSelDomainKey`＝"selection"——§6.5；三页标题键 `kSel*PageKey`）；工厂升级为**装配载体类** `SelectionPluginAssembly`（unique_ptr 模块＋setServices/bind 系/session/refreshFromSession/module 转发面——dynamics WP-17-T09 同款结构）。**plugin/ 七文件**：`SelPanelTypes.hpp`（零 Qt 值类型半区——六服务缝〔目录行/候选行/原因行/缺口行/回填提交/可用性/文案解析〕＋会话态 `SelModuleSessionState`＋呈现 DTO 全自持〔SelCatalogRow/SelCandidateRow/SelRejectionRow/SelGapRow/SelRecalcNotice/SelBackfillRecord——零计算库结果类型耦合〕）、`SelPanelModel.hpp/.cpp`（零 Qt 模型层六流 L-S1~L-S5：就绪投影合成/目录行集归一/候选原因缺口行集透传＋范围外呈现键唯一书写点/回填提交三态＋AT-30 复算提示素材/文案解析哈希守卫）、`SelPanelCommandCatalog.hpp/.cpp`（域命令目录——回填入口一条，token 自持常量与计算库冻结词表逐字对账由契约测试钉住）、`SelPanelModule.hpp`（缝/会话态/面板引用归属）、`SelCatalogPanelWidget.hpp/.cpp`（主面板三页合一 Tab——工作流页〔就绪投影＋缺项＋回填入口＋最近提交＋复算提示〕/目录管理页〔版本清单〕/候选表页〔候选表＋原因明细＋缺口分轨〕；零 Q_OBJECT）、`SelectionPluginAssembly.cpp` 升级（描述符现产＋模块装配 API）。**app/HarnessMain.cpp＋`sdurws_ird_selection_app` 目标**：§15.3 GUI 手动点验通道载体（演示行集缝＋真实装配门面；qt.conf/DLL POST_BUILD——dynamics_app 同款）。**测试**：`test/SelPluginPanelTest.cpp` 13 用例（12 活跃＋1 GUI 呈现 envUnavailable GTEST_SKIP——§15.3 无人值守门禁不做 GUI 运行验证的如实登记）＋契约测试扩展 3 用例（登记面值断言/白名单八 token 自持词表对账/回填 token 与计算库冻结常量逐字对账）；BuildGraphContractTest allowed 集增登 `_app`；CMake 插件源列表＋_test 增链 _plugin＋Qt6::Core/Gui＋app 目标。落位细化七项登记于 §19.3（T10 ①~⑦）；**真实注册缺口登记 P-SEL-10**（B 方案——WP-17-T09 P-DYN-8 同款先例，见 §19.2）。

> **WP-19-T11 落位登记（2026-10-09，契约测试与选型黄金数据集——AT-08/AT-30）**：§15.1"黄金数据集 `testdata/golden/sel-*`"四类样例全部目录化落位——**sel-catalog-golden**（kind=contract-fixture；v1 合法目录包五文件〔方言标识行＋表头＋数据自物理行 3 起〕＋六个单点变异误例表＋正例装配黄金面〔条目值/missing 显式清单/装配序/包内容身份回填面〕＋逐误例期望校验报告〔SEL-CATALOG-RANGE-INVALID/DUPLICATE-MODEL/REF-DANGLING/SEL-CURVE-UNORDERED/FIELD-MISSING/UNIT-INVALID 逐 issue 定位〕）；**sel-screening-golden**（kind=analytic-case；四算例 24 记录——case-heavy 全维淘汰/缺口〔曲线区间外拒绝＝SEL-CURVE-EXTRAPOLATION-DENIED 缺口、力臂核算解析值 250 N、过载时长、制动/保持、工作制/电压/安装方向〕＋case-light 六候选基准可行面＋sf-edge 安全系数恰好满足边界〔240×1.25=300.0〕＋axis-oos 移动关节范围外；每个淘汰项含 actual/required/unit/thresholdSource 全字段——ERR-01）；**sel-combo-golden**（kind=analytic-case；四组合——K1 全维通过＋K2 转速/输入转速双超限＋K3 恰好满足边界〔250=250〕＋K4 兼容表无记录 ComboIncompatible 双保险面；映射事实 c＝1/n 解析换算黄金联动单点在生成脚本）；**sel-backfill-golden**（kind=analytic-case；§12.4 五步合成黄金逐分量＋三类组装拒绝＋复算提示恒值面）；容差档案 `testdata/tolerance/sel-golden/v1.0.0.json`（12 条目全部 appendixD-fixed——组合质量＋回填合成对照通道）。四数据集均按 DatasetManifest（ird-golden-manifest/1）登记：integrity 全覆盖（inputs＋expected＋generate 逐文件 SHA-256/size）＋coveredRequirements/coveredAt（AT-08/AT-30）＋referenceSource 独立声明＋edgeCases 三布尔＋generator committed＋history 首版。**消费测试**：`test/SelGoldenDatasetTest.cpp` 10 用例（GoldenFixture 六步装载——经 ICatalogValidator/ICatalogProvider/IHardConstraintSelector/checkCombinations/IFeasibleSetBuilder/assembleDeviceBackfill/IDeviceBackfillCommandHandler 公共接口消费，接口路径钉扎）＋**契约测试** `contract_test/SelGoldenDatasetContractTest.cpp` 4 用例（四数据集 load 全量校验构造性证明＋integrity 覆盖关系＋sel-golden 档案通道与保守纪律＋数据根布局；drivetrain GoldenDatasetContractTest 同款形态）。**红实验证（非恒真证明——执行留痕 traceability/builds/wp19-t11/execution-log.md §3）**：篡改层〔screening expected BrakeInsufficient required 25→26＋manifest 哈希同步重申报〕→ `SelGoldenScreening.ScreeningGoldenTables` **FAILED**（实测）→还原层〔node 重跑生成脚本 sha256 7a3631b845ee… 与篡改前申报值逐字节一致〕→复绿（实测）。落位细化四项登记于 §19.3（T11 ①~④）。

### 1.3 本卡与上游文档的编号口径

- 需求语义以 REQUIREMENTS 条目为自足定义（RV-13）：不重定义、不收窄、不扩大；阶段 C 任务以 DTB WP-19-T01～T12 为准（§16 只做对齐展开）。
- 诊断码 `SEL-*` 为**建议值**（码值分配与合法性权威＝diagnostics StableCodeRegistry；`SEL` 前缀已在业务域命名空间清单内，无补登事项）。
- 发布批次（R1/R2）与实施阶段（A～E）不是同一维度：R1/R2 是需求交付批次，阶段 C 是任务实施时点——R1 的 SEL 全家族在阶段 C 交付；R2 子项（SEL-09-S1）在阶段 D 交付（§2.2）。

---

## 2. 需求承接、R1/R2 边界与单元定位

### 2.1 需求条目承接（逐条：承接什么 / 不承接什么）

| 需求 | 本卡承接 | 明确不承接 |
| --- | --- | --- |
| **SEL-01** | 版本化电机/减速器 CSV 目录包**业务 schema**（清单、型号主表、能力曲线表、兼容关系表）；字段字典、版本和来源信息；目录包文件清单/文件名结构契约（P-IO-7 注册义务，§5.2） | 文件读取、SafePath/BudgetGuard、CSV 语法与转义 roundtrip、清单核对与引用存在性的**文件层执行**（io §7.8；本卡注册 schema 与消费其校验结果） |
| **SEL-02** | 导入校验的业务层：单位合法性、必填字段、型号唯一性（含稳定 ID 唯一）、数值范围、文件间引用**语义**、能力曲线分段线性插值、默认禁止外推 | 文件层校验执行（io）；路径穿越/资源预算（io） |
| **SEL-03** | 电机硬筛选编排：连续/峰值转矩、转速、功率、过载持续时间、工作制、电压、温度降额、制动/保持、安全系数（＋外形/接口兼容——兼容性筛选，非能力值判定） | 筛选阈值的工程策略权威（判定阈值若归 policy 按 O-11 同路径；用户筛选条件经 Configuration 条目声明并进入切片身份） |
| **SEL-04** | 减速器硬筛选编排：额定/峰值输出转矩、允许输入转速、速比、效率、回程间隙、寿命、安装方向、允许外载荷（＋安装接口兼容） | 同上 |
| **SEL-05** | 组合校核：电机—减速器—负载惯量、组合兼容、每轴工作点——**经共享 `DriveTrainMappingEvaluator`（③端口），不自建映射**；惯量比阈值＝可配置工程规则（归属 P-POL-3/O-11，本卡不自行冻结） | 传动映射、虚功、耦合矩阵、反射惯量公式（drivetrain 唯一实现）；RNEA（dynamics） |
| **SEL-06** | 可行组合、裕量、质量、成本、来源、逐项淘汰原因（含实际值与阈值） | 报告渲染（reporting）；正式工程判定（evidence） |
| **SEL-07** | 企业优选品牌、供应状态、系列限制；**硬能力判定不受优选影响**（偏好与硬能力分轨，§10.4） | 品牌数据的商业权威（企业目录内容本身） |
| **SEL-08** | 目录差异比较；项目锁定目录版本；目录更新不静默改变历史结果 | `catalog/` 存储实现与引用保护（project）；文件层比对（io） |
| **SEL-09** | R1 只支持旋转传动（电机＋减速器）；目标链含移动关节 → 该轴"范围外"诊断（DataInsufficient 语义），不静默套用旋转传动 | 直线传动目录与映射（SEL-09-S1，R2，§17.2） |
| **SEL-10** | 应用选型方案经领域命令更新各轴 `DriveTrainDesign`；记录目录版本与安装关系；按明确参考系合成质量/质心/惯量，区分壳体质量与转子等效惯性、禁止重复计入；应用产生新修订并提示依赖结果复算；复核完成前不沿用原通过结论 | 命令服务机制、事务、修订、双编译编排（project）；`robot-drivetrain` 对象 schema 权威（modeling）；编译（runtime） |
| DYN-03/04（消费） | 消费关节侧事实（峰值/RMS/序列）与电机侧工作点（唯一映射口径）；工作点超限仅产生候选淘汰原因 | 动力学计算与映射（dynamics/drivetrain） |
| MDL-12（边界） | R1 拒绝混合链选型（移动关节轴→范围外）；4/5 轴不放开 | 链型判定与支持矩阵（modeling §2.1） |
| MDL-16（衔接） | 回填合成遵守 MDL-05/16 物性规则（平行轴、来源标记、断言语义） | 物性断言设施所有权（P-SEL-6 登记） |
| MDL-21（边界） | selection 不做第二套矩阵验证；只检查映射结果完整性、身份一致性（AT-38 三方同口径） | 耦合矩阵映射（drivetrain）、建模（modeling） |
| CON-01～06 | 目录冻结与快照绑定（CON-01）；完整性/当前性/判定正交（CON-02）；缓存按切片身份（CON-04/05）；策略/名称身份经快照（CON-06——运行时名称先反解为对象 ID 才接纳，反解在接纳层） | 快照/切片构造（evidence）；归档（execution/project） |
| EVI-01/02 | 承接既有评估模式（Preview/Quick/Verified）；注册选型域 RequiredEvidenceProfile（§8.1 表 4 选型行）；必验工况覆盖 | 新增第三种证据等级（禁止）；证据等级/当前性/正式判定（evidence） |
| TASK-01～03 | 评估器能力声明（取消/分批/检查点）；取消/失败不发布完整可行集；五元组＋切片身份 | 调度/登记表（execution） |
| PM-12（衔接） | 回填基线＝当前分支 tip；URDF 基线修订只读、编辑发生在方案分支（回填经①端口落在方案分支） | 分支/修订实现（project） |
| OPT-03/05/06/07/08（衔接） | R1 OPT-B 不消费器件联合约束（阶段锁）；`drivetrain.ratio` StageB 可编辑编译进候选（V12-02）；R2 OPT-D 中 selection 为器件匹配内层（§17.3）；向 OPT-07 提供器件成本/质量/裕量事实 | 候选搜索/Pareto/鲁棒性/误淘汰审计/暂停继续/检查点调度（optimization/execution） |
| NFR-COR-01～04 | 曲线插值/筛选/组合校核黄金算例；稳定集合与排序；非有限/非法单位/引用缺失不静默通过；结果可定位到目录、工作点、输入快照 | 黄金数据集管理（testkit） |
| NFR-MNT-01/03/04/07 | 计算库零 Qt、模型测试直调；单位换算唯一实现归 core；不建无价值包装器；不做名称前缀操作 | — |
| NFR-PERF-01～03 | UI 线程不执行导入/曲线/筛选/校核（>1 s 转后台）；分页浏览不一次装入全部明细 | 任务调度（execution） |
| NFR-PERF-04～06（R2） | 大规模候选评估分批/流式（§17.3） | 并行调度与资源治理（execution） |
| NFR-REL-02/03 | worker 崩溃只致当前任务失败；中断不伪装完整 | worker 进程管理（execution） |

### 2.2 R1 / R2 边界表

**R1／阶段 C（本卡设计并承诺可实现的范围）**：

| # | R1 承诺 | 依据 |
| --- | --- | --- |
| R1-1 | 旋转传动器件目录（电机＋减速器）；目录包四件套＋字段字典/版本/来源 | SEL-01 |
| R1-2 | 目录导入双层校验（io 文件层＋selection 业务层）；导入失败不替换当前有效目录 | SEL-02、io §7.8 |
| R1-3 | 分段线性能力曲线；默认禁止外推（区间外查询→明确诊断） | SEL-02 |
| R1-4 | 电机硬筛选（SEL-03 全清单）＋减速器硬筛选（SEL-04 全清单）；逐项淘汰原因保留 | SEL-03/04 |
| R1-5 | 组合校核经唯一 `DriveTrainMappingEvaluator`；电机—减速器—负载惯量、兼容、每轴工作点 | SEL-05、ARCH §7.10 |
| R1-6 | 可行集＋裕量/质量/成本/来源＋逐项淘汰原因（实际值/要求值/单位/阈值来源） | SEL-06 |
| R1-7 | 优选品牌/供应状态/系列限制（与硬能力分轨）；目录差异比较；项目锁定目录版本；目录更新不静默改变历史结果 | SEL-07/08 |
| R1-8 | 移动关节轴"范围外"（DataInsufficient 语义）；不伪造电机工作点 | SEL-09 |
| R1-9 | 回填经 ProjectCommandService：新修订、目录版本/安装关系、壳体/转子区分合成不重复计入、复算提示、复核前不沿用原通过结论 | SEL-10、AT-30 |

**R1 目标链含移动关节时的纪律**：不得套用旋转传动；不得静默把移动关节转成旋转关节；不得伪造电机工作点；输出"范围外"诊断（`SEL-INPUT-AXIS-OUT-OF-SCOPE`）；结果按需求使用 DataInsufficient 语义；不得将该结果直接升级为整机工程不可行（判定权在 evidence 汇总规则）。

**R1 不启用（阻断面）**：直线传动目录与映射（SEL-09-S1，R2）；六/七轴含 prismatic 正式产品链（MDL-12-S1，R2，前置 SEL-09-S1）；OPT-D 联合硬约束与器件联合指标（阶段 D）；大规模并行/暂停继续/检查点规模化（R2）。

**R2／阶段 D 承接**（详见 §17）：

| 承接项 | 内容 | 边界声明 |
| --- | --- | --- |
| **SEL-09-S1**（选型层） | 滚珠丝杠/齿条/同步带/直线电机目录模板；直线传动能力曲线；直线传动工作点映射（drivetrain 扩展协作）；导入校验；可行＋不可行样例（不可行含实际值/阈值/原因） | **是选型层能力，不等于完整移动关节产品链已启用**；不改变既有六/七轴全旋转链行为（V12-03） |
| **MDL-12-S1**（产品链） | 六/七轴含 prismatic 正式计算/报告放开——前置 SEL-09-S1；端到端验收 AT-36（建模→运动学→轨迹→动力学→碰撞→传动→选型→报告） | 仅六/七轴；4/5 轴不自动放开（仍走需求变更）；planar/floating/闭环维持阻断；线性耦合链等 MDL-21（R2）；不得在 R1/阶段 C 文档中写成已实现 |
| **OPT-D 消费** | selection 作为器件匹配内层：结构/传动外层候选→轨迹校核→动力学校核→drivetrain 电机侧映射→selection 器件组合校核→联合硬约束与指标反馈（OPT-05） | selection 不实现候选搜索/Pareto/鲁棒性/灵敏度/误淘汰审计/暂停继续/优化检查点/优化器并行调度 |

### 2.3 单元定位（L4 业务域，二分结构）

- selection 是 **L4 业务域单元**：`sdurws_ird_selection`（计算库，L2 形态承载、零 Qt）＋ `sdurws_ird_selection_plugin`（Qt Widgets 插件界面）。**UI 插件只负责目录管理、筛选条件、候选表、淘汰原因和结果投影，不执行筛选计算**（机器可断言：插件翻译单元不含筛选/插值/校核函数，§3.4）。
- 不默认拥有独立 worker：批量筛选/组合校核经 execution 的工作进程承载（评估器在 worker 装配清单注册）；如确需专用 worker，登记 P-SEL-8 架构裁决通道。
- 不在 UI 线程执行目录导入、曲线计算、批量筛选或组合校核（NFR-PERF-01）。

---

## 3. 单元组成、依赖关系、目标布局和公共头文件

### 3.1 组成

```
sdurws_ird_selection（计算库，零 Qt）
  ├─ CatalogModel        目录包业务模型与版本身份（§4）
  ├─ CatalogValidation   业务校验（§5）＋字段字典注册（P-IO-7/C-6 注册面）
  ├─ CapabilityCurve     分段线性插值与外推边界（§6）
  ├─ Screening           电机/减速器硬筛选（§7/§8）
  ├─ CombinationCheck    组合校核评估器（sel.combination-check，③端口；§9）
  ├─ FeasibleSet         可行集/稳定排序/淘汰原因（§10）
  ├─ CatalogDiff         目录差异比较（§13.6）
  └─ Backfill            回填数据组装＋DeviceBackfillCommandHandler（§12）
sdurws_ird_selection_plugin（Qt Widgets，零计算逻辑）
  ├─ 目录管理页／筛选条件表单／候选表／淘汰原因视图／回填入口／进度与取消
  └─ IUiTreeNodesProvider/IUiPropertyPagesProvider 域供给（UI-T21/T22 协议）
```

### 3.2 依赖关系（ARCH §3.2/§3.5 口径自查）

| 边 | 形态 | 说明 |
| --- | --- | --- |
| selection 计算库 → core | 接口依赖 | 身份/内容摘要/比较工具/单位/词表/诊断承载 |
| selection 计算库 → evidence | 接口依赖 | IEngineeringEvaluator/切片/Profile/包络契约（评估器与组合校核实现） |
| selection 计算库 → policy/io/project/diagnostics/ui/runtime/drivetrain | **运行时注入/端口**（不落编译链接边） | ④策略解析（若 O-11 裁决归 policy）、io 校验结果与文件层设施、①命令端口与 HandlerRegistry 注册、诊断 sink、树/属性页协议、⑥名称端口、③端口注册的 `dt.mapping` |
| selection 计算库 ⇢ project 公共头（**include 面，非链接边**） | 接口实现形态消费（WP-19-T09 增量登记，DTB §5.4） | §14.8 回填处理器实现 `project::ICommandHandler` 需 include `CommandService.hpp`（R-2 许可的公共头消费）——**零编译链接边不变**：产品库不链 sdurws_ird_project（ird_gates SUB 面与配置期守卫钉住）、全文件零 project 实现符号引用（`HandlerContext` 零调用——对象 oid 走 `ObjectWrite.objectId` 空值语义由 project S6 分配、基线判定全经 `baseSnapshot` 值快照；两红线测试白名单同步承载） |
| selection_plugin → 计算库 | 单元内依赖 | 同单元插件消费自家计算库（非跨单元边） |
| selection_plugin → Qt Widgets | 允许（L4 插件界面目标） | R-3 例外边界＝插件目标；计算库零 Qt |

**红线自查**：R-1（不链接其他业务域单元——modeling/requirements/kinematics/trajectory/dynamics/optimization 零编译边）；R-2（只经各单元公共头/端口；dynamics/modeling/requirements/optimization 私有头禁止，公共头也不 include——依赖白名单未登记业务间边；**例外＝project 公共头**：WP-19-T09 增量登记的接口实现形态消费〔§14.8 处理器——零链接边，见上表末行〕）；R-3（计算库零 Qt；插件目标为例外类）；R-4（零名称前缀操作，对象名呈现经⑥端口）。**如果实现期 CMake 依赖与本表不一致，登记为待裁决项，不自行扩大依赖边**（P-SEL 通道；见 P-SEL-2 关于 selection↔drivetrain 是否落编译边的登记）。

### 3.3 消费通道总图

```
 io(文件层校验/SafePath/BudgetGuard/CSV 通道/转义)   project(catalog/<id>/<ver> 落位与锁定；①命令端口)
        │ 解析后字段＋来源身份＋行级错误                        ▲ 回填命令（ICommandHandler）
        ▼                                                    │
 selection 计算库 ──目录快照──► sel.combination-check 评估器 ──可行集/淘汰原因──► evidence(包络/当前性/判定)
        ▲                        ▲                                     │
        │ 目录对象(锁定版本)       │ dt.mapping 电机侧工作点(③端口 UpstreamResult)   ▼
 modeling/runtime(轴/关节类型/  dynamics(关节侧事实: 峰值/RMS/序列/工况)      reporting(选型章节)
 传动配置/能力声明——值传递)     execution(任务/取消/检查点/worker/缓存)        optimization(R2 OPT-D 内层消费)
```

### 3.4 插件零计算红线（机器可断言）

- 插件翻译单元不包含筛选/插值/校验/排序算法（静态扫描：筛选词表函数符号零命中）；
- 插件不直接读文件、不直接写项目、不直接调用 io/project 实现（经端口）；
- 编辑/回填提交经页面出口→域命令→①命令端口（UI-T22 IFormEditOutlet 形态）；
- 命令经 ui CommandRegistry 装配期登记（SA-16；重复绑定注册边界拒绝）。

### 3.5 目标布局（全部为设计；落位动作归 WP-19-T02 及后续任务）

```
selection/
├── include/sdurws/ird/selection/
│   ├── CatalogTypes.hpp      # CatalogPackage/MotorCatalogEntry/GearboxCatalogEntry/
│   │                         # CompatibilityRecord/CatalogIdentity/CatalogVersion——§4
│   ├── CatalogProvider.hpp   # ICatalogProvider/ICatalogValidator/CatalogValidationReport——§14
│   ├── Curve.hpp             # IPerformanceCurveEvaluator/CapabilityPoint/插值契约——§14
│   ├── Screening.hpp         # IHardConstraintSelector（电机/减速器）——§14
│   ├── Combination.hpp       # IDeviceCombinationBuilder/DeviceCombination/AxisDeviceAssignment——§14
│   ├── FeasibleSet.hpp       # IFeasibleSetBuilder/FeasibilityRecord/RejectionReason——§14
│   ├── ResultFacts.hpp       # ISelectionResultProvider/ISelectionEvidenceFactsProvider——§14
│   └── Backfill.hpp          # IDeviceBackfillCommandHandler/DeviceBackfillCommand——§14
├── src/                      # 计算库唯一实现
├── plugin/                   # Qt Widgets 插件（零计算逻辑）
├── test/                     # 单元测试（黄金数据集 sel-*）
├── contract_test/            # 契约测试（③端口/①端口/io 交接）
└── CMakeLists.txt            # 计算库 STATIC（链 core＋evidence）＋ _plugin ＋ _test ＋ _contract_test
```

| 目标 | 形态 | 依据 |
| --- | --- | --- |
| `sdurws_ird_selection` | STATIC（别名 `RWS::ird::selection`）；C++17；零 Qt；模型测试直调 | ARCH §3.3、NFR-MNT-01 |
| `sdurws_ird_selection_plugin` | 插件界面目标（Qt Widgets；静态白名单装配 SA-01） | ARCH §3.3、DTB WP-19-T02/T10 |
| `sdurws_ird_selection_test` / `_contract_test` | gtest（DTB §5.5 接入） | DTB WP-19-T11 |
| ~~`sdurws_ird_selection_worker`~~ | **不默认创建** | §2.3；如需 → P-SEL-8（说明 execution worker 为何不足、所有权、避免业务互链、零 Qt、评估器注册、取消/检查点/崩溃处理、架构裁决） |

---

## 4. 目录包、目录快照和版本身份

### 4.1 目录包数据模型（核心类型，签名＝设计基线）

```cpp
// 目录包业务模型：selection 拥有业务 schema；字节级解析归 io（§5）。零 Qt；构造入口结构校验。
struct CatalogIdentity {                       // 目录身份（显示名称永不替代身份——ARC-04/CON-01 纪律）
    std::string catalogId;                     // 稳定目录 ID（企业分配；包内 manifest 声明）
    std::string version;                       // 目录版本号（同 catalogId 多版本并存）
    core::ContentIdentity contentIdentity;     // 包 canonical 字节摘要（SHA-256，core ContentDigester）
    std::string source;                        // 来源信息（SEL-01：企业来源描述——非文件路径身份）
};
struct CatalogVersion {                        // 锁定版本引用（项目内 catalog/<catalogId>/<version>/）
    CatalogIdentity identity;
    core::ObjectId lockObjectId;               // 锁定对象 ID（project 对象库内；切片 Object 条目引用）
};
struct CatalogSource {                         // 来源与导入追溯
    std::string description;                   // 企业来源描述（显示用）
    core::ContentIdentity importedFromRevision;// 导入时项目修订（追溯）
};
struct CatalogManifest {                       // 包清单（selection 注册给 io 的结构契约——P-IO-7 消账面）
    std::string formatVersion;                 // 目录包 schema 版本（NFR-DEP-04 精神；拒绝未知格式）
    CatalogIdentity identity;                  // catalogId/version/contentIdentity/source
    std::vector<ManifestEntry> files;          // 文件清单（名称/角色/必备性/字节哈希）
    FieldDictionary fieldDictionary;           // 字段字典（列名→语义/单位/必填性——SEL-01）
};
struct MotorCatalogEntry {                     // 电机型号主表行（业务模型，非 CSV 行）
    core::StableId modelId;                    // 稳定型号 ID（包内唯一；跨版本同 ID 同型号）
    std::string vendor;                        // 厂商（显示用）
    std::string displayName;                   // 型号显示名（可重复——不参与身份）
    CatalogIdentity catalog;                   // 所属目录版本
    double ratedTorque;                        // 额定连续转矩 N·m
    double peakTorque;                         // 峰值转矩 N·m
    double ratedSpeed;                         // 额定转速 rad/s（目录若以 rpm 提供经字段字典声明换算口径）
    double maxSpeed;                           // 最高转速 rad/s
    double ratedPower;                         // 额定功率 W
    std::optional<OverloadSpec> overload;      // 过载能力＋过载持续时间 s（可缺失→标记）
    std::string dutyClass;                     // 工作制（词表：S1/S2/... 由字段字典登记）
    std::optional<double> ratedVoltage;        // 额定电压 V（可缺失→标记）
    std::optional<ThermalDerating> thermal;    // 温度降额（环境温度 °C→能力系数，曲线或表）
    std::optional<double> brakeTorque;         // 制动能力 N·m（可缺失→标记）
    std::optional<double> holdingTorque;       // 保持能力 N·m（可缺失→标记）
    double rotorInertia;                       // 转子惯量 kg·m²（回填/映射输入）
    double mass;                               // 质量 kg（壳体合成输入）
    MountSpec mounting;                        // 外形和安装接口（兼容性筛选用）
    std::vector<CurveRef> curves;              // 能力曲线引用（§6）
    std::vector<MissingField> missing;         // 缺失字段清单（显式标记，不伪造数值——ERR-01）
    ValidationStatus status;                   // 校验状态（Valid/Partial/Invalid）
};
struct GearboxCatalogEntry {                   // 减速器型号主表行
    core::StableId modelId;                    // 稳定型号 ID
    std::string vendor;
    std::string displayName;
    CatalogIdentity catalog;
    double ratedOutputTorque;                  // 额定输出转矩 N·m（输出轴系）
    double peakOutputTorque;                   // 峰值输出转矩 N·m
    double maxInputSpeed;                      // 允许输入转速 rad/s
    double ratio;                              // 速比（正值；方向语义按 §9.4 与 drivetrain c 口径换算）
    double efficiency;                         // 效率（无量纲 (0,1]）
    std::optional<double> backlash;            // 回程间隙（单位按字段字典：arcmin 或 rad——声明）
    std::optional<double> ratedLife;           // 额定寿命（小时/循环数——单位按字段字典声明）
    std::string mountingOrientation;           // 安装方向词表（字段字典登记）
    std::optional<ExternalLoadSpec> extLoad;   // 允许外载荷（径向/轴向 N＋作用点）
    double mass;                               // 质量 kg
    std::optional<double> housingInertia;      // 壳体惯量或相关惯量字段 kg·m²（合成输入；可缺失→标记）
    MountSpec mounting;                        // 安装接口
    std::vector<CurveRef> curves;
    std::vector<MissingField> missing;
    ValidationStatus status;
};
struct CompatibilityRecord {                   // 兼容关系表行（电机—减速器）
    core::StableId motorId;
    core::StableId gearboxId;
    std::string mountKind;                     // 安装关系（法兰/轴伸……词表）
};
```

> **不引入 `DriveCatalogEntry`**：当前需求 SEL-01～10 明确规定的是**电机和减速器**目录、筛选和组合；不从"驱动器"泛化出未经需求批准的独立驱动器目录/算法/硬约束体系。电压、制动、保持能力是**电机条目上的现有字段**（上表），不是独立器件类型。若未来需求变更新增，走需求变更流程（本卡不留空模块）。

### 4.2 目录快照与项目绑定

- 目录数据冻结：导入成功 → 形成不可变目录快照（CatalogPackageSnapshot＝四表业务模型的规范序列化）→ 经 project 存储端口落 `catalog/<catalogId>/<version>/`（**写入即锁定、只增**——project.md O-12/§5.6 表；不完整导入＝删除重导、不留目标）。
- 项目绑定：目录快照与项目、分支和修订的绑定通过**锁定对象引用**（lockObjectId 进入对象库，随修订引用）——后续评估切片以 Object 条目（lockObjectId＋ContentVersion）引用锁定版本；**文件路径不作为目录身份**（身份＝CatalogIdentity＋内容摘要）。
- 同型号不同目录版本：`(catalogId, version, modelId)` 三元组唯一确定一个目录条目实例；显示名可重复、不参与身份。
- 目录内容身份：包 canonical 字节 SHA-256（core ContentDigester 唯一哈希路径）；变更任何字节 → 新内容身份 → 依赖该目录的切片失效。
- 历史目录：锁定版本只增不删；历史选型结果继续引用其原目录版本对象（CON-02 历史证据），目录更新**不静默改变历史结果**（历史结果的当前性按其切片身份独立判定——目录能力曲线变更→依赖该目录条目的选型切片失效→Superseded，与 evidence §5.3"目录能力曲线"行一致）。

### 4.3 身份关系表

| 身份 | 计算对象 | 用途 |
| --- | --- | --- |
| CatalogIdentity | (catalogId, version, 包内容摘要) | 目录条目归属与版本区分 |
| lockObjectId＋ContentVersion | 项目内锁定版本对象 | 切片 Object 条目（缓存键/失效判据——CON-05） |
| modelId（稳定 ID） | 目录内型号 | 候选身份、兼容关系、淘汰原因定位 |
| DeviceCombinationId | 轴×(电机,减速器) 组合键 | 组合身份（§9.5） |
| sliceId | 评估切片全部冻结条目 | 缓存键、当前性判据（evidence §5.1） |
| TaskIdentity 五元组 | projectId/branchId/revisionId/runId/attemptId | 迟到结果绑定（TASK-03）＋切片身份 |

显示名称、品牌名称、型号显示名**永不**替代稳定对象 ID；显示单位切换不改变计算身份（AT-27）。

### 4.4 单位表（选型输入/输出；单位进入诊断比较字段——ERR-01）

| 物理量 | 单位 | 说明 |
| --- | --- | --- |
| 转矩（电机/减速器/工作点） | N·m | 目录若以其他单位提供，经字段字典声明换算（core 唯一换算） |
| 角速度 | rad/s | 同上（rpm 输入口径在字段字典声明） |
| 角加速度 | rad/s² | — |
| 功率 | W | 机械功率 |
| 转动惯量（转子/壳体/负载） | kg·m² | 逐字段标注参考系（§12.4 合成） |
| 质量 | kg | — |
| 温度 | °C | 温度降额环境温度 |
| 电压 | V | 电机额定电压 |
| 时间 | s | 过载持续时间 |
| 效率 | 无量纲 | (0,1] |
| 回程间隙 | arcmin 或 rad | **按字段字典声明**（目录原单位经换算进 SI；比较在 SI 域） |
| 寿命 | h 或循环数 | 按目录定义、字段字典声明单位 |
| （R2 移动关节扩展）位移/速度/加速度/力 | m / m/s / m/s² / N | SEL-09-S1（§17.2），R1 不使用 |

---

## 5. 目录导入、io 交接、安全和业务校验

### 5.1 固定交接流程

```
用户选择外部目录包
        ↓
io 一次性读取（SafePath/BudgetGuard/CSV 数据解析——公式转义 roundtrip 归 io）
        ↓
io 文件层校验（§7.8：文件清单核对/文件间引用存在性/路径与预算防护）
        ↓
selection 字段映射（字段字典注册面 C-6：列名→语义/单位/必填性）
        ↓
selection 业务校验（单位/必填/引用语义/唯一性/范围/能力曲线——§5.3）
        ↓
CatalogPackageSnapshot（不可变快照＋CatalogValidationReport）
        ↓
经 project 存储端口落 catalog/<id>/<ver>/（锁定；不完整＝删除重导）
        ↓
筛选、组合校核和结果归档（§7~§11）
```

**边界纪律**：io 负责文件读取、安全、路径、压缩包和 CSV/JSON **语法**；selection 负责**业务 schema 与语义**；selection 不绕过 io 直接读取文件；CSV 只按数据解析（不执行公式/命令）；CSV 转义 roundtrip 归 io（NFR-SEC-03）。外部 CSV 公式字符串、路径穿越、超预算样例由 io 层拒绝（本卡验证方案含联合用例，§15）。

### 5.2 目录包文件清单 schema（P-IO-7 注册义务——io 卡明示"由 selection 卡注册"）

| 文件 | 角色 | 必备性 | 通道 | 关键列（字段字典承载） |
| --- | --- | --- | --- | --- |
| `manifest.json` | 包清单：formatVersion/catalogId/version/来源/文件清单（名称+SHA-256）/字段字典 | 必备 | io JSON 通道 | — |
| `motors.csv` | 电机型号主表 | 必备 | io CSV 通道（方言标识+转义 roundtrip） | model_id、vendor、display_name、rated_torque_nm、peak_torque_nm、rated_speed、max_speed、rated_power_w、overload_*、duty_class、rated_voltage_v、thermal_*、brake_torque_nm、holding_torque_nm、rotor_inertia_kgm2、mass_kg、mounting_*、curve_ref… |
| `gearboxes.csv` | 减速器型号主表 | 必备 | 同上 | model_id、vendor、display_name、rated_output_torque_nm、peak_output_torque_nm、max_input_speed、ratio、efficiency、backlash、rated_life、mounting_orientation、external_load_*、mass_kg、housing_inertia_kgm2、mounting_*、curve_ref… |
| `capability_curves.csv` | 能力曲线表（§6） | 必备（可含零行——无曲线的条目按"固定额定值"口径，§6.4） | 同上 | curve_id、owner_kind(motor|gearbox)、owner_model_id、x_quantity、x_unit、y_quantity、y_unit、x_value、y_value、point_index |
| `compatibility.csv` | 兼容关系表 | 必备（可含零行＝全不兼容声明？——**零行语义＝包内无预声明兼容对**，组合兼容校核按"无记录即不兼容"执行，§9.3） | 同上 | motor_model_id、gearbox_model_id、mount_kind |

- 文件名/清单结构由本卡冻结为注册形态（P-IO-7 消账：io 卡 §7.8 校验执行框架按本表执行清单核对与引用存在性；该注册随 WP-19-T03 落位同步 io 侧登记，不在本卡直接修改 io.md）。
- `formatVersion` 未知 → 拒绝导入并给升级指引（不自动升级——PM-06 精神）。

### 5.3 业务校验清单（selection 层；逐项可定位到文件/行/列）

| 校验 | 失败诊断（建议码） | 说明 |
| --- | --- | --- |
| 字段字典完整（manifest 声明列与 CSV 表头一致） | `SEL-CATALOG-SCHEMA-MISMATCH` | 多列/缺列逐列定位 |
| 单位合法（单位词表＋量纲检查） | `SEL-CATALOG-UNIT-INVALID`（比较型：实际/期望/单位） | 未知单位拒绝，不猜测 |
| 必填字段缺失 | `SEL-CATALOG-FIELD-MISSING`（逐字段） | 可缺失字段显式入 missing 清单（不伪造） |
| 型号显示名重复（同稳定 ID）或稳定 ID 重复 | `SEL-CATALOG-DUPLICATE-MODEL` / `SEL-CATALOG-DUPLICATE-ID` | 重复型号指同 ID 多行；显示名重复但 ID 不同→**合法**（登记于验证矩阵） |
| 数值范围（转矩>0、效率∈(0,1]、速比>0、寿命>0 等） | `SEL-CATALOG-RANGE-INVALID`（实际值/期望范围/单位） | 非有限数同路径（NFR-COR-03） |
| 文件间引用语义（curve_ref→曲线存在且 owner 匹配；compatibility 引用双方存在） | `SEL-CATALOG-REF-DANGLING` | 文件层存在性由 io 先行；语义层（owner 匹配）归本卡 |
| 兼容关系冲突（同型号对多行且 mount_kind 矛盾） | `SEL-CATALOG-COMPAT-CONFLICT` | — |
| 能力曲线校验（§6.3） | `SEL-CURVE-*` 族 | — |

- **导入失败不替换当前有效目录**；成功导入先形成不可变快照再落位；目录版本和来源必须进入输入身份（§4.3）。
- 导入过程**可取消**（io 通道取消语义）；取消/失败不得留下半成品目录（io TempArea＋project 端口"删除重导"）。

---

## 6. 能力曲线、插值和外推边界

### 6.1 曲线模型

```cpp
struct CapabilityPoint { double x; double y; };          // 单位随 CurveRef 声明（SI 域比较）
struct PerformanceCurve {
    core::StableId curveId;
    std::string xQuantity, yQuantity;                    // 量纲 token（字段字典词表：speed/torque/power/…）
    std::string xUnit, yUnit;                            // SI 单位（rad/s、N·m、W…）
    std::vector<CapabilityPoint> points;                 // 构造入口排序＋校验
    CatalogIdentity catalog;                             // 曲线版本＝所属目录版本
    core::ContentIdentity contentIdentity;               // 曲线内容身份（点集规范序列化摘要）
};
```

### 6.2 插值规则（分段线性，SEL-02 冻结语义）

- 横坐标**升序排序**后分段线性插值；查询点落在 [x_min, x_max] 闭区间内才允许；
- **默认禁止外推**：区间外查询 → `SEL-CURVE-EXTRAPOLATION-DENIED`（比较型：实际输入点/有效区间/单位）——**不自动使用最近点、不静默外推**；
- 边界点：x＝x_min 或 x_max 返回端点值（闭区间含端点）；
- 插值结果携带：实际输入点、插值区间（[x_i, x_i+1]）、曲线版本（CatalogIdentity）与单位——供淘汰原因与追溯（NFR-COR-04）；
- 数值比较使用 core 统一比较规则与附录 D（本卡不自定义与附录 D 冲突的容差）；
- **能力曲线插值失败 ≠ 候选能力不足**：插值失败（区间外/曲线缺失/数据坏）→ 数据不足类标记；插值成功但值不达标 → 能力不足类淘汰原因——两者分轨（§10.3）。

### 6.3 曲线校验（导入期）

| 校验 | 诊断（建议码） |
| --- | --- |
| 采样点无序（未按 x 升序提交） | `SEL-CURVE-UNORDERED`（构造入口排序后继续？——**否**：拒绝，要求目录修正——避免排序掩盖目录错误） |
| 重复横坐标（同 x 不同 y） | `SEL-CURVE-DUP-X` |
| 非有限点 | `SEL-CURVE-NONFINITE`（NFR-COR-03） |
| 区间不合法（x_max ≤ x_min、单点曲线声明为曲线） | `SEL-CURVE-INTERVAL-INVALID` |
| 曲线缺失（条目引用了不存在的 curve_id） | `SEL-CATALOG-REF-DANGLING` |

### 6.4 固定额定值口径与缺失曲线

- 若某能力字段需求允许以固定额定值参与筛选（如无转矩-转速曲线时以额定转矩常值）：其来源必须显式（目录额定字段＋"固定值"标记）——**不得用额定值伪造缺失的能力曲线**（不生成假曲线点）；
- 缺失曲线且无固定值依据 → 该能力维度对该条目为"数据不足"（显式标记），不默认通过（NFR-COR-03/EVI 纪律）；
- 多曲线引用：一条目多曲线按 (quantity) 区分用途（转矩-转速、效率-转速…）；同 quantity 多曲线 → 导入拒绝（歧义）。

---

## 7. 电机硬筛选

### 7.1 筛选流程图

```
候选电机条目（目录快照）
   │ ①身份与校验状态检查（Invalid 条目不参选；Partial 条目按缺失字段维度降级）
   │ ②接口/安装兼容（MountSpec vs 关节安装关系——兼容性判定，非能力值）
   │ ③逐能力维度判定（对照工作点与筛选条件，全部维度独立执行——不短路）
   │     连续转矩：τ_rms工作点 ≤ 额定连续转矩（×温度降额系数，若启用）
   │     峰值转矩：τ_peak工作点 ≤ 峰值转矩（×过载允许：过载持续时间窗内核查）
   │     转速：ω_peak工作点 ≤ 最高转速；ω_rms工作点 ≤ 额定转速（按字段字典口径）
   │     功率：P_peak/P_rms ≤ 额定功率（或按能力曲线查询）
   │     过载持续时间：峰值段时长 ≤ 目录过载持续时间（τ_peak>额定段）
   │     工作制：需求工作制 ∈ 目录工作制词表
   │     电压：需求电压匹配目录额定电压（字段缺失→该维度数据不足）
   │     温度降额：环境温度下按降额曲线/表折减后复判连续/峰值转矩
   │     制动/保持：保持工况需求保持转矩 ≤ 制动转矩/保持能力（无保持工况需求则不适用）
   │     安全系数：τ/ω/P × 要求安全系数后复判（要求值来自筛选条件——Configuration 条目）
   │ ④汇总：全部维度结果 → FeasibilityRecord（§10）——通过/淘汰/数据不足逐维记录
   ▼
电机候选资格（逐轴）
```

### 7.2 筛选纪律

- **逐维度独立执行，不因第一个失败丢失其他独立淘汰原因**（SEL-06；短路仅允许在"校验边界快速拒绝"的致命输入错误：schema/身份/维度/非有限值——§10.2）；
- 每一失败维度产生一条淘汰原因（实际值/要求值/单位/阈值来源，§10.3）；
- 筛选条件（要求安全系数、电压、环境温度、工作制需求等）作为 `config.sel-screening` Configuration 条目进入切片身份（改变条件→重算）；
- 硬筛选≠正式工程判定（§11）；缺工作点数据的维度标数据不足，不默认通过（不把缺少动力学证据当零负载/零功率）。

## 8. 减速器硬筛选

### 8.1 筛选流程图

```
候选减速器条目（目录快照）
   │ ①身份与校验状态检查
   │ ②安装接口/安装方向兼容（MountSpec、mounting_orientation vs 关节安装关系）
   │ ③传动配置兼容（速比 vs 该轴目标传动配置——目录速比成为候选传动参数 c_j 的来源，§9.4）
   │ ④逐能力维度判定（对照关节侧事实，全部维度独立执行）
   │     额定输出转矩：τ_joint_rms ≤ 额定输出转矩
   │     峰值输出转矩：τ_joint_peak ≤ 峰值输出转矩
   │     允许输入转速：ω_m_peak ＝ ω_joint_peak / c_j ≤ 允许输入转速（ω_m 来自映射工作点，不自算则经映射事实）
   │     速比：候选速比 ∈ 该轴允许传动比范围（来自筛选条件/传动配置意图）
   │     效率：目录效率 ≥ 筛选条件最低效率要求
   │     回程间隙：目录回程间隙 ≤ 筛选条件上限（单位经字段字典换算到 SI 比较）
   │     寿命：目录额定寿命 ≥ 筛选条件要求（单位声明换算）
   │     允许外载荷：负载/工具对输出轴的外载荷 ≤ 允许径向/轴向载荷（作用点力臂核算——几何事实来自 modeling 装配链，值传递）
   │ ⑤汇总 → FeasibilityRecord（逐维记录）
   ▼
减速器候选资格（逐轴）
```

### 8.2 筛选纪律

- 允许输入转速/电机侧量的对照使用**映射工作点事实**（§9），selection 不自算 ω_m＝ω_j/c 之外的任何映射量（速比换算属于"候选传动参数构造"，§9.4——构造≠映射实现）；
- 缺失能力曲线的维度按 §6.4 处理（固定额定值口径或数据不足）；
- 安装方向不兼容、外载荷超限等产生独立淘汰原因（§10.3 词表）。

---

## 9. drivetrain/dynamics 工作点和组合校核

### 9.1 数据流（唯一映射口径）

```
dynamics 关节侧结果（峰值/RMS/逐样本序列/工况分组——DYN-03 口径）
        ↓ UpstreamResult（dyn.joint-series——drivetrain 卡 §12.1 提议契约）
drivetrain::DriveTrainMappingEvaluator（键 dt.mapping——唯一映射实现）
        ↓ 电机侧位置/速度/力矩/功率/效率/反射惯量/惯量比/能量分项/四象限（逐组合）
selection 组合校核评估器（sel.combination-check）
        ↓ 可行集/逐项淘汰原因/组合兼容记录（Facts）
evidence（Profile/包络/当前性/正式判定）
```

**selection 不得重新计算**：传动比映射、耦合矩阵、虚功映射、电机侧力矩/速度/功率、反射惯量、交叉耦合。**selection 只能**：提供候选电机/减速器配置（候选传动参数构造）、经③端口消费映射、按目录能力筛选、记录候选与工作点关系。

### 9.2 两段评估管线（R1 组合校核的执行形态）

| 段 | 评估键 | 切片条目 | 输出 |
| --- | --- | --- | --- |
| ①批映射 | `dt.mapping`（drivetrain 所有，selection 仅提交） | Object：`model.drivetrain`（轴结构/零位偏置锚点，Required）＋ UpstreamResult：`dyn.joint-series`（Required）＋ **Configuration：`config.dt-combo-set`（本批候选组合集——逐组合归一化传动输入）** | 逐组合电机侧工作点/序列/Facts（drivetrain 卡 §11/§12） |
| ②组合校核 | `sel.combination-check`（本卡注册） | Object：目录锁定版本对象（`catalog.lock`）＋ UpstreamResult：`dyn.joint-series`（关节侧事实）＋ UpstreamResult：`dt.mapping` 组合批结果 ＋ Configuration：`config.sel-screening`（筛选条件/偏好） | 可行集/淘汰原因/组合兼容记录/工况资格矩阵（payload 契约 §9.6） |

**候选组合集载荷（`config.dt-combo-set`）与 drivetrain 卡的对齐**：drivetrain 卡 §13.10 已为 `config.dt-mapping`（组合场景值传递）预留条目；本卡将该条目细化为"候选组合集"——逐组合携带归一化传动输入（每轴 c_j〔来自候选减速器速比、按 drivetrain 卡 §5.3 c 口径换算〕、η⁺/η⁻〔候选减速器效率〕、J_rotor,j〔候选电机转子惯量〕、壳体合成参考负载惯量值）；映射核心对同一关节序列按 K 个归一化模型分别求值（纯函数循环，确定性保持）。此细化不改变 drivetrain 卡内核契约（单模型 evaluate），登记 P-SEL-1（跨卡对齐注记，drivetrain 卡不因此修改）。

### 9.3 组合校核清单（SEL-05 全覆盖）

| 校核维度 | 输入事实 | 判定 |
| --- | --- | --- |
| 电机—减速器兼容 | compatibility.csv 记录 | 无记录即不兼容（§5.2 零行语义）→ `SEL-COMBO-INCOMPATIBLE` |
| 电机—减速器—关节轴映射 | 轴对象 ID×组合 | 每轴恰一组合；漏轴 → `SEL-COMBO-AXIS-MAPPING-INCOMPLETE` |
| 负载惯量/反射惯量/惯量比 | 映射事实（惯量比数值）＋阈值（O-11） | 数值核对；**阈值未裁决时该维度"未判定"**（§11.3） |
| 转子惯量/壳体惯量 | 目录字段＋回填合成规则（§12.4） | **不重复计入**（drivetrain 卡 §9.4 衔接；组合校核以映射事实为准） |
| 动力学峰值/RMS（关节侧） | dynamics 上游 | 减速器输出转矩筛选（§8） |
| 电机侧峰值/RMS/功率工作点 | 映射事实 | 电机筛选（§7） |
| 效率 | 映射事实（含来源标记） | 筛选条件最低效率 |
| 工作制 | 目录词表 vs 筛选条件 | 匹配判定 |
| 多负载工况/急停/保持工况 | dynamics 工况分组（含设计工况 DYN-07） | 逐工况资格（§9.4 资格矩阵）；急停峰值/保持转矩对照制动/保持能力 |
| 目录版本一致性 | 切片条目 | 组合校核所用目录版本＝映射批所用候选参数来源版本（不一致→`SEL-IDENTITY-MISMATCH`，拒绝） |
| drivetrain 映射版本一致性 | 上游 Facts 身份块 | 契约版本/算法版本与切片声明一致；不一致→`SEL-IDENTITY-MISMATCH` |
| 映射结果完整性/诊断 | Facts 完整性状态 | Partial/DataInsufficient → 对应维度数据不足（不伪造通过）；映射失败≠器件能力不足（§10.3） |

### 9.4 多工况候选资格矩阵（EVI-02 承载）

```
              工况1(额定负载)   工况2(空载)   工况3(急停)   工况4(保持)   …必验工况
 组合A          Pass            Pass         Fail(峰值)    Pass          → 淘汰(原因: 工况3峰值转矩)
 组合B          Pass            Pass         Pass          Pass          → 该维度可行
 组合C          DataInsuff      Pass         Pass          Pass          → 数据不足(缺工况1证据)
```

- 组合**仅在全部启用必验工况通过**后方可进入可行集（EVI-02）；任一工况数据不足 → 该组合整体 DataInsufficient 素材（缺失项全量列出）；**不得漏验**；
- 多工况**包络合并仅为呈现方式**（跨工况"最坏值"必须携带来源工况/时刻/段）；
- 每格判定绑定工作点时间/轨迹段/工况 ID（淘汰原因定位，§10.3）。

### 9.5 组合身份与规模纪律

- `DeviceCombinationId`＝轴序×(motorModelId, gearboxModelId, catalogVersion) 的规范序列化摘要（候选身份确定可复现）；
- 候选组合集按轴分批（批预算随 `config.dt-combo-set` 声明；NFR-PERF-03 分批/流式）；**重复组合去重**（同组合键只算一次）；
- 缓存粒度＝组合集切片（改任一候选/筛选条件→新 sliceId 重算；per-combo 缓存为 R2 优化项，登记 §19.4 R-SEL-6）。

### 9.6 组合校核 payload 契约（域 payload，canonical）

`SelectionCheckPayload`：候选资格矩阵＋逐组合 FeasibilityRecord＋淘汰原因记录＋组合兼容记录＋目录/映射身份块＋覆盖矩阵素材＋数据完整性状态。该 payload 是 reporting 选型章节与 optimization（R2）的消费面（§13）。

---

## 10. 可行集、稳定排序和逐项淘汰原因

### 10.1 类型设计

```cpp
struct DeviceCandidate   { CatalogIdentity catalog; core::StableId modelId; };   // 候选（电机或减速器）
struct MotorCandidate    { DeviceCandidate motor; };                             // 语义别名（保留扩展位）
struct GearboxCandidate  { DeviceCandidate gearbox; };
struct AxisDeviceAssignment { core::ObjectId jointId; MotorCandidate motor; GearboxCandidate gearbox; };
struct DeviceCombination { DeviceCombinationId id; std::vector<AxisDeviceAssignment> axes; };
struct RejectionReason {                       // 逐项淘汰原因（SEL-06；ERR-01 比较型字段齐备）
    ReasonToken token;                         // 封闭词表（§10.3）
    core::ObjectId candidateId;                // 候选对象定位（型号）
    core::ObjectId axisId;                     // 轴对象 ID
    core::CaseId caseId;                       // 工况
    double atTime;                             // 工作点时间 s（可适用时）
    std::string segmentId;                     // 轨迹段（可适用时）
    double actual;                             // 实际值（SI 单位）
    double required;                           // 要求值
    std::string unit;                          // 单位 token
    std::string thresholdSource;               // 阈值来源（目录字段/筛选条件条目/策略条目引用）
    CatalogIdentity catalog;                   // 目录版本
    core::ContentIdentity inputSliceId;        // 输入切片身份
    core::ContentIdentity mappingId;           // 映射身份（可适用时）
    std::string suggestion;                    // 建议动作（ERR-01）
    std::optional<core::DiagCode> diagRef;     // 稳定诊断引用（SEL-*，注册后回填）
};
struct FeasibilityRecord { DeviceCombinationId id; VerdictKind verdict; std::vector<RejectionReason> reasons; std::vector<DataGap> gaps; };
struct FeasibleSet { std::vector<DeviceCombination> feasible; std::vector<FeasibilityRecord> records; };
struct SelectionRunResult {                     // 运行结果（payload 主体）
    FeasibleSet set;
    std::vector<CaseCoverageEntry> coverage;    // 工况覆盖矩阵素材（EVI-02）
    IdentityBlock identity;                     // 目录/映射/切片/契约版本/模式（§4.3）
    Completeness completeness;                  // Complete/Partial/Estimated
};
```

### 10.2 分层与短路边界

- **分层**：某一轴失败（轴级原因）→ 组合失败（组合级汇总）→ 整机方案（evidence 汇总规则判定）——轴/组合/整机三层不混淆；
- **短路边界**：目录 schema、身份、维度、非有限值等**致命输入错误**在校验边界快速拒绝（整批拒绝＋逐项定位）；**候选能力筛选不短路**（全部独立维度执行完毕才汇总）；不同失败原因**稳定排序**（§10.4）；不留下笼统"不可行"；
- **空集语义**（不得混为一谈，逐类独立标记）：

| 空集/失败形态 | 语义 | 处理 |
| --- | --- | --- |
| 目录没有该型号 | 数据边界（非淘汰） | 诊断＋数据不足素材 |
| 目录缺字段/曲线无法插值 | 数据不足 | 缺失清单全量列出（EVI §8.1 表 2 优先级④） |
| drivetrain 映射失败 | 上游失败 | 上游诊断透传；不伪装成候选淘汰；不自动判整机不可行 |
| dynamics 结果缺失 | 上游缺失 | 派发前失败（UpstreamResult 未满足）或数据不足 |
| 工况未执行 | 覆盖缺口 | 覆盖矩阵缺口＋DataInsufficient |
| 候选能力不足 | 淘汰（逐项原因） | 进入 FeasibilityRecord |
| 组合不兼容 | 淘汰（组合级原因） | 同上 |
| 用户过滤后为空 | 偏好过滤结果 | **与硬约束淘汰分开**（§10.4 reasonToken=用户优选过滤；非工程结论） |
| 所有候选被完整硬约束淘汰 | 全淘汰（逐项原因齐备） | 可行集为空＋原因全集——**不自动等于任务级确定性不可行**（工程判定权在 evidence §8.1 表 2；"所有候选被完整硬约束淘汰"不构成解析界限/约束矛盾/必经状态碰撞三类证明之一，除非覆盖全部允许选择且证据门禁通过——保守：输出全淘汰事实，不可行结论由 evidence 汇总） |
| 任务级工程不可行 | 正式判定 | 仅 evidence 汇总输出（本卡不自判） |

### 10.3 淘汰原因词表（ReasonToken 封闭词表；SEL-* 稳定码建议值随 WP-19-T06 注册）

| 组 | token（示例全表按组登记） |
| --- | --- |
| 电机能力 | torque-continuous-insufficient / torque-peak-insufficient / speed-insufficient / power-insufficient / overload-time-insufficient / duty-mismatch / voltage-mismatch / thermal-derating-insufficient / brake-insufficient / holding-insufficient / safety-factor-insufficient |
| 减速器能力 | gearbox-rated-torque-insufficient / gearbox-peak-torque-insufficient / input-speed-exceeded / ratio-mismatch / efficiency-insufficient / backlash-exceeded / life-insufficient / mounting-incompatible / external-load-exceeded |
| 组合/一致性 | combo-incompatible / axis-mapping-incomplete / inertia-ratio-policy-unsettled（O-11 未裁决——非淘汰，显式未判定）/ identity-mismatch / catalog-version-incompatible / mapping-version-incompatible |
| 上游/数据 | dynamics-missing / drivetrain-missing / case-coverage-gap / input-invalid / compute-failed（计算失败——**与候选淘汰分开**，不进入淘汰原因统计） |
| 边界/偏好 | axis-out-of-scope（R1 移动关节范围外）/ r2-capability-disabled / user-preference-filtered（用户优选过滤——与硬能力失败分离） |

每条原因包含 §10.1 RejectionReason 全部字段（候选/轴/工况/时间/段/实际值/要求值/单位/阈值来源/目录版本/切片身份/映射身份/建议动作/稳定诊断引用）；**原因不被单一状态字段覆盖**（多原因并存）；reporting 只展示原因、不重新生成（§13.7）；不使用未经 diagnostics 注册的数字错误码；不用"系统错误"隐藏实际候选原因。

### 10.4 稳定排序（NFR-COR-02）

- 排序键（字典序级联）：①可行/不可行 → ②轴序 → ③候选稳定 ID（modelId 字节序）→ ④目录版本 → ⑤组合键 → ⑥质量/成本/裕量等指标（用户可选排序键；指标相等时回退前序键）→ ⑦同值稳定次序（输入序）；
- **不依赖哈希表遍历顺序；不因线程数变化产生不同排序**（分批并行只影响完成顺序，汇总前按排序键归并）；
- 淘汰原因稳定排序：reasonToken 词表序 → 轴序 → 工况 ID → 时刻。

---

## 11. Quick/Verified、evidence 和当前性边界

### 11.1 与 evidence 的关系图

```
 selection(评估器 sel.combination-check)                evidence
   │ EvaluationOutput{EvidenceItem*, payload, diagnostics}   │
   │ ──── 选型事实（候选/可行/淘汰/覆盖/身份） ────────────► │ RequiredEvidenceProfile（表 4 选型行）
   │                                                        │ EvidenceItem/证据等级
   │ ◄─────────────────────────────────────────────────────  │ ResultEnvelope::make（SA-13 构造校验）
   │              （不拥有、不判定、不改写）                   │ 当前性（CON-05 切片身份比对）
   │                                                        │ 正式工程判定（§8.1 表 2 五级汇总）
```

### 11.2 模式承接（不自创第三种等级）

| 模式 | selection 行为 | 纪律 |
| --- | --- | --- |
| Preview | 不产生正式证据与结果对象（§8.1 表 1）；草稿期部分预览（候选表预览）——本卡 R1 评估器 supportedModes 先不含 Preview（保守），预览由插件消费已归档 Quick 结果呈现 | 不伪造正式性 |
| Quick | 快速候选筛选（允许 caseSubset 部分工况——evidence 分批机制）；同输入快照机制 | **Quick 淘汰不能直接作为正式淘汰结论**；Quick 结果不进入正式可行集（除非 evidence 契约明确允许——当前不允许） |
| Verified | 完整输入＋完整必验工况＋规定证据；结果绑定目录/动力学/drivetrain/工况/策略身份 | 唯一可支撑正式结论的模式（表 4 选型必需项齐备：目录版本锁定标识/每组合电机侧工作点/逐项淘汰原因/组合兼容记录） |

- 评估模式进入结果身份（EvaluationRequest.mode；envelope 携带）；
- **"候选可行"≠"整机方案正式通过"**（可行集是器件域事实；整机判定经 evidence 汇总）；
- 不将缺少动力学证据默认当作零负载/零功率；不把目录数据不足隐藏为候选不可行；
- 历史结果保留：Superseded 不改变 payload（CON-02）；迟到结果经五元组＋切片身份归档（§13.4）。

### 11.3 O-11/P-POL-3：惯量比阈值的边界处理（待裁决期间的保守行为）

| 事项 | 本卡设计 |
| --- | --- |
| 归属建议（WP-19-T01 落位意见） | **推荐归属 `EngineeringPolicySet`**（阈值随策略内容身份进入依赖切片与缓存键——CON-06/AT-19 跨入口一致精神；policy 卡已预留"策略阈值条目扩展承载"schema 通道，裁决归策略时以 schema 次版本追加字段）；selection 域配置仅承载会话级排序参考（不入身份、非工程约束） |
| 不自建 | 不在 selection 内创建第二套 EngineeringPolicySet；不写死默认阈值；不把显示过滤阈值冒充工程硬约束 |
| 未裁决前行为 | 惯量比**筛选维度输出"未判定"**（`inertia-ratio-policy-unsettled`，显式标记不伪造）——不作为淘汰原因、不作为可行依据；该维度不是 §8.1 表 4 选型域必需项（必需项为目录版本标识/每组合工作点/淘汰原因/组合兼容记录），故 **Verified 结论不因 O-11 未裁决整体阻断**；若裁决后归 policy 且为必验维度，则按表 2 汇总规则（策略未设置→按 policy §4.5 四态）执行 |
| Quick 研究态 | 允许：用户可在界面以临时参考值排序候选（呈现层，明确标注"非工程约束、不入结果身份"） |
| 裁决后 | 阈值经 Policy 条目进入 `sel.combination-check` 切片（descriptor 增加 Policy 依赖声明）；策略变化→切片失效→重算（evidence §5.3"工程策略"行） |

---

## 12. 器件回填、project 命令、事务和复算

### 12.1 回填时序图（SEL-10／AT-30／ARCH S4 走查）

```
用户选择候选组合（插件）
   ↓ 校验候选身份、目录版本、当前基线（expectedRevision；URDF 基线只读——编辑在方案分支 PM-12）
   ↓ 组装 DeviceBackfillCommand（各轴 DriveTrainDesign 回填数据＋目录版本＋安装关系＋合成物性）
   ↓ 提交 ProjectCommandService（①端口；ICommandHandler=selection DeviceBackfillCommandHandler）
        ├─ S1 形式校验（类型/payload 版本/分支/writable）
        ├─ S2 基线解析：expectedRevision≠tip → Rejected(stale-revision)（PRJ-STALE-REVISION-REJECTED）——草稿保留
        ├─ S3 处理器 prepare：回填数据合法性（候选/目录版本存在、ratio 有限>0、合成物性断言①~③同语义）
        ├─ S4 确认决策映射（如产生可确认诊断——当前回填无策略校验类；预留 SA-15 通道）
        ├─ S5 双编译编排（WorkCell/DWC 任一失败→Failed(compile-failed)、无修订无暂存）
        └─ S6 CommitPlan：原子产生恰一新修订（多轴整体成功或整体失败；失败零修订、旧状态不变）
   ↓ 记录目录版本、型号、安装关系、来源（ValueProvenance.sourceVersion 引用锁定版本对象）
   ↓ 计算输入切片失效（⑤事件 DependencyInvalidated）
   ↓ 提示动力学/传动/选型/优化结果需要复算（AT-30；复核完成前不沿用原通过结论——ui 状态呈现）
   ↓ 旧结果保留为历史结果（Superseded 不改 payload）
```

### 12.2 回填纪律（逐条）

| # | 纪律 | 依据 |
| --- | --- | --- |
| 1 | selection 不直接修改 `RobotDesign`/不直接写项目文件/不直接修改 modeling 内存对象——回填全部经 project 命令服务 | SEL-10、ARC-01 |
| 2 | 回填使用只读基线（query 端口）；基线过期 → `StaleRevisionRejected`（草稿保留、可基于当前版本重新编辑后再提交） | PM-04 RV-10、project §8.3 |
| 3 | 多轴回填**整体原子**：一个命令＝恰一新修订；任一轴失败→整体失败零修订（S6 事务语义） | SEL-10、ARC-01 |
| 4 | 回填记录目录版本与安装关系（compatibility mount_kind；ValueProvenance 标记 CatalogBackfill） | SEL-10 |
| 5 | **区分电机壳体质量与转子等效惯量**：壳体质量/惯量进合成物性（§12.4）；转子等效惯量写**独立字段**（不进 DWC 合成惯量——drivetrain 映射显式计入的唯一位置，防重复计入） | SEL-10、MDL-16、drivetrain §9.4 |
| 6 | 按明确参考系合成（§12.4）；记录参考系 | SEL-10、MDL-05 |
| 7 | 触发相关输入切片失效；旧结果不能被覆盖；新修订不复用旧正式通过结论 | CON-02/05、AT-30 |
| 8 | 撤销/重做遵循 project 修订语义（反向/正向命令→新修订，不改写历史） | PM-18 |
| 9 | 回填不改变用户显示单位；不由 UI 线程直接执行（命令提交后异步执行、成功后才发布新修订事件） | AT-27、NFR-PERF-01 |

### 12.3 命令形态与 token

- `DeviceBackfillCommandHandler : project::ICommandHandler`（L5 装配期注册进 HandlerRegistry；重复 commandType 注册边界拒绝）；
- 命令 token 冻结值 `apply-device-backfill`（**无点形态，终态——P-SEL-3 已销案 2026-10-10**）：project 命令 token 语法裁决采纳无点形态＝服从 project.md §4.4.4 冻结语法 `^[a-z0-9-]{3,64}`（O-35 同案——DTB §4 2026-09-22；P-PR-9 同批销案，project.md §6.5 含点示例已订正）。**双词形边界（RUL-TOK 批登记）**：本 token 是 project 命令面（commandType）词形；回填命令在 ui 命令注册表面的 id 为点分词形 `selection.apply-device-backfill`（ui.md §7.1/§7.2 既冻句法——翻译面承载，两词形各自服从既有冻结）；
- 命令摘要含可确认诊断放行留痕（如有）；撤销命令三元组由命令服务持久化（project S6）。

> **T09 落位注记（§12.3 实现口径，DTB §5.4）**：token 以无点冻结常量 `kBackfillCommandToken = "apply-device-backfill"` 落位（**P-SEL-3 销案后为终态冻结、非占位**——O-35/D-MDL-6 同案裁决采纳无点形态；契约测试逐字符钉扎 §4.4.4 冻结语法 ^[a-z0-9-]{3,64} 保持零变化）；payload 版本 1（不受理历史版本——NFR-DEP-04）。**回填记录对象落位形态**：prepare 产出的 `ObjectWrite` 为 selection 域独立对象（objectTypeToken＝`sel-device-backfill`，每命令恰一对象承载全部轴——多轴整体原子的记录面载体；基线闭包已有该对象则继承 oid 改版，PA-2 旧字节随历史闭包保留）。**不冒用 modeling 的 `robot-drivetrain` 对象 token**：该 token 是修订闭包的解析器路由键（modeling ObjectTypes.hpp 登记纪律），selection 自拼 robot-drivetrain canonical 字节＝第二实现（NFR-MNT-03）＋路由歧义风险；权威 robot-drivetrain 对象写入走 modeling 既有 `apply-drivetrain-design` 命令链（requiresDualCompile 在彼侧），由 L5 装配层把本命令的回填字段转译编排（optimization Applier 两步编排同款先例）——该跨命令编排登记 **P-SEL-9**（§19.1）。命令摘要随修订持久化（AT-30 复算提示的摘要留痕面）；本命令不声明双编译、不产出待确认集（§12.1 S4——当前回填无策略校验类可确认诊断，SA-15 通道预留）、不声明逆命令（§12.2 纪律 8——撤销经 project 修订语义）。**〔RUL-TOK 批增量（2026-10-10）——插件面 ui 命令 id 双词形拆分〕**：自持描述符中回填命令的 ui 命令 id 由无点词形修订为点分 `selection.apply-device-backfill`（书写点＝plugin/SelPanelCommandCatalog.hpp `kSelBackfillUiCommandId`；翻译函数零改动——id＝token 逐字直拷，宿主 §7.2 校验自然过验，asm-plug 登记"裁决后翻译产物自然过校验"兑现）；project 命令 token 权威书写点仍在本计算库冻结常量——双词形对齐关系（ui id 前缀＝pluginId＋"."、尾段＝本 token）由契约测试逐段对账钉住。

### 12.4 物性合成规则（SEL-10/MDL-05/16 承接）

```
合成目标：各轴回填后的合成质量/质心/惯量（写入 robot-drivetrain 回填字段，逐项 SourcedValue 来源标记）
参考系：按明确参考系（回填命令显式携带——建议：连杆坐标系/质心系，随命令留痕）
合成项：连杆原值（权威模型）＋电机壳体（质量/质心/惯量，按安装位置）＋减速器壳体（同）
       ——转子等效惯量【不参与合成】：独立字段（映射侧显式计入）
方法：质量求和；质心按质量加权（平行轴迁移）；惯量张量按平行轴定理迁移后求和
     （MDL-05 平行轴规则同源；质心修改时的惯量迁移确认语义见 MDL-05）
断言：合成结果必须满足 MDL-06 断言①~③（m>0、SPD、三角不等式）同语义检查——
     失败即回填失败零修订（附录 D 第 6/7 项容差：对称性相对 1×10⁻¹²、SPD 特征值严格>0）
缺失处理：电机/减速器壳体物性字段缺失 → 合成不可得 → 回填数据不足（该轴不回填物性合成项，
     显式标记；是否允许部分回填＝完整命令性原则下整体失败，登记 P-SEL-6 保守口径：整体失败）
```

- **不重复计入**：转子惯量独立字段；壳体仅计入一次（电机壳体≠电机转子；减速器壳体同理）；
- 合成公式与 modeling MDL-05 断言设施的**共享归属**登记 P-SEL-6（避免第二实现：建议 modeling 卡增量导出断言函数或提为共享设施；裁决前 selection 自持实现并以黄金用例钉住与 MDL-06 同语义）。

---

## 13. execution、project、evidence、drivetrain、dynamics、reporting 协作

### 13.1 责任矩阵

| 事项 | io | modeling/runtime | dynamics | drivetrain | **selection** | evidence | execution | project | reporting |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 目录文件层读取/安全 | **拥有** | — | — | — | 注册 schema、消费结果 | — | — | 落位端口 | — |
| 目录业务 schema/版本/锁定引用 | — | — | — | — | **拥有** | — | — | 存储锁定 | 展示 |
| 能力曲线语义/插值/外推 | — | — | — | — | **拥有** | — | — | — | — |
| 电机/减速器硬筛选 | — | — | — | — | **拥有** | — | — | — | — |
| 传动映射/虚功/反射惯量公式 | — | — | — | **拥有（唯一）** | 只消费 | — | — | — | — |
| 关节侧动力学事实 | — | — | **拥有** | 消费 | 消费 | — | — | — | — |
| 组合校核/可行集/淘汰原因 | — | — | — | 供给工作点 | **拥有** | 判定汇总 | — | — | 展示 |
| 证据等级/当前性/正式判定 | — | — | — | — | 事实输出 | **拥有** | 接纳 | 归档 | 措辞 |
| 任务/worker/取消/检查点/缓存 | — | — | — | — | 能力声明 | — | **拥有** | — | — |
| 修订/事务/回填命令执行 | — | 对象 schema | — | — | 命令组装与处理器 | — | — | **拥有** | — |
| 报告渲染 | — | — | — | — | 章节事实 | — | — | — | **拥有** |

### 13.2 与 dynamics（上游；卡未产出——最小依赖契约沿用）

selection 消费关节侧位置/速度/加速度/广义力/功率/峰值/RMS/轨迹段/工况/工具/负载/切片身份——**经 `dyn.joint-series` UpstreamResult（drivetrain 卡 §12.1 提议契约的同一消费面）**，不复制 dynamics 计算；dynamics 卡（WP-17-T01）产出时对齐（P-SEL-1 同批登记）。

### 13.3 与 drivetrain（唯一映射）

消费电机侧力矩/速度/功率/效率/反射惯量/惯量比/能量分项/四象限/映射诊断/映射版本与内容身份——经③端口两段管线（§9.2）；selection 不重新实现传动映射；dynamics、drivetrain 和 selection 消费一致的模型/轨迹/工况/映射身份（AT-38 三方矩阵身份一致；selection 只检查映射结果完整性、身份一致性与诊断存在性，**不做第二套矩阵验证**）；工作点超限只产生候选淘汰原因，不能直接写成整机工程不可行；映射失败与器件能力不足必须区分（§10.2）。

### 13.4 与 execution

execution 负责任务提交/身份/worker/批次/取消/超时/崩溃/检查点/缓存/内存节流/资源不足诊断；selection 负责评估器输入/目录快照/筛选逻辑/结果批次/候选和淘汰事实。能力声明：支持取消（批次边界查询）、支持分批（组合集批预算）、**不支持暂停**（R1）；worker 崩溃只致当前任务失败；取消/失败不发布完整可行集；检查点（如未来声明）不构成正式结果；缓存命中按切片身份（CON-04：部分/失败结果不作为正式缓存命中）；迟到结果经五元组＋切片身份核对归档（TASK-03/AT-10）。

### 13.5 与 project

project 负责修订/分支/草稿/命令服务/事务/归档/`StaleRevisionRejected`；selection 只通过①端口提交回填命令、经存储端口访问 `catalog/` 锁定版本（只读＋导入落位协作 io）；项目切换后旧任务结果绝不写入新项目（PM-13/TASK-03）。

### 13.6 目录差异比较（SEL-08）

- `CatalogDiff`（计算库纯函数）：输入两个 CatalogPackageSnapshot → 结构化差异（新增/移除/修改型号、字段级增量、曲线点集增量、兼容关系增量）——输出用于呈现与升级影响提示；
- 目录更新不静默改变历史结果：历史选型结果保持原目录版本切片身份；依赖旧版本的结果按当前性规则标记 Superseded（不删除、不改写）；
- 大目录比较 >1 s → 经 execution 后台任务（NFR-PERF-01）；差异结果为会话工具输出，不作为正式证据归档（登记 D-SEL-x）。

### 13.7 与 reporting

reporting 消费目录版本/电机和减速器型号/可行组合/裕量/质量/成本/来源/逐项淘汰原因/版本锁定/数据不足/估算值/外部验证边界/回填修订/复算提示（reporting.md 选型/BOM/淘汰依据章节，C 级）；selection 不渲染 HTML/JSON/CSV 报告；报告与 selection 结果逐字段一致（同一结果对象）；限定语保留（RPT-05）。

---

## 14. 公共接口、线程安全和生命周期

### 14.0 通用约定（适用于 14.1～14.10）

零 Qt（计算库）；只 include core/evidence 公共头；不暴露 QWidget；不直接持有 project 存储对象；不直接修改 RobotDesign；显示单位不入输入身份；用户优选过滤不伪装成硬能力（分轨 token）；目录外推不静默通过；缺失动力学结果不当零值；空可行集不发布为正式通过。调用方错误（契约违约）＝fail-fast 异常；数据/环境类（字段缺失、插值失败、上游缺口）＝返回诊断＋降级素材；确定性 NFR-COR-02；单位 SI 域比较（附录 D）；所有权：输入由调用方持有，输出按值返回。

### 14.1 `ICatalogProvider`（目录快照供给）

```cpp
/// @brief 目录快照只读供给：按锁定版本对象解析目录业务模型。
/// @param[in] lock 锁定版本引用（lockObjectId＋CatalogIdentity）
/// @return 不可变目录快照（四表业务模型；调用方持有返回值）
/// @throws std::invalid_argument 锁定对象不存在/内容摘要不符（引用完整性破坏——fail-fast）
/// @note 线程：ConcurrentReadOnly（快照不可变共享）；R1；无副作用；缓存身份＝对象 ContentVersion
class ICatalogProvider {
public:
    virtual ~ICatalogProvider() = default;
    virtual CatalogPackageSnapshot load(const CatalogVersion& lock) const = 0;
    virtual std::vector<CatalogVersion> listLocked() const = 0;   // 项目内已锁定版本清单
};
```

### 14.2 `ICatalogValidator`（业务校验）

```cpp
/// @brief 目录包业务校验（§5.3 全表）：输入 io 解析后的字段与来源身份（不接触文件系统）。
/// @param[in] parsed io 文件层校验通过的解析结果（列→原始值＋行定位）
/// @param[in] dict  字段字典（manifest 携带；注册面 C-6）
/// @return CatalogValidationReport（逐项：文件/行/列/码/实际值/期望/单位；空报告＝通过）
/// @note 纯函数；确定性；致命结构错误以异常 fail-fast（schema 级），行级错误入报告
class ICatalogValidator {
public:
    virtual ~ICatalogValidator() = default;
    virtual CatalogValidationReport validate(const ParsedCatalogInput& parsed,
                                             const FieldDictionary& dict) const = 0;
};
```

### 14.3 `IPerformanceCurveEvaluator`（插值）

```cpp
/// @brief 分段线性插值（§6.2）：闭区间内插值；默认禁止外推。
/// @param[in] curve   曲线（构造入口已校验/排序）
/// @param[in] x       查询横坐标（SI 单位）
/// @return 插值结果（值＋区间＋曲线版本＋单位）或拒绝（SEL-CURVE-EXTRAPOLATION-DENIED——
///         区间外/空曲线/单点曲线按数据不足标记，不抛异常）
/// @note 纯函数；ConcurrentReadOnly；比较经 core 统一规则（附录 D）；R1
class IPerformanceCurveEvaluator {
public:
    virtual ~IPerformanceCurveEvaluator() = default;
    virtual CurveQueryResult evaluate(const PerformanceCurve& curve, double x) const = 0;
};
```

### 14.4 `IHardConstraintSelector`（电机/减速器硬筛选）

```cpp
/// @brief 硬筛选编排（§7/§8）：逐维度独立判定、全量原因保留、稳定排序。
/// @param[in] snapshot   目录快照（调用方持有）
/// @param[in] axisFacts  关节侧事实（dynamics 上游：峰值/RMS/工况分组——只读视图）
/// @param[in] criteria   筛选条件（安全系数/电压/环境温度/工作制需求/速比范围等——进入切片身份）
/// @param[in] ctx        取消查询（可空；维度批次边界查询）
/// @return 逐候选 FeasibilityRecord（含全部独立原因与数据缺口）
/// @throws std::invalid_argument 致命输入错误（身份/维度/非有限——校验边界快速拒绝）
/// @note 纯计算；不调用映射（电机侧维度由组合校核段执行——§9 两段管线）；R1
class IHardConstraintSelector {
public:
    virtual ~IHardConstraintSelector() = default;
    virtual std::vector<FeasibilityRecord> screenMotors(const CatalogPackageSnapshot& snapshot,
                                                        const JointSideFactsView& axisFacts,
                                                        const ScreeningCriteria& criteria,
                                                        ICancellation* ctx) const = 0;
    virtual std::vector<FeasibilityRecord> screenGearboxes(/* 同上形态 */) const = 0;
};
```

### 14.5 `IDeviceCombinationBuilder`（候选组合构造）

```cpp
/// @brief 由逐轴候选构造 DeviceCombination 集合（§9.5）：兼容性过滤（compatibility 表）、
///        去重（组合键）、批预算切分、组合集载荷（config.dt-combo-set）编码。
/// @return 组合集＋编码字节（进入 dt.mapping 切片 Configuration 条目）
/// @note 纯函数；组合键规范序列化（确定性）；R1；R2 扩展直线传动组合（§17.2）
class IDeviceCombinationBuilder {
public:
    virtual ~IDeviceCombinationBuilder() = default;
    virtual CombinationSet build(const std::vector<AxisCandidateList>& perAxis,
                                 const CompatibilityTable& compat,
                                 const BatchBudget& budget) const = 0;
};
```

### 14.6 `IFeasibleSetBuilder` ＋ `IRejectionReasonProvider`（可行集与原因）

```cpp
/// @brief 由资格矩阵+淘汰原因组装可行集（§10）：分层汇总（轴→组合→整机素材）、
///        稳定排序（§10.4）、空集语义分类（§10.2）、覆盖矩阵素材。
/// @note 纯函数；不判工程不可行（evidence 汇总）；R1
class IFeasibleSetBuilder {
public:
    virtual ~IFeasibleSetBuilder() = default;
    virtual SelectionRunResult build(const std::vector<FeasibilityRecord>& records,
                                     const std::vector<CaseCoverageEntry>& coverage,
                                     const IdentityBlock& identity) const = 0;
};
/// @brief 淘汰原因构造（§10.3）：ReasonToken 词表→比较型记录（实际值/要求值/单位/阈值来源）；
///        SEL-* 稳定码引用（注册后回填 diagRef）。词表唯一实现点；R1
class IRejectionReasonProvider {
public:
    virtual ~IRejectionReasonProvider() = default;
    virtual RejectionReason make(ReasonToken token, const ReasonContext& ctx) const = 0;
};
```

### 14.7 `ISelectionResultProvider` ＋ `ISelectionEvidenceFactsProvider`（结果与事实）

```cpp
/// @brief 选型结果只读供给（报告/优化消费面）：按运行身份取 SelectionRunResult 投影。
///        不拥有归档/当前性（读接纳后的已归档结果）；R1
class ISelectionResultProvider {
public:
    virtual ~ISelectionResultProvider() = default;
    virtual std::optional<SelectionRunResult> find(const SelectionRunRef& ref) const = 0;
};
/// @brief 选型事实 DTO 提取（替代旧提示词 ISelectionEvidenceBuilder——"Builder"暗示拥有证据组装权，
///        与 evidence 所有权冲突）：只提供选型事实、候选身份、目录版本、工作点引用、淘汰原因、
///        数据完整性、诊断引用。【不拥有】RequiredEvidenceProfile、证据等级、正式工程判定、
///        结果当前性、ResultEnvelope 最终接纳、项目归档。
class ISelectionEvidenceFactsProvider {
public:
    virtual ~ISelectionEvidenceFactsProvider() = default;
    virtual SelectionEvidenceFacts facts(const SelectionRunResult& result) const = 0;
};
```

### 14.8 `IDeviceBackfillCommandHandler`（回填命令处理器）

```cpp
/// @brief SEL-10 回填命令处理器（project::ICommandHandler 实现；L5 装配注册 HandlerRegistry）。
///        prepare：回填数据合法性＋合成物性断言（§12.4）——判定在处理器（P-PR-3 读法）；
///        命令提交/事务/双编译/修订由 project 命令服务编排（selection 不自建事务）。
///        token 冻结值 apply-device-backfill（无点终态——P-SEL-3 已销案；
///        ui 命令 id 点分词形 selection.apply-device-backfill——双词形）。
/// @throws 无（拒绝经 CommandResult 返回：stale-revision/invalid-payload/…——命令服务语义）
/// @note 线程：命令服务串行槽内执行；多轴整体原子（一个命令一个修订）；R1
class IDeviceBackfillCommandHandler : public project::ICommandHandler {
public:
    // commandType() / prepare(CommandEnvelope, HandlerContext&) → CommandPlan（objectWrite:
    //   各轴 robot-drivetrain 回填字段＋目录版本引用＋安装关系＋合成物性＋ValueProvenance）
};
```

> **T09 落位注记（§14.8 实现口径，DTB §5.4；上块代码注释中"各轴 robot-drivetrain 回填字段"的落位形态见 §12.3 T09 落位注记——selection 域 `sel-device-backfill` 记录对象承载，权威 robot-drivetrain 写入归 modeling 命令链编排〔P-SEL-9〕）**：
> - **依赖纪律**：include `project/CommandService.hpp`（公共头）实现接口，**零 project 实现符号引用**——`HandlerContext` 在 prepare 壳内零调用（对象 oid 走 `ObjectWrite.objectId` 空值语义＝project S6 装配点分配；基线判定全部基于 `baseSnapshot` 值快照的 objectRefs）；`HandlerRegistry` 注册归 L5 装配（ui 侧，零 selection 编译边）。产品库不链接 `sdurws_ird_project`——§3.2 零编译链接边不变（两红线测试白名单同步：include 集合增 project、链接集合不变）。
> - **可测内核**：接口追加 `planFromEnvelope(CommandEnvelope, RevisionView, CommandPlan&, diags&)`（不依赖 HandlerContext 的计划内核——版本受理→解码→域校验→逐轴合成断言→计划组装/中文摘要）；`prepare` 为直通转发壳（三态 switch 逐项对应）。HandlerContext 构造符号在 project 库内，单元测试面不可达——prepare 端到端（真实命令服务 submit→S1~S6→恰一新修订）随 L5 装配集成面承载，单元内以内核三态全覆盖承载（如实登记，不虚称端到端已执行）。
> - **拒绝语义映射**：载荷版本不受理/解码失败/域校验失败/合成输入范围非法 → `RejectedInvalidInput`（invalid-payload）；合成物性断言①~③失败（MDL-06 同语义）→ `RejectedHardAssert`（硬断言就地阻止）；多轴整体原子＝任一轴失败整体失败零计划（§12.2 纪律 3）。逐项定位诊断＝T09 批 SEL-BACKFILL-* 码（DiagCodes 表尾追加 9 码）。

### 14.9 新增接口说明（对提示词清单的补充及理由）

| 接口 | 需求依据 | 依赖影响 | 是否重复现有端口 | R1/R2 |
| --- | --- | --- | --- | --- |
| `ISelectionEvidenceFactsProvider` | EVI-01 事实边界（§12.3 对齐） | 无新边 | 否（③端口输出的 Facts 提取面） | R1 |
| `ICancellation`（注入接口，core/evidence 侧已有形态则复用） | TASK-01 | 无 | 复用既有取消查询形态 | R1 |
| 目录 diff/锁定管理不设评估器形态 | SEL-08 为会话工具 | 无 | 是——避免滥用③端口（正式评估才进注册表） | R1 |

不新增 `ISelectionEvidenceBuilder`（§14.7 弃用说明）；不以接口占位代替功能实现（每个接口对应 §16 实现任务）。

### 14.10 线程与生命周期汇总

| 对象 | 线程约束 | 生命周期 |
| --- | --- | --- |
| 目录快照 | 不可变共享（ConcurrentReadOnly） | 锁定版本存续期 |
| 评估器实例（sel.combination-check） | 单线程（每 worker 每任务一实例）；stateless=true | 任务期 |
| 筛选/曲线/组合构造（纯函数） | 可重入 | 进程静态 |
| 命令处理器 | 命令服务串行槽内执行 | L5 装配注册、进程期 |
| 插件视图 | UI 线程 | 会话期；零计算逻辑 |

---

## 15. 验证方案及故障注入矩阵

### 15.1 总体口径

- 只设计，不宣称已执行；黄金数据集 `testdata/golden/sel-*`（DTB WP-19-T11）：目录包正/误样例、可行/不可行电机与减速器黄金表（每个淘汰项含实际值和阈值——AT-08）、组合校核黄金算例（与 dynamics/drivetrain 黄金联动）、回填事务样例；
- 插值/筛选/组合校核解析算例对照（NFR-COR-01，附录 D 第 9 项容差）；同输入同线程同种子结果稳定（NFR-COR-02）；
- 每项测试标注：需求/架构依据、AT、R1/R2、前置、输入身份、操作、预期、失败分类、稳定诊断、evidence 影响、execution 状态影响、project 影响、观测点、是否需实际执行；**不得把未执行测试写为"通过"**。

### 15.2 故障注入矩阵（按提示词九组收敛为组行；全部为设计）

| 组 | 用例要点 | 预期 | 稳定诊断（建议） | AT/依据 |
| --- | --- | --- | --- | --- |
| V1 目录导入与数据安全（23 项） | 合法导入；清单缺失；引用悬空；版本/来源缺失；重复型号；重复稳定 ID；显示名重复 ID 不同（**合法**）；必填缺失；非法单位；非有限；越界；兼容冲突；曲线无序/重复横坐标/非有限/区间不合法；**默认禁止外推**；公式字符串；路径穿越；超预算；导入取消；失败后当前目录不变；目录更新不改历史 | 逐项定位拒绝或合法通过；取消/失败零残留、当前目录不变 | SEL-CATALOG-* / SEL-CURVE-EXTRAPOLATION-DENIED（io 层码：IO-*） | SEL-01/02、AT-08、NFR-SEC |
| V2 电机硬筛选（25 项） | 连续/峰值转矩、转速、功率、过载时间、工作制、电压、温度降额、制动、保持、安全系数——各"刚好满足/不足"边界；缺失字段；曲线区间外；**每项失败原因完整记录** | 逐维独立判定；原因含实际值/要求值/单位/阈值来源 | SEL-MOTOR-* | SEL-03、AT-08 |
| V3 减速器硬筛选（14 项） | 额定/峰值转矩、输入转速、速比、效率、回隙、寿命、安装方向、外载荷边界；接口不兼容；缺曲线；数据不足；外推拒绝；原因完整 | 同上 | SEL-GEARBOX-* | SEL-04 |
| V4 传动与动力学交接（17 项） | dynamics 完整/缺失/版本不匹配；工作点完整/映射失败/映射版本不匹配；轴序/工况/段/工具负载身份不一致；功率力矩与 drivetrain 逐字段一致；**不自算电机侧量**；MDL-21 矩阵身份一致；R1 耦合阻断；R2 扩展边界；AT-38 统一口径；关节侧/电机侧不混淆 | 身份不一致拒绝；映射失败≠候选淘汰 | SEL-IDENTITY-MISMATCH 等 | DYN-04、MDL-21、AT-38 |
| V5 组合校核与可行集（24 项） | 单轴/多轴组合；兼容；负载惯量；转子不重复计入；多工况；急停；保持；峰值/RMS；功率；效率；惯量比（含 O-11 未冻结→未判定）；空可行集；全淘汰；目录不足空集；工作点缺失数据不足；稳定排序；去重；Quick/Verified 分离；Quick 淘汰不进正式可行集；候选身份稳定；同输入同线程同种子稳定 | 分层空集语义；确定序 | SEL-COMBO-* / inertia-ratio-policy-unsettled | SEL-05/06、EVI-02 |
| V6 淘汰原因（14 项） | 单/多原因；多轴多工况；实际值/要求值/单位/阈值来源/目录版本/工况/时间/段/诊断引用；稳定排序；用户过滤与硬能力分离；数据不足与能力不足分离；映射失败与候选淘汰分离 | 全字段齐备、分轨 | 词表（§10.3） | SEL-06、ERR-01 |
| V7 器件回填（18 项） | 单轴；多轴整体；恰一新修订；目录版本/安装关系写入；壳体/转子区分不重复；旧结果保留；下游失效；复算提示；StaleRevisionRejected；失败回滚；撤销/重做；基线字节不变；只读项目拒绝；锁冲突；过期结果不作正式依据 | 事务原子；修订语义正确 | PRJ-STALE-REVISION-REJECTED 等 | SEL-10、AT-30、PM-12 |
| V8 execution/可靠性/历史（17 项） | 取消；超时；worker 崩溃；检查点写入/恢复；缓存命中/不兼容；目录版本变化；切片变化；项目切换；迟到结果；历史归档；失败不发布完整可行集；取消不进正式可行集；检查点不构成正式结果；worker 崩溃不退主进程；中断显示为中断 | 状态机语义正确 | EX-*（execution 所有） | TASK、NFR-REL、AT-10 |
| V9 R2 直线传动/混合链（15 项，**全部标记 R2**） | 直线传动目录（丝杠/齿条/同步带/直线电机）；位移/速度/加速度/力单位；能力曲线；工作点；可行/不可行样例；移动关节 R1 阻断；SEL-09-S1 前置；MDL-12-S1 前置；六/七轴限制；4/5 轴仍阻断；AT-36 端到端；不影响全旋转链 | R2 用例（阶段 D 执行） | SEL-INPUT-AXIS-OUT-OF-SCOPE 等 | SEL-09-S1、MDL-12-S1、AT-36 |

### 15.3 GUI 测试流程（只设计，不启动）

- 插件测试遵循 AGENTS.md：Visual Studio x64 Developer Environment；`$env:QT_QPA_PLATFORM='windows'`；一次只启动一个 GUI 测试可执行文件；不使用 `offscreen`；
- **selection 核心筛选和组合校核优先使用模型测试、契约测试和黄金数据集**（计算库零 Qt 直调——NFR-MNT-01）；**插件测试不能代替计算核心测试**；
- GUI 范围：目录管理页/候选表/淘汰原因视图/回填入口的链路用例（WP-19-T10），随插件任务执行。

---

## 16. 阶段 C 实现任务拆分（对齐 DTB §2.20 WP-19-T01～T12）

| 任务 | 交付物 | 依赖 | 验收（DoD 口径） | 红线 |
| --- | --- | --- | --- | --- |
| **WP-19-T01**（本卡） | `units/selection.md`（≥v0.1）：目录包 schema、电机/减速器字段、能力曲线、目录版本、导入校验、硬筛选、组合校核、可行集、淘汰原因、回填命令、evidence/execution/project 交接、P-POL-3 阈值归属落位、R2 直线传动承接 | WP-11/WP-18/WP-04/WP-05 各卡 | 文档自审（§20）；SEL-01～10/AT-08/30 追溯 | 硬能力判定不受优选品牌影响（SEL-07） |
| **WP-19-T02** | `ird/selection/CMakeLists.txt`：零 Qt 计算库＋`_plugin`；公共头与私有实现分离；不默认创建 worker（execution 装配）；注册 `_test`/`_contract_test`；README 文案修正 | T01、WP-03-T01 | 双模式构建零错误；二分结构扫描通过 | 不引入业务单元私有依赖 |
| **WP-19-T03** | 目录模型与导入校验：版本化 CSV 目录包（清单/主表/曲线/兼容表）＋字段字典＋来源＋引用/单位/必填/唯一性/范围校验＋分段线性插值禁外推＋目录版本锁定 | T02、WP-11-T03/T04 | 目录导入/错误字段/版本锁定测试通过 | 默认禁止能力曲线外推 |
| **WP-19-T04** | 电机/减速器硬筛选（§7/§8 全维度） | T03 | 可行/不可行型号黄金表通过 | — |
| **WP-19-T05** | 组合校核：兼容＋负载惯量＋每轴工作点（**共享 DriveTrainMappingEvaluator**）＋多工况＋峰值/RMS＋质量成本＋惯量比配置 | T04、WP-18-T03（③端口） | 经共享映射校核用例通过；惯量比阈值为可配置工程规则 | 不自建映射实现（R-5 精神） |
| **WP-19-T06** | 可行集与淘汰原因输出（§10；EVI-01 选型 Profile 注册） | T05 | 每个淘汰项含实际值和阈值 | — |
| **WP-19-T07** | 优选品牌/供应状态/系列限制＋目录差异比较＋项目锁定版本 | T03 | 硬能力判定不变；目录更新不静默改变历史结果 | — |
| **WP-19-T08** | 移动关节范围外诊断（DataInsufficient 语义） | T04 | 含移动关节链阻断测试 | 不静默套用旋转传动 |
| **WP-19-T09** | 回填命令与合成（§12；ProjectCommandService；壳体/转子区分不重复；复算提示；StaleRevisionRejected） | T06、WP-04-T10 | 回填产生新修订＋依赖失效提示；复核前不沿用原通过结论 | — |
| **WP-19-T10** | selection 插件界面（目录选择/版本/筛选条件/候选表/工作点摘要/淘汰原因/可行集/回填入口/进度/取消/诊断） | T03～T09、WP-10-T08 | 界面链路用例通过 | 插件零计算逻辑；UI 线程不执行筛选 |
| **WP-19-T11** | 契约测试与选型黄金数据集（`test/*`＋`contract_test/*`＋`testdata/golden/sel-*`） | T03～T10、WP-02 | 全部用例通过并留痕 | 未执行不得标注通过 |
| **WP-19-T12**（R2/D） | 直线传动目录与映射（丝杠/齿条/同步带/直线电机；可行＋不可行样例含实际值/阈值/原因） | T05、阶段 D 启用 | 目录模板导入校验通过；不改变现有六/七轴全旋转链行为 | 等待阶段 D 与 MDL-12-S1 前置；不在阶段 C 提前启用 |

---

## 17. R2 阶段 D 承接和接口交接清单

### 17.1 交接总表（各对端单元详设/实现的直接输入）

| 方向 | 交接内容 |
| --- | --- |
| **io → selection** | 解析后的目录字段；文件清单；文件引用；单位；来源；资源内容身份；安全校验结果；行级错误；资源版本；取消和失败状态（io §7.8 契约） |
| **modeling/runtime → selection** | 轴对象 ID；关节类型；旋转/移动能力；传动配置；安装关系；基座和工具相关引用；目标链能力；R1/R2 能力声明；MDL-12 范围外诊断口径；MDL-21 矩阵身份（经映射事实间接核对）；runtime 内容身份（值传递） |
| **dynamics → selection** | 关节侧位置/速度/加速度/广义力/功率/峰值/RMS/轨迹段/工况/工具/负载/快照身份/切片身份/数据完整性/诊断引用（经 `dyn.joint-series` UpstreamResult——P-SEL-1 待 dynamics 卡对齐） |
| **drivetrain → selection** | 电机侧位置/速度/加速度/力矩/功率/η⁺/η⁻/反射惯量/惯量比/能量分项/四象限/工作点/映射身份/矩阵身份/版本/诊断（经③端口 UpstreamResult——drivetrain 卡 §11/§12.2） |
| **selection → evidence** | 目录身份；候选身份；组合身份；可行性事实；淘汰原因；工作点引用；工况覆盖；数据完整性；估算标记；诊断；算法和契约版本 |
| **selection → project** | `DeviceBackfillCommand`；目标修订；轴到器件映射；目录版本；安装关系；物性合成来源；复算依赖；命令摘要；过期基线校验 |
| **selection → reporting** | 选型摘要；型号；目录版本；可行组合；裕量；质量；成本；来源；逐项淘汰原因；数据不足；估算值；外部验证边界；回填修订；复算提示 |
| **execution → selection** | TaskId/RunId/AttemptId/InputSliceId/评估模式/取消/超时/worker/批次/检查点/缓存/资源限制/结果归档 |

### 17.2 SEL-09-S1 直线传动（R2 选型层）

- 直线传动能力曲线 schema（横坐标：速度/载荷；纵坐标：力/功率——单位 m/s、N、W）；线性位移/速度/加速度/力单位进字段字典；drivetrain 扩展端口（drivetrain 卡 §16.2：类型化广义量接口——selection 只消费，不自实现直线映射）；
- SEL-09-S1 与 MDL-12-S1 启用顺序：**SEL-09-S1（选型层）先行**，MDL-12-S1（产品链正式计算）前置之；六/七轴限制；4/5 轴阻断；不影响旋转链兼容策略（V12-03）；
- 本卡预留：字段字典扩展位、曲线量纲词表扩展、组合构造的直线轴通道——**不在阶段 C 实现**。

### 17.3 OPT-D 消费（R2）

selection 作为器件匹配内层（外层候选→轨迹/动力学校核→drivetrain 映射→**selection 组合校核**→联合硬约束与指标反馈，OPT-05）；向 OPT-07 提供器件成本/质量/最小驱动裕量指标事实（裕量＝工作点对能力的余量，含来源工况）；规模化评估（10,000 Quick＋100 Verified，NFR-PERF-06）由 execution/optimization 承担——selection 保证评估器纯函数可并发、稳定排序与线程数无关（§10.4）。selection 不实现候选搜索/Pareto/鲁棒性/灵敏度/误淘汰审计/暂停继续/优化检查点/并行调度。

---

## 18. 需求—设计—验证追踪矩阵

| 需求/架构项 | 设计位置 | 验证位置 |
| --- | --- | --- |
| SEL-01 | 目录包 schema、版本、来源和字段字典（§4/§5.2） | V1 目录模板和版本测试 |
| SEL-02 | 文件清单、引用、单位、必填、唯一性、范围、曲线（§5/§6） | V1 导入校验和错误定位测试 |
| SEL-03 | 电机硬筛选（§7） | V2 电机能力黄金测试 |
| SEL-04 | 减速器硬筛选（§8） | V3 减速器能力黄金测试 |
| SEL-05 | drivetrain 工作点、惯量、组合校核（§9） | V4/V5 共享映射一致性测试 |
| SEL-06 | 可行集、裕量、质量、成本、淘汰原因（§10） | V5/V6 淘汰原因完整性测试 |
| SEL-07 | 优选品牌和供应状态（§10.4 分轨） | V6 偏好不改变硬能力测试 |
| SEL-08 | 目录差异和项目锁定版本（§13.6/§4.2） | V1/V8 历史结果隔离测试 |
| SEL-09 | R1 旋转传动和移动关节范围外（§2.2） | V5/V9 移动关节阻断测试（R1 阻断已落位——WP-19-T08 SelAxisOutOfScope/SelCombinationCheck/SelCombinationCheckContract 测试组；V9 R2 直线传动用例随阶段 D） |
| SEL-09-S1 | R2 直线传动目录和映射（§17.2） | V9 AT-36 选型侧测试 |
| SEL-10 | 回填、物性合成、新修订和复算提示（§12） | V7 AT-30 |
| DYN-04 | 电机侧工作点消费、唯一映射器（§9） | V4 dynamics/drivetrain/selection 一致性测试 |
| MDL-12 | R1 链型阻断（§2.2） | 支持矩阵和范围外测试 |
| MDL-12-S1 | R2 混合链启用（§17.2） | AT-36 端到端测试 |
| MDL-16 | 惯量、质量、摩擦和来源（§12.4） | 物性输入和缺失测试 |
| MDL-21 | R2 矩阵身份和映射消费（§9.3） | AT-38 |
| CON-01～06 | 快照、切片、目录身份、缓存和历史（§4/§11） | 输入切片和当前性测试 |
| EVI-01～02 | 选型事实与证据边界（§11） | DataInsufficient/工况覆盖测试 |
| TASK-01～03 | 取消、失败、迟到结果（§13.4） | execution 契约测试 |
| PM-12 | 分支、修订和回填基线（§12） | 回填和过期基线测试 |
| OPT-03 | R1 不把器件联合约束作为 OPT-B 退出条件（§2.2） | 阶段锁测试 |
| OPT-05 | R2 selection 作为联合优化内层（§17.3） | OPT-D 集成测试 |
| OPT-06 | Quick/Verified、缓存和 R2 扩展（§11/§13.4） | 评估模式测试 |
| OPT-07 | 器件成本、质量和驱动裕量指标消费（§17.3） | R2 指标集测试 |
| AT-08 | 目录导入、硬筛选、可行组合和淘汰原因 | 选型黄金数据（sel-*） |
| AT-09 | R1 静态优化不提前消费完整选型约束 | OPT-B 阶段边界测试 |
| AT-10 | 项目切换和迟到结果 | execution/project 集成测试 |
| AT-19 | 不复制碰撞策略 | 依赖与红线扫描 |
| AT-27 | 单位显示不改变 SI 真值和结果身份 | 单位和配置测试 |
| AT-30 | 器件回填和复算 | project 命令测试 |
| AT-34 | 取消、进度和一站式导出消费链 | execution/reporting 集成测试 |
| AT-36 | 直线传动和混合链 R2 | R2 端到端测试 |
| AT-38 | 耦合矩阵统一消费 | drivetrain 交接测试 |
| NFR-COR-01 | 能力曲线、筛选和组合解析/黄金算例 | 黄金数据集 |
| NFR-COR-02 | 稳定候选集合和排序 | 确定性测试 |
| NFR-COR-03 | 非有限数、非法单位和引用缺失 | 输入拒绝测试 |
| NFR-COR-04 | 结果可定位到目录、工作点和输入快照 | evidence 追溯测试 |
| NFR-PERF-01～03 | UI 线程不阻塞、分页和后台任务 | UI/performance 测试 |
| NFR-PERF-04～06 | R2 大规模候选评估 | execution/optimization 性能测试 |
| NFR-REL-02/03 | worker 崩溃和任务中断 | 故障注入测试 |

---

## 19. 设计决策、风险、待裁决项与变更记录

### 19.1 已采用决策（D-SEL-x）

| # | 决策 | 依据/理由 |
| --- | --- | --- |
| D-SEL-1 | selection 是 L4 业务域；计算库零 Qt＋Qt Widgets 插件二分；插件零计算逻辑 | ARCH §3.3；DTB WP-19-T02/T10 |
| D-SEL-2 | R1 目录范围＝电机＋减速器；**不预建**独立驱动器目录（电压/制动/保持为电机条目字段） | SEL-01～10 明文；§4.1 注记 |
| D-SEL-3 | 目录双层校验：io 文件层（清单/引用存在性/路径预算）＋selection 业务层（字段字典/单位/必填/唯一性/范围/插值）；selection 不绕过 io 读文件 | io §7.8、SEL-02 |
| D-SEL-4 | 目录身份＝(catalogId, version, 内容摘要)；锁定版本对象进切片；文件路径不作身份 | §4.3、CON-05 |
| D-SEL-5 | 分段线性插值闭区间；默认禁止外推；插值失败≠能力不足；额定值不伪造曲线 | SEL-02、§6 |
| D-SEL-6 | 组合校核两段管线：`dt.mapping` 批映射（组合集经 config 条目）→ `sel.combination-check`；selection 零映射实现 | SEL-05、ARCH §7.10、drivetrain 卡 §12.2/§13.10 |
| D-SEL-7 | 硬筛选逐维独立不短路（校验边界致命错误除外）；淘汰原因全量保留稳定排序 | SEL-06、§10.2/10.4 |
| D-SEL-8 | 优选/供应状态/系列限制与硬能力分轨（user-preference-filtered 独立 token；不入硬能力判定） | SEL-07 |
| D-SEL-9 | 惯量比阈值归属推荐 EngineeringPolicySet（P-POL-3 落位意见）；未裁决前该维度"未判定"显式标记，Verified 不整体阻断（非表 4 必需项） | §11.3、policy 卡 P-POL-3 |
| D-SEL-10 | 空可行集不自动判整机不可行（全淘汰事实输出，判定权在 evidence 汇总） | §10.2、EVI §8.1 表 2 |
| D-SEL-11 | 回填经 ProjectCommandService（ICommandHandler）；多轴整体原子；壳体/转子分字段防重复计入；StaleRevisionRejected | SEL-10、§12 |
| D-SEL-12 | Quick 淘汰不作正式结论；Quick 结果不进正式可行集；不自创第三种证据等级 | §11.2、EVI-01 |
| D-SEL-13 | 不默认创建 selection 专属 worker；批量经 execution 承载 | §2.3 |
| D-SEL-14 | 目录差异比较为会话工具（纯函数＋后台任务），不作为正式证据评估器（不滥用③端口） | §13.6 |
| D-SEL-15 | 移动关节轴"范围外"＝DataInsufficient 语义＋独立诊断；不伪造工作点、不升级整机不可行 | SEL-09 |

### 19.2 待裁决项（P-SEL-x；格式按 DTB §4.2）

| # | 问题 | 影响 | 来源 | 建议裁决者 | 状态 |
| --- | --- | --- | --- | --- | --- |
| P-SEL-1 | `DynamicsJointSeries`/`dt.mapping` 组合集载荷与 drivetrain 卡、dynamics 卡（未产出）的对齐 | 上游依赖键与 payload 解码 | §9.2、drivetrain 卡 §12.1 | dynamics 卡（WP-17-T01）＋drivetrain 卡维护者 | 登记（提议契约） |
| P-SEL-2 | selection 计算库 CMake 链接面：core＋evidence 必链；drivetrain 经③端口**不落编译边**（与 drivetrain 卡 D-DT-11/12 一致）——若 T05 实现需直链映射核心（L4→L2 下行边），先经架构裁决并登记 dependency-graph | 门禁白名单 | §3.2、ARCH §3.5 | 架构所有者 | 登记（推荐端口形态） |
| P-SEL-3 | 回填命令 token 语法（P-PR-9：project 冻结语法 vs 含点示例争议未裁决） | 回填命令注册形态 | §12.3、project P-PR-9 | project/所有者（P-PR-9 同题裁决） | **已裁决销案（2026-10-10，RUL-TOK 所有者授权批）**：所有者裁决命名规范双词形并存——project 命令 token 面＝无点 kebab（O-35 裁决"采用无点形态＝服从现行冻结语法"，DTB §4 2026-09-22；P-PR-9 同批销案，project.md §6.5 含点示例订正），ui 命令 id 面＝点分 `<域前缀>.<kebab>`（ui.md §7.1/§7.2 既冻句法）。据此：回填命令 project token 冻结 `apply-device-backfill`（无点终态——kBackfillCommandToken 冻结常量与逐字符钉扎契约测试零变化）；自持描述符中 ui 命令 id 修订为 `selection.apply-device-backfill`（点分，服从既冻句法的实现对齐、非新决策——宿主校验序零改动，asm-plug 登记"裁决后翻译产物自然过校验"已兑现，selection contract_test 激活断言翻转为三查全过 Ok） |
| P-SEL-4 | O-11/P-POL-3 惯量比阈值归属——本卡推荐 EngineeringPolicySet（D-SEL-9）；裁决后 selection 切片加 Policy 条目声明 | 组合校核惯量比维度判定 | §11.3、policy P-POL-3 | 需求所有者＋policy 侧 | 登记（落位意见已给） |
| P-SEL-5 | O-07：selection 获取 runtime 能力的方式（本卡经中立值对象/值传递，不新增依赖边） | 组合校核轴能力输入 | §3.2、DTB O-07 | 架构所有者 | 登记 |
| P-SEL-6 | 合成物性断言（MDL-06①~③）与平行轴公式表的共享归属（modeling 导出 vs selection 自持同语义实现） | 防第二实现（NFR-MNT-03/04） | §12.4 | modeling 卡维护者＋架构所有者 | 登记（裁决前自持＋黄金钉住） |
| P-SEL-7 | 目录包 manifest 通道（JSON）与 CSV 表的格式版本升级器（NFR-DEP-04 前向升级范围） | 未来 schema 演进 | §5.2 | io 卡＋selection 卡 | 登记（R1 拒绝未知格式） |
| P-SEL-8 | 是否需要 selection 专属 worker（默认不需要；execution worker 不足时才议） | 构建目标面 | §3.5 | 架构所有者 | 登记 |
| P-SEL-9 | 回填记录对象（`sel-device-backfill`）与权威 `robot-drivetrain` 对象写入的跨命令编排对齐——selection 回填命令落记录面（SEL-10 语义齐备：目录版本/安装关系/合成物性/转子独立登记/复算提示），权威 robot-drivetrain 对象字节归 modeling Codec 唯一实现（selection 零 modeling 编译边，自拼字节＝第二实现）；L5 装配层把回填字段转译编排为 modeling `apply-drivetrain-design` 提交（optimization Applier 两步编排同款），逐轴回填记录与 modeling.md §4.7 单值 catalogBackfill schema 的逐轴缺口由该编排形态承载 | 权威唯一（PA-1）/防第二实现（NFR-MNT-03）/修订闭包 token 路由唯一性 | §12.1/§12.3/§14.8、modeling.md §4.7 | L5 装配任务所有者＋modeling 卡维护者 | 编排面已落位（2026-10-10，ASM-WF 批——workflow 侧 `SelBackfillTranslateFlow` 编排核八段序落位〔闭包定位→tryObject 只读取数→解码缝→编排复核→转译缝→①端口 apply-drivetrain-design 提交→CommandResult 零加工折叠〕＋HostAssembly 契约测试钉扎；selection 记录面自足落盘不变；真实 modeling 命令处理器桥归宿主装配批次——ASM-WF 契约测试以替身处理器承载 token 路由语义，不伪造已桥接。状态列同步＝acc/asm-wf/1 建议③，GOV-DOC 批 2026-10-10 承接） |
| P-SEL-10 | 插件真实注册端口消费缺口（WP-19-T10 编排侧按 WP-17-T09 先例 B 方案处置——acc/wp-17-t09/1 验收确认成立的同案，不重复升级）：契约 acceptance"经 IPluginUiRegistrar 白名单挂位"需 selection→ui 编译边，本单元依赖白名单（§3.2 两条登记边）与 ird_gates 机器面均无该边且白名单文件不在任务 allowedFiles 不可增登——装配面以**字段同构自持描述符**承载（与 ui::PluginUiDescriptor 逐一对应、零增删），白名单 token 对账以契约测试自持词表（ui.md §11.1 八 token）钉住；**真实 IPluginUiRegistrar/PluginUiRegistrar::whitelist() 端口消费、UI-T21/T22 树/属性页域供给协议（IUiTreeNodesProvider/IUiPropertyPagesProvider——§3.1 点名）的真实 ui 类型注册**归宿主装配批次按治理程序增登编译边后翻译进行 | ui.md §11.1 装配时序的真实执行面（白名单校验/重复注册拒绝/AssemblyReport） | §3.1/§3.2、ui.md §10.9/§11.1、tasks/foundation/WP-19-T10.json | L5 宿主装配批次所有者＋架构所有者 | **注册端口消费已收口（2026-10-10，ASM-PLUG 宿主装配批次）**：selection→ui 编译边按治理程序增登（ird_gates_whitelist.cmake＋dependency-graph.json 双面留痕）；自持描述符经 assembly 门面 `translatePluginUiDescriptor`/`registerWithHostRegistrar` 翻译为真实 ui::IPluginUiRegistrar 注册（§11.2 模块半区 SelUiModule 落位；"字段同构无 ui 侧编译期校验"警告解除——真实编译边由编译器校验）；宿主校验序实测：回填命令 token（无点 kebab——P-SEL-3 待裁决的"无点建议值占位"）不满足 ui §7.2 点分句法被宿主权威判 InvalidDescriptor（插件零改写零本地判定——裁决后翻译产物自然过校验，登记于 §21 ASM-PLUG 段；**〔RUL-TOK 兑现 2026-10-10〕**P-SEL-3 销案后 ui 命令 id 已修订为点分 `selection.apply-device-backfill`，翻译产物自然过校验兑现——selection contract_test 激活断言翻转为宿主三查全过 Ok，ui_contract_test 六域聚合快照翻转为六 Ok 入列）；**UI-T21/T22 树/属性页域供给协议仍保持登记**（随其装配批次——本批不涉） |

### 19.3 偏差与待同步登记（DTB §5.4 精神；本卡不改他文）

| 项 | 内容 |
| --- | --- |
| 占位 README 指向 | ~~README 文案"见 units/selection.md §9"与本卡章节编号不符（实现任务＝§16）——随 WP-19-T02 落位时修正~~ **已闭环（WP-19-T02）**：README 已改写为落位说明版（指向卡 §3.5/§16，列当前公共头与后续落位路线） |
| P-IO-7 消账 | 目录包文件清单/文件名 schema 已在本卡 §5.2 注册——随 WP-19-T03 落位同步 io 侧登记（本卡不直接修改 io.md）。**T03 落位状态（2026-10-07）**：注册数据已物化为 `catalogPackageFileSchema()`（v1 五文件：名称/角色/必备性，序＝§5.2 表行序——CatalogProvider.hpp）；io.md 侧的文字登记与 L5 装配把该数据接入 io §7.8 清单核对框架的接线动作，随后续装配任务收编（本卡零 io 编译边，不越界接线） |
| drivetrain 卡细化 | `config.dt-mapping` 条目细化为"候选组合集"载荷（§9.2）——登记为跨卡对齐注记，drivetrain 卡内核契约不变 |
| 索引状态 | DETAILED-DESIGN.md §20 与 unit-status.json 的 selection 行刷新归治理任务（与 trajectory/drivetrain 卡同批待登记） |
| T03 落位细化 ①（StableId 承载） | 卡 §4.1 写 `core::StableId`：core 单元未落位该类型（core include 树实测无）。T03 以域内强语义别名 `ModelId`/`CurveId`（std::string 承载，包内唯一）落位（CatalogTypes.hpp）；core 侧 StableId 落位后收编，包内唯一性语义不变 |
| T03 落位细化 ②（字段字典按文件承载） | 卡 §4.1 `CatalogManifest::fieldDictionary` 为单数：目录包有四 CSV 表，T03 承载为 `std::vector<FieldDictionary>`（`FieldDictionary::targetFile` 定位），语义不变（列名→语义/单位/必填性——SEL-01） |
| T03 落位细化 ③（v1 单位词表冻结 SI 本位） | core Units 已注册词表无 rpm/arcmin/h/°C（core/src/Units.cpp 实测：m/mm/rad/deg/kg/s/N/N*m/kg*m^2/W/m/s/rad/s/m/s^2/rad/s^2/V/1）。v1 目录模板（formatVersion "1"）数值列单位冻结 SI 口径（rated_speed→rad/s、backlash→rad、rated_life→循环数 "1"、thermal 两列→无量纲档位）；"目录若以 rpm 提供"的换算支持随 core Units 词表扩展（换算唯一经 core，§5.3——本域不自建换算表、不自扩 core） |
| T03 落位细化 ④（两码分配） | §5.3 行 4 的 SEL-CATALOG-DUPLICATE-MODEL / DUPLICATE-ID 两码分配：同 modelId 多行且行内容全同 → DUPLICATE-MODEL（重复行）；同 modelId 多行且内容不同 → DUPLICATE-ID（主键冲突）。显示名重复但 ID 不同＝合法（表行原文） |
| T03 落位细化 ⑤（码归族，不新增码值） | 卡面未指名码的拒绝面按族归类：数值文本无法解析 → 主表 RANGE-INVALID／曲线表 NONFINITE（NFR-COR-03 同路径，actualText 保留原文）；曲线 owner_kind 词表违约（非 motor/gearbox）→ REF-DANGLING（owner 无法归属）；x/y_quantity 词表违约 → SCHEMA-MISMATCH（词表归字段字典，§6.1）；同条目引用同 (xQuantity,yQuantity) 多曲线（§6.4 歧义）→ SCHEMA-MISMATCH。均不新增码值（码表随 DiagCodes.hpp 表尾追加纪律） |
| T03 落位细化 ⑥（manifest 身份分轨） | catalogId/version 为空 → schema 级 fail-fast 抛出（身份三元组前提破坏，§4.3/§14.2 note）；source 为空 → 行级 FIELD-MISSING（V1"来源缺失"可定位拒绝面）；未知 formatVersion → 抛出＋升级指引（§5.2/PM-06） |
| T03 落位细化 ⑦（curve_ref 分隔符） | 条目→多曲线引用的 `curve_ref` 列值以分号（`;`）分隔多 curve_id（§5.2"curve_ref…"列形态的冻结） |
| T04 落位细化 ①（记录类型与 ID 承载分阶段） | §10.1 写 `core::StableId`/`core::CaseId`/`core::ObjectId candidateId`：T04 承载为——候选定位以 `ModelId`（T03 域内别名）落位（`RejectionReason.candidateModelId`；目录型号无项目对象 ID），`core::ObjectId` 保留给 `axisId`（轴是 modeling 项目对象）；`CaseId` 以 std::string 强语义别名落位（core 实测无 CaseId，dynamics 侧收编后不改语义）。`FeasibilityRecord.id`：T04 逐候选路径＝`"<modelId>|<jointId 规范文本>"`（候选×轴资格键）；组合键构造随 WP-19-T05 落位后组合级记录复用本结构。记录新增 deviceKind/candidateModelId/axisId/catalog/mappingId 字段（候选级身份与追溯面）；`inputSliceId`/`mappingId` 在硬筛选直调路径＝全零 ContentIdentity（诚实标记——无 evidence 切片/映射身份可携带，评估器路径由 T05 回填）；`diagRef` 恒 nullopt（SEL-* 稳定码映射随 WP-19-T06——DiagCodes.hpp 头注登记口径）。`DataGap`（卡面只引用未定义）落位为 dimension/detail/axisId/caseId/diagCode 最小集 |
| T04 落位细化 ②（输入形态与取消通道复用） | §14.4 `JointSideFactsView`/`criteria` 卡面未给签名：T04 落位 `AxisWorkpointFacts`（单轴×单工况；关节侧 joint* 三量＋电机侧 motor* 六量＋peakDuration＋保持需求＋外载荷＋安装要求＋atTime/segmentId——关节侧/电机侧分组注释承载 §9.1 数据流纪律）与 `ScreeningCriteria`（SF/工作制/电压＋容差/环境温度/回隙上限/寿命/最低效率/速比范围）；`ICancellation` 按 §14.9"已有形态则复用"落位为 `const evidence::IEvaluationContext*`（可空；`cancellationRequested()` 在候选条目边界查询——批次边界；观测到取消即停止处理剩余候选、返回已完成记录〔截断语义：调用方以记录数对比 候选数×轴数 感知〕；进度/对象读取方法硬筛选不消费） |
| T04 落位细化 ③（mounting-incompatible 为共用安装 token） | §10.3 词表中 `mounting-incompatible` 位于减速器能力组且无 `gearbox-` 前缀（同组其余 token 均带前缀）——落位读法：该 token 为电机/减速器共用安装兼容 token（电机 §7.1 ②法兰/轴伸两子项、减速器 §8.1 ②法兰/轴伸/安装方向三子项），失败分轨独立记因 |
| T04 落位细化 ④（温度降额 v1 档位公式） | ThermalDerating v1 承载为无量纲档位（T03 ③——°C 未入 core Units 词表）。筛选期折减系数：超出档数 n＝ceil(max(0, T_env−T_ref))（档距＝1 档位单位，半档保守向上取整——折减只会更严）；f＝factorPerRef^n；f＜1 时对连续/峰值转矩复判（折减后能力＝目录值×f，独立 token `thermal-derating-insufficient`，与原始转矩维度分轨并行——多原因并存，§10.3）；f＝1（环境不高于参考档位）无折减无原因；筛选条件启用而目录未声明 thermal→DataGap（不默认不折减） |
| T04 落位细化 ⑤（功率维度双口径） | §7.1"额定功率（或按能力曲线查询）"落位：条目声明 speed→power 能力曲线时以工作点转速查曲线（曲线口径优先——更精细能力模型）；无该类曲线引用时退目录 `rated_power_w` 固定额定值口径（§6.4——来源显式入 thresholdSource，不用额定值伪造曲线）；曲线查询拒绝（区间外/坏曲线）＝数据不足分轨（缺口携 SEL-CURVE-EXTRAPOLATION-DENIED，不判淘汰）；有曲线而转速工作点缺失＝DataGap；转速维（`rated_speed`/`max_speed`）与转矩维按卡面消费固定额定字段（曲线口径卡面仅明文于功率维度——不扩大） |
| T04 落位细化 ⑥（外载荷力臂核算公式） | §8.1 ④"作用点力臂核算"落位：径向允许力 F_allow＝F_rated×L_rated/L_actual（仅当 L_actual＞L_rated＞0 时折减——悬臂力矩守恒线性折减）；L_rated＝0（目录未标注作用点）或 L_actual≤L_rated 不折减；轴向力直接比较；阈值来源字段携带核算公式文本（黄金用例以 1200 N @ 0.08 m／1300 N @ 0.16 m→600 N 解析值钉住） |
| T04 落位细化 ⑦（verdict 三态汇总规则） | FeasibilityRecord.verdict：reasons 非空→Rejected（淘汰优先）；否则 gaps 非空→DataInsufficient（数据不足——不默认通过，§7.2）；否则 Feasible。记录内原因稳定排序＝token 词表序→caseId→atTime（stable_sort 保留同键子项执行序——§10.4）；记录输出序＝候选快照序（modelId 升序）×轴 facts 首现序（确定性，NFR-COR-02） |
| T04 落位细化 ⑧（条件缺失/数据缺失分界与容差语义） | 筛选条件侧缺失（criteria 字段 nullopt/空串）＝**维度不适用**（跳过，无原因无缺口——用户未配置不构成候选数据问题）；目录/工作点侧缺失（条件已启用）＝**DataGap**（不默认通过）。电压匹配＝core `closeWithin`（附录 D C4：|V_rated−V_req|≤tol·|V_req|，tol 来自筛选条件 `voltageRelativeTolerance`，默认 0＝精确）；能力阈值比较带 core C7 绝对容差（`runtimeAbsoluteTolerance` 按量纲查——torque/角速度 1e-9、angle/dimensionless 1e-12；C7 未声明默认的量纲〔功率/电压/时间/力等〕容差 0＝精确）；安全系数须 ≥1 且有限、最低效率 ∈(0,1]、速比范围 0＜min≤max（违约 fail-fast——调用方契约）；过载持续时间为触发式维度（τ_peak＞额定连续转矩时才核查时间窗；overload.torque 字段 v1 不单独判定——峰值能力已由 peak_torque_nm 维覆盖，不私加维度） |
| T04 落位细化 ⑨（AxisWorkpointFacts＝P-SEL-1 提议契约 v1 承载） | dynamics 卡未产出（R-SEL-1）——AxisWorkpointFacts 为 selection 单方提议契约的 v1 承载（关节侧三量对齐 DYN-03 口径词面、电机侧六量对齐 drivetrain 卡 §11 MotorOperatingPoint 消费口径）；dynamics 落位后按其卡收编（字段名/分组语义不变的前提下对齐）；本筛选器纯函数定位不变——电机侧工作点由调用方供给（R1 两段管线中在组合校核段经 dt.mapping 批结果值传递），筛选器不自算 §8.2 允许的 ω_m＝ω_j/c 之外的任何映射量 |
| T05 落位细化 ①（组合级 FeasibilityRecord 身份承载） | §10.1 FeasibilityRecord 组合级复用（T04 ① 预登记的兑现）：`id`＝`makeDeviceCombinationId`（§9.5 规范序列化 SHA-256 hex 64 字符——轴序×(motor, gearbox, catalogVersion)）；`deviceKind` 表尾追加 `Combination`（枚举追加只允许表尾，既有项不重排）；组合级无单候选/单轴——`candidateModelId`＝空串、`axisId`＝全零（ERR-01 诚实标记，逐候选定位在各 RejectionReason.candidateModelId/axisId）；`catalog`＝判定所用快照身份（追溯面）；`inputSliceId`/`mappingId` 评估器路径真实回填（切片身份/映射批上游切片身份——T04 ①"评估器路径由 T05 回填"的兑现），直调路径全零 |
| T05 落位细化 ②（评估键 kebab 词形偏差） | 卡 §9.2 记法 "sel.combination-check" 与 evidence 评估键词形闸门 `isValidEvaluationKey`（[a-z][a-z0-9-]{1,63}——不含点）冲突：落位取 `kCombinationCheckEvaluationKey＝"sel-combination-check"`（③端口所有者词形权威——drivetrain kMappingEvaluationKey 同款先例）；依赖键（catalog.lock/dyn.joint-series/dt.mapping/config.sel-screening/config.dt-combo-set）按 `isValidDependencyKey` 词形（允许点）保留卡面原文 |
| T05 落位细化 ③（P-SEL-1 提议契约 v1 的映射批承载） | drivetrain 卡正式跨域契约未冻结（P-SEL-1/R-SEL-1）——dt.mapping 组合批结果的 selection 消费面落位为域内值类型：`MappingBatchFacts`（身份块＋完整性＋组合指派表＋逐轴事实）＋`MappingCombinationFact`（组合键/目录身份/轴表——管线②自足：组合校核所需轴×候选指派随批结果值传递）＋`MappingAxisFact`（电机侧六量/峰值段时长/峰值定位/惯量比/反射惯量/效率降级标记——对齐 drivetrain 卡 §11 消费口径）。由组装方（L5 装配/测试）从③端口上游评估产出提取填充；drivetrain 卡冻结后按注册清单收编。`AxisFactsBundle`（dyn.joint-series 消费面）只消费关节侧＋需求侧字段——电机侧在组合校核以映射批为权威（组合口径纪律：不同 c ⇒ 不同电机侧值）；`selUpstreamAnchor` 为 selection 域内物化锚单点（前 16 字节派生，与 drivetrain 锚空间按域隔离） |
| T05 落位细化 ④（惯量比维度＝参考语义三态） | O-11/P-POL-3 未裁决期（验收 2 的实现面）：`InertiaRatioRule` 为【可配置参考阈值】通道（nullopt＝未裁决/未配置；**不写死任何默认数字**——P-SEL-4"不写死默认阈值"），任何路径都不产生淘汰原因、不改变 verdict（词表无惯量比超限 token——封闭词表不新增；裁决后正式约束形态随 Policy 条目＋descriptor 增登走增量任务）。三态：未配置→显式"未判定"（`inertiaRatioUnsettled` 标记＋格 note 词表文本，非 §8.1 表 4 必需项故 Verified 不整体阻断）；配置且映射事实缺失→inertia-ratio DataGap（不默认通过）；配置且有值→参考比较（C7 dimensionless 容差），超参考仅格 note 标注。评估器路径 R1 恒未配置态（切片无 Policy 条目声明） |
| T05 落位细化 ⑤（组合集载荷与 c＝1/n 单点换算） | `config.dt-combo-set`（§9.2 本卡细化条目）落位为 `makeCombinationDriveInputs`＋`encodeDtComboSetPayload`（管线①提交面）：逐轴 c＝1/n（候选减速器速比 n:1 → drivetrain 卡 §5.3 c 口径——**唯一书写点**，契约测试核对单点）；η⁺＝η⁻＝目录 v1 单一 efficiency 列；J_rotor 取电机目录值（回填独立字段语义——防重复计入）；负载惯量按轴键控（缺键＝nullopt 显式缺失，不默认零）。组合 catalog ≠ 快照身份→构造拒绝（来源版本错配 fail-fast）。字节编码六面（IRDSLKV1/IRDSLBV1/IRDSAFV1/IRDSMBV1/IRDSCCV1/IRDSDCV1）为域内 canonical 协议（drivetrain Codec 同款风格：小端定宽/严格解码/非有限拒绝编码） |
| T05 落位细化 ⑥（筛选复用与判定粒度） | 轴级能力维度（§7/§8）复用 T04 `HardConstraintSelector`（同一实现不分叉——黄金表已由 T04 钉住）；筛选调用粒度＝(组合×轴×工况)（电机侧事实依赖组合 c，无法按轴预筛选——组合口径纪律见 T05 ③）；格聚合覆盖全部轴记录（任一轴失败即格失败——§9.4），Fail 格定位＝首条原因的工作点（轴/时刻/段）；组合级 reasons 合并后按 §10.4 稳定排序（token 词表序→caseId→atTime）；verdict 汇总＝T04 ⑦ 同规则（原因优先→缺口→可行）；映射批整体失败/未供给＝drivetrain-mapping DataGap（§10.2 上游失败不伪装淘汰）、Partial 缺失清单逐条转缺口、效率降级轴＝efficiency DataGap |
| T05 落位细化 ⑦（评估器路径与取消语义） | 评估器构造注入 `ICatalogProvider`（selection 域内供给——T03 InMemoryCatalogProvider/T07 存储适配；catalog.lock Object 条目经 tryObjectBytes 读取 `CatalogLockPayload` 字节后 load 解析，load 抛出＝引用完整性破坏按卡 §14.1 fail-fast 重抛）；对象字节不可得＝数据类空产出（selection 未登记"输入不可得"稳定码——不产诊断记录即不伪造码值，NFR-MNT-03；码面扩展随 T06）；解码违约＝组装协议破坏 fail-fast；批级身份预检（映射批版本非零＋批切片身份与条目声明逐字节一致）不符→`SEL-IDENTITY-MISMATCH` 诊断＋空 payload（AT-38 拒绝评估）；上游 DT-* 诊断码逐码透传（§10.2 不吞错）；取消＝组合边界查询＋截断语义（selection 未登记取消诊断码——截断由产出数感知，单元卡登记边界）；supportedModes＝{Quick, Verified}（§11.2 保守，同 drivetrain）；Profile 绑定 {sel,1}（正式 RequiredEvidenceProfile 注册随 WP-19-T06 EVI-01 行——"Profile 注册在前、评估器注册在后"时序由 L5 装配执行） |
| T05 落位细化 ⑧（组合质量核算与必验工况集边界） | §16 行"质量成本"的落位：组合质量＝Σ 各轴电机＋减速器质量（kg，目录必填字段恒可得——SEL-06 素材面 `totalMass`）；成本指标 v1 目录无字段→不设维度（无数据源≠数据缺口，不伪造），随目录 schema 演进/R2 OPT-07 消费登记。必验工况集边界：组合校核以工作点事实已覆盖的工况集为矩阵列（覆盖缺口的权威核对归 evidence 汇总 EVI-02 跨批次——`workpoint-missing` 缺口为素材面） |
| T06 落位细化 ①（§10.1 结果类型的落位承载与 metrics 表尾扩展） | `SelectionRunResult` 按卡面基线落位（set/coverage/identity/completeness 四成员）并**表尾追加 `metrics` 字段**（`FeasibleCombinationMetrics` 列表）：SEL-06"裕量/质量/成本"与 §17.3 OPT-07 消费面、§13.7 reporting 交接行明文要求输出，而卡面 §10.1 类型未列字段——扩展为登记的实现补全（追加字段有默认值，既有消费零破坏）。`IdentityBlock` 落位五字段（catalog/mappingSliceId/inputSliceId/contractVersion/mode——§4.3 身份关系表＋§11.2"评估模式进入结果身份"；Quick/Verified 两态，Preview 不产生正式结果对象）。`FeasibleSet.feasible` 保持卡面 `vector<DeviceCombination>`（组合实体构成面——报告/BOM 消费）；实体由 build 第 4 参 entries 供给（见 ②）。`completeness` 复用 T05 `CompletenessKind` 两态（Estimated 随 P-SEL-1 收编——卡 §10.7）；组装器输出恒 Complete（批级完整性由上游 SelectionCheckResult 携带经证据汇总消费——单一事实源，组装器不重复承载） |
| T06 落位细化 ②（build 签名第 4 参与资格判定细则） | 卡 §14.6 三参签名落位扩展第 4 参 `entries`（`FeasibleSetEntry{combination, metrics?}` 列表）：组合实体与 SEL-06 指标素材不在 records/coverage 承载内，须由组装方值传递（组合校核段产出＋computeFeasibleCombinationMetrics 的标准三步编排）。资格判定细则：①对位校验 fail-fast（记录须组合级 deviceKind==Combination——分层不混淆；记录键/组合键各自唯一；记录引用未知组合＝组装前提破坏；contractVersion==0 非法）；②记录缺失（entries 有 records 无——取消截断前缀形态）→合成 DataInsufficient 记录（gap `feasible-set-input`——§13.4"取消不发布完整可行集"，不无声缺席）；③记录判 Feasible 的组装面 EVI-02 复核：无覆盖格→降级 DataInsufficient（gap `coverage-missing`）；任一格非 Pass→降级 DataInsufficient（gap `coverage-inconsistent`，定位到格工况——更严者胜，不伪造原因明细）；Rejected/DataInsufficient 记录原样（组装器不翻案） |
| T06 落位细化 ③（稳定排序 R1 固定键） | §10.4 排序键级联的 R1 落位：①verdict 位次＝Feasible→DataInsufficient→Rejected（数据不足不是淘汰——居中分轨；显式映射不依赖枚举数值）→②轴序（jointId 规范文本字典序级联，长度兜底词法序）→③候选稳定 ID（逐轴 motor/gearbox modelId 字典序级联）→④目录版本（catalogId/version 字典序）→⑤组合键（全序兜底）→⑦输入序（stable_sort 同键保留执行序）。**⑥"用户可选排序键"（质量/成本/裕量）归呈现层**（WP-19-T10 插件通道——§11.3 Quick 研究态"临时参考值排序不入结果身份"），R1 组装器不设排序参数。coverage 原样透传（组合校核序＝矩阵呈现序）；metrics 按 records 排序序对位输出（消费面读取序契约） |
| T06 落位细化 ④（diagRef 输出回填与词表映射唯一实现点） | §10.3"每条原因包含全部字段"的输出面闭环：组装器对输出 records 中 diagRef 为空的原因回填 `reasonTokenDiagCode(token)`（**已带引用的不覆盖**——上游显式登记的引用权威更高）；`IRejectionReasonProvider.make` 同函数回填。`reasonTokenDiagCode` 为词表→稳定码**唯一映射点**（DiagCodes.hpp/.cpp——禁第二处映射）；token 为封闭枚举恒命中，越界防御分支返回空串（不私造码值——NFR-MNT-03）。`ReasonContext` 为 make 的 ctx 落位承载（卡面未给签名）：字段集＝RejectionReason 除 token/diagRef 外全部字段；atTime/actual/required 非有限 fail-fast（NFR-COR-03） |
| T06 落位细化 ⑤（T06 批 28 码构造规则与登记表机械比对） | §10.3"SEL-* 稳定码建议值随 WP-19-T06 注册"的落位：28 新码表尾追加（电机族 11＝SEL-MOTOR-<维度 kebab 大写>；减速器族 8＋共用安装码 SEL-MOUNTING-INCOMPATIBLE〔T04 细化 ③ 共用 token 的同构码值〕；惯量比未判定 SEL-INERTIA-RATIO-POLICY-UNSETTLED〔非淘汰码——呈现/追溯引用〕；上游/数据 5；边界/偏好 2）；**复用码 6 token 不新造同义码**（ComboIncompatible/AxisMappingIncomplete→§9.3 两码；IdentityMismatch/CatalogVersionIncompatible/MappingVersionIncompatible→SEL-IDENTITY-MISMATCH〔卡 §9.3 行 10/11 同码〕；AxisOutOfScope→§2.2 码）。selectionCodeEntries 全表 17→45 行（T06 批批内序＝ReasonToken 词表组序；既有行不重排）；码值句法经 core DiagnosticRecord::make 双面校验（DiagCodesTest 表尾同步＋FeasibleSetContractTest 全遍历＋登记表同源核对——失同步即失败） |
| T06 落位细化 ⑥（裕量口径——指标面不参与判定） | §17.3"裕量＝工作点对能力的余量，含来源工况"的 R1 落位：六驱动维固定集（motor-torque-continuous/peak、motor-speed-peak、motor-power-peak〔固定额定值口径——功率曲线口径随 §6.4 扩展〕、gearbox-rated/peak-torque），工作点与能力值都可得才计算（nullopt 维度跳过——不伪造零工作点；快照缺候选＝该维跳过）；margin＝(capability−|actual|)/capability（无量纲；工作点取幅值——四象限负工作点按量值对能力；**负值如实保留**——超限指标事实）；minMargin＝跨轴×工况×维度最小者（遍历序＝组合轴表序→工况首现序→固定维度序；同值保留先见——确定性）。**指标面不参与判定**：可行与否完全由硬筛选/组合校核承载（T04/T05 黄金表钉住），本函数只产出数值事实（基础目录能力口径——不含 SF 复判/温度降额折减，折减口径随 O-11 同域裁决扩展；R2 OPT-07 消费）；totalMass 镜像 T05 核算（单一来源不重算）；cost 恒 nullopt（T05 细化 ⑧ 同口径） |
| T06 落位细化 ⑦（sel Profile itemId 词表与评估器产出对齐） | EVI-01/表 4 选型行的注册面落位：`makeSelRequiredEvidenceProfile()` 纯函数（profileId/version 与 T05 descriptor 绑定同值；contentIdentity 零值＝域不可申报）；必需 4 项 itemId 词表常量 `kSelProfileItem*`（唯一书写点）＝sel.catalog-version-lock／sel.motor-op-point／sel.itemized-rejection-reasons／sel.combo-compatibility，建议 2 项＝sel.cost-mass-summary／sel.preferred-vendor-availability；全部 substitutableByInfeasibility=false（选型四产物在输入齐备时恒可生成——全淘汰亦有原因记录，无"因不可行而无法生成"的豁免面）；Common 类不登记（通用必需项 evidence 内建隐式附加）。**评估器产出对齐**：CombinationCheckEvaluator 证据项 1→4（总证据项 sel.combination-check 保留＋必需①③④随 payload 登记、同摘要）；**必需② sel.motor-op-point 归 drivetrain dt.mapping 评估器**（该 id 已是其产出先例——同一 sel Profile 的项级分工，本评估器不重复登记＝不伪造上游证据）；完备性核对语义经契约测试预演（缺②→requiredGaps 恰含该项；补齐→清零）。Profile 实例经真实 EvidenceProfileRegistry 注册闭环验证（validate 零 issue→注册→findProfile 可解析→内容身份由注册表计算回填→重复注册 ProfileDuplicate 拒绝）；L5 装配"Profile 注册在前、评估器注册在后"时序数据面就绪 |
| T06 落位细化 ⑧（ResultFacts 归属与 T06 范围边界） | §3.5 布局表 `ResultFacts.hpp`（§14.7 ISelectionResultProvider/ISelectionEvidenceFactsProvider）**不在本任务落位**：契约 acceptance 三条均为可行集/淘汰原因/Profile 面——结果只读供给与证据事实 DTO 的消费时点在 reporting/evidence 汇总接入（§16 无对应行；T10 插件候选表/T11 契约测试消费时落位，避免 NFR-MNT-04 无边界价值占位）；`SelectionRunRef`/`SelectionEvidenceFacts` 类型随其任务定义。本任务交付面＝FeasibleSet.hpp（§14.6 两接口＋§10.1 类型＋指标＋Profile 注册面）＋src/FeasibleSet.cpp＋DiagCodes 表尾追加＋T05 评估器证据项对齐 |
| T07 落位细化 ①（公共头落位形态——Preference.hpp 为第七公共头） | §3.1 组成表 CatalogDiff 组件的公共面落位为 `CatalogDiff.hpp`＋`src/CatalogDiff.cpp`（§13.6 结构化差异＋CatalogDiffer 纯函数）；SEL-07 分轨逻辑落位为独立公共头 `Preference.hpp`＋`src/Preference.cpp`（**不在 §3.1 组成表独立成组件**——偏好过滤是 §10.4 分轨纪律的执行面而非新计算组件；独立成头避免 Screening/FeasibleSet 头扩大职责面：T04 Screening.hpp 头注"本头无品牌/偏好输入面，优选过滤随 WP-19-T07 走独立 token 分轨"的兑现形态）。两公共头均不在 §3.5 布局表八头内（布局表为 T01 设计基线，T05/T06 以卡面点名组件落位新头的同款增量，本注登记为 §3.5 的落位细化——DTB §5.4） |
| T07 落位细化 ②（偏好维度语义——白名单式三维度＋品牌双语义） | SEL-07"企业自定义优选品牌、供应状态和系列限制"的落位语义：三维度统一为**白名单式偏好条件**（列表非空＝维度启用——候选值不在白名单即 user-preference-filtered；列表空＝维度不适用零原因——与 ScreeningCriteria"条件缺失≠数据缺失"同款分界；无第二套开关——配置即语义）。品牌维度双语义：①过滤面（vendor ∉ 优选白名单→偏好原因）②标注面（命中→`preferredVendorHit=true` 排序建议信号——§10.4 排序键⑥"用户可选排序键"的呈现层素材，恒计算含硬不可行候选）。**vendor 商业权威＝目录条目**（卡 §2.1 行 SEL-07"不承接"列"品牌数据的商业权威（企业目录内容本身）"——PreferenceFilter 从快照主表读 vendor，企业标注不携品牌避免双权威）；供应状态与产品系列在 v1 目录 schema 无字段（T03 落位细化 ③ v1 列契约冻结，不因本任务扩大 schema）——经 `CandidateEnterpriseFacts` 企业域值传递（availability/series；空串＝未标注）。**未标注保守不默认通过**：维度启用且候选未标注→偏好原因以哨兵文本"(未标注)"承载 actualText（企业漏标呈现层可见，不静默放行） |
| T07 落位细化 ③（apply 五步语义与错误两分法） | PreferenceFilter::apply 固定五步：①契约校验 fail-fast（调用方错误——卡 §14.0）：records 含 DeviceKind::Combination 组合级记录／记录候选不在快照对应主表（引用完整性）／enterpriseFacts 内 modelId 重复（矛盾标注）／任一白名单含空串条目（空串＝未标注哨兵，混入白名单使未标注候选恒命中——语义歧义边界拒绝）；②候选聚合（跨轴按 (deviceKind, candidateModelId) 去重——品牌/供应状态/系列是候选属性与轴无关；map 有序键只影响查找路径）；③硬可行判定（候选存在任一 verdict==Feasible 记录即硬可行——§10.2 分层：偏好维度仅对硬可行候选判定，其余候选 verdict 已由硬筛选/组合校核定论，偏好不适用零原因）；④偏好维度判定（维度序＝品牌→供应状态→系列，全量执行不短路——§10.2"候选能力筛选不短路"同款纪律）；⑤输出装配（每候选一条 PreferenceFilterOutcome；(deviceKind, modelId) 升序——NFR-COR-02）。**企业标注引用不在快照的 modelId 不是错误**（企业标注库可为多代目录超集——该条目忽略不产 outcome，数据类宽容面）；**apply 对输入 records 零改写**（const 只读——契约测试钉住深等，分轨纪律的执行面） |
| T07 落位细化 ④（偏好原因承载——经接口构造＋比较型文本字段） | 偏好原因构造唯一经 `IRejectionReasonProvider::make(ReasonToken::UserPreferenceFiltered, ctx)` 接口（§14.6 词表唯一实现点——本头不自建原因/不第二处构造；契约测试经注入计数 Provider 钉住接口路径——WP-20-T03"接口路径零覆盖"教训的钉扎面）。token=UserPreferenceFiltered、diagRef=SEL-USER-PREFERENCE-FILTERED（reasonTokenDiagCode 映射——T06 批已注册码）；文本类比较：actualText/requiredText 承载（品牌＝vendor vs 白名单逗号连接；供应状态/系列＝标注或"(未标注)" vs 白名单），数值侧 actual/required/atTime=0、unit 空串（T04 文本比较同款分轨）；thresholdSource 三维度词表常量 `kPrefDim*`＝config.sel-preference/preferred-vendors｜allowed-availability｜allowed-series（唯一书写点）；catalog=快照身份（追溯）；axisId 全零＝候选级原因（无单轴定位——T05 组合级同款约定） |
| T07 落位细化 ⑤（diff 型号字段注册表——比较集与不比较集） | §13.6"字段级增量"的比较集为固定注册表（比较执行序＝fieldChanges 序）：电机 20 组（vendor/displayName/ratedTorque/peakTorque/ratedSpeed/maxSpeed/ratedPower/overload.torque/overload.duration/dutyClass/ratedVoltage/thermal.refTemp/thermal.factorPerRef/brakeTorque/holdingTorque/rotorInertia/mass/mounting.flangeKind/mounting.shaftKind/curves）、减速器 18 组（vendor/displayName/ratedOutputTorque/peakOutputTorque/maxInputSpeed/ratio/efficiency/backlash/ratedLife/mountingOrientation/externalLoad.radial/externalLoad.axial/externalLoad.dist/mass/housingInertia/mounting.flangeKind/mounting.shaftKind/curves）。**不比较**：条目 catalog 身份字段（版本演进必然不同——非内容差异）、missing 清单与 status（导入期派生标记——其变化已由对应字段值变化承载）。复合可缺失字段（overload/thermal/extLoad）按子字段展开：optional 为 nullopt 时子字段视为 (absent)——overload 从有到无产出其全部子字段的值→(absent) 差异行，逐字段可定位。数值文本＝std::to_chars 最短 round-trip（与 canonicalPackageText 同源同规则——同值必同文本，locale 无关）；缺失哨兵 `kDiffAbsent="(absent)"`；curves 引用列表为集合语义——按 curveId 排序后序列化比较（列表序不参与语义）。Added/Removed 条目 fieldChanges 为空（实体按 modelId 可得——不重复承载） |
| T07 落位细化 ⑥（diff 曲线点级增量与兼容全键——输出序契约） | 曲线差异（键＝curveId）：Modified 判定＝头部字段或点集任一不同；点级增量双指针按 x 归并（双方点集 x 严格升序为构造/装配入口保证的前置不变量）——to 独有 x→addedPoints、from 独有 x→removedPoints、**同 x 变 y＝removed 记旧值＋added 记新值**（"移除旧值＋加入新值"差分语义）；仅头部字段（xQuantity/xUnit/yQuantity/yUnit）变化时 kind=Modified 且点增量空、fieldChanges 非空（v1 词表单单位无法经真实单位换算表达单位迁移——契约用例以量纲迁移表达该路径，目录包实际单位口径仍由导入校验把守）；Added/Removed 不重复承载实体。兼容关系差异：**全键三列** (motorId, gearboxId, mountKind) 存在性比较（同型号对可声明多安装方式——键取全列避免"新增安装方式"被误判无差异；关系只有存在/不存在两态无 Modified）。输出序（NFR-COR-02）：models 按 (isMotor 电机先)→modelId→kind 枚举序；curves 按 curveId；compatibility 按 (motorId, gearboxId, mountKind)。manifest 级字段（formatVersion/version/source/files/fieldDictionary）**不进差异条目**——版本演进必然差异否则 diff 恒非空失去变更检测语义；版本信息由 fromIdentity/toIdentity 承载（呈现层对照展示）。fail-fast：快照包内容身份无效（CON-05 无身份不可比较）／主表 modelId 重复（装配产物唯一性前提——直构快照同责） |
| T07 落位细化 ⑦（会话工具边界与锁定管理归属） | D-SEL-14/§14.9 的执行面：CatalogDiffer 与 PreferenceFilter 均**不适配 evidence::IEngineeringEvaluator**（不进③端口评估器注册表——契约测试 static_assert 编译期钉住）；差异结果为会话工具输出不作为正式证据归档（§13.6）；大目录比较 >1 s 的后台任务承载归 execution（NFR-PERF-01——R1 无 execution 落位，异步编排为后续任务面）。**锁定管理归属**：写入即锁定只增（§4.2）的本域执行面仍是 T03 落位的 InMemoryCatalogProvider（T03 CatalogProvider.hpp 头注"真实存储适配随 WP-19-T07 经注入落位"的兑现形态＝本注登记：**注入接口 ICatalogProvider 不变、project 对象库真实存储适配随 L5 装配收编**——selection 零 project 编译边为 §3.2 依赖白名单硬约束，本域可交付的是锁定语义＋用例钉扎〔锁定只增/历史隔离/引用完整性〕，非 project 侧存储实现；T07 交付的锁定面用例＝锁定×比较历史隔离组合〔CatalogUpdateKeepsHistoryIntact〕＋锁定表只增契约〔LockTableAppendOnlyUnderCatalogUpdate〕）；与 T03 既有 CatalogLockTest 的分工：T03 钉锁定语义本身（幂等/不可变/摘要不符拒绝），T07 钉"目录更新（新版本锁定）下历史结果零静默变更"的 SEL-08 组合语义 |
| T07 落位细化 ⑧（SEL-07 建议证据项承载与测试面构成） | 表 4 建议项② sel.preferred-vendor-availability（kSelProfileItemVendorAvailability——T06 预留"WP-19-T07 落位"）的数据面本任务落位：PreferenceFilterOutcome（preferredVendorHit＋passesPreference＋preferenceReasons）即该证据项的素材承载（reporting/UI 消费随 T10/T11 接入——建议项缺失不阻断，EVI-01 表 4 语义）。测试面构成（集成树实测）：_test 新增 17＝SelPreferenceFilter 8（品牌维度黄金/供应状态与系列含未标注哨兵/空偏好全适用/★不改变硬能力判定核心〔经真实 HardConstraintSelector 产记录验证 apply 零改写＋偏好 token 零混入＋"用户过滤后为空"硬可行事实保留〕/非硬可行候选跳过偏好维度/确定性输出序/契约违约四连 fail-fast/接口路径计数 Provider）＋SelCatalogDiff 9（空差异/型号三态黄金/可缺失 (absent) 哨兵/曲线点集增量黄金/曲线头部字段/兼容全键增量/纯函数确定性与身份链/契约违约 fail-fast/★锁定×比较历史隔离组合）；_contract_test 新增 4＝SelPreferenceDiffContract（分轨 token 词表/稳定码/登记表三方同源＋偏好零侵入硬记录＋static_assert 非评估器＋CON-05 失效链＋锁定表只增历史隔离） |
| T08 落位细化 ①（JointKind 承载与默认值语义——提议契约 v1 字段追加） | §17.1 交接行"modeling/runtime → selection：关节类型；旋转/移动能力"的落位承载＝`AxisWorkpointFacts.jointKind`（`JointKind` 枚举：Revolute/Prismatic——词面与 modeling 关节类型词对齐，continuous 经 modeling MDL-12 工程工作范围确认后属旋转传动、与本枚举 Revolute 同成员，类型细分不在本枚举承载）。**默认值＝Revolute 的依据**：R1 产品管线中 modeling MDL-12 已在链型入口阻断混合链（本卡 §2.1 MDL-12 行"链型判定与支持矩阵归 modeling"），能流转到选型的轴事实缺省即全旋转链成员；显式 Prismatic 是上游的范围外信号——默认值使 T04~T07 既有构造与调用零破坏；dynamics 卡产出收编 AxisWorkpointFacts 时按其卡对齐（字段名/分组语义不变前提下——T04 ② 同款纪律）。关节类型权威判定归 modeling/runtime（值传递；selection 不自判关节类型） |
| T08 落位细化 ②（范围外记录的承载面与三语义红线） | 卡 §2.2"结果按需求使用 DataInsufficient 语义"的落位读法：范围外记录 **verdict＝DataInsufficient、reasons 恒空、gaps 恰一条 axis-out-of-scope 缺口**（dimension 与 ReasonToken::AxisOutOfScope 词表文本同词——同一语义在"原因词表面"与"数据缺口面"分轨使用同一定位词；diagCode＝SEL-INPUT-AXIS-OUT-OF-SCOPE——T02 批 §2.2 已登记码，T06 reasonTokenDiagCode 映射 AxisOutOfScope→同码，两承载面同码单源）。不走 RejectionReason 承载的依据：T04 ⑦ 汇总规则 reasons 非空→Rejected，若以原因 token 承载会把范围外误升级为"候选淘汰"（违反 D-SEL-15"不升级整机不可行"）；缺口面（DataGap.diagCode 字段 v1 即为携带稳定码而设）语义吻合。三红线（契约测试钉扎）：①移动关节轴零 RejectionReason（不静默套用旋转传动）；②DataInsufficient（既非 Feasible——范围外不得当通过；也非 Rejected——不升级整机不可行，判定权在 evidence 汇总）；③缺口携带登记表稳定码（词表映射唯一实现点同码） |
| T08 落位细化 ③（缺口与工况无关＋记录数不变量） | 范围外是**轴级边界事实**（与工况无关）：筛选路径逐候选×逐 Prismatic 轴恰一条缺口（caseId 空串——DataGap 字段语义"维度级缺口与工况无关时"；多工况同轴不重复计入）；组合校核路径逐范围外轴恰一条缺口（在组合入口登记、置于格循环外——防多工况格循环重复 push；caseId 同空串）。**记录数不变量**：筛选路径记录数＝候选数×唯一轴数（Prismatic 轴也逐候选产出范围外记录）——T04 ④ 截断语义"调用方以记录数对比候选数×轴数感知"的计数契约不受阻断影响；T06 组装器按记录键对位校验的组装前提（记录键/组合键唯一）同不受影响 |
| T08 落位细化 ④（同轴 jointKind 矛盾 fail-fast 与双入口校验的分工） | 同轴多条 facts 声明不同关节类型＝物理矛盾（轴类型是轴级属性）——调用方契约违约，§10.2 校验边界 fail-fast（静默取首现会掩盖上游缺陷）。**双入口显式校验的分工**：T04 validateFacts 校验筛选直调路径；checkCombinations 入口校验组合校核路径——后者显式存在的原因：Prismatic 轴不进入 T04 筛选调用，全移动链时矛盾若只靠筛选器校验会逃过检查（轴级循环对范围外轴短路，矛盾校验必须在消费 jointKind 之前完成） |
| T08 落位细化 ⑤（codec 字段面演进与格级兜底语义） | AxisFactsBundle 编码（IRDSAFV1）表尾追加 jointKind（u8 枚举）——`kAxisFactsCodecVersion` 1→2（本文件 §3.5 篇 T05 canonical 协议"字段面冻结——新增字段只能表尾追加并递增 codec 版本"的既定机制执行；v1 字节流由 expectHeader 版本核对拒绝，不带静默兼容读）。解码侧词表外枚举值**严格拒绝**（协议违约 fail-fast）——静默归默认 Revolute 会把范围外轴误降级为旋转轴、违反 SEL-09 阻断语义。**格级兜底**：组合校核格聚合以轴级记录为输入，全移动链时 cellAxisRecords 为空、聚合会误判 Feasible——comboHasOutOfScopeAxis 标志兜底强制格 DataInsufficient＋note="axis-out-of-scope"；混合链旋转轴既有 Rejected 保持原样（范围外是数据/边界类事实，不覆盖既有淘汰原因——格定位面照旧）；评估器路径（③端口）经同一 checkCombinations 无分叉（EvaluatorPathBlocksPrismaticAxis 经 canonical 字节面物化 Prismatic 事实包钉扎公共接口路径） |
| T09 落位细化 ①（§3.2 include 面扩展——project 公共头，零链接边） | §14.8 处理器实现 `project::ICommandHandler` 需 include `project/CommandService.hpp`（公共头——R-2 许可形态）。**零编译链接边不变**：产品库不链 `sdurws_ird_project`（ird_gates SUB 面与 CMakeLists 配置期守卫钉住），全文件零 project 实现符号引用——`HandlerContext` 在 prepare 壳内零调用（对象 oid 走 `ObjectWrite.objectId` 空值语义＝project S6 装配点分配〔公共头 ObjectWrite 注释原文〕；基线判定全部经 `baseSnapshot` 值快照的 objectRefs 值面）；`HandlerRegistry` 注册归 L5 装配。两红线测试白名单同步（BuildRedLineTest `kAllowedUnits`＋BuildGraphContractTest include 面集合增 project；链接集合不变）——单元内同判据双防线与 ird_gates R2（只查公共头可达性）三面一致 |
| T09 落位细化 ②（回填记录对象落位形态——P-SEL-9） | §14.8 注释"各轴 robot-drivetrain 回填字段"落位为 selection 域独立记录对象（objectTypeToken＝`sel-device-backfill`；每命令恰一对象承载全部轴——多轴整体原子的记录面载体）。不冒用 modeling `robot-drivetrain` token：该 token 是修订闭包解析器路由键（modeling ObjectTypes.hpp 登记纪律——拼写/冒用＝路由失联），selection 自拼 robot-drivetrain canonical 字节＝第二实现（NFR-MNT-03）；权威 robot-drivetrain 写入归 modeling `apply-drivetrain-design` 命令链（requiresDualCompile 在彼侧），L5 装配层转译编排——登记 P-SEL-9（§19.1）。记录对象含逐轴条目（目录引用/安装关系/传动比/输入原值/转子独立字段）＋逐轴合成结果＋AT-30 复算提示（四域＋不沿用标志）——SEL-10 语义在记录面自足齐备 |
| T09 落位细化 ③（合成内核与断言口径——P-SEL-6 裁决前自持） | §12.4 五步：输入卫生（非有限/质量≤0/转子惯量≤0 拒绝——NFR-COR-03）→ 各部件惯量自质心向连杆系原点平行轴迁移 `I_i+m_i(|c_i|²E−c_i c_iᵀ)` → 求和（质量/加权质心/原点系惯量）→ 向合成质心回迁（结果＝BodyData 同语义：绕合成质心、连杆系姿态）→ MDL-06①~③同语义断言（m>0；SPD＝3×3 对称解析特征值严格>0；三角不等式 λ1+λ2≥λ3，相对容差 1×10⁻¹²〔附录 D 第 6 项〕）。对称性断言由六分量存储形态结构性满足（无不对称输入通道——诚实登记）。3×3 特征值＝对称解析闭式（主值法＋det 限幅）；黄金用例以解析可验证算例钉扎（GoldenValuesMassComInertia 断言旁附分数表达式） |
| T09 落位细化 ④（壳体/转子分轨——不重复计入的机器证明） | 壳体（电机/减速器）质量与惯量进合成（§12.4 合成项）；转子等效惯量为**独立登记字段**（`AxisBackfillEntry.rotorInertiaKgM2`/`AxisSynthesis.rotorInertiaKgM2`——映射侧显式计入的唯一位置，drivetrain §9.4 衔接）。用例 RotorInertiaNeverEntersSynthesis 以倍率扰动法证明：转子惯量任意倍率下合成三件套（质量/质心/惯量）逐位不变（IEEE754 精确相等）——若被重复计入必然漂移 |
| T09 落位细化 ⑤（组装面目录取值与数据缺失整体失败） | 组装器从目录快照取壳体质量（电机 6 kg/减速器 3 kg 目录列）与转子惯量；**电机壳体惯量目录 v1 无字段**——由调用方补充输入（`motorHousingInertiaSupplement`，ValueProvenance 归 UserProvided 语义）；**减速器壳体惯量为 optional 列**——缺失即 DataInsufficient 整体失败（§12.4 缺失处理＋P-SEL-6 保守口径：不允许部分回填）。目录标量壳体惯量列承载口径＝对角各向同性近似 {v,v,v,0,0,0}（单点换算，无第二种约定）。锁定引用 P-1 半区：lock.identity 与快照 manifest 身份逐字段一致（失配＝引用完整性破坏，调用方错误轨）；锁定对象 cv 的完整核对随 L5 装配（锁定对象读取器）补齐——v1 载荷携带零 cv、prepare 按 (oid, 任意 cv) 存在性半区核对闭包 |
| T09 落位细化 ⑥（canonical 协议两面与解码分轨） | 命令载荷 IRDSBFP1／记录对象 IRDSBFV1——小端定宽、IEEE754 位模式（非文本）、UTF-8、无填充、无环境量（project.md §4.8 canonical 规则同语义）；轴段字段序两面一致（协议一致性）。解码分结构/版本两轨（Malformed→invalid-payload／UnsupportedVersion→升级指引轨——NFR-DEP-04）；解码失败定位入 SEL-BACKFILL-PAYLOAD-MALFORMED / PAYLOAD-VERSION-UNSUPPORTED。轴数上限 4096、字符串上限 1 MiB（畸形载荷资源防御）；轴身份/jointId 保留值/同轴重复/参考系词表外双面拒绝（编码 fail-fast＋解码严格） |
| T09 落位细化 ⑦（T09 批 9 码——判定序登记） | DiagCodes 表尾追加 SEL-BACKFILL- 族 9 码（全表 45→54）：PAYLOAD-MALFORMED→PAYLOAD-VERSION-UNSUPPORTED→INPUT-INVALID→UNKNOWN-DEVICE→MOUNT-MISMATCH→DATA-INSUFFICIENT→RANGE-INVALID→LOCK-REF-MISMATCH→SYNTHESIS-ASSERT-FAILED（批内序＝§12.1 S3 判定序）；组装期与 prepare 期共用同码（同一拒绝语义不分阶段私设第二码——NFR-MNT-03）。拒绝语义映射：数据/调用方侧（解码/域校验/范围）→ RejectedInvalidInput〔invalid-payload〕；MDL-06①~③同语义断言失败 → RejectedHardAssert〔hard-assert-failed——硬断言就地阻止〕 |
| T09 落位细化 ⑧（prepare 端到端边界——如实登记） | `prepare` 为计划内核直通壳（HandlerContext 零调用）。HandlerContext 构造符号在 project 库内（CommandServiceImpl.cpp）——selection 零 project 链接边下测试面不可达：prepare 端到端（真实 ProjectCommandService submit→S1~S6→恰一新修订）**随 L5 装配集成面承载**；单元内以 planFromEnvelope 三态全覆盖（成功/invalid-payload 三类/hard-assert）＋壳转发 switch 代码面承载，不虚称端到端已执行。StaleRevisionRejected 语义归 project S2（§12.2 纪律 2——expectedRevision 失配由命令服务以 PRJ-STALE-REVISION-REJECTED 拒绝，selection 不复述）；锁定引用的基线侧存在性检查（SEL-BACKFILL-LOCK-REF-MISMATCH）是 selection 侧的防线纵深，与 S2 并行不悖 |
| T09 落位细化 ⑨（AT-30 复算提示承载） | `BackfillRecalcNotice`＝四域全量〔kinematics/dynamics/selection/optimization——SEL-10 原文枚举的封闭词表〕＋`retainPriorConclusion` 恒 false（"复核完成前不沿用原通过结论"）；记录对象编码位图（低 4 位）＋u8 标志，解码严格校验（高位残留/非法标志拒绝）。当前性判定与包络推进归 evidence（§11/§13.1——selection 只产出"哪些域需复算"的事实记录，不越权改判）；命令摘要随修订持久化复算提示（AT-30 摘要留痕面）。§12.2 纪律 7"旧结果不能被覆盖"由记录对象只增（PA-2）＋基线闭包 oid 继承改版承载 |
| T10 落位细化 ①（工厂形态升级——自由函数→装配载体类） | T02 门面为自由函数 `createSelectionPluginAssembly()` 返回纯描述符值；T10 按 dynamics WP-17-T09 同款升级为**装配载体类** `SelectionPluginAssembly`（unique_ptr<SelPanelModule>＋setServices/bind 系/session/refreshFromSession/module 转发面——工厂友元豁免访问私有成员）。T02 契约测试取值路径同步（`bundle.descriptor`），**登记值逐字不动**（pluginId/titleKey 断言原值——"登记值与既有契约测试不动"口径同 dynamics T09 批）。面板工厂闭包捕获模块裸指针——模块存活期由装配层保证（ui.md §10.9 装配时序；宿主在面板创建期与面板存活期保持门面存活） |
| T10 落位细化 ②（呈现 DTO 全自持——插件面零计算库结果类型） | §16 T10 行的候选表/淘汰原因/可行集呈现面落位为**自持呈现 DTO**（SelCatalogRow/SelCandidateRow/SelRejectionRow/SelGapRow——结果→呈现行的翻译归缝组装侧〔宿主装配层/harness〕单点完成）。结构依据：契约测试插件面全文扫描词表（13 计算符号——含可行集/目录快照等结果模型类型名，不剥注释）使插件面**不可 include 计算库结果头**——呈现 DTO 自持即该红线的结构承载（dynamics 无此约束面：其缝返回类型 Replay.hpp 不在词表，selection 词表更严——按最严口径落位）。插件面同时零 project 包含（直接访问词表——Backfill 公共头传递携带 project 公共头故不可 include）。零计算红线纪律不变：翻译归组装侧、本侧零业务判定零排序（呈现序＝缝给定序——稳定序权威在计算库/组装侧，§10.4） |
| T10 落位细化 ③（回填命令双词形自持——对账口径〔RUL-TOK 批更新〕） | 域命令的双词形书写点（P-SEL-3 销案后）：**project 命令 token**（无点 `apply-device-backfill`）权威常量在计算库回填公共头（T09 冻结终态——§4.4.4 语法）；**ui 命令 id**（点分 `selection.apply-device-backfill`）以**自持常量** `kSelBackfillUiCommandId`（SelPanelCommandCatalog.hpp，RUL-TOK 批由 `kSelBackfillCommandToken` 无点词形修订而来）承载——该头传递携带 project 公共头（处理器接口传递依赖），插件面零 project 包含（细化 ②）故不可 include，ui id 词形必须自持。两词形对齐关系（ui id 前缀＝pluginId＋"."、尾段＝project token、必含点）由契约测试消费计算库常量逐段对账钉住（BackfillTokenReconcilesWithComputeHeader——契约测试不在插件面扫描域，包含合法）。任一词形改名属跨版本契约变更（单元卡增量修订），经该对账用例同步改——无第二词表静默漂移空间。标题键按 ui.md §3.5 键族程序化派生（"cmd."+ui 命令 id+".title"——零字面复制；先例 modeling cmd.modeling.*.title 十键同构） |
| T10 落位细化 ④（回填提交流三态与 AT-30 呈现素材） | L-S4 `submitBackfill` 三态：①出口缝未装配→拒绝键 backfill-outlet-missing（诚实反馈不静默丢弃）；②宿主可用性缝返回 false→拒绝键 backfill-unavailable（权威判定透传——插件零本地判定；可用性缝未装配＝按可用呈现不拦截）；③经提交缝发起→受理＋AT-30 复算提示素材（四域文案键全量 `kSelRecalcDomain*`——呈现半区常量，与计算库复算提示域词表逐字对账由契约测试钉住；retainPriorConclusion 恒 false）。**真实修订/事务零知识**：插件面提交出口只发起意图 token——载荷组装（当前选中组合→回填载荷）、ProjectCommandService 提交、S1~S6 事务编排全归宿主装配批次（P-SEL-9/P-SEL-10 收口面）；插件零事务零修订语义（会话记录缓冲只是呈现史——不入持久层）。拒绝键小词表（kSelReject*）为本头唯一书写点 |
| T10 落位细化 ⑤（面板形态——三页合一 Tab 恰一条登记记录） | 工作流页/目录管理页/候选表页承载于**同一主面板**（QTabWidget 三页——回填入口与就绪投影是工作流页命令的直达呈现面、候选表与原因/缺口同数据分轨分区，拆分多面板会造成同数据多实例呈现漂移）——panels 恰一条登记记录（advanced=false 主面板位）。零 Q_OBJECT（QWidget 派生＋lambda 连接——无信号槽需求，AUTOMOC 保持 OFF，DTB §5.1 v0.17 行口径）；全部用户文本经 L-S5 文案解析（UX-02 唯一入口——含页标题/列头/按钮/占位），呈现序＝缝给定序零排序调用；范围外轴的双面一致呈现＝呈现键唯一书写点 `selOutOfScopePresentationKey()`（"axis-out-of-scope"——格级 note 键与缺口维键归一同键，wp19-t08 双面一致性的呈现面承载；范围外候选呈现态恒 data-insufficient＋reasonCount=0——三红线的呈现面语义） |
| T10 落位细化 ⑥（指标呈现与 UX-02 数值纪律） | `formatMetric`＝%g 最短形态＋单位词面直出（R1 无单位切换配置——SI 直显；显示单位切换与量纲换算唯一归宿主文案系统 core 换算入口，AT-27 纪律：呈现文本零回流身份/判定路径）；非有限数守卫（NaN/Inf 不进用户文本——"不适用"占位，ui.md §6.6"不伪造 0/空值"纪律）；候选行的质量/裕量素材存在性以 has* 位承载（false＝"不适用"占位——不伪造数值）；目录身份词面（catalogId/version）进用户文本合法（企业分配词面非哈希——UX-02），内容摘要不进任何呈现结构（ARC-04 身份不进呈现的结构承载——缺列即无从泄漏） |
| T10 落位细化 ⑦（GUI 呈现边界与 harness 通道——如实登记） | §15.3 GUI 流程约定落位：无人值守门禁不做 GUI 运行验证（既有环境事实）——SelPanelGui.WidgetPresentationDeferredToHarnessEnvUnavailable 以 **GTEST_SKIP envUnavailable** 如实登记（非通过——AGENTS §4.2；与 dynamics WP-17-T09 同款口径）；`sdurws_ird_selection_app`（app/HarnessMain.cpp）为 §15.3 手动点验通道载体——**本批交付构建留痕，未启动运行**（运行属手动验证流程：一次只启动一个 GUI 可执行文件、不用 offscreen；启动后可见三页呈现与回填提交流，演示缝零业务语义、回显承载提交出口）。模型层 12 活跃用例为界面链路用例的执行证明面（acceptance 1"界面链路用例通过"＝模型层流＋装配登记面契约用例承载——GUI 渲染不在这两组用例的断言面内，如实分轨） |
| T11 落位细化 ①（黄金目录源同源单点与 missing 收集序） | 四数据集共享同一黄金器件源（cat-sel-golden@1.0.0——M-101/M-102/M-103＋G-201/G-202/G-203＋兼容五对＋c101-power 曲线），生成脚本各自内置同源数据（跨数据集一致性由消费测试构造断言背书）；missing 收集序＝产品装配执行序（电机：过载两列→电压→制动→保持→thermal 两列→安装两列；减速器：回隙→寿命→壳体→外载荷三列→安装两列——CatalogValidation 装配段 optionalNum 调用序）；**curve_ref 不入 missing 清单**（引用列空＝无曲线引用——装配段按空串直跳的非"字段缺失"语义，T03 落位形态的登记面）；status 定级＝missing 非空即 Partial（curve_ref 缺失不影响 status） |
| T11 落位细化 ②（词表序承载与格 note 词面） | 黄金 expected 的逐条原因携带 `tokenOrdinal`（§10.3 词表序数值——消费测试按序对照，词面 `token` 同载供人读）；组合校核格 note 的失败摘要用**词表 kebab 文本**（"speed-insufficient"——与产品 `reasonTokenText()` 唯一映射点输出同词面；"pass;inertia-ratio-policy-unsettled" 的 Feasible 格拼接序＝note+"；"+未判定 token 文本） |
| T11 落位细化 ③（映射事实黄金联动单点与键现算） | sel-combo-golden 的逐组合逐轴电机侧映射事实由生成脚本按 c＝1/n 解析换算（τ_m＝c·τ_joint、ω_m＝ω_joint/c、P＝τ_m·ω_m；ω_m_rms/惯量比/反射惯量 J_rotor/c² 为给定事实——黄金联动单点在脚本，§9.1"selection 不重算"的消费面纪律）；组合键不进黄金 expected（产品 `makeDeviceCombinationId` 计算面——消费测试现算注入 MappingCombinationFact；包内容身份同理全零占位不伪造）；manifest（目录包 io JSON 通道）的 files 条目含 CSV 四文件真实 SHA-256（脚本现算），manifest 自身哈希不可自含全零占位（io 文件层核对职责——selection 透传不校验，CatalogTypes.hpp ManifestEntry 注口径） |
| T11 落位细化 ④（csv 方言行与测试侧 io 半区） | 黄金 CSV 首行为方言标识行 "ird-catalog-v1"、次行表头、数据自物理行 3 起（firstDataRowNo=3——io 卡 §5.6 行号口径，与 CatalogTestSupport.makeTable 契约一致）；消费测试的 CSV→ParsedFileTable 解析为"测试侧扮演 io 映射半区"（selection 零 io 编译边的直构形态——L5 装配前消费通道）；测试辅助函数（非 void）内禁用 gtest 断言宏（数据资产结构违约走 std::runtime_error 异常轨——gtest 测试体内传播即失败） |

### 19.4 风险登记（R-SEL-x）

| # | 风险 | 缓解 |
| --- | --- | --- |
| R-SEL-1 | dynamics 卡未产出，`dyn.joint-series` 提议契约为单方面约定 | P-SEL-1 登记；注册表键唯一性校验兜底；两卡同批对齐 |
| R-SEL-2 | 组合候选规模爆炸（电机×减速器×轴） | 逐轴先筛选（§7/§8）→ 组合构造去重＋批预算（§9.5）；NFR-PERF-03 分批 |
| R-SEL-3 | 组合集切片缓存粒度粗（改一候选全批重算） | R1 规模可接受；per-combo 缓存为 R2 优化项（OPT-D 规模化时评估） |
| R-SEL-4 | 合成物性与 MDL-05/06 断言双实现漂移 | P-SEL-6；黄金用例钉住同语义；附录 D 第 6/7 项容差同源 |
| R-SEL-5 | O-11 裁决前的惯量比维度呈现歧义 | 显式"未判定"标记＋呈现层参考值隔离（不入身份） |
| R-SEL-6 | 直线传动扩展的目录 schema 演进破坏 R1 兼容 | formatVersion 拒绝未知＋字段字典扩展位设计（§17.2） |
| R-SEL-7 | P-PR-9 未裁决阻塞回填命令注册 | **已处置（2026-10-10，P-SEL-3 销案联动）**：P-PR-9 按 O-35 无点形态裁决销案，回填命令 project token 冻结无点终态、ui 命令 id 点分过宿主校验（注册链路全通）；命令服务机制已就绪（PRJ-T10），风险面消除 |

### 19.5 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-10-06 | 首版草案（WP-19-T01 交付物）：基于 REQUIREMENTS v1.16、ARCHITECTURE v0.13、DTB v0.53 与 core/evidence/io/project/policy/modeling/drivetrain 卡既有契约编写；R1/R2 边界、目录包模型与版本身份、io 双层交接（P-IO-7 schema 注册）、能力曲线插值与外推边界、电机/减速器硬筛选、两段管线组合校核（消费唯一映射）、可行集/稳定排序/淘汰原因词表、Quick/Verified 证据边界与 O-11 保守处理、回填命令与物性合成、六方协作与责任矩阵、公共接口、验证方案与故障注入矩阵、任务拆分对齐、追踪矩阵、决策/风险/待裁决登记、自审与未执行声明 |
| v0.2 | 2026-10-06 | WP-19-T02 构建落位登记：头表"构建落位"行刷新＋§1.2 追加落位登记注（CMake/STATIC/插件最小可注册/两测试目标/SEL-* 17 码登记表/README 修正——业务实现随 T03+）＋§19.3 README 行闭环＋§21 执行状态按真实结果刷新；文档语义零变化（设计面不含 T02 实现新增决策——SEL-* 登记表为卡面既有登记值的物化） |
| v0.3 | 2026-10-07 | WP-19-T03 实现落位登记（目录包模型与导入校验）：§1.2 追加 T03 落位登记注（CatalogTypes/CatalogValidation/CapabilityCurve/CatalogProvider 四翻译单元＋CatalogTypes/CatalogProvider/Curve 三公共头＋内存锁定供给）；§19.3 追加 T03 落位细化七项（StableId 域内承载/字段字典按文件承载/v1 单位词表冻结 SI/DUPLICATE 两码分配/码归族不新增/manifest 身份分轨/curve_ref 分隔符）；§21 执行状态按真实结果刷新（集成/冒烟构建、_test 66/66、_contract_test 13/13、ird_gates、gtest XML 留痕 traceability/gtest-reports/wp-19-t03/）；设计语义零变更（全部登记为实现期细化，DTB §5.4） |
| v0.4 | 2026-10-07 | WP-19-T04 实现落位登记（电机/减速器硬筛选——SEL-03/04）：§1.2 追加 T04 落位登记注（Screening.hpp 第四公共头＋src/Screening.cpp 实现翻译单元——§7/§8 全维度＋§10.3 词表 34 token 物化＋IHardConstraintSelector/HardConstraintSelector）；§19.3 追加 T04 落位细化九项（记录与 ID 承载分阶段/输入形态与取消通道复用 evidence::IEvaluationContext/mounting-incompatible 共用 token/温度降额档位公式/功率双口径/外载荷力臂核算公式/verdict 三态汇总/条件缺失与数据缺失分界＋容差语义/AxisWorkpointFacts 提议契约 v1）；§21 执行状态按真实结果刷新（集成/冒烟构建、_test 137/137〔新增 SelScreeningMotor 41＋SelScreeningGearbox 30〕、_contract_test 22/22〔新增 SelHardScreeningContract 9〕、ird_gates 零命中、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t04/）；设计语义零变更（卡面 §7/§8/§10/§14.4 全部按原语义落位——登记项均为卡面未逐字给签名的形态补全与公式冻结，DTB §5.4） |
| v0.5 | 2026-10-08 | WP-19-T05 实现落位登记（组合校核——SEL-05/DYN-04 消费）：§1.2 追加 T05 落位登记注（Combination.hpp 第五公共头＋src/CombinationCheck.cpp 实现翻译单元——组合身份与构造/候选传动参数构造/P-SEL-1 提议契约 v1 映射批承载/惯量比参考语义/资格矩阵素材/校核核心/canonical 编解码六面/评估器 sel-combination-check；Screening.hpp 仅表尾追加 DeviceKind::Combination）；§19.3 追加 T05 落位细化八项（组合级记录身份承载/评估键 kebab 词形偏差/映射批提议契约承载/惯量比参考语义三态/组合集载荷与 c＝1/n 单点换算/筛选复用与判定粒度/评估器路径与取消语义/质量核算与必验工况集边界）；§21 执行状态按真实结果刷新（集成/冒烟构建、_test 184/184〔T04 既有 137 零回归＋新增 47：SelCombinationBuilder 8＋SelCombinationCheck 18＋SelCombinationCheckCodec 13＋SelCombinationCheckEvaluator 8〕、_contract_test 32/32〔T04 既有 22 零回归＋新增 SelCombinationCheckContract 10〕、ird_gates 零命中、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t05/＋构建日志 traceability/builds/wp19-t05/）；设计语义零变更（卡面 §9/§14.5 按原语义落位——评估键 kebab 词形、映射批提议契约承载、惯量比参考语义三态为登记的形态补全与保守细化，DTB §5.4） |
| v0.6 | 2026-10-08 | WP-19-T06 实现落位登记（可行集与淘汰原因输出——SEL-06/EVI-01/EVI-02）：§1.2 追加 T06 落位登记注（FeasibleSet.hpp 第六公共头＋src/FeasibleSet.cpp 实现翻译单元——结果身份块/SEL-06 指标素材/结果类型/可行集组装器/淘汰原因构造器/指标计算/sel 域 Profile 注册面；DiagCodes 表尾追加 28 码〔全表 45〕＋reasonTokenDiagCode 唯一映射；T05 评估器证据项 1→4 对齐 Profile 必需项①③④）；§19.3 追加 T06 落位细化八项（结果类型落位承载与 metrics 表尾扩展/build 第 4 参与资格判定细则/稳定排序 R1 固定键/diagRef 输出回填/T06 批码构造规则/裕量口径——指标面不参与判定/sel Profile itemId 词表与产出对齐/ResultFacts 归属边界）；§21 执行状态按真实结果刷新（集成/冒烟构建、_test 197/197〔T05 既有 184 零回归＋新增 13：SelFeasibleSet〕、_contract_test 37/37〔T05 既有 32 零回归＋新增 5：SelFeasibleSetContract〕、gate-all.ps1 85/85 通过〔含 validate-docs/集成测试全目标/冒烟树——ui_test 需 python313.dll PATH 前置为既有环境项，见 §21 注〕、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t06/＋构建日志 traceability/builds/wp19-t06/）；设计语义零变更（卡面 §10/§14.6 按原语义落位——build 第 4 参、metrics 表尾扩展、diagRef 回填、裕量口径、码值构造规则为登记的形态补全与实现细化，DTB §5.4） |
| v0.7 | 2026-10-08 | WP-19-T07 实现落位登记（优选品牌与目录版本管理——SEL-07/08）：§1.2 追加 T07 落位登记注（Preference.hpp 第七公共头＋CatalogDiff.hpp〔§3.1 组成表 CatalogDiff 组件公共面〕＋src/Preference.cpp＋src/CatalogDiff.cpp 两实现翻译单元——SEL-07 偏好分轨〔企业偏好配置/候选企业标注/偏好过滤输出/PreferenceFilter 五步——user-preference-filtered 独立 token、零改写硬筛选记录、偏好原因唯一经 IRejectionReasonProvider::make 接口构造〕＋SEL-08 结构化差异〔CatalogDiffer 纯函数——型号三态/字段级增量/曲线点集增量/兼容全键增量；D-SEL-14 会话工具 static_assert 非评估器〕＋锁定×比较历史隔离用例；锁定语义执行面仍为 T03 InMemoryCatalogProvider——project 对象库真实存储适配随 L5 装配收编，selection 零 project 编译边为 §3.2 硬约束）；§19.3 追加 T07 落位细化八项（公共头落位形态/偏好维度语义——白名单式三维度＋品牌双语义/apply 五步语义与错误两分法/偏好原因承载——经接口构造＋比较型文本字段/diff 型号字段注册表——比较集与不比较集/diff 曲线点级增量与兼容全键——输出序契约/会话工具边界与锁定管理归属/SEL-07 建议证据项承载与测试面构成）；§21 执行状态按真实结果刷新（集成/冒烟构建退出码 0 error 计数 0、_test 214/214〔T06 既有 197 零回归＋新增 17：SelPreferenceFilter 8＋SelCatalogDiff 9〕、_contract_test 41/41〔T06 既有 37 零回归＋新增 SelPreferenceDiffContract 4〕——集成树＋冒烟树双侧执行、ird_gates 独立运行零命中〔R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02〕、validate-docs PASS〔20 units/12 trace/312 task files〕、gate-all.ps1 一键 81/85——2 项 FAIL 为 modeling/ui 两单元 GUI 业务用例〔编译闭包不含 selection 任何产物，与 T07 改动零因果，分支既有状态如实登记〕、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t07/＋构建日志 traceability/builds/wp19-t07/）；设计语义零变更（卡面 §10.2/§10.4/§13.6/§14.0/§14.9 按原语义落位——偏好维度白名单语义、品牌 vendor 权威归目录、(未标注) 哨兵、diff 字段注册表与 (absent) 哨兵、manifest 级不进差异条目为登记的形态补全与实现细化，DTB §5.4） |
| v0.8 | 2026-10-09 | WP-19-T08 实现落位登记（移动关节范围外诊断——SEL-09/D-SEL-15）：§1.2 追加 T08 落位登记注（零新公共头/零新翻译单元——Screening.hpp 表尾追加 JointKind 枚举＋AxisWorkpointFacts.jointKind〔默认 Revolute〕＋makeAxisOutOfScopeGap 内联构造器；src/Screening.cpp 两筛选入口 Prismatic 轴先于全部旋转传动维度输出"范围外"记录〔verdict＝DataInsufficient、reasons 恒空、gaps 恰一条 axis-out-of-scope 缺口 diagCode＝SEL-INPUT-AXIS-OUT-OF-SCOPE；记录数不变量保持〕＋validateFacts 同轴一致性校验；src/CombinationCheck.cpp 组合入口同轴校验＋逐范围外轴恰一条缺口〔caseId 空〕＋该轴跳过事实查询与 T04 筛选＋格级兜底〔comboHasOutOfScopeAxis 且无轴记录时强制 DataInsufficient＋note="axis-out-of-scope"；混合链旋转轴 Rejected 保持〕＋AxisFactsBundle 编码表尾追加 jointKind〔kAxisFactsCodecVersion 1→2；解码侧词表外值严格拒绝〕；新测试文件 test/AxisOutOfScopeTest.cpp）；§19.3 追加 T08 落位细化五项（JointKind 承载与默认值语义——提议契约 v1 字段追加/范围外记录的承载面与三语义红线/缺口与工况无关＋记录数不变量/同轴 jointKind 矛盾 fail-fast 与双入口校验分工/codec 字段面演进与格级兜底语义）；§21 执行状态按真实结果刷新（集成/冒烟构建退出码 0 error 计数 0、ird_gates 独立运行零命中〔R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02〕、_test 228/228〔T07 既有 214 零回归＋新增 14：SelAxisOutOfScope 8＋SelCombinationCheck 3＋SelCombinationCheckCodec 2＋SelCombinationCheckEvaluator 1〕、_contract_test 42/42〔T07 既有 41 零回归＋新增 SelCombinationCheckContract.OutOfScopeAxisNeverYieldsRejectionReason 1〕、gate-all.ps1 一键未执行于本批〔T07 批 2 项 GUI FAIL 为他单元既有状态，编译闭包不含 selection 产物〕、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t08/＋构建日志 traceability/builds/wp19-t08/）；设计语义零变更（卡面 §2.2/§14.4/§9 按原语义落位——JointKind 值传递承载、默认 Revolute 语义、范围外缺口面承载〔DataInsufficient 而非 Rejected〕、双入口矛盾 fail-fast、格级兜底、codec 版本递增为登记的形态补全与实现细化，DTB §5.4） |
| v0.9 | 2026-10-09 | WP-19-T09 实现落位登记（选型回填命令与合成——SEL-10/MDL-16/AT-30）：§1.2 追加 T09 落位登记注（第八公共头 Backfill.hpp＋src/Backfill.cpp——§12.3 命令形态〔token apply-device-backfill 无点＋payload v1＋参考系 link-frame〕＋§12.4 合成内核〔平行轴五步＋MDL-06①~③同语义断言——壳体进合成/转子独立登记不参与〕＋IRDSBFP1/IRDSBFV1 canonical 编解码＋回填数据组装＋§14.8 处理器〔planFromEnvelope 内核＋prepare 直通壳〕；DiagCodes 表尾追加 9 码〔全表 54〕；新测试文件 test/BackfillTest.cpp〔26 用例〕＋contract_test/BackfillContractTest.cpp〔6 用例〕）；**§3.2 增量修订两处**（依赖表增行"selection 计算库 ⇢ project 公共头〔include 面，非链接边〕"＋红线自查行 R-2 例外注——§14.8 处理器实现 ICommandHandler 的接口实现形态消费，零编译链接边不变：产品库不链 sdurws_ird_project、全文件零 project 实现符号引用〔HandlerContext 零调用〕）；§12.3/§14.8 各追加 T09 落位注记（token 冻结常量与契约测试逐字符钉扎/回填记录对象 sel-device-backfill 落位形态——不冒用 robot-drivetrain token 防路由失联与第二实现/拒绝语义映射）；§19.1 追加 P-SEL-9（回填记录对象与权威 robot-drivetrain 写入的跨命令编排对齐——L5 装配层转译编排，modeling apply-drivetrain-design 命令链承载 requiresDualCompile）；§19.3 追加 T09 落位细化九项（include 面扩展/记录对象落位形态/合成内核与断言口径/壳体转子分轨机器证明/组装面目录取值与数据缺失整体失败/canonical 协议两面与解码分轨/T09 批 9 码判定序/prepare 端到端边界如实登记——L5 装配集成面承载/AT-30 复算提示承载）；§21 执行状态按真实结果刷新（集成/冒烟构建退出码 0 error 计数 0、ird_gates 独立运行零命中〔R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02〕、_test 254/254〔T08 既有 228 零回归＋新增 26：SelBackfillSynthesis 7＋SelBackfillCodec 4＋SelBackfillAssembly 7＋SelBackfillPlan 8——组间计数笔误 2026-10-10 GOV-DOC 批按源码实测更正（原登记 Synthesis 8/Plan 7 为笔误，总数 26 不变），acc/wp-19-t09/1 建议⑶〕、_contract_test 48/48〔T08 既有 42 零回归＋新增 SelBackfillContract 6〕——集成树＋冒烟树双侧执行、validate-docs PASS〔20 units/12 trace/312 task files〕、gate-all.ps1 一键未执行于本批〔T07 批 2 项 GUI FAIL 为他单元既有状态，编译闭包不含 selection 产物〕、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t09/＋构建日志 traceability/builds/wp19-t09/）；设计语义零变更（卡面 §3.2/§12/§14.8 按原语义落位——include 面扩展为 §14.8 接口实现形态的登记细化且零链接边、记录对象 token 形态与跨命令编排为 P-SEL-9 登记的权威唯一处置、合成五步与断言容差为 §12.4 公式的直接物化、prepare 端到端边界为依赖白名单硬约束下的如实登记，DTB §5.4） |
| v1.0 | 2026-10-09 | WP-19-T10 实现落位登记（selection 插件界面——UX-02/UX-10）：§1.2 追加 T10 落位登记注（assembly 门面扩展〔SelectionPluginDescriptor 增 stageToken/readinessDomainKey/commands/panels 四字段同构自持字段＋SelCommandDescriptor/SelPanelRegistration 登记值类型＋kSelStageToken/kSelDomainKey/kSel*PageKey 词表常量＋工厂升级装配载体类 SelectionPluginAssembly——dynamics WP-17-T09 同款〕＋plugin/ 七文件〔SelPanelTypes 六服务缝＋会话态＋呈现 DTO 全自持/SelPanelModel L-S1~L-S5 六流/SelPanelCommandCatalog 回填 token 自持/SelPanelModule/SelCatalogPanelWidget 三页合一 Tab 主面板/门面 TU 升级——零 Q_OBJECT〕＋app/HarnessMain.cpp＋sdurws_ird_selection_app〔§15.3 GUI 手动点验通道载体〕＋test/SelPluginPanelTest.cpp 13 用例＋契约测试扩展 3 用例＋BuildGraph allowed 集增登 _app）；§19.2 追加 P-SEL-10（插件真实注册端口消费缺口——契约 acceptance"经 IPluginUiRegistrar 白名单挂位"需 selection→ui 编译边而依赖白名单与 ird_gates 机器面均无该边且白名单文件不在任务 allowedFiles 不可增登：按 WP-17-T09 先例 B 方案〔acc/wp-17-t09/1 验收确认成立〕落位——装配面字段同构自持描述符承载＋白名单 token 对账以契约测试自持词表钉住＋真实注册端口与 UI-T21/T22 域供给协议消费归宿主装配批次收口）；§19.3 追加 T10 落位细化七项（工厂形态升级——自由函数→装配载体类〔T02 契约测试取值路径同步、登记值逐字不动〕/呈现 DTO 全自持——插件面零计算库结果类型〔13 符号禁词全文扫描的结构承载〕/回填命令 token 自持——两常量逐字对账/回填提交流三态与 AT-30 呈现素材〔真实修订/事务零知识——插件只发起意图 token〕/面板形态三页合一 Tab 恰一条登记记录〔范围外呈现键唯一书写点——wp19-t08 双面一致呈现面〕/指标呈现与 UX-02 数值纪律〔formatMetric %g＋SI 直显＋非有限守卫＋has* 占位位〕/GUI 呈现边界与 harness 通道如实登记〔envUnavailable 非通过〕）；§21 执行状态按真实结果刷新（集成/冒烟构建退出码 0 error 计数 0、ird_gates 独立运行零命中〔R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02〕、_test 267 运行＝266 通过＋1 envUnavailable SKIP〔T09 既有 254 零回归＋新增 13：SelPanelWorkflow 3＋SelPanelCatalog 2＋SelPanelCandidates 3＋SelPanelAssembly 3＋SelPanelText 1＋SelPanelGui 1〕、_contract_test 51/51〔T09 既有 48 零回归＋新增 3：登记面值断言＋白名单八 token 自持词表对账＋回填 token 对账〕——集成树＋冒烟树双侧执行、validate-docs PASS〔20 units/12 trace/312 task files〕、gate-all.ps1 一键未执行于本批〔T07 批 2 项 GUI FAIL 为他单元既有状态，编译闭包不含 selection 产物〕、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t10/＋构建日志 traceability/builds/wp19-t10/）；设计语义零变更（卡面 §3.1/§3.4/§16 T10 行按原语义落位——字段同构自持描述符为 P-SEL-10 登记的 B 方案处置、呈现 DTO 全自持为插件零计算红线的结构承载、回填 token 自持对账为零 project 包含纪律下的对账形态、GUI 呈现边界为 §15.3 约定的如实登记，DTB §5.4） |
| v1.1 | 2026-10-09 | WP-19-T11 实现落位登记（契约测试与选型黄金数据集——AT-08/AT-30）：§1.2 追加 T11 落位登记注（四数据集 testdata/golden/sel-* 目录化落位——sel-catalog-golden〔contract-fixture；v1 正例包五文件＋六误例单点变异表＋装配黄金面＋逐误例期望校验报告〕＋sel-screening-golden〔analytic-case；四算例 24 记录——case-heavy 全维淘汰/缺口＋case-light 基准可行面＋sf-edge SF 恰好满足边界＋axis-oos 移动关节范围外；每个淘汰项含 actual/required/unit/thresholdSource——ERR-01〕＋sel-combo-golden〔analytic-case；四组合——K1 全维通过/K2 双超限/K3 恰好边界/K4 兼容双保险；映射事实 c＝1/n 解析换算黄金联动〕＋sel-backfill-golden〔analytic-case；§12.4 五步合成黄金＋三类组装拒绝＋复算提示恒值〕；容差档案 sel-golden 12 条目全 appendixD-fixed；消费测试 test/SelGoldenDatasetTest.cpp 10 用例〔公共接口路径钉扎〕＋契约测试 contract_test/SelGoldenDatasetContractTest.cpp 4 用例；红实验证〔篡改层 FAILED→node 重跑逐字节一致→复绿——实测留痕〕）；§19.3 追加 T11 落位细化四项（黄金目录源同源单点与 missing 收集序——curve_ref 不入 missing 的装配语义登记/词表序承载与格 note kebab 词面/映射事实黄金联动单点与键现算——包内容身份全零占位不伪造/csv 方言行与测试侧 io 半区）；§21 执行状态按真实结果刷新（集成/冒烟构建退出码 0 error 计数 0、ird_gates 独立运行零命中〔R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02〕、_test 277 运行＝276 通过＋1 envUnavailable SKIP〔T10 既有 267 零回归＋新增 10：SelGoldenCatalog 3＋SelGoldenScreening 2＋SelGoldenCombo 2＋SelGoldenBackfill 3〕、_contract_test 55/55〔T10 既有 51 零回归＋新增 4：SelGoldenDatasetContract〕——集成树＋冒烟树双侧执行、validate-docs PASS〔20 units/12 trace/312 task files〕、gate-all.ps1 一键未执行于本批〔T07 批 2 项 GUI FAIL 为他单元既有状态，编译闭包不含 selection 产物〕、gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t11/＋构建留痕 traceability/builds/wp19-t11/execution-log.md）；设计语义零变更（卡面 §15.1 黄金数据集四类样例的目录化物化与 §15.3"核心筛选优先模型测试＋黄金数据集"的执行面承载——missing 收集序/词面承载/联动单点/io 半区为登记的形态补全与实现细化，DTB §5.4） |
| v1.2 | 2026-10-10 | ASM-PLUG 收口登记（宿主装配批次——P-SEL-10 注册端口消费消账）：§19.2 P-SEL-10 状态更新为**注册端口消费已收口**（selection→ui 编译边按治理程序增登〔ird_gates_whitelist.cmake IRD_ALLOWED_UNIT_EDGES＋IRD_EXTRA_EDGE_REFS＋dependency-graph.json 刷新至 68 边——双面留痕〕；assembly 门面新增 translatePluginUiDescriptor/registerWithHostRegistrar 真实注册翻译面——§10.9 装配期一次、白名单/重复/描述符校验全走宿主注册端口实现、无 registrar 即 fail-fast；§11.2 模块半区 plugin/SelUiModule 落位——readonlyProjections 经 L-S1 透传流翻译、buildDraftCommand 恒 nullopt＝写语义唯一经①命令端口〔卡 §12.1/§12.3〕的域无草稿语义实现、onShellReady 持壳引用；"字段同构无 ui 侧编译期校验"警告解除——真实编译边由编译器校验；SelUiModule.cpp project 公共头包含行具名豁免〔§11.2 冻结接口完整类型——计算库 v0.9 同款 include 面口径，零链接边不变〕）；**P-SEL-3 联动事实登记**：宿主校验序实测回填命令 token（无点 kebab——P-SEL-3"无点建议值占位"）不满足 ui §7.2 点分句法被权威判 InvalidDescriptor——插件零改写零本地判定，报告不入列〔§10.9〕；裁决后翻译产物自然过校验（翻译函数零改动），P-SEL-3 裁决义务不变；UI-T21/T22 树/属性页域供给协议仍保持登记（随其装配批次）；§21 执行状态刷新（_test 277 运行＝276 通过＋1 SKIP＋0 FAILED〔本批零源码改动——基线原值零回归〕、_contract_test 58/58〔既有 55＋SelPluginRegistration 3——替身捕获字段同构／真实注册端口宿主校验序实测〔P-SEL-3 拒绝面如实钉扎〕／空端口 fail-fast〕；集成 11 目标＋冒烟全树构建零错误＋ird_gates 零命中〔含新边 GRAPH 一致性核对〕；GUI 实机点验未启动如实登记）；gtest XML 留痕 traceability/gtest-reports/asm-plug/、执行留痕 traceability/builds/asm-plug/execution-log.md；文档其余章节语义零变化（§16 T10 行设计面与翻译实现一致——差异为收口形态登记非语义变更，DTB §5.4） |
| v1.3 | 2026-10-10 | RUL-TOK 命名词形裁决销案登记（所有者授权装配批——P-SEL-3/P-PR-9 销案＋selection 注册词形修复；分支 rul-tok；裁决原话"按照你的建议裁决命名规范〔无点 vs 点分〕后，选型面板与挂载一起收口"）：**双词形并存裁决落地**——project 命令 token 面＝无点 kebab（O-35 裁决"采用无点形态＝服从现行冻结语法"，P-PR-9 同批销案）；ui 命令 id 面＝点分 `<域前缀>.<kebab>`（ui.md §7.1/§7.2 既冻句法，modeling 十条域命令先例）。**代码修订（实现为既冻句法的对齐，非新决策）**：①自持描述符回填命令 ui 命令 id 由无点 `apply-device-backfill` 修订为点分 `selection.apply-device-backfill`——自持常量更名 `kSelBackfillUiCommandId`（SelPanelCommandCatalog.hpp，双词形单一书写点登记见文件头注）；`kSelBackfillCommandToken` 旧名退役；计算库 `kBackfillCommandToken` 无点冻结终态零变化（值与逐字符钉扎契约测试 BackfillContractTest.CommandTokenDotlessFrozenSyntax_PSEL3 保持零变化——该测试文件历史注释措辞按任务指令保持零变化，销案事实以本卡登记为准）；面板提交意图 token 与按钮标题键随常量联动（标题键派生 `"cmd."+ui id+".title"`——先例 modeling cmd.modeling.*.title 同构）；翻译函数零改动（id＝token 逐字直拷——asm-plug v1.2 登记"裁决后翻译产物自然过校验"兑现）。②测试断言翻转两处：selection contract_test `ActivationPassesRealRegistrarPort_ASMPLUG_ACC2` 的 InvalidDescriptor 诚实拒绝断言翻转为宿主校验序三查全过 Ok（装配报告恰一条 ok=true、面板/命令计数与描述符一致；翻译描述符六面字段同构断言保持）；对账用例 `BackfillTokenReconcilesWithComputeHeader_WP19T10_ACC1` 随双词形更新为前缀/尾段/必含点三步对账；ui_contract_test `SixDomainHostAssemblySequence_Snapshot_ASMSTUDIO_ACC1` 聚合快照五 Ok 翻转为六 Ok 白名单序入列（statuses 全 ok＋applyEntries 2→3 条＋报告行 ok=1 形态——宿主承接 TU 与校验序零改动）。**文档面登记**：§1.3/§12.3/§14.8/§19.1 P-SEL-3 与 P-SEL-10/§19.3 T10 细化③/§19.4 R-SEL-7/上游冲突清单同步翻转；§21 执行状态刷新。**workflow 单元零改动核实**：全域 grep kSelBackfillCommandToken/kBackfillCommandToken/apply-device-backfill 在 workflow/ 零命中（SelBackfillTranslateFlow 以替身处理器承载 token 路由语义——ASM-WF 登记形态，耦合不存在）。**宿主产品源码零改动核实**：ui/src 与 ui/plugin 零 diff（裁决在域侧生效）。验证：集成/冒烟双模式构建零错误、selection/ui/project 三单元 _test 与 _contract_test 回归全绿（gtest XML 留痕 traceability/gtest-reports/rul-tok/）、ird_gates 零命中；GUI 不在无人值守范围如实登记。内置元数据命令族处理器实体不落位（PRJ-T10 登记裁决后随增量任务——本批只销案不落位实体，防越界） |

---

## 20. 交付前自审

> 本节为**文档自审**，不构成实现测试、构建或正式验收结论。

| # | 自审项 | 结论 |
| --- | --- | --- |
| 1 | 是否把 selection 错写成 L2 drivetrain 共享服务 | 否——L4 业务域（§2.3/§3.1） |
| 2 | 是否违反业务域计算库与 Qt 插件分离 | 否——二分结构＋零 Qt 计算库（§3） |
| 3 | 是否让 UI 插件执行筛选或目录计算 | 否——插件零计算红线（§3.4，机器可断言） |
| 4 | 是否让 selection 直接读取 CSV/JSON 文件而绕过 io | 否——io 文件层＋selection 业务层（§5.1） |
| 5 | 是否重复实现 drivetrain 的传动比/虚功/耦合矩阵/效率/反射惯量 | 否——③端口消费唯一映射（§9；禁止项重申） |
| 6 | 是否重复实现 dynamics 的逆动力学 | 否——只消费关节侧事实（§13.2） |
| 7 | 是否把 modeling 的 RobotDesign 编辑归给 selection | 否——回填经命令服务写 robot-drivetrain 对象（§12） |
| 8 | 是否把 project 的事务和修订写成 selection 所有 | 否——project 拥有（§13.5） |
| 9 | 是否把 evidence 的证据等级/当前性/正式工程判定写成 selection 所有 | 否——Facts 只含事实（§11/§14.7） |
| 10 | 是否把 execution 的 worker/取消/检查点/缓存淘汰写成 selection 所有 | 否——能力声明＋execution 所有（§13.4） |
| 11 | 是否遗漏目录版本、来源和内容身份 | 否——§4.3 身份关系表 |
| 12 | 是否遗漏能力曲线默认禁止外推 | 否——§6.2/§6.3 |
| 13 | 是否遗漏文件引用和型号唯一性 | 否——§5.3 |
| 14 | 是否丢失实际值、要求值、单位和阈值来源 | 否——RejectionReason 全字段（§10.1/§10.3） |
| 15 | 是否把目录缺失/映射失败/数据不足/能力不足混为一谈 | 否——§10.2 分层空集语义表 |
| 16 | 是否把空可行集直接判为整机工程不可行 | 否——D-SEL-10 |
| 17 | 是否把 Quick 淘汰当成 Verified 结论 | 否——D-SEL-12 |
| 18 | 是否把器件候选可行当成整机工程通过 | 否——§11.2 |
| 19 | 是否忽略多负载、急停、保持和必验工况 | 否——§9.4 资格矩阵 |
| 20 | 是否遗漏工作点、轨迹段和工况身份 | 否——RejectionReason 定位字段 |
| 21 | 是否遗漏 SEL-05 的共享 DriveTrainMappingEvaluator | 否——D-SEL-6 |
| 22 | 是否忽略 O-11/P-POL-3 待裁决 | 否——P-SEL-4＋保守行为（§11.3） |
| 23 | 是否在 R1 静默接受移动关节 | 否——范围外诊断（D-SEL-15） |
| 24 | 是否把 SEL-09-S1/MDL-12-S1 提前写成阶段 C 已实现 | 否——§2.2/§17.2（全部标记 R2） |
| 25 | 是否把 OPT-D 联合器件优化写成 selection 自己拥有 | 否——内层消费定位（§17.3） |
| 26 | 是否绕过 ProjectCommandService 直接回填 | 否——D-SEL-11 |
| 27 | 是否遗漏 StaleRevisionRejected | 否——§12.1/§12.2-2 |
| 28 | 是否在回填时重复计入转子惯量 | 否——§12.4 分字段 |
| 29 | 是否在取消/失败/崩溃/数据不足后发布完整可行集 | 否——§13.4/§11 |
| 30 | 是否遗漏历史结果不可变和迟到结果隔离 | 否——§4.2/§11.2/§13.4 |
| 31 | 是否允许显示单位改变计算身份 | 否——§4.3/§14.0 |
| 32 | 是否引入未经上游批准的新状态/阈值/证据等级/诊断码 | 否——SEL-* 为建议值（注册归 diagnostics）；未冻结阈值不引入（P-SEL-4） |
| 33 | 是否将只有 README 的 selection 目录写成真实代码已实现 | 否——§1.2 如实登记 |
| 34 | 是否越权修改需求、架构或其他单元机制 | 否——只新建本卡；冲突登记 §19.2/§19.3 |

**上游冲突集中清单**：未发现语义级冲突；四项待对齐（非冲突）：O-11 阈值归属（已给落位意见）、P-PR-9 token 语法（**已裁决销案 2026-10-10**——O-35 无点形态终态，P-SEL-3 同批销案，双词形边界见 §12.3）、`dyn.joint-series` 契约待 dynamics 卡、合成断言设施共享归属——各项已登记影响面、保守设计、裁决者与未裁决前阻断范围（P-SEL-4 未裁决前惯量比维度"未判定"，不阻断其余设计；其余项未裁决前按保守口径占位，不影响已完成设计）。

---

## 21. 未执行的实现测试、构建和 GUI 测试（如实声明）

| 类别 | 状态 |
| --- | --- |
| 产品源码/CMake/插件/测试实现 | **T02 登记面＋T03 领域面＋T04 硬筛选＋T05 组合校核＋T06 可行集与淘汰原因＋T07 优选品牌与目录版本管理＋T08 移动关节范围外诊断＋T09 回填命令与合成＋T10 插件界面＋T11 契约测试与黄金数据集已落位**（T11：testdata/golden/sel-* 四数据集〔sel-catalog-golden contract-fixture＋sel-screening/combo/backfill-golden analytic-case——DatasetManifest ird-golden-manifest/1 登记＋integrity 全覆盖＋generate 脚本 committed〕＋容差档案 testdata/tolerance/sel-golden〔12 条目 appendixD-fixed〕＋新测试文件 `test/SelGoldenDatasetTest.cpp`〔10 用例——公共接口路径钉扎〕＋`contract_test/SelGoldenDatasetContractTest.cpp`〔4 用例〕＋CMake 两测试目标增列——见 §1.2 T11 落位登记注与 §19.3 T11 ①~④） |
| 集成模式构建 / 独立冒烟构建 | **已执行零错误**（WP-19-T11：集成模式 selection 产品库＋两测试目标增量构建退出码 0、error 计数 0〔消费/契约测试编译链接成功——testkit Dataset/JsonLite/ToleranceProfile/GoldenFixture 设施消费面自证〕；独立冒烟全树构建 error 计数 0。注：全仓 ALL_BUILD 中 RobWork/RobWorkSim 框架插件存在与本任务无关的既有编译错误〔框架区域零修改，SA-02〕，industrialrobot 区域零错误）。**〔2026-10-10 RUL-TOK 批刷新〕**：集成模式增量配置＋按目标构建（selection 产品库/插件＋三单元六个测试目标）退出码全 0；独立冒烟全树构建退出码 0 |
| 单元测试 / 契约测试 / 黄金数据集（sel-*） | **_test 277 运行＝276 通过＋1 SKIP〔SelPanelGui 呈现 envUnavailable——T10 既有登记，非通过〕、_contract_test 55/55 已执行通过（集成树执行；sel-* 四数据集目录化黄金已按 DatasetManifest 登记并消费）**（WP-19-T11：T10 既有 266＋51 零回归＋T11 新增 10＋4——SelGoldenCatalog 3＝正例包装配黄金〔条目值/missing 显式清单/装配序/包内容身份回填＋同输入恒同〕＋六误例期望校验报告逐 issue〔码/文件/行/列/型号/比较字段〕＋InMemoryCatalogProvider 锁定语义〔重复锁定拒绝/load 幂等/摘要不符 fail-fast〕；SelGoldenScreening 2＝四算例 24 记录逐字段黄金对照〔verdict/逐原因 ERR-01 四要素/逐缺口——含曲线区间外拒绝缺口、力臂核算解析值 250 N、SF 恰好边界 300 rad/s、手算锚点第三路〕＋两次筛选逐记录全等〔NFR-COR-02〕；SelGoldenCombo 2＝四组合校核黄金〔verdict/组合级原因/格 note kebab 词面/totalMass 经容差档案〕＋可行集组装闭环〔K1 进可行集＋diagRef 回填非空——IFeasibleSetBuilder 接口路径〕；SelGoldenBackfill 3＝组装＋合成黄金逐分量〔容差档案 sel-golden——附录 D 第 9 项〕＋载荷往返＋planFromEnvelope Planned 恰一对象写＋复算提示四域全量不沿用＋三拒绝分类＋双编码逐字节确定；SelGoldenDatasetContract 4＝四数据集 load 全量校验构造性证明＋integrity 覆盖关系＋档案通道与保守纪律＋数据根布局）；**红实验证（非恒真证明）已实测**：篡改层〔screening expected BrakeInsufficient required 25→26＋manifest 哈希同步重申报〕→ ScreeningGoldenTables **FAILED**；还原层〔node 重跑 generate 脚本 sha256 7a3631b845ee… 与篡改前申报值逐字节一致〕→ 复绿——留痕 traceability/builds/wp19-t11/execution-log.md §3）；gtest XML＋ird-test-report.json 留痕 traceability/gtest-reports/wp-19-t11/）；§15 V7 的 L5 装配集成面用例（真实 ProjectCommandService submit→S1~S6→恰一新修订——HandlerContext 构造符号在 project 库内，selection 零链接边不可达）、§15 V1 曲线区间外黄金面的完整 23 项注入矩阵〔T03/T04 单测＋本批黄金面已覆盖主链路——余下注入变体随维护批次〕与 §15 V9 R2 直线传动用例**未执行**（随 L5 装配/R2〔阶段 D〕——V9 全部标记 R2；③端口全链自动化行使〔execution 编排＋drivetrain 评估器真实注册调用〕未执行——R1 无 execution 落位，登记 P-SEL-1/R-SEL-1）。**〔2026-10-10 ASM-PLUG 收口批刷新——P-SEL-10 注册端口消费已执行〕**：`_test` **277 运行＝276 通过＋1 SKIP＋0 FAILED**（本批零源码改动——基线原值零回归）；`_contract_test` **58/58**（既有 55 零回归＋**SelPluginRegistration 3**——TranslatedDescriptorMatchesSelfHeldFace〔替身捕获翻译描述符六面字段同构＋StageId 词表对位〕／ActivationPassesRealRegistrarPort〔真实 createPluginUiRegistrar 八 token 词表——白名单/重复两查过、描述符查被宿主权威判 InvalidDescriptor：回填命令 token 无点 kebab〔P-SEL-3 待裁决的"无点建议值占位"〕不满足 ui §7.2 点分句法，插件零改写零本地判定、报告不入列〔§10.9〕；经 ui 接口消费 readonlyProjections/buildDraftCommand 钉扎 §11.2 交付面——裁决后翻译产物自然过校验，翻译函数零改动〕／ActivationWithoutRegistrarFailsFast〔空端口 std::invalid_argument fail-fast〕）；gtest XML 留痕 traceability/gtest-reports/asm-plug/、构建/门禁/测试执行留痕 traceability/builds/asm-plug/execution-log.md。**〔2026-10-10 RUL-TOK 批刷新——P-SEL-3/P-PR-9 裁决销案＋selection 注册词形修复已执行〕**：`_test` **277 运行＝276 通过＋1 SKIP＋0 FAILED**（既有 SelPanelGui envUnavailable SKIP 保持）；`_contract_test` **58/58**（用例数与 asm-plug 批一致——本批为同三用例的断言翻转与对账口径更新，零新增用例：ActivationPassesRealRegistrarPort 断言翻转为宿主三查全过 Ok＋报告恰一条 ok=true〔P-SEL-3 销案兑现〕／BackfillTokenReconciles 对账更新为双词形前缀-尾段-必含点三步／登记面用例 token-标题键期望值随点分词形更新）；project 侧 `_test` **255 运行＝254 通过＋1 SKIP**（Tx11 StageB 前置既有 SKIP）＋`_contract_test` **28/28**、ui 侧 `_test` **255/255**＋`_contract_test` **34/34**（SixDomain 聚合快照六 Ok 翻转执行证明）——三单元回归全绿；ird_gates 零命中（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02，引擎自测 9 项按预期检出）；gtest XML 留痕 traceability/gtest-reports/rul-tok/（六件）；GUI 不在无人值守范围如实登记（SelPanelGui SKIP 与 harness 手动通道同既有口径） |
| ird_gates 门禁 | **已执行零命中**（WP-19-T11：`cmake --build build --target ird_gates` 独立运行退出码 0——R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 全部检查通过零命中〔引擎自测 9 项按预期检出——pass_clean/pass_r4comment 干净通过＋fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2 按预期检出〕；gate-all.ps1 一键未执行于本批——T07 批登记的 2 项 FAIL 均为 modeling/ui 单元 GUI 业务用例，编译闭包不含 selection 任何产物、与 T11 改动零因果，属分支既有状态如实登记）。**〔2026-10-10 RUL-TOK 批刷新〕**：ird_gates 独立运行退出码 0、全部检查零命中 |
| GUI 测试 | **未启动运行（构建留痕已交付）**（WP-19-T11：本批零 GUI 用例——SelPanelGui 用例 envUnavailable GTEST_SKIP 为 T10 既有登记如实保持非通过；`sdurws_ird_selection_app` harness 仍为 §15.3 手动点验通道载体，运行属手动验证流程：一次只启动一个 GUI 可执行文件、不用 offscreen） |
| 文档验证脚本 | validate-docs.ps1 已执行通过（WP-19-T11：PASS〔20 units/12 trace/312 task files〕——本批文档面改动仅本单元卡增量修订；validate-task/verify-task 随各任务验收会话执行，本批未执行如实登记） |

以上任何一项在后续任务中执行后，须按 DTB §5.4 与本卡 §19.5 登记真实结果；未执行项不得标注"通过"。
