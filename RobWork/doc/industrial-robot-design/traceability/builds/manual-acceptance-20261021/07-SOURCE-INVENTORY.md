# 全软件实现面与源码入口

核对基线`4adc29f1bec2847c64729a13ec45ccc25a675305`。以下覆盖20个产品单元，另列testkit_qt辅助设施。

本目录按真实源码组织，**公共头是能力面索引，不证明其中每个声明均有完整产品闭环**；DTO、接口、适配器和测试设施不能当作终端用户按钮。实现文件数量仅帮助定位，不是功能完成度统计。

## 单元目录

|单元|当前源码能力面|实现.cpp数（排除测试）|人工验收归属|
|---|---|---|---|
|core|身份与摘要、SI单位换算、来源/字段状态、工程判定及比较数据；通过单位、输入、身份与报告验收。|9|[检查清单](05-PROJECT-SHARED.md)|
|testkit|黄金数据、容差、故障注入、运行报告和进程工具；开发验证设施，不是终端用户模块。|10|[检查清单](05-PROJECT-SHARED.md)|
|project|对象库、不可变修订、命令事务、撤销重做、草稿、另存为、包、资源固化及归档。|16|[检查清单](05-PROJECT-SHARED.md)|
|evidence|输入快照、切片、兼容性、证据封装、必验覆盖、当前性和正式判定。|9|[检查清单](05-PROJECT-SHARED.md)|
|runtime|权威模型规范化、WC/DWC编译、名称映射、基座世界变换、缓存、碰撞会话和显示投影。|14|[检查清单](05-PROJECT-SHARED.md)|
|policy|工程策略、版本兼容、按修订解析、关节边界、碰撞域/规则与RobWork检测适配。|12|[检查清单](05-PROJECT-SHARED.md)|
|diagnostics|稳定诊断目录、字段schema、确认型提示、汇总日志、异常报告与脱敏。|11|[检查清单](05-PROJECT-SHARED.md)|
|execution|任务准入/状态机/调度、worker监督、协议、进度事件、取消、缓存和检查点。|13|[检查清单](05-PROJECT-SHARED.md)|
|io|CSV/JSON、资源读取、预算、安全路径、临时区、原子写出和ZIP封装。|11|[检查清单](05-PROJECT-SHARED.md)|
|modeling|模型结构、双权威、导入、几何、工具/TCP、物性、场景、耦合、安装、命名姿态和导出。|38|[检查清单](02-MODELING.md)|
|requirements|工位/姿态规则/段、区域采样、工况、导入导出、模板/派生及分层就绪。|30|[检查清单](01-REQUIREMENTS.md)|
|kinematics|FK/Jacobian/指标、IK、批量、区域/工作空间/外包络/独立位姿内核、配置与结果。|28|[检查清单](03-KINEMATICS.md)|
|trajectory|PTP、笛卡尔段、避障、简化平滑复检、时间化、连续工艺/速度、播放与导出。|22|[检查清单](04-OTHER-MODULES.md)|
|dynamics|逆/正动力学、序列、功率能量、峰值/RMS包络、多工况、曲线回放与证据。|15|[检查清单](04-OTHER-MODULES.md)|
|drivetrain|类型化旋转/直线传动、效率、速比/导程、惯量与能量映射及codec。|4|[检查清单](04-OTHER-MODULES.md)|
|selection|版本目录校验/差异、能力曲线、硬筛选、组合校核、偏好、可行集及回填。|16|[检查清单](04-OTHER-MODULES.md)|
|optimization|变量绑定/补丁、预检、候选生成、硬约束、Pareto、分层联合运行、采用与导出。|19|[检查清单](04-OTHER-MODULES.md)|
|reporting|结构化章节、身份、HTML/JSON/CSV、限定语、证据包、归档、一致性与往返复算。|14|[检查清单](04-OTHER-MODULES.md)|
|ui|主窗口、菜单命令、布局、树/检查器/选择、参数表、单位配置、策略/任务/状态及三维网关。|42|[检查清单](05-PROJECT-SHARED.md)|
|workflow|会话生命周期、关闭监督、草稿/方案/报告命令、阶段门控、建议、设置及回填翻译。|15|[检查清单](05-PROJECT-SHARED.md)|

testkit_qt：Qt测试主程序/夹具辅助，不作为终端用户功能单独计数。框架只读区的通用机器人算法/插件不一概作为本工业机械臂产品已交付功能；按正式程序已暴露入口验收。

## 公共能力面逐头索引

标题来自头注释，必要时点击查看契约和实现；不把空接口当作已实现功能，也不把只有单元测试的能力误标成正式宿主已装配。

### core

|公共头|头注释摘要|
|---|---|
|[Compare.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Compare.hpp)|容差与比较——Tolerance（附录 D C4 公式）＋allCloseWithin＋C7 转写。|
|[DiagData.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp)|公共诊断数据契约——DiagnosticRecord/ComparativeFields/ConfirmableFinding。|
|[Digest.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Digest.hpp)|摘要类型——Digest256/ContentVersion/ContentIdentity＋SHA-256 增量摘要器。|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Errors.hpp)|CoreError——core 单元唯一异常类型（错误表达契约的异常轨载体）。|
|[Evaluation.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Evaluation.hpp)|基础评估语义词表——EvaluationMode/TaskOutcome/EngineeringStatus/TaskState。|
|[Events.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp)|共享事件与端口基础契约——四类领域事件＋sink/订阅/总线接口（⑤端口归 core）。|
|[Identity.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Identity.hpp)|身份基础类型——六个 Id128 强类型＋AttemptId＋TaskIdentity 五元组。|
|[Provenance.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Provenance.hpp)|值来源与缺失值——ValueProvenance（来源标记）＋SourcedValue<T>（四态字段值）。|
|[Units.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Units.hpp)|数量、单位与数值基础契约——QuantityKind/UnitToken 注册表/唯一换算入口/|

### testkit

