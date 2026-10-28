# testkit 第一轮逐单元代码评审

## 1. 分支、评审版本、范围与结论

- 工作树：`C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`。
- 分支：`codex/core-review-fixes`；HEAD：`6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f`。
- 开始与交付前 `git status --short` 均为空。评审提交源码；不存在混入结论的相关未提交版本。
- 实际根、源码区、设计区 AGENTS 已读取；本次遵循所有者明确指定的分支例外。未创建分支、工作树或子智能体，未操作远程、main、历史或其他分支。
- 本轮未修改生产代码、正式测试、正式 testdata、设计契约或治理状态。隔离源码、数据副本、可执行文件、原始日志和本报告均保存在仓库外本报告所在目录。未提交，未自签修复版本独立验收。

**结论：testkit 已有合理的职责分层与有效的常规测试，但当前不能单凭其“全绿”证明黄金验证链可信。** 确认的主要风险是稳定身份被数值回退掩盖、有限性宏重复求值导致假通过、失败详情损坏 JSON，以及容差/数据/lint/复现记录的边界遗漏。现有正式基线121项全部通过，恰好说明这些边界未被现有测试充分证明。

严重性为本报告的修复优先级：P1＝断言可假通过；P2＝格式/数据/复现/运行契约错误；P3＝定位或工具错误分类。R01～R12只是本报告内定位标签，不是新治理台账ID。已存在的 F-015～F-018 直接沿原记录引用。

### 契约及验证链还原

来源标记：需＝REQUIREMENTS 明文；约＝单元/接口约定；实＝实现选择；裁＝仍需所有者裁决。核心依据为 REQUIREMENTS NFR-COR-01/02/03、附录D，ARCHITECTURE §3.1/§3.6/SA-19，以及 units/testkit.md §2～§8。

| 功能 | 业务用途；输入输出/失败契约 | 实现入口 | 直接消费者 | 已有验证与本轮重点 |
| --- | --- | --- | --- | --- |
| 数值/宏 | 需：非对称C4、SI、非有限失败；约：逐元素、长度错位、GTest失败 | Check、AssertMacros | 全域黄金测试；宏FINITE当前仅本单元常量用例 | CheckTest、core既有证据；R02/R03 |
| 容差 | 需：附录D类别与固定上限；约：schema/单位/字段缺失拒绝；实：通配路径匹配 | ToleranceProfile | GoldenFixture；各域profile.resolve/entries | ToleranceProfileTest；R04/R05 |
| 集合/顺序 | 需：身份精确、集合等价与稳定排序；约：身份阶段后仅无身份元素数值一一匹配；实：Boost最大匹配 | SetCheck、SetCheckMatch | kinematics、optimization、policy、ReproBenchmark | T20穷举及真消费记录；R01/R11 |
| 受限JSON | 约：类型化DOM、拒绝清单、确定性数字往返；非完整通用JSON要求 | JsonLite | manifest、profile、repro、原生报告 | JsonLiteTest、T01候选证据；R03/R06/R07 |
| 数据/路径 | 约：schema/来源/版本、版本目录相对路径、全部消费文件完整性，失败阻断装载 | Dataset、TestPaths、testdata_lint | GoldenFixture、各域黄金测试、lint CLI | Dataset/TestPaths/FixtureTest；F-015～18、R09 |
| 夹具/故障 | 约：RAII专属目录、固定记录、命中次数/顺序；种子记录不等于设置被测算法 | Fixture、GoldenFixture、Fault | project进程测试、各域端口替身 | Fixture/FaultTest；R06；资源异常路径疑点 |
| 契约断言 | 约：通用身份/诊断/文件/谓词检查；业务组合规则由消费者注入 | ContractCheck | evidence、diagnostics、project等测试 | ContractCheckTest、core值类型元测试；未见新增业务权威 |
| 报告 | 约：逐例结果六分列、GTest/XML并存、失败详情可机器读取；裁：进程级失败/重复轮次 | Report、RecordListener、TestMain、lsnchild | 各测试main及归档链 | Report/ListenerOwnershipTest；R03；补充fatal/exception/setup/零命中等 |
| 子进程 | 约：参数UTF8、父环境覆盖、超时/终止/进程树；实：Win32 Job与文件事件观察 | ProcessRunner、prochelper | project ProcessCrash/ProcessLock、listener子进程测试 | ProcessRunnerTest含引号/空格/中文/树回收；R08 |
| 性能/复现 | 需：真实样本、规定分位数与多轮集合/排序；约：steady_clock；裁：预热错误计数 | PerfBenchmark、ReproBenchmark | ui两类性能测试、各域复现测试 | 已知分位数解析值、Repro测试；R06/R12及预热争议 |
| 构建/分发 | 约：本体core依赖、零Qt/gtest；适配头和Qt辅助分离、不入产品链 | CMake、BuildRedLineTest、install-scan | 测试目标与打包检查建议稿 | 真构建/公共头独编/生成链接核对；R10 |

验证链并非空接线：GoldenFixture装载数据/档案失败会预置分类并阻止测试体；Check与SetCheck消费core返回值，宏将失败送GoogleTest并登记详情；listener消费真实GTest逐例状态；原生JSON与XML并存。缺口分别位于身份分流、输入校验、数字表示和工具覆盖。验收仍需检查退出码、实际命中、XML失败节点、JSON解析及来源绑定，不能只看`decisive`或某个汇总数。

## 2. 已确认问题（按严重性）


### R01 [P1] 稳定身份不相等仍可经数值阶段判集合等价

位置：[include/sdurws/ird/testkit/SetCheck.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/SetCheck.hpp:263)。

依据与根因：符号 `checkSetEquivalent`；units/testkit.md §5.4.2阶段2明确限定“无身份键的元素”，§5.4.4/附录D#12要求已有身份精确匹配。当前将所有阶段1未匹配元素送入数值阶段。

最小输入、预期/实际：expected=[{id="a",x=0}]、actual=[{id="b",x=0}]，两边traits均返回身份；预期不等价，实际 equivalent=true、matched=1。`04-Review.IdentityCannotFallBack`用GTest原生EXPECT_FALSE真实失败（exit1）。

业务/消费者影响：可能把数值相同、对象或候选身份不同的机械臂结果认成同一集合。optimization黄金调用、其契约测试、policy findings和ReproBenchmark是直接消费者；无身份IK/GoldenPoint数值集不触发此身份反例。

最小整改与回归：仅无身份元素进入阶段2，已有但未命中的身份保留missing/extra；检查身份同名但数值失配仍失败。保留Boost匹配接缝、core比较、独立穷举oracle。回归不同ID同值、单侧缺身份、同ID漂移、重复ID、无身份贪心陷阱。

证据：[04-Review.IdentityCannotFallBack.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/04-Review.IdentityCannotFallBack.log)；[04-Review.IdentityCannotFallBack.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/04-Review.IdentityCannotFallBack.xml)


### R02 [P1] 有限性宏重复求值，非有限首值可被掩盖

位置：[include/sdurws/ird/testkit/gtest/AssertMacros.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/AssertMacros.hpp:69)。

