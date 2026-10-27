# project 首轮评审整改实施记录

起点：`412e173b9079fa43a1f0ef50fe07d38e852b1940`，2026-10-28，工作树 `C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`，分支 `codex/core-review-fixes`。用户已要求按上一轮评审修改；本记录只覆盖实施及自查，不代签独立验收。

依据：2026-10-27 外置 project-review/REVIEW.md 的13项确定问题及W1—W8工作包。保留HEAD唯一提交点、core身份/摘要/事件、Win32事务原语及runtime/io/execution/evidence职责。删除错误的提交后失败传播、只读清理、错误SDK数字副本；在现有查询/闭包/编码器边界适配，不增加事务平台或持久化格式。

## 实施范围与整改映射

最小任务契约为 [PRJ-T19](../../tasks/foundation/PRJ-T19.json)，仅用于既有H6凭证和范围核验；不另建总台账。原报告的编号和严重性沿用如下，状态均为“实施及自查完成，待独验”，不代表旧复用债务已关闭。

| 原问题 | 文件/符号与处置 | 主要回归证据 |
| --- | --- | --- |
| 1 P1 提交后误报失败 | TxEngine::reportDev/publishEventWithRetry/commit隔离观察及清理异常；ProjectStoreImpl::executeCommit直接采用已验证候选推进；CommandServiceImpl::executePlan保留真实提交身份 | PostCommitObserversCannotChangeCommitFact；已有F7/事件失败用例 |
| 2 P1 非法token进入历史 | Codec::isCommandTypeToken单源；HandlerRegistry注册校验token/版本；TxEngine落盘前复用parseCommandRecord验证完整命令及inverse | InvalidRegisteredTokenRejectedBeforeCommit、InvalidInverseRejectedBeforeCommit |
| 3 P1 只读删除writer文件 | scanForRecovery默认只读，仅OS锁持有者启动时显式清理；scanOrphanDrafts同权限边界；DraftServiceImpl::tryLoad不删.new | DraftReadPreservesInFlightNewFile；真实子进程ReaderOpenPreservesWriterTemporaryFiles |
| 4 P1 错误完整性判定 | RevisionIndex::committedClosure拒绝父链/谱系非递减及闭包序号重复；恢复要求必需command、目录身份及HEAD序号/分支一致 | RecoveryRejectsMissingCommittedCommand、RecoveryRejectsParentCycleWithValidDigest、RecoveryRejectsHeadSequenceMismatch |
| 5 P1 缺current无法恢复bak | DraftServiceImpl::tryLoad在current缺失且bak存在时继续既有归属验证/回退，不写回 | MissingCurrentRecoversBackupWithoutWriting |
| 6 P1 伪幂等归档 | ArchiveServiceImpl::finalize重算existing自身摘要并在幂等返回前核验工件；校验循环收敛为一个局部lambda | IdempotentFinalizeRejectsTamperedManifest：合法字段篡改、等长工件篡改、还原再试 |
| 7 P2 新HEAD配旧索引 | 扫描返回verifiedHead及正向committedRevisions；loadCommittedState只装载该集合；refreshCommittedViewLocked复用完整打开验证、成功后整体交换唯一视图 | 真实子进程ReaderRefreshesCompleteViewAfterWriterCommit；历史链锁内取值避免刷新置换竞态 |
| 8 P2 暴露未提交对象 | QueryPortImpl::tryObject/object在writer锁内刷新并检查RevisionIndex::referencesObject | QueryHidesUncommittedObject；旧修订metadata对象在HEAD两次前进后仍可读取 |
| 9 P2 保留名大小写绕过 | ArchiveServiceImpl::validateRelPath折叠ASCII大小写，复用原保留名和路径约束 | ArchiveRejectsReservedNamesCaseInsensitively |
| 10 P2 幻影登记残留 | TxEngine::commit守卫提前至首次登记前；先拒绝重复revisionId，避免误删既有登记 | FailedValidationRollsBackRegistration及已有发布故障回归 |
| 11 P2 错误SDK码 | TxEngine/ObjectStore/StoreClone三个映射使用ERROR_DISK_FULL/ERROR_HANDLE_DISK_FULL/ERROR_DISK_QUOTA_EXCEEDED及ERROR_ACCESS_DENIED | SdkDiskFullErrorsRemainDistinctFromWriteRejected；既有TxEngine磁盘满注入改用SDK常量 |
| 12 P2 丢canonical长路径 | createNew组装区从projectDir的parent/filename构造，保留Win32长路径前缀 | CreateNewPreservesCanonicalLongPath |
| 13 P3 原生报告覆盖 | project的ird_add_gtest在CTest和_report均传每exe唯一--ird_report，XML路径保留 | full-unit及full-contract两个原生JSON共存，分别记录304和34项 |

