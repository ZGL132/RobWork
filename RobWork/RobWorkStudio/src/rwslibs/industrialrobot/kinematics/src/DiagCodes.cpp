/**
 * @file   DiagCodes.cpp
 * @brief  kinematics 稳定诊断码工厂的实现——§9.6 全表 17 码描述符清单
 *         （逐码落值依据）与装配注册函数。
 *
 * 设计依据：
 *   - units/kinematics.md §9.6 全表（17 行：码/级别/语义/任务列——逐码
 *     登记值与锚点的唯一权威）、§5.4（五类结局——过滤族码的素材走向）、
 *     §5.5（失败分类——错误/评价两级与比较型说明）
 *   - units/diagnostics.md §4.3（分类词表逐值语义锚点）、§4.4（分类—
 *     严重—动作族矩阵——retryable 机械映射）、§4.5（字段约束与"不私造
 *     参数名"展开登记纪律）
 *   - 先例：modeling/src/DiagCodes.cpp、requirements/src/DiagCodes.cpp
 *     （逐码注释登记落值依据的同款形态）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4
 *
 * 确定性（NFR-COR-02）：清单序＝§9.6 表行序；每次调用返回同序同值新
 * 清单（描述符为纯值聚合）；码值经 DiagCodes.hpp 常量引用（唯一书写点）。
 */

#include <sdurws/ird/kinematics/DiagCodes.hpp>