依据与根因：符号 `IRD_EXPECT_FINITE`；NFR-COR-03与宏有限性断言契约。`isnan(value)||isinf(value)`会求值两次。

最小输入、预期/实际：传入闭包next()：第一次返回+Inf、第二次返回0。预期单次求值并使GoogleTest失败，实际调用2次，GoogleTest/XML/JSON均passed1、exit0。此处是缺陷被错误PASS，不是探针验证成功。

业务/消费者影响：有状态计算表达式可能吞掉首个非法数，误证机械臂计算可用性。当前定向rg仅找到CheckTest的常量1.0调用，未证实外部消费者已触发。

最小整改与回归：宏局部捕获待测值一次，再调用std::isfinite或等价单次判断；保留ADD_FAILURE_AT/GTest失败接线与报告。回归Inf→0、0→Inf、NaN及递增计数表达式，不放宽容差。

证据：[00-Review.FiniteSideEffect.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/00-Review.FiniteSideEffect.log)；[00-Review.FiniteSideEffect.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/00-Review.FiniteSideEffect.xml)；[00-Review.FiniteSideEffect.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/00-Review.FiniteSideEffect.json)


### R03 [P2] 非有限或长度失配详情生成非法原生JSON

位置：[src/Report.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Report.cpp:143)；[src/JsonLite.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/JsonLite.cpp:458)；[src/Check.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Check.cpp:92)。

依据与根因：符号 `TestRecord::toJson`、`dumpNumber`、`checkAllCloseWithin`；units/testkit.md §4.1/§7.2的JSON文本和机器可读失败详情。

最小输入、预期/实际：实际Inf对0；以及actual=[1]、expected=[]。预期失败且报告仍为合法JSON并保留非有限/缺失语义；实际输出小写inf/nan，Python标准json解析器分别报line46 col21与line47 col23。GTest与XML均failure1、exit1。

业务/消费者影响：会丢失数值失稳或集合长度错误的机器追溯；未造成对应GTest假通过。所有采用宏失败详情归档的消费者可能受影响；有限极值差产生Inf也沿同一路径，但该输入未动态运行。

最小整改与回归：在报告边界明确非有限/缺失合法表示，保留状态及原值信息；dump拒绝不能输出JSON的数值。不得默认为0。若报告schema或字段类型变化，须所有者批准兼容方案；回归独立JSON解析＋XML/exit交叉核对。

证据：[01-Review.NonFiniteReport.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/01-Review.NonFiniteReport.json)；[02-Review.LengthReport.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/02-Review.LengthReport.json)；[probe-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/probe-results.json)


### R04 [P2] 装载后的ToleranceEntry.kind保持未初始化状态

位置：[include/sdurws/ird/testkit/ToleranceProfile.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ToleranceProfile.hpp:53)；[src/ToleranceProfile.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ToleranceProfile.cpp:199)。

依据与根因：符号 `ToleranceEntry`、`ToleranceProfile::load`；§4.3条目的quantityKind与unit量纲契约。局部`ToleranceEntry e;`的enum无默认值，解析和互检后从未赋e.kind，复制入entries。

最小输入、预期/实际：任一合法非空档案即可到达；预期条目kind等于声明及unit.kind，实际静态数据流没有赋值。未通过读取未初始化对象制造随机实测；证据为完整构造—赋值—入容器路径审查。

业务/消费者影响：公共条目复制/相等比较或后续量纲消费可读取未定值，不能可靠说明容差物理维度。现有直接消费者主要读取tolerance/unit；未发现外部明确读取entry.kind，现有测试未断言此字段。

最小整改与回归：将已校验的QuantityKind写入e.kind；复用现有core单位/量纲入口，删除重复probe校验若可在原契约内简化。回归所有已注册量纲的装载值、复制和相等，不新增量纲表。

证据：静态完整数据流与接口条款；无动态执行此非法读取/未定义行为。


### R05 [P2] 容差档案schema缺失或未知版本未被拒绝

位置：[src/ToleranceProfile.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ToleranceProfile.cpp:163)。

依据与根因：符号 `ToleranceProfile::load`；units/testkit.md §4.3.1必填schemaVersion，当前直接从profileId开始读取。

最小输入、预期/实际：合法kin-fk副本分别删schemaVersion、改为ird-tolerance-profile/999。预期DatasetInvalid，实际均装载成功；两个EXPECT_THROW失败。

业务/消费者影响：不同格式版本可能按旧解释参与FK/DH等数值对照。实验未改变容差数值，不能据此声称现有阈值已被放宽。所有档案装载消费者属于接口影响面。

最小整改与回归：按现有协议检查必填schema及支持版本，未知字段的前向兼容另依既有规则。回归缺失/类型错/未来版本/正常版本；不修改附录D数值。

证据：[03-Review.SchemaRequired.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/03-Review.SchemaRequired.log)；[03-Review.SchemaRequired.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/03-Review.SchemaRequired.xml)


### R06 [P2] uint64复现种子经double序列化发生静默舍入

位置：[src/Fixture.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Fixture.cpp:87)；[src/Fixture.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Fixture.cpp:115)；[include/sdurws/ird/testkit/ReproBenchmark.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ReproBenchmark.hpp:188)；[src/Report.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Report.cpp:121)。

依据与根因：符号 `ReproRecord::toJson/fromJson`及相关报告序列化；NFR-COR-02、§6.3的uint64种子与跨进程重放。JsonLite的double模型不能覆盖完整uint64，fromJson还先转int64。

最小输入、预期/实际：seed=9007199254740993；预期往返不变，实际写成9007199254740992并按此值读回，原生GTest相等断言失败。uint64上半区越界转换未执行。

业务/消费者影响：机械臂随机选型/优化重放可能使用错误种子。已读testdata中的8处seed最大20260913；现有默认20260909和常见42不受此反例影响，不宣布全部复现记录失效。

最小整改与回归：确定能无损承载uint64的表示并统一Fixture/Repro/Report/Dataset；可评估已批准渠道的成熟JSON整数能力。协议变更须批准，不擅自缩小公共uint64范围。回归2^53±1、INT64_MAX附近、UINT64_MAX及独立解析。

证据：[06-Review.ReproSeedExact.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/06-Review.ReproSeedExact.log)；[06-Review.ReproSeedExact.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/06-Review.ReproSeedExact.xml)


### R07 [P2] JSON小数解析受进程数值locale影响

位置：[src/JsonLite.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/JsonLite.cpp:404)。

依据与根因：符号 `Parser::parseNumber`；JsonLite.hpp确定性JSON及parse(dump(v))契约。std::strtod使用当前LC_NUMERIC。

最小输入、预期/实际：独立子进程设German_Germany.1252，parseJson("1.5")；预期1.5，实际TestKitError“数字解析不完整”，GTest failure1、exit1；所需locale实际可用，未SKIP。

业务/消费者影响：同一黄金数据/档案在某些宿主locale下不可装载，产生假失败与不可复现；未发现本实验静默得到错误数值。默认C locale的本次121基线不受此反例影响。

