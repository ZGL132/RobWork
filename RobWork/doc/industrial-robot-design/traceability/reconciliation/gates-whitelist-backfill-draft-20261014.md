> **对账结论（2026-10-15）：基线已随 WP-01-T05 消除为零命中，回填项失效，本稿留档。**

# ird_gates 白名单回填草稿——WP-23 perf_test/worker 面既有登记命中（2026-10-14 并行治理批——M6 收口欠账之二）

| 字段 | 值 |
| --- | --- |
| 文档代号 | GATES-WHITELIST-BACKFILL-DRAFT-20261014 |
| 性质 | **草稿（提案）**——本文件只给命中清单、逐条判定与建议白名单行；**门禁脚本本体（cmake/ird_gates.cmake / cmake/ird_gates_whitelist.cmake）零改动**，回填须经 WP-01-T03 治理面裁决后执行 |
| 依据 | DTB §4.5（例外登记册——机器消费面在白名单数据文件，回填归 WP-01-T03）；WP-22-T02 返工回填先例（2026-10-06"机器面滞后既有状态"逐条落机器面）；各 WP-23 卡登记册/验收记录（§3 逐行标注） |
| 验证基线 | 本分支工作树（gov-m6-parallel-debts @ 823c7f87，源码面与 origin/redesign-main afd57b0f 一致） |

---

## 1. 现状实测（引擎直跑——只读）

命令（AGENTS/各验收记录同款引擎直跑口径）：

```bash
cmake -DIRD_ROOT=<industrialrobot 源码根> -P cmake/ird_gates.cmake
```

实测（本工作树，2026-10-14）：**命中恰 18 项（SUB 14＋T1 4），exit=1；引擎自测 11 项全过**。与 WP-21-T02/T03/T06 三份验收记录的"恰 18 命中"亲跑账目逐项一致（acceptance/WP-21-T02-20261013.md §4.4 补充、WP-21-T03 §4.x、WP-21-T06 §59）。

命中根因（引擎分类逻辑，ird_gates.cmake :152-167 形态正则）：`_perf_test` 与三段名 `project_openbench_worker` 均不匹配 `^(test|contract_test|gui_test)$`／`^(plugin|worker|app)$` 任何形态 → 目标面为 **other**（unit 解析为空）→ 全部链接边落入形态二单元级白名单检查（unit 级 miss → SUB），testkit 边再独立触发形态三 T-1（`_t_face` 非 test 且不在 `IRD_T1_EXEMPT_TARGETS`）。**这些目标本是被各卡登记为测试面/测量设施的合法形态——纯机器面滞后（WP-22-T02 返工回填同族），非任何新依赖语义。**

## 2. 命中清单与逐条判定（18 条）

| # | 命中码 | 命中 | 归属判定 | 登记出处 |
| --- | --- | --- | --- | --- |
| 1 | SUB | sdurws_ird_execution_perf_test → sdurws_ird_execution | 已登记——新增测试目标链被测产品目标（同单元引用） | traceability/wp23-t03-gate-registrations.md 条目 1 |
| 2 | SUB | sdurws_ird_execution_perf_test → sdurws_ird_testkit | 已登记——T-1 允许形态（PerfBenchmark＋RecordListener） | traceability/wp23-t03-gate-registrations.md 条目 2 |
| 3 | T1 | sdurws_ird_execution_perf_test 链 testkit | 已登记——同上（测试目标允许集） | traceability/wp23-t03-gate-registrations.md 条目 2 |
| 4 | SUB | sdurws_ird_project_perf_test → sdurws_ird_project | 已登记——project 测量目标链被测库 | acceptance/WP-23-T05-20261013.md（§4.5 表变异 C"project 构建图出现白名单外目标引用…sdurws_ird_project_perf_test"实证 LinkageContractTest 白名单钉在案；**该卡无独立 gate-registrations 文件**——形态缺口见 §5.3） |
| 5 | SUB | sdurws_ird_project_perf_test → sdurws_ird_testkit | 已登记——T-1 允许形态 | acceptance/WP-23-T05-20261013.md §4.11⑧"perf_test（测试目标）链 testkit" |
| 6 | T1 | sdurws_ird_project_perf_test 链 testkit | 已登记——同上 | 同上 |
| 7 | SUB | sdurws_ird_project_openbench_worker → sdurws_ird_project | 已登记——openbench worker＝测量设施 exe（零 testkit 链接——手写 JSON 序列化），链 project 为黄金项目装载面 | acceptance/WP-23-T05-20261013.md §4.11⑧＋traceability/benchmarks/wp23-t05-golden-project-baseline.md |
| 8 | SUB | sdurws_ird_ui_perf_test → sdurws_ird_ui | 已登记——自边（目标属 ui 单元测试面；wp23-t02 登记册附加说明"新增链接边全部落在新增测试目标 sdurws_ird_ui_perf_test"隐含；逐字单行登记缺位——回填时按本行补明） | traceability/wp23-t02-gate-registrations.md 附加说明＋acceptance/WP-21-T02-20261013.md §4.4 十八项清单 |
| 9 | SUB | sdurws_ird_ui_perf_test → sdurws_ird_execution | 已登记——后台态负载注入经 execution 公共头（O-31 测试面直链先例） | traceability/wp23-t02-gate-registrations.md 条目 1 |
| 10 | SUB | sdurws_ird_ui_perf_test → sdurws_ird_testkit | 已登记——T-1 允许形态 | traceability/wp23-t02-gate-registrations.md 条目 2 |
| 11 | T1 | sdurws_ird_ui_perf_test 链 testkit | 已登记——同上 | 同上 |
| 12 | SUB | sdurws_ird_ui_largedata_perf_test → sdurws_ird_requirements | 已登记——腿 A RequirementTypes/Codec 公共头 | traceability/wp23-t04-gate-registrations.md 条目 1 |
| 13 | SUB | sdurws_ird_ui_largedata_perf_test → sdurws_ird_kinematics | 已登记——腿 B Sampling/Coverage 公共头 | traceability/wp23-t04-gate-registrations.md 条目 2 |
| 14 | SUB | sdurws_ird_ui_largedata_perf_test → sdurws_ird_optimization | 已登记——腿 C Run/CandidatePatch/Export 公共头 | traceability/wp23-t04-gate-registrations.md 条目 3 |
| 15 | SUB | sdurws_ird_ui_largedata_perf_test → sdurws_ird_project | 已登记——腿 B ProjectStore/ArchivePort/QueryPort 公共头 | traceability/wp23-t04-gate-registrations.md 条目 4 |
| 16 | SUB | sdurws_ird_ui_largedata_perf_test → sdurws_ird_io | 已登记——io Csv/AtomicFile 设施 | traceability/wp23-t04-gate-registrations.md 条目 5 |
| 17 | SUB | sdurws_ird_ui_largedata_perf_test → sdurws_ird_testkit | 已登记——T-1 允许形态 | traceability/wp23-t04-gate-registrations.md 条目 6 |
| 18 | T1 | sdurws_ird_ui_largedata_perf_test 链 testkit | 已登记——同上 | 同上 |

