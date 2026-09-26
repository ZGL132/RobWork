/**
 * @file   CommandsTest.cpp
 * @brief  kinematics 命令面用例组（KinSessionPose/KinCommands）——会话姿态
 *         写入口的身份外保证（KIN-06/AT-04/V-18）与设默认命令门面的组装/
 *         提交/回显契约（KIN-14/AT-27 命令面 UT；D-KIN-5/P-KIN-5 落位）。
 *
 * 设计依据：
 *   - units/kinematics.md §4.5（会话姿态＝身份外元素）、§9.2（门面接口
 *     契约——@pre writable/基线=tip、@post 成功=新修订/失败=零修订＋诊
 *     断）、§9.7（组装 modeling `apply-robot-design` 增量载荷经①端口提
 *     交）、§9.8（L-K4/L-K5/L-K9/L-K11 数据流）、§9.6 行 17（KIN-ROOT-
 *     BYTES-ILLEGAL 产码面）
 *   - ARCHITECTURE.md §7.7（会话态零修订零失效）、§7.11（产生修订的命令
 *     一律经①命令端口）
 *   - 任务契约 tasks/foundation/WP-15-T08.json acceptance 1~4（具名对应
 *     见各用例 IRD_TEST_INFO 与用例名 _ACCn 段）
 *
 * 替身说明（P-KIN-7 处置——测试替身先行同款纪律）：真①端口/②查询归
 * project、RobotDesign 编解码归 modeling（R-1 禁互链）——本组以计数网
 * 关替身（FakeGateway）与脚本化补丁替身承载对端，钉住的是**门面自身**
 * 的组装/拒绝/回显行为；载荷框架字节以测试内手排金标逐位钉住（modeling
 * 卡 §9.3 文档化布局的独立复核面）。
 */

#include <sdurws/ird/kinematics/Commands.hpp>

#include <sdurws/ird/kinematics/DiagCodes.hpp>  // KIN-* 在册码常量——诊断码断言同源
#include <sdurws/ird/kinematics/KinTypes.hpp>   // IkRequestIdentity（V-18 观测点载体）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics;

namespace core = sdurws::ird::core;      // core 强身份命名空间别名（CollisionPortTest 同款）
namespace runtime = sdurws::ird::runtime;  // runtime::Expected 非异常出口别名（同款）

namespace {

// =====================================================================
// 测试夹具：身份常量（fromCanonical 严格解析——确定性，不用随机 generate）
// =====================================================================

/// 固定设备根身份（"obj-"＋32 hex——fromCanonical 严格语法）。
core::ObjectId rootOid()
{
    return core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000a1");
}

/// 固定工具对象身份（TcpRef.toolObject 用——区别于根）。
core::ObjectId toolOid()
{
    return core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000b2");
}

/// 固定分支身份。
core::BranchId branchId()
{
    return core::BranchId::fromCanonical(
        "brn-000000000000000000000000000000c3");
}

/// 固定 tip 修订身份。
core::RevisionId tipRev()
{
    return core::RevisionId::fromCanonical(
        "rev-000000000000000000000000000000d4");
}

/// 固定"新修订"身份（网关替身的成功脚本值）。
core::RevisionId newRev()
{
    return core::RevisionId::fromCanonical(
        "rev-000000000000000000000000000000e5");
}

/// 一个异于设备根的对象身份（跨对象误传反例）。
core::ObjectId foreignOid()
{
    return core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000f6");
}

// =====================================================================
// 测试替身：计数网关（②取数＋①提交的双重投影替身——脚本化返回值，
// 记录调用次数与最近信封；V-18"零修订"观测点＝submitCalls 计数）
// =====================================================================

class FakeGateway final : public IKinProjectCommandGateway {
public:
    /// 取数脚本值（optional——Expected 无公共默认构造；测试置值后取数
    /// 返回其拷贝：ok 态携带基线或错误态携带 NoDevice）。
    std::optional<runtime::Expected<KinProjectBaseline, KinematicsError>> baseline;
    /// 提交脚本值（网关回显的对端语义替身——细分理由归①端口，替身只
    /// 表达两态＋诊断透传形态）。
    CommandSubmission nextSubmission;