最小整改与回归：采用locale独立的成熟数字解析原语或获批JSON供给，保留语法/范围/错误定位。避免为了解析临时修改进程全局locale。回归C与逗号小数locale下相同数据及尾随/溢出拒绝。

证据：[08-Review.JsonLocale.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/08-Review.JsonLocale.log)；[08-Review.JsonLocale.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/08-Review.JsonLocale.xml)


### R08 [P2] Windows子进程环境覆盖未按大小写无关变量名合并

位置：[src/ProcessRunner.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ProcessRunner.cpp:201)；[src/ProcessRunner.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ProcessRunner.cpp:225)。

依据与根因：符号 `buildEnvironmentBlock`；ProcessSpec.env的“在父环境之上覆盖”契约。std::map<wstring,...>按大小写区分保留两条Windows同名变量。T49原记录已有此待核风险，本轮提供实证，不把T49既有PASS解释为已证明该边界。

最小输入、预期/实际：父TESTKIT_REVIEW_CASE=parent，spec.env["testkit_review_case"]="override"；实际现有prochelper读取大写键得parent，预期override。仅启动/回收本次helper。

业务/消费者影响：project崩溃/锁测试等使用env的消费者可能在继承同名不同大小写变量时用错目录或配置；已读project当前都用统一大写键，未证实已有验收受影响。

最小整改与回归：按Windows变量名比较规则覆盖并保持Unicode环境块排序/双NUL；保留Win32 Job、参数引用及文件事件机制。回归同/异大小写、PATH继承、Unicode值与现有进程树测试。

证据：[09-Review.ProcessEnvironmentCase.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/09-Review.ProcessEnvironmentCase.log)；[env-case.txt](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/env-case.txt)


### R09 [P2] testdata_lint遗漏容差档案存在性与AT字典校验

位置：[tools/testdata/main.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/testdata/main.cpp:102)。

依据与根因：符号lint主循环；units/testkit.md §4.2.2的toleranceProfile须存在（lint）及coveredAt同需求ID字典校验。当前只load数据集并遍历coveredRequirements。

最小输入、预期/实际：复制单个pol-collision-analytic家族，显式传正常230条ID字典。对照exit0；移走整个tolerance目录仍lint-ok、扫描1违规0；另把coveredAt设[AT-999]同样exit0。

业务/消费者影响：lint可为未能进入数值断言的数据集或错误AT追踪给出假合规。GoldenFixture后续装档案会挡住缺档案，不据lint缺口否定已真实执行的算法断言。全部黄金lint消费路径受此工具契约影响。

最小整改与回归：复用ToleranceProfile::load及现有字典检查AT；字段覆盖语义按域声明处理，不凭空推断expected全部字段。修复后用同副本红转绿验证，缺档/坏AT必须非零；正常样例及环境失败分类仍正确。

证据：[26-lint-normal.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/26-lint-normal.log)；[28-lint-no-profile.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/28-lint-no-profile.log)；[29-lint-bad-at.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/29-lint-bad-at.log)；[lint-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/lint-results.json)


### R10 [P2] 安装扫描漏掉testkit公共头并给出合规结论

位置：[tools/install-scan/ird-install-scan.ps1](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/install-scan/ird-install-scan.ps1:34)；[tools/install-scan/ird-install-scan.ps1](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/install-scan/ird-install-scan.ps1:47)。

依据与根因：符号forbiddenPatterns扫描循环；ARCH/testkit §2.4 T-1与§3.6“不随产品分发”，工具自身PASS文案声称testkit未混入。只按叶名匹配sdurws_ird_testkit，目录testkit和Fixture.hpp均不命中。

最小输入、预期/实际：隔离安装树只有include/sdurws/ird/testkit/Fixture.hpp：pwsh -NoProfile -File实际脚本返回0/PASS；对照clean返回0、含sdurws_ird_testkit.lib返回1。预期公共测试设施存在时拒绝。

业务/消费者影响：后续WP-24若把此建议稿作为分发证明，会漏掉公共测试头。当前未见实际安装产品被污染，脚本仍是建议稿，不称其已接正式门禁。另有P3：缺根目录预期exit2，Write-Error受Stop影响实际exit1。

最小整改与回归：按安装根的相对路径组件检查testkit头与既有排除项，避免无关文件名子串误报；保留只读扫描。明确exit2异常出口。正式门禁接入由构建/打包owner批准，回归本三树、缺根及路径相似合法树。

证据：[24-scan-clean.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/24-scan-clean.log)；[24-scan-library.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/24-scan-library.log)；[24-scan-header.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/24-scan-header.log)；[24-scan-no-such-root.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/24-scan-no-such-root.log)；[tool-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/tool-results.json)


### R12 [P2] 分位数q的NaN未被拒绝，进入无效浮点到整数转换

位置：[include/sdurws/ird/testkit/PerfBenchmark.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/PerfBenchmark.hpp:94)；[include/sdurws/ird/testkit/PerfBenchmark.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/PerfBenchmark.hpp:103)。

依据与根因：符号 `percentileLinearInterpolated`；公开接口q∈[0,1]、越界fail-fast。仅q<0||q>1对NaN均false，随后h为NaN并static_cast<size_t>。

最小输入、预期/实际：samples=[1,2]、q=quiet_NaN；预期out_of_range或契约规定的调用错误；静态路径确定绕过检查，NaN向整数转换超出可表示范围，不能保证返回或异常。未执行这条未定义行为，不声称观察到崩溃。

业务/消费者影响：公共测试统计入口对非法规格无法可靠失败，可能损坏测试进程或索引。现有runInteractionGroup固定q=0.95，未找到该消费者传NaN；不影响本次正常P95基线。

最小整改与回归：转换前拒绝非有限q，复用std::isfinite；保留协议线性插值公式和已知向量oracle。回归NaN/±Inf/q=0/1/正常P95；样本非有限是否同样拒绝另由接口owner明确。

证据：静态完整数据流与接口条款；无动态执行此非法读取/未定义行为。


### R11 [P3] 长度不等时首个顺序错位索引被后端长度边界覆盖

位置：[include/sdurws/ird/testkit/SetCheck.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/SetCheck.hpp:415)；[include/sdurws/ird/testkit/SetCheck.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/SetCheck.hpp:440)。

依据与根因：符号 `checkStableOrder`；§5.4.3“首个错位索引”。长度不同先将sameOrder=false、firstDivergence=n，后续只有sameOrder仍true才改首错位。

最小输入、预期/实际：expected身份[a,b]、actual身份[b]；预期sameOrder=false且firstDivergence=0，实际false且1。

业务/消费者影响：整体顺序判定仍失败，没有假通过；工程师追查候选排序或结果缺项时定位错位。optimization和Repro消费该位置或details。

最小整改与回归：首错位取最早的实际失配与长度边界；保留独立顺序入口，不以集合判定替代。回归等长/异长首部、中部、仅尾部及双空。

证据：[05-Review.FirstDivergence.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/05-Review.FirstDivergence.log)；[05-Review.FirstDivergence.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/05-Review.FirstDivergence.xml)


### 既有问题的本分支复核（沿原记录，不重复建账）

