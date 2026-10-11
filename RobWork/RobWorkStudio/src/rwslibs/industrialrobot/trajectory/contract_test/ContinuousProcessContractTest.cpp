/**
 * @file   ContinuousProcessContractTest.cpp
 * @brief  连续工艺段边界行为校验的契约测试（WP-16-T16——TRJ-08-S3）——
 *         ①零占位守护在 S3 启用后的持续成立（TimedSample/SegmentConstraint
 *         本体零扩展——S3 的承载是 SegmentSpaceType 词表扩展，§6.5/§19.1
 *         的机器断言）；②SegmentSpaceType 三值词表封闭性（ContinuousPro-
 *         cess 在册＋值序锁定——扩值须走需求变更）；③违例码复用与码表元
 *         数据对照（TRJ-CONTINUITY-BROKEN 经真实 StableCodeRegistry 的
 *         requiresComparison/severity 一致——UX-03 判定面）；④单位词形环
 *         境面（"rad/s" 在 core 注册表——比较型素材单位承载）；⑤请求违
 *         约的稳定 token 词形族（"trajectory/boundary/" 前缀）；⑥扩展点
 *         消费经 EvaluationDims v4 判定面的协同序（载荷 token→Profile 资
 *         格）。
 *
 * 设计依据：
 *   - units/trajectory.md §6.5（S3 启用经 SegmentSpaceType 词表扩展承载
 *     ——TimedSample/SegmentConstraint 本体守护保持）、§9.1（驻留/不停
 *     驻边界 R2 行）、§12.7（S3 维度项不在已交付 Profile 注册——v4 递进
 *     后对 v1/v2/v3 恒 false）、§14.4（TRJ-CONTINUITY-BROKEN 比较型码
 *     ——requiresComparison=true 与素材三要素匹配）、§19.1（S3 行交接
 *     物——SegmentSpaceType 词表扩展＋段间边界行为定义）
 *   - 需求 TRJ-08-S3（P1/R2——本头为 acceptance 2"扩展点消费经 Evalua-
 *     tionDims 词表、TRJ-08 接口约束不破坏"的契约面）、TRJ-06（比较型
 *     素材）、ERR-01（稳定码）、NFR-COR-03（违约 fail-fast）
 *   - 任务契约 tasks/foundation/WP-16-T16.json（acceptance 2）
 *
 * 本套件为**集成模式专属**（调用 checkProcessBoundaries 库符号——其实现
 * TU src/ContinuousProcess.cpp 为 Q 构造面集成条件源，冒烟树无符号可链；
 * ProcessSpeedContractTest 同款 gating 口径）。纯值面契约（EvaluationDims
 * v4 词表/唯一书写点扫描）两模式覆盖归 EvaluationDims 套件——分域不重复。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/trajectory/ContinuousProcess.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/EvaluationDims.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace diag = sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;  // UnitToken（比较型素材单位——环境面）

namespace {

// SFINAE 负向成员探测（EvaluationDimsTest 同款设施——S3 启用后的持续守
// 护：边界行为校验的承载是 SegmentSpaceType 词表扩展＋独立校验面，Timed-
// Sample/SegmentConstraint 本体零扩展）。
template <typename T, typename = void>
struct HasMemberSwitchKind : std::false_type {};
template <typename T>
struct HasMemberSwitchKind<T,
    std::void_t<decltype(std::declval<T&>().boundarySwitchKind)>>
    : std::true_type {};
template <typename T, typename = void>
struct HasMemberContinuousProcess : std::false_type {};
template <typename T>
struct HasMemberContinuousProcess<T,
    std::void_t<decltype(std::declval<T&>().continuousProcess)>>
    : std::true_type {};

// 时间曲线最小替身（契约校验面越过前置检查用——违约族用例在后续校验点
// 即被拒绝，sampleAt 永不被调；调用即测试缺陷显性失败。类定义零 Q 构造
// ——返回语句不可达，不触发 Q 符号）。
class NeverCurve final : public trj::ITimeCurve {
public:
    trj::TimedSample sampleAt(double) const override
    {
        ADD_FAILURE() << "契约违约族不应到达曲线求值（校验前置拦截面）";
        throw trj::TrajectoryError("trajectory/test/never-curve",
                                   "契约测试替身不可达求值");
    }
};

}  // namespace

// =====================================================================
// 零占位守护（§6.5——S3 启用后的持续成立）与词表封闭性
// =====================================================================

/**
 * S3 启用后 TimedSample/SegmentConstraint 本体仍无边界行为槽位（§6.5
 * "S3 启用时经需求变更＋本卡修订扩展词表"——WP-16-T16 的承载选择＝
 * SegmentSpaceType 词表扩展＋独立校验面 ContinuousProcess.hpp，本断言
 * 锁定本体封闭性在 S3 启用后继续成立；槽位添加即本 TU 编译失败）。
 */