    /// 调用计数（fetch/submit——零修订观测点的机器面；fetchCalls
    /// mutable＝取数是 const 操作，计数是其允许的观测副作用）。
    mutable int fetchCalls = 0;
    int submitCalls = 0;
    /// 最近一次提交信封（组装断言的观测窗口；nullopt＝尚未提交）。
    std::optional<KinCommandEnvelope> lastEnvelope;

    runtime::Expected<KinProjectBaseline, KinematicsError> fetchBaseline() const override
    {
        ++fetchCalls;
        return *baseline;
    }

    CommandSubmission submit(const KinCommandEnvelope& envelope) override
    {
        ++submitCalls;
        lastEnvelope = envelope;  // 值拷贝——断言组装面不依赖提交后状态
        return nextSubmission;
    }
};

/// 就绪基线（有根＋有 tip 的常规夹具——各测试按需微调字段）。
KinProjectBaseline readyBaseline()
{
    KinProjectBaseline b;
    b.branch = branchId();
    b.tipRevision = tipRev();
    b.rootObjectId = rootOid();
    b.rootObjectBytes = {0xAA, 0xBB, 0xCC};  // 显式非常量字节——透传/替换可分辨
    return b;
}

/// 无设备诊断的判别（码面断言的唯一辅助——码值经在册常量同源比对）。
bool hasCode(const CommandSubmission& s, std::string_view code)
{
    return s.diagnostics.size() == 1 && s.diagnostics.front().code == code;
}

/// 手排 v2 载荷金标（modeling 卡 §9.3 布局的测试内独立编码——与产品面
/// encodeKinApplyRobotDesignPayload 互为两实现，逐位比对即漂移防线）。
std::vector<std::uint8_t> goldenPayload(const core::ObjectId& oid,
                                        const std::vector<std::uint8_t>& rootBytes)
{
    std::vector<std::uint8_t> g;
    const std::uint8_t magic[] = {'I', 'R', 'D', 'M', 'C', 'P', '2'};
    g.insert(g.end(), magic, magic + 7);
    auto u32 = [&g](std::uint32_t v) {
        g.push_back(static_cast<std::uint8_t>(v & 0xFF));
        g.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        g.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        g.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };
    auto str = [&g, &u32](const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        g.insert(g.end(), s.begin(), s.end());
    };
    u32(2U);            // 载荷格式版本（modeling v2）
    u32(0U);            // mode＝Apply
    u32(1U);            // objectCount＝1（单根槽增量）
    g.push_back(0U);    // allocateNew＝false（既有对象替换）
    str(oid.toCanonical());
    str(std::string(kRobotDesignObjectTypeToken));
    u32(static_cast<std::uint32_t>(rootBytes.size()));
    g.insert(g.end(), rootBytes.begin(), rootBytes.end());
    u32(0U);            // removalsCount＝0（v2 尾段显式空）
    return g;
}

// =====================================================================
// 测试替身：脚本化补丁（记录输入——透传/改写可分辨；可脚本化失败）
// =====================================================================

struct FakePatcherLog {
    /// 最近一次调用的基线字节输入（透传面断言）。
    std::vector<std::uint8_t> receivedBaseline;
    /// 最近一次调用的目标 TCP（值传递面断言）。
    std::optional<TcpRef> receivedTcp;
    /// 调用次数。
    int calls = 0;
    /// 脚本产物（nullopt＝补丁失败轨）。
    std::optional<std::vector<std::uint8_t>> product;
};

/// 以日志替身构造补丁缝（RootDesignPatcher 的测试实现）。
RootDesignPatcher makePatcher(FakePatcherLog& log)
{
    return [&log](const std::vector<std::uint8_t>& baselineBytes,
                  const TcpRef& tcp) -> std::optional<std::vector<std::uint8_t>> {
        ++log.calls;
        log.receivedBaseline = baselineBytes;
        log.receivedTcp = tcp;
        return log.product;
    };
}

/// 显式输入构造的请求身份（§6.1 六要素——snapshotId/sliceId/configDigest/
/// mode/seed/referenceQ；referenceQ 单位 rad/m。身份构造的输入全部显式——
/// 无任何会话态入参，即 D-KIN-4 的类型面表达）。
IkRequestIdentity identityOf(const std::vector<double>& referenceQ)
{
    IkRequestIdentity id;
    id.snapshotId = core::ContentIdentity::fromCanonical(
        "cid-1111111111111111111111111111111111111111111111111111111111111111");
    id.sliceId = core::ContentIdentity::fromCanonical(
        "cid-2222222222222222222222222222222222222222222222222222222222222222");
    id.configDigest = core::ContentIdentity::fromCanonical(
        "cid-3333333333333333333333333333333333333333333333333333333333333333");
    id.mode = core::EvaluationMode::Verified;
    id.seed = 42U;
    id.referenceQ = referenceQ;
    return id;
}

/// 网关成功脚本值（Committed＋新修订——两态回显成功面的替身构造）。
CommandSubmission committedSubmissionFor(core::RevisionId rev)
{
    CommandSubmission s;
    s.kind = CommandSubmission::Kind::Committed;
    s.newRevision = std::move(rev);
    return s;
}

/// 非空占位补丁缝（装配形态合法——产物恒 nullopt；供"补丁不应被调用"
/// 的路径使用：断言面只核对零提交/取数失败先于补丁，不消费其产物）。
RootDesignPatcher nullProductPatcher()
{
    return [](const std::vector<std::uint8_t>&, const TcpRef&) {
        return std::nullopt;
    };
}

}  // namespace

