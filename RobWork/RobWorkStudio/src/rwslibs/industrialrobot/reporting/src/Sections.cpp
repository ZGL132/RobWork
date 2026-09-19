/**
 * @file   Sections.cpp
 * @brief  章节词表与级别契约的实现——§5.1 词表查找/级别归属/LevelConflict
 *         与 ScopeInsufficient 判定原语/RPT-SCOPE-INSUFFICIENT 诊断挂接。
 *
 * 设计依据：
 *   - units/reporting.md §5.1（词表 14 项与词表规则）、§5.2（B/C 范围图：
 *     "B 级词表 ∩ C 专属词表＝∅；C 级零 C 章节选中＝ScopeInsufficient"）、
 *     §5.3（四态呈现——默认选择规则的语义源）、§5.4（降级与拒绝——保守
 *     处置 P-RPT-5）、§4.6（合法组合矩阵）
 *   - 任务契约 tasks/foundation/RPT-T04.json acceptance 1~4
 *
 * 实现口径：
 *   - 全部查找为词表 constexpr 数组上的线性扫描（≤14 项，注册/构建期一次
 *     性调用，非热点）——不引入哈希/排序容器，避免第二份词表副本（词表
 *     权威唯一＝头文件常量数组，PA-1）；
 *   - 全部判定为纯函数：无共享可变状态、无时钟/locale 依赖——同输入同
 *     结论（NFR-COR-02）；
 *   - 诊断文本在本文件以常量字符串拼装（诊断文案的"稳定码＋结构化字段"
 *     纪律：码值权威归 diagnostics，人读文案仅为 cause/recommendedAction
 *     载体——§4.3.3 渲染侧再经脱敏投影）。
 */

#include <sdurws/ird/reporting/Sections.hpp>

#include <exception>   // std::terminate（noexcept 兜底 fail-fast——见函数内注）
#include <string>
#include <vector>

