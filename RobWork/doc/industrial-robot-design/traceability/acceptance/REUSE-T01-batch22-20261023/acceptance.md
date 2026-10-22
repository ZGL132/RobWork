# REUSE-T01 B22 限定独立验收

冻结提交 `daf0dcd175fb76c956154fe0213f2d791dad8575`，基点 `cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7`。结论：接受 B22 的审计登记与反例证据。该结论不代表产品测试全绿，不授予全 REUSE-T01、T02 或 T06 准入。

验收在本目录的独立冻结副本执行，共享现场只读；没有实施、改产品、提交、推送、合并、远程同步或继续委派。冻结副本 HEAD 匹配且工作区干净。diff 共 20 路径，均在契约允许面，产品/框架/vcpkg 无 diff。

- 已读 Admission.cpp 与 RunRegistry.cpp 的 14+12 个有体定义及内部归档退避 lambda；与 batch22-definitions、functions/execution.json 一一核对。既有 41 行保持逐字段相同，新 26 行 decision/status 均 pending。26 个源码区段、390 个全文输入/审计证据摘要全部匹配；完整源、公共头、原测试、报告 main、Digest/Verdict/Envelope/ArchiveServiceImpl/CMake/DiagCodes 均绑定。
- 真复用链已核：SHA-256 进入 core::ContentDigester；ResultAdmission 组装输入并消费 evidence::aggregateVerdict 返回值，调用绑定快照的 validateCombination，再由 ResultEnvelope::make 构造（make 自身重验上下文无关组合）。归档经 project 端口 begin/writeBatch/finalize/abandon。登记只校验身份/归档材料存在性，不宣称完整材料语义校验；调度线程单写者与借用端口契约未改变。
- 名称反解使用登记 record.nameMapIdentity，不按当前 HEAD 或当前映射自行拼剥名称。原夹具是真 ProjectStore，解码器、名称反解器和注册表含替身；因此不把原 25 项当作真实 worker/runtime 装配证明。本地 cause/action 文案仍 pending，不因使用 DiagnosticRecord 工厂而认定文案权威已收口。
- UTIL-THREAD 补入 TimerUtil.cpp、ThreadTask.cpp 及 sdurw_common 目标绑定，仍 candidate。ThreadTask 是线程父子任务和异常传播；TimerUtil Windows 使用 clock，Unix 使用 gettimeofday，头明确不可用于日历时间。候选不接管业务登记、evidence 或 project owner，不新增模型/State 权限。
- 五组检索在冻结副本重新执行，退出码依次 0/0/0/2/0。不存在的 StableCodeRegistry.cpp 原误猜 exit2 保留；DiagCodes.cpp 重查成功，不隐藏检索失败。

真实验证：VS 2022 x64 开发环境，原 probe CMake、原 core/testkit/evidence/project/execution 目标、原 RunRegistryAdmissionTest.cpp include 夹具、原 TestMainReport.cpp；生成的 vcxproj 确认生产源来自冻结副本原目标，未复制源集。fresh configure/build 均 exit0。单绝对路径模型 exe 运行 `RunRegistryAdmissionTest.*`：原 25=25 passed/0 failed；新增 2=1 passed/1 failed；总 27=26 passed/1 failed/0 skipped/0 notRun，exit1。新生成 JSON/XML 的 27 个用例身份和结局逐项一致，也与送验报告一致。

F-964 保持 major/open：RunRegistry.cpp:314 在校验 cause 前写入 terminationCause；Completed 分支随后抛错。真实反例发现标记残留（source 证明所写即 Completed），currentState 仍 Running，下一次 Canceled 因已有标记抛错；健康取消及二次终结拒绝正例通过。接受的是缺陷证据的真实性，不是接受带缺陷的生产行为；后续原写入口与原回归修复归 REUSE-T05。

`verify-task` 真实执行 3 条机器命令通过（validate-task / validate-reuse / validate-docs）；范围检查 20 路径通过；commit-lint 仅该新增提交通过。机器输出中 pending 债务和 NOT AUDITED evidence 保留，不转述为算法合规。未重跑其他 execution 套件、IPC/强杀/worker、并发/性能、GUI、全库深证据或远程 CI。

环境过程原样保留：最初额外指定 vcpkg toolchain 的配置尝试因上游 RobWorkConfig 的 C:\\local hint 被该路径解释为非法转义失败（logs/configure.log，exit1）。随后使用送验原配置的 prefix 路径与 RobWork_DIR、IRD_SOURCE_DIR，在全新 build-original 目录配置成功；未修改上游、vcpkg 或 probe。该首试不计为成功构建。

复现命令与日志清单位于 acceptance.json；原始 fresh 输出在 logs/，源码绑定复核脚本为 check-bindings.ps1。所有验收产物仅写入本 Temp 目录。
