/**
 * @file   PlaybackExportTest.cpp
 * @brief  WP-16-T10 运行期测试（TRJ-07 领域实现）——动画播放查表（黄金
 *         对照 ITimeCurve/越界拒用/段归投影/驻留帧/确定性/payload 校验）＋
 *         曲线只读投影（三通道黄金/非有限拒用）＋路点局部重规划（组合等
 *         价性/平滑不适用跳检标注/**复检强制边真实会话链路**〔Passed 与
 *         DataInsufficient→回退原路径——KIN-05"不得视为无碰撞"的编排面承
 *         接〕/驻留保留/取消零素材/前置违约族）＋标准轨迹导出（假写出器黄
 *         金布局/六列形态/真实文件字节级确定性＋读回往返/未确认覆盖拒绝/
 *         确认覆盖替换/取消保留既有输出/写出失败 io token 传播/前置违约
 *         族）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1/§14.5.2（三会话域面＋导出）、§16.2（零修
 *     订与零计算红线——动画/曲线只读归档 payload；导出不产生修订〔结构
 *     性：本套件零项目类型消费〕）、§15.13（导出字段字典与失败语义）、
 *     §11.1/§11.2（局部重规划的强制边/范围口径/回退——R-TRJ-3）、§15.8
 *     （请求面字段口径）、§17.2（AT-04 观测性行——零修订断言经结构面＋
 *     契约测试 CommandsBoundaryContractTest 双面承载）
 *   - 需求 TRJ-07（动画/曲线/局部重规划/标准导出）、AT-04（零修订）、
 *     KIN-05（验证器缺失不视为无碰撞——复检 DataInsufficient→回退）、
 *     NFR-COR-01（黄金对照独立书写——查表对 ITimeCurve 的采样误差界）、
 *     NFR-COR-02（确定性——字节级/位级断言）
 *   - 装置先例：RecheckTest.cpp（真实 WorkCell＋手工装配 policy::Collision-
 *     Scene＋真实会话；测试文件自持装置不跨文件共享——同款拷贝精简到
 *     1DOF；safetyClearance=0.01 的 V-20 注入途径同源）
 *   - 任务契约 tasks/foundation/WP-16-T10.json（acceptance 1/2）
 *
 * 本套件为**集成模式专属**（消费 rw::math::Q/policy 会话/真实文件 io——
 * PtpSequenceTest 同款 gating；冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>   // core::ContentDigester（种子身份构造——RecheckTest 同款）
#include <sdurws/ird/io/Csv.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/Export.hpp>
#include <sdurws/ird/trajectory/Playback.hpp>
#include <sdurws/ird/trajectory/Recheck.hpp>
#include <sdurws/ird/trajectory/Smooth.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/core/Ptr.hpp>
#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace io = sdurws::ird::io;
namespace policy = sdurws::ird::policy;
namespace core = sdurws::ird::core;
namespace trj = sdurws::ird::trajectory;

namespace {

// =====================================================================
// 手工 payload 构造（导出黄金算例——直构 TimeParameterization，样本值即
// 期望值，零推导）
// =====================================================================

/// 两关节样本便捷构造（q0/q1 权威角；qd/qdd 逐分量给值——rad、rad/s、rad/s²）。
trj::TimedSample sample2(double t, double q0, double q1, double qd0, double qd1,
                         double a0, double a1)
{
    trj::TimedSample s;
    s.t = t;
    s.q = rw::math::Q(2, q0, q1);
    s.qd = rw::math::Q(2, qd0, qd1);
    s.qdd = rw::math::Q(2, a0, a1);
    return s;
}

/// 导出黄金 payload：3 样本（t=0/0.5/1.0），中点为驻留形态（qd=qdd 全零）。
trj::TimeParameterization goldenPayload()
{
    trj::TimeParameterization tp;
    tp.motionLawToken = trj::kTimeParamMethodQuinticSplineC2;
    tp.totalDurationS = 1.0;
    tp.samples = {
        sample2(0.0, 0.0, 0.2, 0.0, 0.0, 0.0, 0.0),
        sample2(0.5, 0.5, 0.6, 0.0, 0.0, 0.0, 0.0),   // 驻留形态行（qd=qdd=0 显式）
        sample2(1.0, 1.0, 1.0, 0.25, -0.5, 2.0, -1.0),
    };
    return tp;
}

/// 导出身份块（非空最小集——黄金断言逐字符对照）。
trj::TrajectoryExportIdentity goldenIdentity()
{
    trj::TrajectoryExportIdentity id;
    id.snapshotId = "cid-golden-snapshot";
    id.sliceId = "cid-golden-slice";
    id.policyContentIdentity = "cid-golden-policy";
    id.algorithmVersion = trj::kTrjAlgorithmVersion;
    id.planningSeed = 42;
    id.robworkBaselineVersion = "rw-baseline-golden";
    return id;
}

/// 基础导出请求（映射列开——黄金布局断言面；确认位缺省 true——假写出器
/// 用例不受目标存在性预检干扰，真实文件用例按用例语义覆盖）。
trj::TrajectoryExportRequest goldenRequest(const trj::TimeParameterization* payload)
{
    trj::TrajectoryExportRequest req;
    req.targetPath = "unused-path.csv";
    req.overwriteConfirmed = true;
    req.payload = payload;
    req.jointTypes = {trj::TrajectoryExportJointType::Revolute,
                      trj::TrajectoryExportJointType::Prismatic};
    req.segmentDurationsS = {0.4, 0.2, 0.4};   // Σ=1.0==total（§12.4 自洽）
    req.segmentAttribution = {
        {0, std::string("id-golden-station-a")},
        {1, std::string()},
        {2, std::string("id-golden-station-b")},
    };
    req.identity = goldenIdentity();
    return req;
}

// =====================================================================
// 假写出器（T10 导出编排的受控替身——替身只承载行捕获与失败注入位，
// 转义/原子性由真实 ICsvWriter 承担〔另有真实文件用例〕）。
// 生命周期：捕获状态置共享块（shared_ptr）——导出函数以 unique_ptr 持有
// 替身实例并在返回时析构（io 会话 RAII 语义），测试断言面只读共享块，
// 避免"实例已析构再经裸指针取状态"的释放后使用。
// =====================================================================

struct FakeCsvState {
    /// 第 N 次 writeRow 失败（1 基；0＝不注入失败——失败注入位）。
    int failOnRowCall = 0;
    /// 失败注入的稳定码（IoErrorCode::FormatInternal——转译断言锚）。
    io::IoErrorCode failureCode = io::IoErrorCode::FormatInternal;

    std::vector<io::IoString> headerNames;
    std::vector<std::vector<io::CsvCell>> rows;   // writeRow 原文（未转义）
    int rowCalls = 0;
    bool finishCalled = false;
    bool openCalled = false;
    io::CsvWriteOptions openedOptions;
};

class FakeCsvWriter final : public io::ICsvWriter {
public:
    explicit FakeCsvWriter(std::shared_ptr<FakeCsvState> state)
        : m_state(std::move(state))
    {
    }

    io::IoResult<void> open(io::CsvOutputTarget&& target,
                            const io::CsvWriteOptions& options) override
    {
        m_state->openCalled = true;
        m_state->openedOptions = options;
        return {};
    }
    io::IoResult<void> writeHeader(const std::vector<io::IoString>& names) override
    {
        m_state->headerNames = names;
        return {};
    }
    io::IoResult<void> writeRow(const std::vector<io::CsvCell>& cells) override
    {
        ++m_state->rowCalls;
        if (m_state->failOnRowCall > 0
            && m_state->rowCalls == m_state->failOnRowCall) {
            io::IoResult<void> r;
            r.error.code = m_state->failureCode;
            r.error.detail = "fake writer injected failure";
            return r;
        }
        m_state->rows.push_back(cells);
        return {};
    }
    io::IoResult<void> finish() override
    {
        m_state->finishCalled = true;
        return {};
    }

private:
    std::shared_ptr<FakeCsvState> m_state;   // 捕获状态（测试断言面共享）
};

/// 取 Text 单元格原文（kind 断言失败时显性失败——防误读）。
std::string textOf(const io::CsvCell& cell, const char* what)
{
    EXPECT_EQ(cell.kind, io::CsvCell::Kind::Text) << what;
    return cell.text;
}
double realOf(const io::CsvCell& cell, const char* what)
{
    EXPECT_EQ(cell.kind, io::CsvCell::Kind::Real) << what;
    return cell.real;
}
std::int64_t intOf(const io::CsvCell& cell, const char* what)
{
    EXPECT_EQ(cell.kind, io::CsvCell::Kind::Int) << what;
    return cell.integer;
}

// =====================================================================
// 装置（1DOF——RecheckTest 同源拷贝精简；真实 WorkCell＋真实 policy 会话）
// =====================================================================

inline constexpr double kBoxSize = 0.2;   // 立方体全边长（m）
inline constexpr double kArmLength = 0.5; // 臂段长（m，X 向）
inline constexpr double kBlockedAngle = 2.0;  // 障碍方位角（rad）

/// 手工构造的非全零对象身份（种子摘要前 16 字节——RecheckTest 同款）。
template <typename Id>
Id seededObjectId(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize();
    Id id;
    std::copy(digest.begin(), digest.begin()
                + static_cast<std::ptrdiff_t>(id.bytes.size()),
              id.bytes.begin());
    return id;
}

/// 手工构造的非全零内容身份（场景/名称映射身份——CR-04 值传递契约）。
core::ContentIdentity taggedContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}

class RigNameContext final : public policy::IPolicyNameContext {
public:
    std::map<std::string, core::ObjectId> byName;
    std::map<core::ObjectId, std::string> byId;
    core::ContentIdentity mapIdentity;

    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override
    {
        const auto it = byName.find(runtimeName);
        return it == byName.end() ? std::nullopt : std::optional<core::ObjectId>(it->second);
    }
    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        const auto it = byId.find(object);
        return it == byId.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    core::ContentIdentity nameMapContentIdentity() const override { return mapIdentity; }
};

class RigValidationContext final : public policy::IPolicyValidationContext {
public:
    std::map<core::ObjectId, bool> existingObjects;
    std::map<core::ObjectId, std::string> roles;

    bool objectExists(core::ObjectId object) const override
    {
        const auto it = existingObjects.find(object);
        return it != existingObjects.end() && it->second;
    }
    std::optional<std::string> objectRole(core::ObjectId object) const override
    {
        const auto it = roles.find(object);
        return it == roles.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool groupDefined(std::string_view) const override { return false; }
};

struct TrajRig {
    core::ObjectId device;
    core::ObjectId toolObject;
    core::ObjectId envObject;
    std::shared_ptr<rw::models::WorkCell> workcell;
    std::shared_ptr<const rw::models::WorkCell> constWorkcell;
    RigNameContext names;
    RigValidationContext validation;
    std::shared_ptr<const policy::EngineeringPolicySet> policySet;
    std::shared_ptr<const policy::CollisionEvaluationSession> session;
};

/// 挂 0.2 m 立方体碰撞几何到帧（RecheckTest 同款）。
void attachBox(const std::shared_ptr<rw::models::WorkCell>& workcell,
               const rw::kinematics::Frame::Ptr& frame)
{
    rw::core::Ptr<rw::models::RigidObject> object =
        rw::core::ownedPtr(new rw::models::RigidObject(frame));
    object->addGeometry(rw::core::ownedPtr(
        new rw::geometry::Geometry(rw::core::ownedPtr(
            new rw::geometry::Box(kBoxSize, kBoxSize, kBoxSize)))));
    workcell->add(object);
}

/// 经七段解析管线发布启用碰撞的策略（safetyClearance 参数化——V-20 注入
/// 途径同源：间距>0 触发距离查询，内置二值后端无距离能力→evaluate Failed
/// →复检不可采信→DataInsufficient）。
policy::EngineeringPolicySet publishEnabledPolicy(const RigValidationContext& validation,
                                                  const core::ObjectId& policyObject,
                                                  double safetyClearance)
{
    policy::RawPolicyInput in;
    in.schemaVersion = policy::PolicySchema::currentVersion;
    in.policyObject = policyObject;
    in.collision.enabled = true;
    in.collision.enabledDomains = {policy::CollisionDomain::Self,
                                   policy::CollisionDomain::Environment,
                                   policy::CollisionDomain::Tool};
    in.collision.safetyClearance = policy::RawThresholdInput{safetyClearance, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    in.jointThresholds.nearLimitRatio = policy::RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = policy::RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit =
        policy::RawThresholdInput{12.566370614359172, "rad"};
    const policy::PolicyParseResult res = policy::resolvePolicy(in, validation);
    if (!res.policy.has_value()) {
        throw std::runtime_error("夹具策略应可发布，首条诊断: "
                                 + (res.diagnostics.empty()
                                        ? std::string{"-"}
                                        : res.diagnostics.front().code));
    }
    return *res.policy;
}

/// 构造真实会话（场景装配→唯一实现入口三步构建——RecheckTest 同款）。
void buildSession(TrajRig& rig, double safetyClearance)
{
    const std::string deviceName = "Robot";
    const std::string toolFrameName = "Tool";
    const std::string envFrameName = "EnvBox";
    rig.names.mapIdentity = taggedContentIdentity(0x7E);
    rig.names.byName[deviceName] = rig.device;
    rig.names.byName[toolFrameName] = rig.toolObject;
    rig.names.byName[envFrameName] = rig.envObject;
    rig.names.byId[rig.device] = deviceName;
    rig.names.byId[rig.toolObject] = toolFrameName;
    rig.names.byId[rig.envObject] = envFrameName;
    rig.validation.existingObjects[rig.toolObject] = true;
    rig.validation.roles[rig.toolObject] = "Tool";
    rig.validation.existingObjects[rig.envObject] = true;
    rig.validation.roles[rig.envObject] = "EnvironmentObject";

    rig.policySet = std::make_shared<const policy::EngineeringPolicySet>(
        publishEnabledPolicy(rig.validation,
                             seededObjectId<core::ObjectId>("trj-t10-policy"),
                             safetyClearance));

    policy::CollisionScene scene;
    scene.workcell = rig.constWorkcell;
    scene.primaryDevice = rig.device;
    policy::SceneObjectEntry toolEntry;
    toolEntry.objectId = rig.toolObject;
    toolEntry.localName = toolFrameName;
    toolEntry.role = policy::SceneObjectRole::Tool;
    toolEntry.hasCollisionGeometry = true;
    policy::SceneObjectEntry envEntry;
    envEntry.objectId = rig.envObject;
    envEntry.localName = envFrameName;
    envEntry.role = policy::SceneObjectRole::EnvironmentObject;
    envEntry.hasCollisionGeometry = true;
    scene.objects = {toolEntry, envEntry};
    scene.sceneContentIdentity = taggedContentIdentity(0xA7);

    const std::unique_ptr<policy::ICollisionEvaluator> evaluator =
        policy::makeRobWorkCollisionEvaluator();
    rig.session = evaluator->createSession(*rig.policySet, scene, rig.names);
    if (rig.session == nullptr) {
        throw std::runtime_error("夹具会话应构建成功（装置良构前提）");
    }
}

/// 构造单自由度装置（1DOF：Tool 位置 (L·cos q, L·sin q)；环境盒在方位角
/// kBlockedAngle 同半径处——q∈kBlockedAngle 邻域必碰；RecheckTest 同款）。
TrajRig makeOneDofRig(double safetyClearance)
{
    TrajRig rig;
    rig.device = seededObjectId<core::ObjectId>("trj-t10-robot-1dof");
    rig.toolObject = seededObjectId<core::ObjectId>("trj-t10-tool-1dof");
    rig.envObject = seededObjectId<core::ObjectId>("trj-t10-env-1dof");

    rig.workcell = std::make_shared<rw::models::WorkCell>("TrjT10Rig1Dof");
    rw::core::Ptr<rw::kinematics::FixedFrame> base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Base", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(base);
    rw::core::Ptr<rw::models::RevoluteJoint> j1 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J1", rw::math::Transform3D<>::identity()));
    j1->setBounds(rw::math::Q(1, -3.5), rw::math::Q(1, 3.5));  // 单位 rad
    rig.workcell->addFrame(j1, base);
    rw::core::Ptr<rw::kinematics::FixedFrame> tool = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Tool", rw::math::Transform3D<>(
                                                  rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(tool, j1);
    rw::core::Ptr<rw::kinematics::FixedFrame> envFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "EnvBox", rw::math::Transform3D<>(
                          rw::math::Vector3D<>(kArmLength * std::cos(kBlockedAngle),
                                               kArmLength * std::sin(kBlockedAngle), 0.0))));
    rig.workcell->addFrame(envFrame);
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));
    attachBox(rig.workcell, tool);
    attachBox(rig.workcell, envFrame);

    rig.constWorkcell = rig.workcell;
    buildSession(rig, safetyClearance);
    return rig;
}

/// 1DOF 装置解析采样器：代表点集＝{Tool 位置}（RecheckTest 同款——R9 笛
/// 卡尔步长的度量对象）。
class OneDofToolSampler final : public trj::IRepresentPointSampler {
public:
    std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q& q) const override
    {
        return {rw::math::Vector3D<double>(kArmLength * std::cos(q[0]),
                                           kArmLength * std::sin(q[0]), 0.0)};
    }
};

// =====================================================================
// 局部重规划请求构造辅助
// =====================================================================

/// 宽松限值视图（rad/s、rad/s²——时间化不触发缩放，聚焦编排语义）。
trj::JointDynamicLimits looseLimits()
{
    trj::JointDynamicLimits lim;
    lim.velocityLimit = 10.0;
    lim.accelerationLimit = 100.0;
    return lim;
}

/// 单关节折线便捷（权威角 rad）。
std::vector<rw::math::Q> jointPath1(const std::vector<double>& waypoints)
{
    std::vector<rw::math::Q> points;
    points.reserve(waypoints.size());
    for (double q : waypoints) {
        points.emplace_back(1, q);
    }
    return points;
}

/// 局部重规划基础请求骨架（单段三点、中点必经——平滑保留三结点做样条
/// 重拟合；适用性/会话由用例覆盖）。
trj::LocalReplanRequest baseReplanRequest()
{
    trj::LocalReplanRequest req;
    req.frozenIdentity.planningSeed = 7;
    req.frozenIdentity.snapshotId = core::ContentIdentity{};
    req.firstSegmentIndex = 0;
    req.lastSegmentIndex = 0;
    trj::LocalReplanSegmentInput seg;
    seg.segmentIndex = 0;
    seg.waypoints = jointPath1({-0.5, 1.2, 0.5});  // 峰形路点（远离障碍区 2.0±0.4）
    seg.mandatoryWaypointIndices = {1};            // 中点必经——保留三结点
    seg.collisionCheckApplicable = false;
    req.segments = {seg};
    req.jointTypes = {trj::JointTypeKind::Revolute};
    req.lowerBoundQ = rw::math::Q(1, -3.5);
    req.upperBoundQ = rw::math::Q(1, 3.5);
    req.jointLimits = {looseLimits()};
    req.limitsScaleFactor = 1.0;
    req.smoothToleranceJoint = 0.05;   // rad（几何保持容差——黄金算例声明）
    req.smoothToleranceTcp = 0.01;     // m
    req.smoothMaxIterations = 4;
    req.recheckParams = trj::makeP06FrozenRecheckParameters();
    req.motionLawToken = trj::kTimeParamMethodQuinticSplineC2;
    req.timeParamSampleStepS = 0.005;  // s（黄金算例声明）
    req.timeParamMaxIterations = 8;
    return req;
}

/// 唯一临时目录（真实文件导出用例——进程内静态计数防碰撞）。
fs::path makeTempDir(const char* tag)
{
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path()
                       / ("ird-t10-" + std::string(tag) + "-"
                          + std::to_string(static_cast<long long>(::time(nullptr)))
                          + "-" + std::to_string(counter++));
    fs::create_directories(dir);
    return dir;
}

/// 读文件全部字节（字节级确定性/保留断言面）。
std::vector<char> readBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::vector<char>{std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>()};
}

/// 断言抛 TrajectoryError 且 token 精确匹配（前置违约族断言面）。
template <typename Fn>
void expectTrajectoryError(Fn&& fn, const std::string& tokenSuffix)
{
    try {
        fn();
        FAIL() << "应抛 TrajectoryError（token: trajectory/" << tokenSuffix << "）";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/" + tokenSuffix) << e.what();
    }
}

}  // namespace

// =====================================================================
// 动画播放查表（§14.5.1 play/playback-seek 数据面）
// =====================================================================

/**
 * 用例：真实 T08 产物上的查表黄金对照——采样时刻命中位级样本（零插值
 * 漂移）；区间中点线性插值对 ITimeCurve 样条真值的偏差落在采样误差界内
 * （线性插值误差 ≤ max|qd|·采样步长——NFR-COR-01 独立对照：查表面与样
 * 条连续面同源不同视图）。
 */
