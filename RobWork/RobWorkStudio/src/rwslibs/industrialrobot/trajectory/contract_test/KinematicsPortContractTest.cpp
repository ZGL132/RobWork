/**
 * @file   KinematicsPortContractTest.cpp
 * @brief  ③端口消费契约用例组（TrjKinPortContract）——WP-16-T05
 *         acceptance 2"IK 经③端口消费（IKinematicsComputePort 透传
 *         kin.task-point-ik/kin.pose-metrics），不直链 kinematics（R-1）"
 *         的执行证明：R-1 源码扫描＋请求字段透传钉扎＋确定性解序＋端口
 *         错误传播＋取消传播。
 *
 * 设计依据：
 *   - units/trajectory.md §15.4（端口契约——透传不重解释/错误 token/
 *     确定性合法示例"同种子同配置→等价解序"/非法示例"绕过端口直链
 *     kinematics 头（R-1/R-2 违规，构建门禁拦截）"）、§3.2（R-1 业务域
 *     互链禁止）、§17.1（替身纪律——替身只证明端口/状态/错误传播）
 *   - 任务契约 tasks/foundation/WP-16-T05.json（acceptance 2/3）
 *   - 先例：BuildGraphContractTest（源码扫描契约形态）、PtpSequenceTest
 *     （黄金请求基线同构——本文件复制其笛卡尔算例基线以保证独立可读）
 *
 * 测试替身纪律（§17.1）：FakeKinPort 只证明 trajectory 消费面的端口
 * 行为——不证明真实 kinematics IK/FK 后端正确性（归 T13 黄金数据集）。
 */

#include <rw/math/RPY.hpp>                  // RPY 姿态构造（契约算例基线）

#include <sdurws/ird/trajectory/CartesianLine.hpp>
#include <sdurws/ird/trajectory/KinematicsPort.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ObjectId;
using sdurws::ird::trajectory::CartesianLinePlanResult;
using sdurws::ird::trajectory::CartesianLineRequest;
using sdurws::ird::trajectory::CartesianLineStatus;
using sdurws::ird::trajectory::FkPortMetrics;
using sdurws::ird::trajectory::FkPortReply;
using sdurws::ird::trajectory::FkPortRequest;
using sdurws::ird::trajectory::IkPortOutcome;
using sdurws::ird::trajectory::IkPortReply;
using sdurws::ird::trajectory::IkPortRequest;
using sdurws::ird::trajectory::IkPortResult;
using sdurws::ird::trajectory::IkPortSolution;
using sdurws::ird::trajectory::IKinematicsComputePort;
using sdurws::ird::trajectory::KinPortCallStatus;
using sdurws::ird::trajectory::SegmentConstraint;
using sdurws::ird::trajectory::SequenceSegmentAxis;
using sdurws::ird::trajectory::TrajectoryError;
using sdurws::ird::trajectory::kKinPortErrorPrefix;
using sdurws::ird::trajectory::planCartesianLine;

