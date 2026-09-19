/**
 * @file   SectionsTest.cpp
 * @brief  reporting 章节词表与级别契约单测——词表冻结/级别归属锁定/
 *         LevelConflict 原语/未注册域章节缺项/默认选择规则/ScopeInsufficient
 *         原语与诊断挂接（RPT-T04 acceptance 1~4 的单元内具名自证）。
 *
 * 设计依据：
 *   - units/reporting.md §5 全节（§5.1 词表 14 项与词表规则、§5.2 范围图、
 *     §5.3 四态呈现、§5.4 降级与拒绝——P-RPT-5 保守处置）、§4.6（组合矩阵）、
 *     §9.2（注册前置行"minimumLevel 与词表一致"）、§10.1 RP-SCOPE-3/
 *     RP-SCOPE-4 行（验证方式——具名替身用例体随 RPT-T11 收口，本文件交付
 *     其判定原语面的局部夹具用例）
 *   - 需求 RPT-01-B/RPT-01-C（两级章节范围）、NFR-COR-02（同词表同判定）、
 *     §16 验收要点（缺正式结果的章节默认不选并显示缺项——经 §5.3 承接）
 *   - 任务契约 tasks/foundation/RPT-T04.json acceptance 1~4
 *
 * 用例命名约定：`<主题>_<锚点>` 尾缀标注需求/acceptance 追溯字段
 * （AGENTS.md §2.7——每用例注释首行声明"验证哪条验收"）。
 * 替身边界声明（RP-STATE-4 同源）：本文件夹具数据仅验证词表/级别/判定
 * 原语契约，不构成任何业务算法正确性证明。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;

// =====================================================================
// 局部夹具：§5.1 词表的测试侧权威副本（逐字钉死——词表常量若被改动，
// 本副本即失配报警；"token 只增不改名"的回归面）
// =====================================================================

/// §5.1 表 token 列原文（14 项，表序）——acceptance 1 的逐项期望值。
constexpr std::array<std::string_view, 14> kExpectedVocabulary = {
    "project-scheme",                 "input-summary",
    "model",                          "requirements",
    "kinematics-collision",           "optimization-candidates",
    "diagnostics",                    "external-validation-boundary",
    "trajectory-cycle",               "dynamics-envelope",
    "drivetrain-operating-points",    "selection-bom",
    "review-signoff",                 "variant-diff",
};

/// 框架章节期望集（§5.1 表"内容所有者"列为框架的六项：行 1/2/7/8/13/14）。
constexpr std::array<std::string_view, 6> kExpectedFramework = {
    "project-scheme", "input-summary", "diagnostics",
    "external-validation-boundary", "review-signoff", "variant-diff",
};

/// 域章节期望集（§5.1 域八项：行 3/4/5/6/9/10/11/12）。
constexpr std::array<std::string_view, 8> kExpectedDomain = {
    "model", "requirements", "kinematics-collision", "optimization-candidates",
    "trajectory-cycle", "dynamics-envelope", "drivetrain-operating-points",
    "selection-bom",
};

/// C 专属章节期望集（§5.1 行 9~14）。
constexpr std::array<std::string_view, 6> kExpectedCExclusive = {
    "trajectory-cycle", "dynamics-envelope", "drivetrain-operating-points",
    "selection-bom", "review-signoff", "variant-diff",
};

/// 把 string_view 数组转成 vector<string>（与产品接口返回形态对齐的比较器）。
template <std::size_t N>
std::vector<std::string> toStringVector(const std::array<std::string_view, N>& tokens)
{
    std::vector<std::string> out;
    out.reserve(N);
    for (const std::string_view t : tokens) {
        out.emplace_back(t);
    }
    return out;
}

// =====================================================================
// acceptance 1：章节词表冻结落地（14 项 token/级别归属/版本 token）
// =====================================================================

/**
 * 验证 acceptance 1：SectionId 词表 14 项与 §5.1 逐项一致——token 字面、
 * 词表序（＝ReviewReportSection::order 冻结值）与"只增不改名"的回归钉死。
 */