namespace sdurws::ird::reporting {
namespace {

// ---------------------------------------------------------------------
// 词表线性查找工具（模板仅在内部实例化——不进公共契约面）
// ---------------------------------------------------------------------

/// 在词表数组中定位 token；命中返回下标，未命中返回数组长度（哨兵语义，
/// 调用方以 size 判别——避免 optional 的额外分支成本，语义等价）。
template <std::size_t N>
std::size_t indexOf(std::string_view sectionId, const std::array<std::string_view, N>& table)
{
    // 逐项精确比较（区分大小写——词表字面唯一形态，"宽容入口"会在持久化
    // 层造成二义，与 ReportLevel token 解析同口径：非规范输入在边界拒绝）。
    for (std::size_t i = 0; i < N; ++i) {
        if (table[i] == sectionId) {
            return i;
        }
    }
    return N;   // 未命中哨兵——调用方以 == N 判别
}

/// token 是否在词表数组内（indexOf 的布尔包装——可读性优先）。
template <std::size_t N>
bool contains(std::string_view sectionId, const std::array<std::string_view, N>& table)
{
    return indexOf(sectionId, table) != N;
}

/// 把 C 专属章节 token 集转换为字符串向量（判定结论/构建器注入的承载形）。
std::vector<std::string> toStringVector(const std::array<std::string_view, 6>& tokens)
{
    std::vector<std::string> out;
    out.reserve(tokens.size());
    for (const std::string_view t : tokens) {
        out.emplace_back(t);
    }
    return out;
}

}  // namespace

// =====================================================================
// 词表查找与级别归属（acceptance 1）
// =====================================================================

bool isValidSectionId(std::string_view sectionId) noexcept
{
    return contains(sectionId, kAllSectionIds);
}

std::optional<std::uint16_t> trySectionOrder(std::string_view sectionId) noexcept
{
    const std::size_t idx = indexOf(sectionId, kAllSectionIds);
    if (idx == kAllSectionIds.size()) {
        // 词表外 token：返回 nullopt 而非伪造序号（ERR-01 不伪造——order
        // 进入渲染/CSV/JSON 输出序，错误序号会污染全部下游格式）。
        return std::nullopt;
    }
    // 行号＝数组下标＋1（kAllSectionIds 序即 §5.1 表序 1~14——§4.3.4 order
    // 字段"§5 冻结顺序"的数值落点；uint16_t 承载 ReviewReportSection::order）。
    return static_cast<std::uint16_t>(idx + 1);
}

std::optional<ReportLevel> trySectionMinimumLevel(std::string_view sectionId) noexcept
{
    // B 章节先查（行 1~8）；未命中再查 C 专属表（行 9~14）——两表并集即
    // 词表全量（§5.1：B 章节 8 项＋C 专属 6 项＝14 项，无第三级别）。
    if (contains(sectionId, kBLevelSectionIds)) {
        return ReportLevel::B;
    }
    if (contains(sectionId, kCExclusiveSectionIds)) {
        return ReportLevel::C;
    }
    return std::nullopt;
}

bool isFrameworkSectionId(std::string_view sectionId) noexcept
{
    return contains(sectionId, kFrameworkSectionIds);
}

bool isDomainSectionId(std::string_view sectionId) noexcept
{
    return contains(sectionId, kDomainSectionIds);
}

ReportLevelRule frozenReportLevelRule()
{
    ReportLevelRule rule;
    // C 专属词表注入（§4.6 判定的权威源——ReportModel 侧零复制，词表权威
    // 单点在本头；kCExclusiveSectionIds 序＝§5.1 行 9~14 序）。
    rule.cExclusiveSectionIds = toStringVector(kCExclusiveSectionIds);
    return rule;
}

// =====================================================================
// 级别×章节判定原语（acceptance 2/4）
// =====================================================================

std::optional<ReportErrorCode> sectionLevelConflict(ReportLevel level,
                                                    std::string_view sectionId)
{
    // 仅 B∧C 专属章节构成冲突（§4.6 行 2"B 级携带 C 章节＝LevelConflict
    // 拒绝——不能伪造 C 级章节"）。其余三种组合均为合法：
    //   B∧B（B 报告的常规章节）、C∧B（C＝B 全部＋追加——§5.2）、
    //   C∧C（C 报告的追加章节）；
    // 词表外 token 返回 nullopt——本原语不裁决词表外情形（其拒绝面在
    // SectionRegistry 注册边界与构建器字段校验，见头文件注）。
    if (level == ReportLevel::B && contains(sectionId, kCExclusiveSectionIds)) {
        return ReportErrorCode::LevelConflict;
    }
    return std::nullopt;
}

std::vector<std::string> domainSectionsInScope(ReportLevel level)
{
    std::vector<std::string> out;
    out.reserve(kDomainSectionIds.size());
    for (const std::string_view id : kDomainSectionIds) {
        // C 级范围＝B 全部＋C 追加（§5.2）→ 全部八个域章节都在范围内；
        // B 级范围＝词表行 1~8 → 只取其中行 3~6 的四个 B 级域章节
        // （C 专属域章节行 9~12 不在 B 级范围——LevelConflict 面）。
        if (level == ReportLevel::C || contains(id, kBLevelSectionIds)) {
            out.emplace_back(id);
        }
    }
    return out;
}

MissingItemView unregisteredSectionMissingItem(std::string_view sectionId)
{
    MissingItemView item;
    // 缺项 itemId＝章节 token 本身（缺的是"该章节的可用提供方"——itemId
    // 定位缺哪个章节，渲染层据此呈现"章节不可用"行）。
    item.itemId = std::string(sectionId);
    // 缺项文案＝§5.1 词表规则的持久化契约文本（逐字冻结——呈现文案变更
    // 属契约变更，走单元卡变更记录；TRJ-08 同精神：缺什么写什么，不伪造
    // "空章节"）。
    item.reason = "章节不可用（提供方未注册）";
    return item;
}

// =====================================================================
// §5 默认选择规则（acceptance 3——§16 验收要点的契约承载）
// =====================================================================

bool defaultSelectionForStatus(SectionStatus status) noexcept
{
    switch (status) {
    case SectionStatus::NoFormalResult:
        // 缺正式结果→默认不选（§5.3 呈现列＋§16 验收要点"缺正式结果的
        // 章节默认不选并显示缺项"——缺项由 missingItems 承担显示义务）。
        return false;
    case SectionStatus::Populated:
        // 有正式内容→完整章节（选中）。
        return true;
    case SectionStatus::DataInsufficient:
        // 有结果但证据不足→选中＋全量缺失清单＋限定语（§5.3——不因证据
        // 不足隐藏章节：缺失项全量清单是呈现义务，§8.1 表 2④）。
        return true;
    case SectionStatus::NotApplicable:
        // 显式不适用→选中＋"—"＋原因（§5.3/C2 口径：条件不满足不计缺失，
        // 但"—"本身是正式呈现——不选中反而丢失"为何无内容"的说明）。
        return true;
    }
    // 全枚举 switch 无 default（新增枚举值未登记表项时编译器全枚举告警
    // 暴露遗漏——Errors.cpp token 表同款纪律）；到达此处仅可能来自未登记
    // 值（编译期契约违约）。本函数 noexcept——防御性兜底以显式 terminate
    // fail-fast（noexcept 函数内 throw 本身即 terminate，且触发 MSVC C4297
    // 告警；显式调用语义同型、无告警噪音）。
    std::terminate();
}

// =====================================================================
// ScopeInsufficient 判定原语与诊断挂接（acceptance 4——P-RPT-5）
// =====================================================================

SectionScopeVerdict checkSectionScope(ReportLevel level,
                                      const std::vector<SectionStatusEntry>& sectionStates)
{
    SectionScopeVerdict verdict;
    if (level == ReportLevel::B) {
        // B 级请求：无 C 专属章节可言，§5.4 行 2 只约束 C 级——恒不判缺
        // （B 级的范围拒绝面是 LevelConflict，不是 ScopeInsufficient）。
        return verdict;
    }

    // C 级请求：收集输入投影中出现的 C 专属章节及其"是否有正式结果"。
    // 词表外 token 在此被忽略（其合法性归注册边界/字段校验——本原语对
    // 投影数据做纯判定，不重复上游裁决；文档化于头文件 @param）。
    std::size_t cSectionsSeen = 0;
    for (const SectionStatusEntry& entry : sectionStates) {
        if (!contains(entry.sectionId, kCExclusiveSectionIds)) {
            continue;   // 非词表 token 或 B 章节——不在本判定范围
        }
        ++cSectionsSeen;
        if (entry.status == SectionStatus::NoFormalResult) {
            // 缺正式结果——计入降级建议明细（序＝遍历序；调用方传入的
            // 投影按 §5.1 行号序产出时，明细即行号序——确定性）。
            verdict.noFormalResultCSections.push_back(entry.sectionId);
        }
        // 其余三态（Populated/DataInsufficient/NotApplicable）＝该章节有
        // 实质内容或显式说明——不触发整体拒绝（§5.4 行 1/3/4）。
    }

    // 判定成立的两条路径（同一处置——拒绝＋降级建议）：
    //   ①输入含 C 专属章节且全部 NoFormalResult（seen==missing 非空）；
    //   ②输入不含任何 C 专属章节（seen==0——§5.2"C 级零 C 章节选中＝
    //     ScopeInsufficient"同判：没有实质 C 内容的 C 级报告＝虚级）。
    // 两路径统一为"C 级没有任何有实质内容的 C 章节"，P-RPT-5：宁可拒绝
    // 不可虚级。
    if (cSectionsSeen == 0
        || verdict.noFormalResultCSections.size() == cSectionsSeen) {
        verdict.scopeInsufficient = true;
    }
    return verdict;
}

core::DiagnosticRecord makeScopeInsufficientDiagnostic(
    const std::vector<std::string>& noFormalResultCSections)
{
    // cause 明细：逐项点名缺正式结果的 C 章节（ERR-01 不吞细节——诊断
    // 必须可定位到"缺哪些章节"；空集＝零 C 章节有实质内容的空集情形，
    // 文案改述为全量缺席）。
    std::string detail;
    if (noFormalResultCSections.empty()) {
        detail = "C 级请求无任何有正式结果的 C 专属章节（零 C 章节选中——§5.2）";
    } else {
        detail = "C 级请求的全部 C 专属章节缺正式结果（共 "
                 + std::to_string(noFormalResultCSections.size()) + " 项：";
        for (std::size_t i = 0; i < noFormalResultCSections.size(); ++i) {
            if (i > 0) {
                detail += "、";
            }
            detail += noFormalResultCSections[i];
        }
        detail += "）";
    }

    // 稳定码＝diagcodes::kScopeInsufficient（"RPT-SCOPE-INSUFFICIENT"——
    // diagnostics 收编表登记值；码值权威归 StableCodeRegistry，本处为
    // 引用登记——Errors.hpp 码清单声明）。诊断构造经 core make() 的 C-3
    // 校验（码句法＋必填串非空）——常量文本不可能违约，违约即实现缺陷
    // （fail-fast 兜底）。
    return core::DiagnosticRecord::make(
        std::string(diagcodes::kScopeInsufficient),
        std::nullopt,   // subject：范围级诊断无对象锚（拒绝的是"报告范围"，
                        // 不是某个项目对象——伪造 subject 反而误导定位）
        std::nullopt,   // localName：无
        std::nullopt,   // runtimeName：无
        "报告范围检查（RPT-01-C/§5.4）",
        detail + "——C 级报告无可引用的已完成结果，无实质内容"
                 "（宁可拒绝不可虚级——P-RPT-5 保守处置）",
        // 保守处置的语义落点（P-RPT-5）：降级必须显式——建议文本固定
        // 含"显式确认"与"B 级重建"两个契约词（测试钉死，防静默降级）。
        "建议生成 B 级报告：须用户显式确认后按 B 级重建（非静默降级——§5.4）");
}

}  // namespace sdurws::ird::reporting