TEST(PlaybackGolden, SeekAgainstTimeParamPayload)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    // 真实 T08 时间参数化产物（静止-静止单段；黄金算例同源配置）。
    trj::TimeParamRequest request;
    request.limitsScaleFactor = 1.0;
    request.motionLawToken = trj::kTimeParamMethodQuinticSplineC2;
    request.sampleStepS = 0.005;   // s
    request.maxIterations = 8;
    trj::TimeParamSegment seg;
    seg.segmentIndex = 0;
    std::vector<rw::math::Q> points;
    points.emplace_back(1, 0.0);
    points.emplace_back(1, 0.1);
    seg.geometry = trj::makeLinearJointPathGeometry(points);
    request.segments = {seg};
    trj::JointDynamicLimits lim;
    lim.velocityLimit = 1.0;      // rad/s
    lim.accelerationLimit = 10.0; // rad/s²
    request.jointLimits = {lim};
    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_TRUE(result.timeParam.has_value());
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_FALSE(result.timeParam->samples.empty());

    // 查表构造（带段归投影——TimeParamResult.segmentDurationsS 直传）。
    const trj::TrajectoryPlaybackTable table(*result.timeParam,
                                             result.segmentDurationsS);
    ASSERT_EQ(table.totalDurationS(), result.timeParam->totalDurationS);

    // 逐样本命中：位级还原（atSample=true，q/qd/qdd 与样本位级相等——查
    // 表零插值漂移的确定性回放锚）。
    for (const trj::TimedSample& s : result.timeParam->samples) {
        const trj::PlaybackFrame frame = table.sampleAt(s.t);
        EXPECT_TRUE(frame.atSample) << "t=" << s.t;
        EXPECT_EQ(frame.segmentIndex.has_value(), true);
        EXPECT_EQ(frame.segmentIndex.value(), 0u);
        ASSERT_EQ(frame.q.size(), 1u);
        EXPECT_EQ(frame.q[0], s.q[0]) << "t=" << s.t;
        EXPECT_EQ(frame.qd[0], s.qd[0]) << "t=" << s.t;
        EXPECT_EQ(frame.qdd[0], s.qdd[0]) << "t=" << s.t;
    }

    // 区间中点对照：查表（相邻样本线性插值）vs ITimeCurve（样条真值）。
    // 偏差上界＝相邻样本间 |qd| 峰值×采样步长（线性插值误差的一阶界——
    // 独立书写的采样误差界，不与实现共享插值代码）。
    const double total = result.timeParam->totalDurationS;
    const std::size_t n = result.timeParam->samples.size();
    for (std::size_t i = 1; i < n; ++i) {
        const trj::TimedSample& a = result.timeParam->samples[i - 1];
        const trj::TimedSample& b = result.timeParam->samples[i];
        const double mid = 0.5 * (a.t + b.t);
        const trj::PlaybackFrame frame = table.sampleAt(mid);
        EXPECT_FALSE(frame.atSample);
        const double bound = std::max(std::abs(a.qd[0]), std::abs(b.qd[0]))
                           * (b.t - a.t);
        const double truth = result.timeCurve->sampleAt(mid).q[0];
        EXPECT_LE(std::abs(frame.q[0] - truth), bound)
            << "mid=" << mid << " 偏差超采样误差界";
    }
}