|公共头|头注释摘要|
|---|---|
|[Check.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Check.hpp)|数值断言与比较详情——CheckResult/checkCloseWithin/checkAllCloseWithin/|
|[ContractCheck.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ContractCheck.hpp)|契约通用断言——诊断记录/比较型三要素/任务身份/身份唯一性/文件完整性/|
|[Dataset.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Dataset.hpp)|黄金数据集清单模型与装载——DatasetManifest/GoldenDataset＋完整性校验。|
|[Fault.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Fault.hpp)|故障注入原语——计划/命中观测（FaultPlan/FaultLog）＋拦截器适配模板。|
|[Fixture.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Fixture.hpp)|测试夹具与确定性环境（TempDir／ReproRecord／DeterministicEnv）。|
|[AssertMacros.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/AssertMacros.hpp)|gtest 适配层——IRD_EXPECT_* 宏（§5.3.3；仅在消费方 _test 目标的|
|[GoldenFixture.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/GoldenFixture.hpp)|黄金数据集夹具的 gtest 集成形态（§6.1 夹具生命周期六步）。|
|[RecordListener.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp)|TestRecordListener——gtest 事件到 TestRecord/ird-test-report.json 的|
|[JsonLite.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/JsonLite.hpp)|JsonLite——测试侧受限 JSON：值模型、严格解析（行列定位）与确定性序列化。|
|[PerfBenchmark.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/PerfBenchmark.hpp)|性能基准计时统计夹具（PerfBenchmark）——P95 分位数采样与交互|
|[ProcessRunner.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ProcessRunner.hpp)|进程测试支撑——进程级注入原语（Job Object 进程树终止＋事件等待）。|
|[Report.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/Report.hpp)|机器可读测试结果（TestRecord／聚合 Report／进程级登记表）。|
|[ReproBenchmark.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ReproBenchmark.hpp)|确定性复现基准夹具（ReproBenchmark）——同输入＋版本＋配置＋线程＋|
|[SetCheck.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/SetCheck.hpp)|稳定集合与顺序断言——一一配对（匹配）语义＋稳定排序逐位一致（TK-T06）。|
|[TestPaths.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/TestPaths.hpp)|黄金数据根定位——环境变量/编译期默认两级解析（testkit 首个公共头）。|
|[ToleranceProfile.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/ToleranceProfile.hpp)|容差档案（ToleranceProfile）——档案解析、条目校验与 fieldPath 解析。|

### project

|公共头|头注释摘要|
|---|---|
|[ArchivePort.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/ArchivePort.hpp)|归档端口（IResultArchivePort）——任务运行结果/检查点写入|
|[CommandService.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/CommandService.hpp)|命令端口（ProjectCommandService，①端口所有者）——唯一写路径|
|[DraftService.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/DraftService.hpp)|草稿服务端口（DraftService，⑤号服务）——未应用草稿的落盘/恢复/|
|[PackageService.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/PackageService.hpp)|另存为与项目包服务（ISaveAsService/IPackageService）——另存为|
|[PersistenceFormat.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/PersistenceFormat.hpp)|磁盘格式读取类型（PersistenceFormat）——.rwdesign 存储自有格式的|
|[ProjectStore.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/ProjectStore.hpp)|存储上下文（ProjectStore）与工厂（ProjectStoreFactory）——打开/|
|[QueryPort.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/QueryPort.hpp)|查询端口（IProjectQueryPort，②端口所有者）——修订/元数据/分支/|
|[ResourceRefs.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/ResourceRefs.hpp)|外部资源引用与固化（ResourceRefs）——引用保护记录|
|[StoreTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/StoreTypes.hpp)|project 存储层公共值类型（增量落位）——锁持有者记录与诊断 sink 契约。|
|[UndoRedo.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/project/include/sdurws/ird/project/UndoRedo.hpp)|撤销/重做服务（UndoRedoService）——"撤销/重做＝逆命令提交产生|

### evidence

|公共头|头注释摘要|
|---|---|
|[Compatibility.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Compatibility.hpp)|缓存/检查点兼容判定（Compatibility）——正式缓存命中、诊断性读取|
|[Currentness.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Currentness.hpp)|当前性纯投影（ResultCurrentness）——历史结果相对目标上下文是否|
|[Dependency.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Dependency.hpp)|依赖表达模型——DependencyKey/Kind、依赖声明、适用条件、切片条目|
|[Envelope.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Envelope.hpp)|结果包络（ResultEnvelope）——运行终结时唯一正式结果对象的构造|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Errors.hpp)|evidence 错误契约——EvidenceErrorCode 全表（稳定 token）＋EvidenceError。|
|[Evaluator.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Evaluator.hpp)|评估器接口与注册表（③评估器端口所有者）——评估键/descriptor/|
|[Evidence.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Evidence.hpp)|证据 Profile 与证明契约承载——证据项状态与证据清单（§6.2）、|
|[Slice.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Slice.hpp)|输入切片（InputSlice）——评估实际消费依赖的冻结清单与双层内容|
|[Snapshot.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Snapshot.hpp)|正式分析快照（AnalysisSnapshot）——评估输入的不可变闭包载体：|
|[Verdict.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/include/sdurws/ird/evidence/Verdict.hpp)|工程判定汇总（Verdict）——五级优先级决策表的可执行实现（§6.4）、|

### runtime

|公共头|头注释摘要|
|---|---|
|[Adapter.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Adapter.hpp)|RobWork 适配层公共面（§8.2～§8.4）——WorkCell 只读视图与|
|[BaseWorldTransform.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/BaseWorldTransform.hpp)|基座—世界变换规则（§6 单一不变量的规则面）——安装预设、|
|[CacheKey.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/CacheKey.hpp)|编译缓存纯判定——CompileCacheKey 分层键（WC/DWC 两层）＋|
|[CanonicalModel.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/CanonicalModel.hpp)|CanonicalModel——从修订对象闭包确定性编译出的不可变 SE(3) 关节链|
|[Codec.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Codec.hpp)|RT-Codec——CanonicalModel 的 canonical 二进制编码（§4.5 原文契约），|
|[CollisionSession.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/CollisionSession.hpp)|运行时真实碰撞设施（MA-02——主线功能审计 20261021 立项批）。|
|[Compiler.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Compiler.hpp)|编译链公共契约面——CompileOptions/CompileRequest/CompileStatus/|
|[Description.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Description.hpp)|RobotDesignDescription——注入输入的中性值类型（§4.2 原文契约）＋|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Errors.hpp)|runtime 错误契约——RuntimeErrorCode 全表＋RuntimeError＋Expected<T,E>|
|[HostPresentationView.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/HostPresentationView.hpp)|宿主呈现视图（HostPresentationView）——已应用 WorkCell 的只读|
|[NameMap.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/NameMap.hpp)|RuntimeNameMap——ObjectId ↔ RobWork 设备作用域全名 的唯一双向解析器|
|[Resource.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Resource.hpp)|资源引用值类型——ResourceState＋ResourceRef（§8.6 原文契约的值类型部分）。|
|[Snapshot.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Snapshot.hpp)|RuntimeSnapshot——编译链唯一发布产物（§9.1 字段表）；并发只读|
|[Sources.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/Sources.hpp)|runtime 注入最小契约——修订/对象字节只读来源与编译取消令牌|

