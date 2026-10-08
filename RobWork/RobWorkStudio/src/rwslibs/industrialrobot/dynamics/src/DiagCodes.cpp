/**
 * @file   DiagCodes.cpp
 * @brief  dynamics 稳定诊断码工厂的实现——§9.4 登记表全表 16 码描述符
 *         清单（逐码落值依据）与装配注册函数。
 *
 * 设计依据：
 *   - units/dynamics.md §9.4（拟注册清单设计基线——v1 为 15 行，WP-17-T05
 *     增量修订表尾增行 16〔DYN-FD-NUMERIC-ANOMALY〕：码/严重度/比较型/
 *     用途四列；逐码登记值与锚点的唯一权威）、§4.3（轨迹消费契约
 *     ——上游兼容性校验三拒绝码）、§4.6（序列纪律与边界情形——缺样本/
 *     非有限/部分结果）、§5.5（摩擦符号约定与数据不足降级三层）、§5.6
 *     （数值稳定性、失败语义与耦合链防御）、§6（正动力学一致性——建议
 *     证据项三态与 DYN-FD-* 三码＋数值异常增码）、§8.1（工况消费——空
 *     工况集合/引用缺失）
 *   - units/diagnostics.md §4.3（分类词表逐值语义锚点）、§4.4（分类—
 *     严重—动作族矩阵——retryable 机械映射）、§4.5（字段约束与"不私造
 *     参数名"展开登记纪律）
 *   - 先例：kinematics/src/DiagCodes.cpp（WP-15-T02 同款——依赖白名单含
 *     diagnostics 编译边的逐码注释登记落值依据形态；共字段抽取辅助
 *     makeDescriptor 同款）
 *   - 任务契约 tasks/foundation/WP-17-T02.json acceptance 2（v1 全表）＋
 *     tasks/foundation/WP-17-T05.json（行 16 增码随本批单元卡 §9.4 修订）
 *
 * 确定性（NFR-COR-02）：清单序＝§9.4 表行序；每次调用返回同序同值新
 * 清单（描述符为纯值聚合）；码值经 DiagCodes.hpp 常量引用（唯一书写点）；
 * 增码只允许表尾追加——既有行不重排。
 */

#include <sdurws/ird/dynamics/DiagCodes.hpp>