TEST(ReportingSections, Vocabulary14TokensFrozenInTableOrder_RPT04_ACC1)
{
    ASSERT_EQ(kAllSectionIds.size(), std::size_t{14})
        << "词表必须恰为 §5.1 的 14 项（8 B＋6 C 专属）";
    for (std::size_t i = 0; i < kExpectedVocabulary.size(); ++i) {
        EXPECT_EQ(kAllSectionIds[i], kExpectedVocabulary[i])
            << "词表第 " << i + 1 << " 项与 §5.1 表序 token 不一致（token 只增不改名）";
        // 词表行号＝表行号（1 起）——order 的冻结依据（§4.3.4）。
        const auto order = trySectionOrder(kExpectedVocabulary[i]);
        ASSERT_TRUE(order.has_value());
        EXPECT_EQ(*order, static_cast<std::uint16_t>(i + 1));
    }
}

/**
 * 验证 acceptance 1：框架 6 个/域 8 个的归属逐项锁定，且 B 章节 8 项＝
 * 词表行 1~8、C 专属 6 项＝行 9~14（级别归属与框架/域划分两个正交维度
 * 全部钉死）；两组分类互斥完备（无第三个所有者类别）。
 */
TEST(ReportingSections, LevelAndOwnershipAttributionFrozen_RPT04_ACC1)
{
    // 框架/域归属逐项比对（集合相等＝无遗漏无多出）。
    EXPECT_EQ(toStringVector(kFrameworkSectionIds), toStringVector(kExpectedFramework));
    EXPECT_EQ(toStringVector(kDomainSectionIds), toStringVector(kExpectedDomain));

    // 级别归属逐项：行 1~8＝B，行 9~14＝C（§5.1 级别列）。
    for (std::size_t i = 0; i < kExpectedVocabulary.size(); ++i) {
        const auto level = trySectionMinimumLevel(kExpectedVocabulary[i]);
        ASSERT_TRUE(level.has_value())
            << "词表内 token 必有级别：" << kExpectedVocabulary[i];
        const ReportLevel expected = (i < 8) ? ReportLevel::B : ReportLevel::C;
        EXPECT_EQ(*level, expected) << "级别归属违约：" << kExpectedVocabulary[i];
    }

    // B 章节全量数组与词表行 1~8 一致。
    ASSERT_EQ(kBLevelSectionIds.size(), std::size_t{8});
    for (std::size_t i = 0; i < kBLevelSectionIds.size(); ++i) {
        EXPECT_EQ(kBLevelSectionIds[i], kExpectedVocabulary[i]);
    }

    // 分类互斥完备：框架∩域＝∅；框架∪域＝全量（所有者二分类封闭）。
    for (const std::string_view f : kFrameworkSectionIds) {
        EXPECT_FALSE(isDomainSectionId(f)) << "框架/域交集非空：" << f;
    }
    for (const std::string_view d : kDomainSectionIds) {
        EXPECT_TRUE(isDomainSectionId(d));
        EXPECT_FALSE(isFrameworkSectionId(d));
    }
    for (const std::string_view a : kAllSectionIds) {
        EXPECT_TRUE(isFrameworkSectionId(a) || isDomainSectionId(a))
            << "存在未归类 token：" << a;
    }
}

/**
 * 验证 acceptance 1：ird-report-section-model/1 版本 token 就位（§5.1
 * 标题原文）＋C 专属词表注入面（ReportLevelRule 权威源＝frozenReport
 * LevelRule，与 kCExclusiveSectionIds 一致——ReportModel 侧零复制的单点
 * 权威纪律）。
 */
TEST(ReportingSections, SectionModelVersionTokenAndLevelRuleInjection_RPT04_ACC1)
{
    // 版本 token 恰为 §5.1 标题原文（词表升级＝该 token 升版——§5.1 词表
    // 规则；常量定义在 ReportModel.hpp，本头消费——单一权威）。
    EXPECT_EQ(kSectionModelVersion, "ird-report-section-model/1");

    // C 专属词表注入值与常量数组逐项一致（ReportLevelRule.cExclusiveSectionIds
    // 的权威源＝kCExclusiveSectionIds——acceptance 2 的 LevelConflict 判定与
    // ReviewReport::make 字段校验共用同一基准）。
    const ReportLevelRule rule = frozenReportLevelRule();
    EXPECT_EQ(rule.cExclusiveSectionIds, toStringVector(kExpectedCExclusive));
    EXPECT_EQ(toStringVector(kCExclusiveSectionIds), toStringVector(kExpectedCExclusive));
}

