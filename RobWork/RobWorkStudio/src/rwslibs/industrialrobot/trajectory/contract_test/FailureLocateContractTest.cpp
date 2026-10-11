/**
 * @file   FailureLocateContractTest.cpp
 * @brief  失败段诊断定位的契约测试（WP-16-T09）——ERR-01 字段义务锁定
 *         （对象/上下文/原因/建议动作；比较型含实际值/要求值/单位）＋码表
 *         元数据对照（severity→level 映射、requiresComparison 一致性）＋
 *         subject 回退绑定链＋违约族 fail-fast＋确定性（输入序＝输出序）＋
 *         全轨迹级定位语义。素材直接构造（T04~T08 各管线的素材**语义**
 *         已随各自任务验证——本套件钉扎的是"素材→诊断条目"组装契约本身，
 *         端到端管线→素材→诊断的全链用例见 test/FailureLocateTest.cpp）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.4（诊断构造经 IDiagnosticFactory；产生者仅
 *     登记判定不越权）、§6.2（FailedSegmentRecord 字段义务）、§20 追踪
 *     矩阵 TRJ-06/ERR-01 行（任务 T09）
 *   - 需求 TRJ-06（四类失败定位到具体段落与原因）、ERR-01（稳定诊断项以
 *     subjectObjectId 绑定对象＋局部/运行时名称＋原因＋建议动作；比较型
 *     额外实际值/期望值/单位；不适用显式标记不伪造）、NFR-COR-02（确定性）
 *   - units/diagnostics.md §4.2/§4.5（工厂校验链——码已注册/subject 边界/
 *     比较型完整性/占位一致；码表元数据＝severity/requiresComparison 权威）
 *   - 任务契约 tasks/foundation/WP-16-T09.json（acceptance 2——"诊断字段
 *     满足 ERR-01（对象/上下文/原因/建议动作；比较型含实际值/要求值/单位）"）
 *
 * 本套件为**两模式皆跑**（非条件源）：被测面 locateFailures 与全部输入
 * （FailedSegmentRecord/diagnostics 注册表与工厂）均为纯值面——零
 * rw::math::Q 构造、零框架库符号引用（FailureLocate.cpp 同款口径）。真实
 * StableCodeRegistry＋DiagnosticsFactory（非替身）——码表元数据权威链
 * （§14.4"码值权威归 StableCodeRegistry"）在真实设施上验证。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/FailureLocate.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace diag = sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 测试设施：固定时钟＋真实码表/工厂装置（diagnostics IClock 注释的
// "ManualClock 兼容形态"替身——确定性 emittedAtUtc）
// =====================================================================

/// 固定时钟（emittedAtUtc 确定性——NFR-COR-02；2026-10-11T00:00:00Z）。
class FixedClock final : public diag::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override
    {
        // 固定参考时刻（s；2026-10-11T00:00:00Z——unix 秒 1792051200）。
        return std::chrono::system_clock::time_point{
            std::chrono::seconds(1792051200)};
    }
};

/**
 * @brief 真实码表＋工厂装置（TRJ-* 12 码装配期注册→seal→工厂——生产装配
 *        序同款；真实设施非替身——码表元数据权威链在真实注册表上验证）。
 */
class FailureLocateContractTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 装配序（L5 同款）：TRJ-* 全表注册→seal（进入运行期）→工厂。
        trj::registerTrajectoryCodes(m_registry);
        m_registry.seal();
        m_factory = std::make_unique<diag::DiagnosticsFactory>(
            m_registry, m_clock);
    }

    /// 合法身份（"cid-<64hex>" 严格解析——测试锚定值，非全零即可）。
    static core::ContentIdentity someIdentity()
    {
        return core::ContentIdentity::fromCanonical(
            "cid-00000000000000000000000000000000000000000000000000000000000000"
            "01");
    }

    /// 合法对象 ID（"obj-<32hex>" 严格解析）。
    static core::ObjectId someObject(const std::string& hex)
    {
        return core::ObjectId::fromCanonical("obj-" + hex);
    }

    /// 空请求骨架（合法身份＋无回退；用例按需覆盖字段）。
    trj::FailureLocateRequest baseRequest()
    {
        trj::FailureLocateRequest request;
        request.snapshotId = someIdentity();
        request.sliceId = someIdentity();
        return request;
    }

    diag::StableCodeRegistry m_registry;
    FixedClock m_clock;
    std::unique_ptr<diag::DiagnosticsFactory> m_factory;
};