| 原记录/本轮优先级 | 当前位置、契约与触发 | 本轮实际结果/原始证据 | 影响、最小整改与回归 |
| --- | --- | --- | --- |
| F-015 open；P2 | [src/Dataset.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Dataset.cpp:289)，GoldenDataset::load；§4.2.2 parameters可选默认{} | 单集副本删除parameters，lint exit1，提示必填；见30-lint-no-parameters.log | 合法黄金数据假失败；按现约定缺省{}、错误类型仍拒绝；保留现数据与摘要；回归省略/对象/错误类型 |
| F-016 open；P2 | [tools/testdata/main.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/testdata/main.cpp:85)，缺省dict取root.parent_path；正式字典位于root内 | 隔离正常树有root/requirements-ids.json；不传--dict exit2；显式传则exit0；见26/27-lint日志 | 默认工具不可用；复用goldenDataRoot约定定位root/requirements-ids.json，保留显式覆盖与环境错误分类 |
| F-017 open；P2 | [src/Dataset.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Dataset.cpp:457)，完整性覆盖只要求inputs/expected；§4.2.2/§4.5明确包含generate | 从副本清单删生成脚本条目并改脚本，load仍成功；07-Review.ManifestValidation中missing-generator断言失败 | 不能证实期望数据生成来源未变；补清单覆盖，实际登记文件继续用core SHA-256。不能把摘要正确写成数学正确，不能重写expected/摘要洗绿 |
| F-018 open；P2（其整数子项） | [src/Dataset.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Dataset.cpp:367)；seed:uint64、threadCount:int、sizeBytes非负整数 | seed=1.5、threadCount=1.5、实际字节数+0.75均装载成功；07同名三变体断言失败 | 随机/线程复现与完整性元数据被静默截断；转换前校验有限、整数与范围，保留合法格式。超范围转换与stoi异常子项仅静态核对，未运行UB输入 |

原登记的severity字段维持原状，本报告优先级不是治理翻牌。F-017不是已修F-959的重复：T49修的是URDF脚本与已登记摘要漂移，本项是装载器根本未要求脚本完整性覆盖。F-018错误前缀/异常和测试重复include等继续引用原记录。F-763涉及集合大规模歧义成本；本轮以T20当前源码、接受范围和独验为准，不照搬旧算法复杂度结论。F-863超时边界抖动未在本次121基线触发，不新增容差或flaky登记。

### 历史证据影响边界

- R01影响“只靠SetCheck证明身份一致”的语句。已定向读到optimization LHS另有checkStableOrder，Pareto另有逐位CandidateId断言；policy ThreeEntries另有完整结果operator==；ReproBenchmark同时检查集合和顺序。这些附加断言能挡住本反例的整体误绿，不能一概宣布旧验收无效。单独的集合身份声明仍不足。
- R02目前正式调用是常量，未找到有副作用实参的既有消费者；历史触发尚无证据。
- R03只使含非有限/缺失数值详情的原生JSON不合法；应核查这些失败报告及依赖其解析的归档。成功JSON、原始XML和退出码并未因此失效。
- R06只在不能精确表示的种子范围触发；当前已读testdata小种子不受此反例影响。R07取决于LC_NUMERIC。R08取决于继承环境中异大小写同名键。不能无差别重验全部历史数据/进程任务。
- lint与install-scan的PASS只能说明其实际检查范围；它们没有覆盖的数值字段、AT、档案或公共头不能由PASS推导已验证。真实算法GTest和独立生成器证据应分开判断。

## 3. 未证实疑点、契约冲突与尚缺验证

| 项目/位置 | 已知事实和具体前提 | 未执行验证/所需裁决 |
| --- | --- | --- |
| TestRecordStore并发；Report.cpp:277、332及appendComparisons | current()锁内返裸指针、调用者解锁后写。线程A取指针→B endAndTake并销毁记录→A写，会悬空；两线程追加同vector会竞态。头注称不依赖同线程 | 未找到直接异步报告写消费者，未运行竞态/故意UAF。先明确只允许GTest线程还是兑现已宣称并发；后者需把写操作和生命周期纳入同一锁，不能只加外层mutex |
| 进程/套件setup失败；RecordListener只收逐例OnTestEnd | 外部实际listener探针：SetUpTestSuite或GlobalEnvironment fatal后，exit1、XML含额外failure节点，原生JSON仅skipped1/failed0/decisive=true；未把测试计passed，但失败原因未进原生报告 | 16/17 fatal和exception正确failed；18/19结果原件保存。现schema只有逐例六类，是否增加run/suite failure须owner裁决；JSON不能独立替代exit/XML。不是已证明CI会绿灯 |
| repeat/disabled/零命中 | repeat2保留两条相同testId，XML主要反映末轮；disabled/零命中JSON total0、passed0、decisive=true，未伪造passed | iteration/计划用例集不在当前schema。零命中按NOT RUN，decisive只是不含三类不可判定，不是成功。未全审全部验收聚合脚本；已读reuse-contract/flake脚本存在计数/节点检查，不能泛称都可假绿 |
| 预热异常计数；PerfBenchmark.hpp:212 | 预热第一次抛、随后有效采样成功，samples[0].valid=false但invalidCount=0；10探针按“全量无效样本计数”预期失败 | 头注“无效样本数、消费者断言0”与详设“预热剔除统计”边界不清；UI InteractionResponse:902/990、LargeData:1391只查invalidCount。须明确预热失败是否应阻断本轮；本轮不强加性能标准 |
| Dataset路径边界 | versionDir/rel直接拼接，缺明确绝对路径/父目录/链接归属校验；§4.2.2称版本目录内相对路径 | 未运行逃逸/链接探针。需owner明确逻辑路径与解析后路径边界，再补专用副本；不借此扩大生产安全审计 |
| TempDir碰撞/所有权 | 随机目录名含PID、tag、随机后缀；create_directories未以“本次确实新建”为所有权依据，碰撞前提下可能共用/删除 | 未控制随机源复现碰撞；基线多线程隔离通过不等于证明碰撞不可能。仅建议原子独占建目录并重试的最小方案，不引入目录平台 |
| ProcessRunner异常/状态极值 | raw Job持有后环境/分配异常、resume后Impl分配失败路径；自然退出尚未outcome时kill标Killed；极大timeout转换 | 正常启动/超时/树回收/参数用例已通过。未注入bad_alloc或等待极值，未证实资源泄漏；不终止无关进程。仅文件事件日志接口，API没有stdout/stderr捕获管道，不能凭清单虚构“满管道死锁” |
| JSON支持边界 | 非法原始UTF8透传、极小数下溢、公共members可构造重复键、dump深度无护栏 | 受限格式并非完整JSON库；需明确哪些是输入前置条件。没有据名称要求新增平台。locale/非法报告是已复现项，其他未动态验证 |
| 百分位样本/计数极值 | q NaN为R12；样本非有限、minValidSamples乘10溢出等另有接口缺口 | 未运行危险极值或不必要的大分配；当前实际消费者非负steady_clock毫秒、小规格。需明确拒绝行为，不能增加产品性能标准 |
| lint字典健壮性 | tools/testdata/main.cpp:49直接root.find("ids")->items；缺ids可空指针解引用；覆盖字段路径还依域schema | 未运行故意崩溃/不可信字典实验。应在同一lint整改包中用现JsonValue检查必要类型/非空；不新建字典框架 |

