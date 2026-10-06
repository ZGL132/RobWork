/**
 * @file   DiagCodes.cpp
 * @brief  optimization 稳定诊断码工厂的实现——§6.6 登记表全表 15 码描述
 *         符清单（逐码落值依据）与装配注册函数。
 *
 * 设计依据：
 *   - units/optimization.md §6.6（登记表 v1 设计基线——15 行：码/severity/
 *     语义/备注四列＋"失败语义归类"段三组划分；逐码登记值与锚点的唯一
 *     权威）、§5.7（阶段锁全表——STAGE-LOCKED 触发面）、§6.4（Preflight
 *     检查项全表——BLOCKED/EVALUATOR-MISSING 产出面）、§6.2（候选编译
 *     ——COMPILE-FAILED 透传 RT-*）、§7.2（指标缺失语义——NOT-COMPUTABLE
 *     显示"—"）、§8.2（幸存集为空——SEARCH-EMPTY）、§10.3（应用前置
 *     校验——APPLY-PLAN-INVALID）、§11.4（契约过期阻断——EXPORT-CONTRACT-
 *     STALE）
 *   - units/diagnostics.md §4.3（分类词表逐值语义锚点）、§4.4（分类—
 *     严重—动作族矩阵——retryable 机械映射）、§4.5（字段约束与"不私造
 *     参数名"展开登记纪律）
 *   - 先例：kinematics/src/DiagCodes.cpp（WP-15-T02 同款——依赖白名单
 *     含 diagnostics 编译边的逐码注释登记落值依据形态；共字段抽取辅助
 *     makeDescriptor 同款）、dynamics/src/DiagCodes.cpp（WP-17-T02 同因
 *     同款）、trajectory/src/DiagCodes.cpp（WP-16-T03 同因同款）
 *   - 任务契约 tasks/foundation/WP-20-T02.json acceptance 2
 *
 * 确定性（NFR-COR-02）：清单序＝§6.6 表行序；每次调用返回同序同值新
 * 清单（描述符为纯值聚合）；码值经 DiagCodes.hpp 常量引用（唯一书写点）。
 */

#include <sdurws/ird/optimization/DiagCodes.hpp>