// =====================================================================
// 素材构造辅助（§6.2 FailedSegmentRecord 字段义务的最小合法素材）
// =====================================================================

/// 段级素材（无 subject——回退链用例与非回退用例共用骨架）。
trj::FailedSegmentRecord segmentRecord(const std::string& reasonToken,
                                       std::uint32_t segmentIndex,
                                       const std::string& phaseToken,
                                       std::optional<double> s = std::nullopt)
{
    trj::FailedSegmentRecord record;
    record.segmentIndex = segmentIndex;
    record.phaseToken = phaseToken;
    record.reasonToken = reasonToken;
    record.pathParameter = s;
    record.cause = "测试原因（中文——ERR-01 必填）";
    record.recommendedAction = "测试建议动作（中文——ERR-01 必填）";
    return record;
}

/// 比较型三要素（条件数对照——实际/阈值/无量纲 "1"；奇异邻域素材用）。
core::ComparativeFields conditionNumberComparison(double actual, double threshold)
{
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        actual, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                            {}, {}, std::string("cond")));
    // 无量纲单位 token "1"（core Units 注册表——条件数无纲量）。
    cmp.actual.unit = *core::UnitToken::find("1");
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        threshold,
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    cmp.expected.unit = *core::UnitToken::find("1");
    return cmp;
}

// =====================================================================
// 契约用例
// =====================================================================

/**
 * 契约：四类失败（无路径/分支跳变/奇异邻域/限制超标）素材各组装一条诊断
 * 条目，ERR-01 字段义务逐项成立（TRJ-06/ERR-01——acceptance 2）。
 * 观测点：code=reasonToken 透传、subject 有效、context 含段落/阶段定位、
 * cause/action 透传、比较型条目三要素齐备（实际值/要求值/单位）。
 */
