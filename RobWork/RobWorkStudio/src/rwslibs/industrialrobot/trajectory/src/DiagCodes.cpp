/**
 * @file   DiagCodes.cpp
 * @brief  trajectory 稳定诊断码工厂的实现——§14.4 登记表全表 12 码描述
 *         符清单（逐码落值依据）与装配注册函数。
 *
 * 设计依据：
 *   - units/trajectory.md §14.4（拟注册清单 v1 设计基线——12 行：码/
 *     严重度/比较型/用途四列；逐码登记值与锚点的唯一权威）、§8.3/§8.5
 *     （IK 分支跳变/奇异邻域——warning 素材两码的触发条件）、§9.2（连
 *     续性破坏诊断示例）、§11.2/§11.3（复检预算/验证器缺失——两码的
 *     R9 定位义务）、§12.2/§12.6（限值校验唯一判定点/超限失败定位——
 *     超标与时间化失败两码）、§12.4（节拍输出——Should 口径超标码）、
 *     §15.13（导出——io 通道错误转译码）
 *   - units/diagnostics.md §4.3（分类词表逐值语义锚点）、§4.4（分类—
 *     严重—动作族矩阵——retryable 机械映射）、§4.5（字段约束与"不私造
 *     参数名"展开登记纪律）
 *   - 先例：kinematics/src/DiagCodes.cpp（WP-15-T02 同款——依赖白名单
 *     含 diagnostics 编译边的逐码注释登记落值依据形态；共字段抽取辅助
 *     makeDescriptor 同款）、dynamics/src/DiagCodes.cpp（WP-17-T02 同因
 *     同款）
 *   - 任务契约 tasks/foundation/WP-16-T03.json acceptance 3
 *
 * 确定性（NFR-COR-02）：清单序＝§14.4 表行序；每次调用返回同序同值新
 * 清单（描述符为纯值聚合）；码值经 DiagCodes.hpp 常量引用（唯一书写点）。
 */

#include <sdurws/ird/trajectory/DiagCodes.hpp>