namespace sdurws::ird::kinematics {

namespace {

/**
 * @brief 单码描述符装配辅助：填入 17 码共用不变的字段（ownerUnit/键约定/
 * paramSchema/确认与可见性/登记版本），可变面（码值/分类/级别/比较标记/
 * 重试族）由调用点逐码实参给出。
 *
 * 抽取目的：共字段的登记口径只在函数体注释一处陈述，17 个调用点各自只
 * 携带差异字段——落值依据可读性与"同码同口径"两得（不引入任何运行期
 * 开销——描述符构造本就是值聚合）。
 *
 * 共字段口径（§4.5/diagnostics 展开登记纪律）：
 *   - ownerUnit＝"kinematics"（§4.5 前缀-所有权表 KIN→kinematics）；
 *   - titleKey/detailKey＝"diag.<code-lower>.title/.detail"（P-DIAG-9
 *     命名约定——注册期键形校验强制，偏离即 Usage 拒绝）；
 *   - paramSchema＝"[]"（无参数显式声明——产码路径落地时按需增量登记
 *     并升 registryVersion，不私造参数名；比较型三要素走实例 comparison
 *     面，不占参数名）；
 *   - confirmable＝false（17 码均无 SA-15 确认流语义）；
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）；
 *   - registryVersion＝1（首次登记）；deprecated=false；
 *     supersededBy 缺省即 nullopt。
 *
 * @param code              [in] 码值文本（DiagCodes.hpp 常量——唯一书写点）
 * @param category          [in] 分类（§4.3 词表——逐码锚点见调用点注释）
 * @param severity          [in] 级别（§9.6"级别"列原值）
 * @param requiresComparison [in] 比较型三要素强制（§5.5/§9.6 点名才置 true）
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
    // ——此处以显式字面量书写，逐码测试断言全表 34 键（17×title/detail）
    // 与码值小写逐字一致，失同步即失败（不重复实现派生逻辑防两处漂移）。
    std::string lower{code};
    for (char& ch : lower) {
        // 码值句法保证仅含 A-Z/0-9/'-'（isValidDiagCodeSyntax 前置），
        // ASCII 小写化无 locale 依赖（确定性——不经 std::locale 面）。
        if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
    }

    diagnostics::CodeDescriptor d;
    d.code = std::string{code};                                  // §9.6"码"列原文
    d.ownerUnit = "kinematics";                                  // §4.5 前缀-所有权表
    d.category = category;                                       // §4.3 词表落值（调用点锚点）
    d.severity = severity;                                       // §9.6"级别"列
    d.titleKey = "diag." + lower + ".title";                     // P-DIAG-9 命名约定
    d.detailKey = "diag." + lower + ".detail";                   // （键/值分离，值归文案资源）
    d.paramSchema = "[]";                                        // 无参数显式声明（不私造参数名）
    d.confirmable = false;                                       // 无 SA-15 确认流语义
    d.requiresComparison = requiresComparison;                   // §5.5/§9.6 比较型点名
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

std::vector<diagnostics::CodeDescriptor> kinematicsCodeDescriptors()
{
    // 清单序＝§9.6 表行序 1~17（确定性序；T05/T08 表尾各追加一行——既有
    // 行不重排）；逐码注释给出分类/重试族落值
    // 锚点（级别直接取 §9.6"级别"列）。比较标记仅两码 true——逐码注明。
    return {
        // ---- 行 1：KIN-NO-DEVICE（error）——无可用设备 ----
        // 分类 input-invalid：评估输入快照不满足接口前置（§9.2 IFkEvaluator
        // @pre"view 为请求绑定 RuntimeSnapshot"——设备缺失即输入面不成立；
        // §4.4 输入非法默认 Error 与 §9.6"error"一致）。重试族 fix-input
        // （配置设备后复评）→UserRetry。
        makeDescriptor(kKinNoDevice,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 2：KIN-NO-TCP（error）——TCP 未配置/悬空 ----
        // 分类 input-invalid：同上——TCP 引用悬空即接口前置不成立（§9.2
        // @错误 NoTcp）。fix-input→UserRetry。
        makeDescriptor(kKinNoTcp,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 3：KIN-TARGET-ILLEGAL（error）——目标位姿/容差非法 ----
        // 分类 input-invalid：目标位姿/容差是求解请求的输入分量（§9.6
        // 语义"目标位姿/容差非法（InvalidTarget）"；§4.4 fix-input＝定位
        // 对象/字段→编辑）。→UserRetry。
        makeDescriptor(kKinTargetIllegal,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 4：KIN-RESIDUAL-EXCEEDED（warning）——FK 验算残差超容差 ----
        // 分类 data-insufficient：该解被残差硬过滤是结局 3"全部候选被
        // 过滤→DataInsufficient"（§5.4）的构型级明细素材——§4.3 数据不足
        // 行语义锚点"engineeringStatus=DataInsufficient 轴（搜索未果 C5）"。
        // requiresComparison=true：§9.6 本行原文"比较型：实际/期望/单位"
        // （§5.5"残差 m·rad"）。supply-evidence 族（补全输入后同基准复评）
        // →UserRetry。
        makeDescriptor(kKinResidualExceeded,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 5：KIN-JOINT-LIMIT-VIOLATED（warning）——解超限位 ----
        // 分类 policy-denied：§4.3 策略拒绝行语义锚点原文点名"行程上限
        // 硬口径"（限位硬过滤＝域内事实，非用户错误）。adjust-policy 族
        // →UserRetry。requiresComparison=false：§5.5 比较型举例未点名
        // 超限位（从严按卡面字面，参数面随 T04 消费者复核）。
        makeDescriptor(kKinJointLimitViolated,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 6：KIN-NEAR-LIMIT（warning）——接近限位（阈值读 policy）----
        // 分类 policy-denied：近限位判定阈值读 policy（§9.6 本行原文），
        // 属"域内事实，非用户错误"的策略阈值评价族（与行 5 同族呈现分组
        // ——§4.3 词表内最贴近族；分类仅驱动呈现分组不改写任何轴）。
        // requiresComparison=true：§5.5 比较型举例点名"裕量比无量纲"
        // （近限位裕量比 actual/expected/unit）。adjust-policy 族（调阈值）
        // →UserRetry。
        makeDescriptor(kKinNearLimit,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Warning,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 7：KIN-NEAR-SINGULAR（warning）——条件数恶化/可操作度低 ----
        // 分类 policy-denied：阈值读 policy（§9.6 本行原文）——同行 5/6
        // 的策略阈值评价族。requiresComparison=false：§5.5 比较型举例
        // （残差/裕量比）未点名条件数——从严按卡面字面，随 T03 消费者
        // 落位复核。adjust-policy 族→UserRetry。
        makeDescriptor(kKinNearSingular,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 8：KIN-COLLISION-FILTERED（warning）——构型级碰撞过滤 ----
        // 分类 policy-denied：§4.3 策略拒绝行语义锚点原文点名"碰撞过滤"
        // （§5.4 结局 4/C8——构型级碰撞仅过滤该解，域内事实非用户错误）。
        // adjust-policy 族→UserRetry。
        makeDescriptor(kKinCollisionFiltered,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 9：KIN-COLLISION-UNAVAILABLE（warning）——检测器缺失/
        // 策略未启用→证据缺失 ----
        // 分类 data-insufficient：§4.3 数据不足行典型来源码原文点名
        // "KIN-05 口径（POLICY-CLL-DETECTOR-UNAVAILABLE）"——缺检测器
        // →DataInsufficient 素材（KIN-05：不视为无碰撞）。supply-evidence
        // 族→UserRetry。
        makeDescriptor(kKinCollisionUnavailable,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 10：KIN-SEARCH-EXHAUSTED（warning）——搜索未果 ----
        // 分类 data-insufficient：§9.6 本行语义"→DataInsufficient 素材"
        // ＋§4.3 数据不足行锚点"搜索未果 C5"（结局 2/3 的汇总记录码）。
        // supply-evidence 族（扩大初值/预算后按同一冻结输入复评——§8.1）
        // →UserRetry。
        makeDescriptor(kKinSearchExhausted,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 11：KIN-SOLVER-INTERNAL（error）——求解器内部错误 ----
        // 分类 internal：§9.6 本行语义"求解器内部错误（SolverError；
        // fail-fast 轨道）"＝不变量违反/防御性检查失败族（§4.3 内部错误
        // 行锚点）。§4.4 internal 默认 Dev，但 §9.6 显式登记"error"——
        // 严重级按码面登记值优先（diagnostics 展开登记先例：RT-NAME-
        // CONFLICT＝internal/Error 同型组合）。report-bug 族→Never。
        makeDescriptor(kKinSolverInternal,
                       diagnostics::DiagnosticCategory::Internal,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::Never),

        // ---- 行 12：KIN-CONFIG-ILLEGAL（error）——求解配置非法 ----
        // 分类 input-invalid：求解配置（seed/容差/计数）是评估输入分量
        // （§9.6 本行语义"seed=0/容差≤0/计数负——I-KIN-4"；§4.4 fix-input
        // ＝定位字段→编辑配置）。→UserRetry。
        makeDescriptor(kKinConfigIllegal,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 13：KIN-SAMPLE-IDENTITY-MISMATCH（error）——样本集身份
        // 与快照不一致 ----
        // 分类 format-or-version：样本集身份与快照的绑定契约不兼容
        // （§9.6 本行语义"未冻结/漂移——§7.2"；§4.3 该族锚点"schema/
        // 契约不兼容（CON-04）"；EVI-CACHE-INCOMPATIBLE 同型先例——
        // 陈旧/外来数据集对不上当前版本）。choose-compatible 族（重新
        // 生成/换一致样本集）→UserRetry。
        makeDescriptor(kKinSampleIdentityMismatch,
                       diagnostics::DiagnosticCategory::FormatOrVersion,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 14：KIN-COVERAGE-ZERO-SAMPLES（warning）——零样本→
        // 覆盖率不定义 ----
        // 分类 data-insufficient：§9.6 本行语义"DataInsufficient 素材"
        // （§7.2 零样本→DataInsufficient——KIN-04 R8 口径）。supply-
        // evidence 族（提供样本后复评）→UserRetry。
        makeDescriptor(kKinCoverageZeroSamples,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 15：KIN-RESULT-INCOMPLETE（warning）——批次不完整 ----
        // 分类 execution-failed：§9.6 本行语义"批次不完整（取消/失败；
        // NotRun 清单）"——任务执行轴异常（§4.3 该族锚点"outcome=Failed
        // 轴"；"取消失败归执行失败类"同段）。§4.4 该族默认 Error，§9.6
        // 登记为 warning（用户级内按码面登记值——批次不完整不阻断已
        // 完成部分的呈现）。retry-task 族（续跑/重跑批次）→UserRetry。
        makeDescriptor(kKinResultIncomplete,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 16：KIN-POINT-REF-DANGLING（error）——任务点引用悬空 ----
        // 随 WP-15-T05 表尾追加（§9.6 行 16；登记随卡 §14.6 v0.5）：批量
        // 通道展开期发现 appliesTo 显式清单引用点集外对象（悬空引用——
        // V-12），对应工作项产 InputInvalid 素材并附本码诊断（该项不参与
        // 求解、不影响其余工作项）。分类 input-invalid：§9.6 本行语义命中
        // §4.3"输入/配置非法"族（引用悬空＝数据引用面非法——fix-input
        // 族→UserRetry）。级别 error：该工作项的评估无法进行（与
        // KIN-TARGET-ILLEGAL"目标非法"同族——不能评估≠评估失败）。
        makeDescriptor(kKinPointRefDangling,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 17：KIN-ROOT-BYTES-ILLEGAL（error）——设默认门面根对象
        // 字节补丁失败 ----
        // 随 WP-15-T08 表尾追加（§9.6 行 17；登记随卡 §14.6 v0.8）：设默
        // 认 TCP 命令门面（§9.7/D-KIN-5）的根对象字节补丁缝返回失败（基
        // 线根不可解码或补丁产物编码失败——数据侧错误）→本次提交未发出
        // （零修订零提交），门面回显携带本码诊断。分类 format-or-version：
        // §9.6 本行语义命中 §4.3"schema/契约不兼容"族（对象字节与预期
        // canonical 格式不符——解码面失配，非调用方输入错误）；fix-input
        // 族（修复项目数据后重试）→UserRetry。级别 error：本次设默认操
        // 作无法完成（与 KIN-TARGET-ILLEGAL"无法进行"同族——不产半提交）。
        makeDescriptor(kKinRootBytesIllegal,
                       diagnostics::DiagnosticCategory::FormatOrVersion,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),
    };
}

void registerKinematicsCodes(diagnostics::IDiagnosticRegistry& registry)
{
    // 逐条转发注册：注册期验证（句法/前缀-所有权/paramSchema/键唯一与
    // 键形/字段不变量）全部在注册表内执行；任何一条失败即抛
    // DiagnosticsError——装配期 fail-fast，不静默跳过（NFR-MNT-03；
    // requirements/modeling registerXxxCodes 同款语义）。
    for (const auto& descriptor : kinematicsCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::kinematics