TEST_F(FailureLocateContractTest, FourFailureClassesProduceErr01CompleteEntries)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06", "ERR-01"},
                  std::vector<std::string>{}, std::nullopt);
    // ---- 四类素材一次投递（§14.4 词表前三类＋限制超标；phase 取各自管线
    // 词表——§14.1.6）：无路径（plan-ptp）、分支跳变（plan-line，s=0.5）、
    // 奇异邻域（plan-line，s=0.75，比较型条件数）、限制超标（time-param，
    // s=0.25，比较型速度）。奇异邻域绑定对象（碰撞代表点投影语义）；其余
    // 素材未绑对象→落请求面回退（subject 绑定链——ERR-01）。
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("000000000000000000000000000000a1");
    request.records.push_back(segmentRecord(
        std::string(trj::kTrjNoPath), 2u, trj::kPhasePlanPtp));
    trj::FailedSegmentRecord branch =
        segmentRecord(std::string(trj::kTrjBranchJump), 0u, trj::kPhasePlanLine,
                      0.5);
    branch.subject = someObject("0000000000000000000000000000000a");
    request.records.push_back(std::move(branch));
    trj::FailedSegmentRecord singular = segmentRecord(
        std::string(trj::kTrjSingularNeighborhood), 0u, trj::kPhasePlanLine,
        0.75);
    singular.cause = "雅可比条件数进入奇异邻域（TRJ-SINGULAR-NEIGHBORHOOD）";
    singular.comparison = conditionNumberComparison(2500.0, 1000.0);
    request.records.push_back(std::move(singular));
    trj::FailedSegmentRecord limit =
        segmentRecord(std::string(trj::kTrjLimitExceeded), 1u,
                      trj::kPhaseTimeParam, 0.25);
    core::ComparativeFields limitCmp;
    limitCmp.actual.quantity = core::SourcedValue<double>::provided(
        3.14, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                          {}, {}, std::string("peak")));
    limitCmp.actual.unit = *core::UnitToken::find("rad/s");
    limitCmp.expected.quantity = core::SourcedValue<double>::provided(
        1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    limitCmp.expected.unit = *core::UnitToken::find("rad/s");
    limit.comparison = std::move(limitCmp);
    request.records.push_back(std::move(limit));

    // ---- 组装（真实注册表＋真实工厂）。
    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);

    // 后置：一一对应（实现保证——FailureLocateResult 注）。
    ASSERT_EQ(result.entries.size(), 4u);
    ASSERT_EQ(result.locatedRecords.size(), result.entries.size());

    // ---- 条目 1：无路径（error 码——§14.4 严重度列）。
    const diag::DiagnosticEntry& noPath = result.entries[0];
    EXPECT_EQ(noPath.record.code, std::string(trj::kTrjNoPath));
    ASSERT_TRUE(noPath.record.subject.has_value());
    EXPECT_TRUE(noPath.record.subject->isValid());  // ERR-01 对象绑定
    // 非比较型码素材未携带 comparison→条目不伪造比较型字段。
    EXPECT_FALSE(noPath.record.comparison.has_value());
    EXPECT_EQ(noPath.severity, diag::DiagnosticSeverity::Error);

    // ---- 条目 2：分支跳变（warning 码＋s 定位）。
    const diag::DiagnosticEntry& branchEntry = result.entries[1];
    EXPECT_EQ(branchEntry.record.code, std::string(trj::kTrjBranchJump));
    EXPECT_EQ(branchEntry.severity, diag::DiagnosticSeverity::Warning);

    // ---- 条目 3：奇异邻域（warning 码＋比较型三要素）。
    const diag::DiagnosticEntry& singularEntry = result.entries[2];
    EXPECT_EQ(singularEntry.record.code,
              std::string(trj::kTrjSingularNeighborhood));
    ASSERT_TRUE(singularEntry.record.comparison.has_value());
    // 实际值/要求值/单位三要素齐备（UX-03/ERR-01）——SourcedValue Provided
    // 态可取值、单位 token 有效。
    EXPECT_EQ(singularEntry.record.comparison->actual.quantity.state(),
              core::FieldState::Provided);
    ASSERT_TRUE(
        singularEntry.record.comparison->actual.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*singularEntry.record.comparison->actual.quantity.tryValue(),
                     2500.0);
    ASSERT_TRUE(singularEntry.record.comparison->expected.quantity.tryValue()
                    .has_value());
    EXPECT_DOUBLE_EQ(
        *singularEntry.record.comparison->expected.quantity.tryValue(), 1000.0);
    EXPECT_TRUE(singularEntry.record.comparison->actual.unit.isValid());
    EXPECT_EQ(std::string(singularEntry.record.comparison->actual.unit.symbol()),
              "1");
    EXPECT_EQ(singularEntry.severity, diag::DiagnosticSeverity::Warning);

    // ---- 条目 4：限制超标（error 码＋比较型 rad/s）。
    const diag::DiagnosticEntry& limitEntry = result.entries[3];
    EXPECT_EQ(limitEntry.record.code, std::string(trj::kTrjLimitExceeded));
    ASSERT_TRUE(limitEntry.record.comparison.has_value());
    EXPECT_TRUE(limitEntry.record.comparison->actual.unit.isValid());
    EXPECT_EQ(std::string(limitEntry.record.comparison->actual.unit.symbol()),
              "rad/s");
    EXPECT_EQ(limitEntry.severity, diag::DiagnosticSeverity::Error);

    // ---- ERR-01 共性字段逐条目断言（对象/上下文/原因/建议动作）。
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        const diag::DiagnosticEntry& entry = result.entries[i];
        EXPECT_TRUE(entry.record.subject.has_value()) << "条目 " << i;
        EXPECT_TRUE(entry.record.subject->isValid()) << "条目 " << i;
        EXPECT_FALSE(entry.record.context.empty()) << "条目 " << i;
        EXPECT_FALSE(entry.record.cause.empty()) << "条目 " << i;
        EXPECT_FALSE(entry.record.recommendedAction.empty()) << "条目 " << i;
        // 上下文（ERR-01"上下文"＝TRJ-06"具体段落"承载）含定位要素。
        EXPECT_NE(entry.record.context.find("轨迹段"), std::string::npos)
            << "条目 " << i << " context=" << entry.record.context;
        // 产生者仅登记：分类/严重度自码表（域内未越权改判——§14.4）。
        EXPECT_TRUE(entry.entryId >= 1u) << "条目 " << i;
    }
}