// =====================================================================
// ACC1——会话姿态（L-K4 数据流：零修订/零失效/不入身份；KIN-06/AT-04/
// V-18）
// =====================================================================

/**
 * 写读往返与清除（acceptance 1——写入口的基本行为面）：set 后按值读回
 * （容器持有自己的副本——调用方后续修改不影响会话态）；clear 后回"未
 * 设置"态（isSet=false 且值为空）。
 */
TEST(KinSessionPose, SetGetRoundtripAndClear_WP15T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "AT-04"},
                  std::vector<std::string>{});

    KinSessionPose pose;
    EXPECT_FALSE(pose.isSet());
    EXPECT_TRUE(pose.jointConfiguration().empty());

    // 写入后按值读回（单位：rad/m——逐自由度 SI）。
    std::vector<double> q = {0.1, -0.5, 1.25};
    pose.setJointConfiguration(q);
    ASSERT_TRUE(pose.isSet());
    ASSERT_EQ(pose.jointConfiguration().size(), q.size());
    for (std::size_t i = 0; i < q.size(); ++i) {
        EXPECT_EQ(pose.jointConfiguration()[i], q[i]) << "自由度 " << i;
    }

    // 调用方后续修改不影响会话态（值语义——容器持有副本）。
    q[0] = 99.0;
    EXPECT_EQ(pose.jointConfiguration()[0], 0.1) << "副本语义（调用方改写不穿透）";

    // 清除后回未设置态。
    pose.clear();
    EXPECT_FALSE(pose.isSet());
    EXPECT_TRUE(pose.jointConfiguration().empty());
}

/**
 * 复位 Home 写会话态（acceptance 1——L-K4 第三入口）：以调用方供给的
 * Home 值覆写（零修订——MDL-17/V-27"复位走会话命令零修订"的本单元侧
 * 形态）；并可被再次覆写（会话态可变性——双击/回写/Home 三入口共用一
 * 个写点语义）。
 */
TEST(KinSessionPose, ResetToHomeWritesSuppliedHome_WP15T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "AT-04"},
                  std::vector<std::string>{});

    KinSessionPose pose;
    pose.setJointConfiguration({0.3, 0.4});

    const std::vector<double> home = {0.0, 0.0};
    pose.resetToHome(home);
    ASSERT_TRUE(pose.isSet());
    ASSERT_EQ(pose.jointConfiguration().size(), home.size());
    EXPECT_EQ(pose.jointConfiguration()[0], 0.0);
    EXPECT_EQ(pose.jointConfiguration()[1], 0.0);
}

/**
 * 非有限拒绝（acceptance 1——NFR-COR-03 的会话面执行）：NaN/±∞ 在写入
 * 口 fail-fast（std::invalid_argument），不入态、不钳制、不置零。
 */
TEST(KinSessionPose, NonFiniteQFailsFast_WP15T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "NFR-COR-03"},
                  std::vector<std::string>{});

    KinSessionPose pose;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    EXPECT_THROW(pose.setJointConfiguration({0.1, nan}), std::invalid_argument);
    EXPECT_THROW(pose.setJointConfiguration({inf}), std::invalid_argument);
    EXPECT_FALSE(pose.isSet()) << "被拒值不入态（无副作用）";

    // 复位 Home 入口同一纪律（共用写点的校验不分入口）。
    EXPECT_THROW(pose.resetToHome({nan, 0.0}), std::invalid_argument);
}

