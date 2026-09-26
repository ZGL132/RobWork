/**
 * @file   PoseMetricsEvaluatorContractTest.cpp
 * @brief  kin.pose-metrics 评估器契约用例组（KinPoseMetricsEval）——
 *         descriptor 依赖声明/模式/无状态（§4.3 行）、评估键词形闸门
 *         （evidence 冻结面）、宿主注入工厂（O-37 裁决形态）、装配期
 *         fail-fast、结构化错误素材（KIN-NO-DEVICE/KIN-NO-TCP）与
 *         确定性（同 (snapshot,q,tcp) 同字节）。
 *
 * 设计依据：
 *   - units/kinematics.md §4.3/§9.2/§9.3（评估器形态与注册契约）、§14.3
 *     P-KIN-7/R-KIN-1（对端契约消费面以真实 evidence 冻结类型＋本地替身
 *     承载——本文件消费 evidence 已落位的 IEngineeringEvaluator/
 *     EvaluatorDescriptor/isValidEvaluationKey 冻结契约，不预建任何
 *     evidence 侧未落卡类型——O-37 裁决随附义务 c）
 *   - evidence 冻结契约（Evaluator.hpp/Slice.hpp/Envelope.hpp）与 core
 *     ContentDigester（CR-02 摘要唯一算法）
 *   - 任务契约 tasks/foundation/WP-15-T03.json acceptance 3/4
 *
 * 测试替身边界声明：NoopContext（evidence 上下文替身）与 TestView（宿主
 * 注入视图替身，见 KinFkFixture.hpp）只承载宿主语义；断言全部针对真实
 * 产品代码（PoseMetricsEvaluator/PoseMetricsEvaluatorFactory）。
 */

#include "../test/KinFkFixture.hpp"

#include <sdurws/ird/evidence/Slice.hpp>  // isValidEvaluationKey——evidence 冻结词形闸门
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;  // 对端命名空间短名（fixture 只带 core/rt）
using sdurws::ird::kinematics::FkEvaluator;
using sdurws::ird::kinematics::PoseMetricsEvaluator;
using sdurws::ird::kinematics::PoseMetricsEvaluatorFactory;
using sdurws::ird::kinematics::PoseMetricsQuery;
using sdurws::ird::kinematics::TcpRef;
using sdurws::ird::kinematics::encodePoseMetricsCanonical;
using sdurws::ird::kinematics::kPoseMetricsContractVersion;
using sdurws::ird::kinematics::kPoseMetricsEvaluationKey;
using sdurws::ird::kinematics::kPoseMetricsPayloadToken;
using sdurws::ird::kinematics::makePoseMetricsDescriptor;

namespace {

/// 查询便捷构造（canonical TCP＋给定 q）。
PoseMetricsQuery query(const std::vector<double>& q)
{
    PoseMetricsQuery query;
    query.tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    query.tcp.tcpKey.clear();
    query.q = q;
    return query;
}

/// 请求壳（评估器计算面不消费请求字段——确定性元组不含 mode；此处仅
/// 供签名传参，成员均为默认构造值）。
evidence::EvaluationRequest shellRequest(core::EvaluationMode mode
                                         = core::EvaluationMode::Verified)
{
    evidence::EvaluationRequest req;
    req.mode = mode;
    return req;
}

}  // namespace

// =====================================================================
// acceptance 4——descriptor 依赖声明（§4.3 kin.pose-metrics 行）
// =====================================================================

