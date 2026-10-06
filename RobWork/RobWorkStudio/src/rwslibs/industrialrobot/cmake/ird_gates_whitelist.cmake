# =====================================================================
# ird_gates_whitelist.cmake — ird_gates 门禁白名单与例外登记数据（WP-01-T01）
#
# 本文件是门禁的【数据面】：引擎（ird_gates.cmake）从这里读取允许的依赖边
# 与例外登记，代码零改动即可增补数据。修改本文件＝修改门禁口径，必须同时
# 满足：
#   1. 依赖边唯一数据源＝ARCHITECTURE.md §3.5 单元级依赖表（接口依赖边）；
#      表外新边须先补登 §3.5（架构所有者）或经 DTB §4.5 例外登记。
#   2. 例外登记的【机器消费面】在本文件，【人工登记册】＝
#      development-task-breakdown.md §4.5（WP-01-T03 维护）——两处必须一致，
#      以 DTB §4.5 为准，本文件随登记同步回填。
#   3. 未登记例外被门禁检出即失败（SA-10：红线是构建期硬门禁，不是评审约定）。
#
# WP-22-T02 返工第 1 轮（2026-10-06）机器面同步回填说明：WP-10-T02 起各
# 落位任务累积的"机器面滞后既有状态"命中（各任务留痕/登记提交件中反复
# 注明"登记册回填归 WP-01-T03"而未回填）在本轮按既有登记逐条落机器面，
# 使 ird_gates 目标达到退出 0——每条回填的出处（DTB §4.5 行/单元卡 §3.2
# 行/登记提交件）随条目注释登记，无一条私裁新增语义。分类：
#   a) 产品边回填（modeling/requirements/kinematics 三卡 §3.2"插件目标→
#      本计算库＋sdurws_ird_ui"明文的 ->ui 边；WP-24-T08 DTB §4.5 生效行的
#      studio 装配边）；
#   b) R-3/R-4 例外回填（DTB §4.5 首行 ui Widgets 唯一例外 WP-10-T02 生效
#      ——bare 目标名即 sdurws_ird_ui 而落扫描集合，例外表此前为空册；
#      project 创建工具版本标记常量的 R-4 文件例外——F-011"判定对象是
#      行为"精神的误报纠正）；
#   c) 测试/工具链接面表（ui.md §3.1 v0.4"测试目标链接面按本表承载，不属
#      ARCH §3.5 产品边管辖"——ui_contract_test/ui_test 各登记提交件；
#      TK-T03 lint 工具与 owner 演示工具 demo6r 的 CMakeLists 登记注释）；
#   d) 第三方依赖词表（io.md §3.4"ird_gates LIB 词表扩登随 F-191 归门禁
#      所有者处置"明文＋O-40 已登记行——libzip/expat/pugixml/Eigen3）。
# =====================================================================