namespace sdurws::ird::optimization {

namespace {

/**
 * @brief 单码描述符装配辅助：填入 15 码共用不变的字段（ownerUnit/键约定/
 * paramSchema/确认与可见性/登记版本），可变面（码值/分类/级别/比较标记/
 * 重试族）由调用点逐码实参给出。
 *
 * 抽取目的：共字段的登记口径只在函数体注释一处陈述，15 个调用点各自只
 * 携带差异字段——落值依据可读性与"同码同口径"两得（不引入任何运行期
 * 开销——描述符构造本就是值聚合；kinematics/dynamics/trajectory 同款
 * 辅助先例）。
 *
 * 共字段口径（§4.5/diagnostics 展开登记纪律）：
 *   - ownerUnit＝"optimization"（§4.5 前缀-所有权表 OPT→optimization）；
 *   - titleKey/detailKey＝"diag.<code-lower>.title/.detail"（P-DIAG-9
 *     命名约定——注册期键形校验强制，偏离即 Usage 拒绝）；
 *   - paramSchema＝"[]"（无参数显式声明——产码路径落地时按需增量登记
 *     并升 registryVersion，不私造参数名；VAR-LOCKED 的"绑定 token＋
 *     对象定位"等比较型定位面走诊断实例 subject/context，不占参数名）；
 *   - confirmable＝false（15 码均无 SA-15 确认流语义——阶段锁/Preflight
 *     阻塞是运行启动拒绝面（§6.5"不静默降级、呈现为阻塞横幅"），非
 *     "策略校验超限待用户显式确认"面；R1 优化域无可确认诊断产生点）；
 *   - requiresComparison＝false（§6.6 表无比较型列——文件头注逐字段
 *     口径段；从调用点省略该实参，防误置 true）；
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）；
 *   - registryVersion＝1（首次登记）；deprecated=false；
 *     supersededBy 缺省即 nullopt。
 *
 * @param code      [in] 码值文本（DiagCodes.hpp 常量——唯一书写点）
 * @param category  [in] 分类（§4.3 词表——逐码锚点见调用点注释；落值
 *                  依据＝卡面 §6.6"失败语义归类"段）
 * @param severity  [in] 级别（§6.6"severity"列原值）
 * @param retryable [in] 重试族（§4.4 动作族机械映射）
 * @return 填充完成的描述符（纯值——调用方拷贝入清单）
 */
diagnostics::CodeDescriptor makeDescriptor(std::string_view code,
                                           diagnostics::DiagnosticCategory category,
                                           diagnostics::DiagnosticSeverity severity,
                                           diagnostics::RetryKind retryable)
{
    // 小写键派生与注册表 derivedTextKey 同一约定（diag.<code-lower>.段）
    // ——此处以显式字面量书写，逐码测试断言全表 30 键（15×title/detail）
    // 与码值小写逐字一致，失同步即失败（不重复实现派生逻辑防两处漂移）。
    std::string lower{code};
    for (char& ch : lower) {
        // 码值句法保证仅含 A-Z/0-9/'-'（isValidDiagCodeSyntax 前置），
        // ASCII 小写化无 locale 依赖（确定性——不经 std::locale 面）。
        if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
    }

    diagnostics::CodeDescriptor d;
    d.code = std::string{code};                                  // §6.6"码"列原文
    d.ownerUnit = "optimization";                                // §4.5 前缀-所有权表
    d.category = category;                                       // §4.3 词表落值（调用点锚点）
    d.severity = severity;                                       // §6.6"severity"列
    d.titleKey = "diag." + lower + ".title";                     // P-DIAG-9 命名约定
    d.detailKey = "diag." + lower + ".detail";                   // （键/值分离，值归文案资源）
    d.paramSchema = "[]";                                        // 无参数显式声明（不私造参数名）
    d.confirmable = false;                                       // 无 SA-15 确认流语义（§6.5 拒绝面非确认面）
    d.requiresComparison = false;                                // §6.6 无比较型列（文件头注口径段）
    d.retryable = retryable;                                     // §4.4 动作族机械映射
    d.userVisible = true;                                        // 用户级码（非 Dev）
    d.reportable = true;                                         // 进报告（溯源/评审面）
    d.historical = true;                                         // 进项目历史（PA-2 追溯面）
    d.registryVersion = 1;                                       // 首次登记
    d.deprecated = false;                                        // 未废弃（tombstone 仅 §4.5.1 置位）
    // d.supersededBy 保持 nullopt（无迁移目标——optional 缺省即空）。
    return d;
}

}  // namespace

std::vector<diagnostics::CodeDescriptor> optimizationCodeDescriptors()
{
    // 清单序＝§6.6 表行序 1~15（确定性序；后续任务增码只允许表尾追加——
    // 既有行不重排）；逐码注释给出分类/重试族落值锚点（严重度直接取
    // §6.6"severity"列原值；分类落值依据＝§6.6"失败语义归类"段）。
    return {
        // ---- 行 1：OPT-STAGE-LOCKED（error）——阶段锁 ----
        // 分类 input-invalid：§6.6 归类段点名 STAGE-LOCKED 归"input-invalid
        // 或 format-or-version（调用方可修复）"——阶段锁的处置是"用户修正
        // 研究定义后重新 Preflight"（§6.5），属修正输入面（切换阶段/移除
        // 不支持能力）→InputInvalid。fix-input 族→UserRetry。P-OPT-1：
        // 需求引用名 IRD-OPT-STAGE-LOCKED 的落位码（前缀归一——对应关系
        // 登记于卡面 §6.6 行注/§16.3，不改需求语义）。
        makeDescriptor(kOptStageLocked,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 2：OPT-INPUT-INVALID（error）——研究定义/配置非法 ----
        // 分类 input-invalid：归类段首组点名——绑定互斥（I-OPT-7）/预算
        // 非法（§6.4 #16）/种子 0（I-OPT-2）等调用方可修复面。fix-input
        // 族→UserRetry。
        makeDescriptor(kOptInputInvalid,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 3：OPT-VAR-LOCKED（error）——补丁触及锁定/未授权变量 ----
        // 分类 input-invalid：归类段点名 VAR-LOCKED 归第一组——改型未授权
        // 参数默认锁定（§5.4），用户显式授权后可行（授权集合变化＝新
        // config.opt——修正输入）。fix-input 族→UserRetry。
        makeDescriptor(kOptVarLocked,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 4：OPT-VAR-UNBINDABLE（warning）——绑定到不可绑定字段 ----
        // 分类 data-insufficient：归类段末组点名（data-insufficient/
        // warning 数据面，不阻断进程）——Mesh 基线截面/Explicit 基线 DH
        // 等基线事实使该绑定暂不可用（§5.3 绑定前提列），属"素材缺口"
        // 而非调用方违约。supply-evidence 族（更换基线权威形态后复评）
        // →UserRetry。
        makeDescriptor(kOptVarUnbindable,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 5：OPT-PATCH-ILLEGAL（error）——补丁非法 ----
        // 分类 input-invalid：归类段点名 PATCH-ILLEGAL 归第一组——未知
        // 绑定/重复/越界/序列化失败（§5.5；I-OPT-9 不静默截断，构造期
        // fail-fast），修正补丁值即可。fix-input 族→UserRetry。
        makeDescriptor(kOptPatchIllegal,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 6：OPT-CANDIDATE-COMPILE-FAILED（error）——候选编译失败 ----
        // 分类 execution-failed：归类段单独点名"归 execution-failed（环境
        // 面，透传 cause）"——编译链失败是环境/任务面错误（RT-* 码 cause
        // 链保留，§6.2），非调用方输入错误；全表唯一 execution-failed 码。
        // retry-task 族（修正候选值或排除环境故障后重算）→UserRetry。
        makeDescriptor(kOptCandidateCompileFailed,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 7：OPT-TOPOLOGY-REJECTED（error）——拓扑/链型不支持 ----
        // 分类 input-invalid：归类段点名 TOPOLOGY-REJECTED 归第一组——
        // R1 首版不启用的链型（prismatic/mimic/闭环等，§2.2 红线 7）在
        // 研究定义/基线中出现即拒绝，用户换链型或等 R2 前置放开。修正
        // 输入面→fix-input 族→UserRetry。
        makeDescriptor(kOptTopologyRejected,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 8：OPT-EVALUATOR-MISSING（error）——评估器缺失 ----
        // 分类 format-or-version：归类段点名归第一组二选一——"阶段必需
        // 评估器未注册**或契约版本不符**"（§6.4 #7）的语义重心在契约/
        // 装配面（EvaluatorSetId 缺项或 contractVersion 失配——§4.5 失效
        // 清单"评估器契约版本"行同源），落 format-or-version（契约不
        // 兼容词表锚点）。supply/retry 混合面（注册评估器或对齐契约版
        // 本后重跑）→UserRetry。
        makeDescriptor(kOptEvaluatorMissing,
                       diagnostics::DiagnosticCategory::FormatOrVersion,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 9：OPT-PREFLIGHT-BLOCKED（warning）——Preflight 有阻塞项 ----
        // 分类 data-insufficient：归类段末组点名（数据面，不阻断进程——
        // 本码是 §6.4 检查面汇总的目录条目，逐项定位在报告；warning 级
        // 呈现），修正阻塞项后重跑。fix-input 族→UserRetry。
        makeDescriptor(kOptPreflightBlocked,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 10：OPT-METRIC-NOT-COMPUTABLE（warning）——指标不可算 ----
        // 分类 data-insufficient：归类段末组点名——指标缺数据显示"—"
        // （§7.2/DOPT-13：缺失不是零，不参与 Pareto，不判不可行），候选
        // 数据面素材缺口。supply-evidence 族（补齐来源证据后重评）→
        // UserRetry。
        makeDescriptor(kOptMetricNotComputable,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 11：OPT-COMBO-OUT-OF-SCOPE（error）——超出启用范围 ----
        // 分类 input-invalid：归类段点名 COMBO-OUT-OF-SCOPE 归第一组——
        // 离散器件/直线传动/耦合链变量在 R1 启用前置（SEL-09-S1 等）完成
        // 前被研究定义引用即拒（§5.6），用户移除该绑定可修复（与 SEL-09
        // 侧"范围外 DataInsufficient"是两域各表——本码是 optimization
        // 绑定校验面的拒绝码，选型侧口径由该域码承载）。fix-input 族
        // →UserRetry。
        makeDescriptor(kOptComboOutOfScope,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 12：OPT-ROBUSTNESS-PROTOCOL-MISSING（error）——P-04 未冻结 ----
        // 分类 format-or-version：归类段点名归第一组二选一——协议/契约
        // 面缺失（P-04 扰动/鲁棒性协议未冻结——附录 C 硬前置、O-28；
        // WP-21-T01 冻结后方启用），属"格式/版本类不可用"而非研究定义
        // 错误。supply/等待面（P-04 冻结后启用）→UserRetry。
        makeDescriptor(kOptRobustnessProtocolMissing,
                       diagnostics::DiagnosticCategory::FormatOrVersion,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 13：OPT-SEARCH-EMPTY（warning）——搜索未产生合法候选 ----
        // 分类 data-insufficient：归类段末组点名——幸存集为空是数据不足
        // 语义（§8.2/§4.4 状态图"Completed：幸存集为空（OPT-SEARCH-EMPTY，
        // 非不可行）"），调整策略/预算后可重跑。retry-task 族→UserRetry。
        makeDescriptor(kOptSearchEmpty,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 14：OPT-EXPORT-CONTRACT-STALE（error）——导出契约过期 ----
        // 分类 format-or-version：归类段点名归第一组——"导出契约/证据
        // 契约版本过期"（§11.4 版本三元组校验；reporting RP-CUR-2 同源）
        // 是典型 format-or-version 面。refresh/复核面（契约对齐后重新
        // 导出）→UserRetry。
        makeDescriptor(kOptExportContractStale,
                       diagnostics::DiagnosticCategory::FormatOrVersion,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 15：OPT-APPLY-PLAN-INVALID（error）——应用组装非法 ----
        // 分类 input-invalid：归类段点名 APPLY-PLAN-INVALID 归第一组——
        // 基线过期/身份不符/补丁与结果不对应（§10.3 六项前置校验不满足
        // 即组装拒绝；step2 失败定位诊断同码——§10.2 原子性边界），修正
        // 应用来源后重试。fix-input 族→UserRetry。
        makeDescriptor(kOptApplyPlanInvalid,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       diagnostics::RetryKind::UserRetry),
    };
}

void registerOptimizationCodes(diagnostics::IDiagnosticRegistry& registry)
{
    // 装配期注册执行面（§6.6 注册协议——ownerUnit="optimization"）：逐码
    // 转发 registry.registerCode，注册期验证（句法/前缀-所有权/键唯一/
    // 字段不变量）全部由注册表侧执行——本函数零旁路、零吞错（重复注册
    // 抛 DiagnosticsError 不捕获，装配期 fail-fast）。
    for (const auto& descriptor : optimizationCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::optimization