/**
 * 契约：定位上下文串承载"具体段落与原因"的段落半区（TRJ-06）——段序号/
 * 阶段 token/段内路径参数 s 以确定性格式进 context；全轨迹级保留值
 * （0xFFFFFFFF）不渲染段序号（渲染"段 4294967295"会误导）而以"全轨迹级"
 * 表达（§6.2 FailedSegmentRecord 注）。
 */
TEST_F(FailureLocateContractTest, LocateContextCarriesSegmentPhaseAndPathParameter)
{
    IRD_TEST_INFO("TRJ-06", {}, std::nullopt);
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("000000000000000000000000000000a2");
    // 段级＋可定位到点：段 3（阶段 time-param）s=0.5。
    request.records.push_back(segmentRecord(std::string(trj::kTrjLimitExceeded),
                                            3u, trj::kPhaseTimeParam, 0.5));
    // 全轨迹级（限值未定义——T08 makeUndefinedLimitRecord 同款形态）。
    request.records.push_back(segmentRecord(
        std::string(trj::kTrjTimeParamFailed), trj::kWholeTrajectorySegmentIndex,
        trj::kPhaseTimeParam));
    // 段级不可定位到点（pathParameter 缺省）。
    request.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 7u, trj::kPhasePlanPtp));
    request.records[0].comparison = conditionNumberComparison(1.0, 1.0);

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);
    ASSERT_EQ(result.entries.size(), 3u);

    // 条目 1：段级＋s 固定六位小数（确定性格式——NFR-COR-02）。
    EXPECT_EQ(result.entries[0].record.context,
              "轨迹段 3（阶段 time-param） 路径参数 s=0.500000");
    // 条目 2：全轨迹级（无段序号渲染）。
    EXPECT_EQ(result.entries[1].record.context,
              "全轨迹级（阶段 time-param）");
    // 条目 3：段级不可定位到点（无 s 段）。
    EXPECT_EQ(result.entries[2].record.context, "轨迹段 7（阶段 plan-ptp）");
}

/**
 * 契约：severity→level 机械映射（core DiagData.hpp 登记的注册表强制映射
 * "severity==Error ⇒ level==Error，其余 ⇒ Warning"）——error 码（限制超标）
 * 阻断级、warning 码（分支跳变/奇异邻域——V-07"不阻断"）非阻断级。
 * 对照权威＝码表描述符（trajectoryCodeDescriptors——§14.4 物化面）。
 */
TEST_F(FailureLocateContractTest, SeverityFromCodeTableDrivesBlockingLevel)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    // 码表元数据锚定（§14.4 登记表：LIMIT-EXCEEDED=error、BRANCH-JUMP=
    // SINGULAR-NEIGHBORHOOD=warning）——注册表 find 为唯一权威查询面。
    const diag::CodeDescriptor* limitDesc = m_registry.find(trj::kTrjLimitExceeded);
    const diag::CodeDescriptor* branchDesc =
        m_registry.find(trj::kTrjBranchJump);
    const diag::CodeDescriptor* singularDesc =
        m_registry.find(trj::kTrjSingularNeighborhood);
    ASSERT_NE(limitDesc, nullptr);
    ASSERT_NE(branchDesc, nullptr);
    ASSERT_NE(singularDesc, nullptr);
    EXPECT_EQ(limitDesc->severity, diag::DiagnosticSeverity::Error);
    EXPECT_EQ(branchDesc->severity, diag::DiagnosticSeverity::Warning);
    EXPECT_EQ(singularDesc->severity, diag::DiagnosticSeverity::Warning);
    // 比较型登记与 §14.4"比较型"列一致（LIMIT-EXCEEDED 是/SINGULAR 是/
    // NO-PATH 否）。
    EXPECT_TRUE(limitDesc->requiresComparison);
    EXPECT_TRUE(singularDesc->requiresComparison);
    const diag::CodeDescriptor* noPathDesc = m_registry.find(trj::kTrjNoPath);
    ASSERT_NE(noPathDesc, nullptr);
    EXPECT_FALSE(noPathDesc->requiresComparison);

    // 条目侧：level 随码表 severity 映射（阻断轴），severity 自码表原值。
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("000000000000000000000000000000a3");
    trj::FailedSegmentRecord limit = segmentRecord(
        std::string(trj::kTrjLimitExceeded), 0u, trj::kPhaseTimeParam, 0.0);
    limit.comparison = conditionNumberComparison(2.0, 1.0);
    request.records.push_back(std::move(limit));
    request.records.push_back(
        segmentRecord(std::string(trj::kTrjBranchJump), 1u, trj::kPhasePlanLine));

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);
    ASSERT_EQ(result.entries.size(), 2u);
    EXPECT_EQ(result.entries[0].record.level, core::DiagnosticLevel::Error);
    EXPECT_EQ(result.entries[0].severity, diag::DiagnosticSeverity::Error);
    EXPECT_EQ(result.entries[1].record.level, core::DiagnosticLevel::Warning);
    EXPECT_EQ(result.entries[1].severity, diag::DiagnosticSeverity::Warning);
}