文档冲突按当前证据解释：units/testkit.md §7.3仍有“静态listener待修复”历史段，当前堆分配Append与F-958独验及本次子进程证明已修；不再报旧缺陷。README仍称ProcessRunner仅声明、遗漏Perf/Repro，JsonLite头仍称无成熟供给；它们是过期说明。Boost.JSON渠道已有2026-10-26 A4批准，但不等于本单元已完成迁移或所有格式决策已批准。上述项本轮均未改文档/批准状态。

## 4. 架构、上游复用与简洁性评价

本体STATIC库以core为唯一产品单元依赖；GTest仅适配头/测试/helper，Qt6::Core位于单独testkit_qt INTERFACE目标。Boost::headers PRIVATE，Boost头收敛于SetCheckMatch.cpp，没有扩散进公共模板。生成工程及定向源码扫描未见产品源码包含testkit、产品库/真实worker反向链接testkit；测试worker/工具应按其实际用途区分。testkit自身及工业机械臂根CMake没有install命令；这不是对未来完整安装包的实测验收。

ContractCheck使用core公共值类型/注入谓词，未发现直接依赖evidence业务私有头或重建产品状态机。Fault留在测试端口替身，GoldenFixture未接管产品事务。DeterministicEnv是记录容器；实际seed/threadCount必须由被测路径消费者设置，记录本身不证明生效。ReproBenchmark实际执行多腿、调用SetCheck和稳定顺序入口，超出“只记种子”层面；身份缺口仍按R01修复。

### 候选检索及语义边界

| 当前能力 | 已核具体上游/供给 | 语义限制与判断 |
| --- | --- | --- |
| C4数值断言 | core Compare.hpp closeWithin/allCloseWithin；RobWork rw/math/MetricUtil.hpp norm1/norm2、Q.hpp相等；RobWorkSim rwsimlibs/test/EngineTest.hpp epsilon检查 | 范数/相等不是value/reference非对称逐元素C4。保留core唯一比较入口；未发现testkit另写判定公式，详情差值不参与判定。不能改成GTest EXPECT_NEAR或上游epsilon以求复用 |
| 一一匹配 | SetCheckMatch.cpp公开Boost.Graph matching/greedy_matching/edmonds_augmenting_path_finder/maximum_cardinality_matching_verifier/matching_size；RobWork/Sim相关检索未定位同契约集合断言 | 保留已批薄适配，返回mate真实影响判定。T20真返回值扰动红转绿及穷举证据可复用；不得恢复旧Kuhn。身份/顺序/歧义属于测试业务规则 |
| JSON | rw/core/DOMParser.cpp:10/27的make/Factory默认BoostXMLParser，仅XML；rw/core/StringUtil.hpp；T01 probe-r8的Boost.PropertyTree公开JSON | XML或丢标量类型/空容器区别的ptree不能直接替换受限类型DOM。不得反向链接io。已批准vcpkg Boost.JSON渠道可评估薄profile，但先捕获本单元格式/字节基线及失败行为，再单元任务实施与独验；本轮零迁移 |
| 摘要 | core Digest.hpp ContentDigester（Dataset/ContractCheck调用）；RobWork/Sim SHA检索及现GAP-DIGEST-01依据 | 不在testkit重复SHA。文件摘要证明字节，不证明expected数学独立性；不把本评审解释为摘要替换批准 |
| 时钟/统计 | rw/common/TimerUtil.cpp:57（Windows clock，其他平台gettimeofday）；rw/math/Statistics.hpp mean/median | 不等于steady_clock单调墙钟及协议P95线性插值。保留std::chrono和指定公式，不为复用退化语义，也不审查/修复上游统计实现 |
| 进程 | rw/common/ThreadPool.hpp addWork/stop/waitForEmptyQueue、ThreadTask.hpp、TimerUtil；catalog UTIL-THREAD | 线程任务不提供OS进程树Job终止与Windows环境块；保留最小系统薄适配。无需引入产品execution或Qt进程平台 |
| GTest入口/报告 | RobWork/gtest/test-main.cpp:22、TestEnvironment.cpp:39、仓库ADD_RW_GTEST模式；GTest公开Append/Release与EXPECT/ASSERT | 上游入口自动RobWork::init并读取配置，不能强接所有值类型测试；现自持main+listener可保留。GTest负责结果，testkit只补测试追踪，不成为产品evidence/reporting权威 |

functions/testkit.json共116条均以现schema标为migrated/business-specific；已定向核对sources、search、differences与T49非重叠记录，不能把标签当成每项已独立证明。catalog TEST-HARNESS、UTIL-TEXT、UTIL-THREAD、MATH-STATS所列限制仍适用。T20第三方catalog表达债务及规模性能接受范围继续保留，不在本轮翻牌。

### 可选精简项（不实施）

| 删除什么 | 复用什么/适配边界 | 保留什么 | owner/影响消费者/收益 |
| --- | --- | --- | --- |
| ToleranceProfile量纲probe token循环与重复校验（若赋值后已无用途） | 已有kindFromToken与core UnitToken::kind，沿既有失败消息边界 | 强类型量纲及unit互检、错误字段路径 | testkit；档案消费者；减少漏赋值和双校验维护点 |
| DatasetTest/FixtureTest重复临时树/摘要样板，固定全局临时文件名 | 本单元已有TempDir与core ContentDigester；保留测试被测边界，避免用待测装载器自证期望 | 独立已知摘要/损坏输入断言、失败现场 | testkit测试owner；仅元测试；减少全局路径碰撞和夹具重复，不删除oracle |
| 已无事实用途的历史占位叙述、ProcessRunner“仅声明”等注释 | 当前接口说明与已核T20/T49/F958记录 | 实际限制、来源、批准和不可变历史证据 | testkit文档owner；调用者；减少误接线，零运行行为变化 |
| 后续JSON迁移若成立，删除已被成熟供给替代的词法/Unicode/数字核心 | 获批Boost.JSON公开API＋最小拒绝/键序/错误位置profile | 原公共值模型兼容、黄金格式/报告契约、独立解析验证 | testkit及构建owner；数据/报告全链；减少解析核心维护，不新增通用facade |

范围代码量只作覆盖事实：实现/公共头27文件6723行，正式测试17文件4240行，工具4文件477行，CMake350行，接口README34行。合计50文件11824行。没有以减行数、调用次数或测试全绿作为质量结论；仓库外评审探针/证据体量不计入产品实现。

## 5. 文件/功能覆盖与实际验证

### 构建与证据适用性

核对本工作树build/CMakeCache.txt：`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`，CMAKE_HOME_DIRECTORY指向本工作树/RobWork，CACHEFILE_DIR本树/build，x64。按源码依赖重新构建testkit/core及helper，没有拿旧成品冒充本分支结果。正式完整testkit套件只执行一次；未再执行_report以避免重复。lint只增量构建自己的登记目标。