### policy

|公共头|头注释摘要|
|---|---|
|[CollisionEvaluator.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/CollisionEvaluator.hpp)|碰撞评估唯一实现——CollisionScene 场景模型、会话构建（作用域展开/|
|[CollisionQuery.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/CollisionQuery.hpp)|碰撞评估的查询与输出类型——CollisionQueryKind/CollisionQuery/|
|[Compatibility.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/Compatibility.hpp)|策略兼容判定（Compatibility）——消费方要求 × 已发布策略的纯函数|
|[Contexts.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/Contexts.hpp)|注入用最小接口——policy 对 runtime/project/宿主能力的零编译依赖|
|[Diagnostics.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/Diagnostics.hpp)|policy 诊断构造——建议码表（policyDiagCodes）＋IPolicyDiagnostics|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/Errors.hpp)|policy 错误契约——PolicyErrorCode 全表（稳定 token）＋PolicyError。|
|[JointLimits.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/JointLimits.hpp)|关节限位与行程阈值评估契约与唯一实现——JointLimitSpec/JointLimitQuery/|
|[PolicyInput.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/PolicyInput.hpp)|策略原始输入（RawPolicyInput）与策略编码器（PolicyCodec）——|
|[PolicyParsing.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/PolicyParsing.hpp)|策略解析器与校验器——resolvePolicy 七段管线、IPolicyValidator|
|[PolicyPort.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/PolicyPort.hpp)|④策略端口（ARCH §7.2④）——IPolicyProvider 契约、|
|[PolicySet.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/PolicySet.hpp)|EngineeringPolicySet 数据模型——策略对象类型、碰撞规则与关节阈值|
|[SystemDefaultPolicy.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/SystemDefaultPolicy.hpp)|系统缺省策略集与其 ④ 端口供给器（宿主装配的缺省策略源）。|

### diagnostics

|公共头|头注释摘要|
|---|---|
|[Aggregation.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Aggregation.hpp)|诊断聚合器——CauseLink 原因链遍历（§6.3）、聚合规则与聚合视图|
|[Catalog.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Catalog.hpp)|诊断目录与信封——DiagEntryId/DedupKey/DiagContext/DiagnosticEntry、|
|[Confirmable.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Confirmable.hpp)|可确认诊断（ConfirmableFinding）服务端设施——FindingId/绑定四元组/|
|[CrashReport.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/CrashReport.hpp)|崩溃诊断文件（CrashReport）——§7.6 写出设施：脱敏后的进程/版本|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/DiagCodes.hpp)|稳定诊断码注册表——分类/严重词表、CodeDescriptor、IDiagnosticRegistry、|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Errors.hpp)|diagnostics 错误契约——DiagnosticsErrorCode 全表（稳定 token）＋DiagnosticsError。|
|[Factory.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Factory.hpp)|诊断工厂与跨单元错误转译——IDiagnosticFactory（唯一创建入口＋|
|[Logging.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Logging.hpp)|两级日志（Logging）——LogRecord/两级分流管线/采样节流/轮转/|
|[Redaction.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/include/sdurws/ird/diagnostics/Redaction.hpp)|脱敏服务（Redaction）——路径四策略/凭据模式零记录/降级不放行/|

### execution

|公共头|头注释摘要|
|---|---|
|[CacheCoordinator.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/CacheCoordinator.hpp)|执行缓存协调器（ExecutionCacheCoordinator）——结果缓存与运行时|
|[ChannelProtocol.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/ChannelProtocol.hpp)|主进程↔worker 通道协议（IRDCHN/1）——帧布局、消息类型枚举、|
|[Checkpoint.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/Checkpoint.hpp)|检查点协调器（CheckpointCoordinator）——检查点的写出编排、|
|[Controller.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/Controller.hpp)|任务控制协议编排（EX-T03 取消/终止＋WP-21-T07 暂停/继续/暂停中|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/Errors.hpp)|execution 单元错误类型——ExecutionErrorCode（稳定 token 枚举）＋|
|[EventBus.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/EventBus.hpp)|领域事件总线（DomainEventBusImpl）——core IDomainEventBus 接口的|
|[Ports.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/Ports.hpp)|execution 的最小注入端口（§3.3）——诊断 sink、名称反解适配、|
|[RunRegistry.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/RunRegistry.hpp)|运行登记表与迟到结果两段式接纳（RunRegistry/Admission）——|
|[Scheduler.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/Scheduler.hpp)|任务调度器（EX-T05：ITaskScheduler 契约面＋TaskScheduler 调度|
|[StateMachine.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/StateMachine.hpp)|任务状态机——九态逐转移表（§5.3 T1~T14）＋守卫＋事件发布接缝，|
|[TaskTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/TaskTypes.hpp)|任务身份与记录类型——TaskId 强类型、TaskSubmission/TaskRecord/|
|[WorkerSupervisor.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/WorkerSupervisor.hpp)|worker 监督器——进程池（launch/握手/心跳/崩溃检测/退出码分类/|

### io