TEST(KinPoseMetricsEval, DescriptorMatchesSection43Row_WP15T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{});

    const evidence::EvaluatorDescriptor d = makePoseMetricsDescriptor();

    // 键与契约版本（唯一书写点常量——descriptor 与本测试同源）。
    EXPECT_EQ(d.key, std::string(kPoseMetricsEvaluationKey));
    EXPECT_EQ(d.contractVersion, kPoseMetricsContractVersion);
    EXPECT_GT(d.contractVersion, 0U) << "注册期校验前提（契约版本 >0）";

    // 依赖声明五条（§4.3 行：键/Kind/必需性逐项；无条件依赖）。
    ASSERT_EQ(d.inputs.size(), 5U);
    EXPECT_EQ(d.inputs[0].key, "model.robot-design");
    EXPECT_EQ(d.inputs[0].kind, evidence::DependencyKind::Object);
    EXPECT_EQ(d.inputs[1].key, "tcp");
    EXPECT_EQ(d.inputs[1].kind, evidence::DependencyKind::Object);
    EXPECT_EQ(d.inputs[2].key, "policy.resolved");
    EXPECT_EQ(d.inputs[2].kind, evidence::DependencyKind::Policy);
    EXPECT_EQ(d.inputs[3].key, "namemap");
    EXPECT_EQ(d.inputs[3].kind, evidence::DependencyKind::NameMap);
    EXPECT_EQ(d.inputs[4].key, "config.ik");
    EXPECT_EQ(d.inputs[4].kind, evidence::DependencyKind::Configuration);
    for (const auto& decl : d.inputs) {
        EXPECT_EQ(decl.requiredness, evidence::DependencyRequiredness::Required);
        EXPECT_FALSE(decl.applicability.has_value())
            << "Required 声明不得携带适用条件（配对纪律）";
    }

    // 模式集：§4.3 行全三值（Preview/Quick/Verified）。
    ASSERT_EQ(d.supportedModes.size(), 3U);
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Preview);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[2], core::EvaluationMode::Verified);

    // 无状态（实例可共享——§9.4）与 Profile 声明引用（kin 域；身份零值
    // ＝域不可申报，注册期由 evidence Profile 注册表计算权威值——R-3）。
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.profile.profileId, "kin");
    EXPECT_FALSE(d.profile.contentIdentity.isValid())
        << "contentIdentity 域不可申报（evidence §9.5/R-3）";
}

/// 评估键过 evidence 冻结词形闸门（kebab 实现形态——卡面点形键的随附
/// 同步，登记随卡 §14.6；DependencyKey 才允许点——evidence 两闸门差异）。
TEST(KinPoseMetricsEval, KeyWordFormGateCompliant_WP15T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-04"},
                  std::vector<std::string>{});

    EXPECT_TRUE(evidence::isValidEvaluationKey(kPoseMetricsEvaluationKey))
        << "评估键必须过 evidence 冻结闸门 [a-z][a-z0-9-]{1,63}";
}

// =====================================================================
// acceptance 4——宿主注入工厂（O-37 裁决形态）与产出契约
// =====================================================================

TEST(KinPoseMetricsEval, HostInjectionFactoryProducesEvaluator_WP15T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;

    // 宿主闭包注入：工厂构造捕获 (视图, 查询)；create() 无参签名。
    PoseMetricsEvaluatorFactory factory(&view, query({0.0, 0.0}));
    const evidence::EvaluatorDescriptor& fd = factory.descriptor();
    EXPECT_EQ(fd.key, std::string(kPoseMetricsEvaluationKey));

    auto evaluator = factory.create();
    ASSERT_TRUE(evaluator != nullptr);
    // 产出实例的 descriptor 与工厂声明同值（注册表 manifest 面一致性）。
    EXPECT_EQ(evaluator->descriptor().key, fd.key);
    EXPECT_EQ(evaluator->descriptor().contractVersion, fd.contractVersion);
    EXPECT_EQ(evaluator->descriptor().inputs, fd.inputs);
    EXPECT_EQ(evaluator->descriptor().supportedModes, fd.supportedModes);
    EXPECT_EQ(evaluator->descriptor().stateless, fd.stateless);

    // 评估成功：payload token/canonical 字节/摘要三面齐全。
    auto out = evaluator->evaluate(shellRequest(), context);
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_EQ(out.payload->kindToken, std::string(kPoseMetricsPayloadToken));
    EXPECT_TRUE(out.diagnostics.empty());

    // canonical 字节＝纯函数服务结果的直接编码（单一计算实现点）。
    FkEvaluator service;
    auto direct = service.evaluate(view, query({0.0, 0.0}).tcp, query({0.0, 0.0}).q);
    ASSERT_TRUE(direct.ok());
    EXPECT_EQ(out.payload->canonicalBytes, encodePoseMetricsCanonical(direct.get()));

    // 摘要＝对 canonicalBytes 的 SHA-256（CR-02：core ContentDigester
    // 唯一算法——重算比对作为完整性凭据）。
    core::ContentDigester digester;
    digester.update(out.payload->canonicalBytes.data(),
                    out.payload->canonicalBytes.size());
    EXPECT_EQ(out.payload->digest.bytes, digester.finalize());
}