// =====================================================================
// acceptance 2：级别校验与注册边界（LevelConflict 原语/缺项表达）
// =====================================================================

/**
 * 验证 acceptance 2：B 级∧C 专属章节＝LevelConflict 判定原语（§4.6 行 2
 * "不能伪造 C 级章节"）——六个 C token 逐项命中；B∧B、C∧B、C∧C 均为
 * 合法组合（nullopt）。
 */
TEST(ReportingSections, LevelConflictPrimitive_RPT04_ACC2)
{
    // B 级请求每个 C 专属章节→LevelConflict（伪造 C 级章节的判定面）。
    for (const std::string_view cId : kExpectedCExclusive) {
        const auto conflict = sectionLevelConflict(ReportLevel::B, cId);
        ASSERT_TRUE(conflict.has_value()) << "B 级携 C 章节未判冲突：" << cId;
        EXPECT_EQ(*conflict, ReportErrorCode::LevelConflict);
    }
    // 合法组合全扫描：B∧B（8 项）与 C∧B、C∧C（14 项）均无冲突。
    for (const std::string_view bId : kBLevelSectionIds) {
        EXPECT_FALSE(sectionLevelConflict(ReportLevel::B, bId).has_value())
            << "B∧B 误判冲突：" << bId;
        EXPECT_FALSE(sectionLevelConflict(ReportLevel::C, bId).has_value())
            << "C∧B 误判冲突（C＝B 全部＋追加）：" << bId;
    }
    for (const std::string_view cId : kExpectedCExclusive) {
        EXPECT_FALSE(sectionLevelConflict(ReportLevel::C, cId).has_value())
            << "C∧C 误判冲突：" << cId;
    }
}

/**
 * 验证 acceptance 2：未注册域章节呈现为"章节不可用（提供方未注册）"缺项
 * 而非空壳（§5.1 词表规则/TRJ-08 同精神）——缺项文案逐字冻结＋级别应有
 * 域章节集（B 四项/C 八项）与 §5.2 范围图一致。
 */
TEST(ReportingSections, UnregisteredDomainSectionMissingItem_RPT04_ACC2)
{
    // 缺项表达：itemId＝章节 token，reason＝词表规则契约文案（逐字）。
    const MissingItemView item = unregisteredSectionMissingItem("trajectory-cycle");
    EXPECT_EQ(item.itemId, "trajectory-cycle");
    EXPECT_EQ(item.reason, "章节不可用（提供方未注册）");
    // 缺项条目本身非空壳（itemId/reason 均非空——ERR-01 不伪造）。
    EXPECT_FALSE(item.itemId.empty());
    EXPECT_FALSE(item.reason.empty());

    // 级别应有域章节集：B→词表行 3~6 四项；C→域八项（C＝B 全部＋追加，
    // §5.2——B 的域章节在 C 级仍在范围内）。
    const std::vector<std::string> bDomain = domainSectionsInScope(ReportLevel::B);
    const std::vector<std::string> expectedBDomain = {
        "model", "requirements", "kinematics-collision", "optimization-candidates",
    };
    EXPECT_EQ(bDomain, expectedBDomain);

    const std::vector<std::string> cDomain = domainSectionsInScope(ReportLevel::C);
    EXPECT_EQ(cDomain, toStringVector(kExpectedDomain));
}

// =====================================================================
// acceptance 3：缺正式结果表达契约（四态 token/默认选择规则/五词不合并）
// =====================================================================

/**
 * 验证 acceptance 3：SectionStatus 四态 token 与 §5.3 词表逐项一致
 * （populated/no-formal-result/data-insufficient/not-applicable）＋
 * try 解析往返（恰接受四规范字面）。
 */
TEST(ReportingSections, SectionStatusFourStatesTokens_RPT04_ACC3)
{
    // 四态 token 逐项（§5.3 token 列原文）。
    EXPECT_EQ(token(SectionStatus::Populated), "populated");
    EXPECT_EQ(token(SectionStatus::NoFormalResult), "no-formal-result");
    EXPECT_EQ(token(SectionStatus::DataInsufficient), "data-insufficient");
    EXPECT_EQ(token(SectionStatus::NotApplicable), "not-applicable");

    // 往返：四规范字面可解析且恰为对应态；非规范字面拒绝（§1.4 try* 约定
    // ——RPT-T03 已建解析面，此处消费自证四态逐项可达）。
    for (const SectionStatus s :
         {SectionStatus::Populated, SectionStatus::NoFormalResult,
          SectionStatus::DataInsufficient, SectionStatus::NotApplicable}) {
        const auto parsed = trySectionStatusFromToken(token(s));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, s);
    }
    EXPECT_FALSE(trySectionStatusFromToken("no_formal_result").has_value());
    EXPECT_FALSE(trySectionStatusFromToken("").has_value());
}