|公共头|头注释摘要|
|---|---|
|[AtomicFile.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/AtomicFile.hpp)|导出文件原子写出（IAtomicFileWriter）——单文件导出（CSV/JSON/|
|[Budget.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/Budget.hpp)|BudgetGuard——资源预算的维度表、规格（默认/硬上限/收紧放宽）、|
|[Csv.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/Csv.hpp)|CSV 读写器——方言标识行（v1）、可逆转义编码（唯一转义/还原实现）、|
|[IoDiagnostics.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/IoDiagnostics.hpp)|io 诊断面——IoErrorCode 的稳定码面 token、IO-\* 稳定诊断码表|
|[IoError.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/IoError.hpp)|io 单元错误契约——IoErrorCode 全表（稳定 token）＋IoError 值类型。|
|[IoFwd.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/IoFwd.hpp)|io 单元公共前向头——IoString/IoResult<T> 公共别名、IoCancelToken|
|[Json.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/Json.hpp)|JSON 读写器——受限 DOM（每值位置区间）、JsonProfile 声明式结构|
|[Package.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/Package.hpp)|.rwpack 包导入导出的 io 侧设施（IPackageImporter/|
|[ResourceIo.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/ResourceIo.hpp)|资源导入服务与外部源检测——ResourceSnapshot/ResourceContentId、|
|[SafePath.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/SafePath.hpp)|SafePath——路径角色模型、Windows 路径规范化与穿越/reparse point|
|[TempArea.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/TempArea.hpp)|临时导入/导出/固化中转目录的创建、互斥、RAII 清理与失败残留|
|[ZipChannel.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/ZipChannel.hpp)|ZIP 通道——.rwpack 传输封装（§7.1）的容器层读取：条目枚举、|

### modeling

|公共头|头注释摘要|
|---|---|
|[CanonicalBridge.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/CanonicalBridge.hpp)|CanonicalBridge——建模对象到 runtime 编译输入的桥梁（§9.1/§9.2/|
|[Codec.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Codec.hpp)|IRobotDesignCodec——建模对象 canonical 编码/解码（§9.4.9 行原文|
|[CommandHandlers.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/CommandHandlers.hpp)|建模命令处理器族——IModelingCommandHandler 基类（§9.4.8，|
|[DhConvert.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/DhConvert.hpp)|DH↔显式转换器（IDhExplicitConverter，§9.4.7）——DH 权威→显式|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/DiagCodes.hpp)|modeling 稳定诊断码工厂——MDL-* 码的 CodeDescriptor 登记数据与|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Errors.hpp)|modeling 错误契约——ModelingErrorCode 全表（稳定 token）、|
|[GeometryLinkEdit.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/GeometryLinkEdit.hpp)|几何资源引用编辑原语（UI-T48——差距清单 G3 资源选择器＋G4 引用|
|[Import.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Import.hpp)|URDF 导入字段映射与链型判定——IModelImportMapper 纯函数服务、|
|[ModelDiff.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/ModelDiff.hpp)|IModelDiffService——Model Diff 差异增量表数据实体（MDL-08）：两个|
|[ObjectTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/ObjectTypes.hpp)|modeling 对象类型登记——五对象 objectTypeToken 常量与场景对象|
|[Package.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Package.hpp)|规范模型包导出/导入（IModelPackagePort）——MDL-20 的"自有工件|
|[Parts.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Parts.hpp)|modeling 四部件对象值模型——ToolDefinition（tool-definition）/|
|[PropertyEstimation.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/PropertyEstimation.hpp)|物性估算唯一公式表（版本 mdl-property-formula/1）——连杆段元|
|[Readiness.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Readiness.hpp)|IModelReadinessChecker——模型就绪校验器（§8.2 分层检查表＋§9.4.4|
|[RobotDesign.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/RobotDesign.hpp)|RobotDesign 聚合值模型——权威参数化的业务编辑模型（§4.3 根对象|
|[StructureEdit.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/StructureEdit.hpp)|关节链结构编辑原语（§5.2 v0.28"关节链结构编辑"四操作词表的域|
|[Template.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/Template.hpp)|模板创建与参数化编辑——IRobotDesignTemplateFactory（模板清单/|
|[WorkCellReverseImport.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/WorkCellReverseImport.hpp)|WorkCell 反向导入（MDL-18/AT-33，R2/阶段 D）——.wc.xml 有损提取|
|[XacroExpand.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/XacroExpand.hpp)|Xacro 受控展开语义——IXacroExpandService 纯函数服务、|

### requirements

|公共头|头注释摘要|
|---|---|
|[Capture.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Capture.hpp)|三维拾取与 TCP 捕获的域侧消费面（§9.8 L-R6/L-R7——WP-14-T06）：|
|[Codec.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Codec.hpp)|IRequirementCodec——需求对象 canonical 编码/解码（§9.5 行原文|
|[CommandHandlers.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/CommandHandlers.hpp)|需求命令处理器族——IRequirementCommandHandler 基类（§9.5，|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/DiagCodes.hpp)|requirements 稳定诊断码工厂——REQ-* 码的 CodeDescriptor 登记数据|
|[Editor.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Editor.hpp)|IRequirementEditor——需求编辑器（§9.3 行原文契约的落位）：基线|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Errors.hpp)|requirements 错误契约——RequirementErrorCode 全表（稳定 token）、|
|[Import.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Import.hpp)|需求导入器——CSV/JSON 字段映射（字段字典自动识别＋手动改映射）、|
|[ObjectTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/ObjectTypes.hpp)|requirements 对象类型登记——五对象 objectTypeToken 常量、|
|[OrientationResolution.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/OrientationResolution.hpp)|姿态与方向规则解析（§5.3 五规则——WP-14-T06）：把规则参数解析为|
|[Readiness.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Readiness.hpp)|IRequirementReadinessChecker——需求就绪校验器（§9.5 行原文契约|
|[RequirementTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/RequirementTypes.hpp)|requirements 值模型——任务点/工作区域/工况/采样计划四类领域对象|
|[Sampling.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Sampling.hpp)|ISamplingPlanBuilder——采样计划**定义**的构建/规范化/校验与|
|[Services.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/Services.hpp)|requirements 领域服务——任务点/区域/工况三服务（§9.4 原文契约|
|[TemplateArray.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/include/sdurws/ird/requirements/TemplateArray.hpp)|ITemplateArrayService——工艺模板/工位镜像/四类批量阵列/一键重生成|

### kinematics

