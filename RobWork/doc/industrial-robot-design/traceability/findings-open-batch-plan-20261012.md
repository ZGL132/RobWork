# findings open 存量家族类分批处置清单（2026-10-12 治理批——只立清单，零翻牌）

| 字段 | 值 |
| --- | --- |
| 文档代号 | FINDINGS-BATCH-PLAN |
| 依据 | 2026-10-12 全面盘点报告（第五节账实不符族）＋任务卡指令"524 条 open 存量的家族类批量处置只出分批清单待所有者裁决，不擅自翻" |
| 计数口径 | 基线 a82f58bc（origin/redesign-main，2026-10-12）实测 findings.json 共 815 条，其中 **open 506 条**（非 524——任务卡数字与盘点时点差异，如实登记）；severity：blocking 3／major 24／minor 8／suggestion 471 |
| 纪律 | 本文件只做家族聚类与分批建议；**任何状态翻转须经所有者裁决后按批执行**（findings 追加优先、勘误留痕） |

## 一、P0——blocking 3 条（产品缺陷，建议立卡修复，不属家族销案）

| id | 日期 | 摘要 |
| --- | --- | --- |
| F-460 | 2026-10-02 | 需求域二连 draft.apply 必拒：首次应用回执只回填根对象身份，编辑器工作集根引用表四集合挂载态停留首应用前 |
| F-495 | 2026-10-04 | 需求三维区域采样结果状态着色未闭环（宿主审核 P1，UI-T65 已消费其着色判定部分——残余面待核对后收窄或翻牌） |
| F-552 | 2026-10-08 | 新建项目触发进程 crash（无错误对话框；所有者实例 exe 取证） |

## 二、P1——major 24 条（两组）

**组 1 宿主装配/呈现类（8 条，2026-10-08~09 宿主审核批产出）**：F-550（首应用前三维无模型可拾取——RT-T15/UI-T78 图元渲染落位后残余面待复测收窄）、F-554（就绪门控空 CheckContext）、F-557（拒绝反馈空禁用键 resolveText）、F-558（pick-feature 会话闭包缝恒空）、F-559/F-560/F-561（三维投影缓存清理缺位族）、F-562（区域框角序不符）。
**组 2 领域代码审计类（16 条，2026-10-12 批 D/E1 审计产出）**：F-714（框架 RWSimulatorPlugin fresh 构建失败——SA-02 patch 通道裁决）、F-795（drivetrain getString 空串 UB）、F-798（trajectory 游标失效 Transfer 占位丢站）、F-799/F-800（modeling 断言序/六轴重置 defaultTcp）、F-801/F-802/F-803（io Csv/Json 三项）、F-806（runtime parseNameMap 复核相邻比对）、F-811/F-812/F-813/F-814（selection 四项）、F-815/F-816（reporting 两项）。

## 三、家族类批量处置建议（全部待所有者裁决，本批零翻转）

| 批 | 家族 | 计数（open） | 特征 | 建议处置口径（供裁决） |
| --- | --- | --- | --- | --- |
| F1 | **F-012 提交标题家族**（"同型再现：…标题名词短语开头，非动词开头"） | **86** | suggestion；全部指向历史提交的标题风格违规（F-012 首例 2026-09 起） | ①裁决"历史提交不可改写＋动词开头规范已生效"后**整族销案**（逐条 resolution 追注销案依据，status→closed 或 fixed-历史豁免，二选一由所有者定）；或②按"现行流程已自查到位"批量转 fixed。任一口径均可一次批处理 86 条 |
| F2 | **幻影清单/悬空引用家族**（"幻影清单家族第 N 例／悬空引用"） | **21** | suggestion；留痕报告字段/清单与实物不符（F-111/F-113 起家族计数） | 逐例核实**现行**留痕链是否已由后续批次修复：已修复→翻 fixed（fixedAt＝修复批）；核实仍缺→登记修复任务。建议分两小批（reporting 类／builds 留痕类） |
| F3 | **P3 前缀代码建议族**（round2/单元代码审查产出的 "P3：＜文件＞:＜行＞…"） | **35** | suggestion；代码级小项（Csv.cpp/Import.cpp/TaskPresentation.cpp 等） | 按单元分批（io／ui／runtime…）随对应单元的下一实现任务顺手修复或显式豁免，逐条追注归宿任务号 |
| F4 | **契约 JSON 路径类**（"tasks/foundation/WP-13-Txx／WP-15-Txx…"） | **16** | 契约面问题（designRefs/knownPitfalls/allowedFiles 登记类） | 契约编译批（contract-compile 通道）统一复核：登记失实→契约增量修订；已过时→追注消账 |
| F5 | **F-122/F-133/F-134/F-140 同型族** | **9** | suggestion；同型登记 | 与 F2 同法逐例核实现状后分诊 |
| F6 | 零散 open 余量 | 约 336 | 其余 suggestion/minor | 不属家族批量；维持逐条流转（对应单卡验收时随手消费） |

## 四、本批已按任务卡明列完成的滞后修正（另见该批提交）

governance-log 8 行翻 closed（P-EV-9/O-14、P-POL-5/O-20、P-PR-4＋P-EX-5、P-PR-7、P-RT-4）＋DTB O-20 行；runtime.md P-RT-4/P-RT-8/P-RT-9、kinematics.md P-KIN-2/P-KIN-6、testkit.md P-TK-4、policy.md F-546 状态句、trajectory.md P-TRJ-1、ui.md §13 四行、requirements.md §11 段、selection.md §19.3 复核注＋§19.5 版本链勘误（v1.6）——均先复核对端消账事实再回改。

## 五、顺带观察（本批不修，登记待裁）

- units/trajectory.md §22 变更记录表存在**双 v0.15、双 v0.4 行**及整表非单调排序——与 selection.md 同族版本链缺陷，建议随下一治理批勘误（selection.md 本批已修）。
- traceability/unit-status.json（20 单元全 not-verified）与 DETAILED-DESIGN.md §20 各行版本词普遍滞留——批量索引刷新涉及全部单元口径，已在 selection.md §19.3 复核注中登记"留待所有者另批"。
