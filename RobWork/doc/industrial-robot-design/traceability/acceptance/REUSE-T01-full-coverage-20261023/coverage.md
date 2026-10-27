# REUSE-T01 全项目覆盖独立验收

冻结提交：`b83fa7a3fc8ff233e56736adabd53f11c914dbad`。结论：**NOT PASS，仅全功能覆盖退出条件**。

548条关系尚未证明全部20单元及基础构建/测试/脚本已逐功能审计。下列20组是实读确认的具体遗漏、不是仅凭计数推断；其中18个文件路径不在原16样本锚列表。已登记文件内部也有未覆盖行为。原16项只是下限，不能将其逐项补齐当作全T01完成。

本次新建独立TEMP clone，只检出此SHA，排除父树未提交B15；初末checkout干净。报告只写TEMP，不修改矩阵/产品/上游/vcpkg，不提交、推送、合并或派代理，不授T02准入。

## 验收依据与方法

REUSE-T01 acceptance[0]要求“全部20单元及基础构建/测试/脚本按真实功能登记”；audit-contract §2明确覆盖既有adapter、计算核心、测试辅助、黄金生成器、inline/template/macro、可执行脚本、页面代码及CMake，样板只能按明确语义规则成组；§4要求语义功能的实际区段/调用链/结果消费。当前task仍是implementationState=audit-in-progress；不能把机器门禁或输入文件摘要当覆盖结项。

