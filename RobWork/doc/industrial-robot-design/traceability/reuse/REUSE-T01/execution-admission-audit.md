# B22：运行登记与接纳边界审计

本批审读Admission.cpp/RunRegistry.cpp全部26个有体定义，逐项登记精确区段及全文输入；不把文件级登记冒充execution整体覆盖。Controller/调度/worker仍需后续审查，已读部分不提前授予结论。

复用证据：sha256Hex真正委托core::ContentDigester，只做本地hex；ResultAdmission按登记事实构EvidenceManifest并调用evidence::aggregateVerdict、快照绑定validateCombination及ResultEnvelope::make，不自写包络/五级规则。归档只通过project端口begin/writeBatch/finalize/abandon，不另开持久化写路；名称反解委托登记nameMapIdentity，不拿当前HEAD/映射替代。稳定码在diagnostics/DiagCodes.cpp登记，但本地中文cause/action仍留待文案权威审查，不能因用了core工厂就认定纯净性全部合规。

上游候选检索/输出见batch22-search.json（误猜StableCodeRegistry.cpp不存在的exit2也保留，随后已定位DiagCodes.cpp重查成功）。ThreadTask实际为线程父子任务与异常传播，非业务五元组/包络/项目归档或worker进程隔离。TimerUtil在Windows用clock、Unix用gettimeofday，头明确不可作日历；不能替代注入UTC的manifest日期或以sleep代替成功判据。不为复用而把登记/证据/project owner塞进上游线程对象。现有UTIL-THREAD仍candidate；新增两个实现文件绑定补强限制，不授生产正向证据。

探测沿原core/testkit/evidence/project/execution CMake目标，直接include原RunRegistryAdmissionTest.cpp夹具并使用原TestMainReport.cpp，未复制生产源集。VS x64 fresh配置与Release构建exit0。单绝对exe实际27项：原25项全通过；新增健康取消1通过、拒绝Completed后状态保持1失败；总26passed/1failed/0skipped/notRun、exit1，JSON/XML保留。原夹具使用真实ProjectStore，解码器/名称反解器为替身，不证明真实worker通道和runtime接线。没有执行其他execution套件、IPC/进程杀死/并发/性能或GUI。

F-964：RunRegistry::markTerminated先给terminationCause赋值，遇Completed再throw；调用方捕获异常后登记仍残留Completed，下一次合法Canceled被“已带终结标记”拒绝，而currentState仍Running。新增用例真实核上述两个断言，健康取消/二次终结拒绝正例正常。后续T05在原写入口先校验允许终结原因再写一次，并把反例落原测试；复用现有core词表/登记表，不新建状态机、恢复补丁或绕路取消。当前缺陷保持open，不提前修产品。

登记构造负责身份/归档材料存在性，非全材料语义验证；classifyArrival身份/绑定/允许类型/终结拒绝，evidence继续承重。admitResultBatch用已成功字节指纹判重、到达序命名，非通道显式批次身份；跨进程幂等/取消归档所有分支未以本次25例宣称穷尽。runArchiveTransaction有界重试、A7重取/端口重绑定与责任终结；异常、资源释放/锁序和配置极值仍需按真实调用链复核。未引新算法，无产品/上游/vcpkg修改，无全T01/T02准入。