TEST(TrjContinuousProcessContract, BodyTypesNoExtensionSlots_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    // 负向：TimedSample 零边界语义槽位（编译期——占位添加即编译失败；
    // "TimedSample 五成员"守护在 S3 启用后继续成立）。
    static_assert(!HasMemberSwitchKind<trj::TimedSample>::value,
        "TimedSample 出现 boundarySwitchKind 槽位——S3 的承载是 SegmentSpace"
        "Type 词表扩展＋独立校验面（§6.5/§19.1 S3 行），离散样本不得扩槽位");
    // 负向：SegmentConstraint 零连续工艺槽位（本体三成员守护保持——与
    // S2 批 ProcessSpeedConstraint 的承载选择同源纪律）。
    static_assert(!HasMemberContinuousProcess<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 continuousProcess 槽位——S3 的承载是词表扩"
        "展，本体不得扩槽位");
}

/**
 * SegmentSpaceType 三值词表封闭性（§19.1 S3 行交接物"SegmentSpaceType
 * 词表扩展"的落值面）：ContinuousProcess 在册（运行期值消费）＋值序锁
 * 定 0/1/2（词表封闭性——扩值须走需求变更＋本卡修订，不得静默插值）。
 */
TEST(TrjContinuousProcessContract, SegmentSpaceTypeVocabularyShape_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    // 值序锁定（封闭词表——表尾追加语义：既有值序不得因扩展而变化，
    // 序列化/词表分派的消费面依赖值稳定）。
    static_assert(
        static_cast<std::uint8_t>(trj::SegmentSpaceType::JointLinear) == 0,
        "SegmentSpaceType 词表第一值必须为 JointLinear（§6.2 R1 形态）");
    static_assert(
        static_cast<std::uint8_t>(trj::SegmentSpaceType::CartesianLine) == 1,
        "SegmentSpaceType 词表第二值必须为 CartesianLine（§6.2 R1 形态）");
    static_assert(
        static_cast<std::uint8_t>(trj::SegmentSpaceType::ContinuousProcess)
            == 2,
        "SegmentSpaceType 词表第三值必须为 ContinuousProcess（§19.1 S3 行"
        "交接物——WP-16-T16 批表尾追加）");

    // 运行期值消费（ContinuousProcess 段经段结构守卫的合法面——词表值
    // 不是纸面声明，validateTrajectorySegment 第三分支真实受理；域一致
    // 的最小段构造）。
    trj::TrajectorySegment segment;
    segment.segmentIndex = 0;
    segment.spaceType = trj::SegmentSpaceType::ContinuousProcess;
    trj::Waypoint wp0;
    wp0.kind = trj::WaypointKind::Start;
    wp0.segmentIndex = 0;
    wp0.q = rw::math::Q(1, 0.0);
    trj::Waypoint wp1;
    wp1.kind = trj::WaypointKind::End;
    wp1.segmentIndex = 0;
    wp1.q = rw::math::Q(1, 1.0);
    segment.waypoints = {wp0, wp1};
    EXPECT_NO_THROW(trj::validateTrajectorySegment(segment))
        << "ContinuousProcess 词表值未被段结构守卫受理（词表扩展与校验面"
           "不同步——实现缺陷）";

    // 边界切换语义词表两值封闭（BoundarySwitchKind——值序 0/1）。
    static_assert(
        static_cast<std::uint8_t>(trj::BoundarySwitchKind::Continuous) == 0,
        "BoundarySwitchKind 词表第一值必须为 Continuous（§9.1 驻留/不停驻"
        "边界 R2 行）");
    static_assert(
        static_cast<std::uint8_t>(trj::BoundarySwitchKind::Dwell) == 1,
        "BoundarySwitchKind 词表出现第三值——切换语义扩展须走需求变更＋本"
        "卡增量修订，不得静默扩值");
}