/**
 * 用例：段归投影与驻留帧——两运动段＋一驻留段的 payload 上，各段内时刻
 * 的 segmentIndex 与段时长半开区间一致；驻留段内帧 qd/qdd 全零（§9.1 驻
 * 留＝零速零加速度常值段）；t==total 落末段。
 */
TEST(PlaybackGolden, SegmentAttributionAndDwellFrame)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    // 三段请求：运动（0→0.1）＋驻留 0.8 s＋运动（0.1→0.2）——端点位级相
    // 等（链连续）。
    trj::TimeParamRequest request;
    request.limitsScaleFactor = 1.0;
    request.motionLawToken = trj::kTimeParamMethodQuinticSplineC2;
    request.sampleStepS = 0.01;
    request.maxIterations = 8;
    trj::TimeParamSegment move0;
    move0.segmentIndex = 0;
    move0.geometry = trj::makeLinearJointPathGeometry(jointPath1({0.0, 0.1}));
    trj::TimeParamSegment dwell;
    dwell.segmentIndex = 1;
    dwell.dwellDurationS = 0.8;    // s（工况 Dwell 事件）
    trj::TimeParamSegment move1;
    move1.segmentIndex = 2;
    move1.geometry = trj::makeLinearJointPathGeometry(jointPath1({0.1, 0.2}));
    request.segments = {move0, dwell, move1};
    trj::JointDynamicLimits lim;
    lim.velocityLimit = 1.0;
    lim.accelerationLimit = 10.0;
    request.jointLimits = {lim};
    const trj::TimeParamResult result = trj::timeParameterize(request);
    ASSERT_EQ(result.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(result.timeParam.has_value());
    ASSERT_EQ(result.segmentDurationsS.size(), 3u);

    const trj::TrajectoryPlaybackTable table(*result.timeParam,
                                             result.segmentDurationsS);
    // 段 0 内时刻（总时长前 10% 内——运动段 0 的样条远短于驻留）。
    const double d0 = result.segmentDurationsS[0];
    const trj::PlaybackFrame f0 = table.sampleAt(0.5 * d0);
    EXPECT_EQ(f0.segmentIndex.value(), 0u);
    // 段 1（驻留）内时刻：qd/qdd 全零（常值段——§9.1）。
    const double tDwell = result.segmentDurationsS[0]
                        + 0.5 * result.segmentDurationsS[1];
    const trj::PlaybackFrame fd = table.sampleAt(tDwell);
    EXPECT_EQ(fd.segmentIndex.value(), 1u);
    EXPECT_EQ(fd.qd[0], 0.0);
    EXPECT_EQ(fd.qdd[0], 0.0);
    EXPECT_EQ(fd.q[0], 0.1);   // 驻留保持进入构型（位级）
    // 段 2 内时刻。
    const double t2 = result.segmentDurationsS[0] + result.segmentDurationsS[1]
                    + 0.5 * result.segmentDurationsS[2];
    const trj::PlaybackFrame f2 = table.sampleAt(t2);
    EXPECT_EQ(f2.segmentIndex.value(), 2u);
    // t==total 落末段（半开区间右端闭合到末段——定位键契约）。
    const trj::PlaybackFrame fEnd = table.sampleAt(result.timeParam->totalDurationS);
    EXPECT_EQ(fEnd.segmentIndex.value(), 2u);
}

