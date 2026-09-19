/**
 * @file   SectionProviderTest.cpp
 * @brief  reporting 章节提供方契约单测——注册边界五类拒绝/注册表查找与
 *         稳定排序/运行期并发只读/纯投影后置（同请求同输出、值拷贝请求）/
 *         SectionContent 结构自检原语与 entryKey 词形（RPT-T04 acceptance
 *         2/5 的单元内具名自证）。
 *
 * 设计依据：
 *   - units/reporting.md §9.2 全节（结构逐字段、IReportSectionProvider 前置/
 *     后置/错误行、SectionRegistry 注册边界与维度表：注册期单线程/运行期
 *     find 并发只读/registeredSections 字典序稳定）、§5.1 词表规则（框架
 *     章节不注册不可覆盖、未注册域章节缺项非空壳）、§5.3（四态 presence
 *     纪律——结构自检语义源）、§10.1 RP-SCOPE-1/2/4 行（验证方式——具名
 *     替身用例体随 RPT-T11 契约套件收口，本文件交付局部夹具用例）
 *   - 需求 RPT-01-B/RPT-01-C、NFR-COR-02（同请求同输出/稳定排序）
 *   - 任务契约 tasks/foundation/RPT-T04.json acceptance 2/5
 *
 * 夹具说明（替身边界声明——RP-STATE-4 同源）：ScriptedSectionProvider 为
 * §5.1 词表规则"阶段 A 前不存在——替身仅供测试"的局部夹具替身，仅验证
 * 提供方契约（注册/查找/纯投影/结构），不构成任何业务章节内容的正确性
 * 证明；业务替身与 RP-* 具名用例体随 RPT-T11 落位。
 *
 * 用例命名约定：`<主题>_<锚点>` 尾缀标注需求/acceptance 追溯字段。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;

// =====================================================================
// 局部夹具：脚本化章节提供方（§5.1 词表规则"替身仅供测试"的承载）
// =====================================================================

/**
 * @brief 脚本化提供方——注册边界/纯投影/并发契约的观测夹具。
 *
 * 记录 project() 调用次数与请求副本：projectCalls 观测"纯投影零副作用"
 * （调用本身不改输出），lastRequest 观测"值拷贝请求"（调用方事后修改
 * 请求不得影响已记录副本——提供方不得持有请求的结构性体现）。
 */
class ScriptedSectionProvider final : public IReportSectionProvider {
public:
    ScriptedSectionProvider(std::string id, ReportLevel minLevel, SectionContent scripted)
        : m_id(std::move(id)), m_minLevel(minLevel), m_scripted(std::move(scripted))
    {
    }

    std::string sectionId() const override { return m_id; }
    ReportLevel minimumLevel() const override { return m_minLevel; }
    std::vector<std::string> requiredEvaluationKeys() const override
    {
        // 夹具统一声明消费 "kin.batch-ik"（§9.2 调用示例键——仅作请求
        // 过滤声明的形态占位，过滤本身归构建器 RPT-T05）。
        return {"kin.batch-ik"};
    }

    SectionContent project(const SectionRequest& request) override
    {
        ++m_projectCalls;              // 调用计数（副作用仅观测面——不影响输出）
        m_lastRequest = request;       // 请求值拷贝（结构性留存——非引用/指针）
        return m_scripted;             // 固定脚本输出（同请求同输出的夹具形态）
    }

    /// 观测面（测试线程内单线程读取——注册/调用与断言不同线程并发访问
    /// 该夹具观测字段的场景在并发用例中不存在：并发用例只用 find()）。
    long projectCalls() const { return m_projectCalls; }
    const std::optional<SectionRequest>& lastRequest() const { return m_lastRequest; }

private:
    std::string m_id;
    ReportLevel m_minLevel;
    SectionContent m_scripted;
    long m_projectCalls = 0;
    std::optional<SectionRequest> m_lastRequest;
};

/// 合法域提供方工厂（B 级"model"章节；脚本内容为最小合法 Populated 形态）。
std::unique_ptr<ScriptedSectionProvider> makeModelProvider(SectionContent scripted = {})
{
    if (scripted.status == SectionStatus::NoFormalResult && scripted.entries.empty()
        && scripted.missingItems.empty()) {
        // 默认脚本：最小合法 Populated 内容（一条最小条目——结构自检口径）。
        SectionEntryView entry;
        entry.entryKey = "axis-count";
        scripted.status = SectionStatus::Populated;
        scripted.entries.push_back(entry);
        scripted.renderHint = RenderHint::Table;
    }
    return std::make_unique<ScriptedSectionProvider>("model", ReportLevel::B,
                                                     std::move(scripted));
}