# ---------------------------------------------------------------------
# IRD_ALLOWED_UNIT_EDGES —— 允许的单元级依赖边（产品目标之间；接口依赖形态）
#
# 全量来源：ARCHITECTURE.md §3.5（2026-09-10 实测 20 条接口依赖边），
# 另含 2026-09-10 补登的 testkit→core（O-21 消账——此前 §3.5 未列 testkit，
# WP-01-T01 按 DTB §2.2 验收第 3 条在 §3.5 表内补登该行，本表随之增补）；
# 另含 2026-09-22 增登的 modeling→core/diagnostics/project/runtime/policy/
# io 六条（WP-13-T02——units/modeling.md §3.2 边表，属 ARCH §3.5"各业务域
# 单元→L2/L3 公共接口"既有许可方向的实例化，非新增架构边；dependency-
# graph.json 的刷新归 traceability 维护任务，故六边经 IRD_EXTRA_EDGE_REFS
# 登记出处——见下方第 6 步一致性核对口径）；另含 2026-09-25 增登的
# requirements→core/diagnostics/project/io/evidence 五条（WP-14-T02——
# units/requirements.md §3.2 边表，同属 ARCH §3.5 既有许可方向的实例化；
# runtime 边未登记——T02 零 runtime 公共值类型引用，实际引用时按卡 §3.2
# 增登。五边已随本任务同步刷新进 dependency-graph.json〔traceability/ 在
# allowedFiles 内——与 modeling 六边"图刷新归治理侧"的处置不同〕，并仍
# 经 IRD_EXTRA_EDGE_REFS 登记出处——出处登记簿与图镜像双面留痕）；另含
# 2026-09-26 增登的 kinematics→core/diagnostics/runtime/policy/evidence/
# execution 六条（WP-15-T02——units/kinematics.md §3.2 边表，同属
# ARCH §3.5 既有许可方向的实例化。六边已随本任务同步刷新进 dependency-
# graph.json〔traceability/ 在 allowedFiles 内——WP-14-T02 双面留痕同款〕，
# 并仍经 IRD_EXTRA_EDGE_REFS 登记出处）；另含 2026-10-06 增登的
# workflow→core/ui/diagnostics/project/execution/io/evidence/reporting 八条
# （WP-22-T02——units/workflow.md §3.2 依赖白名单，属 ARCH §3.4"不直链
# 任何业务域单元，只经端口与事件协作"＋§3.5"L4 编排→L2/L3 公共接口"
# 既有许可方向的实例化。八边已随本任务同步刷新进 dependency-graph.json
# 〔traceability/ 在 allowedFiles 内——WP-14/WP-15 双面留痕同款〕，并仍
# 经 IRD_EXTRA_EDGE_REFS 登记出处）；另含 2026-10-06 增登的
# selection→core/evidence 两条（WP-19-T02——units/selection.md §3.2 边表
# 编译链接列，属 ARCH §3.5"各业务域单元→L2/L3 公共接口"既有许可方向的
# 实例化；卡面"运行时注入/端口"列七单元〔policy/io/project/diagnostics/
# ui/runtime/drivetrain〕零编译边故不登记。两边已随本任务同步刷新进
# dependency-graph.json〔traceability/ 在 allowedFiles 内——WP-14/WP-15/
# WP-22 三面留痕同款〕，并仍经 IRD_EXTRA_EDGE_REFS 登记出处）。
# 书写格式："依赖方->被依赖方"（与 traceability/dependency-graph.json 同序）。
# ---------------------------------------------------------------------
set(IRD_ALLOWED_UNIT_EDGES
    "evidence->core"
    "policy->core"
    "runtime->core"
    "drivetrain->core"
    "drivetrain->evidence"
    "diagnostics->core"
    "project->core"
    "project->diagnostics"
    "execution->core"
    "execution->evidence"
    "execution->diagnostics"
    "execution->project"
    "io->core"
    "io->diagnostics"
    "reporting->core"
    "reporting->evidence"
    "reporting->diagnostics"
    "reporting->project"
    "ui->core"
    "ui->diagnostics"
    "testkit->core"   # O-21 补登（2026-09-10，ARCH §3.5；testkit.md §2.4 T-2）
    "modeling->core"        # WP-13-T02 六边（modeling.md §3.2 边表——身份/SourcedValue/单位/比较/诊断契约/事件）
    "modeling->diagnostics" # WP-13-T02（稳定码注册、IDiagnosticFactory/IDiagnosticSink）
    "modeling->project"     # WP-13-T02（ICommandHandler/CommandPlan/HandlerContext/DraftService/查询端口）
    "modeling->runtime"     # WP-13-T02（RobotDesignDescription/IRobotDesignReader/资源引用类型）
    "modeling->policy"      # WP-13-T02（EngineeringPolicySet 只读解析、IJointLimitEvaluator）
    "modeling->io"          # WP-13-T02（SafePath/BudgetGuard/IResourceReader/ResourceSnapshot/AtomicFile 值与服务类型）
    "requirements->core"        # WP-14-T02 五边（requirements.md §3.2 边表——身份/SourcedValue/单位/比较/诊断契约/事件）
    "requirements->diagnostics" # WP-14-T02（稳定码注册、IDiagnosticFactory/IDiagnosticSink）
    "requirements->project"     # WP-14-T02（ICommandHandler/CommandPlan/HandlerContext/DraftService/查询端口）
    "requirements->io"          # WP-14-T02（Csv/Json 解析值与服务、SafePath/BudgetGuard、AtomicFile）
    "requirements->evidence"    # WP-14-T02（ReadinessSummary/Snapshot 角色键值类型——请求方角色；无评估器注册）
    "kinematics->core"          # WP-15-T02 六边（kinematics.md §3.2 边表——身份/单位/比较/EvaluationMode 词表/诊断契约/事件）
    "kinematics->diagnostics"   # WP-15-T02（稳定码注册、IDiagnosticFactory/IDiagnosticSink）
    "kinematics->runtime"       # WP-15-T02（IRuntimeModelView/RuntimeSnapshot 只读消费——worker 物化入口）
    "kinematics->policy"        # WP-15-T02（④端口 ICollisionEvaluator/IPolicyProvider/IJointLimitEvaluator 只读调用）
    "kinematics->evidence"      # WP-15-T02（IEngineeringEvaluator/EvaluationRequest/IEvaluationContext/EvaluationOutput 契约实现、DependencyDeclaration 值）
    "kinematics->execution"     # WP-15-T02（任务提交/取消令牌/检查点/结果归档通道——接口消费）
    "workflow->core"        # WP-22-T02 八边（workflow.md §3.2 依赖白名单——身份/事件 DomainEventKind/TaskState 九态/单位）
    "workflow->ui"          # WP-22-T02（StageId/StageViewStatus 词表、StageReadinessSnapshot/DomainReadinessItem 投影、ICommandRegistry 注册端口、IUiProjectionStore——L4→L3 接口依赖，D-WF-6 两权分立）
    "workflow->diagnostics" # WP-22-T02（统一诊断目录条目消费——横幅/建议的问题数据源；R1 零新增码，卡 §6.5）
    "workflow->project"     # WP-22-T02（①命令提交协议/HandlerContext、打开协议服务侧、ProjectMetadata/分支会话切换、存储上下文排空语义）
    "workflow->execution"   # WP-22-T02（shutdown(DrainPolicy)/drained()、任务清单九态数据、Interrupted 条目、进度/取消控制接口）
    "workflow->io"          # WP-22-T02（包导入校验执行、JSON canonical 写出——用户设置、SafePath/BudgetGuard）
    "workflow->evidence"    # WP-22-T02（当前性投影 Superseded 原因清单、失效范围判定结果经投影——级联提示数据源）
    "workflow->reporting"   # WP-22-T02（报告导出服务——"报告导出"命令执行面，RPT-02/UX-13）
    "selection->core"       # WP-19-T02 两边（selection.md §3.2 边表编译链接列——身份/内容摘要/单位/比较工具/词表/诊断承载）
    "selection->evidence"   # WP-19-T02（IEngineeringEvaluator/切片/Profile/包络契约——sel.combination-check 评估器与组合校核实现；T02 仅链接不消费——P-RPT-9 先例，消费随 WP-19-T05 回填）
    "modeling->ui"          # WP-22-T02 返工回填（modeling.md §3.2"插件目标→本计算库＋sdurws_ird_ui"明文——WP-13-T15 落位的装配面边；WP-15-T02 附件"逐条具名登记——登记册回填归 WP-01-T03"的执行）
    "requirements->ui"      # WP-22-T02 返工回填（requirements.md §3.2 同款插件装配面边——WP-14-T08 落位）
    "kinematics->ui"        # WP-22-T02 返工回填（kinematics.md §3.2 同款插件装配面边——WP-15-T12 落位）
    "studio->ui"            # WP-22-T02 返工回填（DTB §4.5 WP-24-T08 行 2026-09-28 生效：studio 装配基座 ui 边——正式产品主程序 SA-18 D1）
    "studio->project"       # WP-22-T02 返工回填（DTB §4.5 WP-24-T08 行生效：ui_plugin 先例同款适配边）
    "studio->modeling"      # WP-22-T02 返工回填（DTB §4.5 WP-24-T08 行生效：DomainAssembly 建模装配门面消费边）
    "studio->kinematics"    # WP-22-T02 返工回填（WP-24-T08 行后装配面增量——studio 链接面实测边，ui/CMakeLists studio 段；出处登记 EXTRA_EDGE_REFS）
    "studio->requirements"  # WP-22-T02 返工回填（同上——studio 链接面实测增量边）
)

