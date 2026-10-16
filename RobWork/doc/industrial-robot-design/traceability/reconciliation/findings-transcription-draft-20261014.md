# findings 转登欠账盘点与落账草稿（2026-10-14 并行治理批——M6 收口欠账之一）

| 字段 | 值 |
| --- | --- |
| 文档代号 | FINDINGS-TRANSCRIPTION-DRAFT-20261014 |
| 性质 | **草稿（提案）**——本文件只提供可直接并入 `traceability/findings.json` 的 JSON 片段与对照表；**findings.json 本体零改动**，落账须经所有者/治理批裁决后执行 |
| 依据 | ACC v1.2 §5（验收建议级问题必须逐条转登 findings.json，只写进验收记录"留档"不构成闭环）；M6 收口欠账清单（并行治理任务卡） |
| 盘点基线 | origin/redesign-main @ afd57b0f（2026-10-14 fetch 后） |
| 编号规则 | 自现有最大号 **F-833** 顺延，新条目 **F-834~F-874**（共 41 条） |

---

## 1. 盘点方法（命令留痕——全部只读）

1. 全量扫描验收记录中的转登声明：

   ```bash
   grep -l "转登" traceability/acceptance/*.md          # 293 份提及
   grep -c "随记录转登" traceability/acceptance/*.md    # 14 份含该字面
   ```

2. 对每份提及转登的记录，检查 findings.json（820 条，最大号 F-833，open 502/fixed 304/closed 14）中是否存在指向该记录的条目（文件名与任务名两种形态均查）：

   ```bash
   grep -o "<记录名>" traceability/findings.json | wc -l
   ```

3. 零命中记录逐份人工读其建议级清单原文，区分三类：(a) 真欠账（声明转登且无对应条目）；(b) 已由后续治理批以描述式 source 落账（如 `acc/ui-t68/1@4c6d2b2b 验收建议级 G-C` 形态）；(c) 明示不转登（记录原文声明"无新增建议级"/"不转登"）。

## 2. 甄别结论总表

### 2.1 真欠账（16 份记录，41 条，本草稿编号 F-834~F-874）

| 记录 | 建议级条数 | 记录内预占编号 | 本草稿编号段 | 落库提交 |
| --- | --- | --- | --- | --- |
| traceability/acceptance/rul-tok-20261010.md | 2 | — | F-846~F-847 | 127e0b49 |
| traceability/acceptance/asm-panel-20261010.md | 2 | — | F-843~F-844 | c9fc453a |
| traceability/acceptance/asm-studio-20261010.md | 2（1 条与 F-251 同源，不另立号） | — | F-845（另 1 条见 §4 去重） | 48fb1c0c |
| traceability/acceptance/gov-find-20261010.md | 2 | — | F-848~F-849 | 11f3c783 |
| traceability/acceptance/wp-16-t07-20261011.md | 3 | F-717~F-719（**已被占用**，见 §4.1） | F-835~F-837 | 8a6ce828 |
| traceability/acceptance/wp-18-t05-20261011.md | 3 | 顺延（未指定号） | F-838~F-840 | 3fea94f2 |
| traceability/acceptance/wp-19-t12-20261011.md | 2 | F-717/F-718（**已被占用**，见 §4.1） | F-841~F-842 | 325eca33 |
| traceability/acceptance/WP-21-T02-20261013.md | 2 | — | F-850~F-851 | c42c405a |
| traceability/acceptance/WP-21-T03-20261013.md | 4（G-1 为 T02 遗留补登载体，不另立号） | — | F-852~F-854 | afd57b0f |
| traceability/acceptance/WP-15-T14-20261013.md | 1 | F-834（未被占用，沿用） | F-834 | 5f3ad834 |
| traceability/acceptance/WP-23-T01-20261013.md | 3 | — | F-855~F-857 | 25abce74 |
| traceability/acceptance/WP-23-T02-20261013.md | 6 | — | F-858~F-863 | a76fd7b8 |
| traceability/acceptance/WP-23-T03-20261013.md | 3 | — | F-864~F-866 | 24ee6aad |
| traceability/acceptance/WP-23-T04-20261013.md | 4 | — | F-867~F-870 | 020cb14a |
| traceability/acceptance/WP-23-T05-20261013.md | 4 | — | F-871~F-874 | 9366e470 |

（表中记录路径省略共同前缀 `RobWork/doc/industrial-robot-design/`；JSON 片段的 source 字段内使用全路径。）

### 2.2 已核销/明示不转登（甄别排除，无需落账）

| 记录 | 排除理由 |
| --- | --- |
| traceability/acceptance/wp-16-t08-20261011.md | 建议级一节明文"无（无需转登 findings.json 的新建议级发现）" |
| traceability/acceptance/DIAG-T03-20260914.md | 明文"无新增建议级问题需要转登（编号顺延自 F-102 起，未动用）" |
| traceability/acceptance/DOC-T03-20260910.md | G-1 明文"不转登 findings（一次性过程瑕疵，收尾即消除）" |
| traceability/acceptance/gate-toolchain-param-20261010.md | 已落账＝F-629（open 在册，grep 亲验） |
| wp-21-t03 合并提交 9649e5fd 信息所称"T02 G-1/G-2 遗留缺口补登" | **信息与实物不符**：findings.json 全文零处提及 WP-21-T02/T03（grep 亲验）；afd57b0f（T03 收尾）明文"本批 findings.json 零触碰——誊登义务随治理批执行"。T02 G-1/G-2 实际仍未落账，由本草稿 F-850/F-851 承载 |

### 2.3 已由后续治理批落账（抽查实证，无需重复登记）

