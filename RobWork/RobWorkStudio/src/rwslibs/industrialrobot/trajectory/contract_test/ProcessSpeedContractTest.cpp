/**
 * @file   ProcessSpeedContractTest.cpp
 * @brief  工艺速度约束校验的契约测试（WP-16-T15——TRJ-08-S2）——①零占
 *         位守护在 S2 启用后的持续成立（SegmentConstraint 本体零工艺速
 *         度槽位——§6.5"以段约束扩展版本承载"的机器断言）；②违例码复
 *         用与码表元数据对照（TRJ-LIMIT-EXCEEDED 经真实 StableCodeRegistry
 *         的 requiresComparison/severity 一致——UX-03 判定面）；③单位词
 *         形环境面（"m/s" 在 core 注册表——比较型素材单位承载）；④请求
 *         违约的稳定 token 词形族（"trajectory/process-speed/" 前缀——
 *         错误二分的调用方违约面）；⑤扩展点消费经 EvaluationDims v3 判
 *         定面的协同序（载荷 token→Profile 资格）。
 *
 * 设计依据：
 *   - units/trajectory.md §6.5（S2 启用后 SegmentConstraint 仍无槽位——
 *     扩展版本结构承载）、§12.7（S2 维度项不在已交付 Profile 注册）、
 *     §14.4（TRJ-LIMIT-EXCEEDED 比较型码——requiresComparison=true 与
 *     素材三要素匹配）、§19.1（S2 行交接物）
 *   - 需求 TRJ-08-S2（P1/R2——本头为 acceptance 2"扩展点消费经 Evalua-
 *     tionDims 词表、TRJ-08 接口约束不破坏"的契约面）、TRJ-06（比较型
 *     素材）、ERR-01（稳定码）、NFR-COR-03（违约 fail-fast）
 *   - 任务契约 tasks/foundation/WP-16-T15.json（acceptance 2）
 *
 * 本套件为**集成模式专属**（调用 checkProcessSpeed 库符号——其实现 TU
 * src/ProcessSpeed.cpp 为 Q 构造面集成条件源，冒烟树无符号可链；Kinema-
 * ticsPortContractTest 同款 gating 口径）。纯值面契约（EvaluationDims v3
 * 词表/唯一书写点扫描）两模式覆盖归 EvaluationDims 套件——分域不重复。
 */

#include <gtest/gtest.h>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/EvaluationDims.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/KinematicsPort.hpp>
#include <sdurws/ird/trajectory/ProcessSpeed.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace diag = sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;  // UnitToken（比较型素材单位——环境面）