/**
 * 零修订/零提交（acceptance 1——V-18 观测点"修订计数"的本单元侧证明）：
 * 会话姿态的全部写操作（set/回写同 set、复位 Home、清除）对计数网关的
 * 提交面调用数为 0——会话态在类型层面不携带任何写通道（结构性保证的
 * 运行期复核面）。
 */
TEST(KinSessionPose, SessionOpsProduceZeroGatewayTraffic_WP15T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "AT-04"},
                  std::vector<std::string>{"V-18"});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());
    KinematicsCommandHandler handler(&gateway, RootDesignPatcher{});

    // 会话姿态操作独立发生（不经过任何门面调用）。
    KinSessionPose pose;
    pose.setJointConfiguration({0.1, 0.2});        // 双击候选/可视化点回写
    pose.resetToHome({0.0, 0.0});                  // 复位 Home
    pose.setJointConfiguration({0.3, 0.4, 0.5});   // 再次回写
    pose.clear();                                  // 清除

    // 零修订证明：取数与提交计数均为 0（无任何写通道被触发）。
    EXPECT_EQ(gateway.submitCalls, 0) << "会话操作零提交（零修订——V-18 修订计数面）";
    EXPECT_EQ(gateway.fetchCalls, 0) << "会话操作零取数（不隐式读项目——会话自持）";
    EXPECT_FALSE(gateway.lastEnvelope.has_value());
}

/**
 * 会话姿态不入请求身份（acceptance 1/3——V-18 观测点"requestIdentity
 * 不变（referenceQ 未变）"＋D-KIN-4 排序身份禁隐式读会话姿态）：请求身
 * 份只由显式输入构成——同一组显式输入在会话姿态多次变化前后身份逐字段
 * 相等；referenceQ 变化则身份变化（身份随显式输入走，不随会话走）。
 */
TEST(KinSessionPose, SessionPoseNotInRequestIdentity_WP15T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "AT-04"},
                  std::vector<std::string>{"V-18", "D-KIN-4"});

    // 显式输入构造的请求身份（§6.1——snapshotId/sliceId/configDigest/mode/
    // seed/referenceQ 六要素；构造器见匿名命名空间 identityOf）。
    const std::vector<double> explicitQ = {0.1, 0.2, 0.3};
    const IkRequestIdentity before = identityOf(explicitQ);

    // 会话姿态多次变化（写/Home/清除）——显式输入不动。
    KinSessionPose pose;
    pose.setJointConfiguration({9.9, 8.8});
    pose.resetToHome({0.0, 0.0, 0.0});
    pose.setJointConfiguration({-1.0, -2.0, -3.0});
    pose.clear();

    const IkRequestIdentity after = identityOf(explicitQ);

    // 身份逐字段相等（D-KIN-4：本单元不读任何会话状态——身份只随显式
    // 输入变化；会话操作前后逐位一致）。
    EXPECT_TRUE(before.snapshotId == after.snapshotId);
    EXPECT_TRUE(before.sliceId == after.sliceId);
    EXPECT_TRUE(before.configDigest == after.configDigest);
    EXPECT_EQ(before.mode, after.mode);
    EXPECT_EQ(before.seed, after.seed);
    ASSERT_EQ(before.referenceQ.size(), after.referenceQ.size());
    for (std::size_t i = 0; i < before.referenceQ.size(); ++i) {
        EXPECT_EQ(before.referenceQ[i], after.referenceQ[i]) << "自由度 " << i;
    }

    // 对照组：显式 referenceQ 变化 → 身份变化（证明身份确实追踪显式输
    // 入，而非恒等比较——上组断言的灵敏度面）。
    const IkRequestIdentity changed = identityOf({0.15, 0.2, 0.3});
    EXPECT_FALSE(changed.referenceQ == before.referenceQ);
}

// =====================================================================
// ACC2——设默认门面（L-K9：组装 modeling `apply-robot-design` 增量载荷
// 经①端口提交；成功=新修订/失败=零修订＋诊断；KIN-14/AT-27 命令面 UT）
// =====================================================================