/// 非零 16 字节 Id128 夹具（core 身份类型族——铺位递增字节，同 seed 同值；
/// 夹具数据仅验证结构契约，RP-STATE-4 边界声明）。
template <typename T> T fixtureId(std::uint8_t seed)
{
    T id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

/// 构造一个结构合法的最小投影请求（前置"results 非空"——§9.2 前置行；
/// envelope 为默认构造值：本夹具只验证请求-内容结构契约，envelope 载荷
/// 语义归 evidence 单元测试，RP-STATE-4 边界声明）。
SectionRequest makeRequest(ReportLevel level = ReportLevel::B)
{
    SectionRequest r;
    r.level = level;
    r.revision = fixtureId<co::RevisionId>(0x10);
    r.snapshotId = fixtureId<co::ContentIdentity>(0x20);
    r.results.emplace_back();   // 非空结果子集（前置行——构建器已过滤的形态）
    // 显示单位只能经 core 冻结注册表 find() 取得（UnitToken 无字符串构造
    // ——注册句柄纪律）；"mm" 为 core 单位表冻结项（Length 量纲）。
    const auto mm = co::UnitToken::find("mm");
    if (mm.has_value()) {
        r.units.displayUnits[co::QuantityKind::Length] = *mm;
    }
    return r;
}

// =====================================================================
// acceptance 2：注册边界（五类拒绝——全部 Usage fail-fast）
// =====================================================================

/**
 * 验证 acceptance 2：注册边界五类拒绝——空指针/词表外 token/框架章节/
 * minimumLevel 与词表级别不一致（含 B 级注册 C 专属章节）/重复 sectionId，
 * 一律 ReportError(Usage)（装配期调用方违约 fail-fast）；拒绝后注册表
 * 保持原状（无半注册状态）。
 */
TEST(ReportingSectionProvider, RegistrationBoundaryRejections_RPT04_ACC2)
{
    SectionRegistry registry;

    // ①空指针——无对象可投影。
    EXPECT_THROW(registry.registerProvider(nullptr), ReportError);

    // ②词表外 token——词表是封闭集（§5.1）。
    EXPECT_THROW(
        registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
            "model-x", ReportLevel::B, SectionContent{})),
        ReportError);

    // ③框架章节不可注册（§5.1"不注册、不可覆盖"——内建实现无覆盖通道）。
    EXPECT_THROW(
        registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
            "diagnostics", ReportLevel::B, SectionContent{})),
        ReportError);

    // ④a B 级 minimumLevel 注册 C 专属章节＝注册拒绝（acceptance 2 原文/
    // §9.2 前置行"minimumLevel 与词表一致"）。
    EXPECT_THROW(
        registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
            "trajectory-cycle", ReportLevel::B, SectionContent{})),
        ReportError);

    // ④b 反向（B 章节申报 C）同属级别与词表不一致——一并拒绝。
    EXPECT_THROW(
        registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
            "model", ReportLevel::C, SectionContent{})),
        ReportError);

    // ⑤首次合法注册后重复注册拒绝（§9.2"重复 sectionId 拒绝"——不覆盖）。
    registry.registerProvider(makeModelProvider());
    EXPECT_THROW(
        registry.registerProvider(
            std::make_unique<ScriptedSectionProvider>("model", ReportLevel::B,
                                                      SectionContent{})),
        ReportError);

    // 拒绝无副作用：注册表只含唯一合法注册（无半注册/覆盖残留）。
    const auto sections = registry.registeredSections();
    ASSERT_EQ(sections.size(), std::size_t{1});
    EXPECT_EQ(sections[0], "model");
    EXPECT_NE(registry.find("model"), nullptr);
}

/**
 * 验证 acceptance 2：B 级域章节正常注册路径（对照面——边界用例的合法
 * 基线；C 专属章节以 C 级 minimumLevel 注册同样放行）。
 */
TEST(ReportingSectionProvider, LegitimateRegistrationsAccepted_RPT04_ACC2)
{
    SectionRegistry registry;
    registry.registerProvider(makeModelProvider());
    registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        "trajectory-cycle", ReportLevel::C, SectionContent{}));

    const auto sections = registry.registeredSections();
    ASSERT_EQ(sections.size(), std::size_t{2});
    EXPECT_NE(registry.find("model"), nullptr);
    EXPECT_NE(registry.find("trajectory-cycle"), nullptr);
}

// =====================================================================
// acceptance 5：注册表查找/稳定排序/并发只读
// =====================================================================

/**
 * 验证 acceptance 5：registeredSections 字典序稳定（§9.2"稳定排序（字典
 * 序）"）——注册顺序不影响输出序，重复调用同输出（NFR-COR-02）；find
 * 对未注册域章节返回 nullptr（缺项而非空壳的判定依据——§5.1 词表规则）。
 */
