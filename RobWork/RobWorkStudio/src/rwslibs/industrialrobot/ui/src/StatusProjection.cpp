/**
 * @file   StatusProjection.cpp
 * @brief  七态公共状态呈现的实现——词表 token/文案键表与 §6.3 映射求值。
 *
 * 设计依据：
 *   - units/ui.md §6.3（UX-10 七态状态词表与映射：词表 token/中文列、七态×
 *     权威词表映射表、求值优先级 empty-project＞computing＞incomplete＞
 *     failed＞data-insufficient＞results-stale＞computable、九态短标签、
 *     P-UI-2 建议口径、FormalPassEligibility 显示纪律）、§6.8（UI 状态×
 *     任务状态×工程状态×当前性正交关系表——显示纪律的组合呈现规则）、
 *     §3.5（文案键体系 state.<token>.label）、§3.3（UiProjections.hpp 承载）；
 *   - §3.2 冻结基准（投影值形态逐字段对齐对端锚点，不重定义权威类型——
 *     NFR-MNT-03）：core 九态/结局/工程判定词表（core.md §4.7 token）、
 *     evidence CurrentnessResult/EvidenceManifest/FormalPassEligibility、
 *     execution TaskSnapshot/ProgressReport、evidence §6.4.1① ReadinessSummary；
 *   - 需求 UX-06（统一状态词与图例——ui 主责）、UX-10（七态统一呈现）、
 *     PM-03/PM-11（九态短标签/状态栏）；P-UI-1（冻结稿实现）、P-UI-2/
 *     P-EV-4（NotEvaluable 呈现＝results-stale＋「无法判定」原因）。
 *
 * 背景说明（为什么实现是纯函数＋静态表）：七态是呈现投影而非判定（R-2），
 * 本翻译单元不持有任何可变状态、不回写权威对象；全部词表数据编译期固定
 * （与 §6.3 冻结稿逐字对应——词表/优先级变更必须走单元卡增量修订，不允许
 * 运行期注入改词表）。九态 token 直接复用 core::toToken(TaskState) 的词表
 * 对应关系（键里的 token 段与 core 持久化 token 一致——单一权威，不另立
 * 第二份九态 token 表）。
 *
 * 线程安全：全部函数纯/只读，可重入（UI 线程调用约定见头文件）。
 */

#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiText.hpp>

#include <array>
#include <utility>

namespace sdurws::ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 七态词表静态数据（§6.3 词表表序＝枚举序＝下表序——三序一致便于核对）
// ---------------------------------------------------------------------

/// 词表行数（§6.3 七态——编译期自检下界；少一行即漏登记）。
constexpr std::size_t kStatusWordCount = 7;

/// 七态词表行（token＋中文——§6.3 表前两列原文；中文列自 UI-T09 起的
/// 解析值源在 UiText 内建过渡文案表，本表保留中文列作词表完整性对照，
/// token 列是 statusWordToken 的唯一数据源）。
struct StatusWordRow
{
    StatusWord word;        ///< 枚举值（表序）
    const char* token;      ///< 冻结 token（小写连字符——持久化/日志用）
    const char* label;      ///< 中文呈现词（§6.3"中文"列——对照用，解析经 UiText）
};

/// 词表全表（行序＝枚举序；增删行必须同步 StatusWord 枚举与单元卡 §6.3）。
constexpr std::array<StatusWordRow, kStatusWordCount> kStatusWordTable{{
    { StatusWord::EmptyProject,     "empty-project",     "空项目"   },
    { StatusWord::Incomplete,       "incomplete",        "未完成"   },
    { StatusWord::Computing,        "computing",         "计算中"   },
    { StatusWord::ResultsStale,     "results-stale",     "结果过期" },
    { StatusWord::DataInsufficient, "data-insufficient", "数据不足" },
    { StatusWord::Failed,           "failed",            "失败"     },
    { StatusWord::Computable,       "computable",        "可计算"   },
}};

/// 七态词表静态自检（表序与枚举序一致——错位会在 token/标签上串行，
/// 编译期数组无法表达该约束，此处以惰性校验兜底：首表行必须为枚举首值）。
static_assert(kStatusWordTable[0].word == StatusWord::EmptyProject,
              "七态词表行序必须与 StatusWord 枚举序一致（ui.md §6.3）");