/**
 * 用例：越界/非有限查询拒用（NaN、负值、超 total、+inf）——fail-fast
 * token "trajectory/playback/t-out-of-range"（NFR-COR-03 不钳制不静默；
 * UI 进度条应在调用前钳位）。
 */
TEST(PlaybackGolden, SeekOutOfRangeRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    const trj::TrajectoryPlaybackTable table(tp, {});
    expectTrajectoryError([&] { table.sampleAt(-1e-12); },
                          "playback/t-out-of-range");
    expectTrajectoryError([&] { table.sampleAt(1.0000001); },
                          "playback/t-out-of-range");
    expectTrajectoryError(
        [&] {
            table.sampleAt(std::numeric_limits<double>::quiet_NaN());
        },
        "playback/t-out-of-range");
    expectTrajectoryError(
        [&] { table.sampleAt(std::numeric_limits<double>::infinity()); },
        "playback/t-out-of-range");
}

/**
 * 用例：payload 完整性校验族（空样本/时刻回退/缺端点样本/维度漂移/非有
 * 限值/段归投影失配）——各自 fail-fast token 精确（归档数据损坏属调用方
 * 数据契约违约）。
 */
TEST(PlaybackGolden, PayloadValidationFamily)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    // 空样本。
    trj::TimeParameterization empty;
    empty.totalDurationS = 0.0;
    expectTrajectoryError([&] { trj::TrajectoryPlaybackTable table(empty, {}); },
                          "curve/payload-empty");

    // 时刻回退。
    trj::TimeParameterization backward = goldenPayload();
    std::swap(backward.samples[0], backward.samples[2]);
    expectTrajectoryError([&] { trj::TrajectoryPlaybackTable table(backward, {}); },
                          "payload-time-order");

    // 缺端点样本（末样本 t≠total）。
    trj::TimeParameterization shortEnd = goldenPayload();
    shortEnd.samples.pop_back();
    expectTrajectoryError([&] { trj::TrajectoryPlaybackTable table(shortEnd, {}); },
                          "payload-missing-end");

    // 维度漂移。
    trj::TimeParameterization ragged = goldenPayload();
    ragged.samples[1].q = rw::math::Q(1, 0.5);
    expectTrajectoryError([&] { trj::TrajectoryPlaybackTable table(ragged, {}); },
                          "payload-dim");

    // 非有限分量。
    trj::TimeParameterization nanSample = goldenPayload();
    nanSample.samples[2].qd = rw::math::Q(
        2, std::numeric_limits<double>::quiet_NaN(), 0.0);
    expectTrajectoryError([&] { trj::TrajectoryPlaybackTable table(nanSample, {}); },
                          "payload-non-finite");

    // 段归投影：Σ≠total（不同源混装拒绝——§12.4 自洽）。
    const trj::TimeParameterization tp = goldenPayload();
    expectTrajectoryError(
        [&] { trj::TrajectoryPlaybackTable table(tp, {0.5, 0.6}); },
        "payload-segment-sum");
    expectTrajectoryError([&] { trj::TrajectoryPlaybackTable table(tp, {0.5, -0.1}); },
                          "payload-segment-duration");
}

/**
 * 用例：同查询位级同帧（查表二分与插值全确定——NFR-COR-02）。
 */
TEST(PlaybackGolden, SeekDeterminismBitwise)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    const trj::TrajectoryPlaybackTable table(tp, {0.4, 0.2, 0.4});
    for (const double t : {0.0, 0.13, 0.5, 0.71, 1.0}) {
        const trj::PlaybackFrame a = table.sampleAt(t);
        const trj::PlaybackFrame b = table.sampleAt(t);
        EXPECT_EQ(a.q[0], b.q[0]) << "t=" << t;
        EXPECT_EQ(a.q[1], b.q[1]) << "t=" << t;
        EXPECT_EQ(a.qd[0], b.qd[0]) << "t=" << t;
        EXPECT_EQ(a.atSample, b.atSample) << "t=" << t;
        EXPECT_EQ(a.segmentIndex, b.segmentIndex) << "t=" << t;
    }
}

// =====================================================================
// 曲线只读投影（§14.5.1 show-curve 数据面）
// =====================================================================

/**
 * 用例：三通道投影黄金——逐关节序列与样本逐一对应（非重采样）；spot 值
 * 位级相等；关节升序；同请求两次投影逐点相等（确定性）。
 */
TEST(CurveProjection, ThreeChannelsGoldenAndDeterminism)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    const std::vector<trj::TrajectoryCurveSeries> pos =
        trj::projectTrajectoryCurves(tp, trj::TrajectoryCurveChannel::Position);
    const std::vector<trj::TrajectoryCurveSeries> vel =
        trj::projectTrajectoryCurves(tp, trj::TrajectoryCurveChannel::Velocity);
    const std::vector<trj::TrajectoryCurveSeries> acc =
        trj::projectTrajectoryCurves(tp, trj::TrajectoryCurveChannel::Acceleration);

    ASSERT_EQ(pos.size(), 2u);
    ASSERT_EQ(vel.size(), 2u);
    ASSERT_EQ(acc.size(), 2u);
    for (std::size_t j = 0; j < 2; ++j) {
        EXPECT_EQ(pos[j].jointIndex, j);
        ASSERT_EQ(pos[j].timesS.size(), tp.samples.size());
        ASSERT_EQ(pos[j].values.size(), tp.samples.size());
        // spot 对照：三样本位级相等（位置/速度/加速度三通道各自取列）。
        for (std::size_t i = 0; i < tp.samples.size(); ++i) {
            EXPECT_EQ(pos[j].timesS[i], tp.samples[i].t);
            EXPECT_EQ(pos[j].values[i], tp.samples[i].q[static_cast<int>(j)]);
            EXPECT_EQ(vel[j].values[i], tp.samples[i].qd[static_cast<int>(j)]);
            EXPECT_EQ(acc[j].values[i], tp.samples[i].qdd[static_cast<int>(j)]);
        }
    }
    // 确定性：同请求逐点相等（NFR-COR-02）。
    const std::vector<trj::TrajectoryCurveSeries> posAgain =
        trj::projectTrajectoryCurves(tp, trj::TrajectoryCurveChannel::Position);
    ASSERT_EQ(posAgain.size(), pos.size());
    for (std::size_t j = 0; j < pos.size(); ++j) {
        EXPECT_EQ(posAgain[j].values, pos[j].values);
        EXPECT_EQ(posAgain[j].timesS, pos[j].timesS);
    }
}

/**
 * 用例：非有限 payload 拒投影（fail-fast——非有限静默进入曲线会沿投影
 * 链污染呈现与导出，NFR-COR-03）。
 */