|公共头|头注释摘要|
|---|---|
|[AnalysisConfig.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/AnalysisConfig.hpp)|求解配置与显示单位（KIN-12/13，T10 批次）——AnalysisConfiguration|
|[Bounds.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Bounds.hpp)|解析工作半径界限（§3.3 布局表 T04 行）——连杆长度和上界的|
|[Collision.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Collision.hpp)|④端口碰撞接入（KIN-05/AT-19/NFR-COR-05）——碰撞场景的请求方|
|[Commands.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Commands.hpp)|会话姿态写入口（KinSessionPose——KIN-06/AT-04）与设默认命令门面|
|[Coverage.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Coverage.hpp)|覆盖率计算值类型（KIN-04）——CoverageTotals/CoverageResult|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/DiagCodes.hpp)|kinematics 稳定诊断码工厂——KIN-* 码的 CodeDescriptor 登记数据|
|[Envelope.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Envelope.hpp)|近似外包络估算（KIN-10，R2）——按配置方向数（默认 180，KIN-10|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Errors.hpp)|kinematics 错误契约——KinematicsErrorCode 全表（稳定 token）、|
|[Evaluators.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Evaluators.hpp)|kin.pose-metrics 评估器（KIN-01 的 IEngineeringEvaluator 形态）——|
|[Evidence.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Evidence.hpp)|批量任务点验证的值模型与证据组装器（KIN-03）——批量工作项|
|[Export.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Export.hpp)|结果导出值行（KIN-08/§7.4）——把运动学结果整理为 JSON/CSV|
|[Fk.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Fk.hpp)|FK 与位姿指标（KIN-01）——PoseMetrics 值模型、IFkEvaluator 接口|
|[Ik.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Ik.hpp)|多初值 IK 求解（KIN-02）——IkRequest/IkOutcome 值、IIkSolver|
|[KinTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/KinTypes.hpp)|kinematics 值模型公共头——TcpRef（TCP 引用值）、|
|[PoseReach.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/PoseReach.hpp)|独立位姿可达性分析（KIN-11）——位置列表×方向采样×工具滚转|
|[Recompute.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Recompute.hpp)|批量复算请求构造（KIN-08/§7.4）——"按原 slice 重新提交|
|[Render.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Render.hpp)|失败点/薄弱区三维渲染数据（KIN-07）——四类渲染数据结构、|
|[Sampling.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Sampling.hpp)|区域采样与覆盖率（KIN-04）的采样通道值模型——SamplingPlan/|
|[SolutionSet.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/SolutionSet.hpp)|解集视图（§6.2）——稳定排序/筛选/统计/最差项的**唯一定义点**|
|[Workspace.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Workspace.hpp)|工作空间探索式采样与指标可视化浏览（KIN-09/R2）——探索式采样|

### trajectory

|公共头|头注释摘要|
|---|---|
|[CartesianLine.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/CartesianLine.hpp)|笛卡尔直线段规划（§8）——接近/撤离段几何构造（§8.2 轴向解析＋|
|[Commands.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|trajectory 领域命令适配器（units/trajectory.md §15.11/§14.5.1|
|[Continuity.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Continuity.hpp)|路径连接与连续性检查（§9）——段边界几何连续（位置/姿态，附录|
|[ContinuousProcess.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/ContinuousProcess.hpp)|连续工艺段边界行为校验（WP-16-T16——TRJ-08-S3"连续（不停驻）|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/DiagCodes.hpp)|trajectory 稳定诊断码工厂——TRJ-* 码的 CodeDescriptor 登记数据|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Errors.hpp)|trajectory 域错误类型——TrajectoryError（token 前缀 trajectory/...）|
|[EvaluationDims.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/EvaluationDims.hpp)|评估维度扩展点（WP-16-T11 铺设；WP-16-T14 首次真实递进——|
|[Export.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Export.hpp)|标准轨迹导出（units/trajectory.md §15.13 Export——格式字段字典|
|[FailureLocate.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/FailureLocate.hpp)|失败段诊断定位（WP-16-T09）——把 T04~T08 各管线产出的失败素材|
|[KinematicsPort.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/KinematicsPort.hpp)|kinematics IK/FK 注入端口（IKinematicsComputePort）——trajectory|
|[PlanConfig.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/PlanConfig.hpp)|轨迹求解配置（§5.5 TrajectoryPlanConfiguration，schemaVersion=1）|
|[Planner.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Planner.hpp)|RobWork 规划器适配面（§15.5 IPathPlannerAdapter）与避障重规划|
|[Playback.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Playback.hpp)|轨迹会话域面三件（WP-16-T10——TRJ-07 领域承载）：动画播放查表|
|[ProcessSpeed.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/ProcessSpeed.hpp)|工艺速度约束校验（WP-16-T15——TRJ-08-S2"指定段（如作业段）|
|[Ptp.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Ptp.hpp)|关节空间 PTP 规划（§7）——构型选择规则（§7.5 三键全序）、逐轴|
|[Recheck.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Recheck.hpp)|TRJ-04 复检协议（§15.6 RecheckPipeline——平滑后按冻结协议重新|
|[Sequence.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Sequence.hpp)|任务序列展开（§5.4——TRJ-01"由任务点组成的有序作业序列"的领域|
|[Smooth.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Smooth.hpp)|路径简化与平滑（§15.7 SmoothPipeline，TRJ-04"对路径进行简化|
|[TimeParam.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/TimeParam.hpp)|时间参数化与节拍（§12/§15.8 TimeParameterize——TRJ-05"使用至少|
|[TrjTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/TrjTypes.hpp)|轨迹数据模型（WP-16-T04 面）——路点/段/约束/段计划的身份与|

### dynamics

|公共头|头注释摘要|
|---|---|
|[Commands.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Commands.hpp)|dynamics 领域命令适配器（units/dynamics.md §10.7|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/DiagCodes.hpp)|dynamics 稳定诊断码工厂——DYN-* 码的 CodeDescriptor 登记数据|
|[DynTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/DynTypes.hpp)|dynamics 数据模型（units/dynamics.md §4.4 设计基线的本任务落地|
|[Envelope.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Envelope.hpp)|峰值/RMS/多工况包络计算器（units/dynamics.md §10.4|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Errors.hpp)|dynamics 域错误契约——DynamicsError（调用方错误 fail-fast 异常轨；|
|[EvidenceBuilder.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/EvidenceBuilder.hpp)|dyn Profile 证据装配器（units/dynamics.md §10.6|
|[ForwardDynamics.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/ForwardDynamics.hpp)|正动力学响应一致性检查（units/dynamics.md §6/§10.2，DYN-05）——|
|[InverseDynamics.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/InverseDynamics.hpp)|RNEA 逆动力学评估器（units/dynamics.md §5/§10.1）——逐样本递归|
|[PowerEnergy.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/PowerEnergy.hpp)|机械功率/能量摘要计算器（units/dynamics.md §10.5|
|[Replay.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Replay.hpp)|DYN-08 数据面——各关节曲线联动投影、三维轨迹时刻回放数据与峰值|
|[SeriesBuilder.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/SeriesBuilder.hpp)|动力学序列构建器（units/dynamics.md §10.3 IDynamicsSeriesBuilder|