/**
 * 违例码复用与码表元数据对照（TRJ-CONTINUITY-BROKEN"连续性破坏"语义本
 * 体的复用面——零新增码）：①码表仍 12 码（§14.4 全表零连续工艺专属码
 * ——T14/T15"码表零专属码"守护同族）；②经真实 StableCodeRegistry 注册
 * 后 find 命中且 requiresComparison=true（违例素材比较型三要素为强制
 * ——UX-03 判定面）＋severity=Error（阻断级——§14.4 行 6）。
 */
TEST(TrjContinuousProcessContract, ReusedContinuityBrokenCodeMetadata_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    // ①码表规模（12 码冻结——零 S3 专属码；描述符工厂为库符号）。
    const std::vector<diag::CodeDescriptor> descriptors =
        trj::trajectoryCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 12U);

    // ②真实注册表对照（ProcessSpeedContractTest 同款真实设施——非自持
    // 副本；seal 前注册、seal 后只读查询）。
    diag::StableCodeRegistry registry;
    trj::registerTrajectoryCodes(registry);
    registry.seal();
    const diag::CodeDescriptor* continuityCode =
        registry.find(std::string(trj::kTrjContinuityBroken));
    ASSERT_NE(continuityCode, nullptr)
        << "TRJ-CONTINUITY-BROKEN 未注册（码表元数据对照前提缺失）";
    // 比较型强制：边界行为违例素材携带比较型三要素（实际/期望/单位），
    // 码描述符的 requiresComparison 必须与素材形态一致。
    EXPECT_TRUE(continuityCode->requiresComparison);
    // 阻断级：§14.4 行 6——TRJ-CONTINUITY-BROKEN 是 error 码。
    EXPECT_EQ(continuityCode->severity, diag::DiagnosticSeverity::Error);
}

/**
 * 比较型素材单位词形的环境面（"rad/s" 在 core 单位注册表——违例素材单
 * 位承载的前提；量纲＝角速度——关节域比较型素材的量纲诚实转写对应）。
 */
TEST(TrjContinuousProcessContract, AngularVelocityUnitTokenRegistered_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    const auto rads = core::UnitToken::find("rad/s");
    ASSERT_TRUE(rads.has_value())
        << "core 单位注册表缺少 rad/s token（环境缺陷——违例素材单位口径"
           "不可用，fail-fast 面）";
    EXPECT_EQ(std::string(rads->symbol()), "rad/s");
    EXPECT_EQ(rads->kind(), core::QuantityKind::AngularVelocity);
}

// =====================================================================
// 请求违约的稳定 token 词形族（NFR-COR-03——错误二分的调用方违约面）
// =====================================================================

/**
 * 违约 token 前缀族（"trajectory/boundary/" 稳定前缀——错误二分的机器
 * 可判别面）：曲线缺失/时长非法/邻域半步非法/阈值非法/边界时刻越界五
 * 类首错各抛 TrajectoryError 且 token 前缀命中；文案中文非空。
 */