TEST(CurveProjection, RejectsNonFinitePayload)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    trj::TimeParameterization bad = goldenPayload();
    bad.samples[1].q = rw::math::Q(
        2, std::numeric_limits<double>::infinity(), 0.6);
    expectTrajectoryError(
        [&] {
            trj::projectTrajectoryCurves(
                bad, trj::TrajectoryCurveChannel::Position);
        },
        "payload-non-finite");
}

// =====================================================================
// 路点局部重规划（§14.5.1 local-replan——worker 编排）
// =====================================================================

/**
 * 用例：组合等价性（NoChange 路径）——全必经内部点使平滑零变化，编排退
 * 化为"折线几何＋时间化"直连；结果与手工 smooth→makeLinearJointPath-
 * Geometry→timeParameterize 同配置产物逐样本位级相等（编排零新算法的
 * 结构性证明——NFR-COR-02）；frozenIdentity/区间回显如实。
 */
TEST(LocalReplan, CompositionEquivalenceOnZeroChangePath)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    trj::LocalReplanRequest req = baseReplanRequest();
    // 两点路径（无内部点）→ 平滑静态零变化分支（§11.1——无可平滑对象，
    // Smooth.cpp"simplified.size()<=2"静态 NoChange；共线路点的实测偏差
    // 带浮点噪声不保证位级零，故选静态分支保证确定性）。
    req.segments[0].waypoints = jointPath1({0.0, 0.1});
    req.segments[0].mandatoryWaypointIndices.clear();

    const trj::LocalReplanOutcome outcome = trj::localReplan(req);
    ASSERT_EQ(outcome.status, trj::LocalReplanStatus::Ok);
    ASSERT_EQ(outcome.segmentReports.size(), 1u);
    EXPECT_EQ(outcome.segmentReports[0].smoothStatus, trj::SmoothStatus::NoChange);
    EXPECT_FALSE(outcome.segmentReports[0].geometryChanged);
    EXPECT_TRUE(outcome.segmentReports[0].recheckSkipped);
    EXPECT_EQ(outcome.segmentReports[0].recheckSkipReason, "no-geometry-change");
    EXPECT_FALSE(outcome.anySmoothFallback);
    EXPECT_EQ(outcome.frozenIdentity.planningSeed, req.frozenIdentity.planningSeed);
    EXPECT_EQ(outcome.rangeFirstSegmentIndex, 0u);
    EXPECT_EQ(outcome.rangeLastSegmentIndex, 0u);
    ASSERT_EQ(outcome.rangeTimeParam.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(outcome.rangeTimeParam.timeParam.has_value());

    // 手工直连管线（同配置）——逐样本位级对照（组合等价性）。
    trj::TimeParamRequest direct;
    direct.limitsScaleFactor = req.limitsScaleFactor;
    direct.motionLawToken = req.motionLawToken;
    direct.sampleStepS = req.timeParamSampleStepS;
    direct.maxIterations = req.timeParamMaxIterations;
    trj::TimeParamSegment dseg;
    dseg.segmentIndex = 0;
    dseg.geometry = trj::makeLinearJointPathGeometry(
        jointPath1({0.0, 0.1}));
    direct.segments = {dseg};
    direct.jointLimits = req.jointLimits;
    const trj::TimeParamResult directResult = trj::timeParameterize(direct);
    ASSERT_EQ(directResult.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(directResult.timeParam.has_value());
    ASSERT_EQ(outcome.rangeTimeParam.timeParam->samples.size(),
              directResult.timeParam->samples.size());
    for (std::size_t i = 0; i < directResult.timeParam->samples.size(); ++i) {
        const trj::TimedSample& a = outcome.rangeTimeParam.timeParam->samples[i];
        const trj::TimedSample& b = directResult.timeParam->samples[i];
        EXPECT_EQ(a.t, b.t) << "i=" << i;
        EXPECT_EQ(a.q[0], b.q[0]) << "i=" << i;
        EXPECT_EQ(a.qd[0], b.qd[0]) << "i=" << i;
        EXPECT_EQ(a.qdd[0], b.qdd[0]) << "i=" << i;
    }
}

/**
 * 用例：平滑成功且无碰撞义务段——Smoothed（几何变化）但适用性 false→
 * 跳过复检并显式标注 "collision-not-applicable"（§11.2 范围口径——不冒
 * 充已复检）；时间化产物 Ok。
 */
TEST(LocalReplan, SmoothedSkipsRecheckWhenNotApplicable)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    trj::LocalReplanRequest req = baseReplanRequest();  // 峰形三点、中点必经
    const trj::LocalReplanOutcome outcome = trj::localReplan(req);
    ASSERT_EQ(outcome.status, trj::LocalReplanStatus::Ok);
    ASSERT_EQ(outcome.segmentReports.size(), 1u);
    EXPECT_EQ(outcome.segmentReports[0].smoothStatus, trj::SmoothStatus::Smoothed);
    EXPECT_TRUE(outcome.segmentReports[0].geometryChanged);
    EXPECT_FALSE(outcome.segmentReports[0].recheckExecuted);
    EXPECT_TRUE(outcome.segmentReports[0].recheckSkipped);
    EXPECT_EQ(outcome.segmentReports[0].recheckSkipReason,
              "collision-not-applicable");
    EXPECT_FALSE(outcome.anySmoothFallback);
    EXPECT_EQ(outcome.rangeTimeParam.status, trj::TimeParamStatus::Ok);
}

/**
 * 用例：复检强制边真实会话链路（Passed）——1DOF 真实装置＋真实 policy 会
 * 话〔内置 ProximityStrategyRW，safetyClearance=0〕；Smoothed 必触发复检
 * （§11.1 强制边），全样本无碰撞→Passed→平滑产物被采纳（无回退）；时间
 * 化 Ok。替代身纪律：碰撞判定全部由真实后端承担（§17.3 同源）。
 */
TEST(LocalReplan, SmoothedTriggersRecheckPassedWithRealSession)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07", "TRJ-04"},
                  std::vector<std::string>{"AT-04", "AT-06"});

    TrajRig rig = makeOneDofRig(0.0);   // 二值碰撞（无间距查询）
    OneDofToolSampler sampler;

    trj::LocalReplanRequest req = baseReplanRequest();
    req.segments[0].collisionCheckApplicable = true;
    req.collisionSession = rig.session;
    req.representPointSampler = &sampler;

    const trj::LocalReplanOutcome outcome = trj::localReplan(req);
    ASSERT_EQ(outcome.status, trj::LocalReplanStatus::Ok);
    ASSERT_EQ(outcome.segmentReports.size(), 1u);
    EXPECT_EQ(outcome.segmentReports[0].smoothStatus, trj::SmoothStatus::Smoothed);
    ASSERT_TRUE(outcome.segmentReports[0].recheckExecuted);
    ASSERT_TRUE(outcome.segmentReports[0].recheck.has_value());
    EXPECT_EQ(outcome.segmentReports[0].recheck->status, trj::RecheckStatus::Completed);
    EXPECT_EQ(outcome.segmentReports[0].recheck->conclusion,
              trj::RecheckConclusion::Passed);
    EXPECT_FALSE(outcome.segmentReports[0].recheck->budget.budgetExhausted);
    EXPECT_FALSE(outcome.segmentReports[0].smoothRejectedFallback);
    EXPECT_FALSE(outcome.anySmoothFallback);
    EXPECT_EQ(outcome.rangeTimeParam.status, trj::TimeParamStatus::Ok);
}

/**
 * 用例：复检不可采信→回退原路径（KIN-05"不得视为无碰撞"的编排面承接）
 * ——safetyClearance=0.01 真实会话触发距离查询→内置二值后端无距离能力
 * →evaluate Failed→复检 DataInsufficient（EvidenceUnavailable，V-20 同源
 * 注入）→该平滑结果被淘汰、回退原折线（R-TRJ-3），时间化在回退几何上 Ok；
 * 淘汰事实经 smoothRejectedFallback＋anySmoothFallback 如实披露（不静默
 * 放行——§11.1 分支二）。
 */