**结论：18/18 全部为 WP-23 性能测量面（四 perf_test 目标＋openbench worker）的已登记既有形态，零新增语义、零产品目标命中**——与 M6 各验收"恰 N 命中全为已登记项零新增"放行口径互证；欠账仅是机器面回填。

## 3. 建议白名单行（数据面回填——零引擎改动）

引擎对 face=other 目标的查表次序（ird_gates.cmake :299-350）：`IRD_TOOL_TARGET_EDGES` → `IRD_TARGET_LEVEL_EDGES` → `IRD_ALLOWED_UNIT_EDGES`；testkit 另经形态三查 `IRD_T1_EXEMPT_TARGETS`（:357-364）。因此 14 条 SUB 全部可经 `IRD_TARGET_LEVEL_EDGES`（目标粒度精确 tgt->lib 匹配）吸收，4 条 T1 经 `IRD_T1_EXEMPT_TARGETS` 吸收。

### 3.1 `ird_gates_whitelist.cmake` → `IRD_TARGET_LEVEL_EDGES` 增 14 行

```cmake
    # ---- WP-23 性能测量面回填（2026-10-14 草稿——DTB §4.5 登记册机器面，
    # ---- 出处＝各卡 gate-registrations/验收记录，见
    # ---- traceability/reconciliation/gates-whitelist-backfill-draft-20261014.md §2；
    # ---- 形态语义：_perf_test/openbench_worker 为测量目标，三段名不匹配
    # ---- 引擎 test/worker 形态正则（ird_gates.cmake :152-167），机器面滞后
    # ---- 回填（WP-22-T02 返工同族），产品目标链接面零变化） ----
    "sdurws_ird_execution_perf_test->sdurws_ird_execution"        # wp23-t03 登记册#1
    "sdurws_ird_execution_perf_test->sdurws_ird_testkit"          # wp23-t03 登记册#2
    "sdurws_ird_project_perf_test->sdurws_ird_project"            # WP-23-T05 验收记录（无独立登记册——§5.3）
    "sdurws_ird_project_perf_test->sdurws_ird_testkit"            # WP-23-T05 验收记录 §4.11⑧
    "sdurws_ird_ui_perf_test->sdurws_ird_ui"                      # wp23-t02 登记册附加说明（自边）
    "sdurws_ird_ui_perf_test->sdurws_ird_execution"               # wp23-t02 登记册#1
    "sdurws_ird_ui_perf_test->sdurws_ird_testkit"                 # wp23-t02 登记册#2
    "sdurws_ird_ui_largedata_perf_test->sdurws_ird_requirements"  # wp23-t04 登记册#1
    "sdurws_ird_ui_largedata_perf_test->sdurws_ird_kinematics"    # wp23-t04 登记册#2
    "sdurws_ird_ui_largedata_perf_test->sdurws_ird_optimization"  # wp23-t04 登记册#3
    "sdurws_ird_ui_largedata_perf_test->sdurws_ird_project"       # wp23-t04 登记册#4
    "sdurws_ird_ui_largedata_perf_test->sdurws_ird_io"            # wp23-t04 登记册#5
    "sdurws_ird_ui_largedata_perf_test->sdurws_ird_testkit"       # wp23-t04 登记册#6
    "sdurws_ird_project_openbench_worker->sdurws_ird_project"     # WP-23-T05 验收记录＋benchmarks 基线文档
```