namespace {

/// 读文件全文（二进制安全——UTF-8 中文注释按字节比对）。
std::string readFileText(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集 trajectory 产品面（include/＋src/）全部 .hpp/.cpp 文件
/// （BuildGraphContractTest 同款扫描域口径——测试/契约测试/插件面除外）。
/// ★ 路径口径：IRD_TRAJECTORY_UNIT_ROOT 以 "/.." 结尾＝industrialrobot
/// 根（值内以单元名作子目录前缀——CMake 注释原文），须再拼 "trajectory"。
std::vector<fs::path> collectProductFaceFiles()
{
    std::vector<fs::path> out;
    const fs::path unitRoot = fs::path{IRD_TRAJECTORY_UNIT_ROOT} / "trajectory";
    for (const auto* sub : {"include", "src"}) {
        const fs::path base = unitRoot / sub;
        if (!fs::exists(base)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(base), end; it != end; ++it) {
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".cpp") {
                out.push_back(it->path());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// IK 端口解便利构造（黄金请求链——同分支连续解）。
IkPortSolution sol(std::initializer_list<double> q, std::uint32_t idx, double margin)
{
    IkPortSolution s;
    for (const double v : q) {
        s.q.push_back(v);
    }
    s.stableIndex = idx;
    s.minimumJointMargin = margin;
    return s;
}

/**
 * @brief IK/FK 端口假实现（记录请求＋脚本响应——透传钉扎被测面）。
 */
class FakeKinPort final : public IKinematicsComputePort {
public:
    std::vector<IkPortRequest> ikRequests;
    std::vector<FkPortRequest> fkRequests;
    std::vector<IkPortResult> ikScript;
    KinPortCallStatus ikStatus = KinPortCallStatus::Ok;
    std::string errorToken = "trajectory/kin-port-solve-ik";
    std::string errorMessage = "适配器契约不匹配（假实现注入）";

    IkPortReply solveIk(const IkPortRequest& request) override
    {
        ikRequests.push_back(request);
        IkPortReply reply;
        if (ikStatus != KinPortCallStatus::Ok) {
            reply.status = ikStatus;
            reply.errorToken = errorToken;
            reply.errorMessage = errorMessage;
            return reply;
        }
        if (ikScript.empty()) {
            reply.status = KinPortCallStatus::PortError;
            reply.errorToken = "trajectory/kin-port-script-exhausted";
            reply.errorMessage = "测试脚本耗尽";
            return reply;
        }
        reply.status = KinPortCallStatus::Ok;
        reply.result = ikScript.front();
        ikScript.erase(ikScript.begin());
        return reply;
    }

    FkPortReply evaluateFk(const FkPortRequest& request) override
    {
        fkRequests.push_back(request);
        FkPortReply reply;
        reply.status = KinPortCallStatus::Ok;
        // 单位位姿用默认构造（Rotation3D 默认＝单位——不用 identity()：
        // 该符号为框架库内定义形态，避免链接面依赖——CMake 冒烟口径注）。
        reply.metrics.tcpInBase =
            rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0),
                                          rw::math::Rotation3D<double>());
        return reply;
    }
};

/// 构造黄金契约请求（同分支连续解脚本——4 样本 ToolZ approach；端口由
/// 调用方注入——§15.4 前置非空义务）。★ 姿态用 RPY(θ,0,0)＝Rz(θ)
/// （rw RPY 首参绕 Z——实测口径，见单元测试文件头同名注）。
CartesianLineRequest makeContractRequest(FakeKinPort* port)
{
    CartesianLineRequest req;
    req.workPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.5, 0.0, 0.4),
        rw::math::RPY<double>(30.0 * 3.14159265358979323846 / 180.0, 0.0, 0.0).toRotation3D());
    req.axis = SequenceSegmentAxis::ToolZ;
    req.distanceM = 0.3;
    req.role = sdurws::ird::trajectory::CartesianLineRole::Approach;
    req.tcpRef = ObjectId::generate();
    req.sourceTaskPoint = ObjectId::generate();
    req.constraint = SegmentConstraint{};
    req.constraint.cartesianSampleStep = 0.1;
    req.segmentIndex = 0;
    req.ikContinuityThreshold = 1e-2;
    req.branchSeedQ = rw::math::Q(3);
    req.branchSeedQ[0] = 0.1;
    req.branchSeedQ[1] = 0.2;
    req.branchSeedQ[2] = 0.3;
    req.lowerBoundQ = rw::math::Q(3);
    req.upperBoundQ = rw::math::Q(3);
    for (int i = 0; i < 3; ++i) {
        req.lowerBoundQ[i] = -2.0;
        req.upperBoundQ[i] = 2.0;
    }
    req.planningSeed = 99U;
    req.kinPort = port;
    return req;
}

/// 填充 4 样本同分支连续解脚本（0.1＋0.001·i）。
void fillScript(FakeKinPort& port)
{
    for (int i = 0; i < 4; ++i) {
        IkPortResult r;
        r.outcome = IkPortOutcome::SolutionsFound;
        r.solutions.push_back(sol({0.1 + 0.001 * i, 0.2, 0.3}, 0U, 0.8));
        port.ikScript.push_back(r);
    }
}

}  // namespace

// =====================================================================
// R-1 源码扫描（acceptance 2——"不直链 kinematics（R-1）"）
// =====================================================================

/** 产品面（include/＋src/）零 kinematics include：trajectory 对 kinematics
 *  的全部消费只经本单元 IKinematicsComputePort 注入端口——直链头出现即
 *  R-1 违规（§15.4 非法示例原文"绕过端口直链 kinematics 头"）。 */