namespace {

// SFINAE 负向成员探测（EvaluationDimsTest 同款设施——S2 启用后的持续
// 守护：工艺速度约束的承载是扩展版本结构，SegmentConstraint 本体零槽位）。
template <typename T, typename = void>
struct HasMemberProcessSpeed : std::false_type {};
template <typename T>
struct HasMemberProcessSpeed<T,
    std::void_t<decltype(std::declval<T&>().processSpeed)>>
    : std::true_type {};
template <typename T, typename = void>
struct HasMemberProcessSpeedTcp : std::false_type {};
template <typename T>
struct HasMemberProcessSpeedTcp<T,
    std::void_t<decltype(std::declval<T&>().processSpeedTcp)>>
    : std::true_type {};

// FK 端口最小替身（恒回 Ok＋恒等 TCP——违约族用例在校验入口即被拒绝，
// 不消费端口；完好面最小化）。
class StubFkPort final : public trj::IKinematicsComputePort {
public:
    trj::FkPortReply evaluateFk(const trj::FkPortRequest& request) override
    {
        trj::FkPortReply reply;
        reply.status = trj::KinPortCallStatus::Ok;
        reply.metrics.tcpInBase = rw::math::Transform3D<double>();
        return reply;
    }
    trj::IkPortReply solveIk(const trj::IkPortRequest&) override
    {
        trj::IkPortReply reply;
        reply.status = trj::KinPortCallStatus::PortError;
        reply.errorToken = std::string(trj::kKinPortErrorPrefix) + "stub";
        reply.errorMessage = "契约测试不消费 IK";
        return reply;
    }
};

// 时间曲线最小替身（契约校验面越过前置检查用——违约族用例在后续校验
// 点即被拒绝，sampleAt 永不被调；调用即测试缺陷显性失败。类定义零 Q
// 构造——返回语句不可达，不触发 Q 符号）。
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
// 零占位守护（§6.5——S2 启用后的持续成立）
// =====================================================================

/**
 * S2 启用后 SegmentConstraint 本体仍无工艺速度槽位（§6.5"不建工艺速度
 * 字段：SegmentConstraint 无工艺速度槽位；S2 启用时以段约束扩展版本承
 * 载"——WP-16-T15 的承载选择＝扩展版本结构 ProcessSpeedConstraint，本
 * 断言锁定本体封闭性在 S2 启用后继续成立；槽位添加即本 TU 编译失败）。
 * 同时锁定扩展版本结构自身词形（mode 两值＋速度/带字段存在——正向探测）。
 */
TEST(TrjProcessSpeedContract, SegmentConstraintExtensionVersionShape_WP16T15_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    // 负向：本体零槽位（编译期——占位添加即编译失败）。
    static_assert(!HasMemberProcessSpeed<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 processSpeed 槽位——S2 的承载是扩展版本结"
        "构 ProcessSpeedConstraint（§6.5/§19.1），本体不得扩槽位");
    static_assert(!HasMemberProcessSpeedTcp<trj::SegmentConstraint>::value,
        "SegmentConstraint 出现 processSpeedTcp 槽位——同上");

    // 正向：扩展版本结构的模式词表两值（受限/恒定——TRJ-08-S2 行原文
    // "受限或恒定"）＋模式值序锁定（词表封闭性——扩值须走需求变更）。
    static_assert(
        static_cast<std::uint8_t>(trj::ProcessSpeedMode::Limited) == 0,
        "ProcessSpeedMode 词表第一值必须为 Limited（§6.5 扩展版本结构）");
    static_assert(
        static_cast<std::uint8_t>(trj::ProcessSpeedMode::Constant) == 1,
        "ProcessSpeedMode 词表出现第三值——TRJ-08-S2 词表扩展须走需求变更"
        "＋单元卡修订，不得静默扩值");
}

/**
 * 违例码复用与码表元数据对照（TRJ-LIMIT-EXCEEDED"限制超标"语义本体的
 * 复用面——零新增码）：①码表仍 12 码（§14.4 全表零工艺速度专属码——
 * T14"码表零 jerk 码"守护同族）；②经真实 StableCodeRegistry 注册后
 * find 命中且 requiresComparison=true（违例素材比较型三要素为强制——
 * UX-03 判定面）＋severity=Error（阻断级——超限是工程违例非警告）。
 */
TEST(TrjProcessSpeedContract, ReusedLimitExceededCodeMetadata_WP16T15_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    // ①码表规模（12 码冻结——零 S2 专属码；描述符工厂为库符号）。
    const std::vector<diag::CodeDescriptor> descriptors =
        trj::trajectoryCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 12U);

    // ②真实注册表对照（FailureLocateContractTest 同款真实设施——非自
    // 持副本；seal 前注册、seal 后只读查询）。
    diag::StableCodeRegistry registry;
    trj::registerTrajectoryCodes(registry);
    registry.seal();
    const diag::CodeDescriptor* limitCode =
        registry.find(std::string(trj::kTrjLimitExceeded));
    ASSERT_NE(limitCode, nullptr)
        << "TRJ-LIMIT-EXCEEDED 未注册（码表元数据对照前提缺失）";
    // 比较型强制：工艺速度违例素材携带比较型三要素（实际/期望/单位），
    // 码描述符的 requiresComparison 必须与素材形态一致。
    EXPECT_TRUE(limitCode->requiresComparison);
    // 阻断级：超限诊断 severity=Error（§14.4 行 5——不阻断是 warning 码
    // 的事，TRJ-LIMIT-EXCEEDED 是 error）。
    EXPECT_EQ(limitCode->severity, diag::DiagnosticSeverity::Error);
}

/**
 * 比较型素材单位词形的环境面（"m/s" 在 core 单位注册表——违例素材单
 * 位承载的前提；注册表缺失属环境缺陷，本断言把该前提显性化）。
 */
TEST(TrjProcessSpeedContract, VelocityUnitTokenRegistered_WP16T15_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    const auto mps = core::UnitToken::find("m/s");
    ASSERT_TRUE(mps.has_value())
        << "core 单位注册表缺少 m/s token（环境缺陷——违例素材单位口径"
           "不可用，fail-fast 面）";
    EXPECT_EQ(std::string(mps->symbol()), "m/s");
    // 量纲＝线速度（TCP 速度是任务空间线速度——与关节域 rad/s 的量纲
    // 分界，实现 TU 的 LinearVelocity 族转写对应）。
    EXPECT_EQ(mps->kind(), core::QuantityKind::LinearVelocity);
}

// =====================================================================
// 请求违约的稳定 token 词形族（NFR-COR-03——错误二分的调用方违约面）
// =====================================================================

/**
 * 违约 token 前缀族（"trajectory/process-speed/" 稳定前缀——错误二分
 * 的机器可判别面）：曲线缺失/端口缺失/区间非法/恒定带越域/Limited 非
 * 零带五类首错各抛 TrajectoryError 且 token 前缀命中；文案中文非空。
 */