### drivetrain

|公共头|头注释摘要|
|---|---|
|[Codec.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/Codec.hpp)|传动映射域的 canonical 字节编解码契约（组装方协议面）——输入侧|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/DiagCodes.hpp)|drivetrain 稳定诊断码登记表——DT-* 码值常量（21 码：卡 §6.3|
|[Evaluator.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/Evaluator.hpp)|传动映射评估器（③端口注册面，units/drivetrain.md §13.10）——|
|[Facts.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/Facts.hpp)|传动事实 DTO 与提取接口（units/drivetrain.md §12.3/§13.6）——|
|[MappingCore.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/MappingCore.hpp)|传动映射核心契约（units/drivetrain.md §13.1/§13.2/§13.3/§13.4/|
|[MappingTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/MappingTypes.hpp)|传动映射输入侧归一化值类型（units/drivetrain.md §5）——关节轴/|
|[Series.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/include/sdurws/ird/drivetrain/Series.hpp)|传动映射序列与电机侧工作点值类型（units/drivetrain.md §10/§11/|

### selection

|公共头|头注释摘要|
|---|---|
|[Backfill.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/Backfill.hpp)|器件回填（SEL-10）——回填命令组装＋DeviceBackfillCommandHandler|
|[CatalogDiff.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/CatalogDiff.hpp)|目录差异比较（selection 单元）——SEL-08"目录差异比较"的结构|
|[CatalogProvider.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/CatalogProvider.hpp)|目录快照供给与业务校验接口（selection 单元）——ICatalogProvider/|
|[CatalogTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/CatalogTypes.hpp)|目录包业务模型（selection 单元）——版本化 CSV 目录包的四表数据|
|[Combination.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/Combination.hpp)|组合校核（selection 单元）——候选组合构造（§14.5）、组合身份|
|[Curve.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/Curve.hpp)|能力曲线分段线性插值契约（selection 单元）——CurveQueryResult|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/DiagCodes.hpp)|selection 稳定诊断码登记表——SEL-* 码值常量（58 码：T02 批|
|[FeasibleSet.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/FeasibleSet.hpp)|可行集与淘汰原因输出（selection 单元）——结果身份块（§4.3/|
|[LinearDrive.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/LinearDrive.hpp)|直线传动选型层（selection 单元）——直线轴类型化广义量工作点|
|[Preference.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/Preference.hpp)|企业偏好过滤（selection 单元）——SEL-07"企业自定义优选品牌、|
|[Screening.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/include/sdurws/ird/selection/Screening.hpp)|电机/减速器硬筛选（selection 单元）——筛选条件、工作点事实、|

### optimization

|公共头|头注释摘要|
|---|---|
|[Applier.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Applier.hpp)|候选应用组装面（IOptimizationCandidateApplier，O13）——"设为当前|
|[CandidatePatch.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/CandidatePatch.hpp)|候选补丁（CandidatePatch）与确定性身份——补丁项、canonical 序列化、|
|[Constraint.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Constraint.hpp)|静态硬约束编排（O4/O7 管线执行件）——R1 约束词表与执行清单、|
|[DiagCodes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/DiagCodes.hpp)|optimization 稳定诊断码工厂——OPT-* 码的 CodeDescriptor 登记|
|[EvaluatorPorts.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/EvaluatorPorts.hpp)|优化批量编排与缓存/种子消费面（O7/O15——§8/§9 的 WP-20-T06 交付|
|[Export.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Export.hpp)|优化证据一站式导出（OPT-12/O12——§11.4 六工件契约的 WP-20-T09|
|[Generator.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Generator.hpp)|搜索策略扩展点接口（ICandidateGenerator——OPT-10）与首版两个内置|
|[JointRun.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/JointRun.hpp)|StageD 联合运行编排面（OPT-03/06 D 部分——§8.5 外层两级批量编排|
|[JointStrategy.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/JointStrategy.hpp)|分层联合策略（OPT-05——§8.5 外层—内层结构图的编排侧承载）：|
|[Objective.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Objective.hpp)|目标指标配置与八项指标分层（OPT-07）——指标词表物化（方向/单位/|
|[Pareto.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Pareto.hpp)|Pareto 非支配集构建（OPT-04/O9）——支配判定（严格序/容差支配）、|
|[Preflight.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Preflight.hpp)|优化预检 Preflight（OPT-11）——运行启动前的结构化只读检查面：|
|[Run.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Run.hpp)|优化运行结果（OptimizationRunResult）聚合面——运行身份、任务映射、|
|[Types.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Types.hpp)|optimization 基础词表与域异常——优化阶段词表、随机种子别名与|
|[Variable.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/include/sdurws/ird/optimization/Variable.hpp)|设计变量模型——变量类别、绑定、R1 内置词表（9 类）、新机型/改型|

### reporting