T20送验9276f19b26bfe095cbe2ccf460a64dec39bf45e9到当前HEAD，SetCheck.hpp/SetCheckMatch.cpp/SetCheckTest.cpp无差异；独验REUSE-T20-20261025-r1为PASS。沿用其512个3×3边图、240个固定种子4×5边图、15625网格、真贪心陷阱与返回值扰动证据；本次基线也实际执行了对应元测试。1024稠密118.73ms对旧3.26ms及O(E)图开销有所有者明确接受，不重测/否定，不外推100000规模或新退化。

T49 REUSE-T49-20261026-r1 PASS对应59d76f8a429231becbd8a61b61053bad1b3a8e80，已核当前脚本/manifest及记录适用面；旧F-959已修，不重做29生成器、不改expected。其结论不覆盖R01～R12或既有F-017等未验证边界。

### 实际命令与结果

```powershell
# 工作目录均为指定仓库；完整输出在本报告目录。
$env:CL='/MP1'
cmake --build build --config Release --target sdurws_ird_testkit_test -- /m:1
& '<repo>/build/RobWorkStudio/bin/Release/sdurws_ird_testkit_test.exe' `
  --gtest_output=xml:<本报告目录>/baseline.xml `
  --ird_report=<本报告目录>/baseline.json
cmake --build build --config Release --target sdurws_ird_testdata_lint -- /m:1
# 外部探针用本树Release库、相同include与vcpkg公开GTest链接；未接入CMake。
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
cl.exe '@<本报告目录>/probe.rsp'
cl.exe '@<本报告目录>/supplement.rsp'
```

上述尖括号仅为阅读简写；run-probes.py、supplement.py、tools-check.py、lint-check.py与各results.json保存实际展开的绝对路径命令、cwd、输入及退出码。probe.rsp和supplement.rsp保存实际编译/链接输入。所有动态探针调用本分支真实入口/公共模板；没有把算法模型实验标成生产接口验证。

| 验证 | PASS | FAIL | SKIP | NOT RUN/解释 |
| --- | --- | --- | --- | --- |
| 本分支正式testkit完整基线 | 121 | 0 | 0 | 0；exit0，XML/JSON/独立解析一致，19.318s |
| 16公共头单独TU /c | 16 | 0 | 0 | 仅本树集成include模式，未另配冒烟 |
| 最初边界探针00～10 | 00被错误计PASS | 01～10各1例失败 | 0 | 00＝缺陷假通过；01/02预期GTest失败且报告解析失败；10预热计数待裁决。FAIL是反例观察，不是环境失败 |
| 现有lsnchild控制样本 | passes1；repeat2累计passed2 | fails1/exit1 | skips1 | 零命中total0按NOT RUN；repeat不是第二次完整基线 |
| 外部真实listener补验16/17/20 | 参数化2例通过 | fatal1、异常1均正确失败 | 0 | 参数化testId唯一，XML/JSON一致 |
| suite/global setup补验18/19 | 0 | 进程各exit1、XML各含failure | 正文各skipped1 | JSON仅skip、未记录上层原因，见§3，不写成passed |
| disabled过滤21 | 0 | 0 | 0 | 选中disabled未启用，实际执行0，NOT RUN |
| 报告写盘 | 正常目录创建22成功 | 25目标是目录，写失败并stderr | 0 | 25测试本身1 PASS/exit0为现设计；缺报告不能当归档成功 |
| lint单集 | 26正常exit0 | 27缺省字典exit2（F016）、30缺parameters exit1（F015） | 0 | 28缺档/29坏AT均错误exit0，R09反例 |
| pwsh安装扫描 | clean0、lib拒绝1符合预期 | header错误0；缺根错误1（约定2） | 0 | 未扫描实际安装产物；只有专用隔离树 |

工具过程错误也保留：首次补充脚本读错tests键/缩进导致驱动错误，修正后仅执行补充场景；不计为产品缺陷。23-*曾误用Windows PowerShell5读取UTF8无BOM脚本并解析失败，属于运行时不适用，随后按脚本声明pwsh重跑为24-*；不拿23-*判断产品扫描。22的“缺父目录”被实现自动创建，是成功写盘而非失败验证，故另以25“目录充当文件”证明真实失败行为。没有删除失败原件。

本轮NOT RUN：全库重配/全库构建、全部消费者测试、GUI套件、全量29生成器、core全单元测试、fresh独立冒烟、真实安装包扫描、危险UB/资源耗尽/竞态探针。未变接口没有具体运行影响需消费者重测；整改实施时再按工作包扩到实际消费者，不省略必要独验。

### 逐文件静态覆盖

以下50项全部阅读其生产/模板/工具/测试逻辑；部分前阶段阅读内容通过SHA256与本分支逐字节相同核验后复用。文件清单source-manifest.json保存绝对路径、行数与SHA256。每个测试入口都核对了注册和断言目的，公共头还另作自包含编译。范围内未读文件：无。定向消费者只读对应符号及邻近断言，不称其他单元已完整评审。

| 文件（本单元内） | 行数 | 覆盖/主要结果 |
| --- | ---: | --- |

| [CMakeLists.txt](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt) | 350 | 本体/测试/工具/Qt隔离及注册 |

| [include/sdurws/ird/testkit/Check.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Check.hpp) | 106 | core委托、方向、非有限/长度详情；R02/R03 |

| [include/sdurws/ird/testkit/ContractCheck.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ContractCheck.hpp) | 126 | 身份/诊断/文件/谓词；未见重复业务权威 |

| [include/sdurws/ird/testkit/Dataset.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Dataset.hpp) | 203 | 字段/完整性/路径；F015/F017/F018 |

| [include/sdurws/ird/testkit/Fault.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Fault.hpp) | 126 | 次数/顺序/值持有，元测试实执行 |

| [include/sdurws/ird/testkit/Fixture.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Fixture.hpp) | 191 | 目录/复现/清理；R06与所有权疑点 |

| [include/sdurws/ird/testkit/gtest/AssertMacros.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/AssertMacros.hpp) | 87 | GTest/单求值；R02 |

| [include/sdurws/ird/testkit/gtest/GoldenFixture.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/GoldenFixture.hpp) | 194 | 失败阻断、来源绑定、TearDown |

| [include/sdurws/ird/testkit/gtest/RecordListener.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp) | 154 | GTest映射/Append所有权；补充子进程 |

| [include/sdurws/ird/testkit/JsonLite.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/JsonLite.hpp) | 127 | 全部解析/转义/输出；R03/R07 |

| [include/sdurws/ird/testkit/PerfBenchmark.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/PerfBenchmark.hpp) | 497 | 时钟/统计/计数/序列化；R12及预热裁决 |

| [include/sdurws/ird/testkit/ProcessRunner.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ProcessRunner.hpp) | 153 | 命令行/环境/Job/等待/kill；R08 |

| [include/sdurws/ird/testkit/README.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/README.md) | 34 | 接口说明；过期声明列维护建议 |