本次解析全部functions/*.json的sources和auditedSections构建反向区段索引，逐一关联固定1401库存，并用git ls-files补查现行产品/治理代码范围。先以扩展名/头内函数体/template作候选分流，再实际阅读风险优先定义及关键步骤，逐项反查全部矩阵区段。没有来源引用只是候选，实读出的行为才列具体遗漏；inputs仅有摘要绑定，不算功能sources。

## 系统清点

- 548关系、547个不同functionId；545 pending、3 proposal。共有138个基线文件出现功能sources/auditedSections，1263个文件没有；这不是语义完成率。
- 按原库存role：已引用82 production、36 test、20 build；未引用794 production、466 test、3 build。role是库存原值，部分gui/perf/生成器被分到production，不能据此当实际部署身份。
- 1263候选中，1074个有实现/脚本/构建特征，188个暂作声明/DTO头候选，另1个是静态用户手册HTML（未见script，不作为可执行算法遗漏）。头分类仅启发式，188不能全部豁免。
- 未引用包含443个.hpp，其中255个检测到函数体或template；786个.cpp。29个.mjs全无sources。另有19个现行治理.ps1不在固定1401库存，均无功能sources，单列范围候选而不改库存。
- 22个文件只有inputs引用，例如Compare.hpp、ChannelProtocolTest.cpp、ProcessRunnerTest.cpp、PersistenceFormat.hpp；摘要新鲜不证明其中函数已审。

以下“无sources”是候选文件数；“实现候选/声明候选”为分流线索，不是确证缺口数，不能相加当功能总数或百分比。完整逐文件名单见candidate-files.tsv/inventory-analysis.json。

| 单元 | 库存 | 关系 | 有sources文件 | 无sources候选 | 实现/脚本候选 | 声明/DTO候选 | 仅inputs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| core | 30 | 4 | 4 | 26 | 24 | 2 | 1 |
| diagnostics | 38 | 4 | 3 | 35 | 32 | 3 | 0 |
| drivetrain | 22 | 13 | 5 | 17 | 14 | 3 | 0 |
| dynamics | 61 | 12 | 8 | 53 | 38 | 15 | 1 |
| evidence | 37 | 27 | 7 | 30 | 30 | 0 | 0 |
| execution | 60 | 41 | 6 | 54 | 47 | 7 | 3 |
| foundation-governance | 33 | 0 | 0 | 33 | 32 | 0 | 0 |
| io | 37 | 15 | 5 | 32 | 26 | 6 | 0 |
| kinematics | 104 | 37 | 14 | 90 | 77 | 13 | 1 |
| modeling | 138 | 48 | 13 | 125 | 107 | 18 | 3 |
| optimization | 76 | 20 | 7 | 69 | 55 | 14 | 2 |
| policy | 43 | 10 | 5 | 38 | 34 | 4 | 0 |
| project | 87 | 72 | 4 | 83 | 65 | 18 | 3 |
| reporting | 66 | 68 | 5 | 61 | 57 | 4 | 3 |
| requirements | 92 | 15 | 8 | 84 | 74 | 10 | 0 |
| runtime | 57 | 39 | 8 | 49 | 38 | 11 | 0 |
| selection | 71 | 19 | 6 | 65 | 57 | 8 | 0 |
| testkit | 46 | 44 | 6 | 40 | 36 | 4 | 2 |
| trajectory | 81 | 37 | 9 | 72 | 59 | 13 | 1 |
| ui | 162 | 13 | 9 | 153 | 122 | 31 | 0 |
| workflow | 60 | 10 | 6 | 54 | 50 | 4 | 2 |

## 前20组风险优先具体遗漏

按存储/权限/身份状态及数值测试可信度优先列出；不是对全库所有风险的严格排序。下列每项路径均无任何功能sources或auditedSections，且已实读定义和关键行为；sources/index反查和原文摘要见coverage-gaps.json，每项完整冻结区段见FCOV-xx.frozen.txt。表内路径前缀统一为 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`，全限定符号完整记录在机读文件中。

| 缺口 | 精确文件/起始行 | 定义符号 | 实际未登记行为 |
| --- | --- | --- | --- |
| FCOV-01 | `project/src/TxEngine.cpp:287` | `sdurws::ird::project::tx::TxEngine::commit` | 校验计划后发布不可变对象、切换HEAD并发送事件；恢复扫描区分闭包内损坏与闭包外残留。仅ObjectStore发布/读取两条不能覆盖事务编排。 |
| FCOV-02 | `project/src/CommandServiceImpl.cpp:169` | `sdurws::ird::project::CommandServiceImpl::submit` | 互斥执行槽和在途票据，S1形式校验、S2基线，executePlan执行确认/编译/事务并归类失败。Codec登记不覆盖该状态与写权威行为。 |
| FCOV-03 | `project/src/QueryPortImpl.cpp:347` | `sdurws::ird::project::QueryPortImpl::branchHistory` | 在writer锁内取权威tip，锁外沿不可变parent链组装RevisionView，悬挂parent拒绝；不是纯字段访问器。 |
| FCOV-04 | `project/src/ArchiveServiceImpl.cpp:853` | `sdurws::ird::project::ArchiveServiceImpl::finalize` | 复核五元组/写权威，重算manifest摘要，互斥仲裁finalize与abandon，幂等/冲突与文件核验。 |
| FCOV-05 | `project/src/win32/AtomicFile.cpp:226` | `sdurws::ird::project::win32::AtomicFile::writeThrough` | RAII句柄、分块写、flush和close失败路径决定是否允许发布；publishNew/replaceExisting承载不同原子语义，不等于普通文件工具。 |
| FCOV-06 | `policy/src/PolicyParsing.cpp:489` | `sdurws::ird::policy::(anonymous namespace)::runParsePipeline` | schema拒绝、角色词表/对象解析、阈值合法性、默认供给、规则规范化与冲突判定；已有ByteReader/Writer条目不是此语义。 |
| FCOV-07 | `policy/src/JointLimits.cpp:266` | `sdurws::ird::policy::JointLimitEvaluator::evaluate` | 有限性/名称检查、规范排序、有限/连续关节分支，有符号margin与比例、Should/Must和不适用记录；直接影响可行性判定。 |
| FCOV-08 | `policy/src/CollisionEvaluator.cpp:508` | `sdurws::ird::policy::(anonymous namespace)::expandRulesAndCheckConflicts` | 将role/group/object规则展开成对象对，拒绝必检与排除交集；同文件会话构造还决定域/相邻过滤/必检优先，不由CollisionQuery::evaluate已登记区段覆盖。 |
| FCOV-09 | `execution/worker/main.cpp:613` | `::wWinMain` | 命令读/心跳线程、Hello握手、分帧派发、物化摘要重算、临时目录及线程收尾；当前runScript为替身评估器，也需审进程/文件边界，不能误称已机器人计算迁移。 |
| FCOV-10 | `execution/src/Admission.cpp:581` | `sdurws::ird::execution::ResultAdmission::runArchiveTransaction` | 复用预留会话、begin失权重取并重绑写面、有限退避、writeBatch/finalize与失败阶段记账；ChannelProtocol编解码不覆盖归档状态。 |
| FCOV-11 | `execution/src/Controller.cpp:686` | `sdurws::ird::execution::TaskController::forceKillSequence` | 一次终止保护、运行超时/心跳失联区分、因果诊断顺序与Canceling到Failed转换；同文件poll/requestCancel等未由scheduler submit审计替代。 |
| FCOV-12 | `diagnostics/src/Confirmable.cpp:498` | `sdurws::ird::diagnostics::ConfirmableService::submitConfirmation` | 互斥状态迁移、惰性过期、callbackToken一次性检查、binding重验和失效/确认凭据写入，是身份/放行边界，不是文案DTO。 |
| FCOV-13 | `diagnostics/src/Redaction.cpp:580` | `sdurws::ird::diagnostics::RedactionService::redact` | 实际执行规则链、累计统计，任何内部失败返回降级文本而非原文；同文件路径清理/UTF8截断/凭据规则需要语义审查，LoggingPipeline登记不能涵盖。 |
| FCOV-14 | `testkit/src/Dataset.cpp:197` | `sdurws::ird::testkit::GoldenDataset::load` | manifest/schema/版本/单位/容差/edgeCases与完整性清单校验，逐文件size和SHA256检查；决定黄金对照是否可信，不能靠JsonLite骨架审计代替。 |
| FCOV-15 | `testkit/include/sdurws/ird/testkit/SetCheck.hpp:163` | `sdurws::ird::testkit::checkSetEquivalent<T>` | 身份重复、身份匹配/逐字段容差、Kuhn增广路径数值一一匹配及稳定序检查；真实模板算法，不能作为简单DTO或三行断言样板跳过。 |
| FCOV-16 | `core/src/Compare.cpp:18` | `sdurws::ird::core::closeWithin` | C4非有限拒绝/相对绝对阈值与逐元素比较；关联Compare.hpp::Tolerance::make内联合法性判定和C7默认转写，决定大量测试/生产数值结论。 |
| FCOV-17 | `cmake/ird_gates.cmake:152` | `ird_classify_target` | 目标形态分类后，顶层IRD_EDGES扫描应用自边/测试/工具/白名单和Qt规则。20个ird_add_gtest只登记测试注册宏，不覆盖这些门禁或主CMake接线。 |
| FCOV-18 | `testdata/golden/rt-fk-equation/1.0.0/generate/make_fk_equation.mjs:53` | `<ES module>::closedFormFk` | 自行累计平面3R姿态/关节原点/轴，写expected/fk.json；需审适用域/offset/单位/独立性及实际消费，不因生成脚本或独立oracle身份而豁免。 |
| FCOV-19 | `testdata/golden/dyn-two-link-analytic/1.0.0/generate/make_dyn_two_link_golden.mjs:77` | `<ES module>::rneaReference` | 闭式二连杆质量/重力/科氏/惯性/摩擦分项；同模块trapezoid/mapCase计算功率能量、包络与身份，必须明确参考实现独立性及适用域。 |
| FCOV-20 | `testdata/golden/pol-collision-analytic/1.0.0/generate/make_pol_collision_analytic.mjs:41` | `<ES module>::handDerived` | 手算轴对齐盒距离/接触/碰撞，含直立/倒挂与独立自检，决定测试oracle边界；只审RobWork生产碰撞适配不能覆盖脚本。 |

这些是覆盖缺口，不是产品bug或“必须重写”的裁决。后续应沿真实调用链核上游、权限、差异、消费者并登记或有依据成组；本报告不替实施者填决策。尤其黄金参考公式应核其独立性/适用域和消费，不能盲改为与被测实现同源。

## 不能把有sources视为文件完成

已实读以下三个同文件反例，声明区段与行为定义无相交：

| 文件 | 已登记范围 | 未覆盖定义 |
| --- | --- | --- |
| `project/src/ObjectStore.cpp` | `project.ObjectStore.publishObject 179–322`；`project.ObjectStore.readVerified 328–389` | `sdurws::ird::project::objstore::ObjectStore::scanObjects 441–591`；双向引用/磁盘扫描和可选摘要校验；同文件两项已审区段不覆盖 |
| `execution/src/WorkerSupervisor.cpp` | `execution.WorkerSupervisor.spawnNewWorker 264–344` | `sdurws::ird::execution::WorkerSupervisor::handleHandshake 999–1053`；协议/manifest拒绝、进程终结及握手后派发；spawnNewWorker区段不覆盖 |
| `execution/src/Scheduler.cpp` | `execution.TaskScheduler.submit 163–237` | `sdurws::ird::execution::TaskScheduler::dispatchOne 538–645`；并发预算/优先级/FIFO/单项目额度/内联派发；submit区段不覆盖 |

另有测试fixture helper `optimization/test/ParetoTest.cpp:78 makeCandidate`；头内 `core/include/sdurws/ird/core/Compare.hpp:57 Tolerance::make` 和 `testkit/include/sdurws/ird/testkit/SetCheck.hpp:122 detail::tryAugmentAt`，以及TxEngine::scanForRecovery:871，均有冻结原文与零区段重叠证据，见supplemental-gaps.json。模板最大匹配、数值护栏和事务恢复不是简单访问器。

## 可以成组的边界

已读示例：core/Evaluation.hpp中的四类枚举及纯声明，可按冻结词表/接口合同成组；policy/Contexts.hpp的纯虚注入接口可按所有权/生命周期合同成组；core/DiagData.hpp的ComparativeValue/ComparativeFields相等比较是简单值转发，可按同语义分组。它们仍需要明确范围与证据，不代表整个头、toToken实现、DiagnosticRecord::make校验或实际适配器已审。不能把所有.hpp或188个声明候选按文件名统一结项。

## 29个黄金生成mjs与基础脚本

29个模块全部无sources；以下列出具名function入口以免继续按少数样本推进。“顶层生成”表示未匹配具名function，仍有模块顶层数据变换/写出，不能当作无行为。完整路径、行号及摘要在mjs-function-index.json；词法索引没有覆盖箭头函数/所有嵌套语义，只有上述3个oracle做了风险优先实读，其余仍为候选未审清单。

| 数据集 | 具名入口（最多列6个，完整索引另存） | sources |
| --- | --- | ---: |
| dt-mapping-golden | `trapezoid:43`、`mapCase:52` | 0 |
| dyn-two-link-analytic | `conditionIdHex:57`、`trapezoid:63`、`rneaReference:77`、`statValue:106`、`mapCase:119`、`mergeEnvelopeReference:213` | 0 |
| ev-slice-fixture | `idBytes:48`、`hexToBytes:56`、`bytesToHex:63`、`sha256Hex:67`、`appendU16:72`、`appendU32:77` 等14项 | 0 |
| kin-coverage | `gridCenters:78` | 0 |
| kin-fk-analytic | `rt:46`、`mul:62`、`refFk:81`、`jacobiEigen:130`、`singularValues:162`、`planar2rInverse:206` | 0 |
| kin-ik-dedup | `threeLinkBranches:46`、`forward:62`、`margin:70` | 0 |
| kin-search-outcomes | `fk:53`、`threeLinkBranches:65` | 0 |
| mdl-dh-equivalence | 顶层生成 | 0 |
| mdl-not-expressible | 顶层生成 | 0 |
| mdl-template-6r | 顶层生成 | 0 |
| mdl-urdf-import | 顶层生成 | 0 |
| opt-adopt-golden | `sha256Hex:115`、`patchCanonicalBytes:119`、`candidateIdHex:141`、`designTableBytes:153`、`diffEntries:173` | 0 |
| opt-lhs-golden | `splitmixNext:135`、`unitFrom:145`、`quantizeHalfEven:150`、`samplingDims:163`、`generateBatch:178`、`patchCanonicalBytes:245` 等7项 | 0 |
| opt-pareto-golden | `aabb:106`、`splitmixNext:210`、`unitFrom:219`、`sha256Hex:223`、`patchCanonicalBytes:227`、`candidateIdHex:250` 等14项 | 0 |
| pol-collision-analytic | `handDerived:41` | 0 |
| pol-policy-codec-fixture | `u32:42`、`u8:49`、`presence:54`、`lpText:59`、`id128:65`、`f64:72` 等21项 | 0 |
| req-partial-pose | `norm:33` | 0 |
| req-payload-events | `projectSourced:40` | 0 |
| req-region-sampling | `planByRegion:68` | 0 |
| req-transport-task | 顶层生成 | 0 |
| rt-fk-equation | `rz:33`、`mulVec:44`、`closedFormFk:53` | 0 |
| rt-namemap-roundtrip | 顶层生成 | 0 |
| sel-backfill-golden | `parallelAxis:78`、`addI:85`、`subI:89`、`synthesize:95` | 0 |
| sel-catalog-golden | `toCsv:165`、`sha256:169`、`editRow:185`、`missingOf:230`、`motorGolden:239`、`gearboxGolden:259` | 0 |
| sel-combo-golden | `mappingFactOf:112`、`interp:156`、`screenAxis:169`、`sortReasons:242`、`checkCombo:255` | 0 |
| sel-linear-golden | `readCsv:15`、`col:28`、`num:49`、`issue:139`、`linearRowsOf:143`、`validateLinearTable:152` 等12项 | 0 |
| sel-screening-golden | `interp:130`、`interpAnalytic:143`、`reason:145`、`gap:150`、`screenMotorCase:156`、`screenGearboxCase:327` 等8项 | 0 |
| trj-at06-recheck-golden | `subdivisionPlan:61`、`fk2R:103` | 0 |
| wf-gate-advice-fixture | `replayEvents:92`、`sortedStale:125`、`evaluateCase:135`、`adviseFor:229`、`mapInvalidation:294` | 0 |

固定库存foundation-governance还有产品根CMakeLists、ird_gates.cmake及其白名单，都无功能来源；20个测试宏的登记不涵盖依赖提取、Qt红线、私有头、权限和主目标接线。除固定库存外，RobWork/scripts/industrialrobot/有19个现行脚本：

`build-context-pack.ps1`、`commit-lint.ps1`、`gate-all.ps1`、`index-evidence.ps1`、`merge-findings-union.ps1`、`pipeline-lock.ps1`、`reuse-contract.ps1`、`run-flake-probe.ps1`、`test-governance-lite.ps1`、`test-pipeline-pipeline.ps1`、`test-reuse-governance.ps1`、`validate-b1-batch.ps1`、`validate-context-pack.ps1`、`validate-docs.ps1`、`validate-reuse.ps1`、`validate-state.ps1`、`validate-task.ps1`、`verify-task-scope.ps1`、`verify-task.ps1`。

这些脚本不因位于产品目录之外就自动排除。已实读reuse-contract.ps1::CheckFunctions:196的绑定/变动源码覆盖逻辑，其中:223要求source必须位于产品前缀；因此当前逐功能字段无法直接按现状接纳外部治理脚本。应先明确T01如何关联其实际功能和已有GOV-T21/22等独验，不能伪造产品路径/目标或把已有治理PASS抹去。这里记录覆盖/表示范围缺口，不实施校验器修改，也不宣称已完整重审19脚本。函数索引见governance-script-index.json。

## 旧16样本是否成为退出边界

不能推断实施者主观上“只做16项”。可观察事实是：B11/12/13/14已在若干旧锚点补充了函数登记，但仍没有全项目覆盖证明。旧报告COV-12测试辅助、COV-13生成器、COV-14构建和COV-15宿主预览的原入口锚点仍为0命中；本次前20还有18个不在旧样本锚列表的文件路径。project 72条关系集中在Codec.cpp而仅4个文件有sources，policy/execution/diagnostics等也仍有状态/权限/流程空白。

旧16逐锚点当前命中数保存在old16-crosswalk.json；“某行命中”只反映区段定位，不证明语义全闭合。特别是JsonParser/Writer类声明头的0命中不自动证明其方法遗漏，本报告没有把这两处当新缺陷。B15未提交的宿主补审不在冻结版本内，不用它改写本次结论。

## 验证、证据与限制

独立冻结现场真实运行：

| 验证 | 结果 |
| --- | --- |
| clone / detached checkout | PASS，exit0 / 0 |
| validate-task -TaskFile REUSE-T01.json | PASS，exit0，task.log |
| validate-reuse -Head b83fa7a… -TaskFile REUSE-T01.json | PASS治理结构，exit0，reuse.log |
| validate-docs | PASS，exit0，docs.log；DeepEvidence未运行 |
| node scan-coverage.mjs | PASS元数据清点执行，exit0；1401文件无缺失 |
| node capture-evidence.mjs | PASS证据提取/反查执行，exit0；20项零来源、补充3项区段不相交 |

三门禁passed=3、failed=0、skipped=0。此计数是门禁，不是覆盖验收；全覆盖判定NOT PASS。没有产品测试passed计数；C++/GUI/原API探测/生成器及脚本正负例均NOT RUN。

1. 这是完整文件元数据清点＋风险优先语义抽查，不是1401文件全部函数的穷尽审计，也没有以正则替代C++/Qt/CMake/PowerShell/JS的完整语法/控制流分析。
2. 无sources只是候选入口，不自动证明漏审；报告20项通过实际行为审读及全部sources/auditedSections反查证实。138个已引用文件也不自动表示完整：仅被声明区段覆盖，其他函数仍可能遗漏。
3. implementation/declaration分类是去注释、扩展名、函数体/template特征的启发式分流，有误报/漏报；188个声明/DTO候选不是188个可免审文件。原inventory role沿用不改，部分perf_test/gui_test或生成器的role并不等于实际运行身份。
4. scope外现行19治理脚本不是宣称19个新文件：它们是不在固定1401库存里的当前可执行文件。逐一列索引供补全范围，不修改库存。当前CheckFunctions只接受产品前缀sources，因此脚本的登记/已有独验关联方式须明确，不能伪造产品路径或目标。
5. 本次不逐条重验548关系的语义正确性/上游适用性，不撤销既有局部PASS。pending/proposal及closedFiles=0不是本次NOT PASS的直接理由；T01审计可包含明确阻塞提案，T06迁移结项另议。
6. 29mjs全部做了文件/具名function/顶层代码索引，3个高风险数值oracle实读列入缺口；其余26个没有逐个证明全部语义，不将索引等同审计完成。独立oracle可以保留独立参考路线，不能为复用把期望值改为调用同一被测生产实现。
7. 未构建/执行产品、GUI、探测、生成器或脚本负例；只运行文档/治理门禁和本TEMP只读清点脚本。DeepEvidence NOT RUN。未同步远程，也不需要拉取。
8. 未提交B15排除；同冻结之外的后续修补不能回填本次结论。T02准入未授予。

报告根：`C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-full-coverage-4128bd11`。主结果coverage.md/coverage.json，具体缺口coverage-gaps.json，系统候选candidate-files.tsv，单元计数unit-counts.tsv，区段反索引source-reference-index.json。报告可归档TEMP根文件，排除repo/。无需为本次覆盖结论重跑C++；尚缺行为应继续按真实功能审计，而非以计数阈值或原16项列表结项。