TEST(TrjProcessSpeedContract, FailFastTokenPrefixFamily_WP16T15_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    StubFkPort port;
    const auto expectPrefix = [](const trj::ProcessSpeedCheckRequest& request,
                                 const char* what) {
        try {
            (void)trj::checkProcessSpeed(request);
            FAIL() << "应拒绝: " << what;
        } catch (const trj::TrajectoryError& e) {
            EXPECT_EQ(e.token().find("trajectory/process-speed/"), 0U)
                << "token 前缀违约: " << e.token() << "（" << what << "）";
            EXPECT_FALSE(e.what() == nullptr);
        }
    };

    // 曲线缺失。
    {
        trj::ProcessSpeedCheckRequest request;
        request.fkPort = &port;
        request.totalDurationS = 1.0;
        expectPrefix(request, "timeCurve 缺失");
    }
    // 端口缺失（曲线在位＋spans 非空＋端口空——KIN-05 家族：未校验绝
    // 不冒充满足）。
    {
        trj::ProcessSpeedCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.totalDurationS = 1.0;
        request.sampleStepS = 0.01;
        request.spans = {trj::ProcessSpeedSegmentSpan{}};
        expectPrefix(request, "fkPort 缺失");
    }
    // 区间非法（逆序——曲线/端口在位才能命中区间校验点）。
    {
        trj::ProcessSpeedCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.fkPort = &port;
        request.totalDurationS = 1.0;
        request.sampleStepS = 0.01;
        trj::ProcessSpeedSegmentSpan span;
        span.segmentIndex = 0U;
        span.tStartS = 0.95;
        span.tEndS = 0.05;
        span.constraint = trj::ProcessSpeedConstraint{};  // Limited 默认
        request.spans = {span};
        expectPrefix(request, "逆序区间");
    }
    // 恒定带越域（Constant 模式带=0——强制显式 ∈(0,1)）。
    {
        trj::ProcessSpeedCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.fkPort = &port;
        request.totalDurationS = 1.0;
        request.sampleStepS = 0.01;
        trj::ProcessSpeedConstraint constraint;
        constraint.mode = trj::ProcessSpeedMode::Constant;
        constraint.tcpSpeedMps = 0.05;
        constraint.constantBandRelative = 0.0;
        trj::ProcessSpeedSegmentSpan span;
        span.tStartS = 0.05;
        span.tEndS = 0.95;
        span.constraint = constraint;
        request.spans = {span};
        expectPrefix(request, "恒定带越域");
    }
    // Limited 携带非零带（不消费面携带语义值＝违约——NFR-COR-03）。
    {
        trj::ProcessSpeedCheckRequest request;
        request.timeCurve = std::make_shared<NeverCurve>();
        request.fkPort = &port;
        request.totalDurationS = 1.0;
        request.sampleStepS = 0.01;
        trj::ProcessSpeedConstraint constraint;
        constraint.mode = trj::ProcessSpeedMode::Limited;
        constraint.tcpSpeedMps = 0.1;
        constraint.constantBandRelative = 0.05;
        trj::ProcessSpeedSegmentSpan span;
        span.tStartS = 0.05;
        span.tEndS = 0.95;
        span.constraint = constraint;
        request.spans = {span};
        expectPrefix(request, "Limited 携带非零恒定带");
    }
}

// =====================================================================
// 扩展点消费协同（acceptance 2——v3 词表判定面的消费序）
// =====================================================================

/**
 * S2 交付面经 v3 词表的协同消费序（评估器组装面协议的机器承载）：payload
 * token=v3 已知 ∧ 工艺速度项过 v3 资格判定 → 接受；v3 项冒入 v1/v2 判
 * 定面 → 拒绝（已交付 Profile 集合永不收编——§12.7）。
 */
TEST(TrjProcessSpeedContract, EvaluationDimsV3ConsumerOrder_WP16T15_ACC2)
{
    IRD_TEST_INFO("TRJ-08-S2", {}, std::nullopt);

    // 接受：v3 载荷＋v3 注册项（当前产出组合）。
    EXPECT_TRUE(
        trj::isKnownTrjPayloadKindToken(trj::kTrjPayloadKindToken)
        && trj::isTrjProfileV3ItemId(trj::kTrjItemIdProcessSpeedRecord));
    // 拒绝：v3 项对历史判定面（v1/v2 集合永不收编 S2 维度项）。
    EXPECT_FALSE(
        trj::isTrjProfileV1ItemId(trj::kTrjItemIdProcessSpeedRecord));
    EXPECT_FALSE(
        trj::isTrjProfileV2ItemId(trj::kTrjItemIdProcessSpeedRecord));
    // 拒绝：未来 S3 词形对 v3（未注册词形一律不入）。
    EXPECT_FALSE(trj::isTrjProfileV3ItemId("trj.continuous-process-boundary"));
}
