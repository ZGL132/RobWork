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
# 经 IRD_EXTRA_EDGE_REFS 登记出处——出处登记簿与图镜像双面留痕）。
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
    "requirements->evidence")
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
    "WP-14-T02 落位登记（2026-09-25）：units/requirements.md §3.2 边表——ReadinessSummary/Snapshot 角色键值类型，请求方角色、无评估器注册（dependency-graph.json 已随任务同步刷新）")

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
# R-4 扫描；IRD_R4_EXCEPTION_UNITS 中的整单元豁免（当前两者皆空）。
# ---------------------------------------------------------------------
set(IRD_R4_EXCEPTION_FILES "")
set(IRD_R4_EXCEPTION_UNITS "")

# ---------------------------------------------------------------------
# R-3 例外登记（机器面）——L2 产品库零 Qt。
# 既有登记（DTB §4.5）：ui 单元界面目标（Widgets 唯一例外，WP-10-T02 生效）。
# 该例外以目标形态承载（_plugin／应用壳目标），不落入本门禁的"L2 裸计算库"
# 扫描集合（sdurws_ird_<unit> 与 _worker），故机器面无需登记条目；
# 若未来裸计算库需豁免，在此追加目标名。
# ---------------------------------------------------------------------
set(IRD_R3_EXCEPTION_TARGETS "")

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