/// 装配期 fail-fast（std::invalid_argument——调用方错误轨，NFR-COR-03）：
/// 空视图/q 维度不符/q 非有限/tcpKey 不命中。
TEST(KinPoseMetricsEval, AssemblyFailFastRejectsIllegalQuery_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 空视图。
    EXPECT_THROW(PoseMetricsEvaluatorFactory(nullptr, query({0.0, 0.0})),
                 std::invalid_argument);
    // q 维度不符（2 自由度模型传 3 分量）。
    EXPECT_THROW(PoseMetricsEvaluatorFactory(&view, query({0.1, 0.2, 0.3})),
                 std::invalid_argument);
    // q 非有限（NaN 不置零）。
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(PoseMetricsEvaluatorFactory(&view, query({0.1, nan})),
                 std::invalid_argument);
    // tcpKey 不命中 canonical TCP 身份（帧未解析——装配期可判即提前）。
    PoseMetricsQuery badKey = query({0.0, 0.0});
    badKey.tcp.tcpKey = "no_such_tcp";
    EXPECT_THROW(PoseMetricsEvaluatorFactory(&view, badKey), std::invalid_argument);
}

// =====================================================================
// acceptance 3——评估期结构化错误素材（KIN-NO-DEVICE/KIN-NO-TCP）
// =====================================================================

TEST(KinPoseMetricsEval, StructuredErrorMaterialNoTcp_WP15T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"},
                  std::vector<std::string>{"AT-03"});

    // "未配置"分支：无工具模型 → KIN-NO-TCP error 级诊断素材。
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.9, 2.9));
    const rt::CanonicalModel noToolModel = makeModel(joints, trans(0, 0, 0),
                                                     trans(0, 0, 0), false);
    TestView view(noToolModel);
    NoopContext context;
    PoseMetricsEvaluatorFactory factory(&view, query({0.0}));
    auto evaluator = factory.create();
    auto out = evaluator->evaluate(shellRequest(), context);

    EXPECT_FALSE(out.payload.has_value()) << "错误面不产正式载荷";
    EXPECT_TRUE(out.evidence.empty());
    ASSERT_EQ(out.diagnostics.size(), 1U);
    EXPECT_EQ(out.diagnostics[0].code, std::string("KIN-NO-TCP"));
    // 结构化素材携带指名工具身份（subject——不伪造 localName/runtimeName）。
    ASSERT_TRUE(out.diagnostics[0].subject.has_value());
    EXPECT_EQ(*out.diagnostics[0].subject, query({0.0}).tcp.toolObject);

    // "悬空"分支：指名工具不在闭包 → 同码（映射纪律唯一出口）。
    PoseMetricsQuery dangling = query({0.0});
    dangling.tcp.toolObject = idFrom<core::ObjectId>("ghost-tool");
    PoseMetricsEvaluatorFactory factory2(&view, dangling);
    auto out2 = factory2.create()->evaluate(shellRequest(), context);
    ASSERT_EQ(out2.diagnostics.size(), 1U);
    EXPECT_EQ(out2.diagnostics[0].code, std::string("KIN-NO-TCP"));
}

// =====================================================================
// acceptance 4——确定性（同 (snapshot,q,tcp) 同字节；mode 不在元组）
// =====================================================================

TEST(KinPoseMetricsEval, DeterministicBytesAcrossCallsAndModes_WP15T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{"AT-03"});

    const rt::CanonicalModel model = sixAxisModel();
    TestView view(model);
    NoopContext context;
    PoseMetricsEvaluatorFactory factory(&view, query({0.3, -0.5, 0.8, 0.2, -0.4, 1.0}));
    auto evaluator = factory.create();

    // 同实例两次评估：字节一致（无跨调用状态——stateless 面）。
    auto out1 = evaluator->evaluate(shellRequest(), context);
    auto out2 = evaluator->evaluate(shellRequest(), context);
    ASSERT_TRUE(out1.payload.has_value());
    ASSERT_TRUE(out2.payload.has_value());
    EXPECT_EQ(out1.payload->canonicalBytes, out2.payload->canonicalBytes);
    EXPECT_EQ(out1.payload->digest, out2.payload->digest);

    // 新实例（factory.create 再次产出）同输入：字节一致。
    auto evaluator2 = factory.create();
    auto out3 = evaluator2->evaluate(shellRequest(), context);
    EXPECT_EQ(out1.payload->canonicalBytes, out3.payload->canonicalBytes);

    // 模式不在确定性元组：Preview 与 Verified 同字节（效力门禁在汇总/
    // 包络层——EVI-01；计算面不因模式改变）。
    auto outPreview = evaluator->evaluate(shellRequest(core::EvaluationMode::Preview),
                                          context);
    EXPECT_EQ(out1.payload->canonicalBytes, outPreview.payload->canonicalBytes);
}