/**
 * 契约：subject 回退绑定链（素材可空面→诊断必填面的桥）——素材有对象→
 * 直接绑定（回退值不覆盖）；素材缺对象→落请求面 subjectFallback；两级皆
 * 缺→TrajectoryError fail-fast（ERR-01 subject 边界——diagnostics §4.2
 * "用户级码必须携带合法非全零 subjectObjectId"）。
 */
TEST_F(FailureLocateContractTest, SubjectBindingPrefersRecordThenFallbackThenRejects)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    const core::ObjectId fallback = someObject("0000000000000000000000000000000b");
    const core::ObjectId own = someObject("0000000000000000000000000000000c");

    // 素材有对象：直接绑定。
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = fallback;
    trj::FailedSegmentRecord withOwn =
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp);
    withOwn.subject = own;
    request.records.push_back(std::move(withOwn));
    // 素材缺对象：落回退。
    request.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 1u, trj::kPhasePlanPtp));

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);
    ASSERT_EQ(result.entries.size(), 2u);
    ASSERT_TRUE(result.entries[0].record.subject.has_value());
    EXPECT_EQ(*result.entries[0].record.subject, own);   // 素材值优先
    ASSERT_TRUE(result.entries[1].record.subject.has_value());
    EXPECT_EQ(*result.entries[1].record.subject, fallback);  // 回退兜底

    // 两级皆缺：拒绝（fail-fast——不产无对象条目）。
    trj::FailureLocateRequest orphan = baseRequest();
    orphan.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp));
    EXPECT_THROW(trj::locateFailures(orphan, m_registry, *m_factory),
                 trj::TrajectoryError);
}

/**
 * 契约：码面违约族 fail-fast（调用方契约违约不转诊断码——§15.0）——
 * 未注册码/他域所有者码拒绝；ERR-01 名称投影（局部/运行时名）透传条目、
 * 缺席显式 nullopt 不伪造。
 */