// ---------------------------------------------------------------------
// 九态短标签静态数据（§6.3"九态短标签"行——PM-03/PM-11）
// ---------------------------------------------------------------------
// （九态中文短标签值已随 UI-T09 迁入 UiText 内建过渡文案表——键
// state.<token>.label 的 token 半区仍由本表与 core::toToken 词表逐字
// 对应；本翻译单元只保留键所需的 token 表。）

/// 九态 token（与 core::toToken(TaskState) 词表逐字一致——core.md §4.7
/// 持久化契约；此处独立成表是因为 toToken 定义于 core 翻译单元，键构造
/// 需要编译期字面量。一致性由测试 StatusWordModel.NineStateLabelKeys
/// 对断 core token 逐字核对，防两表漂移）。
constexpr std::array<std::pair<core::TaskState, const char*>, 9> kTaskStateTokenTable{{
    { core::TaskState::Queued,      "queued"      },
    { core::TaskState::Preparing,   "preparing"   },
    { core::TaskState::Running,     "running"     },
    { core::TaskState::Paused,      "paused"      },
    { core::TaskState::Canceling,   "canceling"   },
    { core::TaskState::Canceled,    "canceled"    },
    { core::TaskState::Completed,   "completed"   },
    { core::TaskState::Failed,      "failed"      },
    { core::TaskState::Interrupted, "interrupted" },
}};

const char* findTaskStateToken(core::TaskState state) noexcept
{
    for (const auto& row : kTaskStateTokenTable) {
        if (row.first == state) {
            return row.second;
        }
    }
    return "";  // 不可达——同上
}

}  // namespace

// =====================================================================
// 词表 token 与文案键
// =====================================================================

const char* statusWordToken(StatusWord word) noexcept
{
    // 词表全量覆盖七枚举（枚举封闭）——顺序扫描命中即返回。
    for (const auto& row : kStatusWordTable) {
        if (row.word == word) {
            return row.token;
        }
    }
    return "";  // 不可达（非法枚举值防御——返回空串便于日志显性暴露）
}

std::string statusWordLabelKey(StatusWord word)
{
    // 键约定 state.<token>.label（§3.5 文案键体系）——键即冻结契约，
    // UI-T09 UiText 落地后值经资源文件解析，键不变。
    return std::string("state.") + statusWordToken(word) + ".label";
}

std::string statusWordTransitionalLabel(StatusWord word)
{
    // 值源已随 UI-T09 切换至 UiText 内建过渡文案表（§3.5 唯一解析出口——
    // "一切文本经 UiText::resolve"；键 statusWordLabelKey(word) 不变，值
    // 逐字同源＝§6.3"中文"列原文。过渡函数保留至资源文件交接后退役，
    // 届时键不变）。非法枚举的防御行为从"返回空串"收紧为"缺键 fail-fast"
    // （枚举封闭、正常路径不可达——调用方违约显性暴露优于静默空串）。
    return resolveText(statusWordLabelKey(word));
}

int statusWordPriority(StatusWord word) noexcept
{
    // 冻结优先级（§6.3 原文）：empty-project＞computing＞incomplete＞
    // failed＞data-insufficient＞results-stale＞computable。
    // 词表行序≠优先级序，此处显式映射——变更必须走单元卡增量修订。
    switch (word) {
        case StatusWord::EmptyProject:     return 0;
        case StatusWord::Computing:        return 1;
        case StatusWord::Incomplete:       return 2;
        case StatusWord::Failed:           return 3;
        case StatusWord::DataInsufficient: return 4;
        case StatusWord::ResultsStale:     return 5;
        case StatusWord::Computable:       return 6;
    }
    return 99;  // 不可达（非法枚举值排最低优先——防御性兜底）
}