UI 波次与早期审计记录（UI-T11/T20/T21/T22/T24/T27/T33/T35/T43/T44/T46/T47/T48/T49/T50/T52/T53/T54/T55/T57/T63/T64/T65/T66/T67/T68/T71/T72/T74、WP-13-T02/T21、WP-14-T10/T11、WP-15-T18、WP-24-T08/T09、audit-p0-p1d）在 findings.json 中均有引用（grep 计数 UI-T46=9、UI-T53=22、UI-T64=11、UI-T74=9、audit-p0-p1d=8 等），落账形态为描述式 source（如 `acc/ui-t68/1@4c6d2b2b 验收建议级 G-C`、`F-474~F-477`＝UI-T46/UI-T51 attempt 1）。**注意**：本批只做了"记录名有引用"的抽查核对，未逐条核对每份记录的每条建议级项与 findings 条目一一对应；如需全量闭环核对，建议另立专项（工作量约 40 份记录×2~6 条）。

## 3. 转登 JSON 片段（可直接并入 findings.json 的 findings 数组尾部）

> 字段形态沿用现行册内近期条目（F-629/F-717~F-719/F-829~833 同款）：id/source/severity/status/registeredAt/title/owner（+resolution）。registeredAt 以落账执行日为准，本草稿统一写草稿日 2026-10-14，落账批可按实际执行日改写。

### 3.1 WP-15-T14（1 条）

```json
{
  "id": "F-834",
  "source": "traceability/acceptance/WP-15-T14-20261013.md §9/§13（acc/WP-15-T14/1，验收日期 2026-10-13，verdict=pass；记录提交 5f3ad834）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-15-T14 验收建议级：units/kinematics.md 卡头\"日期\"行停留 2026-10-12（v0.16），版本行已 v0.20（2026-10-13）——v0.17~v0.20 四跳未落日期行，与 F-122/F-133/F-134/F-140 家族（版本半边落地、日期半边连续跳）同型，kinematics 卡新例；纯文档内部不一致，validate-docs 不校验该维度",
  "owner": "kinematics 卡所有者（后续触及该卡的批次顺手补齐）＋validate-docs 家族校验维度诉求沿 open"
}
```

### 3.2 wp-16-t07（3 条——记录预占 F-717~F-719 已被 wp-16-t11/t12 批占用，按 §4.1 重编）

```json
{
  "id": "F-835",
  "source": "traceability/acceptance/wp-16-t07-20261011.md §14 第 1 条（记录内预占编号 F-717——该号已被 wp-16-t11 批占用，落账重编为 F-835；验收日期 2026-10-11，verdict=pass；记录提交 8a6ce828）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-16-T07 验收建议级：units/trajectory.md §17.1 测试文件名登记失准——登记写 test/SmoothRecheckTest.cpp（20 用例），实际落位为 test/SmoothTest.cpp（10）＋test/RecheckTest.cpp（10）两文件（SmoothRecheckTest 是 gtest 套件名非文件名）；用例数 20 正确、追溯链不断；契约测试文件头注 RecheckContractTest.cpp:7 同句亦然；建议下批改两文件实名",
  "owner": "trajectory 单元卡所有者（随下次卡增量修订更正）"
},
{
  "id": "F-836",
  "source": "traceability/acceptance/wp-16-t07-20261011.md §14 第 2 条（记录内预占编号 F-718——已被占用，落账重编为 F-836；验收日期 2026-10-11，verdict=pass；记录提交 8a6ce828）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-16-T07 验收建议级：平滑失败素材 reasonToken 借用 TRJ-LIMIT-EXCEEDED（Smooth.cpp:654）——该码卡面语义为\"速度/加速度/限位超标\"，平滑几何保持偏差超容差非限位超标；TRJ-* 12 码词表无平滑专用码、卡面亦未规定；实现复用现码＋cause 文案自描述＋比较型三要素齐备；建议后续需求变更时增补 TRJ-SMOOTH-TOLERANCE 类专用码或在 §14.4 行 5 语义注中扩展登记",
  "owner": "trajectory 需求/卡面所有者（随下次需求变更或卡面修订裁定）"
},
{
  "id": "F-837",
  "source": "traceability/acceptance/wp-16-t07-20261011.md §14 第 3 条（记录内预占编号 F-719——已被占用，落账重编为 F-837；验收日期 2026-10-11，verdict=pass；记录提交 8a6ce828）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-16-T07 验收建议级：预算耗尽比较型素材单位硬编码 rad（Recheck.cpp:447,459-464）——actual.unit 恒取 rad、expected 恒取 maxJointStepRevolute；当最严不满足轴为移动轴（m）或细分由笛卡尔界驱动时 comparison 的单位/对照对象与实测失配（分字段账目本身正确、cause 文案双值并列含单位；测试仅锁定关节驱动场景）；建议按逐层最严轴的 axisKind 取单位 token、expected 按驱动界取对应步长字段",
  "owner": "trajectory 实现侧（随下一 trajectory 契约顺手修复）"
}
```

### 3.3 wp-18-t05（3 条）