TEST_F(FailureLocateContractTest, CodeFaceViolationsFailFastAndNamesProject)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "TRJ-06"},
                  std::vector<std::string>{}, std::nullopt);
    // 未注册码（TRJ 词形但未在册——码值权威归注册表）。
    trj::FailureLocateRequest unregistered = baseRequest();
    unregistered.records.push_back(
        segmentRecord("TRJ-DOES-NOT-EXIST", 0u, trj::kPhasePlanPtp));
    EXPECT_THROW(trj::locateFailures(unregistered, m_registry, *m_factory),
                 trj::TrajectoryError);

    // 他域所有者码（diagnostics 内置 RT 族——TRJ 素材管道混线拒绝）。
    trj::FailureLocateRequest foreign = baseRequest();
    foreign.subjectFallback = someObject("0000000000000000000000000000000d");
    foreign.records.push_back(
        segmentRecord("RT-ROBWORK-ERROR", 0u, trj::kPhasePlanPtp));
    EXPECT_THROW(trj::locateFailures(foreign, m_registry, *m_factory),
                 trj::TrajectoryError);

    // 空码拒绝。
    trj::FailureLocateRequest emptyCode = baseRequest();
    emptyCode.subjectFallback = someObject("0000000000000000000000000000000d");
    emptyCode.records.push_back(
        segmentRecord("", 0u, trj::kPhasePlanPtp));
    EXPECT_THROW(trj::locateFailures(emptyCode, m_registry, *m_factory),
                 trj::TrajectoryError);

    // 名称投影：有值透传（ERR-01 局部/运行时名），缺席 nullopt 不伪造。
    trj::FailureLocateRequest named = baseRequest();
    named.subjectFallback = someObject("0000000000000000000000000000000e");
    named.subjectLocalName = "轨迹段 0（工程名）";
    named.subjectRuntimeName = "UR10e/joint_3";
    named.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp));
    const trj::FailureLocateResult namedResult =
        trj::locateFailures(named, m_registry, *m_factory);
    ASSERT_EQ(namedResult.entries.size(), 1u);
    EXPECT_EQ(namedResult.entries[0].record.localName,
              std::optional<std::string>("轨迹段 0（工程名）"));
    EXPECT_EQ(namedResult.entries[0].record.runtimeName,
              std::optional<std::string>("UR10e/joint_3"));

    trj::FailureLocateRequest unnamed = baseRequest();
    unnamed.subjectFallback = someObject("0000000000000000000000000000000f");
    unnamed.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp));
    const trj::FailureLocateResult unnamedResult =
        trj::locateFailures(unnamed, m_registry, *m_factory);
    ASSERT_EQ(unnamedResult.entries.size(), 1u);
    EXPECT_FALSE(unnamedResult.entries[0].record.localName.has_value());
    EXPECT_FALSE(unnamedResult.entries[0].record.runtimeName.has_value());
}

/**
 * 契约：素材违约族 fail-fast——比较型码缺 comparison（UX-03 三要素）、
 * 文案空串（ERR-01 必填）、评估路径身份缺失（§14.4 snapshotId/sliceId
 * 必填）、空素材序列合法（零失败零诊断，非错误）。
 */
TEST_F(FailureLocateContractTest, RecordViolationsFailFastAndEmptyInputIsLegal)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    // 比较型码缺 comparison（限制超标素材无三要素）。
    trj::FailureLocateRequest noCmp = baseRequest();
    noCmp.subjectFallback = someObject("00000000000000000000000000000010");
    noCmp.records.push_back(
        segmentRecord(std::string(trj::kTrjLimitExceeded), 0u,
                      trj::kPhaseTimeParam, 0.5));
    EXPECT_THROW(trj::locateFailures(noCmp, m_registry, *m_factory),
                 trj::TrajectoryError);

    // cause 空串（ERR-01 原因必填）。
    trj::FailureLocateRequest emptyCause = baseRequest();
    emptyCause.subjectFallback = someObject("00000000000000000000000000000011");
    trj::FailedSegmentRecord blank =
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp);
    blank.cause.clear();
    emptyCause.records.push_back(std::move(blank));
    EXPECT_THROW(trj::locateFailures(emptyCause, m_registry, *m_factory),
                 trj::TrajectoryError);

    // recommendedAction 空串（ERR-01 建议动作必填）。
    trj::FailureLocateRequest emptyAction = baseRequest();
    emptyAction.subjectFallback = someObject("00000000000000000000000000000012");
    trj::FailedSegmentRecord blankAction =
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp);
    blankAction.recommendedAction.clear();
    emptyAction.records.push_back(std::move(blankAction));
    EXPECT_THROW(trj::locateFailures(emptyAction, m_registry, *m_factory),
                 trj::TrajectoryError);

    // snapshotId 缺失（全零保留值——§14.4 评估路径必填）。
    trj::FailureLocateRequest noSnapshot = baseRequest();
    noSnapshot.snapshotId = core::ContentIdentity{};  // 全零＝空
    EXPECT_THROW(trj::locateFailures(noSnapshot, m_registry, *m_factory),
                 trj::TrajectoryError);
    // sliceId 缺失。
    trj::FailureLocateRequest noSlice = baseRequest();
    noSlice.sliceId = core::ContentIdentity{};
    EXPECT_THROW(trj::locateFailures(noSlice, m_registry, *m_factory),
                 trj::TrajectoryError);

    // 空素材序列：合法结局（两向量皆空）。
    const trj::FailureLocateResult empty =
        trj::locateFailures(baseRequest(), m_registry, *m_factory);
    EXPECT_TRUE(empty.entries.empty());
    EXPECT_TRUE(empty.locatedRecords.empty());
}