|公共头|头注释摘要|
|---|---|
|[Archive.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Archive.hpp)|报告归档协调——IReportArtifactSink 工件汇集座契约（project 实现）、|
|[Builder.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Builder.hpp)|报告构建器契约——ReportBuildSession 会话状态机＋IReviewReportBuilder|
|[Bundle.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Bundle.hpp)|证据包组装——IArchiveWriter 注入契约（reporting 定义、io ZIP 薄适配，|
|[Consistency.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Consistency.hpp)|一致性检查器（RPT-T07 产物）——多格式渲染工件的逐字段一致性|
|[Errors.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Errors.hpp)|reporting 错误契约——ReportErrorCode 全表（16 个稳定 token）＋|
|[Export.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Export.hpp)|报告导出服务——IReportExportService（§9.5：导出编排＝渲染→二次|
|[FrameworkSections.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/FrameworkSections.hpp)|框架章节内建投影器——B 级四章节（project-scheme/input-summary/|
|[Identity.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Identity.hpp)|reporting 身份类型——ReportId（rpt- tag）＋ReportLevel＋|
|[ModelSummary.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/ModelSummary.hpp)|模型摘要投影 schema（ModelSummary）与 runtime 只读摘要注入接口|
|[Render.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Render.hpp)|报告渲染契约——FieldMatrix 字段矩阵（单次投影）＋IReportRenderer|
|[ReportModel.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/ReportModel.hpp)|ReviewReport 数据模型——报告级身份/引用类型全族/章节冻结视图/|
|[Roundtrip.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Roundtrip.hpp)|报告往返复算契约——roundtripCheck 比对入口＋复算请求/结果值类型＋|
|[SectionProvider.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/SectionProvider.hpp)|章节投影提供方契约——SectionRequest/SectionContent 请求-内容结构|
|[Sections.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/Sections.hpp)|章节词表与级别契约——SectionId 冻结词表（§5.1，14 项）＋级别归属|
|[TaskStatusSource.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/include/sdurws/ird/reporting/TaskStatusSource.hpp)|execution 任务状态只读投影注入接口（ITaskStatusSource）与投影值|

### ui

|公共头|头注释摘要|
|---|---|
|[AboutDialog.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/AboutDialog.hpp)|帮助入口与关于对话框的呈现契约（UI-T10）——静态白名单 ∩ 装配|
|[CommandInteractionBridge.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/CommandInteractionBridge.hpp)|命令确认交互桥（§9.2）——IUiCommandInteraction 的 ui 实现：|
|[DomainReadinessSummaryCard.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/DomainReadinessSummaryCard.hpp)|跨域就绪摘要卡（Right 区右栏）——三域就绪判定的只读汇总呈现|
|[FlowLayout.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/FlowLayout.hpp)|流式栅格布局（FlowLayout）——控件按行排列、行满自动换行的|
|[FormEditCommon.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/FormEditCommon.hpp)|参数表与表单公共件（UI-T08 阶段 B 交付面）——UX-04/05/07 公共|
|[ICommandRegistry.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/ICommandRegistry.hpp)|命令注册表（CommandRegistry）——工作台命令的唯一登记/查询/提交|
|[IDiagnosticPresentationModel.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IDiagnosticPresentationModel.hpp)|诊断呈现模型（§10.8）——诊断表/详情/建议动作/恢复横幅/日志面板|
|[IDraftController.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IDraftController.hpp)|草稿控制器（IDraftController）——未保存标记/草稿投影/会话状态机|
|[IGlobalShortcutRegistry.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IGlobalShortcutRegistry.hpp)|全局快捷键唯一注册点（GlobalShortcutRegistry）——键→命令映射的|
|[IHostController.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IHostController.hpp)|宿主控制器簇（IHostController）——方案 B.1 宿主融合产品形态下，|
|[IndustrialProjectTree.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IndustrialProjectTree.hpp)|工业项目树（IndustrialProjectTree）——业务主导航（D3）：五分组|
|[IPluginUiModule.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IPluginUiModule.hpp)|插件界面模块接口（IPluginUiModule）——各业务域插件界面目标实现|
|[IPluginUiRegistrar.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IPluginUiRegistrar.hpp)|插件界面注册端口（IPluginUiRegistrar）——静态白名单装配的登记|
|[IStageNavigationModel.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IStageNavigationModel.hpp)|阶段导航投影（IStageNavigationModel）——七阶段呈现状态、导航|
|[ITaskPresentationModel.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/ITaskPresentationModel.hpp)|任务呈现模型（§10.7）——任务清单/进度轮询/控制请求/归档徽标/|
|[IWorkbenchShell.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/IWorkbenchShell.hpp)|工作台壳门面（WorkbenchShell）——L5 应用壳装配 ui 的唯一入口：|
|[PolicySummaryCard.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/PolicySummaryCard.hpp)|工程策略摘要只读卡的呈现契约（UI-T07 阶段 A）——§6.7 右栏|
|[PropertyInspector.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/PropertyInspector.hpp)|共享属性检查器（PropertyInspector）——全域唯一跨域属性呈现面|
|[RuntimePublishBridge.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/RuntimePublishBridge.hpp)|宿主运行时发布桥（RuntimePublishBridge）——已应用 WorkCell 的|
|[SelectionService.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/SelectionService.hpp)|选择服务（SelectionService）——业务选择的唯一汇聚点：维护"当前|
|[UiPorts.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/UiPorts.hpp)|ui 单元最小端口接口——对 project/policy/runtime 协作面的 ui 自有|
|[UiProjections.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/UiProjections.hpp)|ui 单元值投影承载——对端能力经 ui 自有端口进入时的只读值形态|
|[UiSessionController.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/UiSessionController.hpp)|UI 会话控制器——§5 会话状态机的唯一执行者与关闭/切换统一确认|
|[UiText.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/UiText.hpp)|工程用语文案体系（UiText）——界面文本经文案键解析的唯一出口|
|[UiTheme.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/UiTheme.hpp)|工业风主题基建（UI-T37——调色板常量/QSS 模板/卡片容器）。|
|[UiTypes.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/UiTypes.hpp)|ui 单元公共值类型词表——命令 id／文案键／菜单路径／阶段词表等|
|[View3DContract.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/View3DContract.hpp)|三维视图占位契约（UI-T05 阶段 A 登记）——中央区三维视图的|
|[View3DPreviewContract.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/View3DPreviewContract.hpp)|三维会话预览协议（UI-T33 收口——工位标记/区域边界框/采样格|
|[WorkbenchContent.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/WorkbenchContent.hpp)|工作台内容装配层（WorkbenchContent）——五区内容/命令设施/布局记忆|

### workflow