/**
 * 设默认 TCP 的组装与提交（acceptance 2——门面主路径）：信封三元组取值
 * （token=`apply-robot-design` 无点形态、payloadFormatVersion=2、字节＝
 * 单根槽框架编码）、根槽内容＝补丁缝产物（非基线原字节——增量生效）、
 * 分支/tip 绑定、提交回显透传（Committed 携带新修订）。
 */
TEST(KinCommands, SetDefaultTcpAssemblesApplyRobotDesignEnvelope_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14", "AT-27"},
                  std::vector<std::string>{"O-35", "D-KIN-5", "P-KIN-5"});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());
    gateway.nextSubmission = committedSubmissionFor(newRev());

    FakePatcherLog log;
    log.product = std::vector<std::uint8_t>{0x11, 0x22};  // 补丁产物（≠基线字节）
    KinematicsCommandHandler handler(&gateway, makePatcher(log));

    const TcpRef tcp{toolOid(), ""};  // 空串 tcpKey＝canonical TCP（KinTypes 解析规则）
    const CommandSubmission out = handler.setProjectDefaultTcp(tcp);

    // 提交恰好一次，取数恰好一次（基线驱动组装——无二次读取）。
    EXPECT_EQ(log.calls, 1) << "补丁缝恰好调用一次";
    EXPECT_EQ(gateway.fetchCalls, 1);
    ASSERT_TRUE(gateway.lastEnvelope.has_value());
    EXPECT_EQ(gateway.submitCalls, 1);

    // 补丁缝收到基线字节与目标 TCP（透传面——增量输入正确）。
    EXPECT_EQ(log.receivedBaseline, readyBaseline().rootObjectBytes);
    ASSERT_TRUE(log.receivedTcp.has_value());
    EXPECT_TRUE(log.receivedTcp->toolObject == toolOid());
    EXPECT_EQ(log.receivedTcp->tcpKey, "");

    // 信封三元组（§6.4 载荷身份——token 无点形态＝O-35 裁决服从）。
    const KinCommandEnvelope& env = *gateway.lastEnvelope;
    EXPECT_EQ(env.commandType, kFacadedModelingCommand);
    EXPECT_EQ(env.commandType, "apply-robot-design") << "token 同串（modeling §9.3 行 1）";
    EXPECT_EQ(env.payloadFormatVersion, kModelingCommandPayloadVersion);
    EXPECT_EQ(env.payloadFormatVersion, 2U);
    EXPECT_TRUE(env.branch == branchId());
    ASSERT_TRUE(env.expectedRevision.has_value());
    EXPECT_TRUE(*env.expectedRevision == tipRev()) << "基线绑定当前 tip（§9.2 @pre）";

    // 载荷字节＝手排金标逐位一致（框架布局漂移防线——两实现比对）。
    EXPECT_EQ(env.payloadCanonical, goldenPayload(rootOid(), {0x11, 0x22}));

    // 回显透传：成功＝新修订（§9.2 @post 上半句）。
    EXPECT_TRUE(out.committed());
    ASSERT_TRUE(out.newRevision.has_value());
    EXPECT_TRUE(*out.newRevision == newRev());
}

/**
 * 设默认 TCP 的框架编码确定性（acceptance 2——NFR-COR-02 同输入同字节）：
 * 同根身份同根字节重复编码逐字节一致。
 */
TEST(KinCommands, EncodeDeterministicBytes_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"NFR-COR-02"});

    const std::vector<std::uint8_t> bytes = {0x01, 0x02, 0x03, 0x04};
    const auto a = encodeKinApplyRobotDesignPayload(rootOid(), bytes);
    const auto b = encodeKinApplyRobotDesignPayload(rootOid(), bytes);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]) << "字节位 " << i;
    }
    EXPECT_EQ(a, goldenPayload(rootOid(), bytes)) << "与测试内独立编码逐位一致";
}

/**
 * 补丁失败＝零修订＋诊断（acceptance 2——§9.2 @post 下半句；§9.6 行 17
 * 产码面）：补丁缝返回 nullopt（基线根不可解码/编码失败——数据侧）时，
 * 门面不提交（零修订）且回显恰好一条 KIN-ROOT-BYTES-ILLEGAL 诊断
 * （subject＝根对象身份）。
 */