TEST(LocalReplan, RecheckDataInsufficientFallsBackToOriginalPath)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07", "TRJ-04", "KIN-05"},
                  std::vector<std::string>{"AT-04", "AT-06"});

    TrajRig rig = makeOneDofRig(0.01);  // 间距>0→距离查询→后端能力缺失
    OneDofToolSampler sampler;

    trj::LocalReplanRequest req = baseReplanRequest();
    req.segments[0].collisionCheckApplicable = true;
    req.collisionSession = rig.session;
    req.representPointSampler = &sampler;

    const trj::LocalReplanOutcome outcome = trj::localReplan(req);
    ASSERT_EQ(outcome.status, trj::LocalReplanStatus::Ok);
    ASSERT_EQ(outcome.segmentReports.size(), 1u);
    EXPECT_EQ(outcome.segmentReports[0].smoothStatus, trj::SmoothStatus::Smoothed);
    ASSERT_TRUE(outcome.segmentReports[0].recheckExecuted);
    ASSERT_TRUE(outcome.segmentReports[0].recheck.has_value());
    EXPECT_EQ(outcome.segmentReports[0].recheck->conclusion,
              trj::RecheckConclusion::DataInsufficient);
    EXPECT_EQ(outcome.segmentReports[0].recheck->dataInsufficientReason,
              trj::RecheckDataInsufficientReason::EvidenceUnavailable);
    // 回退事实：平滑产物淘汰、原折线回位（未平滑候选路径保留——R-TRJ-3）。
    EXPECT_TRUE(outcome.segmentReports[0].smoothRejectedFallback);
    EXPECT_TRUE(outcome.anySmoothFallback);
    EXPECT_FALSE(outcome.segmentReports[0].geometryChanged);
    // 回退几何上时间化 Ok（产物＝原路径——可对照手工直连验证）。
    ASSERT_EQ(outcome.rangeTimeParam.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(outcome.rangeTimeParam.timeParam.has_value());
    // 采样中点构型应落在原折线包络内（[−0.5, 1.2]——平滑产物淘汰后不再
    // 出现样条圆角；取 t=中点核对回退几何真的回位）。
    const double total = outcome.rangeTimeParam.timeParam->totalDurationS;
    const trj::TimedSample mid =
        outcome.rangeTimeParam.timeCurve->sampleAt(0.5 * total);
    EXPECT_GE(mid.q[0], -0.5 - 1e-12);
    EXPECT_LE(mid.q[0], 1.2 + 1e-12);
}

/**
 * 用例：驻留段保留与标注——三段区间（运动＋驻留＋运动）的编排保留驻留
 * 条目（segmentDurationsS 三条目对齐、驻留时长原值不缩放——§12.3/§12.4）；
 * 驻留报告 skip 标注 "dwell-segment"。
 */
TEST(LocalReplan, DwellPreservedAndReported)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    trj::LocalReplanRequest req = baseReplanRequest();
    req.lastSegmentIndex = 2;
    trj::LocalReplanSegmentInput move1;
    move1.segmentIndex = 0;
    move1.waypoints = jointPath1({0.0, 0.1});
    move1.mandatoryWaypointIndices = {};
    trj::LocalReplanSegmentInput dwell;
    dwell.segmentIndex = 1;
    dwell.dwellDurationS = 0.8;    // s
    trj::LocalReplanSegmentInput move2;
    move2.segmentIndex = 2;
    move2.waypoints = jointPath1({0.1, 0.2});
    req.segments = {move1, dwell, move2};

    const trj::LocalReplanOutcome outcome = trj::localReplan(req);
    ASSERT_EQ(outcome.status, trj::LocalReplanStatus::Ok);
    ASSERT_EQ(outcome.segmentReports.size(), 3u);
    EXPECT_TRUE(outcome.segmentReports[1].recheckSkipped);
    EXPECT_EQ(outcome.segmentReports[1].recheckSkipReason, "dwell-segment");
    ASSERT_EQ(outcome.rangeTimeParam.status, trj::TimeParamStatus::Ok);
    ASSERT_TRUE(outcome.rangeTimeParam.timeParam.has_value());
    ASSERT_EQ(outcome.rangeTimeParam.segmentDurationsS.size(), 3u);
    EXPECT_EQ(outcome.rangeTimeParam.segmentDurationsS[1], 0.8);  // 原值不缩放
}

/**
 * 用例：取消零素材——立即取消→Canceled、零报告零产物（UX-03 取消非错
 * 误）；identity/区间回显保持。
 */
TEST(LocalReplan, CancelYieldsZeroMaterial)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    trj::LocalReplanRequest req = baseReplanRequest();
    req.cancel = [] { return true; };
    const trj::LocalReplanOutcome outcome = trj::localReplan(req);
    EXPECT_EQ(outcome.status, trj::LocalReplanStatus::Canceled);
    EXPECT_TRUE(outcome.segmentReports.empty());
    EXPECT_FALSE(outcome.rangeTimeParam.timeParam.has_value());
    EXPECT_TRUE(outcome.rangeTimeParam.failureRecords.empty());
    EXPECT_FALSE(outcome.anySmoothFallback);
}

/**
 * 用例：前置违约族（十七连——区间/段表/形态/维度/保护集/配置/协议可执
 * 行/链连续性）——各 token 精确（确定性首错序，NFR-COR-02/COR-03）。
 */
TEST(LocalReplan, ValidationFamily)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    // 空区间。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.lastSegmentIndex = 0;
        req.firstSegmentIndex = 1;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/range-empty");
    }
    // 段表条目数与区间不一致。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.lastSegmentIndex = 1;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/range-mismatch");
    }
    // segmentIndex 与区间错位。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].segmentIndex = 3;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/segment-order");
    }
    // 驻留段携带路点。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].dwellDurationS = 0.5;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/dwell-shape");
    }
    // 运动段路点不足。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].waypoints = jointPath1({0.0});
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/waypoints");
    }
    // 维度漂移。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].waypoints[1] = rw::math::Q(2, 1.2, 0.0);
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/dim");
    }
    // 保护集含端点。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].mandatoryWaypointIndices = {0};
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/mandatory-endpoint");
    }
    // 保护集越界。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].mandatoryWaypointIndices = {9};
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/mandatory-oob");
    }
    // 保护集乱序。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        trj::LocalReplanSegmentInput seg = req.segments[0];
        seg.waypoints = jointPath1({0.0, 0.05, 0.1, 0.15});
        seg.mandatoryWaypointIndices = {2, 1};
        req.segments = {seg};
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/mandatory-order");
    }
    // 维度投影不一致。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.jointLimits.push_back(looseLimits());
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/projection-dim");
    }
    // 限位区间倒置。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.upperBoundQ = rw::math::Q(1, -4.0);
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/bounds");
    }
    // 缩放因子越界。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.limitsScaleFactor = 1.5;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/scale");
    }
    // 容差非法。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.smoothToleranceJoint = 0.0;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/tolerance");
    }
    // 运动律词表外。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.motionLawToken = "cubic-spline-c1";
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/motion-law");
    }
    // 采样步长非法。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.timeParamSampleStepS = 0.0;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/sample-step");
    }
    // 协议可执行前置（适用段但会话缺席）。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.segments[0].collisionCheckApplicable = true;
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/recheck-unavailable");
    }
    // 链连续性（两运动段端点位级不等）。
    {
        trj::LocalReplanRequest req = baseReplanRequest();
        req.lastSegmentIndex = 1;
        trj::LocalReplanSegmentInput a;
        a.segmentIndex = 0;
        a.waypoints = jointPath1({0.0, 0.1});
        trj::LocalReplanSegmentInput b;
        b.segmentIndex = 1;
        b.waypoints = jointPath1({0.2, 0.3});   // 0.1 ≠ 0.2——链断裂
        req.segments = {a, b};
        expectTrajectoryError([&] { trj::localReplan(req); },
                              "local-replan/chain-break");
    }
}

// =====================================================================
// 标准轨迹导出（§15.13）
// =====================================================================

/**
 * 用例：假写出器黄金布局（映射列开）——行序＝方言标记（真实现写入，假
 * 替身不落）→魔数→身份块六行（逐字符黄金）→表头八列→数据长表（样本
 * 优先、关节次之）；逐单元格 kind/值黄金（含驻留行 qd=qdd 显式零、段归
 * 列半开区间、joint_type 词表）；outcome 计数（identityLineCount=7、
 * dataRowCount=样本×关节）。
 */