校验后发现的性能问题：同一不可变对象跨历史修订反复出现在扫描输入，ObjectStore会重复读/摘要。首次性能实测热启动P95=8939.726ms（FAIL，不能接受）。在TxEngine收集闭包引用时复用ObjectKey/ObjectKeyHash，仅当次去重并保留首次发现顺序；每个唯一对象仍完整实算。未增加跨扫描缓存，未更改C1免检语义。追加RecoveryChecksSharedHistoricalObjectBytes，证明共享对象等长篡改仍拒开、恢复原字节后历史引用仍有效。该变化后只补测受影响路径，不重复无关全集。去重后热P95独立重算为917.376ms（PASS）；额外冷组样本导出为670.986ms（软指标，边界见下文）。环境采集见environment.txt；热组首次与去重后存在独立构建负载差异，不能将二者比值作为精确加速比，但去重后真实打开满足既有预算。

## 架构、复用与维护边界

定向搜索原文见 [upstream-search.log](upstream-search.log)。命令检索RobWork的rw::core::IOUtil::{readFile,getFilesInFolder,getFileSize}，以及RobWorkSim文件/存储候选；IOUtil提供普通读取/目录工具，不能兑现不可覆盖发布、flush、HEAD提交或修订闭包，不用于替换Win32事务原语。RobWorkSim定向文件族未找到对应事务供给；这不是宣称全框架无其他能力。保留既有AtomicFile/IFileOps/OS锁，不另建文件平台。

- 删除：错误SDK数字副本、tryLoad的删除副作用、提交后额外元数据I/O/解析、过晚守卫窗口、归档重复校验循环，以及扫描的重复对象工作。
- 复用：core::ContentDigester/Identity/DomainEvent、codec::parseCommandRecord/isCommandTypeToken、RevisionIndex::committedClosure、现有loadCommittedState、ObjectKeyHash、Win32原语、testkit::TempDir/TestProcessRunner/EventWatch和既有gtest报告通道。
- 保留：canonical冻结编码、Boost.JSON结构解析、项目唯一写权威、HEAD唯一提交点、结果五元组/原存储上下文、runtime/io注入端口。新闭包快照只是原上下文的候选，成功后替换唯一权威，不形成第二可变缓存平台。
- owner仍为project；直接消费者是execution归档、ui/workflow命令/草稿、io桥和evidence证据解释。产品未引入testkit/Qt、未跨其他单元私有头，未改公共签名、持久化字段、权限或门禁。
- 产品变更与测试/凭证增量分开观察；主要新增是回归与真实子进程设施，凭证大量变化是源区段重锚及输入摘要更新，不能用减行数或migrated计数证明正确性。

公共CommandResult的optional newHeadState沿用原有防御性缺省形态：提交后视图I/O失败时保留Committed和newRevision，通过开发诊断表明附属视图缺席，调用方应重查而非盲目重提。未将此边界解释为“未提交”，也未扩大到任意内存耗尽保证。HEAD切换后自校验失败仍是既有极端硬错误/提交结果需核实，不伪装为普通提交前失败。

## 待裁决及保留范围

C1 ObjectStore逐key免检语义、C2 GAP-PRJ-DIAG-BOUNDARY-01正式诊断归属、C3 listRuns完整性强度、C4真实policy内容身份，均不由本轮自行裁决。包/固化生产接线及未验证并发疑点不冒充已修复。可选Codec精简、空锚点清理不与可靠保存修复捆绑。

## 验证与结果

环境：C盘指定工作树；build/CMakeCache.txt的CMAKE_HOME_DIRECTORY是该树的RobWork子目录，RWS_BUILD_INDUSTRIALROBOT=ON，VS17 2022 x64 Release；build/core-smoke为同一工作树industrialrobot独立配置。D盘vcpkg工具链只读共享，不使用D盘源码或可执行文件。测试未启动GUI。运行PATH前缀为本树build/RobWorkStudio/bin/Release和共享vcpkg/installed/x64-windows/bin。