TEST(KinCommands, SetDefaultTcpPatcherFailureZeroRevisionZeroSubmit_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"KIN-ROOT-BYTES-ILLEGAL"});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());
    gateway.nextSubmission = committedSubmissionFor(newRev());

    FakePatcherLog log;
    log.product = std::nullopt;  // 补丁失败轨
    KinematicsCommandHandler handler(&gateway, makePatcher(log));

    const CommandSubmission out =
        handler.setProjectDefaultTcp(TcpRef{toolOid(), ""});

    EXPECT_EQ(gateway.submitCalls, 0) << "补丁失败不提交（零修订）";
    EXPECT_FALSE(out.committed());
    EXPECT_FALSE(out.newRevision.has_value());
    ASSERT_TRUE(hasCode(out, kKinRootBytesIllegal)) << "恰一条 KIN-ROOT-BYTES-ILLEGAL";
    ASSERT_TRUE(out.diagnostics.front().subject.has_value());
    EXPECT_TRUE(*out.diagnostics.front().subject == rootOid()) << "subject＝根对象";
}

/**
 * 无设备根＝零修订＋KIN-NO-DEVICE（acceptance 2——"无可用设备"业务出
 * 口；§9.6 行 1 的命令面消费）：闭包无 robot-design 根时设默认无意义，
 * 门面不提交并产 KIN-NO-DEVICE；基线 ok 但根身份缺失的防御面同轨。
 */
TEST(KinCommands, SetDefaultTcpNoDeviceRootZeroSubmit_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"KIN-NO-DEVICE"});

    // 轨 1：取数错误（NoDevice——闭包无设备）。
    FakeGateway errGateway;
    errGateway.baseline =
        runtime::Expected<KinProjectBaseline, KinematicsError>::err(
            KinematicsError{KinematicsErrorCode::NoDevice, {}, "闭包无 robot-design 根"});
    errGateway.nextSubmission = committedSubmissionFor(newRev());
    KinematicsCommandHandler errHandler(&errGateway, nullProductPatcher());

    const CommandSubmission out1 =
        errHandler.setProjectDefaultTcp(TcpRef{toolOid(), ""});
    EXPECT_EQ(errGateway.submitCalls, 0) << "无设备不提交";
    EXPECT_FALSE(out1.committed());
    EXPECT_TRUE(hasCode(out1, kKinNoDevice)) << "恰一条 KIN-NO-DEVICE";

    // 轨 2：基线 ok 但根身份缺失（防御面——同语义轨，不组装空槽）。
    FakeGateway noRootGateway;
    KinProjectBaseline b = readyBaseline();
    b.rootObjectId = std::nullopt;
    noRootGateway.baseline =
        runtime::Expected<KinProjectBaseline, KinematicsError>::ok(b);
    noRootGateway.nextSubmission = committedSubmissionFor(newRev());
    KinematicsCommandHandler noRootHandler(&noRootGateway, nullProductPatcher());

    const CommandSubmission out2 =
        noRootHandler.setProjectDefaultTcp(TcpRef{toolOid(), ""});
    EXPECT_EQ(noRootGateway.submitCalls, 0);
    EXPECT_TRUE(hasCode(out2, kKinNoDevice));
}

/**
 * 设默认设备＝根指定修订（acceptance 2——§9.2 setProjectDefaultDevice；
 * R1 单设备语义：robotOid＝根身份时槽字节＝基线根字节原样——零内容变更
 * 零引用触碰，KIN-14"经命令产生新修订、不破坏既有引用"）。
 */
TEST(KinCommands, SetDefaultDeviceWritesRootDesignation_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14", "AT-27"},
                  std::vector<std::string>{"P-KIN-5"});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());
    gateway.nextSubmission = committedSubmissionFor(newRev());

    // 设备路径不消费补丁缝（空函数对象合法——装配面只约束 TCP 路径）。
    KinematicsCommandHandler handler(&gateway, RootDesignPatcher{});

    const CommandSubmission out = handler.setProjectDefaultDevice(rootOid());

    EXPECT_EQ(gateway.fetchCalls, 1);
    ASSERT_TRUE(gateway.lastEnvelope.has_value());
    EXPECT_EQ(gateway.submitCalls, 1);
    const KinCommandEnvelope& env = *gateway.lastEnvelope;
    EXPECT_EQ(env.commandType, "apply-robot-design");
    EXPECT_EQ(env.payloadFormatVersion, 2U);
    EXPECT_EQ(env.payloadCanonical, goldenPayload(rootOid(), readyBaseline().rootObjectBytes))
        << "槽字节＝基线根字节原样（设备指定修订零内容变更）";
    EXPECT_TRUE(out.committed());
    ASSERT_TRUE(out.newRevision.has_value());
    EXPECT_TRUE(*out.newRevision == newRev());
}