std::optional<StatusWord> dominantStatusWord(const std::vector<StatusWord>& words)
{
    // 工作台总徽标＝取各活跃阶段中优先级最高者（§6.3 原文）。
    // 空输入返回 nullopt：无活跃阶段时不虚构状态词（N-12"不伪造业务事实"）。
    if (words.empty()) {
        return std::nullopt;
    }
    const StatusWord* best = nullptr;
    for (const auto& word : words) {
        if (best == nullptr || statusWordPriority(word) < statusWordPriority(*best)) {
            best = &word;
        }
    }
    return *best;
}

std::string taskStateLabelKey(core::TaskState state)
{
    // 键约定 state.<token>.label（§3.5；PM-03/PM-11）——token 段与 core
    // 持久化 token 一致（九态单一权威，键由 core 词表派生）。
    return std::string("state.") + findTaskStateToken(state) + ".label";
}

std::string taskStateTransitionalLabel(core::TaskState state)
{
    // 值源已随 UI-T09 切换至 UiText 内建过渡文案表（键 taskStateLabelKey
    // (state) 不变——token 段与 core toToken 词表一致；值逐字同源＝§6.3
    // 九态短标签行原文）。非法枚举经缺键 fail-fast（同上——显性暴露）。
    return resolveText(taskStateLabelKey(state));
}

std::string currentnessUnevaluableLabelKey(NotEvaluableCause cause)
{
    // 键约定 state.currentness.unevaluable.<cause>.label——cause token 与
    // evidence UnevaluableCause 语义一一对应（跨上下文/依赖无法解析）。
    switch (cause) {
        case NotEvaluableCause::CrossContext:
            return "state.currentness.unevaluable.cross-context.label";
        case NotEvaluableCause::UnresolvedDependency:
            return "state.currentness.unevaluable.unresolved-dependency.label";
    }
    return "";  // 不可达（非法枚举值防御）
}

std::string currentnessUnevaluableTransitionalLabel(NotEvaluableCause cause)
{
    // 值源已随 UI-T09 切换至 UiText 内建过渡文案表（键
    // currentnessUnevaluableLabelKey(cause) 不变；值逐字同源＝P-UI-2
    // 建议口径原文——冻结前不私定其它措辞）。非法枚举经缺键 fail-fast。
    return resolveText(currentnessUnevaluableLabelKey(cause));
}

// =====================================================================
// 证据清单不满足项（§6.8"缺失项全量清单"数据源）
// =====================================================================

std::vector<EvidenceItemProjection>
unsatisfiedEvidenceItems(const EvidenceManifestProjection& manifest)
{
    // 不满足＝Missing/Invalid/Unverified（evidence §6.2 判定后果原文三态）；
    // Satisfied＝满足、NotApplicable＝显式不适用不计缺失（C2/ERR-01）。
    // 全量列出、不短路、保持原序（表 2 ④同口径——呈现顺序不重排）。
    std::vector<EvidenceItemProjection> unsatisfied;
    for (const auto& item : manifest.items) {
        switch (item.status) {
            case EvidenceItemProjection::Status::Missing:
            case EvidenceItemProjection::Status::Invalid:
            case EvidenceItemProjection::Status::Unverified:
                unsatisfied.push_back(item);
                break;
            case EvidenceItemProjection::Status::Satisfied:
            case EvidenceItemProjection::Status::NotApplicable:
                break;  // 满足/显式不适用——不进缺失清单
        }
    }
    return unsatisfied;
}

// =====================================================================
// 七态求值（§6.3 映射表＋优先级的唯一实现）
// =====================================================================