```json
{
  "id": "F-838",
  "source": "traceability/acceptance/wp-18-t05-20261011.md 问题清单第 1 条（验收日期 2026-10-11，verdict=pass；记录提交 3fea94f2）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-18-T05 验收建议级：条件数阈值 1×10⁸ 三处同值靠人工纪律同步——P-RT-7/P-DT-2 阈值现存三处定义（runtime DescriptionValidator.cpp:55 私有、modeling CouplingMath.hpp:62、drivetrain MappingCore.hpp:136 公共头），编译边不可达故无法单源 include，单元卡已登记\"裁决变更时三处同步\"纪律；O-11 裁决落地时应收敛为单一来源（policy 条目或共享公共头），消除人工同步风险",
  "owner": "需求所有者（O-11 裁决）＋drivetrain/policy 侧"
},
{
  "id": "F-839",
  "source": "traceability/acceptance/wp-18-t05-20261011.md 问题清单第 2 条（验收日期 2026-10-11，verdict=pass；记录提交 3fea94f2）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-18-T05 验收建议级：R2 负矩阵元通道缺显式正命题用例——P-DT-5 登记负传动比经 R2 矩阵通道表达；现有 R2 黄金用例矩阵元全为正，负元仅由\"§7.2 检查表无正性项\"隐式可达；建议后续任务补一条含负元素耦合窗口的黄金用例（符号逐轴保留＋虚功恒等），把该通道钉进回归面",
  "owner": "drivetrain 后续契约"
},
{
  "id": "F-840",
  "source": "traceability/acceptance/wp-18-t05-20261011.md 问题清单第 3 条（验收日期 2026-10-11，verdict=pass；记录提交 3fea94f2）",
  "severity": "suggestion",
  "status": "fixed",
  "registeredAt": "2026-10-14",
  "title": "WP-18-T05 验收建议级：主仓库 traceability/builds/ui-t74/smoke-tour/ 下冒烟导览三件在途残留（与本任务链无关的工作树残留，建议流水线收尾环节统一处置）——登记时已消账：治理批 2026-10-12 提交 dcdb9958 将三件补录入库（本工作树 ls 亲验九件在位），与 F-718 resolution 同一事实",
  "owner": "已消账（流水线编排者责任面已兑现）",
  "resolution": "随治理批 2026-10-12 [governance] ui-t74 冒烟留痕补录提交（dcdb9958）入库——落账时以 fixed 登记并引用该提交（F-717/F-718 先例同型：登记即销案）。",
  "fixedAt": "2026-10-12",
  "fixedBy": "治理批 2026-10-12（dcdb9958）"
}
```

### 3.4 wp-19-t12（2 条——记录预占 F-717/F-718 已被占用，按 §4.1 重编）

```json
{
  "id": "F-841",
  "source": "traceability/acceptance/wp-19-t12-20261011.md 建议级第 1 条（记录内预占编号 F-717——该号已被 wp-16-t11 批占用，落账重编为 F-841；验收日期 2026-10-11，verdict=pass；记录提交 325eca33）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-19-T12 验收建议级：SelLinearDriveContract 用例数登记文案\"5 用例\"与实际 TEST 数 6 不一致——execution-log.md §3、单元卡 §1.2/§19.5/§21、commit 正文均写 5；实际 contract_test/LinearDriveContractTest.cpp 含 6 个 TEST（T12TokenVocabularyAndDiagCodes 与 LinearDriveKindVocabularyClosed 为两个独立 TEST），XML 实数 64＝基线 58＋6（留痕 XML 准确，仅文字叙述少计 1）；处置：登记文案随下次单元卡/留痕修订更正为 6",
  "owner": "selection 单元卡/留痕所有者（随下次修订更正数字）"
},
{
  "id": "F-842",
  "source": "traceability/acceptance/wp-19-t12-20261011.md 建议级第 2 条（记录内预占编号 F-718——已被占用，落账重编为 F-842；验收日期 2026-10-11，verdict=pass；记录提交 325eca33）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-19-T12 验收建议级：_test 运行数登记\"282 运行＝281 通过＋1 SKIP\"与 gtest 实际输出/留痕 XML（tests=283＝282 passed＋1 skipped）差 1——execution-log §1/§3 与单元卡 §21 两处；单元卡 §21 同行\"T11 既有 277＋新增 6\"（＝283）自洽面反证 282/281 为笔误；验收者复现 283/282/1 与 XML 一致；处置：随下次留痕修订更正数字",
  "owner": "selection 留痕所有者（随下次修订更正数字）"
}
```

### 3.5 asm-panel（2 条）

```json
{
  "id": "F-843",
  "source": "traceability/acceptance/asm-panel-20261010.md §9 S-1（验收日期 2026-10-10，verdict=pass；记录提交 c9fc453a）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "asm-panel 验收建议级：execution-log §4 表与 commit 信息的 skip 口径\"dynamics/selection/optimization_test 各 3\"与 gtest XML 权威数据（各 1、三域合计 3）不符——用例总数/失败数全部一致（tests=105/277/164，failures=0），测试结论不受影响，属留痕数字口径不精确",
  "owner": "asm-panel 留痕所有者（随下次留痕修订更正口径）"
},
{
  "id": "F-844",
  "source": "traceability/acceptance/asm-panel-20261010.md §9 S-2（验收日期 2026-10-10，verdict=pass；记录提交 c9fc453a）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "asm-panel 验收建议级：双变异 M1/M2 做红仅 execution-log §5 文字描述，logs/ 无原始失败输出文件（asm-panel-final-run.log 仅为恢复后权威轮绿色输出）；验收者受只读约束未独立做红；建议后续批次做红时将失败输出原样落 logs/",
  "owner": "验收留痕流程所有者（后续做红批补原始输出落盘口径）"
}
```

### 3.6 asm-studio（1 条立号；S-2 与 F-251 同源不立号，见 §4.2）

```json
{
  "id": "F-845",
  "source": "traceability/acceptance/asm-studio-20261010.md §3 S-1（验收日期 2026-10-10，verdict=pass；记录提交 48fb1c0c）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "asm-studio 验收建议级：任务依据将 ird_gates 白名单 :251-255 称为\"目标级表\"，该行域实为 IRD_EXTRA_EDGE_REFS_EDGES（出处登记表）；真正的目标级表 IRD_TARGET_LEVEL_EDGES（:544 起）studio 行不适用该形态（studio 非单元节点，与既有 studio 五行一致且 ird_gates 零命中实证无检出）——不构成偏差；建议后续批次引用行号/表名时对齐白名单实际表名，避免误读为\"studio 应登 IRD_TARGET_LEVEL_EDGES 而未登\"",
  "owner": "studio 装配批文档所有者（后续引用时对齐表名）"
}
```