# ---------------------------------------------------------------------
# IRD_EXTRA_EDGE_REFS —— 白名单多出 dependency-graph.json 的边及其登记出处
#
# traceability/dependency-graph.json（20 条边）是 ARCH §3.5 的机器镜像，
# 门禁逐条核对其 ⊆ 本白名单；反向（白名单多出的边）必须在此登记出处，
# 否则视为"数据源不一致"失败（DTB §2.2 验收第 3 条）。
# 注：dependency-graph.json 的补登行刷新（追加 testkit->core 并更正说明）
# 不在本任务 allowedFiles 内，转 traceability 维护任务承接（DTB §4 O-21 行）。
# WP-13-T02 增登的 modeling 六边同形态：dependency-graph.json 刷新不在该
# 任务 allowedFiles 的机器镜像维护责任内（traceability 机器索引同步归
# 治理侧——modeling.md §14.5 遗留行同口径），故经本表登记出处。
# WP-14-T02 增登的 requirements 五边：traceability/ 在该任务 allowedFiles
# 内，dependency-graph.json 已随任务同步刷新（五边入图，⊆ 方向成立）；
# 仍在本表登记出处——出处登记簿与图镜像双面留痕（契约 acceptance 2 双
# 义务的执行面）。
# WP-15-T02 增登的 kinematics 六边：traceability/ 在该任务 allowedFiles
# 内（WP-14-T02 双面留痕同款），dependency-graph.json 已随任务同步刷新
# （六边入图，⊆ 方向成立）；仍在本表登记出处。
# WP-22-T02 增登的 workflow 八边：traceability/ 在该任务 allowedFiles 内
# （WP-14/WP-15 双面留痕同款），dependency-graph.json 已随任务同步刷新
# （八边入图，⊆ 方向成立）；仍在本表登记出处。
# 格式：边 与 出处成对书写（各一个条目，数量必须相等）。
# ---------------------------------------------------------------------
set(IRD_EXTRA_EDGE_REFS_EDGES
    "testkit->core"
    "modeling->core"
    "modeling->diagnostics"
    "modeling->project"
    "modeling->runtime"
    "modeling->policy"
    "modeling->io"
    "requirements->core"
    "requirements->diagnostics"
    "requirements->project"
    "requirements->io"
    "requirements->evidence"
    "kinematics->core"
    "kinematics->diagnostics"
    "kinematics->runtime"
    "kinematics->policy"
    "kinematics->evidence"
    "kinematics->execution"
    "workflow->core"
    "workflow->ui"
    "workflow->diagnostics"
    "workflow->project"
    "workflow->execution"
    "workflow->io"
    "workflow->evidence"
    "workflow->reporting"
    "selection->core"
    "selection->evidence"
    "modeling->ui"
    "requirements->ui"
    "kinematics->ui"
    "studio->ui"
    "studio->project"
    "studio->modeling"
    "studio->kinematics"
    "studio->requirements")