| [include/sdurws/ird/testkit/Report.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Report.hpp) | 251 | 全聚合/序列化/store；R03/R06及并发疑点 |

| [include/sdurws/ird/testkit/ReproBenchmark.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ReproBenchmark.hpp) | 416 | 三腿/集合/顺序/观察值；R06 |

| [include/sdurws/ird/testkit/SetCheck.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/SetCheck.hpp) | 452 | 两阶段/歧义/顺序/护栏；R01/R11 |

| [include/sdurws/ird/testkit/TestPaths.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/TestPaths.hpp) | 180 | 根三态/错误/回退边界 |

| [include/sdurws/ird/testkit/ToleranceProfile.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ToleranceProfile.hpp) | 111 | 格式/量纲/上限/解析；R04/R05 |

| [src/Check.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Check.cpp) | 153 | core委托、方向、非有限/长度详情；R02/R03 |

| [src/ContractCheck.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ContractCheck.cpp) | 183 | 身份/诊断/文件/谓词；未见重复业务权威 |

| [src/Dataset.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Dataset.cpp) | 508 | 字段/完整性/路径；F015/F017/F018 |

| [src/Fault.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Fault.cpp) | 36 | 次数/顺序/值持有，元测试实执行 |

| [src/Fixture.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Fixture.cpp) | 241 | 目录/复现/清理；R06与所有权疑点 |

| [src/JsonLite.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/JsonLite.cpp) | 602 | 全部解析/转义/输出；R03/R07 |

| [src/ProcessRunner.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ProcessRunner.cpp) | 666 | 命令行/环境/Job/等待/kill；R08 |

| [src/Report.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Report.cpp) | 432 | 全聚合/序列化/store；R03/R06及并发疑点 |

| [src/SetCheckMatch.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/SetCheckMatch.cpp) | 95 | Boost策略/验证/索引/基数，保留T20 |

| [src/TestPaths.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/TestPaths.cpp) | 118 | 根三态/错误/回退边界 |

| [src/ToleranceProfile.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/ToleranceProfile.cpp) | 315 | 格式/量纲/上限/解析；R04/R05 |

| [test/BuildRedLineTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/BuildRedLineTest.cpp) | 265 | 真实构建信息与目标红线元测试 |

| [test/CheckTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/CheckTest.cpp) | 131 | 已读全部断言/负例/注册；正式121基线实际执行；core委托、方向、非有限/长度详情；R02/R03 |

| [test/ContractCheckTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ContractCheckTest.cpp) | 200 | 已读全部断言/负例/注册；正式121基线实际执行；身份/诊断/文件/谓词；未见重复业务权威 |

| [test/CoreIntegrationExampleTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/CoreIntegrationExampleTest.cpp) | 51 | 真实core单位/比较；示例非独立数学oracle |

| [test/DatasetTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/DatasetTest.cpp) | 292 | 已读全部断言/负例/注册；正式121基线实际执行；字段/完整性/路径；F015/F017/F018 |

| [test/FaultTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/FaultTest.cpp) | 232 | 已读全部断言/负例/注册；正式121基线实际执行；次数/顺序/值持有，元测试实执行 |

| [test/FixtureTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/FixtureTest.cpp) | 453 | 已读全部断言/负例/注册；正式121基线实际执行；目录/复现/清理；R06与所有权疑点 |

| [test/JsonLiteTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/JsonLiteTest.cpp) | 162 | 已读全部断言/负例/注册；正式121基线实际执行；全部解析/转义/输出；R03/R07 |

| [test/ListenerOwnershipTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ListenerOwnershipTest.cpp) | 238 | 真实子进程pass/fail/skip与所有权 |

| [test/PerfBenchmarkTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/PerfBenchmarkTest.cpp) | 248 | 已读全部断言/负例/注册；正式121基线实际执行；时钟/统计/计数/序列化；R12及预热裁决 |

| [test/ProcessRunnerTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ProcessRunnerTest.cpp) | 436 | 已读全部断言/负例/注册；正式121基线实际执行；命令行/环境/Job/等待/kill；R08 |

| [test/ReportTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ReportTest.cpp) | 253 | 已读全部断言/负例/注册；正式121基线实际执行；全聚合/序列化/store；R03/R06及并发疑点 |

| [test/ReproBenchmarkTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ReproBenchmarkTest.cpp) | 284 | 已读全部断言/负例/注册；正式121基线实际执行；三腿/集合/顺序/观察值；R06 |

| [test/SetCheckTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/SetCheckTest.cpp) | 484 | 已读全部断言/负例/注册；正式121基线实际执行；两阶段/歧义/顺序/护栏；R01/R11 |

| [test/TestMain.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/TestMain.cpp) | 33 | 自持main/参数/listener一次安装 |

| [test/TestPathsTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/TestPathsTest.cpp) | 296 | 已读全部断言/负例/注册；正式121基线实际执行；根三态/错误/回退边界 |

| [test/ToleranceProfileTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ToleranceProfileTest.cpp) | 182 | 已读全部断言/负例/注册；正式121基线实际执行；格式/量纲/上限/解析；R04/R05 |

| [tools/install-scan/ird-install-scan.ps1](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/install-scan/ird-install-scan.ps1) | 61 | 只读扫描及exit；R10 |

| [tools/lsnchild/main.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/lsnchild/main.cpp) | 104 | passed/failed/skipped、模式参数及main；真实补验 |

| [tools/prochelper/main.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/prochelper/main.cpp) | 187 | 参数/文件事件/后代/退出/环境helper；真实消费 |

| [tools/testdata/main.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/testdata/main.cpp) | 125 | lint全循环/字典/exit；R09/F016与隔离单集补验 |


## 6. 可执行整改工作包（仅方案，待所有者另行实施授权）