### 3.7 rul-tok（2 条）

```json
{
  "id": "F-846",
  "source": "traceability/acceptance/rul-tok-20261010.md §十一 S-1（验收日期 2026-10-10，verdict=pass；记录提交 127e0b49）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "rul-tok 验收建议级：P-SEL-3/P-PR-9 销案后源码注释失实残留共 8 处——project/include/sdurws/ird/project/CommandService.hpp:93、project/src/ProjectStoreImpl.cpp:115、project/src/TxEngine.hpp:176、ui/plugin/ExtraDomainAssembly.cpp:82、ui/plugin/ExtraDomainAssembly.hpp:43/76/82/100 仍描述\"待裁决/裁决前口径/预期登记未 Ok\"等销案前状态，与裁决后实际行为不符（零行为影响、ird_gates/测试全绿；ui/plugin 四处系任务范围有意豁免）；按 AGENTS §2.5\"过期注释比没有注释更糟\"，建议随内置元数据命令族落位增量任务/下一 ui 宿主批次一并对齐",
  "owner": "project/ui 宿主批次所有者（随对应增量任务对齐注释）"
},
{
  "id": "F-847",
  "source": "traceability/acceptance/rul-tok-20261010.md §十一 S-2（验收日期 2026-10-10，verdict=pass；记录提交 127e0b49）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "rul-tok 验收建议级（备忘类，零行动义务）：SelCatalogPanelWidget.cpp 回填按钮标题键由编译期字面改为运行期拼接（\"cmd.\"＋kSelBackfillUiCommandId＋\".title\"）——行为等价（两键均无 UiText 注册、同键名回退），与键族派生单一书写点纪律一致；提示后续装配 selection 文案键（cmd.selection.apply-device-backfill.title）时以该派生式为准，勿再引入第三书写点",
  "owner": "selection 文案装配批次（消费时遵守派生式口径；无独立行动义务）",
  "resolution": "登记观察（记录原文\"零行动义务，留档即可\"）"
}
```

### 3.8 gov-find（2 条）

```json
{
  "id": "F-848",
  "source": "traceability/acceptance/gov-find-20261010.md §11 S-1（验收日期 2026-10-10，verdict=pass；记录提交 11f3c783）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "gov-find 验收建议级：F-630~F-713 索引条目 status 一律 fixed、severity 一律 suggestion——对其中 15 条 verdict=fail 的轮次，status=fixed 的字面语义（\"该发现已修复\"）与实际（fail 轮次遗留建议级问题未必已修复）存在张力；resolution 已自洽声明\"本条为轮次索引指针\"，但仅按 status 列筛选的读者可能低估未决建议级存量；建议（不强制）后续治理批考虑为索引条目引入 index（或 pointer）状态值",
  "owner": "findings 台账 schema 所有者（状态词表扩展裁决）"
},
{
  "id": "F-849",
  "source": "traceability/acceptance/gov-find-20261010.md §11 S-2（验收日期 2026-10-10，verdict=pass；记录提交 11f3c783）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "gov-find 验收建议级：execution-log.md\"治理/ui 会话已现存条目（F-558～F-628 等）\"的\"等\"字把 F-603～F-608 归入其中，而该六号经全引用搜索实为从未登记的历史缺口（非任何现存会话条目占用）；跳号决策本身正确，仅归因表述不精确",
  "owner": "execution-log 所有者（随下次修订更正归因表述）"
}
```

### 3.9 WP-21-T02/T03（T02 两条立号；T03 G-1 为 T02 补登载体不立号，见 §4.3；T03 G-2/G-3/G-4 立号）