set(IRD_EXTRA_EDGE_REFS_NOTES
    "ARCH §3.5 补登（O-21 消账，2026-09-10）；testkit.md §2.4 T-2 允许形态"
    "WP-13-T02 落位登记（2026-09-22）：units/modeling.md §3.2 边表——ARCH §3.5 业务域→L2/L3 公共接口许可方向的实例化（身份/SourcedValue/单位/比较/诊断契约/事件）"
    "WP-13-T02 落位登记（2026-09-22）：units/modeling.md §3.2 边表——稳定码注册、IDiagnosticFactory/IDiagnosticSink"
    "WP-13-T02 落位登记（2026-09-22）：units/modeling.md §3.2 边表——ICommandHandler/CommandPlan/HandlerContext/DraftService/查询端口"
    "WP-13-T02 落位登记（2026-09-22）：units/modeling.md §3.2 边表——RobotDesignDescription/IRobotDesignReader/资源引用类型"
    "WP-13-T02 落位登记（2026-09-22）：units/modeling.md §3.2 边表——EngineeringPolicySet 只读解析、IJointLimitEvaluator"
    "WP-13-T02 落位登记（2026-09-22）：units/modeling.md §3.2 边表——SafePath/BudgetGuard/IResourceReader/ResourceSnapshot/AtomicFile 值与服务类型"
    "WP-14-T02 落位登记（2026-09-25）：units/requirements.md §3.2 边表——ARCH §3.5 业务域→L2/L3 公共接口许可方向的实例化（身份/SourcedValue/单位/比较/诊断契约/事件）；dependency-graph.json 已随任务同步刷新（双面留痕）"
    "WP-14-T02 落位登记（2026-09-25）：units/requirements.md §3.2 边表——稳定码注册、IDiagnosticFactory/IDiagnosticSink（dependency-graph.json 已随任务同步刷新）"
    "WP-14-T02 落位登记（2026-09-25）：units/requirements.md §3.2 边表——ICommandHandler/CommandPlan/HandlerContext/DraftService/查询端口（dependency-graph.json 已随任务同步刷新）"
    "WP-14-T02 落位登记（2026-09-25）：units/requirements.md §3.2 边表——Csv/Json 解析值与服务、SafePath/BudgetGuard、AtomicFile（dependency-graph.json 已随任务同步刷新）"
    "WP-14-T02 落位登记（2026-09-25）：units/requirements.md §3.2 边表——ReadinessSummary/Snapshot 角色键值类型，请求方角色、无评估器注册（dependency-graph.json 已随任务同步刷新）"
    "WP-15-T02 落位登记（2026-09-26）：units/kinematics.md §3.2 边表——ARCH §3.5 业务域→L2/L3 公共接口许可方向的实例化（身份/单位/比较/EvaluationMode 词表/诊断契约/事件）；dependency-graph.json 已随任务同步刷新（双面留痕）"
    "WP-15-T02 落位登记（2026-09-26）：units/kinematics.md §3.2 边表——稳定码注册、IDiagnosticFactory/IDiagnosticSink（dependency-graph.json 已随任务同步刷新）"
    "WP-15-T02 落位登记（2026-09-26）：units/kinematics.md §3.2 边表——IRuntimeModelView/RuntimeSnapshot 只读消费、worker 物化入口（dependency-graph.json 已随任务同步刷新）"
    "WP-15-T02 落位登记（2026-09-26）：units/kinematics.md §3.2 边表——④端口 ICollisionEvaluator/IPolicyProvider/IJointLimitEvaluator 只读调用（dependency-graph.json 已随任务同步刷新）"
    "WP-15-T02 落位登记（2026-09-26）：units/kinematics.md §3.2 边表——IEngineeringEvaluator/EvaluationRequest/IEvaluationContext/EvaluationOutput 契约实现、DependencyDeclaration 值（dependency-graph.json 已随任务同步刷新）"
    "WP-15-T02 落位登记（2026-09-26）：units/kinematics.md §3.2 边表——任务提交/取消令牌/检查点/结果归档通道（接口消费；dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——ARCH §3.4 编排定位＋§3.5 L4→L2/L3 公共接口许可方向的实例化（身份/事件/任务状态词表——消费 core 四类事件与九态）；dependency-graph.json 已随任务同步刷新（双面留痕）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——StageId/StageViewStatus 词表、StageReadinessSnapshot/DomainReadinessItem 投影消费、ICommandRegistry 注册端口、IUiProjectionStore（L4→L3 接口依赖——D-WF-6 两权分立非循环；dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——统一诊断目录条目消费（横幅/建议的问题数据源；R1 零新增码——卡 §6.5；dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——①命令提交协议/HandlerContext、打开协议服务侧（五步②③⑤）、ProjectMetadata/分支会话切换、存储上下文排空语义（dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——shutdown(DrainPolicy)/drained()、任务清单九态数据、Interrupted 条目（重跑入口）、进度/取消控制接口（dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——包导入校验执行、JSON canonical 写出（用户设置 PM-14）、SafePath/BudgetGuard（dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——当前性投影（Superseded 原因清单）、失效范围判定结果经投影（级联提示数据源——D-WF-2 不复制失效计算；dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 落位登记（2026-10-06）：units/workflow.md §3.2 依赖白名单——报告导出服务（\"报告导出\"命令执行面——RPT-02/UX-13；dependency-graph.json 已随任务同步刷新）"
    "WP-19-T02 落位登记（2026-10-06）：units/selection.md §3.2 边表编译链接列——ARCH §3.5 业务域→L2/L3 公共接口许可方向的实例化（身份/内容摘要/单位/比较工具/词表/诊断承载——core::ContentDigester/Units/比较规则/DiagCode 句法）；dependency-graph.json 已随任务同步刷新（双面留痕）"
    "WP-19-T02 落位登记（2026-10-06）：units/selection.md §3.2 边表编译链接列——IEngineeringEvaluator/切片/Profile/包络契约（sel.combination-check 评估器与组合校核实现面）；T02 仅链接目标不消费上游公共头（P-RPT-9 先例），消费随 WP-19-T05 回填（dependency-graph.json 已随任务同步刷新）"
    "WP-22-T02 返工回填（2026-10-06）：units/modeling.md §3.2 插件目标行\"→本计算库＋sdurws_ird_ui\"——WP-13-T15 落位的装配面边（WP-15-T02 附件\"逐条具名登记——登记册回填归 WP-01-T03\"的机器面执行；dependency-graph.json 已随本任务同步刷新——双面留痕）"
    "WP-22-T02 返工回填（2026-10-06）：units/requirements.md §3.2 同款插件装配面边——WP-14-T08 落位（dependency-graph.json 已随本任务同步刷新）"
    "WP-22-T02 返工回填（2026-10-06）：units/kinematics.md §3.2 同款插件装配面边——WP-15-T12 落位（dependency-graph.json 已随本任务同步刷新）"
    "WP-22-T02 返工回填（2026-10-06）：DTB §4.5 WP-24-T08 行（2026-09-28 生效）——sdurws_ird_studio 装配基座 ui/project/modeling 三边；studio 为正式产品主程序目标（SA-18 D1）非 20 单元节点，不入 dependency-graph.json（图节点集＝单元），出处登记于本表（ui/CMakeLists studio 段同源）"
    "WP-22-T02 返工回填（2026-10-06）：同上——studio->project"
    "WP-22-T02 返工回填（2026-10-06）：同上——studio->modeling"
    "WP-22-T02 返工回填（2026-10-06）：WP-24-T08 行后装配面增量（ui/CMakeLists studio 链接段实测）——studio->kinematics；同前不入 dependency-graph.json"
    "WP-22-T02 返工回填（2026-10-06）：同上——studio->requirements")