### 3.2 `ird_gates_whitelist.cmake` → `IRD_T1_EXEMPT_TARGETS` 增 4 行

```cmake
# WP-23 性能测量目标回填（2026-10-14 草稿）：_perf_test 四目标为测试面
# （PerfBenchmark/RecordListener 报告设施——testkit.md §2.4 T-1 允许形态
# "测试目标 → sdurws_ird_testkit"；各卡登记册逐条出处见 §2 表），因引擎
# 形态正则不含 _perf_test 而被误归产品面——目标粒度豁免（testdata_lint
# 先例同款"非产品交付物"豁免语义），不放宽任何真产品目标的 T-1 红线。
set(IRD_T1_EXEMPT_TARGETS "sdurws_ird_testdata_lint"
    "sdurws_ird_execution_perf_test"
    "sdurws_ird_project_perf_test"
    "sdurws_ird_ui_perf_test"
    "sdurws_ird_ui_largedata_perf_test")
```

（或以 `list(APPEND IRD_T1_EXEMPT_TARGETS ...)` 形态追加，由治理面按文件既有风格定。）

### 3.3 回填后预期输出（已实证）

本批以**工作树内脚本副本**（非本体）预演回填：复制两份 cmake 文件至临时目录、追加 §3.1/§3.2 全部行后直跑引擎——

```
[ird_gates] 全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中
（exit=0；引擎自测 11 项全过——pass_clean/pass_r4comment/pass_r4cliff＋
 fail_r1/r3/r4×2/r5/t1/t2/sub 按预期检出）
```

即回填后干净运行零命中、退出 0，IRD-GATE-GRAPH 检查（白名单↔dependency-graph.json 一致性）亦通过（目标级行不参与单元级图比对）。

## 4. 备选方案（引擎一行改动——语义最正，供治理面裁决）

参照 WP-22-T02 返工先例（`_gui_test` 后缀并入 test 形态——那次同为引擎改动回填），将 `ird_gates.cmake :161` 形态正则扩为：

```cmake
elseif(name MATCHES "^sdurws_ird_([a-z]+)_(test|contract_test|gui_test|perf_test)$")
```

效果：四 perf_test 目标归入 test 形态 → 规则①同单元自边（#1/4/8）与规则②testkit 边（#2/5/10/17 及全部 T1）自动放行；仅真实跨单元边 6 条（#9/#12~#16）需入 `IRD_TEST_TARGET_EDGES`（该表语义"测试目标链接面"与本组边完全同构）。openbench_worker（三段名）仍需一行 `IRD_TOOL_TARGET_EDGES`（测量工具语义本贴切）。

**取舍**：备选语义最正、白名单表职责不漂移，但动引擎本体＋需补引擎自测用例（perf_test 形态夹具）——属 WP-01-T03 辖权裁决；主方案（§3）零引擎改动、与 testdata_lint"目标粒度豁免"先例同型，但 `IRD_TARGET_LEVEL_EDGES` 表头注释自称"装配层特权边"，回填时须同步在该表注释补一段"测量目标面"语义扩展说明（否则表注释与数据失配——AGENTS §2.5 注释随代码更新纪律的门禁版）。

## 5. 回填注意事项

1. **两处同步**：DTB §4.5 登记册（人工面）与白名单数据文件（机器面）必须同批回填并互相引用——白名单文件头纪律 2 明文"两处必须一致，以 DTB §4.5 为准"。
2. **登记出处如实性**：#8（ui_perf_test→ui 自边）在 wp23-t02 登记册无逐字单行（仅附加说明隐含）——回填行注释按 §3.1 标注"登记出处＝WP-21-T02 验收 §4.4 十八项清单＋登记册附加说明"，不虚构逐字出处。
3. **WP-23-T05 无独立 gate-registrations 文件**（T02/T03/T04 三卡均有）——属登记形态缺口（其 #4/#5/#6/#7 四条出处只能引验收记录）。建议治理面在回填同时要求后续卡补齐独立登记件形态，或在该卡 DTB §4.5 行内补登并注明"登记面随验收记录"。
4. **回填提交单独成笔**（`[governance] ird_gates 白名单回填——WP-23 perf_test/worker 面 18 命中机器面落账`口径），回填前后各直跑一次引擎留痕（回填前 exit=1 恰 18 命中、回填后 exit=0 零命中），与任何实现/验收批分离。
5. 本草稿验证均在**脚本副本**上实施（工作树 scratch 目录，验后已删）；门禁脚本本体在本分支零改动（diff 亲验仅 reconciliation 新文件）。