```json
{
  "id": "F-850",
  "source": "traceability/acceptance/WP-21-T02-20261013.md 第三节 G-1（acc/wp21-t02/1，验收日期 2026-10-13，verdict=PASS；记录提交 c42c405a；WP-21-T03 验收 §G-1 为其补登载体——T02 记录宣称转登实未落账，T03 记录亲验并转述在案）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-21-T02 验收建议级 G-1：units/optimization.md 暂停/继续边界锚（:1104 一带）\"用例 OPT-VER-228~232 逐条对应\"引用范围差一号——四边界之一\"继续不重复统计\"的专用用例实为 OPT-VER-227（\"恢复后不重复统计\"），不在所引区间内（228~232 覆盖其余三边界＋暂停中取消）；语义面齐备，仅引用范围差一号；建议随下一增量修订改为\"OPT-VER-227~232\"（T03 批亲验 :1111 现状仍为 228~232，勘正未顺手执行）",
  "owner": "optimization 单元卡所有者（随 T04/T05 或下一增量修订勘正）"
},
{
  "id": "F-851",
  "source": "traceability/acceptance/WP-21-T02-20261013.md 第三节 G-2（acc/wp21-t02/1，验收日期 2026-10-13，verdict=PASS；记录提交 c42c405a；WP-21-T03 验收 §G-1 为其补登载体）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-21-T02 验收建议级 G-2：units/optimization.md 适用边界条（:1056 一带）将附录 C 表④原文\"不适用：……P-04 未冻结期……\"转写为\"P-04 未冻结槽位对应面（当前＝判定阈值）\"——系部分冻结态下的必要语义适配（\"未冻结期\"作为整期已不存在），冻结规则值零改动、语义收窄为零（当前未冻结面恰为判定阈值槽位）；如实指出供需求所有者复核该转写口径（如需逐字化可随需求变更注）",
  "owner": "需求所有者（转写口径复核）"
},
{
  "id": "F-852",
  "source": "traceability/acceptance/WP-21-T03-20261013.md 第三节 G-2（acc/wp21-t03/1，验收日期 2026-10-13，verdict=PASS；记录提交 afd57b0f）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-21-T03 验收建议级 G-2：实施者证据目录缺 ird-test-report*.json 聚合登记件（WP-21-T02 先例有 ird-test-report-opt-{model,contract}-smoke.json 两件，含逐用例 requirementIds/atIds 表）——gtest XML 本身在位且数字经验收者亲跑双证，缺的是聚合登记形态；建议实施者补生成或由收尾批落库时从 XML 聚合补齐",
  "owner": "WP-21-T03 实施者/收尾批（补聚合登记件或登记豁免）"
},
{
  "id": "F-853",
  "source": "traceability/acceptance/WP-21-T03-20261013.md 第三节 G-3（acc/wp21-t03/1，验收日期 2026-10-13，verdict=PASS；记录提交 afd57b0f）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-21-T03 验收建议级 G-3：单元卡 §13.2 表 OPT-VER-248/253 验证层级列标注\"契约\"，而落位登记注与实际落位均为模型测试目标（JointStrategyTest.cpp 十用例）；OPT-VER-203~208 亦为\"契约\"层级标注、实际以模型面编排断言承载（trj/dyn 各层语义验证仍归各域评估器自身测试——本批只钉编排面）；属卡面层级标注与落位目标的措辞差，非覆盖缺失；建议随 T04/T05 批增量修订统一口径",
  "owner": "optimization 单元卡所有者（随 T04/T05 增量修订统一层级标注口径）"
},
{
  "id": "F-854",
  "source": "traceability/acceptance/WP-21-T03-20261013.md 第三节 G-4（acc/wp21-t03/1，验收日期 2026-10-13，verdict=PASS；记录提交 afd57b0f）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-21-T03 验收建议级 G-4（提醒性登记，防漏）：契约 verify[0] 为集成口径命令（cmake --build build…），本批以冒烟树同目标等价执行（exit=0）；集成口径复测务必随合入批执行——T03 合入批（9649e5fd）信息已声明\"集成复测（optimization 受影响套件）随本合入批执行\"，落账核证该复测留痕在位后可翻 fixed",
  "owner": "WP-21-T03 合入收尾员（核证集成复测留痕后翻牌）"
}
```

### 3.10 WP-23-T01（3 条）

```json
{
  "id": "F-855",
  "source": "traceability/acceptance/WP-23-T01-20261013.md §4 S-1（验收日期 2026-10-13，verdict=pass；记录提交 25abce74）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T01 验收建议级 S-1：scripts/industrialrobot/validate-task.ps1:75-76 ready 必备字段空判使用 @($t.$k).Count -eq 0，字段完全缺失时 @($null).Count==1 逃逸检查（失败注入实验亲证）；同脚本 :113 knownPitfalls 检查已做滤空修正（Where-Object null -ne）而 :76 未做——同文件内防御不一致；修法：:76 前置滤空后判空，或改 PSObject.Properties 存在性判断；本任务产物不受影响（字段实际在档）",
  "owner": "治理脚本所有者（WP-01 侧，随脚本维护批修复）"
},
{
  "id": "F-856",
  "source": "traceability/acceptance/WP-23-T01-20261013.md §4 S-2（验收日期 2026-10-13，verdict=pass；记录提交 25abce74）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T01 验收建议级 S-2：performance-baseline.md v0.1 的 DTB 版本行登记延后至验收合入批（契约 note/提交 Why 段/文档 §7 三处已钉）——合入批必须实际补登 DTB 版本行，遗漏即文档链失实；落账核证 DTB 版本行已在位后可翻 fixed",
  "owner": "WP-23-T01 合入收尾员（核证补登后翻牌）"
},
{
  "id": "F-857",
  "source": "traceability/acceptance/WP-23-T01-20261013.md §4 S-3（验收日期 2026-10-13，verdict=pass；记录提交 25abce74）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T01 验收建议级 S-3：performance-baseline.md:17 将 testkit.md §4.2.1 performance-baseline 类的\"（NFR-PERF 系列，阶段 D）\"转写为\"（NFR-PERF 系列，阶段 C/D）\"——转写与 DTB §2.24 组头（\"C 起 R1、D 起 R2\"）及 REQUIREMENTS 阶段绑定一致、无数值语义变更，但与 testkit 原文字面有差，属上游两文档既有张力的弥合转述；建议 testkit 卡所有者复核该类别表的阶段标注口径",
  "owner": "testkit 详设所有者（阶段标注口径复核）"
}
```

### 3.11 WP-23-T02（6 条）