# ---------------------------------------------------------------------
# 业务域单元清单（R-1 的判定范围；来源 AGENTS.md §5 架构红线 2）
# 业务域单元之间任何链接边即 R-1 命中（白名单即使登记也不得豁免 R-1——
# R-1 无例外，ARCH §3.2 SA-10）。
# ---------------------------------------------------------------------
set(IRD_BUSINESS_UNITS
    modeling requirements kinematics trajectory dynamics
    selection optimization)

# ---------------------------------------------------------------------
# R-4 例外登记（机器面）——O-12 未裁决，按 DTB §2.2 验收第 4 条以
# 【空登记册＋占位说明】交付，不私裁：
#   - runtime 名称解析器实现文件清单：DTB §4.5 预登记，清单随 WP-06-T13
#     提交转生效——届时由 WP-01-T03 回填 IRD_R4_EXCEPTION_FILES。
#   - policy 策略消费点（只读消费整名）：DTB §4.5 待 O-12 措辞确认后回填。
# 语义：IRD_R4_EXCEPTION_FILES 中的文件（相对 IRD_ROOT 的路径）豁免
# R-4 扫描；IRD_R4_EXCEPTION_UNITS 中的整单元豁免。
# WP-22-T02 返工回填：project/src/ProjectStoreImpl.cpp——kCreatedWithTool-
# Version 常量（"RobWork-IndustrialRobot/0.1"——项目文件 createdWith 元
# 数据的创建工具版本标记，ARC-04 身份留痕值）。F-011 修复后的 R-4 判定
# 对象是"名称拼接/剥离的代码行为"（引擎第 4c 步注释原文），注释性提及
# 已剥离；本常量为**标识值**而非行为（无前缀拼接/剥离运算），属该启发
# 式的字面量误报面——按同精神以文件例外登记（回填归 WP-01-T03 抽查）。
# ---------------------------------------------------------------------
set(IRD_R4_EXCEPTION_FILES
    "project/src/ProjectStoreImpl.cpp")