TEST(TrjContinuousProcessContract, FailFastTokenPrefixFamily_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    const auto expectPrefix = [](const trj::ProcessBoundaryCheckRequest&
                                     request,
                                 const char* what) {
        try {
            (void)trj::checkProcessBoundaries(request);
            FAIL() << "应拒绝: " << what;
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token().find("trajectory/boundary/"), 0U)
                << "token 前缀违约: " << e.token() << "（" << what << "）";
            EXPECT_FALSE(e.what() == nullptr);
        }
    };

    // 曲线缺失。
    {
        trj::ProcessBoundaryCheckRequest request;
        request.totalDurationS = 1.0;
        request.neighborDeltaS = 1e-7;
        request.minContinuousSpeed = 0.1;
        expectPrefix(request, "timeCurve 缺失");
    }
    // 时长非法（曲线在位＋边界空——命中时长校验点）。
    {
        trj::ProcessBoundaryCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.totalDurationS = 0.0;
        request.neighborDeltaS = 1e-7;
        request.minContinuousSpeed = 0.1;
        expectPrefix(request, "总时长 0");
    }
    // 邻域半步非法（零——本域零自设默认，强制显式）。
    {
        trj::ProcessBoundaryCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.totalDurationS = 1.0;
        request.neighborDeltaS = 0.0;
        request.minContinuousSpeed = 0.1;
        expectPrefix(request, "邻域半步 0");
    }
    // 不停驻阈值非法（零——工艺语义量强制显式）。
    {
        trj::ProcessBoundaryCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.totalDurationS = 1.0;
        request.neighborDeltaS = 1e-7;
        request.minContinuousSpeed = 0.0;
        expectPrefix(request, "阈值 0");
    }
    // 边界时刻越界（恰在端点——严格内语义；前四项合法使循环可达）。
    {
        trj::ProcessBoundaryCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.totalDurationS = 1.0;
        request.neighborDeltaS = 1e-7;
        request.minContinuousSpeed = 0.1;
        trj::ProcessBoundarySpan span;
        span.boundaryIndex = 0;
        span.tBoundaryS = 1.0;  // ＝总时长——越界
        span.expectedSwitch = trj::BoundarySwitchKind::Continuous;
        request.boundaries = {span};
        expectPrefix(request, "边界时刻＝总时长");
    }
}

// =====================================================================
// 扩展点消费协同（acceptance 2——v4 词表判定面的消费序）
// =====================================================================

/**
 * S3 交付面经 v4 词表的协同消费序（评估器组装面协议的机器承载）：payload
 * token=v4 已知 ∧ 连续工艺边界项过 v4 资格判定 → 接受；v4 项冒入 v1/v2/
 * v3 判定面 → 拒绝（已交付 Profile 集合永不收编——§12.7）。
 */
TEST(TrjContinuousProcessContract, EvaluationDimsV4ConsumerOrder_WP16T16_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S3", {}, std::nullopt);

    // 接受：v4 载荷＋v4 注册项（当前产出组合）。
    EXPECT_TRUE(
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken)
        && trj::isTrjProfileV4ItemId(
            trj::kTrjItemIdContinuousProcessBoundary));
    // 拒绝：v4 项对历史判定面（v1/v2/v3 集合永不收编 S3 维度项——T15
    // 批"未来 S3 词形对 v3 恒 false"的负向断言在本批转正后保持）。
    EXPECT_FALSE(trj::isTrjProfileV1ItemId(
        trj::kTrjItemIdContinuousProcessBoundary));
    EXPECT_FALSE(trj::isTrjProfileV2ItemId(
        trj::kTrjItemIdContinuousProcessBoundary));
    EXPECT_FALSE(trj::isTrjProfileV3ItemId(
        trj::kTrjItemIdContinuousProcessBoundary));
    // 历史载荷词形仍然已知（旧 payload 永久可读——词表只增不删）。
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v3"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v2"));
    EXPECT_TRUE(trj::isKnownTrjPayloadKindToken("trj.sequence-plan.v1"));
}