```json
{
  "id": "F-858",
  "source": "traceability/acceptance/WP-23-T02-20261013.md §3 S1（验收日期 2026-10-13，verdict=pass；记录提交 a76fd7b8）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T02 验收建议级 S1（F-012 家族同型再现）：五提交标题均为名词/单元名开头（\"testkit 新增…\"\"交互响应基准测量目标落位：…\"\"B-01 复现命令位回填…\"），非 AGENTS §6.3 动词开头；正文四段齐备、验证与留痕一致，纯风格瑕疵不阻断（先例口径一致）",
  "owner": "提交者（F-012 家族模板补救落地前口径持续漂移——随家族批量处置）"
},
{
  "id": "F-859",
  "source": "traceability/acceptance/WP-23-T02-20261013.md §3 S2（验收日期 2026-10-13，verdict=pass；记录提交 a76fd7b8）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T02 验收建议级 S2：\"视图切换\"操作以显示单位制式批量切换（KIN-12）承载、命令面板 view.displayMode 模型半区（workflow 域）不在测量面——实施者已登记为开放语义点（首测报告 §7.3/manifest equivalenceNotes）；验收复核认为登记如实、达标数值不受该路径量级影响（命令分发量级远低于预算）；语义裁决权归所有者，若改判需补 workflow 侧测量（跨单元测试面直链扩登）",
  "owner": "所有者（B-01 语义边界裁决；改判则补 workflow 侧测量）"
},
{
  "id": "F-860",
  "source": "traceability/acceptance/WP-23-T02-20261013.md §3 S3（验收日期 2026-10-13，verdict=pass；记录提交 a76fd7b8）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T02 验收建议级 S3：无头模型层等价口径（任务口径允许、复核通过）——五操作驱动产品模型本体（StageNavigationModel/SelectionService/ProjectTreeModel/ParamEditModel），Widget 重绘与输入分派在口径外——200 ms 预算的规模敏感部分确在模型层；等价性边界四处如实登记（用例文件头/manifest/首测报告 §4/报告 notes）；集成模式复测挂点（首测报告 §7.1）随合入批兑现",
  "owner": "WP-23-T02 合入收尾员（核证集成复测后翻牌）"
},
{
  "id": "F-861",
  "source": "traceability/acceptance/WP-23-T02-20261013.md §3 S4（验收日期 2026-10-13，verdict=pass；记录提交 a76fd7b8）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T02 验收建议级 S4：标准规模数据的\"100 修订\"维度以语境条数登记承载（kGoldenRevisionCount），未物化为 100 条修订记录——测试注释与首测报告均如实声明\"不虚构权重\"；黄金项目文件数据集（含修订明细）归 WP-23-T05（performance-baseline §6 分工在册）；另 ASSERT_GE(kGoldenRevisionCount, 100) 为编译期常量恒真断言（登记性质），建议改 static_assert 或注释说明（纯风格）",
  "owner": "testkit 基准测试所有者（随 WP-23-T05 数据集落位一并处理）"
},
{
  "id": "F-862",
  "source": "traceability/acceptance/WP-23-T02-20261013.md §3 S5（验收日期 2026-10-13，verdict=pass；记录提交 a76fd7b8）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T02 验收建议级 S5：ird_gates 引擎双态未直跑（实施段与验收段同受限——冒烟树无全量目标面、F-009 worktree 限制）；预期恰增命中集登记册在案（perf_test→execution/testkit 测试面直链＋worker 目标依赖，产品库链接块零变化），LinkageContractTest 机械钉已过；直跑核定随合入批/治理面补做（与申报一致，如实登记）",
  "owner": "WP-23-T02 合入收尾员/治理面（ird_gates 直跑核定后翻牌）"
},
{
  "id": "F-863",
  "source": "traceability/acceptance/WP-23-T02-20261013.md §3 S6（验收日期 2026-10-13，verdict=pass；记录提交 a76fd7b8）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T02 验收建议级 S6：testkit 既有用例 ProcessRunnerOutcome.TimeoutReportsTerminatedByTimeoutAndKillsTree（ProcessRunnerTest.cpp:170，TK-T11 交付面）存在 1 ms 墙钟边界抖动——验收首直跑实测 duration 499 < timeout 500 翻红一次，隔离复跑即绿、ctest 全量与复跑 110/110 全绿（与实施者首轮抖动申报同源互证）；建议治理面为该超时边界断言加容差或登记 flaky（与 F-629 同族面）",
  "owner": "testkit 所有者（超时边界容差或 flaky 登记）"
}
```

### 3.12 WP-23-T03（3 条）

```json
{
  "id": "F-864",
  "source": "traceability/acceptance/WP-23-T03-20261013.md §3 S1（验收日期 2026-10-13，verdict=PASS；记录提交 24ee6aad）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T03 验收建议级 S1：数据集版本引用过期（v1→v2 换版未同步的 3 处字面）——execution/CMakeLists.txt:393 注释、CancelTimingBenchmarkTest.cpp:26 文件头、CancelTimingBenchmarkTest.cpp:889 运行时 notes 字符串（随报告 JSON 落盘，与同报告 datasetVersion:\"2\" 字段自相矛盾）；权威字段均正确，纯注释/注记漂移（AGENTS §2.1\"注释随代码更新\"）；处置：合入批或紧随治理批一并改为 @2（889 行字符串属产物面宜优先）",
  "owner": "execution 基准所有者（随治理批顺手勘正）"
},
{
  "id": "F-865",
  "source": "traceability/acceptance/WP-23-T03-20261013.md §3 S2（验收日期 2026-10-13，verdict=PASS；记录提交 24ee6aad）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T03 验收建议级 S2（F-012 家族同型再现）：4/4 提交标题名词短语开头（\"契约编译放行…\"\"B-02 取消时序基准测量目标落位…\"等）；正文四段齐备、无粉饰，随家族口径登记",
  "owner": "提交者（F-012 家族批量处置）"
},
{
  "id": "F-866",
  "source": "traceability/acceptance/WP-23-T03-20261013.md §3 S3（验收日期 2026-10-13，verdict=PASS；记录提交 24ee6aad）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T03 验收建议级 S3（观察两条，低危留档）：①正常腿预热剔除样本在 settle 组承载的是 d1 值（代码复用同一 sample 对象），因剔除不参与统计，纯语义洁癖；②超时腿 evidenceNote 累计串逐场景整体重打印（stderr 日志噪声；JSON 内仅一份完整串）——均不影响判定面，随 S1 批次顺手处理或留档",
  "owner": "execution 基准所有者（随 F-864 批次顺手处理或维持留档）"
}
```