set(IRD_R4_EXCEPTION_UNITS "")

# ---------------------------------------------------------------------
# R-3 例外登记（机器面）——L2 产品库零 Qt。
# 既有登记（DTB §4.5 首行，2026-09-10 预登记、WP-10-T02 生效）：ui 单元
# 界面目标（Widgets 唯一例外）。ui 单元的产品目标名即 sdurws_ird_ui（bare
# 形态——ui 是平台单元，其"界面目标"与产品库同名同目标），**事实落入**本
# 门禁的"L2 裸计算库"扫描集合（sdurws_ird_<unit> 与 _worker）：链接面（Qt
# 三件套）与产品面文件扫描（ui/include、ui/src 的 Qt 呈现构件——FlowLayout/
# UiTheme 等各文件已按 DTB §4.5"命中集增量登记"逐批生效）此前因例外表为
# 空册而逐条命中。WP-22-T02 返工回填：把该已生效登记落到机器面——
# sdurws_ird_ui 列入例外后，2b 链接面检查与 4b 文件扫描对 ui 目标豁免
# （引擎侧按本表判定）。ui 之外零条目——L2 计算内核与业务计算库零 Qt
# （R-3）的硬红线不因本例外松动（ui 是全产品唯一例外单元，NFR-MNT-01）。
# ---------------------------------------------------------------------
set(IRD_R3_EXCEPTION_TARGETS "sdurws_ird_ui"
    "sdurws_ird_studio")   # WP-22-T02 返工回填：DTB §4.5 WP-24-T08 行（2026-09-28 生效）——
                           #   "R-3 命中×3＝产品名形态目标链 Qt6::Core/Gui/Widgets（sdurws_ird_ui
                           #   同款既有命中模式——NFR-MNT-01 ui 界面目标例外类：宿主包装目标
                           #   本体即 Qt 界面，非 L2 计算内核）"；正式产品主程序（SA-18 D1）。

# ---------------------------------------------------------------------
# R-5 例外登记（机器面）——proximity 直链/包含禁止（policy.md §3.4，
# DTB §4.4 已接收为红线扩展；登记册 DTB §4.5）：
#   - policy 产品实现（碰撞唯一实现，WP-07-T01 起生效）
#   - runtime 场景装配若需 proximity 类型（仅限构造 WorkCell 几何挂载）
#     另行登记——当前无条目。
# 语义：IRD_R5_EXEMPT_TARGETS 中的目标名豁免 R-5 扫描。
# ---------------------------------------------------------------------
set(IRD_R5_EXEMPT_TARGETS "sdurws_ird_policy")

# ---------------------------------------------------------------------
# T-1/T-2 —— testkit 分发/依赖红线（testkit.md §2.4）：无例外（DTB §4.5）。
# 允许形态（引擎按此硬编码判定）：
#   sdurws_ird_<unit>_test / _contract_test → { 同单元产品目标,
#     sdurws_ird_testkit, gtest 系（GTest::） }
#   sdurws_ird_testkit → { sdurws_ird_core }（＋标准库，无编译边）
# ---------------------------------------------------------------------