/**
 * 验证 acceptance 3：默认选择规则——NoFormalResult 默认不选（§16 验收
 * 要点"缺正式结果的章节默认不选并显示缺项"的契约承载），其余三态默认
 * 选中（§5.3 呈现列）。
 */
TEST(ReportingSections, DefaultSelectionRule_RPT04_ACC3)
{
    EXPECT_FALSE(defaultSelectionForStatus(SectionStatus::NoFormalResult))
        << "缺正式结果必须默认不选（§16 验收要点）";
    EXPECT_TRUE(defaultSelectionForStatus(SectionStatus::Populated));
    EXPECT_TRUE(defaultSelectionForStatus(SectionStatus::DataInsufficient))
        << "数据不足仍是选中＋全量缺失清单（§5.3——不隐藏章节）";
    EXPECT_TRUE(defaultSelectionForStatus(SectionStatus::NotApplicable))
        << "不适用仍选中＋'—'＋原因（§5.3/C2——不选会丢失说明）";
}

/**
 * 验证 acceptance 3（P-EV-8 消费）：证据五词各有其位不合并——证据状态
 * 词（evidence::EvidenceItemStatus 五值）在 reporting 侧以**原类型**承载
 * （EvidenceBinding.status 类型＝evidence 枚举本身，零本地重定义、零值域
 * 复制），与章节状态四态（SectionStatus）、工程判定轴（core::
 * EngineeringStatus）类型隔离——"缺失/无效/未验证/不适用/数据不足"五词
 * 分居 evidence 词表与 core 判定轴，reporting 逐一消费呈现、互不映射
 * （§5.3 区分规则/O-13）。
 */
TEST(ReportingSections, FiveEvidenceWordsKeepSeparatePlaces_RPT04_ACC3)
{
    // 五词承载：EvidenceBinding.status 的类型即 evidence::EvidenceItemStatus
    // （类型级自证——reporting 不复制/不重定义/不换轨，O-13 消费口径）。
    static_assert(std::is_same_v<decltype(EvidenceBinding::status),
                                 sdurws::ird::evidence::EvidenceItemStatus>,
                  "证据状态五词必须以 evidence 原词表类型承载（P-EV-8 消费）");

    // evidence 词域钉死：恰五值（缺失/无效/未验证/满足/不适用——evidence
    // §6.2 词表），五词全列＝值域封闭的常驻自证（evidence 侧新增/删减值
    // 时此处编译失配报警，reporting 消费面随 diff 增量同步——P-RPT-9）。
    using EIS = sdurws::ird::evidence::EvidenceItemStatus;
    const std::array<EIS, 5> kEvidenceItemWords = {
        EIS::Satisfied, EIS::Missing, EIS::Invalid, EIS::Unverified, EIS::NotApplicable,
    };
    EXPECT_EQ(kEvidenceItemWords.size(), std::size_t{5});

    // 词位隔离：同形英文词在不同词表各归其位、不互相推导——章节态
    // NotApplicable（呈现语义：整章显式不适用）与证据态 NotApplicable
    // （证据语义：条件不满足不计缺失）是**两个枚举域**的同名词面，类型
    // 系统隔离（无互转）；"数据不足"同理分居章节态与工程判定轴
    // （core::EngineeringStatus::DataInsufficient——CON-02 三轴正交）。
    static_assert(!std::is_same_v<SectionStatus, EIS>,
                  "章节状态与证据状态必须是不同词表类型（五词不合并）");
    static_assert(!std::is_same_v<SectionStatus, co::EngineeringStatus>,
                  "章节状态与工程判定轴必须是不同词表类型（CON-02 正交）");
    EXPECT_EQ(token(SectionStatus::NotApplicable), "not-applicable");
    EXPECT_EQ(token(SectionStatus::DataInsufficient), "data-insufficient");
}