### 3.13 WP-23-T04（4 条）

```json
{
  "id": "F-867",
  "source": "traceability/acceptance/WP-23-T04-20261013.md §建议级 S-1（验收日期 2026-10-13，verdict=PASS；记录提交 020cb14a）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T04 验收建议级 S-1（预优化证据链缺口）：b03-preopt-direct-run.log（13:32）记录的是一次失败的探针草稿（腿 B/C 因夹具身份长度 66/65 解析异常 0 ms 即断），而 perf-reports-preopt/b03-sampleresults.json（13:38，P95 9,011.87 ms）来自其后另一次成功探针运行——该测量运行自身的直跑日志未保留；报告 §8 将该日志笼统登记为\"预优化探针直跑留痕\"易误读为测量运行本身；且 preopt 两 JSON 的 gitCommit 锚口径不一（preopt-c0d3 类自造标记 vs 空）；数据真实性不受影响（JSON 原始样本完整自洽＋验收者 O(N²) 独立复现 8,591.85 ms 同量级）；处置：归集时补注或换留真实测量运行日志",
  "owner": "WP-23-T04 归集批（补注或换留真实日志）"
},
{
  "id": "F-868",
  "source": "traceability/acceptance/WP-23-T04-20261013.md §建议级 S-2（验收日期 2026-10-13，verdict=PASS；记录提交 020cb14a）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T04 验收建议级 S-2（F-012 家族同型再现）：4 条提交标题中 3 条名词短语开头（\"契约编译放行…\"\"区域覆盖载荷编码对齐回查…\"\"B-03 首测达标落库…\"），不合 ACC §4.9 动词开头字面（历次均判建议级）",
  "owner": "提交者（F-012 家族批量处置）"
},
{
  "id": "F-869",
  "source": "traceability/acceptance/WP-23-T04-20261013.md §建议级 S-3（验收日期 2026-10-13，verdict=PASS；记录提交 020cb14a）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T04 验收建议级 S-3（契约 verify ③ 路径口径）：本卡 verify 第 3 条 ctest 路径写作 <冒烟构建目录>/RobWorkStudio/src/rwslibs/industrialrobot（集成树布局）；冒烟树 ctest 注册在单元叶子目录（kinematics/、ui/），字面路径不存在；验收以同 -R 集合分叶目录亲跑全绿等价覆盖；处置：随治理批将该路径改为叶子目录口径（T02/T03 卡先例）",
  "owner": "契约/治理批（verify 路径改叶子目录口径）"
},
{
  "id": "F-870",
  "source": "traceability/acceptance/WP-23-T04-20261013.md §建议级 S-4（验收日期 2026-10-13，verdict=PASS；记录提交 020cb14a）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T04 验收建议级 S-4（ird-test-report.json 归集缺口）：实施者未拷入 _m5-evidence，但其冒烟树在位亲查（smoke/ui/ird-test-report.json：total 3 passed 3，LargeDataBenchmark 三用例 requirementIds=[\"NFR-PERF-03\"]/atIds=[\"B-03\"] 追溯字段在位）——留痕真实，仅归集缺口；验收者亲跑已独立产出同面留痕",
  "owner": "实施证据归集流程（后续卡验收前置清单补该件）"
}
```

### 3.14 WP-23-T05（4 条）

```json
{
  "id": "F-871",
  "source": "traceability/acceptance/WP-23-T05-20261013.md §建议级 S-1（验收日期 2026-10-13，verdict=PASS；记录提交 9366e470）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T05 验收建议级 S-1（测试强度）：黄金项目规模恒等式\"每修订 10 对象＝1,000 数据对象\"缺用例级独立断言——生成器构造（buildBatchPayload）与处理器断言（GoldenAppendHandler::prepare）共享 kObjectsPerRevision 常量，变异 10→12 全绿不翻红；磁盘 oidDirs 打点仅 fprintf 不 assert；建议对 oidDirs（1,101）或对象库实际对象计数加独立断言钉（与常量解耦的字面量或 manifest 读回核对），防生成器常量漂移静默突破 PM-16 规模（证据：4.5 变异 B 实测记录）",
  "owner": "project 基准测试所有者（补独立断言钉）"
},
{
  "id": "F-872",
  "source": "traceability/acceptance/WP-23-T05-20261013.md §建议级 S-2（验收日期 2026-10-13，verdict=PASS；记录提交 9366e470）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T05 验收建议级 S-2（留痕归集，T04 S-4 同型）：ird-test-report.json 未拷入实施证据目录 _m5-evidence/（验收者在其冒烟树亲查在位：total 2 passed 2 failed 0；requirementIds=[\"PM-16\"]/atIds=[\"B-04\"] 追溯字段在位）——留痕真实仅归集缺口",
  "owner": "实施证据归集流程（后续卡验收前置清单补该件）"
},
{
  "id": "F-873",
  "source": "traceability/acceptance/WP-23-T05-20261013.md §建议级 S-3（验收日期 2026-10-13，verdict=PASS；记录提交 9366e470）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T05 验收建议级 S-3（可移植性）：OpenBenchWorkerMain.cpp:138 与 GoldenProjectBenchmarkTest.cpp 使用 std::filesystem 未显式 #include <filesystem>（经 StoreTypes.hpp/Fixture.hpp 传递引入；MSVC 2022 编译零错误，标准库头传递依赖非契约保证）",
  "owner": "project 目标所有者（随下次触及补显式 include）"
},
{
  "id": "F-874",
  "source": "traceability/acceptance/WP-23-T05-20261013.md §建议级 S-4（验收日期 2026-10-13，verdict=PASS；记录提交 9366e470）",
  "severity": "suggestion",
  "status": "open",
  "registeredAt": "2026-10-14",
  "title": "WP-23-T05 验收建议级 S-4（warning）：GoldenProjectBenchmarkTest.cpp:638 预打开块丢弃 requestClose() 的 [[nodiscard]] 返回值触发 C4834（返回值 pending 数为有意丢弃——建议 (void) 修饰消警，与本文件 :577 关闭排空面的处理风格对齐）",
  "owner": "project 基准测试所有者（(void) 修饰消警）"
}
```