TEST(ReportingSectionProvider, RegistrySortedLookupAndStableOrder_RPT04_ACC5)
{
    // 逆字典序注册——输出仍须字典序。
    SectionRegistry registry;
    registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        "selection-bom", ReportLevel::C, SectionContent{}));
    registry.registerProvider(makeModelProvider());
    registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        "dynamics-envelope", ReportLevel::C, SectionContent{}));

    const auto first = registry.registeredSections();
    const std::vector<std::string> expected = {
        "dynamics-envelope", "model", "selection-bom",
    };
    EXPECT_EQ(first, expected);

    const auto second = registry.registeredSections();
    EXPECT_EQ(second, first) << "重复调用必须同输出（稳定排序）";

    // 未注册域章节→nullptr（构建器据此产出"章节不可用"缺项——不空壳）。
    EXPECT_EQ(registry.find("trajectory-cycle"), nullptr);
    // 词表外/空 token→nullptr（找不到即缺——不抛）。
    EXPECT_EQ(registry.find("model-x"), nullptr);
    EXPECT_EQ(registry.find(""), nullptr);
}

/**
 * 验证 acceptance 5：注册完成后的运行期并发只读（§9.2 维度表"运行期
 * find 并发只读"）——多线程对全部词表 token 与未知 token 反复查找，结果
 * 与注册事实一致（命中指针稳定、未注册恒 nullptr）。
 */