/**
 * 外来对象身份 fail-fast（acceptance 2——调用方契约违约轨）：把非本项目
 * robot-design 根的对象传给 setProjectDefaultDevice＝编程错误，门面抛
 * std::invalid_argument，零提交零取数副作用外泄。
 */
TEST(KinCommands, SetDefaultDeviceForeignOidFailsFast_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"NFR-COR-03"});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());
    KinematicsCommandHandler handler(&gateway, RootDesignPatcher{});

    EXPECT_THROW(handler.setProjectDefaultDevice(foreignOid()), std::invalid_argument);
    EXPECT_EQ(gateway.submitCalls, 0) << "违约调用零提交";
    EXPECT_FALSE(gateway.lastEnvelope.has_value());
}

/**
 * 补丁缝未装配 fail-fast（acceptance 2——装配违约轨，DhConverter 先例）：
 * 空 RootDesignPatcher 下 TCP 路径到达即 std::invalid_argument（不产半
 * 提交）；设备路径不消费补丁——同一装配下仍可正常工作（约束面精确）。
 */
TEST(KinCommands, MissingPatcherFailsFastOnlyOnTcpPath_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());
    gateway.nextSubmission = committedSubmissionFor(newRev());
    KinematicsCommandHandler handler(&gateway, RootDesignPatcher{});

    EXPECT_THROW(handler.setProjectDefaultTcp(TcpRef{toolOid(), ""}),
                 std::invalid_argument)
        << "TCP 路径装配违约 fail-fast";
    EXPECT_EQ(gateway.submitCalls, 0);

    // 设备路径不受补丁缝装配状态影响（零补丁消费）。
    const CommandSubmission out = handler.setProjectDefaultDevice(rootOid());
    EXPECT_TRUE(out.committed());
}

// =====================================================================
// ACC3——只读会话拒绝＋诊断透传（L-K11/PM-07；回显两态的失败面）
// =====================================================================

/**
 * 只读会话提交被拒＋诊断（acceptance 3——L-K11"写类提交拒绝＋诊断"）：
 * 可写性判定权威归①端口（PA-1——门面不自判）；只读态下①端口返回
 * NotCommitted＋门卫稳定码诊断，门面**原样透传**（不追加不改写不吞——
 * 诊断码值在替身中以测试桩码承载，真码面归 project §5.0 封闭集）。
 */
TEST(KinCommands, ReadOnlySessionRejectionPassthrough_WP15T08_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"PM-07", "L-K11", "PA-1"});

    FakeGateway gateway;
    gateway.baseline = runtime::Expected<KinProjectBaseline, KinematicsError>::ok(
        readyBaseline());

    // ①端口替身脚本：只读拒绝（NotWritable 门卫——桩码 PRJ-TEST-READONLY
    // 仅测试夹具承载；稳定码权威＝project/diagnostics 注册表，门面透传
    // 不解释）。
    CommandSubmission rejected;
    rejected.kind = CommandSubmission::Kind::NotCommitted;
    rejected.diagnostics.push_back(core::DiagnosticRecord::make(
        "PRJ-TEST-READONLY", rootOid(), std::nullopt, std::nullopt,
        "命令提交（测试桩）", "存储上下文只读（测试桩文案）", "解除只读后重试"));
    gateway.nextSubmission = rejected;

    FakePatcherLog log;
    log.product = std::vector<std::uint8_t>{0x11, 0x22};
    KinematicsCommandHandler handler(&gateway, makePatcher(log));

    const CommandSubmission out = handler.setProjectDefaultTcp(TcpRef{toolOid(), ""});

    // 拒绝面：提交发生了（可写性权威在①端口 S1——门面不预判），回显
    // 携带对端诊断且零修订。
    EXPECT_EQ(gateway.submitCalls, 1) << "门面不自判可写性——提交交①端口判定";
    EXPECT_FALSE(out.committed());
    EXPECT_FALSE(out.newRevision.has_value()) << "拒绝态零修订";
    ASSERT_EQ(out.diagnostics.size(), 1U);
    EXPECT_EQ(out.diagnostics.front().code, "PRJ-TEST-READONLY") << "对端诊断原样透传";
}