# ---------------------------------------------------------------------
# 框架基线库白名单（L1；DTB §4.6 实测表）——产品目标可链的框架库前缀。
# 需登记方可使用的框架库（DTB §4.6"若启用须登记"）单列于
# IRD_FRAMEWORK_REGISTER_REQUIRED；命中即失败，登记后经
# IRD_FRAMEWORK_REGISTER_ALLOWED 豁免（当前为空）。
# ---------------------------------------------------------------------
set(IRD_FRAMEWORK_LIB_PREFIXES "sdurw" "sdurwsim" "sdurws_")
set(IRD_FRAMEWORK_REGISTER_REQUIRED "sdurw_loaders")
set(IRD_FRAMEWORK_REGISTER_ALLOWED "")

# ---------------------------------------------------------------------
# 测试目标允许链接的非 ird 目标前缀（gtest 家族；DTB §5.5 唯一接入机制）
# ---------------------------------------------------------------------
set(IRD_TEST_ALLOWED_LIB_PREFIXES "GTest::" "gtest" "gmock" "Threads::")

# ---------------------------------------------------------------------
# IRD_THIRDPARTY_ALLOWED_TARGETS —— 允许产品/测试目标链接的 vcpkg 第三方
# 目标词表（引擎 2e"未知目标族"的放行面；DTB §5.1"第三方依赖：一律经
# vcpkg……新增依赖先在本文增量修订登记"条款的机器承载）。
# 每条依赖均有登记出处（无一条新增语义）：
#   - libzip::zip / expat::expat：io.md §3.4 库目标行明文"ird_gates LIB 词
#     表扩登（libzip::zip/expat::expat）随 F-191 归门禁所有者处置"——P-IO-3
#     冻结裁决（2026-09-17 所有者裁决，governance-log §1.9；vcpkg 版本
#     1.11.4/2.8.3 随 IO-T04 登记）；
#   - pugixml::pugixml：O-40（DTB §4.2，2026-09-22 所有者批次授权"已登
#     记"）——modeling 计算库 PRIVATE，URDF/Xacro DOM 解析（WP-13-T05/T06）；
#   - Eigen3::Eigen：O-40 同行——kinematics 计算库 PRIVATE，Jacobian SVD
#     （WP-15-T03；kinematics/CMakeLists.txt O-40 注释同源）。
# 全部 PRIVATE 于各计算库（依赖面最小化），非产品分发改变项。
# ---------------------------------------------------------------------
set(IRD_THIRDPARTY_ALLOWED_TARGETS
    "libzip::zip"
    "expat::expat"
    "pugixml::pugixml"
    "Eigen3::Eigen")

# ---------------------------------------------------------------------
# IRD_TOOL_TARGET_EDGES —— 开发/演示工具目标的允许链接边（ird_gates 形态
# 解析为 other 的非单元词表目标；引擎 2a 形态二查本表放行）。
#   - sdurws_ird_testdata_lint：testkit 单元 testdata lint 工具（TK-T03
#     交付——testkit/CMakeLists.txt"只链 testkit（数据根经 TestPaths 两级
#     解析）；不进产品安装面（T-1 同款纪律——testkit 与其工具不随产品
#     分发）"）——lint 工具与 testkit 同生命周期，链 testkit 属 T-1 分发
#     红线的既有语义内（工具不入产品面，DTB §2.2 TK-T03 行）；
#   - sdurws_ird_demo6r：6 自由度机械臂 demo 项目生成器（owner 演示指令
#     2026-09-26——modeling/CMakeLists.txt"链接面＝modeling＋project（两
#     登记边；demo 工具非产品交付路径）"）。
# ---------------------------------------------------------------------
set(IRD_TOOL_TARGET_EDGES
    "sdurws_ird_testdata_lint->sdurws_ird_testkit"
    "sdurws_ird_demo6r->sdurws_ird_modeling"
    "sdurws_ird_demo6r->sdurws_ird_project")

# ---------------------------------------------------------------------
# IRD_T1_EXEMPT_TARGETS —— T-1（产品目标禁链 testkit）的目标豁免表。
#   - sdurws_ird_testdata_lint：见 IRD_TOOL_TARGET_EDGES 注——lint 工具
#     与 testkit 同生命周期（TK-T03），不随产品分发（豁免的是"产品目标"
#     形态误判——该目标为开发工具，非产品交付物）。
# ---------------------------------------------------------------------
set(IRD_T1_EXEMPT_TARGETS "sdurws_ird_testdata_lint")