// =====================================================================
// acceptance 4：C 级降级/拒绝规则原语（ScopeInsufficient＋诊断挂接）
// =====================================================================

/**
 * 验证 acceptance 4（RP-SCOPE-3 判定面②）：C 级全部 C 专属章节缺正式结果
 * ＝ScopeInsufficient 判定成立，缺项明细逐项点名（§5.4 行 2）。
 */
TEST(ReportingSections, ScopeInsufficientAllCSectionsMissing_RPT04_ACC4)
{
    // 投影：全部 14 章节（B 章节 Populated——B 侧内容不影响判定；C 六章
    // 全部 NoFormalResult——§5.4 行 2 情形）。
    std::vector<SectionStatusEntry> states;
    for (const std::string_view bId : kBLevelSectionIds) {
        states.push_back({std::string(bId), SectionStatus::Populated});
    }
    for (const std::string_view cId : kExpectedCExclusive) {
        states.push_back({std::string(cId), SectionStatus::NoFormalResult});
    }

    const SectionScopeVerdict v = checkSectionScope(ReportLevel::C, states);
    EXPECT_TRUE(v.scopeInsufficient) << "全部 C 章节缺正式结果必须判 ScopeInsufficient";
    EXPECT_EQ(v.noFormalResultCSections, toStringVector(kExpectedCExclusive))
        << "缺项明细应逐项点名六个 C 章节（§5.1 行号序）";
}

/**
 * 验证 acceptance 4（RP-SCOPE-3 判定面①的反面）：部分 C 章节缺正式结果
 * ＝**不**整体拒绝（§5.4 行 1——按缺项/默认不选呈现，报告可生成）。
 */
TEST(ReportingSections, PartialMissingCSectionsNotScopeInsufficient_RPT04_ACC4)
{
    std::vector<SectionStatusEntry> states;
    for (const std::string_view cId : kExpectedCExclusive) {
        // 仅轨迹章有正式结果，其余五章缺——部分缺失情形。
        states.push_back({std::string(cId),
                          cId == "trajectory-cycle" ? SectionStatus::Populated
                                                    : SectionStatus::NoFormalResult});
    }
    const SectionScopeVerdict v = checkSectionScope(ReportLevel::C, states);
    EXPECT_FALSE(v.scopeInsufficient) << "部分缺失不触发整体拒绝（§5.4 行 1）";
    EXPECT_EQ(v.noFormalResultCSections.size(), std::size_t{5});
}

/**
 * 验证 acceptance 4：B 级请求恒不判 ScopeInsufficient（§5.4 行 2 只约束
 * C 级）＋C 级零 C 章节有实质内容（空集情形）＝ScopeInsufficient（§5.2
 * "C 级零 C 章节选中"同判——宁可拒绝不可虚级）。
 */
TEST(ReportingSections, ScopeInsufficientBoundaries_RPT04_ACC4)
{
    // B 级＋全部 NoFormalResult→不判（B 级无 C 章节可言）。
    std::vector<SectionStatusEntry> allMissing;
    for (const std::string_view id : kAllSectionIds) {
        allMissing.push_back({std::string(id), SectionStatus::NoFormalResult});
    }
    EXPECT_FALSE(checkSectionScope(ReportLevel::B, allMissing).scopeInsufficient);

    // C 级＋空投影→判缺（零 C 章节选中同判——§5.2）。
    const SectionScopeVerdict empty = checkSectionScope(ReportLevel::C, {});
    EXPECT_TRUE(empty.scopeInsufficient);
    EXPECT_TRUE(empty.noFormalResultCSections.empty());

    // C 级＋仅有 B 章节投影（无任何 C 章节条目）→同上。
    std::vector<SectionStatusEntry> onlyB;
    for (const std::string_view bId : kBLevelSectionIds) {
        onlyB.push_back({std::string(bId), SectionStatus::Populated});
    }
    EXPECT_TRUE(checkSectionScope(ReportLevel::C, onlyB).scopeInsufficient);

    // 非缺正式结果的三态不触发：C 章节为 NotApplicable（显式不适用——
    // 有说明的"—"不是"缺"，§5.4 行 4）→不判缺。
    std::vector<SectionStatusEntry> allNA;
    for (const std::string_view cId : kExpectedCExclusive) {
        allNA.push_back({std::string(cId), SectionStatus::NotApplicable});
    }
    EXPECT_FALSE(checkSectionScope(ReportLevel::C, allNA).scopeInsufficient);

    // 词表外 token 在投影中被忽略（拒绝面在注册边界/字段校验——本原语
    // 纯判定；"model-x" 不算 C 章节，等价于空集情形→仍判缺）。
    std::vector<SectionStatusEntry> withUnknown = {
        {"model-x", SectionStatus::Populated},
    };
    EXPECT_TRUE(checkSectionScope(ReportLevel::C, withUnknown).scopeInsufficient);
}