| 批次/文件符号 | 保留/删除/适配；复用来源 | owner/消费者/接口影响 | 验证与独验 | 批准与回滚边界 |
| --- | --- | --- | --- | --- |
| A 断言可信：SetCheck::checkSetEquivalent/checkStableOrder、AssertMacros::IRD_EXPECT_FINITE；R01/R02/R11 | 保留Boost匹配/core C4/traits/独立oracle；删错误身份回退与重复求值，适配首错位 | testkit owner；optimization/policy/kinematics/Repro；签名可不变，错误判定恢复现契约 | 新边界先红后绿；既有匹配穷举；定向Opt/Policy带身份消费者及Repro；不跑无关GUI；冻结SHA交独立会话验收 | 不需新增算法/容差批准，但本轮不实施；公用断言/数值影响按§6.4独验。整包本地提交可回滚；不恢复Kuhn/旧错误公式 |
| B 档案/数据/lint：ToleranceProfile::load、Dataset::load、tools/testdata；R04/R05/R09、F015～18 | 保留core单位/摘要与正式黄金内容；删重复量纲探测（有证据才删），适配schema/整数/清单覆盖/默认dict/AT检查 | testkit+WP02/各域数据owner；全部黄金消费者；拒绝已非法输入、恢复parameters可选，不改合法范围 | 专用副本覆盖缺档/未来schema/生成脚本/小数整数/坏AT；正常单集；相关profile/fixture元测试。数学expected不因hash通过免审 | 按现契约修复可不新增语义批准；路径/覆盖粒度争议先裁决。只回滚装载/工具与测试提交，正式expected不覆盖，沿原F记录 |
| C JSON/复现/失败报告：JsonLite、Fixture::ReproRecord、Report、ReproBenchmark；R03/R06/R07 | 保留唯一JSON接缝、失败状态、键序/兼容、独立Python解析；用成熟locale独立数字原语；若迁移获批Boost.JSON再删被替代核心 | testkit/构建owner；所有报告、数据和重放消费者；可能涉及number表示/schema/消费者兼容 | 先捕获原字节与错误基线；uint64全界、NaN/Inf/缺项、locale、XML/exit/JSON交叉核对；新供给真返回值消费/断开红转绿 | Boost.JSON渠道已有批准，逐单元实施/格式方案仍需批准与三件套，不以本报告批准。按协议版本/适配包回滚，不将旧损坏报告冒充新成功；独立验收必需 |
| D 子进程：buildEnvironmentBlock；R08（异常/kill/超时疑点并入核查） | 保留Win32 Job/CreateProcess/现有quote及文件事件；只适配Windows环境键比较，不引产品execution/Qt | testkit owner；project ProcessCrash/ProcessLock、listener helper；签名不变 | 现prochelper同/异大小写、Unicode/参数/树回收/超时；具体影响时才运行project进程契约测试；不杀非本次进程 | 系统/跨单元边界修复独验；异常安全需故障点证据后改。回滚独立环境块提交，不改变worker生产实现 |
| E 工具/分发：install-scan；R10 | 保留只读扫描与三排除对象；删叶名不足/无法到达exit2路径，适配相对路径组件 | testkit工具owner＋WP01/WP24；打包链；脚本行为恢复约定，不自行接门禁 | clean/lib/headers/testdata/相似名/缺根正负例，真实安装树由打包方补验 | 正式接门禁需owner安排，不自行改门禁/权限。脚本与接线分提交回滚；独立打包验收 |
| F 性能规格/报告生命周期：PerfBenchmark q、预热计数、ReportStore/Listener run级状态 | q有限检查保留P95公式；预热/并发/进程失败先裁决，禁止新调度/报告平台 | testkit接口owner＋ui性能/归档消费者；R12签名可不变，其他可能改契约 | 解析分位值与非法q；预热抛异常；setup/fatal/exception/repeat/disabled/零命中；若承诺并发再用受控生命周期探针 | 不新增性能阈值。计数/线程/run级schema先批准；独验核对消费者。各独立行为面小提交回滚，不把记录种子当确定性 |

每批按reuse/policy §1.1在已有任务中写清保留/删除/适配、owner与实际影响；实现稳定后只更新本包凭证、矩阵和必要说明，执行相关gates/复用检查，不新建重复总台账。需要公共格式/依赖变更时才扩大到实际对端契约与独立冒烟。实施者不得自签独验；本报告不代替后续修复SHA的独立验收。

## 7. 本分支core修复的影响核查

基点cdd0eae9bdba3d5c7acd049d5ad405f4446f0ceb至本HEAD的core变化集中在Identity随机播种、Events类型访问及订阅RAII、删除Core.cpp空占位、配套CMake/测试/说明。实现提交63e2b6305；6dc1141为其独验登记。Compare/Units/Digest及公共头未被本次core修复改变；testkit全部50文件与先前阅读快照逐字节一致。

testkit实际调用core.closeWithin/allCloseWithin、单位/量纲、ContentDigester及身份/诊断值类型。它没有Events/ReferenceEventBus调用。身份生成用于ContractCheck等元测试构造有效ID，不以随机字节固定值作oracle；新的播种不改变有效性/强类型契约。删除Core.cpp没有被testkit引用的导出符号。

因此core修复不要求重新core全审/全测，不存在有据可见的testkit数值补偿需求。本次按本分支增量重建后的121基线与真实接口探针覆盖了其实际接入；T20的比较/匹配证据和T49数据生成证据因相关源码/输入/依赖面未变继续适用。R01～R12及F015～18属于testkit自身边界，未借机修改已完成core修复，也不将旧主树验收直接冒充本分支测试。

## 8. project下一单元须继承的限制或阻断

1. project评审可继续读取生产源码，无需等待本轮全部整改；本轮不接管project修复。其实现与事务/归档权威仍归project，不放入testkit/core。
2. ProcessCrashContractTest/ProcessLockContractTest依赖ProcessRunner.env和TempDir；评审时记录父环境，特别同名异大小写键；跨进程路径/参数必须保留UTF8及真实helper证明。不得用testkit替身替代真实锁/崩溃事务接线。
3. 不能仅凭SetCheck::equivalent证明带身份结果一致；修复前需同时核对已有精确身份断言。不要为了绕过R01在project再造集合匹配核心。
4. ReproRecord在完整uint64范围不可靠；使用种子超2^53的历史重放证据需定向核查原值与报告，不能擅自改合法范围或默认种子。
5. 原生报告暂不能独立承担进程级成功证明：必须并看exit、XML实际failure/skipped节点、非零命中与新鲜报告。非有限/长度失配报告可能不合法；写盘失败按现约定不改测试退出码，缺报告不得绿灯。
6. 数据/容差使用应遵守附录D和现schema；lint PASS不证明档案存在、AT有效或数学expected正确；F015～18沿原记录整改。不新增project私有容差或EAA批准外推。
7. 只有依赖上述缺口才能成立的自动验收结论暂不能据其单一PASS放行；project自身实际独立断言、XML/日志和真实进程证据可继续使用。没有有据阻断project静态审查或要求重跑全库。

本报告是评审与整改建议，不是后续修复版本的独立验收，不更新任务done或批准状态。


附：[source-manifest.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/source-manifest.json)；[baseline-build.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/baseline-build.log)；[baseline-test.log](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/baseline-test.log)；[baseline.xml](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/baseline.xml)；[baseline.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/baseline.json)；[baseline-crosscheck.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/baseline-crosscheck.json)；[probe.cpp](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/probe.cpp)；[probe-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/probe-results.json)；[supplement-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/supplement-results.json)；[header-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/header-results.json)；[tool-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/tool-results.json)；[lint-results.json](C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/testkit-review-6dc1141/lint-results.json)。

历史依据：[traceability/acceptance/REUSE-T20-20261025-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T20-20261025-r1.json)；[traceability/acceptance/REUSE-T49-20261026-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T49-20261026-r1.json)；[traceability/reuse/REUSE-T05/setcheck-max-matching/README.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/setcheck-max-matching/README.md)；[traceability/reuse/REUSE-T49/README.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T49/README.md)；[traceability/reuse/REUSE-T43/byte-primitives-decision.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T43/byte-primitives-decision.md)；[traceability/findings.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/findings.json)；[reuse/functions/testkit.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/reuse/functions/testkit.json)；[reuse/catalog.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/reuse/catalog.json)；[reuse/policy.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/reuse/policy.md)。

在所列版本、范围和已执行验证内，未发现其他问题。