# ---------------------------------------------------------------------
# IRD_TEST_TARGET_EDGES —— 测试目标（_test/_contract_test/_gui_test）的
# 跨单元产品目标链接面（引擎 2a 形态一的第三规则放行面）。
# 设计权威＝units/ui.md §3.1 sdurws_ird_ui_contract_test 目标行 v0.4 原文：
# "测试目标链接面按本表承载，不属 ARCH §3.5 产品边管辖"（O-31 裁决
# 2026-09-19 的测试面承载；F-228 随之消解）——ui 单元契约测试套件与
# 三域集成测试按各任务登记的链接面承载（每条边的出处逐条登记；DTB §4.5
# R-5 行"各 _test/_contract_test 经 testkit/policy 替身消费：生效"为
# testkit 边的总登记，testkit 边由引擎按 testkit.md §2.4 T-1 允许形态
# 硬编码判定，不入本表）。
#   - ui_contract_test→project/execution/diagnostics：UI-T14 契约测试套件
#     （ui.md §12.1 第二层行"对接 project/execution/diagnostics 公共头与
#     桩实现"；diagnostics 为表内登记边 ui→diagnostics 的测试侧显式自证）；
#   - ui_contract_test→runtime：UI-T20（登记提交件
#     traceability/wp10-t20-gate-registrations.md——真实④端口呈现桥对账）；
#   - ui_contract_test→modeling_plugin/kinematics_plugin/requirements_plugin：
#     三域集成批次（UI-T23 宿主集成收口与后续装配批次——域模块消费面）；
#   - ui_test→requirements/modeling_plugin/kinematics_plugin：UI-T64（登记
#     册 traceability/wp10-t64-gate-registrations.md——执行器消费通道值面
#     与 SamplingPlanBuilder::digest 切片消费）。
# 产品库链接块不受本表影响（各单元产品面守卫/LinkageContractTest 硬断言
# 不变——本表仅属测试目标）。
# ---------------------------------------------------------------------
set(IRD_TEST_TARGET_EDGES
    "sdurws_ird_ui_contract_test->sdurws_ird_project"
    "sdurws_ird_ui_contract_test->sdurws_ird_execution"
    "sdurws_ird_ui_contract_test->sdurws_ird_diagnostics"
    "sdurws_ird_ui_contract_test->sdurws_ird_runtime"
    "sdurws_ird_ui_contract_test->sdurws_ird_modeling_plugin"
    "sdurws_ird_ui_contract_test->sdurws_ird_kinematics_plugin"
    "sdurws_ird_ui_contract_test->sdurws_ird_requirements_plugin"
    "sdurws_ird_ui_test->sdurws_ird_requirements"
    "sdurws_ird_ui_test->sdurws_ird_modeling_plugin"
    "sdurws_ird_ui_test->sdurws_ird_kinematics_plugin")

# ---------------------------------------------------------------------
# IRD_TARGET_LEVEL_EDGES —— 装配层特权边的【目标级】登记（引擎形态二放行
# 面；unit 粒度白名单 miss 后查本表）。为什么用目标粒度而非 unit 粒度：
# ui 单元的产品库 sdurws_ird_ui 保持零对 project 的链接/include（O-31
# 常驻红线——ui/CMakeLists 配置期守卫＋BuildRedLineTest 扫描钉住），特权
# 边仅属装配层目标（同时看见两边的适配器 TU）——unit 粒度登记会放宽产
# 品库红线，目标粒度忠实于 DTB §4.5 登记行的逐目标原文。
#   - sdurws_ird_ui_app->sdurws_ird_project：DTB §4.5 O-31 行（2026-09-22
#     生效——"ui->project（打开五步协议适配链接面）"开发验证 harness）；
#   - sdurws_ird_ui_plugin->sdurws_ird_project：DTB §4.5 O-38 行（2026-09-23
#     生效——宿主集成边，插件复用 harness PortAdapters 适配形态；登记提交
#     件 traceability/wp10-t16-gate-registrations.md）；
#   - sdurws_ird_ui_plugin->sdurws_ird_modeling_plugin：WP-24-T03 首版装
#     配边（ui/CMakeLists ui_plugin 链接段注——DTB §4.5 同款 SUB 登记模
#     式；装配层 O-31 特权，业务域互不依赖 R-1 不因装配边破坏）；
#   - sdurws_ird_ui_plugin->sdurws_ird_requirements_plugin／
#     sdurws_ird_kinematics_plugin：UI-T23 三域装配边（ui_plugin 链接段
#     注——登记提交件 traceability/wp10-t23-gate-registrations.md；消费面
#     仅 O-45 补建 requirements 装配门面与 WP-15-T18 出线的 kinematics 装
#     配门面，域私有头零触碰）。
# （ui->ui 自边两处——ui_app/ui_plugin 链接本单元产品库——由引擎同单元
# 装配自边豁免承载，不入本表。）
# ---------------------------------------------------------------------
set(IRD_TARGET_LEVEL_EDGES
    "sdurws_ird_ui_app->sdurws_ird_project"
    "sdurws_ird_ui_plugin->sdurws_ird_project"
    "sdurws_ird_ui_plugin->sdurws_ird_modeling_plugin"
    "sdurws_ird_ui_plugin->sdurws_ird_requirements_plugin"
    "sdurws_ird_ui_plugin->sdurws_ird_kinematics_plugin")