## 4. 去重与编号冲突警示（落账前必读）

### 4.1 记录内预占编号已被占用——禁止按记录原编号落账

wp-16-t07（预占 F-717~F-719）与 wp-19-t12（预占 F-717/F-718）的编号声明，已被 2026-10-12 治理批以 wp-16-t11/t12 的条目实际占用（F-717/F-718＝wp-16-t11、F-719＝wp-16-t12，grep findings.json 亲验）。**若按记录原编号并入将造成双占冲突**——一律按本草稿 F-834 段顺延重编，并在各条 source 字段内注明重编缘由（片段已写入）。

### 4.2 asm-studio S-2 不立新号——与 F-251 同源

asm-studio-20261010.md §3 S-2（ctest 注册树列表为 0 的既有缺陷）与 **F-251**（框架顶层至 RobWorkStudio/src 各级 CTestTestfile.cmake 缺失致 ctest 恒"No tests were found"，status=open 在册）同一根因；记录原文亦自认"既有缺陷……保持 open，归属原登记责任方"。落账时**不新增条目**，仅在 F-251 的 resolution/note 追加一行来源引用（asm-studio-20261010.md §3 S-2 复现确认，2026-10-10）。

### 4.3 WP-21-T03 G-1 不立新号——为 T02 G-1/G-2 的补登载体

WP-21-T03 记录 §G-1 的内容＝WP-21-T02 G-1/G-2 未实际转登的遗留缺口转述（T03 记录亲验 findings.json 零命中后在案转述）。若把 T02-G-1、T02-G-2、T03-G-1 三条全部立号将构成同一事实三重登记。本草稿处置：T02 两条立号（F-850/F-851），T03-G-1 不立号、在 F-850/F-851 的 source 字段内注明补登载体链（片段已写入）。

### 4.4 合入批已声明兑现项的翻牌纪律

F-854（T03 集成复测）、F-856（T01 DTB 版本行补登）、F-860（T02 集成复测挂点）、F-862（T02 ird_gates 直跑核定）四条的兑现责任在各自合入批，且合入提交信息已声明执行。本草稿按记录原文以 open 登记；**落账批应先核证对应留痕在位，在位则直接以 fixed 登记（fixedAt＝核证日，fixedBy＝核证命令与证据路径），不得未经核证直接翻牌，也不得无据保留 open。**

## 5. 来源对照表（记录声明 ↔ 本草稿编号）

| 记录 | 记录内声明 | 草稿编号 |
| --- | --- | --- |
| WP-15-T14-20261013.md §9/§13 | F-834 | F-834 |
| wp-16-t07-20261011.md §14 第 1/2/3 条 | F-717/F-718/F-719 | F-835/F-836/F-837 |
| wp-18-t05-20261011.md 问题清单 1/2/3 | （未指定号） | F-838/F-839/F-840 |
| wp-19-t12-20261011.md 建议级 1/2 | F-717/F-718 | F-841/F-842 |
| asm-panel-20261010.md §9 S-1/S-2 | （治理批转登） | F-843/F-844 |
| asm-studio-20261010.md §3 S-1/S-2 | （协议 §5 转登） | F-845／（S-2 归并 F-251，不立号） |
| rul-tok-20261010.md §十一 S-1/S-2 | （随记录转登建议） | F-846/F-847 |
| gov-find-20261010.md §11 S-1/S-2 | （建议级 2 条） | F-848/F-849 |
| WP-21-T02-20261013.md 第三节 G-1/G-2 | （随记录转登） | F-850/F-851 |
| WP-21-T03-20261013.md 第三节 G-1/G-2/G-3/G-4 | （编号顺延自 F-833） | （G-1 由 F-850/F-851 承载）/F-852/F-853/F-854 |
| WP-23-T01-20261013.md §4 S-1/S-2/S-3 | （须转登） | F-855/F-856/F-857 |
| WP-23-T02-20261013.md §3 S1~S6 | （转登动作归合入批） | F-858~F-863 |
| WP-23-T03-20261013.md §3 S1~S3 | （按 ACC §5 转登） | F-864~F-866 |
| WP-23-T04-20261013.md §建议级 S-1~S-4 | （随本记录转登） | F-867~F-870 |
| WP-23-T05-20261013.md §建议级 S-1~S-4 | （待治理面转登） | F-871~F-874 |

## 6. 落账顺序建议

1. **先落零争议直登条目**：F-834~F-837、F-841~F-847（登记文案/注释残留/纯文档失实类，无兑现依赖）。
2. **再落含 fixed 初判条目**：F-840（ui-t74 残留已由 dcdb9958 消账，登记即 fixed）。
3. **后落兑现核证条目**：F-848/F-849（schema/留痕口径建议）、F-850~F-874（含 §4.4 四条核证后翻牌项）——落账批对每条核证"合入批声明已兑现"的留痕在位性。
4. **同步动作**：F-251 追加 asm-studio S-2 来源引用行（§4.2）。
5. **注意**：落账提交单独成笔（`[governance] findings 转登欠账补登 F-834~F-874`口径），与任何实现/验收批分离；并入后跑 validate-docs 并抽查 JSON 可解析（ConvertFrom-Json）；各条 source 字段内的记录提交 SHA 已亲验（git log -1 -- <记录路径>），落账时勿改动。