TEST(TrjKinPortContract, ProductFaceHasZeroKinematicsIncludes_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01", "ARC-02"},
                  std::vector<std::string>{});
    // 匹配形如 #include <sdurws/ird/kinematics/…>（尖括号与引号两种形态）
    // ——子串扫描（头路径含 sdurws/ird/kinematics 即越界）。
    const std::string kForbidden = "sdurws/ird/kinematics/";
    for (const auto& file : collectProductFaceFiles()) {
        const std::string text = readFileText(file);
        EXPECT_EQ(text.find(kForbidden), std::string::npos)
            << "产品面直链 kinematics 头（R-1 违规——消费只经注入端口，"
               "§15.4）: " << file.string();
    }
}

/** 端口头存在性＋接口形态自证：KinematicsPort.hpp 声明
 *  IKinematicsComputePort 抽象接口（solveIk/evaluateFk 双方法）——端口
 *  是③消费的唯一编译面。 */
TEST(TrjKinPortContract, PortHeaderDeclaresInjectedInterface_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    const fs::path portHeader =
        fs::path{IRD_TRAJECTORY_UNIT_ROOT} / "trajectory" / "include"
        / "sdurws" / "ird" / "trajectory" / "KinematicsPort.hpp";
    ASSERT_TRUE(fs::exists(portHeader)) << portHeader.string();
    const std::string text = readFileText(portHeader);
    EXPECT_NE(text.find("class IKinematicsComputePort"), std::string::npos);
    EXPECT_NE(text.find("virtual IkPortReply solveIk"), std::string::npos);
    EXPECT_NE(text.find("virtual FkPortReply evaluateFk"), std::string::npos);
}

// =====================================================================
// 端口透传与消费行为（acceptance 2——"透传 kin.task-point-ik/kin.pose-
// metrics"的请求面钉扎）
// =====================================================================

/** 请求透传钉扎：planCartesianLine 逐采样构造的端口请求——评价区间/
 *  种子逐字段等于请求投影；目标位姿随采样计划推进（首末采样端点必含）。 */
TEST(TrjKinPortContract, PlanProjectsRequestFieldsIntoPort_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillScript(port);
    CartesianLineRequest req = makeContractRequest(&port);

    const CartesianLinePlanResult out = planCartesianLine(req);
    ASSERT_EQ(out.status, CartesianLineStatus::Ok);
    ASSERT_EQ(port.ikRequests.size(), 4U);

    for (std::size_t i = 0; i < port.ikRequests.size(); ++i) {
        const IkPortRequest& r = port.ikRequests[i];
        // 限位区间投影逐轴透传（不重解释——§15.4 透传纪律）。
        ASSERT_EQ(r.lowerBoundQ.size(), 3U);
        ASSERT_EQ(r.upperBoundQ.size(), 3U);
        EXPECT_DOUBLE_EQ(r.lowerBoundQ[0], -2.0);
        EXPECT_DOUBLE_EQ(r.upperBoundQ[0], 2.0);
        // 种子透传（确定性复现要素——NFR-COR-02 端口侧）。
        EXPECT_EQ(r.seed, 99U);
        // 目标位姿有限性（构造面校验——非有限位姿不进端口）。
        for (std::size_t k = 0; k < 3; ++k) {
            EXPECT_TRUE(std::isfinite(r.targetPose.P()[k]));
        }
    }
    // 采样计划推进：末采样目标位姿＝任务点位姿（端点还原）。
    EXPECT_TRUE(port.ikRequests[3].targetPose.P() == req.workPose.P());
    EXPECT_TRUE(port.ikRequests[3].targetPose.R() == req.workPose.R());
    // 首采样目标位姿＝接近点（沿 ToolZ 回退 0.3 m——z 分量 0.4−0.3=0.1）。
    EXPECT_DOUBLE_EQ(port.ikRequests[0].targetPose.P()[2], 0.1);
}

/** 确定性解序（§15.4 合法示例"同种子同配置→等价解序"）：同输入两次
 *  规划 → 端口请求序列逐字段等价。 */