namespace sdurws::ird::trajectory {

namespace {

/**
 * @brief 单码描述符装配辅助：填入 12 码共用不变的字段（ownerUnit/键约定/
 * paramSchema/确认与可见性/登记版本），可变面（码值/分类/级别/比较标记/
 * 重试族）由调用点逐码实参给出。
 *
 * 抽取目的：共字段的登记口径只在函数体注释一处陈述，12 个调用点各自只
 * 携带差异字段——落值依据可读性与"同码同口径"两得（不引入任何运行期
 * 开销——描述符构造本就是值聚合；kinematics/dynamics 同款辅助先例）。
 *
 * 共字段口径（§4.5/diagnostics 展开登记纪律）：
 *   - ownerUnit＝"trajectory"（§4.5 前缀-所有权表 TRJ→trajectory）；
 *   - titleKey/detailKey＝"diag.<code-lower>.title/.detail"（P-DIAG-9
 *     命名约定——注册期键形校验强制，偏离即 Usage 拒绝）；
 *   - paramSchema＝"[]"（无参数显式声明——产码路径落地时按需增量登记
 *     并升 registryVersion，不私造参数名；比较型五要素走实例 comparison
 *     面，不占参数名）；
 *   - confirmable＝false（12 码均无 SA-15 确认流语义——R1 轨迹域无
 *     可确认诊断产生点，卡 §14.4 末条）；
 *   - userVisible/reportable/historical＝true（全部为用户级码——§4.5
 *     仅对 Dev 码强制三项 false）；
 *   - registryVersion＝1（首次登记）；deprecated=false；
 *     supersededBy 缺省即 nullopt。
 *
 * @param code              [in] 码值文本（DiagCodes.hpp 常量——唯一书写点）
 * @param category          [in] 分类（§4.3 词表——逐码锚点见调用点注释）
 * @param severity          [in] 级别（§14.4"严重度"列原值）
 * @param requiresComparison [in] 比较型三要素强制（§14.4"比较型"列点名才置 true）
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
    // ——此处以显式字面量书写，逐码测试断言全表 24 键（12×title/detail）
    // 与码值小写逐字一致，失同步即失败（不重复实现派生逻辑防两处漂移）。
    std::string lower{code};
    for (char& ch : lower) {
        // 码值句法保证仅含 A-Z/0-9/'-'（isValidDiagCodeSyntax 前置），
        // ASCII 小写化无 locale 依赖（确定性——不经 std::locale 面）。
        if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
    }

    diagnostics::CodeDescriptor d;
    d.code = std::string{code};                                  // §14.4"码"列原文
    d.ownerUnit = "trajectory";                                  // §4.5 前缀-所有权表
    d.category = category;                                       // §4.3 词表落值（调用点锚点）
    d.severity = severity;                                       // §14.4"严重度"列
    d.titleKey = "diag." + lower + ".title";                     // P-DIAG-9 命名约定
    d.detailKey = "diag." + lower + ".detail";                   // （键/值分离，值归文案资源）
    d.paramSchema = "[]";                                        // 无参数显式声明（不私造参数名）
    d.confirmable = false;                                       // 无 SA-15 确认流语义（卡 §14.4 末条）
    d.requiresComparison = requiresComparison;                   // §14.4"比较型"列点名
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

std::vector<diagnostics::CodeDescriptor> trajectoryCodeDescriptors()
{
    // 清单序＝§14.4 表行序 1~12（确定性序；后续消费任务增码只允许表尾
    // 追加——既有行不重排）；逐码注释给出分类/重试族落值锚点（严重度与
    // 比较型直接取 §14.4 表列原值）。
    return {
        // ---- 行 1：TRJ-INPUT-INVALID（error）——输入非法 ----
        // 分类 input-invalid：评估输入不满足前置（§14.4 用途列"序列环/
        // 悬空引用/配置非法/模式组合非法——对应汇总①级素材"；§5.4 序列
        // 展开环检测、§5.5 配置校验）。fix-input 族（修正输入后复评）
        // →UserRetry。
        makeDescriptor(kTrjInputInvalid,
                       diagnostics::DiagnosticCategory::InputInvalid,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 2：TRJ-NO-PATH（error）——无路径 ----
        // 分类 execution-failed：输入合法但求解未果（§14.4 用途列"IK 无
        // 解/规划失败段定位"——§7.5 候选解选择无果、§10 规划失败段），
        // 属 outcome=Failed 轴（TASK-02）素材而非调用方输入错误。
        // retry-task 族（调整构型延续/初值策略后重算）→UserRetry。
        makeDescriptor(kTrjNoPath,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 3：TRJ-BRANCH-JUMP（warning）——关节分支跳变 ----
        // 分类 data-insufficient：跳变记录使该段 IK 分支连续性证据不可
        // 用（§8.3——附采样点 s 与两端解摘要的段级明细素材），与
        // kinematics KIN-RESIDUAL-EXCEEDED 同构：该明细是"段级淘汰→
        // 汇总素材缺口"（§4.3 数据不足行锚点"engineeringStatus=
        // DataInsufficient 轴"）的构成面。supply-evidence 族（调整采样/
        // 分支延续策略后同冻结输入复评）→UserRetry。
        makeDescriptor(kTrjBranchJump,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Warning,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 4：TRJ-SINGULAR-NEIGHBORHOOD（warning，比较型）——奇异邻域 ----
        // 分类 policy-denied：奇异邻域判定阈值读 policy（§8.5/V-07——
        // "policy conditionNumberWarning 启用"；阈值缺失→notApplicable
        // 显式标记，零自设数值）——与 kinematics KIN-NEAR-SINGULAR 同族
        // 的策略阈值评价（§4.3 策略拒绝行"域内事实，非用户错误"；分类
        // 仅驱动呈现分组不改写任何轴）。requiresComparison=true：§14.4
        // 比较型列"（条件数/阈值/1）"原文。adjust-policy 族（调阈值）
        // →UserRetry。
        makeDescriptor(kTrjSingularNeighborhood,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Warning,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 5：TRJ-LIMIT-EXCEEDED（error，比较型）——限值超标 ----
        // 分类 policy-denied：§4.3 策略拒绝行语义锚点原文点名"行程上限
        // 硬口径"（§12.2 限值校验唯一判定点——模型限位/速度/加速度约束
        // 的域内事实评价，非用户输入错误；与 kinematics KIN-JOINT-LIMIT-
        // VIOLATED 同锚点）。severity 按 §14.4 表登记 error（码面值优先
        // ——KIN-SOLVER-INTERNAL 先例：分类默认级与码面级正交，码面登记
        // 值收窄生效）。requiresComparison=true：§14.4 比较型列"（实际/
        // 期望/单位）"原文。adjust-policy 族（调规划参数/限值来源复核）
        // →UserRetry。
        makeDescriptor(kTrjLimitExceeded,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Error,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 6：TRJ-CONTINUITY-BROKEN（error，比较型）——连续性破坏 ----
        // 分类 execution-failed：连续性检查已执行但实测差超容差（§9.2
        // 诊断示例；§9.1"至少加速度连续"R1 验收锚点破坏→该段结论不可
        // 用）——与 dynamics DYN-FD-CONSISTENCY-FAILED 同构："检查已
        // 执行但响应误差超容差"的 outcome 层面 Failed 素材（TASK-02）。
        // requiresComparison=true：§14.4 比较型列"（实际差/容差/单位）"
        // 原文。retry-task 族（调整采样/平滑参数后重算）→UserRetry。
        makeDescriptor(kTrjContinuityBroken,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Error,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 7：TRJ-RECHECK-COLLISION（error）——平滑后重新碰撞 ----
        // 分类 policy-denied：§4.3 策略拒绝行语义锚点原文点名"碰撞过滤"
        // （§11.1 复检强制边检出——平滑诱发碰撞的域内事实，非用户错误；
        // 与 kinematics KIN-COLLISION-FILTERED 同锚点）。adjust-policy
        // 族（调安全间距/回退未平滑候选重规划——§11.3）→UserRetry。
        makeDescriptor(kTrjRecheckCollision,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 8：TRJ-RECHECK-BUDGET-EXHAUSTED（error，比较型）——细分预算耗尽 ----
        // 分类 data-insufficient：细分预算耗尽→段级 DataInsufficient
        // 素材（§14.4 用途列"细分预算耗尽（R9：附实际最大步长与预算
        // 占用）"；V-19 反例——"不默认接受"）——§4.3 数据不足行锚点
        // "搜索未果 C5"同族（细分搜索预算用尽）。requiresComparison=
        // true：§14.4 比较型列"（实际步长/预算）"原文。supply-evidence
        // 族（增大预算后同冻结输入复评——D-TRJ-5 零自设预算值）→
        // UserRetry。
        makeDescriptor(kTrjRecheckBudgetExhausted,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Error,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 9：TRJ-RECHECK-DATA-INSUFFICIENT（error）——复检证据缺失 ----
        // 分类 data-insufficient：§4.3 数据不足行典型来源码原文点名
        // "KIN-05 口径（POLICY-CLL-DETECTOR-UNAVAILABLE）"——验证器/
        // 碰撞证据缺失不视为无碰撞（V-20 反例；KIN-COLLISION-UNAVAILABLE
        // 同款处置）。supply-evidence 族→UserRetry。
        makeDescriptor(kTrjRecheckDataInsufficient,
                       diagnostics::DiagnosticCategory::DataInsufficient,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 10：TRJ-TIME-PARAM-FAILED（error）——时间参数化失败 ----
        // 分类 execution-failed：时间化计算未产出可用时间轴（§12.2/§12.6
        // ——含限值未定义情形；V-18 反例"节拍 NotProvided 不伪造"），
        // 属 outcome=Failed 轴（TASK-02）。retry-task 族（修正限值定义/
        // 输入后重算）→UserRetry。
        makeDescriptor(kTrjTimeParamFailed,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 11：TRJ-CYCLE-TIME-EXCEEDED（warning，比较型）——节拍超标 ----
        // 分类 policy-denied：目标节拍为 Should 口径的工程策略阈值
        // （§12.4 节拍输出；不阻断——warning 素材），属"域内事实，非
        // 用户错误"的策略阈值评价族（§4.3 策略拒绝行）。requiresComparison=
        // true：§14.4 比较型列"（实际节拍/目标/s）"原文（单位＝秒）。
        // adjust-policy 族（调工艺节拍目标）→UserRetry。
        makeDescriptor(kTrjCycleTimeExceeded,
                       diagnostics::DiagnosticCategory::PolicyDenied,
                       diagnostics::DiagnosticSeverity::Warning,
                       true,
                       diagnostics::RetryKind::UserRetry),

        // ---- 行 12：TRJ-EXPORT-FAILED（error）——导出失败 ----
        // 分类 execution-failed：导出操作失败（§15.13/§14.5.1 导出命令
        // ——io 通道错误转译；先前输出完整保留语义随 io），属 outcome=
        // Failed 轴。retry-task 族（排除通道问题后重新导出）→UserRetry。
        makeDescriptor(kTrjExportFailed,
                       diagnostics::DiagnosticCategory::ExecutionFailed,
                       diagnostics::DiagnosticSeverity::Error,
                       false,
                       diagnostics::RetryKind::UserRetry),
    };
}

void registerTrajectoryCodes(diagnostics::IDiagnosticRegistry& registry)
{
    // 装配期注册执行面（§14.4 注册协议——ownerUnit="trajectory"）：逐码
    // 转发 registry.registerCode，注册期验证（句法/前缀-所有权/键唯一/
    // 字段不变量）全部由注册表侧执行——本函数零旁路、零吞错（重复注册
    // 抛 DiagnosticsError 不捕获，装配期 fail-fast）。
    for (const auto& descriptor : trajectoryCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::trajectory