namespace sdurws::ird::dynamics {

namespace {

/**
 * @brief 单码描述符装配辅助：填入 16 码共用不变的字段（ownerUnit/键约定/
 * paramSchema/确认与可见性/登记版本），可变面（码值/分类/级别/比较标记/
 * 重试族）由调用点逐码实参给出。
 *
 * 抽取目的：共字段的登记口径只在函数体注释一处陈述，16 个调用点各自只
 * 携带差异字段——落值依据可读性与"同码同口径"两得（不引入任何运行期
 * 开销——描述符构造本就是值聚合；kinematics 同款辅助先例）。
 *
 * 共字段口径（§4.5/diagnostics 展开登记纪律）：
 *   - ownerUnit＝"dynamics"（§4.5 前缀-所有权表 DYN→dynamics）；
 *   - titleKey/detailKey＝"diag.<code-lower>.title/.detail"（P-DIAG-9
 *     命名约定——注册期键形校验强制，偏离即 Usage 拒绝）；
 *   - paramSchema＝"[]"（无参数显式声明——产码路径落地时按需增量登记
 *     并升 registryVersion，不私造参数名；比较型三要素走实例 comparison
 *     面，不占参数名）；
 *   - confirmable＝false（16 码均无 SA-15 确认流语义）；
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）；
 *   - registryVersion＝1（首次登记）；deprecated=false；
 *     supersededBy 缺省即 nullopt。
 *
 * @param code              [in] 码值文本（DiagCodes.hpp 常量——唯一书写点）
 * @param category          [in] 分类（§4.3 词表——逐码锚点见调用点注释）
 * @param severity          [in] 级别（§9.4"严重度"列原值）
 * @param requiresComparison [in] 比较型三要素强制（§9.4"比较型"列点名才置 true）
 * @param retryable         [in] 重试族（§4.4 动作族机械映射）
 * @return 填充完成的描述符（纯值——调用方拷贝入清单）
 */
diagnostics::CodeDescriptor makeDescriptor(std::string_view code,
                                           diagnostics::DiagnosticCategory category,
                                           diagnostics::DiagnosticSeverity severity,
                                           bool requiresComparison,
                                           diagnostics::RetryKind retryable)
{
    // 小写键派生与注册表 derivedTextKey 同一约定（diag.<code-lower>.段）
    // ——此处以显式字面量书写，逐码测试断言全表 32 键（16×title/detail）
    // 与码值小写逐字一致，失同步即失败（不重复实现派生逻辑防两处漂移）。
    std::string lower{code};
    for (char& ch : lower) {
        // 码值句法保证仅含 A-Z/0-9/'-'（isValidDiagCodeSyntax 前置），
        // ASCII 小写化无 locale 依赖（确定性——不经 std::locale 面）。
        if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
    }

    diagnostics::CodeDescriptor d;
    d.code = std::string{code};                                  // §9.4"码"列原文
    d.ownerUnit = "dynamics";                                    // §4.5 前缀-所有权表
    d.category = category;                                       // §4.3 词表落值（调用点锚点）
    d.severity = severity;                                       // §9.4"严重度"列
    d.titleKey = "diag." + lower + ".title";                     // P-DIAG-9 命名约定
    d.detailKey = "diag." + lower + ".detail";                   // （键/值分离，值归文案资源）
    d.paramSchema = "[]";                                        // 无参数显式声明（不私造参数名）
    d.confirmable = false;                                       // 无 SA-15 确认流语义
    d.requiresComparison = requiresComparison;                   // §9.4"比较型"列点名
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

std::vector<diagnostics::CodeDescriptor> dynamicsCodeDescriptors()
{
    // 清单序＝§9.4 表行序 1~16（确定性序；后续消费任务增码只允许表尾
    // 追加——既有行不重排）；逐码注释给出分类/重试族落值锚点（严重度与
    // 比较型直接取 §9.4 表列原值）。
    return {
        // ---- 行 1：DYN-INPUT-INVALID（error）——输入非法 ----
        // 分类 input-invalid：评估输入不满足冻结链前置（§4.1"不得混用
        // 不同项目/分支/修订/模型/轨迹和工况——跨修订引用→拒绝"；§9.4
        // 用途列"跨修订绑定/维度不匹配/配置非法/模式组合非法——①级
        // 素材"）。fix-input 族（修正输入后复评）→UserRetry。
        makeDescriptor(kDynInputInvalid,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 2：DYN-UPSTREAM-TRAJECTORY-INCOMPATIBLE（error）——上游轨迹不兼容 ----
        // 分类 input-invalid：轨迹 payload 绑定的 model.robot-design cv
        // ≠本快照闭包内 cv（§4.3 兼容性校验第 2 条"模型一致"）——上游
        // 结果与当前快照不构成合法输入组合，拒绝评估。fix-input→UserRetry。
        makeDescriptor(kDynUpstreamTrajectoryIncompatible,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 3：DYN-TIME-PARAM-MISSING（error）——轨迹缺时间参数 ----
        // 分类 input-invalid：TimeParameterization 不存在或 totalDurationS≤0
        // （§4.3 第 2 条"时间参数完整"）——轨迹无时间参数即拒绝正式评估，
        // 不伪造节拍/功率/能量（§4.6"无时间参数"行）。fix-input→UserRetry。
        makeDescriptor(kDynTimeParamMissing,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 4：DYN-SERIES-NON-MONOTONIC（error）——时间轴非单调 ----
        // 分类 input-invalid：样本时间重复/倒退/零间隔（§4.3 第 2 条）——
        // 评估器不排序修复、不插值抹平（§4.6"时间重复/倒退/零间隔"行：
        // 排序会掩盖上游缺陷）。fix-input→UserRetry。
        makeDescriptor(kDynSeriesNonMonotonic,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 5：DYN-SAMPLE-GAP（warning，比较型）——缺样本/采样间隙 ----
        // 分类 data-insufficient：plannedSampleCount−actualSampleCount＝
        // 缺口（§4.6"缺样本/采样间隙"行）——Partial 不阻断该工况其余
        // 统计（④级素材而非输入非法）。requiresComparison=true：§9.4
        // 比较型列"（缺口数/计划数/1）"原文。supply-evidence 族（补全
        // 采样后同基准复评）→UserRetry。
        makeDescriptor(kDynSampleGap,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 6：DYN-NON-FINITE（error）——非有限输入/输出 ----
        // 分类 input-invalid：NaN/±Inf 输入或输出（§4.6"非有限数"行；
        // NFR-COR-03"非有限数拒绝"）——不静默转 0、不静默丢弃。fix-input
        // （修正物性/轨迹数据后复评）→UserRetry。
        makeDescriptor(kDynNonFinite,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 7：DYN-DIMENSION-MISMATCH（error，比较型）——维度不匹配 ----
        // 分类 input-invalid：轨迹关节数≠模型关节数（§5.6"输入维度不匹配"
        // 行——评估终止）。requiresComparison=true：§9.4 比较型列
        // "（实际/期望/1）"原文。fix-input→UserRetry。
        makeDescriptor(kDynDimensionMismatch,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 8：DYN-RNEA-FAILED（error）——RNEA 计算失败 ----
        // 分类 execution-failed：RNEA 递推数值异常（§5.6"RNEA 计算失败"
        // 行——工况级失败，附 t/段/关节定位）。重试族：卡面 §5.6 明文
        // "单工况失败不自动推出其他工况失败"——工况级重算语义成立
        // （retry-task）→UserRetry；与 kinematics SOLVER-INTERNAL（Never）
        // 的差异来自各自卡面失败边界的明文差异（如实登记，非映射疏漏）。
        makeDescriptor(kDynRneaFailed,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 9：DYN-FD-INITIAL-STATE-MISSING（warning）——FD 初始状态缺失 ----
        // 分类 data-insufficient：参考段起点状态不可解析（§6.2 第 2 行）
        // ——建议证据项 dyn.forward-dynamics-consistency＝NotRun（缺失
        // 不阻断，不伪造 Passed）。supply-evidence→UserRetry。
        makeDescriptor(kDynFdInitialStateMissing,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 10：DYN-FD-DIVERGED（warning）——积分发散 ----
        // 分类 execution-failed：正动力学积分发散/非有限（§6.1 ④——
        // DYN-05 异常检测本义）。严重度按 §9.4 表 warning（建议证据项
        // Failed 只降级该项——§6.3.5"正动力学失败不能直接判定模型无效"
        // ；分类与严重度正交，卡面登记值优先）。retry-task→UserRetry。
        makeDescriptor(kDynFdDiverged,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 11：DYN-FD-CONSISTENCY-FAILED（warning，比较型）——一致性超阈值 ----
        // 分类 execution-failed：检查已执行但响应误差超 config.dyn 容差
        // （§6.1 ④比较分支——outcome 层面建议证据项 Failed）。requiresComparison=
        // true：§9.4 比较型列"（实际误差/阈值/单位）"原文——附录 D C7
        // "分析配置来源"容差的比较型承载。retry-task→UserRetry。
        makeDescriptor(kDynFdConsistencyFailed,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Warning,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 12：DYN-PROPERTY-DOWNGRADED（warning）——物性/负载估算降级 ----
        // 分类 data-insufficient：GeometricEstimate 来源物性或 §5.4 负载
        // 保守估算（com→安装点、惯量→点质量）——DYN-06"不把估算结果
        // 包装成精确结论"；附来源计数（estimatedLinkCount 等）。supply-
        // evidence（补实测物性后复评）→UserRetry。
        makeDescriptor(kDynPropertyDowngraded,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 13：DYN-FRICTION-MISSING（warning）——摩擦参数缺失 ----
        // 分类 data-insufficient：MDL-16 摩擦 fv/fc/bias 任一 NotProvided
        // （§5.5 三层来源第 1 层；MDL-16 M-8 闭环"未填写标记 DataInsufficient
        // 并触发 DYN-06 降级"）——分项按 0 计入、证据不包装精确。supply-
        // evidence→UserRetry。
        makeDescriptor(kDynFrictionMissing,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 14：DYN-CONDITION-REF-MISSING（error）——工况引用缺失 ----
        // 分类 input-invalid：空工况集合/工况引用不可解析（§8.1 末条
        // "空工况集合/工况引用缺失→可定位诊断"）——评估对象集不成立。
        // fix-input→UserRetry。
        makeDescriptor(kDynConditionRefMissing,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 15：DYN-COUPLING-GATE-REJECTED（error）——耦合链防御拒绝 ----
        // 分类 format-or-version：快照携带 R2 才会出现的耦合结构标识而
        // MDL-21 未启用（§5.6 耦合链防御行——"跨版本快照"即 schema/契约
        // 不兼容族锚点）。DYN-04/M-12 红线兜底：绝不静默按独立关节链
        // 计算、绝不对角化/准静态替代。fix-input（用当前版本工具链重建
        // 快照）→UserRetry。
        makeDescriptor(kDynCouplingGateRejected,
                       diagnostics::DiagnosticCategory::FormatOrVersion,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 16（WP-17-T05 表尾增行）：DYN-FD-NUMERIC-ANOMALY（warning）
        //      ——正动力学数值异常 ----
        // 分类 execution-failed：检查已执行但积分数值路径异常——质量阵
        // 奇异/病态致线性求解失败（§6.1 ④"数值异常（刚性/溢出）"分支；
        // 异常判据＝消元主元越相对下界 1e-14×‖M‖∞，P-DYN-7 落位锁定）。
        // 不比较型：异常事实由 numericAnomaly 定位文本承载（首因/stage），
        // 无"实际/期望"比较对。Failed 只进建议证据项 Invalid＋warning
        // 诊断——不判定模型无效（§6.3.5）。fix-input（补全质量/惯量参数
        // 或减小步长）→UserRetry。
        makeDescriptor(kDynFdNumericAnomaly,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),
    };
}

void registerDynamicsCodes(diagnostics::IDiagnosticRegistry& registry)
{
    // 装配期注册执行面（§9.4 注册协议——ownerUnit="dynamics"）：逐码转发
    // registry.registerCode，注册期验证（句法/前缀-所有权/键唯一/字段
    // 不变量）全部由注册表侧执行——本函数零旁路、零吞错（重复注册抛
    // DiagnosticsError 不捕获，装配期 fail-fast）。
    for (const auto& descriptor : dynamicsCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::dynamics