/**
 * 契约：确定性（NFR-COR-02）——同请求两次调用，输出条目的 ERR-01 语义面
 * 逐字段等价（record 全等；entryId/emittedAtUtc 为工厂会话态分配——固定
 * 时钟下 emittedAtUtc 相同、entryId 单调递增不属值等价面）；输出序＝输入
 * 序（管线编排序语义保留——不重排）。
 */
TEST_F(FailureLocateContractTest, DeterministicOutputOrderEqualsInputOrder)
{
    IRD_TEST_INFO("NFR-COR-02", {}, std::nullopt);
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000013");
    // 故意按"时间参数化→PTP"的非常规序投递——断言输出保持该序（不重排）。
    trj::FailedSegmentRecord limit = segmentRecord(
        std::string(trj::kTrjLimitExceeded), 5u, trj::kPhaseTimeParam, 1.0);
    limit.comparison = conditionNumberComparison(1.5, 1.0);
    request.records.push_back(std::move(limit));
    request.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 2u, trj::kPhasePlanPtp));

    const trj::FailureLocateResult first =
        trj::locateFailures(request, m_registry, *m_factory);
    const trj::FailureLocateResult second =
        trj::locateFailures(request, m_registry, *m_factory);

    ASSERT_EQ(first.entries.size(), 2u);
    // 输入序＝输出序（非常规序保持——TRJ-06 定位不依赖排序假设）。
    EXPECT_EQ(first.entries[0].record.code, std::string(trj::kTrjLimitExceeded));
    EXPECT_EQ(first.entries[1].record.code, std::string(trj::kTrjNoPath));
    // 语义面等价：record 逐字段全等（同固定时钟→emittedAtUtc 同值）。
    for (std::size_t i = 0; i < first.entries.size(); ++i) {
        EXPECT_TRUE(first.entries[i].record == second.entries[i].record)
            << "条目 " << i;
        EXPECT_EQ(first.entries[i].severity, second.entries[i].severity);
        EXPECT_EQ(first.entries[i].category, second.entries[i].category);
        EXPECT_EQ(first.entries[i].emittedAtUtc, second.entries[i].emittedAtUtc);
        EXPECT_EQ(first.locatedRecords[i], second.locatedRecords[i]);
    }
    // entryId 为工厂单调分配（会话态——跨调用必然不同，不属值等价面）。
    EXPECT_NE(first.entries[0].entryId, second.entries[0].entryId);
}

/**
 * 契约：上下文身份绑定（§14.4"评估路径 snapshotId/sliceId 必填"）——条目
 * DiagContext 携带请求面身份原值＋来源 token（"trajectory"／
 * "evaluate.failure-locate"——诊断目录按此归组）；params 空（TRJ 码
 * paramSchema "[]"——工厂占位一致性校验通过即为证据）。
 */
TEST_F(FailureLocateContractTest, EntryContextCarriesEvaluationIdentity)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000014");
    request.records.push_back(
        segmentRecord(std::string(trj::kTrjNoPath), 0u, trj::kPhasePlanPtp));

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);
    ASSERT_EQ(result.entries.size(), 1u);
    const diag::DiagContext& ctx = result.entries[0].context;
    EXPECT_TRUE(ctx.snapshotId.has_value());
    EXPECT_EQ(*ctx.snapshotId, request.snapshotId);   // 身份原值透传
    EXPECT_TRUE(ctx.sliceId.has_value());
    EXPECT_EQ(*ctx.sliceId, request.sliceId);
    EXPECT_EQ(ctx.sourceUnit, "trajectory");
    EXPECT_EQ(ctx.sourceInterface, "evaluate.failure-locate");
    EXPECT_TRUE(ctx.params.empty());  // 占位一致（paramSchema "[]"）
}

}  // namespace
