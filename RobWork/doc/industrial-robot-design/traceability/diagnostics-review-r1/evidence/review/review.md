# diagnostics 专项代码评审（只评审）

评审日期：2026-10-28。结论：确认 **15 项问题（P1 3 项、P2 11 项、P3 1 项）**；另列未验证疑点、契约冲突和可选维护项。最优先的实际生产问题是日志脱敏装配缺失，以及脱敏规则和日志出口的旁路。现有两个测试目标 149 项全部通过，不能覆盖这些反例。本报告没有证明真实 project 命令错误放行，也不构成后续修复的独立验收。

## 1. 现场、版本与边界

- 指定仓库与实际根：`C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`；分支 `codex/core-review-fixes`。
- 启动 SHA：`4284e8e05a422ac247f357f847718888c2cc16b3`；启动 `git status --short` 为空。四条启动命令已在本轮前半段真实执行，记录来自会话工具输出。
- 续接时发现他人新增未跟踪 `traceability/acceptance/EX-T11-20261028-r1/`；随后其他会话提交 `af375242c7b632d572c8190ee0d954aa149d80dc`。已核对 `4284e8e05..af375242c`：只新增 EX-T11 验收证据并更新 execution 评审 README，industrialrobot 生产/测试/CMake 源码无变化。没有切换现场或替换测试产物。
- 最终现场、差异和单元文件 SHA-256 见 [site.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/site.json)、[evidence-index.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/evidence-index.json)。本轮没有 Git 写操作；新增内容仅位于被忽略的 `C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028`。
- 已读指定三份 AGENTS.md，diagnostics 内未发现下级 AGENTS.md。按需核对 diagnostics 单元卡、ERR-01/UX-03/PM-17/NFR-REL-05/NFR-SEC-07、ARCH §3.5/§3.6.3/SA-12/SA-15、migration §13、逐功能矩阵及 T16/T24/POL-T13 原始验收。没有通读全库存或审查其他业务单元全部实现。
- 全部构建、exe、XML/JSON 和探针来自 C: 本树。`build/CMakeCache.txt` 的 `CMAKE_HOME_DIRECTORY` 指向本树 `RobWork`，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`，VS 2022 x64；D: 仅用于既有 vcpkg 依赖供给。diagnostics 无 Widgets 测试，本轮未启动 GUI。

## 2. 职责与真实数据流

正式诊断链：业务产生 `core::DiagnosticRecord` → 以已注册稳定码查询 descriptor → 工厂验证 subject/来源/参数名集/比较字段是否存在 → 构造 `DiagnosticEntry` → 目录去重、关系校验、容量管理 → 用户投影/安全摘要 → evidence 结果或 project 命令的 owner 持久化。entryId 是会话身份，不是项目持久化外键。工厂当前对比较值仅验证“有无”，并不证明工程数值或量纲正确；这些检查必须与 core/域边界分清。

日志实际链：`log/logDev/replayWorkerBatch` 入队 → 单日志线程截断、脱敏、级别过滤、节流/采样 → 编码 → 文件追加/轮转/flush → dev 尾部快照 → 关闭收尾或 CrashReportWriter。设计图写“脱敏再入异步队列”，当前原文会先进入进程内队列；本轮确认的泄漏发生在最终出口，不把仅在内存队列保留输入本身判成违反写盘合同。

确认设施链：descriptor 允许确认且有 comparison → create 冻结绑定与 callbackToken → Pending → confirm/reject/invalidate/显式 expire → project 编译前复核 → 命令终结清理。默认 `expiresAtUtc=nullopt` 不会立即过期；终态不可再确认、token 清零已有实际检查。权限失效由 project 调 invalidate/绑定复核负责，服务不自行决定项目或模型合格。

**真实 project 接线与替身不同**：[CommandServiceImpl.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/CommandServiceImpl.cpp:450) 的 S4 直接经纯交互端口取决定、冻结凭据、复核后调用 core finding.confirm；S5 在双编译前再次复核，编译失败不进入 S6，确认摘要于第 707 行写入 CommandRecord。其绑定设施位于 [ConfirmationFlow.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/src/ConfirmationFlow.cpp)。当前生产搜索未发现 ConfirmableService 或 replayWorkerBatch 的真实调用方；注释/端口声明不等于执行接线。[ConfirmableProjectStubTest.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/test/ConfirmableProjectStubTest.cpp:18) 只证明替身编排，不能代验这一真实 project 实现。

owner 边界保持：core＝纯数据；diagnostics＝码/分类/文案键/工厂/目录/确认设施/日志；policy＝工程阈值与判定；project＝命令/修订/确认摘要；evidence＝结果与证据判定；execution＝运行/worker；ui＝呈现与交互。未发现 diagnostics 链业务单元或代替 evidence 产生工程结论。日志不作为正式项目事实。

## 3. 按严重度排序的确认问题

下列“复现”均使用合成输入；每项修复建议尚未实施。P1 指实际生产安全出口问题，P2 指明确的正确性/生命周期合同问题，P3 指运维统计错误。

### D01 · P1 · 生产诊断栈只给目录挂脱敏，普通日志与异常工厂未接

**位置/符号**：[DiagnosticsAssembly.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/app/DiagnosticsAssembly.cpp:58) `assembleDiagnostics`（51–61）；真正入口为 [HarnessMain.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/app/HarnessMain.cpp:199)、[UiPlugin.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:719)。pipeline/factory 已构造，但只执行 `catalog->attachRedactionService`。工厂在 [Factory.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Factory.cpp:279) 未挂接时直接保留截断后的异常原文。

**合同**：NFR-SEC-07；diagnostics §7.3 强制脱敏、§7.7 凭据一律不记录、§9.5 合法调用面包含日志与工厂。

**最小输入与结果**：原装配函数装入隔离目录，写 `password=FakeAssembly932`。预期写盘前替换；实际 `dev-diagnostics.log` 原文包含该假密码。证据：`ReviewWiring.ProductionAssemblyRedactsBeforeDisk`，[probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log) 及 [dev-diagnostics.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-real-assembly/dev-diagnostics.log)。探针直接编译原 `DiagnosticsAssembly.cpp`，仅 `uiDiagnosticCodeDescriptors()` 供体为空替身；日志、工厂、脱敏、目录与磁盘是真实实现。这证明装配缺失，不冒称完整 UI/Widgets 回归。

**影响/责任**：根因 owner 为 ui 装配层；diagnostics 对未安装服务保留装配前降态，不能把降态当成生产配置。普通日志先泄漏，后续 Crash writer 再脱敏无法撤销磁盘原文；工厂快照/异常转译也缺这一接线。

**最小修复/复用/回滚**：使用已有两处 `attachRedactionService` 接口，在栈向消费者发布前接齐，保留 catalog 双保险与对象生命周期；不新增脱敏器、不改正式诊断原始数据语义。补原装配函数真实文件测试，检查两 Tier 与 factory translate/参数快照。回滚仅装配函数及相应回归；公共 API 不变。

### D02 · P1 · 凭据规则遗漏 JSON 引号键与明确要求的裸 key

**位置/符号**：[Redaction.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp:162) `RedactionService::Impl::reCredentialKv`。

**合同**：NFR-SEC-07；diagnostics §7.7 第 710 行明确 `password/token/secret/key/apikey` 命中即替换。

**复现/预期/实际**：`{"password":"FakePassword918"}` 与 `key=FakeKey927` 脱敏后仍完整保留；对照 `password=FakeOk928` 正确替换。引号使键与冒号不再相邻；正则候选表也缺 `key`。原始输入输出见 `ReviewRedaction.JsonQuotedCredentialAndKeyAreRemoved`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)。

**影响**：所有使用同一服务的自由文本日志、异常摘要、目录导出和 CrashReportWriter 都可能保留这两类合成凭据。不是要求识别任意未知秘密，而是已有明确模式的漏项。

**最小修复/复用/回滚**：沿既有规则链补齐受限键语法及转义值边界，复用当前手工替换器、失败整条替换与 core 摘要；不引入另一套 JSON/脱敏平台。回归大小写、带引号键、转义引号、空值、普通单词不误伤、重复脱敏、日志/崩溃文件真实出口。回滚为 Redaction.cpp 与定向测试。

### D03 · P1 · 可由公开输入伪装“内部通道”，元字段未经脱敏与单行编码

**位置/符号**：[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:820) `renderRecord` 按 channel 跳过脱敏；[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:1080) `formatLine` 原样拼接 channel/code/threadTag；[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:1275) `validateRecordBasics` 主要只校验长度。message 有转义，元字段没有等效防线。

**合同**：NFR-SEC-07、NFR-REL-05；diagnostics §7.3/§7.7 全量脱敏及头文件单行编码合同（DT-LOG-2）。内部安全常量的递归保护不能由调用方自由命名的通道取得。

**复现/结果**：已挂真实 RedactionService。正常 message 中 `password=FakeNormal928` 消失；channel=`diag/logging` 的 `password=FakeInternal929` 仍在；threadTag=`password=FakeTag930` 仍在；channel=`review\nFORGED password=FakeChan931` 在一条输出中形成真实换行并保留假密码。见 `ReviewLogging.InternalChannelAndMetadataStillRedact`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)；MemoryOps 的写出字符串和 devTailSnapshot 是同一真实格式化结果。该探针检查的是内存文件接缝/尾缓存，真实磁盘装配证据另由 D01 提供。

**影响**：普通 log/worker 载荷可绕过安全投影，伪造额外物理日志行；泄漏在写盘和尾缓存之前已形成。code、tag 内的内部摘要也能绕过 User message 的专用过滤。

**最小修复/复用/回滚**：统一各自由文本字段的脱敏与输出编码；对 token 语法执行既有 fail-fast 边界。把可信内部记录的旁路限制到内部生成路径或等效有限失败路径，保留脱敏失败常量不递归，不能仅删除旁路而引入失败日志自激。复用既有 `escapeLineMessage`、redaction 与队列，不创建新公共日志平台。回归普通/worker/User/Dev/内部同名通道、CR/LF/控制字符和脱敏器失败；回滚局限 Logging.cpp 内部记录/格式化及测试。

### D04 · P2 · 工厂接受与稳定码相反的阻断级别

**位置/符号**：[Factory.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Factory.cpp:174) `DiagnosticsFactory::create` 复制 `record.level`，却从 descriptor 单独取 severity；没有一致性验证。权威字段合同见 [diagnostics.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/units/diagnostics.md:359)。

**复现**：`RT-INPUT-INVALID` descriptor.level=Error，把合法 record.level 改为 Warning 后 create。预期拒绝不一致或按已裁定规则输出权威级别；实际 entry.severity=Error、entry.record.level=Warning。见 `ReviewFactory.StableCodeBlockingLevelCannotBeDowngraded`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)：`descriptor=0 record=1`。

**影响/消费者**：runtime 的 [CanonicalModel.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/CanonicalModel.cpp:862) 以 record.level 判断发布阻断；此处只证明产生互相矛盾的正式数据，未证明该条探针数据已经穿过真实模型发布或命令提交。反方向已有直接消费者证据：[Diagnostics.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/Diagnostics.cpp:116) 对 Warning 码调用 core.make 时没有传 level，而 [DiagData.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:126) 默认 Error。因此不能只收紧工厂后就认为所有消费者兼容。

**最小修复/复用/回滚**：沿已有 CodeDescriptor.level 单点落实一致性，优先在边界拒绝错配并同步受影响直接产码者；不要在业务单元再造 severity 表。是拒绝还是规范化须按“core 原样内嵌”合同明确，不能静默修改已持久化记录。回归两方向错配、policy Warning/Info、unknown 异常兜底、runtime 阻断与 Warning 不阻断。回滚为工厂校验及直接受影响产码改动，不改 codec 历史格式。

### D05 · P2 · 非法分类枚举可进入稳定注册表

**位置/符号**：[DiagCodes.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/DiagCodes.cpp:628) `StableCodeRegistry::registerCode` 的验证链到 734 行未检查 category/severity/retryable 的闭合值域；[DiagCodes.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/DiagCodes.cpp:535) token 兜底会把非法 category 显示成 input-invalid。

**合同**：§4.3 冻结分类/严重词表、§4.5 descriptor 有效性与 fail-fast。合法枚举值能编译，不等于任意底层整数都是合法 descriptor。

**复现**：复制真实 `RT-INPUT-INVALID` descriptor，令 `category=static_cast<DiagnosticCategory>(255)`，在空注册表注册。预期拒绝且表不变；实际成功存入 255。见 `ReviewRegistry.InvalidCategoryIsRejectedBeforeManifest`，[probe-registry.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-registry.log)。日志中 `sameManifest=0` 比较的是含 entries 的整个对象，不能据此声称摘要相同；本条不依赖该比较。

**影响/修复**：装配错误可污染后续分类/呈现和 manifest 文本编码。尚无真实生产供体产生非法枚举的证据，影响限注册边界健壮性。最小修复是既有注册链前置检查闭合值域，沿 DiagnosticsError(Usage) 错误轨，保留 token 的合法值映射和 core ContentDigester；回归各枚举非法值、失败后再次合法注册、查询与 manifest。回滚仅 DiagCodes.cpp 与定向测试，公共 descriptor 不扩展。

### D06 · P2 · 确认状态已提交后日志抛错；noexcept 过期查询可因此终止进程

**位置/符号**：[Confirmable.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Confirmable.cpp:543) `submitConfirmation` → [Confirmable.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Confirmable.cpp:757) `finishTransition`；[Confirmable.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Confirmable.cpp:633) `tryFind noexcept` → `expireIfDue` → 同一日志回调。

**合同**：§9.3 结果枚举优先、短临界区无 I/O、值查询 noexcept；日志非权威，不应把确认结果变成日志故障。IDevLogSink 没有 noexcept 或不准抛错的前置。

**复现/实际**：create 时 sink 正常，提交确认时 sink 抛合成异常。调用者收到 `synthetic sink failure`，但查询已是 Confirmed，token=0，重试无效。见 `ReviewConfirmable.LoggingFailureDoesNotReplaceConfirmationOutcome`，[probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log)。再在 gtest 子进程显式 armExpiration(now)、令过期转换的 sink 抛错，tryFind 经 noexcept 进入 terminate；自定义 terminate handler `_Exit(86)` 被 `EXPECT_EXIT` 捕获，[probe-final.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-final.log) 中该证明用例 PASS。此处有具体可达调用链，不是因 std::string 分配做一般推断。

**线程/生命周期/影响**：m_mutex 从 submit/tryFind 一直持有到 logDev；日志慢/重入会延长临界区。复核环境明确禁止反向进入服务，不能把环境违约当 bug。当前生产 project 未使用本服务、也未启用显式过期，故未证明生产错误放行或正常无超时路径 terminate。

**最小修复/复用/回滚**：锁内完成状态与快照，锁外发已有 IDevLogSink 通知并隔离日志异常，返回既定确认结果；过期查询遵守同一有限降级，保留 core finding 不变量、绑定与 token 规则。回归抛错 sink、显式过期子进程、确认/拒绝并发终态唯一性、读回结果和 principal 不入日志。回滚为 Confirmable.cpp 的转换/通知边界及测试；不接管 project 确认编排，不加新确认公共接口。

### D07 · P2 · 容量腾位忽略待追加条目的引用，制造悬空原因链

**位置/符号**：[Catalog.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Catalog.cpp:374) `DiagCatalog::append` 先验证关系存在，397 行腾位，411 行才插入；[Catalog.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Catalog.cpp:232) `evictForCapacity` 引用快照仅含旧条目。

**合同**：§6.3 链接完整性；DIAG-T09/§14.4 v0.10 明确保护被引用条目，无法腾位时软溢出优先保留证据。

**复现**：capacity=1；加入 Info `RT-CANCELLED` 根 A 并 markConsumed；追加 Error B，B.causedBy=A。预期 A 因新引用受保护，软溢出保留两条；实际 A 被删，只剩 B。见 `ReviewCatalog.IncomingCauseProtectsConsumedRoot`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)：`catalogSize=1 surviving=2`。

首批 helper 对 Info 记录沿用 core 默认 Error，涉及 D04 的实例级别问题。已补充 `ReviewValidInputs.IncomingReferenceProtectsCanonicalRoot` 将 root.level 显式对齐 descriptor，再次得到 catalogSize=1，见 [probe-valid.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-valid.log)；因此 D07 不依赖 D04 的错误输入。

**影响/最小修复**：目录根因导航/后续 CauseLink 查询断链；不影响已由 owner 持久化的 record，但会丢会话排障证据。将本次待加入条目的 causedBy/relatedTo/supersedes 纳入本次保护集后再腾位，复用既有引用计数/软溢出，不新建原因图。回归三类关系、链保护、多遍淘汰和无关系正常腾位。回滚仅 Catalog.cpp 与容量测试，接口和 owner 不变。

### D08 · P2 · 目录快照未按公开 orderKey 排序

**位置/符号**：[Catalog.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Catalog.cpp:407) 追加保存入目录序；[Catalog.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Catalog.cpp:458) `snapshot` 直接遍历，未按 key 排序。

**合同**：diagnostics §6.4 第 611 行：目录与聚合视图输出按 `(时间戳计数, entryId, code)`。

**复现/具体交错**：同一固定时钟，T1 创建 A(id1) 后暂停；T2 创建 B(id2) 并 append；T1 再 append A。目录锁只串行 append，不能恢复创建序。探针用等效顺序 B→A 入目录，实际快照 2,1，预期 1,2。见 `ReviewCatalog.ConcurrentArrivalUsesOrderKey`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)，没有依赖概率线程调度。

**影响/修复**：UI/报告快照顺序漂移，限量导出可能取到不同前缀。只在查询/导出投影按既有 orderKey 排序，过滤后再取前 N；容量淘汰“最旧”的历史实现定义为入目录序，本条不擅自更改它。复用原 key，无新公共排序字段；回归逆序追加、同钟、跨线程、过滤及导出上限。回滚查询/投影内部代码与测试。

### D09 · P2 · 同根因聚合键缺任务分区

**位置/符号**：[Aggregation.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Aggregation.cpp:185) `groupKeyOf` 的 cause-root 分支固定 `hasTask=false`；code 分支在 192–196 行正确带 task。

**合同**：§6.4 明确不同 TaskIdentity 不聚合；aggregate 前置允许调用方取“全部”目录子集。causedBy 要求已存在/无环，没有一概禁止跨任务指因的条款；不自行添加这种禁令来掩盖分组漏洞。

**复现**：根 R；A 属 att-1，B 属 att-2，均 causedBy=R。手工预期 A/B 不同分区；实际两者同组 count=2。见 `ReviewAggregation.SameCauseRootDoesNotMergeAcrossTasks`，[probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log)。输入均为真实工厂条目，关系有根且无环；期望未使用生产聚合 helper 计算。

补充 `ReviewValidInputs.CatalogAcceptedCausesRemainPartitioned` 显式对齐所有实例 level，并把三个条目实际 append 到真实目录；成功接收 size=3 后聚合仍得到跨任务 count=2，见 [probe-valid.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-valid.log)。它补强了 aggregate“目录子集”的前置，不要求新增禁止跨任务原因关系的公共规则。

**影响/修复**：全局诊断折叠把两次尝试混成一次显示/计数，用户无法直接区分当前尝试。沿现有 GroupKey 的 task 字段让两类分组一致，保留 rootId、原始成员和 cause 导航，不改变 evidence 工程判定；回归同根因同任务可聚合、跨 attempt/run/project 不聚合、无任务域、过滤子集断链容差及排序。回滚 Aggregation.cpp 与用例。

### D10 · P2 · flush 的时限未覆盖文件冲刷，析构也未执行承诺的 3 秒排空预算

**位置/符号**：[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:1374) `flush` 只给 queue/processing 等待 deadline，1409 行仍在调用线程同步 flushFile；[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:1331) 析构无 deadline，直接排 Shutdown 后 join。公开析构合同在 [Logging.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Logging.hpp:524) 明定等待 kLogDefaultFlushDeadline、超时丢剩余行。

**复现/结果**：注入一次有界 250ms flushFile，`flush(20ms)` 实际 259ms 且 true；见 `ReviewLogging.FlushDeadlineIncludesFileFlush`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)。另每次 append 只延迟 20ms，180 条正常 Info 入队，析构实际 5293ms，超出 3000ms 排空预算；见 `ReviewLoggingExtra.DestructionHonorsThreeSecondDrainBudget`，[probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log)。所有注入操作都返回，未用永久挂死替身制造无法回收进程。

**影响/线程**：关闭调用者可能超时等待；单条“快速返回”并不推出整个排空满足预算。真实 FileLogFileOps 同实例锁与操作系统文件操作没有硬截止，永久阻塞是尚未实测的更强条件，本报告不声称已动态复现永久挂起。

**最小修复/复用/回滚**：flush 请求沿已有队列做带完成状态的屏障，调用方只等待自己的 deadline，避免排空后再同步 I/O；关闭按预算停止继续消费剩余普通行并计数。保留一个日志线程、原 FileOps 与借持生命周期，不能 detach 后继续访问已销毁 clock/ops。对无法中断的在途 OS I/O 如何实现完整关闭硬界需具体方案评审；若涉及公共接缝语义变更须所有者裁决，不能私自降低承诺。回归 gated 在途写、队列为空但 flushFile 慢、调用超时、关停余量与对象销毁次序。回滚限日志关闭/flush 内部状态及测试。

### D11 · P2 · Windows 文件追加经窄字符串转换，合法 Unicode 日志目录写不出

**位置/符号**：[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:345) `FileLogFileOps::Impl::appendOne`，Windows 使用 `p.string()` + `fopen_s`。

**合同**：现有 Windows + std::filesystem 路径支持范围，NFR-SEC-07 路径按配置处理；用户目录/Crash 输出不应被本机 ANSI 代码页收窄。

**复现**：本隔离目录下创建 `probe-unicode-机器人-🤖`，追加 `test.log`。目录创建成功，追加的路径转换抛异常，append=false、文件不存在。见 `ReviewFiles.UnicodePathPreserved`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)；原异常 what 使用本机代码页，日志保留了原始字节，不将其乱码解释为业务错误。合成数据不含真实用户秘密。

**影响/修复**：此路径同时服务普通日志与 CrashReportWriter，非代码页字符的用户目录会丢排障文件。沿原 filesystem、实例锁、bool/异常错误轨，用 Windows 原生宽路径 `_wfopen_s` 等最薄平台适配；不改框架、不引入依赖、不重写轮转。回归中文/空格/非 BMP 字符路径、真实管线及 writer、失败路径无旁边文件误写。回滚仅文件打开适配和测试。

### D12 · P2 · 异常消息的 512 字节截断破坏 UTF-8

**位置/符号**：[Factory.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Factory.cpp:45) `truncateMessage` 用 `substr(0,512)`。

**合同**：中文诊断文案与 UTF-8 输出；§9.2 转译摘要截断，不能产生不完整编码。不是总长度是否包含标记的争议（后者见 C03）。

**复现**：511 个 ASCII `a` 后接“中文”。实际切点保留 `中` 的首字节 e4，随后立即加截断标记的 e2；独立 Python strict UTF-8 解码在 511 处报 invalid continuation byte。证据：[translation-cause.bin](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/translation-cause.bin)、[utf8-decode.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/utf8-decode.log)、`ReviewUtf8.TranslationPrefixDecodes` / [probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log)。

**测试更正**：第一批 `ReviewFactory.TranslationTruncationPreservesUtf8` 断言“中必须保留”过强，已撤回作为合格判据，未删除失败原件。正确修复裁到 511 字节也可以；最终依据是原始字节及独立严格解码。

**影响/最小修复**：中文异常经转译后可能显示替代符，或被严格编码的导出消费者拒绝。复用本单元 Logging/Redaction 已有 UTF-8 边界回退做私有单点，保留原摘要与标记合同，不新建 core Unicode 平台。回归 2/3/4 字节边界、精确阈值、短输入和无效输入的既定处理。回滚 Factory.cpp/本单元私有 helper 与定向测试。

### D13 · P2 · final 到达期望序号时先排空高序缓存，收尾后数据被接受

**位置/符号**：[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:1136) `processBatch`：accept final → expected++ → drainReady → 最后才 closed；对比 1163 行另一分支会丢弃滞留缓存。

**合同**：[Logging.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Logging.hpp:289) final=true 表示之后无批；陈旧/收尾后批应丢弃计数，不能因到达顺序而改变。

**复现**：先 seq=1、非 final、message=`LATE_AFTER_FINAL` 进入缓存；后 seq=0、final、message=`FINAL`。预期只写 FINAL 并丢弃高序缓存；实际两条都写出。见 `ReviewLogging.WorkerFinalDiscardsAlreadyBufferedLaterBatches`，[probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)。

**影响/修复**：重放接受已被收尾定义排除的记录，迟到批语义与计数不一致。当前未找到真实 execution → replayWorkerBatch 接线，不声称真实 worker 端到端事故。统一收尾分支，先限定 final 边界、丢弃高序缓存，再完成有序处理，沿原 streams/计数器而非新通道框架。回归 final=expected、跨缺口 final、重复 final、先缓存后收尾、同 worker 不同任务隔离。回滚 Logging.cpp worker 流逻辑与测试。

### D14 · P2 · 采样末条已直接写出时仍补写更早尾槽，日志倒序

**位置/符号**：[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:938) `samplingDrop` 首/每 N 条返回 false 时未清除 `st.tail`，finalize 后补旧尾条。

**合同**：§7.4 首条＋每 N 条＋实际末条、单线程 FIFO；关闭兑现末条并不允许把更早一条当最后事件。

**复现**：N=4，同通道 Debug 1、2、3、4 后关闭。预期只保留 1、4；实际输出 `SAMPLE_1`、`SAMPLE_4`、`[sampled-tail] SAMPLE_3`。见 `ReviewLoggingSampling.LastAlreadySelectedDoesNotReplayEarlierTail`，[probe-final.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-final.log)。

**影响/修复**：排障时间线尾端被旧消息覆盖，出现不应保留的采样行。成功选中最新一条时清理旧 tail，保留通道分组、首/每 N/末策略和原时间戳；无需第二缓存。回归总数=N、N+1、1、多通道和关闭前 flush。回滚 samplingDrop 及用例。

### D15 · P3 · 队列丢弃统计把 Error 误报成 Debug/Trace

**位置/符号**：[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:682) 全高等级队列丢新行；[Logging.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Logging.cpp:757) 固定写“丢弃 Debug/Trace 行”。

**合同**：§9.6 丢弃 Dev 计数须能解释实际损失，日志不得掩盖关键记录丢失。**本条只确认错误统计，Error 是否允许丢失另列 C04。**

**复现**：接缝闸门固定一条 Error 在途，capacity=2，再放 A/B/C 三条 Error；C 被丢弃，汇总却称 Debug/Trace 丢 1 行。见 `ReviewLoggingExtra.FullHighPriorityQueueKeepsError`，[probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log)；测试“保留 C”的断言属于 C04 冲突探测，错误文案则由原始输出独立证明。

**影响/最小修复**：运维误以为只损失调试噪声。沿既有 overflow 计数记录实际级别/原因，或用不误导的类别中性统计；保留既有溢出策略直到 C04 裁决，不自行改为阻塞/无限队列。回归全 Error、混合队列、Debug 淘汰和 configure/batch 不误丢。回滚计数与汇总文案、对应测试，不改变正式诊断。

## 4. 未验证疑点（不计入上述 15 项）

| 项 | 具体依据与待验证条件 | 边界/责任 |
| --- | --- | --- |
| S01 订阅退订与在途裸指针 | [Catalog.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Catalog.cpp:416) 在锁内复制 `IDiagObserver*`、锁外第 449 行调用；Subscription 析构只从原表删除，不能撤回已复制指针。具体交错：T1 复制后暂停，T2 退订并销毁 observer，T1 回调。需要有界并发探针/内存检查确认实际消费者的关闭协议。 | [DiagnosticPresentation.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/DiagnosticPresentation.cpp:469) 的真实 handle 先 reset adapter 再 reset 底层订阅，析构成员也先销毁 adapter；在途 Qt lambda 的 alive 位不能保护目录尚未发出的原始 adapter 回调。另有 `shared_ptr<bool>` 非原子跨线程声明。只定向定位，未跑 Widgets/TSan/ASan，留给 ui 与 diagnostics 共同核验，不把它冒称已测出的 UAF。 |
| S02 finding 终结回收无落点 | §5.4/§9.3 写命令终结回收；Confirmable.cpp 的 m_records 只 emplace/find，没有 erase/clear，目录清理没有连接服务。 | 设施生命周期/接口缺口确有源码依据；当前生产 project 不用此服务，尚未测量真实会话增长，不能直接声称生产内存泄漏。是否添加受控回收入口涉及公共合同，先提交缺口与 owner 方案。 |
| S03 回调异常与重入 | Catalog 通知在 append 已提交后直接调用，observer 抛错会让调用者收到失败；Confirmable 的 logger 在服务锁内调用，若 sink 反调 tryFind 将再次等待同一 mutex。 | 未触发真实消费者异常或做重入进程探针；环境探针明确禁反向进入，不混为这一疑点。与 D06 的已复现异常路径区分。 |
| S04 参数/比较有效性边界 | 工厂只验证 schema 名集与 comparison 有无，未检查直接手改 record 的字段完整性/量纲；ParamSchema 的 cctype 已转 unsigned char，但仍受全局 locale 影响，文法文字是 ASCII。 | core 值类型、域校验与工厂各自的责任须定向核对；尚无经合法生产构造路径产生错误工程比较的证据。受限 schema 本来是参数名列表，不支持类型表达，不能自行要求它实现完整 JSON Schema。 |
| S05 轮转失败/时间窗/大输入 | writeLine 忽略 rotateFile=false 并把估计大小清零；idle 时没有独立时间唤醒；正则大输入与 worker pending/seq 极值尚未性能验证。 | 逐行 fclose 使真实 FileOps 无长期标准库脏缓冲，不能据此声称闲置日志必丢；失败轮转策略已有实现登记，不能重开 T16 保留数已修问题。本轮没测永久占用/只读冲突/最坏正则耗时，均 NOT RUN。 |

## 5. 契约冲突与所有者裁决项

| 项 | 相冲突的现有文字/行为 | 本轮处置 |
| --- | --- | --- |
| C01 默认路径策略 | [diagnostics.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/units/diagnostics.md:705) 同一行同时写 Keep“默认开发机”与默认 Root-only；RedactionPolicy 实际默认 RootOnly。 | 标为文字冲突；没有选择更宽松策略或把默认 RootOnly 报成 bug。 |
| C02 跨修订/数值目录去重 | §6.4 冻结 DedupKey 四元组、同 code/subject/scope 保留首条；无 task 的 Object scope 不含 revision/command/参数。§6.2 又要求目录关联与宿主同源、支持 revision 查询。 | `ReviewCatalog.RevisionQueryDoesNotLoseNewOccurrence` 真实得到 all=1/newRevision=0，见 [probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)；不同实际值也可落同键。这证明现象，不能自动判实现违反冻结去重键。由 owner 裁定是否按会话/修订隔离目录、增加呈现过滤分区或升级键语义，禁止修复包偷偷扩大公共键。 |
| C03 safeSummary/异常摘要总字节上限 | [Redaction.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Redaction.hpp:177) 称输出上限；实现与 `SafeSummaryTruncatesUtf8Boundary` 明确 maxBytes 是前缀预算、另加 `[trunc]`；§7.6 异常摘要又称 512B。Factory 也在 512 后加中文标记。 | 由 owner 决定是否总输出含标记。D12 只修 UTF-8 完整性，不借机改变预算。 |
| C04 Error 丢失承诺 | §7.4“用户语义事件不丢”、§7.6“0 行 Error”、§9.6 只描述丢最旧 Debug/Trace；但 §14.4 v0.8⑨明确全高等级队列无可丢项时丢新行。 | 探针已复现 Error 丢弃，不能忽略后续登记硬报为未授权实现。需要 owner 选择容量/非阻塞/关键行保留的具体折中，并给出可验收范围；本轮不放宽标准，不选新框架。D15 的统计真实性可独立修复。 |
| C05 真正崩溃环境中的安全承诺 | Writer 是正常上下文摘要设施；WP-24-T02 壳捕获已真实调用它，CrashCapture 文档也承认沿既有 Writer 承担分配。 | 不要求普通 writer 无依据承担 async-signal-safe/堆损坏全保证。若要新增这些承诺，应由 ui 壳/平台方案承担；本轮只核现有调用与降级，不新增 crash dump 机制。 |

## 6. 可选维护性改进

1. [Diagnostics.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Diagnostics.cpp) 仍是纯注释空 TU。已有十个真实 TU 后可删除该文件与 CMake 列项、同步过期“87 码/后续实现”注释；不因此做语义迁移或放宽测试。
2. Logging.cpp/Redaction.cpp 重复 UTF-8 边界截断；Factory.cpp 的第三种字节截断已是 D12。可在 diagnostics 私有层统一一份经过独立编码测试的 helper，保留各自标记/长度语义。core 公开接口未检出可直接替代的截断能力，不新增 core 平台。
3. 小写码/文案键机械派生散于 DiagCodes、EntryDetail、Confirmable，适合局部私有复用；不要把 project/diagnostics 不同版本的持久化 binding 编码擅自统一。
4. `ReviewCrash.SameTimestampReportsHaveDistinctFiles` 证明同毫秒同 pid 的两次 writer.write 返回同路径，FileOps append 导致两段 format 块共存，见 [probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)。但文件名合同就是 timestamp-pid，真实 [CrashCapture.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/studio/CrashCapture.cpp:339) 一次性 atomic_flag 拦住重复发射；未找到需要连续多写的真实路径。因此撤回“应总是不同文件”的探针预期作为缺陷判据，只列 API 可重用性限制。是否支持重复写入先明确合同。

## 7. 稳定码、确认、日志与崩溃接线结论

**稳定码权威**：目前 builtin 实际 94 行；这是版本事实，不是正确性总数标准。POL-T13 新增六个 policy 生产码、工程范围输出拼写 `POLICY-ENGINEERING-RANGE-INVALID` 已落位；[POL-T13-20261028-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/POL-T13-20261028-r1.json) 有独验 pass。不重复报告“policy 缺码/旧拼写”已解决问题。注册表 map 节点地址在后续 insert 稳定；注册/废弃仅装配单线程、seal 后只读，这一合同没有承诺运行期并发改表，不能要求热注册安全。普通重复/键冲突/未知码/deprecated/level 映射检查均有现有通过用例；D05 是遗漏的非法枚举边界。多容器登记在 bad_alloc 下的强异常保证未做故障注入，不把理论分配失败列成确认半写入缺陷。

**参数工厂/转译**：T24 已把 names 暂存后成功交换，失败清空输出；现有 F968 回归本轮实际通过，见 [REUSE-T24-20261025-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T24-20261025-r1.json) 与 migration §13。关闭边界仅 partial output，F-968 原 finding 仍 open，不在本报告擅自关闭。参数名缺/多/重复及文法失败已有负例。转译按明确 typeid/基类登记找码，不解析异常 what() 当码；unknown fallback 是已登记 Dev 诊断，不返回业务成功。级别一致性 D04 和 UTF-8 D12 仍需修复。

**确认权限**：现有 service create 校验 confirmable/requiresComparison/绑定必填，错误 token、终态、输入/策略/命令变化会拒绝；默认无限期行为与 P-DIAG-6 一致。principal 不出现在服务转换日志，writer 只收 findingId/state。绑定合法也不代表命令成功：真实 project 保留逐 finding 决定、两次复核、双编译、失败不提交和确认摘要留痕。D06 是设施通知事务边界问题；S02 是尚未消费设施的回收缺口，均不可用替身结果夸大成真实 project 错放行。

**两级日志/脱敏**：User 的 message 额外屏蔽地址/栈/内部摘要，User 行镜像 Dev；禁用日志不影响目录或正式 owner 留痕。生产装配缺口 D01、规则漏项 D02、元字段/内部通道旁路 D03 使安全出口目前不能据基线全绿判合格。Redaction policy 用共享不可变快照切换，核心 hash 调 ContentDigester；异常降级返回常量并吞失败 sink 异常，已有真实故障注入通过，未观察到 fallback 回写原文。RootOnly 同一合成路径二次脱敏相同，探针 PASS；不报幂等缺陷。未验证最坏正则耗时，不宣称性能全部通过。

**轮转**：T16 批已独验通过，[REUSE-T16-20261025-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T16-20261025-r1.json) 与当前五个 DiagRetention 真实临时文件系统用例一致：keep 含活动文件、keep=1 不留 .1、缩容只处理规范正整数普通世代、目录冲突不覆盖、相似/非规范名不误删。配置 keep=0 被拒绝；FileOps 沿同实例锁和 bool/error_code，未承诺跨实例/跨进程同路径协调。符号链接保护已静态见 symlink_status/is_regular_file；当前五例没有创建符号链接，本轮真实 symlink 动态验证 NOT RUN，不能据代码守卫冒称已实测。新发现 D11 是打开路径编码，不是重新发明轮转。

**崩溃文件**：CrashReportWriter 的六段格式有界尾部 512 行，异常调用 safeSummary，输出值再脱敏/换行消毒；finding 不含载荷与 principal，project 身份不包含路径原文；失败返回 nullopt 并有限 stderr/Dev 错误轨。[CrashCapture.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/studio/CrashCapture.cpp:326) 真实独立创建 redaction/writer，防重入 gate 在 339 行，不能把缺失的日志装配等同于 writer 根本没脱敏。D02 共用规则漏项仍影响其自由文本。现有 diagnostics 所谓“崩溃测试”是正常调用 writer；本轮没触发真实 SEH/abort/watchdog。writer 无 fsync/断电持久化承诺；逐行 fwrite/fclose 只涉及标准库缓冲提交至操作系统，不能写成断电安全。crash 文件不用于恢复项目事实。

## 8. 架构、已有接口复用与简洁性

已按 H1–H6 核对上游本地源码，版本以本工作树 Git 对象与缓存 `ROBWORK_VERSION=26.10.28` 绑定，未查询远程：

| 来源/符号/目标 | 适用性与边界 |
| --- | --- |
| [Log.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/src/rw/core/Log.hpp:243) `rw::core::Log::setWriter/write/flush`；`Log.*`；目标 `sdurw_core` | 可提供通用等级流路由，但无稳定码 descriptor、参数模式、目录、确认 binding、两级安全投影、worker 收尾合同。不能因同名就替换整个产品管线。 |
| [LogWriter.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/src/rw/core/LogWriter.hpp:54) `LogWriter::write/flush`；[LogStreamWriter.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/src/rw/core/LogStreamWriter.hpp) 流 writer 及对应 cpp | 可作将来输出适配候选；本轮文件路径、保留世代和失败接缝需要独立语义，沿现 FileOps 修小缺口成本更低，未证明用上游 writer 能直接满足 T16/Windows 宽路径。 |
| [Exception.hpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/src/rw/core/Exception.hpp) 与 Exception.cpp | 异常载体，不是产品稳定码/分类表。不把解析异常文案当复用；保持已登记类型/码映射。 |
| core `ContentDigester`、ID/TaskIdentity/UnitToken、已有 gtest 与 TestMainReport 监听器 | 已直接复用摘要、身份、单位和测试报告；不自写 SHA/单位转换、不新建第二报告渠道。 |

源码/公共头与 CMake 的单元编译边为 diagnostics→core，未见 Qt 头、业务私有头或 product→testkit 反向依赖。集成构建显示全局 AUTOMOC 生成空 mocs TU，不等于 diagnostics 使用 Qt；现有源码/链接扫描只能证明结构，不能证明运行行为。

[diagnostics.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/reuse/functions/diagnostics.json) 当前 267 项中 232 migrated、35 pending；计数是矩阵状态，不是本轮合规证书。T16/T24 后续独验优先于历史 README 的“待独验”。产品必须保留稳定码/中文语义/确认绑定/安全投影；最薄适配为 Windows 宽路径和上游错误转译；可直接复用的是已有 core 值类型/摘要/测试报告；可以删除的是空 TU、重复私有截断和过期注释。没有新核心、上游改动、新依赖渠道或 owner 迁移建议进入实施。

## 9. 文件/功能覆盖与实际验证

静态覆盖全部 **23 个生产/构建文件**（9 个公共头、11 个 .cpp、2 个私有 .hpp、CMakeLists.txt），含所有头内实现；另核全部测试注册/用例名、关键失败注入与替身正文。完整 41 文件清单见 [coverage.md](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/coverage.md)，哈希见 [evidence-index.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/evidence-index.json)。未将“测试正文定向精读”夸大为每行测试均已人工审计。

| 生产文件/功能 | 本轮覆盖与结论 |
| --- | --- |
| DiagCodes.hpp/.cpp | 注册/查找/废弃/manifest/全内置表；D04 的 authority 输入、D05；注册期单线程和指针稳定性边界已核。 |
| Factory.hpp/.cpp、ParamSchema.hpp/.cpp | create/translate/异常与参数；T24 修复有效；D04/D12；类型/量纲责任见 S04。 |
| Catalog.hpp/.cpp、EntryDetail.hpp | 身份/去重/关系/订阅/容量/快照/导出/清理；D07/D08，C02、S01/S03。 |
| Confirmable.hpp/.cpp | FindingId/摘要/绑定/状态/token/过期/环境与日志借持；D06，S02；真实 project 编排另评边界。 |
| Aggregation.hpp/.cpp | CauseLink 严格遍历、聚合容差遍历、键/计数/排序/成员保留；D09；relatedTo 不强加无环规则，supersedes 保留旧条目。 |
| Logging.hpp/.cpp | 队列/并发配置/采样节流/投影/文件/关闭/重放；D03/D10/D11/D13/D14/D15，C04。 |
| Redaction.hpp/.cpp、CrashReport.hpp/.cpp | 全规则、路径策略、hash、降级、并发快照、输出格式/计数/失败；D02/D11，C01/C03/C05，重复文件限制为可选项。 |
| Errors.hpp/.cpp、Diagnostics.cpp、CMakeLists.txt | 错误码/token、异常包装、空 TU、目标/依赖/测试注册与 _report 行为；未新增缺陷，空 TU 可选删除。 |

实际构建：`cmake --build build --config Release --target sdurws_ird_diagnostics_test` 与对应 `sdurws_ird_diagnostics_contract_test`，均成功，日志为 [build-test.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/build-test.log)、[build-contract.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/build-contract.log)。没有重配 D: 源码。探针为独立 CMake 小项目，链接本 C: diagnostics/core/testkit 产物；源码 [probe.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.cpp)、[CMakeLists.txt](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/CMakeLists.txt)。复现命令见 [commands.ps1](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/commands.ps1)。

| 执行组 | passed | skipped | failed | NOT RUN | 证据/结论 |
| --- | ---: | ---: | ---: | ---: | --- |
| diagnostics 全量基线（一次） | 123 | 0 | 0 | 0 | [diagnostics.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/diagnostics.log)、[diagnostics.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/diagnostics.xml)、[diagnostics.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/diagnostics.ird-test-report.json)；PASS |
| diagnostics contract 全量基线（一次） | 26 | 0 | 0 | 0 | [contract.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/contract.log)、[contract.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/contract.xml)、[contract.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/contract.ird-test-report.json)；PASS |
| 首批隔离探针 | 1 | 0 | 11 | 0 | [probe.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.log)、[probe.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.xml)、[probe.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe.ird-test-report.json)；FAIL，含冲突/限制和一个撤回的过强 UTF-8 断言 |
| 定向装配/确认/关闭/聚合/UTF-8 | 0 | 0 | 6 | 0 | [probe-extra.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.log)、[probe-extra.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.xml)、[probe-extra.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-extra.ird-test-report.json)；FAIL |
| 过期查询 terminate 证明＋采样 | 1 | 0 | 1 | 0 | [probe-final.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-final.log)、[probe-final.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-final.xml)、[probe-final.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-final.ird-test-report.json)；terminate 证明 PASS，采样反例 FAIL |
| 注册非法枚举 | 0 | 0 | 1 | 0 | [probe-registry.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-registry.log)、[probe-registry.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-registry.xml)、[probe-registry.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-registry.ird-test-report.json)；FAIL |
| 合法实例级别/真实目录前置补强 | 0 | 0 | 2 | 0 | [probe-valid.log](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-valid.log)、[probe-valid.xml](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-valid.xml)、[probe-valid.ird-test-report.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/diagnostics-review-20261028/probe-valid.ird-test-report.json)；FAIL，排除 D04 输入干扰 |

合计现有基线 **149/0/0**；隔离探针 **2 passed / 0 skipped / 21 failed，共 23 项**。原始失败保留，不将探针失败数当成缺陷数，也不把 gtest 的 decisive=true 解释为设计通过。所有 filter 都实际匹配非零项。UTF-8 strict decode 是额外脚本检查，结果 FAIL，未混入 gtest 计数。未运行 `_report` 以免重复现有用例。

**测试有效性判断**：

- StableCodeRegistryTest 真实校验 builtin、重复/键冲突、seal/deprecated/manifest/F618/T24；没有非法 category 反例和实例 level 错配，故基线全绿不能排除 D04/D05。
- CatalogFactoryTest/CatalogLifecycleTest/CollaborationQueryTest 覆盖普通去重、链、容量、失败保留、查询；未覆盖“新条目指向即将淘汰根”、逆序追加、无 task 同 subject 跨 revision。DT-AT 是形状/观测点，不是整个 AT 的端到端验收。
- AggregationTest 有独立预期与任务分区反例，但未覆盖“同根因且不同任务”的键优先分支。新探针预期手工给定。
- ConfirmableTest 的环境/日志是可控替身，默认过期/绑定负例有效；未测 sink 抛错与终止查询。ConfirmableProjectStubTest、CatalogHostTraceStubTest 明确是桩，不能当真实 project/UI/宿主持久化证明。
- LoggingTest 的五个 DiagRetention 加原多代轮转均使用真实临时文件系统；写失败测试确有 append throw/flush false 命中计数。`DtLog3_WriteFailureInjectedStaysNonBlockingAndBounded` 的 flush false 是文件失败，未触发慢操作/queue deadline 超时。现有采样输入是 N=3、7 条，没覆盖最后恰为 N 倍数。
- 日志并发例 4 线程×150 行、join 后 flush，能证明这一输入下 FIFO；没有为“线程永久挂死”提供外部终止条件，不能称全部生命周期压力验收。溢出闸门有有界等待；本轮闸门固定 in-flight，延迟探针每个 I/O 有界，terminate 用 gtest 子进程退出码。
- RedactionTest 确实用 applyRules 抛错虚缝触发失败整条替换，磁盘错误走真实失败分支；没有覆盖 JSON 引号键/key 与装配缺口。所谓崩溃测试是正常调用 writer，并非真正 SEH/CRT 崩溃。
- DiagContractCheckTest 消费的 testkit 句法/subject/字段与量纲谓词是独立代码，不是直接调用 Factory 生成相同期望；但双方共享 core 数据类型，且 testkit 不查稳定 registry 或 record.level，正例“双过”不证明文案权威和级别一致性。
- BuildRedLineTest/LinkageContractTest/ScriptedDoubleBoundary 是源码/链接边扫描，不冒称运行证据。产品库不链 testkit，替身不进产品。

**NOT RUN（不混列为 SKIP/PASS）**：本轮独立 smoke、全库门禁/DeepEvidence、真实 GUI/worker IPC 日志链、SEH/abort/watchdog、ASan/TSan、真实符号链接夹具、永久阻塞文件 I/O、最坏正则/大输入性能、故障分配注入。当前没有生产改动，不机械重跑这些未受影响入口。

## 10. 最小修复工作包（按根因合并，尚未实施）

各包实施时沿现有任务/复用凭证绑定具体文件和送验 SHA；只重验实际影响链。下列回归应转为正式测试时才修改正式测试，本轮仅保留隔离反例。

| 包/问题 | 保留、删除、适配与 owner | 接口/回滚范围与必要验证 |
| --- | --- | --- |
| W1 稳定元数据边界：D04/D05 | 保留 descriptor 唯一权威、core 数据及既有注册表；删除实例可漂移与非法枚举通路；diagnostics 负责边界校验，policy 等直接生产者只做薄适配，不能新建严重度表。 | 公共字段/codec 不变；D04 拒绝或规范化的合同口径先明确。成组回滚 Factory/DiagCodes 与实际消费者修改。验证 registry 正负例、两向 level 错配、policy 输出、runtime Warning/Error 发布对端。 |
| W2 安全出口：D01/D02/D03 | 保留唯一 RedactionService、两 Tier、有限失败链；删除公开 channel 能自取的可信旁路及自由字段漏处理；ui 接齐装配，diagnostics 收紧规则与编码。 | 优先无公共 API 改动，可信内部标记限实现层；整组回滚装配/Redaction/Logging 与新回归。真实临时磁盘核两 Tier、工厂异常/参数、worker、export、crash，注入脱敏失败证明无原文与无回环。 |
| W3 确认通知事务：D06 | 保留 core finding、binding/token、终态唯一；删除锁内不受控通知与日志异常改变调用结果；diagnostics owner，project 只消费既定结果。 | 不改变状态机或 principal 权限。回滚 Confirmable 转换/通知边界。验证抛错 sink、查询过期子进程、并发确认/拒绝与生命周期；S02 回收公共入口另提缺口，不夹带实现。 |
| W4 目录与聚合：D07/D08/D09 | 保留正式 record、原关系图与容量护栏；删除待追加引用遗漏、无序投影、根因键漏分区；diagnostics owner。 | 无公共键升级；C02 暂不进入。回滚 Catalog/Aggregation 与定向测试。验证三关系保留、软溢出、逆序/同钟/过滤前 N、同根因跨任务与成员明细。 |
| W5 有界日志关闭：D10 | 保留单线程队列/原 FileOps/借持生命周期；删除调用线程越过 deadline 同步冲刷，落实排空预算；diagnostics owner。 | 局部 flush 屏障可先修；不可中断 OS I/O 的关闭方案若要改接缝/公共承诺须先裁决，不 detach。回滚 shutdown/flush 内部状态；有界 gated 慢 I/O、超时返回、排空计数和退出生命周期回归。 |
| W6 Windows 文件打开：D11 | 保留 T16 轮转/同实例锁/错误轨；仅把路径打开适配至 Windows 宽 API；diagnostics owner，不改框架。 | 公共 API 无变化，回滚 FileOps 打开点。C: 隔离中文/空格/emoji 路径真实日志与 writer；T16 五例作为相同消费链回归，勿重写轮转。 |
| W7 UTF-8 摘要：D12 | 保留各摘要用途与现有标记，删除不安全字节切割/可复用重复 helper；diagnostics 私有单点。 | 不擅定 C03 总字节预算；回滚 Factory/私有 helper/用例。独立严格 UTF-8 验证 2/3/4 字节边界，确认异常来源/原因保留。 |
| W8 日志顺序与观测：D13/D14/D15 | 保留 stream/采样/overflow 设施，删除 final 后缓存重放、过时 tail、误导统计；diagnostics owner。 | 公共帧语义不变，不偷偷解决 C04 策略冲突。回滚 Logging 对应三支。定向重放/采样边界、全 Error 与 Debug 溢出计数、禁用日志不影响正式诊断。 |

后续普通产品修复按 AGENTS §4 执行受影响目标/相关测试/ird_gates/复用检查；公共接口/跨单元接线扩大到相应消费者和必要独立 smoke，不能默认全库重编。现有树级阻断直接引用原件：POL-T13 与 EX-T11 的 admissionGate 登记 runtime/CMakeLists.txt 冻结证据绑定及隐藏 COVERAGE 结案债，不能把其独验 pass 写成全局准入 PASS。本轮未修改或反复运行这些门禁。

## 11. 前序单元改动的实际影响

| 前序范围 | 本轮相关事实与影响（不是重审整个单元） |
| --- | --- |
| core / CORE-T12 | [CORE-T12-20261027-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/CORE-T12-20261027-r1.json) reviewed `63e2b6305`、pass；身份/事件修复已在本库组合内。diagnostics 继续使用 core 值契约、ContentDigester；D04 是 descriptor→实例的一致性遗漏，不能归因于 core make 默认 Error 本身错误。core 事件回收改进不能自动证明 Catalog 自己的裸指针订阅安全。 |
| testkit / TK-T13 | [TK-T13-20261027-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/TK-T13-20261027-r1.json) reviewed `7a941b988`、pass；现有监听器产生本轮七组 XML/原生 JSON，已按字段复核计数。ContractCheck 是独立谓词但无 registry 语义，不能用“双过”自证 D04/D05。 |
| project / PRJ-T19 | [PRJ-T19-20261028-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/PRJ-T19-20261028-r1.json) reviewed `59af1f9b4`、pass；提交事实/恢复/归档修复不改本轮所见 S4–S6 确认编排。真实服务仍有两次复核和编译失败不提交；diagnostics 替身未代验它，也没有据 D06 推翻其独验。 |
| evidence / EV-T13 | [EV-T13-20261027-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/EV-T13-20261027-r1.json) reviewed `4afdc201a`、pass；evidence 继续拥有结果判定和 envelope 诊断留痕。D07–D09 属会话目录/呈现问题，不改已持久化 envelope 或工程 Verdict；本轮未重审其全部证据门禁。 |
| runtime | 本地最近相关变更为 POL-T13 移除 runtime CollisionSession 与旧 proximity 边；后续 RNE/FD/SVD 迁移不是本轮诊断算法替换理由。当前 runtime builder 按 record.level 拒绝 Error，是 D04 必要对端验证面；未因日志/目录问题重跑数值核。 |
| policy / POL-T13 | 六个生产码与范围拼写已修，当前 registry 正确收编，避免重复旧发现。实际直接构造 DiagnosticRecord 未传 level 的生产路径见 D04，若工厂收紧必须同包核查；未把这一点冒称已由 POL-T13 覆盖或否定其既有数值验收。 |
| execution / EX-T11 | 起始 HEAD 就是实施 `4284e8e05`；评审中新增 [EX-T11-20261028-r1.json](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/doc/industrial-robot-design/traceability/acceptance/EX-T11-20261028-r1.json)、reviewed 同 SHA、verdict pass，admission 仍 fail-registered-inherited。身份/IPC/生命周期修复未改 diagnostics 源码。生产搜索未见 replayWorkerBatch 消费，D13 设施反例不能宣称已贯通真实 worker 日志。正式诊断通道与日志回传应继续区分。 |

## 12. 下一单元 ui 的必要提醒

1. 首先处理 D01 装配：用原工厂、原管线和同一服务，覆盖 harness/plugin 两入口，普通日志必须在首条生产记录前接好脱敏。
2. 对 S01 定向核实目录通知→MarshaledDiagObserver→退订/析构的生命周期；只保护已经排队的 Qt lambda 不足以证明源端裸回调安全。需要真实 Widgets 对端时按 VS x64、`QT_QPA_PLATFORM=windows`、绝对路径、每次一个 exe 验证；本轮未执行。
3. 呈现只读投影/稳定文案键，不复制码表/严重度/确认判定，不显示 principal、原始异常或 payload；安全导出继续经过既有服务，C01 默认策略先裁决。
4. 区分真实 project 交互端口与 ConfirmableProjectStubTest；关闭/拒绝/取消/失权不得被 UI 合并为“确认成功”，确认后仍由 project 硬断言、编译与提交。
5. 当前源码指示 finding/worker 日志设施尚无真实消费者；需要补接时先明确 owner 与实际协议，不能用接口存在、源码扫描或桩结果标成端到端完成。

本轮仅生成评审与隔离证据，没有修复、提交、推进 ui、改任务状态或自签独立验收。在所列版本、范围和已执行验证内，未发现其他问题。