| 验证 | PASS | FAIL | SKIP | NOT RUN | 证据/含义 |
| --- | ---: | ---: | ---: | ---: | --- |
| 新增原始14单元反例红灯 | 0 | 14 | 0 | 0 | red-unit.log/xml/json，均为目标安全断言失败 |
| 原始2真实子进程反例红灯 | 0 | 2 | 0 | 0 | red-process.log/xml/json |
| 修复后14单元/2进程定向 | 16 | 0 | 0 | 0 | green-unit、green-process |
| 稳定后相关功能回归 | 146 | 0 | 0 | 0 | affected-unit2；前次84项中的1失败是裸写夹具缺command，已补合法夹具，不削弱校验 |
| project完整单元（各相关全集只跑一次） | 304 | 0 | 0 | 0 | full-project.log、full-unit.xml及独立原生JSON |
| project完整契约 | 34 | 0 | 0 | 0 | 同日志、full-contract.xml及独立原生JSON；含真实崩溃重开 |
| execution归档直接消费者 | 6 | 0 | 0 | 0 | consumer-execution：ArchiveCollaborationContractTest全3项及WorkerProcessContractTest定向3项 |
| 独立配置新增回归 | 14 | 0 | 0 | 0 | build-smoke/smoke-project日志及XML/原生JSON |
| 性能首次完整组 | 1 | 1 | 0 | 0 | perf-project.log、perf-project-before；热组超3秒，冷组仅软指标 |
| 去重后单元定向 | 58 | 0 | 0 | 0 | dedup-unit：15个评审回归＋TxEngineTest＋ProjectStoreTest；不是声称又跑305项全集 |
| 去重后真实进程定向 | 10 | 0 | 0 | 0 | final-process：Tx7ProcessLock及F8五边界 |
| 去重后独立配置受影响定向 | 25 | 0 | 0 | 0 | build-smoke-final/smoke-final：共享对象新增例＋TxEngineTest |
| 去重后完整性能组 | 2 | 0 | 0 | 0 | perf-project-after；热样本原文及独立重算见perf-hot-oracle.json |
| 补齐冷组样本导出 | 1 | 0 | 0 | 0 | perf-cold-details及perf-details/b04-cold-open.json；首次未设置IRD_PERF_REPORT_DIR，只补该组既有导出，不算第三次全套性能验收 |

后续消费者不因内部扫描去重而扩大重跑：execution生产归档路径不调用恢复扫描，归档修复已由6项直接契约实测；evidence产品不依赖project链接，本包未改它的接纳/当前性规则；未构建或运行无关GUI。原CORE-T12和TK-T13/EV-T13的独验记录保持不变。

主要命令（均在本记录首行工作树执行；日志/XML/原生JSON位于本目录）：

```powershell
cmake --build build --config Release --target sdurws_ird_project_test_report sdurws_ird_project_contract_test_report
cmake --build build --config Release --target sdurws_ird_execution_contract_test sdurws_ird_project_perf_test
cmake --build build/core-smoke --config Release --target sdurws_ird_project_test
cmake --build build --config Release --target sdurws_ird_project_perf_test_report
```

定向gtest过滤：新增红/绿为ProjectReviewRegression.*和两个Tx7ProcessLock.Reader*；去重后为ProjectReviewRegression.*:TxEngineTest.*:ProjectStoreTest.*，真实进程为Tx7ProcessLock.*:TransactionBoundaries/F8ProcessCrash.*，独立补测为ProjectReviewRegression.RecoveryChecksSharedHistoricalObjectBytes:TxEngineTest.*。所有实际过滤非零命中。每次直接运行均使用本树绝对exe路径，并显式传--gtest_output=xml和--ird_report。execution过滤为ArchiveCollaborationContractTest.*及WorkerCompletesViaHandshakeAndNineStepAdmission_EX_WKR_1、ResultBatchesStreamInOrderBeforeFinal、TempDirCleanupFailureKeepsTerminalStateAndReportsDev_EX_ARC_3。

冷组的计时位于新进程main内，测open及查询，不包含完整CreateProcess/DLL启动时间，更不等于OS磁盘冷缓存或真实Studio冷启动；已有性能JSON中更宽的“重置”说明不扩展本轮结论。热组按1预热＋15有效样本、线性插值P95重算。进程kill只证明所测边界的异常终止恢复，不证明断电持久性。Win32原子可见性与持久性保证未因本轮扩大。

中间编译/setup失败如实保留build-contract-red、build-green、build-green2；绿色阶段一次TempDir清理告警未在完整及后续同例重现，不作为产品保存失败。既有handlerRegistry noexcept/C4297等警告保留，未声称零警告。

## 复用准入、回滚与独验入口

旧11项pending完整保留，详见binding-audit.json。ObjectStore两项、CommandService submit、Query分支投影、Archive五项及loadCommittedState的未决审计没有被本包改成migrated。T40后来批准TxEngine整文件收口，其诊断功能行当前已migrated；这不代表GAP-PRJ-DIAG-BOUNDARY-01提案整体获批。本轮不裁决该语义冲突。14个变动文件仍有存量全文件区段覆盖缺口，结构清单如实保存；不通过整文件业务豁免补齐。