StatusWordProjection evaluateStatusWord(const StatusFacts& facts)
{
    // 输出先取 formalPass 同快照随行：无论命中哪个七态，"是否可显示正式
    // 通过"都必须有与 word 同一时刻的数据源（§6.8 组合呈现行——显示纪律
    // 的唯一放行数据源是 FormalPassEligibility，不允许呈现层二次取数拼接）。
    StatusWordProjection out;
    out.formalPass = facts.formalPass;

    // ---- 优先级 0：empty-project ------------------------------------
    // 触发：UiSessionState ∈ {NoProject}（§5.2）——无项目时任务/判定/当前性
    // 轴均无语义（映射表行 1 全"—"），最先短路。
    if (!facts.projectOpen) {
        out.word = StatusWord::EmptyProject;
        return out;
    }

    // ---- 优先级 1：computing ----------------------------------------
    // 触发：当前作用域存在非终态任务（TaskState ∈ {Queued, Preparing,
    // Running, Paused, Canceling}——映射表行 3；非终态无 outcome，结局轴
    // 不参与本行）。计算中必须最先呈现（UX-10：用户需要取消入口）——
    // 伴随数据：进度阶段文案键＋取消可用位（acceptance 显示纪律）。
    if (!facts.tasks.activeStates.empty()) {
        out.word = StatusWord::Computing;
        out.progressStageKey = facts.tasks.progressStageKey;
        out.cancelAvailable = facts.tasks.cancelAvailable;
        return out;
    }

    // ---- 优先级 2：incomplete ---------------------------------------
    // 触发：就绪投影无效（任一启用 Must 条目非法——ReadinessSummary.valid
    // ==false，evidence §6.4.1①；REQ-06"输入未完成"，正式评估不派发）。
    // 输入级数据不足归本态，判定级归 data-insufficient（§6.3 词表注）。
    if (!facts.readiness.valid) {
        out.word = StatusWord::Incomplete;
        return out;
    }

    // ---- 优先级 3：failed -------------------------------------------
    // 触发（§6.3 failed 行触发数据源原文两分句）：①最近任务 outcome==
    // Failed（终态失败——映射表行 7：TaskState Failed（终态）/TaskOutcome
    // Failed/EngineeringStatus NotApplicable（表 3 约束——取消/失败/中断
    // 无工程判定，ui 不复判））；②Error 级诊断活跃。命中即伴随失败诊断
    // 投影项（"附对象定位与修复建议"——DiagProjectionItem 原样承载）。
    const bool latestTaskFailed = facts.tasks.latestOutcome.has_value()
        && *facts.tasks.latestOutcome == core::TaskOutcome::Failed;
    if (latestTaskFailed || facts.errorDiagActive) {
        out.word = StatusWord::Failed;
        out.failureDiagnostics = facts.failureDiagnostics;
        return out;
    }

    // ---- 优先级 4：data-insufficient --------------------------------
    // 触发：最近正式评估 EngineeringStatus==DataInsufficient（判定级——
    // 证据/工况缺失由 evidence 汇总判定，映射表行 6；输入级在优先级 2 已
    // 短路）。伴随数据：不满足证据项全量清单（§6.8 组合呈现行）。
    const bool latestDataInsufficient =
        facts.tasks.latestEngineeringStatus.has_value()
        && *facts.tasks.latestEngineeringStatus
               == core::EngineeringStatus::DataInsufficient;
    if (latestDataInsufficient) {
        out.word = StatusWord::DataInsufficient;
        out.unsatisfiedEvidence = unsatisfiedEvidenceItems(facts.evidence);
        return out;
    }

    // ---- 优先级 5：results-stale ------------------------------------
    // 触发（映射表行 5）：无活跃任务 ∧ Completed ∧ 当前性 Superseded——
    // 到此步时前两条件已由优先级 1/3 的短路保证（当前性轴是本行唯一剩余
    // 判别面）。Superseded 伴随逐条目失效原因清单（"附 InvalidationReason
    // 清单"）；NotEvaluable（status==nullopt 计算形态）按 P-UI-2 建议口径
    // 归入本态呈现＋「无法判定」原因——不显示为 Current、不显示为通过
    // （evidence"无默认 Current"规则不被违反；冻结前不私定其它口径）。
    if (facts.currentness.status == CurrentnessProjection::Status::Superseded) {
        out.word = StatusWord::ResultsStale;
        out.staleReasons = facts.currentness.reasons;
        return out;
    }
    if (!facts.currentness.status.has_value()) {
        out.word = StatusWord::ResultsStale;
        out.notEvaluableCause = facts.currentness.unevaluableCause;
        return out;
    }

    // ---- 优先级 6：computable ---------------------------------------
    // 兜底（映射表行 4）：就绪有效 ∧ 无在途计算 ∧ 无更高优先状态——结果
    // Current 或尚无结果均为可计算（"Current（可选）"的可选语义）。
    out.word = StatusWord::Computable;
    return out;
}

}  // namespace ui
}  // namespace ird