TEST(StandardExport, GoldenLayoutWithMappingColumnsViaFakeWriter)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    trj::TrajectoryExportRequest req = goldenRequest(&tp);

    const auto state = std::make_shared<FakeCsvState>();
    trj::TrajectoryExportIoPorts ports;
    ports.csvWriterFactory = [state]() -> std::unique_ptr<io::ICsvWriter> {
        return std::make_unique<FakeCsvWriter>(state);
    };

    const trj::TrajectoryExportOutcome outcome =
        trj::exportStandardTrajectory(req, ports);
    ASSERT_EQ(outcome.status, trj::TrajectoryExportOutcome::Status::Ok);
    EXPECT_TRUE(outcome.errorToken.empty());
    EXPECT_EQ(outcome.identityLineCount, 7u);
    EXPECT_EQ(outcome.dataRowCount, 6u);   // 3 样本×2 关节
    EXPECT_TRUE(state->finishCalled);
    EXPECT_TRUE(state->openCalled);
    EXPECT_TRUE(state->openedOptions.emitDialectMarker);  // 方言标识行自写

    // 行 1：魔数（单 Text 单元格）。
    ASSERT_EQ(state->rows.size(), 1u + 6u + 6u);  // 魔数＋身份块＋数据
    EXPECT_EQ(textOf(state->rows[0][0], "magic"), "#ird-trajectory-export v1");

    // 行 2..7：身份块（key=value 黄金逐字符——读回解析锚）。
    EXPECT_EQ(textOf(state->rows[1][0], "snapshot"),
              "# snapshotId=cid-golden-snapshot");
    EXPECT_EQ(textOf(state->rows[2][0], "slice"), "# sliceId=cid-golden-slice");
    EXPECT_EQ(textOf(state->rows[3][0], "policy"),
              "# policyContentIdentity=cid-golden-policy");
    EXPECT_EQ(textOf(state->rows[4][0], "alg"),
              "# algorithmVersion=trj-alg-1");
    EXPECT_EQ(textOf(state->rows[5][0], "seed"), "# planningSeed=42");
    EXPECT_EQ(textOf(state->rows[6][0], "baseline"),
              "# robworkBaselineVersion=rw-baseline-golden");

    // 行 8..13：数据长表（样本优先、关节次之）。
    const std::vector<trj::TimedSample>& s = tp.samples;
    const char* jointTokens[2] = {"revolute", "prismatic"};
    std::size_t row = 7;
    for (std::size_t i = 0; i < s.size(); ++i) {
        // 段归半开区间 [cum_i, cum_{i+1})，t==total 落末段。
        std::size_t seg = 2;
        if (s[i].t < 0.4) { seg = 0; }
        else if (s[i].t < 0.6) { seg = 1; }
        for (std::size_t j = 0; j < 2; ++j) {
            const std::vector<io::CsvCell>& cells = state->rows[row++];
            ASSERT_EQ(cells.size(), 8u);
            EXPECT_EQ(realOf(cells[0], "t"), s[i].t);
            EXPECT_EQ(intOf(cells[1], "joint"), static_cast<std::int64_t>(j));
            EXPECT_EQ(realOf(cells[2], "q"), s[i].q[static_cast<int>(j)]);
            EXPECT_EQ(realOf(cells[3], "qd"), s[i].qd[static_cast<int>(j)]);
            EXPECT_EQ(realOf(cells[4], "qdd"), s[i].qdd[static_cast<int>(j)]);
            EXPECT_EQ(textOf(cells[5], "joint_type"), jointTokens[j]);
            // segment_index 为 Int 单元格（导出直写整数——文本化在 io 层）。
            EXPECT_EQ(intOf(cells[6], "segment_index"),
                      static_cast<std::int64_t>(seg));
            EXPECT_EQ(textOf(cells[7], "source_task_point"),
                      req.segmentAttribution[seg].sourceTaskPoint);
        }
    }
    // 驻留形态行显式：t=0.5 的四行 qd/qdd 单元格为零值实数（§15.13"驻留
    // 行显式 qd=qdd=0"——不删行）。
    EXPECT_EQ(realOf(state->rows[9][3], "dwell qd"), 0.0);
    EXPECT_EQ(realOf(state->rows[10][3], "dwell qd"), 0.0);
    EXPECT_EQ(realOf(state->rows[9][4], "dwell qdd"), 0.0);
    EXPECT_EQ(realOf(state->rows[10][4], "dwell qdd"), 0.0);

    // 表头（writeHeader 八列序——字段字典）。
    ASSERT_EQ(state->headerNames.size(), 8u);
    EXPECT_EQ(state->headerNames[0], "t_s");
    EXPECT_EQ(state->headerNames[1], "joint_index");
    EXPECT_EQ(state->headerNames[2], "q");
    EXPECT_EQ(state->headerNames[3], "qd");
    EXPECT_EQ(state->headerNames[4], "qdd");
    EXPECT_EQ(state->headerNames[5], "joint_type");
    EXPECT_EQ(state->headerNames[6], "segment_index");
    EXPECT_EQ(state->headerNames[7], "source_task_point");
}

/**
 * 用例：六列形态（段映射列关）——segmentDurationsS/segmentAttribution 同
 * 空→表头六列、数据行六格（投影开关成对契约的关半区）。
 */
TEST(StandardExport, SixColumnModeWithoutMapping)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    trj::TrajectoryExportRequest req = goldenRequest(&tp);
    req.segmentDurationsS.clear();
    req.segmentAttribution.clear();

    const auto state = std::make_shared<FakeCsvState>();
    trj::TrajectoryExportIoPorts ports;
    ports.csvWriterFactory = [state]() -> std::unique_ptr<io::ICsvWriter> {
        return std::make_unique<FakeCsvWriter>(state);
    };
    const trj::TrajectoryExportOutcome outcome =
        trj::exportStandardTrajectory(req, ports);
    ASSERT_EQ(outcome.status, trj::TrajectoryExportOutcome::Status::Ok);
    ASSERT_EQ(state->headerNames.size(), 6u);
    ASSERT_EQ(state->rows.size(), 1u + 6u + 6u);
    for (std::size_t i = 7; i < state->rows.size(); ++i) {
        ASSERT_EQ(state->rows[i].size(), 6u) << "row " << i;
    }
}

/**
 * 用例：真实文件确定性＋读回往返（缺省通道 makeCsvWriter）——同请求两次
 * 导出字节级相同（io canonical：NFR-COR-02）；经 ICsvReader 读回：头部行
 * 与表头/数据行逐字段还原（headerRow=9 定位——文件布局契约）。
 */
TEST(StandardExport, RealFileDeterministicRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    const fs::path dir = makeTempDir("roundtrip");
    trj::TrajectoryExportRequest req = goldenRequest(&tp);

    const fs::path fileA = dir / "trj-a.csv";
    const fs::path fileB = dir / "trj-b.csv";
    trj::TrajectoryExportRequest reqA = req;
    reqA.targetPath = fileA;
    trj::TrajectoryExportRequest reqB = req;
    reqB.targetPath = fileB;
    ASSERT_EQ(trj::exportStandardTrajectory(reqA).status,
              trj::TrajectoryExportOutcome::Status::Ok);
    ASSERT_EQ(trj::exportStandardTrajectory(reqB).status,
              trj::TrajectoryExportOutcome::Status::Ok);

    // 字节级确定性。
    const std::vector<char> bytesA = readBytes(fileA);
    const std::vector<char> bytesB = readBytes(fileB);
    ASSERT_FALSE(bytesA.empty());
    EXPECT_EQ(bytesA, bytesB) << "同请求两次导出应字节级相同（io canonical）";

    // 读回往返：headerRow=9（行 1 方言标记＋行 2 魔数＋行 3..8 身份块＋
    // 行 9 表头）；表头前单字段行照常交付（读侧语义——hasHeader 前无
    // arity 约束），据此断言头部块原文。
    auto reader = io::makeCsvReader();
    io::CsvReadOptions options;
    options.headerRow = 9;
    options.retainRows = true;
    const io::IoResult<io::RawTable> table =
        reader->read(fileA, options, [](std::uint64_t, io::CsvRowView&&) { return true; },
                     nullptr, nullptr, {});
    ASSERT_TRUE(static_cast<bool>(table)) << table.error.detail;
    EXPECT_TRUE(table.value.report.marked);   // 方言标识行识别
    EXPECT_TRUE(table.value.report.hasHeader);
    ASSERT_EQ(table.value.report.header.size(), 8u);
    EXPECT_EQ(table.value.report.header[0], "t_s");
    EXPECT_EQ(table.value.report.header[5], "joint_type");
    // 行流：魔数＋6 身份行（1 字段）＋6 数据行（8 字段）＝13 行。
    ASSERT_EQ(table.value.rows.size(), 13u);
    EXPECT_EQ(table.value.rows[0][0], "#ird-trajectory-export v1");
    EXPECT_EQ(table.value.rows[1][0], "# snapshotId=cid-golden-snapshot");
    EXPECT_EQ(table.value.rows[6][0], "# robworkBaselineVersion=rw-baseline-golden");
    // 首数据行（t=0，关节 0）：SI 真值位级还原（to_chars 最短往返）。
    const std::vector<io::IoString>& first = table.value.rows[7];
    ASSERT_EQ(first.size(), 8u);
    EXPECT_EQ(first[0], "0");
    EXPECT_EQ(first[1], "0");
    EXPECT_EQ(first[2], "0");
    EXPECT_EQ(first[5], "revolute");
    EXPECT_EQ(first[6], "0");
    EXPECT_EQ(first[7], "id-golden-station-a");
    // 末数据行（t=1.0，关节 1）：段归落末段＋负值还原。
    const std::vector<io::IoString>& last = table.value.rows[12];
    ASSERT_EQ(last.size(), 8u);
    EXPECT_EQ(last[1], "1");
    EXPECT_EQ(last[5], "prismatic");
    EXPECT_EQ(last[6], "2");
    EXPECT_EQ(last[7], "id-golden-station-b");

    fs::remove_all(dir);
}