正式门禁：`cmake -DIRD_ROOT=<本树industrialrobot绝对路径> -P <本树cmake/ird_gates.cmake>` PASS（ird-gates.log，含既有负夹具）；`pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/PRJ-T19.json` FAIL（validate-docs.log，exit 1）。首败为`REUSE-COVERAGE : .../project/include/sdurws/ird/project/DraftService.hpp : no functional matrix`；11项pending及其余覆盖缺口是后续仍需处理的准入问题，不能仅补头文件后宣称通过。全库DeepEvidence为NOT RUN；未将组合文档/复用检查冒称单独文档PASS。未重复执行包含同一必败复用检查的ird_gates构建目标。先前RepoRoot层级及masterWp登记错误分别存validate-docs-invocation-error/validate-docs-task-error，纠正后才得到上述有效准入失败；这些是实施准备错误，不是产品缺陷。

sync-bindings.py只重锚已变源/输入和登记实际新增函数/回归，不改原功能决策/状态；逐文件凭证不是全文件语义关闭凭证。独立Python核对原功能decision/status变化为0，新行24条，execution矩阵只更新26个实际输入摘要，未改其源码或功能裁决。

本地提交后用`verify-task-scope.ps1 -TaskFile <PRJ-T19> -Base 412e173b9079fa43a1f0ef50fe07d38e852b1940 -Head <实际提交SHA>`及`commit-lint.ps1 -Range 412e173b9079fa43a1f0ef50fe07d38e852b1940..HEAD`核对精确范围。原文输出留在外置评审目录的implementation-precheck/，避免为记录自身SHA反复提交；最终对话报告真实结果。

回滚边界：以PRJ-T19本地提交整体revert回到起点语义；格式无迁移，已有项目/备份/工件不改写、不清理。若后续按W1—W7拆回滚，TxEngine结果字段/宿主推进、扫描快照/查询刷新、command校验/合法夹具须成组，不能只回退一端。不得通过回滚删除用户数据或恢复已被篡改的实验文件到真实工程。

下一步由所有者另开独立会话审查冻结提交：核对原13项、实际红绿、HEAD提交后错误分类、全文件准入缺口、只读与历史引用、性能及无格式变化；本实施不自签独验，不写任务done，不合并/推送。待裁决C1—C4及close/acquireInFlight、abandon/writeBatch未证实交错继续保留。evidence不得把listRuns存在、归档成功、摘要字段匹配或本批自查当作“当前结果可用于工程判断”；仍须独立输入/修订/策略/完整性与当前性判断。

## 独立验收（2026-10-28）

所有者另开独立会话对实施与回归面验收 **PASS**，登记于 [acceptance/PRJ-T19-20261028-r1.json](../acceptance/PRJ-T19-20261028-r1.json)（证据同目录），范围显式 `-Base 412e173b9 -Head 59af1f9b4`（100 路径允许面）。独立会话受影响目标增量重建后真实重跑：project 完整单元 305（终态，含其后追加的共享对象新例）／完整契约 34／去重回归 58／真实进程 10（Tx7ProcessLock＋TransactionBoundaries/F8ProcessCrash 精确过滤）／execution 消费者 6／独立冒烟 39／性能 2（热样本 930～1016ms，3 秒硬预算断言 PASS）全部 decisive 零失败；ird_gates 零命中、scope 与 commit-lint PASS。validate-docs 的登记 FAIL（首败 REUSE-COVERAGE DraftService.hpp no functional matrix＋既有 AD-PENDING）独立复现且失败面一致——**包级文档/复用准入仍未通过，任务保持 ready 不收口 done**；13 项修复、扫描去重与性能预算经源码 diff 与真实子进程/性能测试核对成立。

## 准入结案批（2026-10-28，dad436e3f）

所有者指示"解决阻塞的 fail"后实施：[admission-closure.md](admission-closure.md) 为 evidence 锚点，[fill-coverage.py](fill-coverage.py)（可重复生成器）对 14 个已变更文件按语义区段补全 **440 条功能行**（函数/类/TEST/文件壳，描述取自源码注释与签名）、重锚 **13 条**既有行区段、将压在已变更文件上的 **11 条 pending** 结案为 business-specific/migrated（逐条依据在 admission-closure.md；AtomicFile.cpp 8 条 pending 因文件未变更保持原状）。复刻口径自检（区段摘要/符号/行覆盖/目标注册/字段完整性）全绿后提交，真实门禁 `validate-reuse.ps1 -Head dad436e3f` 按单元序检查——**project.json 全部通过**，其后唯一失败为 `REUSE-FRESHNESS: runtime/CMakeLists.txt`（POL-T13 批 a167f4d4d 自行登记的生产证据冻结遗留，涉及 REUSE-T53 冻结报告链，不归本批处置）。本批零产品代码改动；任务保持 ready，树级门禁转绿后收口 done。