|公共头|头注释摘要|
|---|---|
|[Advice.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Advice.hpp)|下一步建议引擎（O3）——UX-01 四要素承载 StageAdvice、建议八规则|
|[BackfillTranslate.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/BackfillTranslate.hpp)|P-SEL-9 转译编排——selection 回填记录（sel-device-backfill）到|
|[CloseSupervisor.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/CloseSupervisor.hpp)|L5 关闭控制器（StoreCloseSupervisor）——存储上下文关闭等待|
|[Commands.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Commands.hpp)|命令集编排核（WP-22-T12）——工业命令的 id 词表、执行分流与|
|[Comparison.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Comparison.hpp)|方案比较视图编排数据面（UX-13/V15-02）——|
|[Gate.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Gate.hpp)|七阶段门控模型（O1 门控规则＋O2 级联失效提示）——门控输入板、|
|[Lifecycle.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Lifecycle.hpp)|生命周期入口流程——新建项目三步向导（PM-01）＋打开协议编排|
|[Projection.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Projection.hpp)|标题栏/状态栏与恢复横幅的状态投影数据面（PM-11/PM-15）——|
|[Settings.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Settings.hpp)|用户级设置存储（PM-14——§7.7）：最近项目／包导出默认勾选／|
|[Types.hpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/include/sdurws/ird/workflow/Types.hpp)|workflow 单元公共词表与值类型——建议动作 token、域↔阶段映射、|

## 命令定义/登记来源

以下从当前命令目录/公共命令头提取，**仅证明定义或登记，不能证明执行链已装配**。优化四页按钮不依赖公开命令目录，见对应面板。

|命令标识（供工程师定位）|定义来源|
|---|---|
|`draft.apply`|[WorkflowCommandCatalog.cpp:49](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`draft.save`|[WorkflowCommandCatalog.cpp:48](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`dynamics.analyze`|[Commands.hpp:81](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Commands.hpp)|
|`dynamics.export-curve-data`|[Commands.hpp:93](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Commands.hpp)|
|`dynamics.locate-peak`|[Commands.hpp:87](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Commands.hpp)|
|`dynamics.replay-at`|[Commands.hpp:90](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Commands.hpp)|
|`dynamics.show-curves`|[Commands.hpp:84](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/include/sdurws/ird/dynamics/Commands.hpp)|
|`kinematics.analyze-pose`|[KinPanelCommandCatalog.cpp:21](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.evaluate-coverage`|[KinPanelCommandCatalog.cpp:24](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)；[KinPanelCommandCatalog.cpp:105](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.export-results`|[KinPanelCommandCatalog.cpp:27](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.reset-session-pose`|[KinPanelCommandCatalog.cpp:28](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.set-default-device`|[KinPanelCommandCatalog.cpp:26](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)；[KinPanelCommandCatalog.cpp:107](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.set-default-tcp`|[KinPanelCommandCatalog.cpp:25](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)；[KinPanelCommandCatalog.cpp:106](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.solve-ik`|[KinPanelCommandCatalog.cpp:22](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`kinematics.validate-task-points`|[KinPanelCommandCatalog.cpp:23](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)；[KinPanelCommandCatalog.cpp:104](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinPanelCommandCatalog.cpp)|
|`modeling.diff-baseline`|[PanelCommandCatalog.cpp:54](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.estimate-properties`|[PanelCommandCatalog.cpp:49](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.export-package`|[PanelCommandCatalog.cpp:56](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.export-workcell-xml`|[PanelCommandCatalog.cpp:65](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.generate-placeholder-geometry`|[PanelCommandCatalog.cpp:51](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.import-package`|[PanelCommandCatalog.cpp:58](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.import-urdf`|[PanelCommandCatalog.cpp:43](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.import-workcell`|[PanelCommandCatalog.cpp:70](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.import-xacro`|[PanelCommandCatalog.cpp:45](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.new-from-template`|[PanelCommandCatalog.cpp:41](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.reset-home-zero`|[PanelCommandCatalog.cpp:61](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`modeling.switch-authority`|[PanelCommandCatalog.cpp:47](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp)|
|`package.export`|[WorkflowCommandCatalog.cpp:54](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`project.new`|[WorkflowCommandCatalog.cpp:46](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`project.open`|[WorkflowCommandCatalog.cpp:47](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`project.redo`|[WorkflowCommandCatalog.cpp:51](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`project.undo`|[WorkflowCommandCatalog.cpp:50](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`report.export`|[WorkflowCommandCatalog.cpp:55](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`requirements.apply-template`|[PanelCommandCatalog.cpp:60](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.capture-tcp`|[PanelCommandCatalog.cpp:52](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.create-array`|[PanelCommandCatalog.cpp:58](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.export-copy`|[PanelCommandCatalog.cpp:49](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.import-csv`|[PanelCommandCatalog.cpp:45](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.import-json`|[PanelCommandCatalog.cpp:47](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.mirror-stations`|[PanelCommandCatalog.cpp:56](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.pick-feature`|[PanelCommandCatalog.cpp:54](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`requirements.regenerate-linked`|[PanelCommandCatalog.cpp:62](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/PanelCommandCatalog.cpp)|
|`scheme.switch`|[WorkflowCommandCatalog.cpp:52](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)|
|`selection.apply-device-backfill`|[SelPanelCommandCatalog.hpp:27](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelPanelCommandCatalog.hpp)；[SelPanelCommandCatalog.hpp:65](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelPanelCommandCatalog.hpp)|
|`trajectory.export`|[Commands.hpp:97](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|
|`trajectory.local-replan`|[Commands.hpp:86](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|
|`trajectory.plan-sequence`|[Commands.hpp:79](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|
|`trajectory.play`|[Commands.hpp:90](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|
|`trajectory.playback-seek`|[Commands.hpp:93](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|
|`trajectory.preview-segment`|[Commands.hpp:82](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|
|`trajectory.show-curve`|[Commands.hpp:100](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Commands.hpp)|

正式宿主挂位核对：[DomainAssembly.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/DomainAssembly.cpp)、[ExtraDomainAssembly.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/ExtraDomainAssembly.cpp)、[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)。

本轮检索可见：轨迹/动力学/选型/优化具有工厂、服务缝、面板及内核；正式宿主的部分服务供给尚不能由登记代码证明。必须实际点运行并看到真实数据/归档，不能用各域Harness的演示供给代替。

需求/建模的命令流程、运动学面板及后台通道已查到实现；全局命令搜索是否与面板等价可用仍要单独验。源码里的旧“未装配”注释不作为当前结论的唯一依据。