/**
 * 验证 acceptance 4：RPT-SCOPE-INSUFFICIENT 诊断挂接——稳定码、范围级
 * subject 缺席、cause 逐项点名、recommendedAction 固定含"显式确认＋B 级
 * 重建"（P-RPT-5 保守处置：非静默降级的语义落点）。
 */
TEST(ReportingSections, ScopeInsufficientDiagnosticHookup_RPT04_ACC4)
{
    const std::vector<std::string> missing = toStringVector(kExpectedCExclusive);
    const co::DiagnosticRecord rec = makeScopeInsufficientDiagnostic(missing);

    // 稳定码＝收编表登记值（Errors.hpp diagcodes 引用清单——码值权威在
    // diagnostics StableCodeRegistry，contract_test 逐码自证）。
    EXPECT_EQ(rec.code, std::string(diagcodes::kScopeInsufficient));
    EXPECT_EQ(rec.code, "RPT-SCOPE-INSUFFICIENT");

    // 范围级诊断：无对象锚（拒绝的是"报告范围"，不是某个项目对象——
    // 伪造 subject 会误导定位）。
    EXPECT_FALSE(rec.subject.has_value());

    // cause 逐项点名六个缺正式结果章节（ERR-01 不吞细节）。
    for (const std::string& id : missing) {
        EXPECT_NE(rec.cause.find(id), std::string::npos)
            << "cause 未点名章节：" << id;
    }
    EXPECT_NE(rec.cause.find("宁可拒绝不可虚级"), std::string::npos)
        << "cause 必须承载 P-RPT-5 保守处置语义";

    // recommendedAction＝"用户显式确认后按 B 级重建"（§5.4 行 2 原文——
    // 两个契约词钉死，防静默降级）。
    EXPECT_NE(rec.recommendedAction.find("显式确认"), std::string::npos);
    EXPECT_NE(rec.recommendedAction.find("B 级"), std::string::npos);
    EXPECT_NE(rec.recommendedAction.find("非静默降级"), std::string::npos);

    // 空集情形（零 C 章节有实质内容，§5.2）：诊断仍成立且文案改述为
    // 全量缺席（不产生空括号噪声）。
    const co::DiagnosticRecord emptyCase = makeScopeInsufficientDiagnostic({});
    EXPECT_EQ(emptyCase.code, std::string(diagcodes::kScopeInsufficient));
    EXPECT_NE(emptyCase.cause.find("零 C 章节选中"), std::string::npos);
}

// =====================================================================
// 词表边界（词表外 token 的全接口拒绝面）
// =====================================================================

/**
 * 验证 acceptance 1/2 的词表封闭性：词表外 token（空串/大小写变体/未知/
 * 带空白）在全部查找接口一律否定——词表是封闭集（§5.1"token 只增不改名"，
 * 非规范输入不归一化）。
 */
TEST(ReportingSections, VocabularyClosedToUnknownTokens_RPT04_ACC1)
{
    const std::string_view unknown[] = {
        "",            // 空串
        "Model",       // 大小写变体（词表字面全小写）
        "model-x",     // 未知 token（词表外相似词）
        "model ",      // 尾随空白
        " trajectory-cycle",  // 前导空白
        "REVIEW-SIGNOFF",     // 全大写变体
    };
    for (const std::string_view t : unknown) {
        EXPECT_FALSE(isValidSectionId(t)) << "未知 token 被接受：" << t;
        EXPECT_FALSE(trySectionOrder(t).has_value());
        EXPECT_FALSE(trySectionMinimumLevel(t).has_value());
        EXPECT_FALSE(isFrameworkSectionId(t));
        EXPECT_FALSE(isDomainSectionId(t));
    }
}

}  // namespace