TEST(TrjKinPortContract, SameSeedSameConfigEquivalentRequestSequence_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});
    FakeKinPort portA;
    CartesianLineRequest reqA = makeContractRequest(&portA);
    fillScript(portA);
    const CartesianLinePlanResult a = planCartesianLine(reqA);
    FakeKinPort portB;
    // reqB 从 reqA 拷贝（身份字段含 tcpRef 保持同一——确定性对照要求
    // 全字段一致，仅端口指向不同实例）。
    fillScript(portB);
    CartesianLineRequest reqB = reqA;
    reqB.kinPort = &portB;
    const CartesianLinePlanResult b = planCartesianLine(reqB);

    ASSERT_EQ(portA.ikRequests.size(), portB.ikRequests.size());
    for (std::size_t i = 0; i < portA.ikRequests.size(); ++i) {
        EXPECT_TRUE(portA.ikRequests[i].targetPose.P() == portB.ikRequests[i].targetPose.P());
        EXPECT_TRUE(portA.ikRequests[i].targetPose.R() == portB.ikRequests[i].targetPose.R());
        EXPECT_EQ(portA.ikRequests[i].seed, portB.ikRequests[i].seed);
        EXPECT_EQ(portA.ikRequests[i].lowerBoundQ, portB.ikRequests[i].lowerBoundQ);
        EXPECT_EQ(portA.ikRequests[i].upperBoundQ, portB.ikRequests[i].upperBoundQ);
    }
    EXPECT_EQ(a.status, b.status);
    EXPECT_TRUE(a.segment == b.segment);
}

// =====================================================================
// 错误与取消传播（§15.4 错误行——"端口层错误 token trajectory/kin-port-*"）
// =====================================================================

/** 端口层错误传播：solveIk PortError → TrajectoryError，token 恒取端口
 *  前缀常量族（kKinPortErrorPrefix——不吞错不静默降级）。 */
TEST(TrjKinPortContract, PortErrorSurfacesWithStableTokenPrefix_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillScript(port);
    port.ikStatus = KinPortCallStatus::PortError;
    port.errorToken = std::string(kKinPortErrorPrefix) + "contract-mismatch";

    CartesianLineRequest req = makeContractRequest(&port);
    try {
        (void) planCartesianLine(req);
        FAIL() << "端口层错误应显性抛出（§15.4/§15.0 不吞错）";
    } catch (const TrajectoryError& e) {
        EXPECT_EQ(e.token(), std::string(kKinPortErrorPrefix) + "contract-mismatch");
    }
}

/** 取消传播：端口返回 Canceled（适配层取消令牌命中）→ 段规划以 Canceled
 *  收尾、零错误素材（取消不是错误——UX-03）。 */
TEST(TrjKinPortContract, PortCancelPropagatesAsNonError_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-01"}, std::vector<std::string>{});
    FakeKinPort port;
    fillScript(port);
    port.ikStatus = KinPortCallStatus::Canceled;

    const CartesianLinePlanResult out = planCartesianLine(makeContractRequest(&port));
    EXPECT_EQ(out.status, CartesianLineStatus::Canceled);
    EXPECT_FALSE(out.failure.has_value());
}

// =====================================================================
// FK 消费（kin.pose-metrics 透传面——§8.3/§8.5）
// =====================================================================

/** FK 请求透传：条件数阈值启用时逐采样以当前分支解调用 evaluateFk
 *  （kin.pose-metrics 语义——q 为权威关节向量）。 */
TEST(TrjKinPortContract, FkRequestCarriesBranchSolution_WP16T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-02"}, std::vector<std::string>{});
    FakeKinPort port;
    fillScript(port);
    CartesianLineRequest req = makeContractRequest(&port);
    req.conditionNumberWarning = 100.0;   // 启用奇异检查（FK 消费唯一目的）

    const CartesianLinePlanResult out = planCartesianLine(req);
    ASSERT_EQ(out.status, CartesianLineStatus::Ok);
    ASSERT_EQ(port.fkRequests.size(), 4U);
    // FK 请求的 q＝当采分支解（逐采样链式一致解——§8.3 跟踪分支）。
    for (std::size_t i = 0; i < port.fkRequests.size(); ++i) {
        ASSERT_EQ(port.fkRequests[i].q.size(), 3U);
        EXPECT_DOUBLE_EQ(port.fkRequests[i].q[0], 0.1 + 0.001 * static_cast<double>(i));
        EXPECT_DOUBLE_EQ(port.fkRequests[i].q[1], 0.2);
    }
}