TEST(ReportingSectionProvider, FindConcurrentReadOnlyAfterAssembly_RPT04_ACC5)
{
    SectionRegistry registry;
    registry.registerProvider(makeModelProvider());
    registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        "kinematics-collision", ReportLevel::B, SectionContent{}));

    IReportSectionProvider* const modelPtr = registry.find("model");
    IReportSectionProvider* const kinPtr = registry.find("kinematics-collision");
    ASSERT_NE(modelPtr, nullptr);
    ASSERT_NE(kinPtr, nullptr);

    // 4 线程 × 反复查找：命中指针逐次一致（同一对象），未注册恒 nullptr。
    std::vector<std::thread> threads;
    std::vector<bool> outcomes(4, true);   // 每线程结论（true＝全部查找一致）
    for (std::size_t t = 0; t < 4; ++t) {
        threads.emplace_back([&registry, &outcomes, t, modelPtr, kinPtr] {
            for (int i = 0; i < 200; ++i) {
                if (registry.find("model") != modelPtr
                    || registry.find("kinematics-collision") != kinPtr
                    || registry.find("trajectory-cycle") != nullptr
                    || registry.find("model-x") != nullptr) {
                    outcomes[t] = false;
                    return;
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const bool ok : outcomes) {
        EXPECT_TRUE(ok) << "并发只读查找出现不一致结果";
    }
}

// =====================================================================
// acceptance 5：project() 纯投影后置与请求值拷贝
// =====================================================================

/**
 * 验证 acceptance 5（§9.2 后置行）：同请求同输出——同一请求两次投影产出
 * 逐字段相等的内容（NFR-COR-02 在章节链的落点）；请求为值拷贝传递——
 * 调用方在投影后修改原请求，不影响提供方已收到的请求内容（"提供方不得
 * 持有"的结构性体现）。
 */
TEST(ReportingSectionProvider, ProjectPureFunctionAndRequestValueCopy_RPT04_ACC5)
{
    SectionRegistry registry;
    auto provider = makeModelProvider();
    ScriptedSectionProvider* raw = provider.get();
    registry.registerProvider(std::move(provider));

    SectionRequest request = makeRequest();
    const SectionRequest snapshot = request;   // 调用方侧期望请求

    const SectionContent first = raw->project(request);
    const SectionContent second = raw->project(request);
    EXPECT_EQ(first, second) << "同请求两次投影必须同输出（纯投影后置）";
    EXPECT_EQ(raw->projectCalls(), 2);

    // 投影后修改原请求——提供方记录的请求副本不受影响（值拷贝语义）。
    request.level = ReportLevel::C;
    request.results.clear();
    ASSERT_TRUE(raw->lastRequest().has_value());
    EXPECT_EQ(*raw->lastRequest(), snapshot)
        << "提供方收到的是请求副本——调用方事后修改不得回溯";
    // 夹具输出为固定脚本——两次调用间无状态漂移（有状态缓存对输出不可见
    // 的纯性义务由本断言钉住）。
}

// =====================================================================
// acceptance 5：SectionContent 结构自检原语与 entryKey 词形
// =====================================================================

/**
 * 验证 acceptance 5：结构自检原语按固定检查序拒绝四态 presence 纪律违约
 * （§5.3——Populated 空条目/缺章无缺项清单/不适用无原因/其余态携带原因）
 * ＋entryKey 结构违约（语法/重复——§9.2 错误行"entryKey 重复"）；合法
 * 内容逐态通过（nullopt）。
 */
TEST(ReportingSectionProvider, SectionContentStructuralChecks_RPT04_ACC5)
{
    // ①Populated 但零条目——伪造"有内容"（§5.3 进入条件）。
    {
        SectionContent c;
        c.status = SectionStatus::Populated;
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // ②a NoFormalResult 无缺项清单——丢"显示缺项"义务（§16 验收要点）。
    {
        SectionContent c;
        c.status = SectionStatus::NoFormalResult;
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // ②b DataInsufficient 无缺项清单——同上（§8.1 表 2④ 全量口径）。
    {
        SectionContent c;
        c.status = SectionStatus::DataInsufficient;
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // ③a NotApplicable 无原因——"—"无依据（ERR-01 不伪造）。
    {
        SectionContent c;
        c.status = SectionStatus::NotApplicable;
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // ③b 其余态携带原因——presence 对称纪律。
    {
        SectionContent c;
        c.status = SectionStatus::Populated;
        SectionEntryView entry;
        entry.entryKey = "ok";
        c.entries.push_back(entry);
        c.notApplicableReason = "不应在场";
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // ④a entryKey 语法违约（大写/过短/过长/非法字符）。
    {
        SectionContent c;
        c.status = SectionStatus::Populated;
        SectionEntryView bad;
        bad.entryKey = "Bad_Key";   // 大写＋下划线（词形 [a-z0-9.-]{2,63} 外）
        c.entries.push_back(bad);
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // ④b entryKey 节内重复（§9.2 结构违约"entryKey 重复"）。
    {
        SectionContent c;
        c.status = SectionStatus::Populated;
        SectionEntryView e1;
        e1.entryKey = "dup-key";
        SectionEntryView e2 = e1;
        c.entries.push_back(e1);
        c.entries.push_back(e2);
        EXPECT_EQ(firstSectionContentViolation(c), ReportErrorCode::DataInvalid);
    }
    // 合法内容逐态通过（nullopt——presence 与键面全过）。
    {
        SectionContent populated;
        populated.status = SectionStatus::Populated;
        SectionEntryView entry;
        entry.entryKey = "peak-torque";   // 词形内：小写＋'-'
        populated.entries.push_back(entry);
        EXPECT_EQ(firstSectionContentViolation(populated), std::nullopt);

        SectionContent noResult;
        noResult.status = SectionStatus::NoFormalResult;
        noResult.missingItems.push_back({"kin.batch-ik.result", "缺正式结果"});
        EXPECT_EQ(firstSectionContentViolation(noResult), std::nullopt);

        SectionContent insufficient;
        insufficient.status = SectionStatus::DataInsufficient;
        insufficient.entries.push_back(entry);   // 有结果条目——允许
        insufficient.missingItems.push_back({"evidence.item", "证据缺失"});
        EXPECT_EQ(firstSectionContentViolation(insufficient), std::nullopt);

        SectionContent notApplicable;
        notApplicable.status = SectionStatus::NotApplicable;
        notApplicable.notApplicableReason = "纯关节路径不涉及段内 IK 连续性";
        EXPECT_EQ(firstSectionContentViolation(notApplicable), std::nullopt);
    }
}

/**
 * 验证 acceptance 5：entryKey 词形边界（§9.2 注释 [a-z0-9.-]{2,63}）——
 * 长度窗与字符集的双侧边界逐点锁定。
 */
TEST(ReportingSectionProvider, EntryKeySyntaxBoundaries_RPT04_ACC5)
{
    // 合法边界：恰 2 字符（下限）、恰 63 字符（上限）、字符集四类各自在列。
    EXPECT_TRUE(isValidEntryKey("ab"));
    EXPECT_TRUE(isValidEntryKey(std::string(63, 'a')));
    EXPECT_TRUE(isValidEntryKey("a.9-x"));   // '.'/'-'/数字/字母混合
    // 非法边界：过短（1）、过长（64）、空串、大写、空白、UTF-8 字节。
    EXPECT_FALSE(isValidEntryKey("a"));
    EXPECT_FALSE(isValidEntryKey(std::string(64, 'a')));
    EXPECT_FALSE(isValidEntryKey(""));
    EXPECT_FALSE(isValidEntryKey("Ab"));
    EXPECT_FALSE(isValidEntryKey("a b"));
    EXPECT_FALSE(isValidEntryKey("轴数"));   // 非 ASCII 词形外字节
    EXPECT_FALSE(isValidEntryKey("a_b"));    // 下划线不在词表
}

}  // namespace