/**
 * 用例：未确认覆盖拒绝（§15.13"不覆盖未经确认文件"）——目标已存在且
 * overwriteConfirmed=false→TargetNotConfirmed＋IO-PACK-TARGET-EXISTS＋目
 * 标字节未触碰。
 */
TEST(StandardExport, UnconfirmedOverwriteRejectedAndTargetUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    const fs::path dir = makeTempDir("nooverwrite");
    const fs::path file = dir / "trj.csv";
    trj::TrajectoryExportRequest first = goldenRequest(&tp);
    first.targetPath = file;
    first.overwriteConfirmed = true;   // 首次落盘（目标不存在，确认位无效）
    ASSERT_EQ(trj::exportStandardTrajectory(first).status,
              trj::TrajectoryExportOutcome::Status::Ok);
    const std::vector<char> before = readBytes(file);

    trj::TrajectoryExportRequest second = goldenRequest(&tp);
    second.targetPath = file;
    second.overwriteConfirmed = false;  // 未确认
    const trj::TrajectoryExportOutcome outcome = trj::exportStandardTrajectory(second);
    EXPECT_EQ(outcome.status,
              trj::TrajectoryExportOutcome::Status::TargetNotConfirmed);
    EXPECT_EQ(outcome.errorToken, "IO-PACK-TARGET-EXISTS");
    EXPECT_FALSE(outcome.errorMessage.empty());
    EXPECT_EQ(readBytes(file), before) << "目标字节未触碰";

    fs::remove_all(dir);
}

/**
 * 用例：确认覆盖原子替换——overwriteConfirmed=true→Ok 且目标内容＝新
 * payload 的导出字节（与另一路径的新鲜导出逐字节一致）。
 */
TEST(StandardExport, ConfirmedOverwriteReplacesContent)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tpA = goldenPayload();
    trj::TimeParameterization tpB = goldenPayload();
    tpB.samples[2].q = rw::math::Q(2, 2.0, 1.0);   // 不同的已归档 payload

    const fs::path dir = makeTempDir("overwrite");
    const fs::path file = dir / "trj.csv";
    trj::TrajectoryExportRequest first = goldenRequest(&tpA);
    first.targetPath = file;
    first.overwriteConfirmed = true;
    ASSERT_EQ(trj::exportStandardTrajectory(first).status,
              trj::TrajectoryExportOutcome::Status::Ok);

    trj::TrajectoryExportRequest second = goldenRequest(&tpB);
    second.targetPath = file;
    second.overwriteConfirmed = true;
    const trj::TrajectoryExportOutcome outcome = trj::exportStandardTrajectory(second);
    ASSERT_EQ(outcome.status, trj::TrajectoryExportOutcome::Status::Ok);

    trj::TrajectoryExportRequest fresh = goldenRequest(&tpB);
    fresh.targetPath = dir / "fresh.csv";
    fresh.overwriteConfirmed = true;
    ASSERT_EQ(trj::exportStandardTrajectory(fresh).status,
              trj::TrajectoryExportOutcome::Status::Ok);
    EXPECT_EQ(readBytes(file), readBytes(dir / "fresh.csv"))
        << "确认覆盖后目标内容＝新 payload 导出字节";

    fs::remove_all(dir);
}

/**
 * 用例：取消保留既有输出（io RAII 放弃语义的导出面钉扎）——样本批边界
 * 取消命中→Canceled；先前目标字节完整保留（替换未发生——§15.13 后置）。
 */
TEST(StandardExport, CancelPreservesExistingTarget)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    const fs::path dir = makeTempDir("cancel");
    const fs::path file = dir / "trj.csv";
    // 先前输出（完整导出）。
    trj::TrajectoryExportRequest first = goldenRequest(&tp);
    first.targetPath = file;
    first.overwriteConfirmed = true;
    ASSERT_EQ(trj::exportStandardTrajectory(first).status,
              trj::TrajectoryExportOutcome::Status::Ok);
    const std::vector<char> before = readBytes(file);

    // 取消注入：第 3 次 writeRow 命中（身份块中部——写会话中途取消）。
    int rowCalls = 0;
    trj::TrajectoryExportRequest canceled = goldenRequest(&tp);
    canceled.targetPath = file;
    canceled.overwriteConfirmed = true;
    canceled.cancel = [&rowCalls] { return ++rowCalls > 2; };
    const trj::TrajectoryExportOutcome outcome = trj::exportStandardTrajectory(canceled);
    EXPECT_EQ(outcome.status, trj::TrajectoryExportOutcome::Status::Canceled);
    EXPECT_TRUE(outcome.errorToken.empty());
    EXPECT_EQ(readBytes(file), before) << "先前输出完整保留（io RAII 放弃）";

    fs::remove_all(dir);
}

/**
 * 用例：写出失败 io token 传播（TRJ-EXPORT-FAILED 的素材锚）——假写出器
 * 在第 4 次 writeRow 注入 IO-FORMAT-INTERNAL→Failed、errorToken＝注入码、
 * finish 未被调用（目标不变的结构前提）。
 */
TEST(StandardExport, WriterFailureSurfacesIoToken)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();
    trj::TrajectoryExportRequest req = goldenRequest(&tp);

    const auto state = std::make_shared<FakeCsvState>();
    state->failOnRowCall = 4;   // 魔数＋两身份行后失败
    trj::TrajectoryExportIoPorts ports;
    ports.csvWriterFactory = [state]() -> std::unique_ptr<io::ICsvWriter> {
        return std::make_unique<FakeCsvWriter>(state);
    };
    const trj::TrajectoryExportOutcome outcome =
        trj::exportStandardTrajectory(req, ports);
    EXPECT_EQ(outcome.status, trj::TrajectoryExportOutcome::Status::Failed);
    EXPECT_EQ(outcome.errorToken, "IO-FORMAT-INTERNAL");
    EXPECT_FALSE(outcome.errorMessage.empty());
    EXPECT_FALSE(state->finishCalled) << "失败路径不得 finish（目标不变前提）";
}

/**
 * 用例：前置违约族（八连——工厂/目标/payload/维度/有限性/映射半开/映射
 * 自洽/身份块）——各 token 精确。
 */
TEST(StandardExport, ValidationFamily)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TimeParameterization tp = goldenPayload();

    // 工厂缺席（装配违约）。
    {
        trj::TrajectoryExportRequest req = goldenRequest(&tp);
        trj::TrajectoryExportIoPorts ports;   // 空 std::function
        ports.csvWriterFactory = nullptr;
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req, ports); },
                              "export/factory-missing");
    }
    // 目标路径空。
    {
        trj::TrajectoryExportRequest req = goldenRequest(&tp);
        req.targetPath.clear();
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/target-empty");
    }
    // payload 缺席。
    {
        trj::TrajectoryExportRequest req = goldenRequest(nullptr);
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/payload-missing");
    }
    // jointTypes 维度不一致。
    {
        trj::TrajectoryExportRequest req = goldenRequest(&tp);
        req.jointTypes.pop_back();
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/joint-types-dim");
    }
    // 非有限样本分量。
    {
        trj::TimeParameterization bad = goldenPayload();
        bad.samples[0].qd = rw::math::Q(2, 0.0, std::numeric_limits<double>::infinity());
        trj::TrajectoryExportRequest req = goldenRequest(&bad);
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/payload-non-finite");
    }
    // 映射列半开（只给了段时长）。
    {
        trj::TrajectoryExportRequest req = goldenRequest(&tp);
        req.segmentAttribution.clear();
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/mapping-half-open");
    }
    // 段归投影失配（Σ≠total）。
    {
        trj::TrajectoryExportRequest req = goldenRequest(&tp);
        req.segmentDurationsS = {0.5, 0.5, 0.5};   // Σ=1.5≠1.0
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/mapping-sum");
    }
    // 身份块缺字段。
    {
        trj::TrajectoryExportRequest req = goldenRequest(&tp);
        req.identity.robworkBaselineVersion.clear();
        expectTrajectoryError([&] { trj::exportStandardTrajectory(req); },
                              "export/identity-incomplete");
    }
}
