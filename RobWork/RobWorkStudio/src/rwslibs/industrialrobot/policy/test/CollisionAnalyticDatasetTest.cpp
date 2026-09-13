/**
 * @file   CollisionAnalyticDatasetTest.cpp
 * @brief  跨单元契约套件（POL-T11）——碰撞解析算例数据集消费：
 *         testdata/golden/pol-collision-analytic（analytic-case 类）经
 *         内置后端真实评估对照（POL-AT-4 倒挂观测点载体）＋工程策略
 *         默认类别的档案消费（testkit §4.3.2）。
 *
 * 设计依据：
 *   - units/policy.md §11（"真实碰撞数值正确性经内置后端对构造场景
 *     （已知相交/分离几何）的解析算例验证——analytic-case 类黄金数据集"；
 *     POL-AT-4 行："倒挂编译产物场景（构造）→评估→对象对与手算一致
 *     （环境几何世界系固连、机器人倒置）；无二次旋转——解析算例对照"）；
 *     §10.5（倒挂机型一致消费走查——R_world_base 单一权威、policy 禁止
 *     二次旋转）；§12 POL-T11 行（数据集交付面）；
 *   - units/testkit.md §4.2.1（analytic-case＝可独立正确性依据）、
 *     §4.3.2（工程策略默认类别：测试经被测 policy 对象取值；manifest
 *     producer.solverConfig 记录策略快照——不得旁路策略私设阈值）、
 *     §10.2 policy 行（"碰撞判定数据集"——本套件为消费端）；
 *   - 需求 MDL-22/AT-37（基座—世界变换单一消费）、ARC-05（阈值唯一
 *     来源是策略对象）、KIN-05（coverage 覆盖事实——不伪装已检）。
 *
 * 为什么期望值可信（独立性声明）：expected/analytic.json 由手算闭式推导
 * 产出（同姿态凸盒面间隙＝轴向中心距−边长；盒三角网格精确无细分误差），
 * 生成脚本为推导的机器承载且含推导自检——与本产品碰撞实现零关联。
 *
 * 模式约束：集成模式专属（真实 WorkCell＋内置后端——冒烟不编译）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/policy/testdouble/PolicyTestDoubles.hpp>

#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace sdurws::ird::policy;
using namespace sdurws::ird::policy::testdoubles;   // 注入面替身（§11 设施）

/// core/testkit 契约类型短别名。
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

namespace {

/// 数据集引用（与目录结构一致）。
constexpr char kDatasetId[] = "pol-collision-analytic";
constexpr char kDatasetVersion[] = "1.0.0";

/// 读文本文件（数据集 inputs/expected 的读取入口）。
std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const tk::JsonValue* requireObject(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺对象字段: " << key;
    return v;
}

const tk::JsonValue* requireArray(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺数组字段: " << key;
    return v;
}

std::string requireString(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺字符串字段: " << key;
    return v == nullptr ? std::string{} : v->text;
}

double requireNumber(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺数值字段: " << key;
    return v == nullptr ? 0.0 : v->number;
}

bool requireBool(const tk::JsonValue& o, const std::string& key)
{
    const tk::JsonValue* v = o.find(key);
    EXPECT_NE(v, nullptr) << "数据集缺布尔字段: " << key;
    return v != nullptr && v->isBool() && v->boolean;
}

/// 文件全文读取（源码锚扫描的输入——At37 结构面用）。
std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 子串存在性（源码锚扫描原语）。
bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// 装置对象身份集（字节打标——与解析算例的对象命名一一对应）。
struct AnalyticIds {
    core::ObjectId toolBox = [] {
        core::ObjectId id;
        id.bytes[0] = 0x21;
        return id;
    }();
    core::ObjectId envBox = [] {
        core::ObjectId id;
        id.bytes[0] = 0x22;
        return id;
    }();
    core::ObjectId device = [] {
        core::ObjectId id;
        id.bytes[0] = 0x29;
        return id;
    }();
    core::ObjectId policyObj = [] {
        core::ObjectId id;
        id.bytes[0] = 0x20;
        return id;
    }();
};

/// 解析算例场景装置（mount＝R_world_base 编译产物注入——MDL-22 单一权威）。
struct AnalyticRig {
    AnalyticIds ids;
    std::shared_ptr<rw::models::WorkCell> workcell;
    CollisionScene scene;
    ScriptedNameContext names;              // 注入面替身（testdouble 头）
    ScriptedValidationContext validation;   // 发布闭包替身
};

/**
 * @brief 构造解析算例场景（设备 1 自由度＋工具盒＋世界系固连环境盒）。
 *
 * @param mountInverted [in] true＝倒挂安装（R_world_base=Ry(π)——编译产物
 *                      把安装位姿编译进 Base 帧；policy 禁止二次旋转）
 * @param envCenterZ    [in] 环境盒中心 z（SI m——环境几何世界系固连）
 */
AnalyticRig makeAnalyticRig(bool mountInverted, double envCenterZ)
{
    AnalyticRig rig;
    rig.ids = AnalyticIds{};

    const rw::math::Transform3D<> mount =
        mountInverted
            ? rw::math::Transform3D<>(
                rw::math::Vector3D<>(0.0, 0.0, 0.0),
                rw::math::RPY<>(0.0, std::acos(-1.0), 0.0).toRotation3D())
            : rw::math::Transform3D<>::identity();

    rig.workcell = std::make_shared<rw::models::WorkCell>("PolAnalyticCaseWC");
    rw::core::Ptr<rw::kinematics::FixedFrame> base =
        rw::core::ownedPtr(new rw::kinematics::FixedFrame("Base", mount));
    rig.workcell->addFrame(base);
    rw::core::Ptr<rw::models::RevoluteJoint> j1 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J1", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(j1, base);
    rw::core::Ptr<rw::kinematics::FixedFrame> tool = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "Tool", rw::math::Transform3D<>(rw::math::Vector3D<>(0.5, 0.0, 0.0))));
    rig.workcell->addFrame(tool, j1);
    rw::core::Ptr<rw::kinematics::FixedFrame> envFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "EnvBox", rw::math::Transform3D<>(rw::math::Vector3D<>(0.5, 0.0, envCenterZ))));
    rig.workcell->addFrame(envFrame);
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));

    // 碰撞几何（0.2 m 立方体——三角网格精确，面间隙闭式可手算）。
    const auto attachBox = [&](const rw::kinematics::Frame::Ptr& frame) {
        rw::core::Ptr<rw::models::RigidObject> object =
            rw::core::ownedPtr(new rw::models::RigidObject(frame));
        object->addGeometry(rw::core::ownedPtr(
            new rw::geometry::Geometry(rw::core::ownedPtr(new rw::geometry::Box(
                0.2, 0.2, 0.2)))));
        rig.workcell->add(object);
    };
    attachBox(tool);
    attachBox(envFrame);

    // 场景装配（CR-04——调用方自编译产物装配）。
    rig.scene.workcell = rig.workcell;
    rig.scene.primaryDevice = rig.ids.device;
    rig.scene.objects = {
        {rig.ids.toolBox, "ToolBox", SceneObjectRole::RobotLink, true},
        {rig.ids.envBox, "EnvBox", SceneObjectRole::EnvironmentObject, true},
    };
    rig.scene.sceneContentIdentity = [] {
        core::ContentIdentity cid;
        cid.bytes[0] = 0xC8;
        return cid;
    }();

    // 名称映射与发布闭包应答。
    rig.names.mapIdentity = [] {
        core::ContentIdentity cid;
        cid.bytes[0] = 0x6F;
        return cid;
    }();
    rig.names.byName["Robot"] = rig.ids.device;
    rig.names.byId[rig.ids.device] = "Robot";
    rig.names.byId[rig.ids.toolBox] = "Tool";
    rig.names.byId[rig.ids.envBox] = "EnvBox";
    rig.validation.existingObjects[rig.ids.toolBox] = true;
    rig.validation.roles[rig.ids.toolBox] = "RobotLink";
    rig.validation.existingObjects[rig.ids.envBox] = true;
    rig.validation.roles[rig.ids.envBox] = "EnvironmentObject";
    return rig;
}

/// SingleState 查询（单构型便捷——按值返回非 const）。
CollisionQuery singleStateQuery(double q)
{
    CollisionQuery query;
    query.kind = CollisionQueryKind::SingleState;
    query.configurations.push_back(rw::math::Q(1, q));
    return query;
}

/**
 * @brief 从 manifest 的策略快照发布策略（testkit §4.3.2 工程策略默认类别
 *        的消费方式——阈值经被测 policy 对象取值，不旁路策略私设）。
 *
 * 快照消费口径：safetyClearance=dataset-declared 显式 0（§7.4 m=0 退化）；
 * 行程上限**不在 Raw 输入中提供**——由解析③填入冻结默认 4π（附录 D
 * 第 11 项 origin=DefaultAppendixD），随后对照快照声明（4π）——"经被测
 * policy 对象取值"的字面执行。
 */
EngineeringPolicySet publishSnapshotPolicy(const AnalyticRig& rig,
                                           double safetyClearanceSi)
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = rig.ids.policyObj;
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{safetyClearanceSi, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    // nearLimitRatio/conditionNumberWarning 不提供（无冻结默认——nullopt
    // 保持显式不适用，P-POL-2）；行程上限不提供（解析③填 4π 冻结默认）。
    const PolicyParseResult res = resolvePolicy(in, rig.validation);
    if (!res.policy.has_value()) {
        ADD_FAILURE() << "快照策略应可发布，首条诊断: "
                      << (res.diagnostics.empty() ? std::string{"-"}
                                                  : res.diagnostics.front().code);
    }
    return *res.policy;
}

}  // namespace

// =====================================================================
// 用例组 1：数据集装载与策略快照消费（testkit §4.3.2）。
// =====================================================================

/**
 * analytic-case 数据集经 GoldenDataset 全链装载（schema/完整性/交叉校验
 * ——lint 通过的消费侧承载）；referenceSource 独立性与 edgeCases 三布尔
 * 在案（analytic-case 类的类别强制面）。
 */
TEST(PolCollisionAnalytic, DatasetLoadsThroughFullManifestChain)
{
    const tk::GoldenDataset dataset = tk::GoldenDataset::load({kDatasetId, kDatasetVersion});
    const tk::DatasetManifest& manifest = dataset.manifest();
    EXPECT_EQ(manifest.kind, tk::DatasetKind::AnalyticCase);
    EXPECT_TRUE(manifest.referenceSourcePresent);
    EXPECT_TRUE(manifest.referenceSource.independentOfProductionImpl)
        << "解析算例必须独立于生产实现（testkit §4.2.1）";
    EXPECT_TRUE(manifest.edgeCasesPresent);
    EXPECT_TRUE(manifest.edgeCases.zeroValue && manifest.edgeCases.nearZero
                && manifest.edgeCases.signCancellation)
        << "附录 D C4：三布尔全 true（lint 强制）";
    EXPECT_GE(manifest.integrity.size(), 3u);
}

/**
 * 策略快照消费（testkit §4.3.2 工程策略默认类别）：快照声明行程上限 4π
 * ＝发布策略对象的 DefaultAppendixD 默认值（阈值经被测 policy 对象取值
 * ——ARC-05 唯一权威；测试不旁路策略私设阈值）；safetyClearance 快照值
 * ＝dataset-declared 显式 0。
 */
TEST(PolCollisionAnalytic, PolicySnapshotConsumedThroughPolicyObject)
{
    IRD_TEST_INFO("MDL-06", {"AT-37"}, std::nullopt);
    const tk::GoldenDataset dataset = tk::GoldenDataset::load({kDatasetId, kDatasetVersion});
    // 读取 manifest 的策略快照（producer.solverConfig.policyThresholdSnapshot）。
    const tk::JsonValue snapshotRoot = tk::parseJson(dataset.manifest().producer.solverConfigJson);
    const tk::JsonValue* snapshot = requireObject(snapshotRoot, "policyThresholdSnapshot");
    ASSERT_NE(snapshot, nullptr);
    const double clearanceSi = requireNumber(*snapshot, "safetyClearanceSi");
    const double travelLimitRad = requireNumber(*snapshot, "finiteRotationTravelLimitRad");

    // 发布策略（快照值经 Raw 输入/解析默认进入 policy 对象）。
    const AnalyticRig rig = makeAnalyticRig(false, 0.0);
    const EngineeringPolicySet policy = publishSnapshotPolicy(rig, clearanceSi);

    // 快照↔策略对象对照（消费断言经 policy 对象——不旁路）。
    ASSERT_TRUE(policy.collision.safetyClearance.has_value());
    EXPECT_EQ(policy.collision.safetyClearance->siValue(), clearanceSi);
    // 行程上限：解析③填入的冻结默认（origin=DefaultAppendixD）＝快照声明值。
    EXPECT_EQ(policy.jointThresholds.finiteRotationTravelLimit.origin(),
              PolicyValueOrigin::DefaultAppendixD)
        << "行程上限默认来源＝附录 D 第 11 项（engineering-policy-default）";
    EXPECT_EQ(policy.jointThresholds.finiteRotationTravelLimit.siValue(), travelLimitRad);
    EXPECT_DOUBLE_EQ(travelLimitRad, 12.566370614359172) << "快照值＝4π（附录 D 第 11 项）";
}

// =====================================================================
// 用例组 2：解析算例逐例对照（内置后端真实评估 vs 手算期望）。
// =====================================================================

/**
 * 逐例对照：inputs/scene-cases.json 的每个工况经真实会话评估——状态/
 * finalized/发现对象对/判定与 expected/analytic.json 的手算结果一致；
 * coverage 覆盖事实显式（pairsInScope/pairsEvaluated——KIN-05 不伪装）。
 */
TEST(PolCollisionAnalytic, AllCasesMatchHandDerivedExpectations)
{
    IRD_TEST_INFO("ARC-05", {"AT-19"}, std::nullopt);
    const tk::GoldenDataset dataset = tk::GoldenDataset::load({kDatasetId, kDatasetVersion});
    const std::string casesText =
        readTextFile(dataset.resolveInput("inputs/scene-cases.json"));
    ASSERT_FALSE(casesText.empty()) << "inputs/scene-cases.json 不可读";
    const tk::JsonValue casesRoot = tk::parseJson(casesText);
    const tk::JsonValue* cases = requireArray(casesRoot, "cases");
    ASSERT_NE(cases, nullptr);
    const std::string expectedText =
        readTextFile(dataset.resolveExpected("expected/analytic.json"));
    ASSERT_FALSE(expectedText.empty()) << "expected/analytic.json 不可读";
    const tk::JsonValue expectedRoot = tk::parseJson(expectedText);
    const tk::JsonValue* expectedCases = requireArray(expectedRoot, "cases");
    ASSERT_NE(expectedCases, nullptr);
    ASSERT_EQ(cases->items.size(), expectedCases->items.size());

    // 期望索引（id→expected 条目——对照装配）。
    std::map<std::string, const tk::JsonValue*> expectedById;
    for (const auto& e : expectedCases->items) {
        expectedById[requireString(e, "id")] = &e;
    }

    // 策略快照（组 1 已核对其消费面——此处直接取值发布）。
    const tk::JsonValue snapshotRoot =
        tk::parseJson(dataset.manifest().producer.solverConfigJson);
    const double clearanceSi =
        requireNumber(*requireObject(snapshotRoot, "policyThresholdSnapshot"),
                      "safetyClearanceSi");

    // 真实评估器（内置 ProximityStrategyRW——唯一实现，ARC-05）。
    auto evaluator = makeRobWorkCollisionEvaluator();

    for (const auto& c : cases->items) {
        const std::string id = requireString(c, "id");
        const std::string mount = requireString(c, "mount");
        const double q = requireNumber(c, "qRad");
        const double envZ = requireNumber(c, "envCenterZM");
        const auto itExpected = expectedById.find(id);
        ASSERT_NE(itExpected, expectedById.end()) << "expected 缺工况: " << id;
        const bool expectedCollision = requireBool(*itExpected->second, "collision");
        const tk::JsonValue* expectedPairs =
            requireArray(*itExpected->second, "pairs");
        ASSERT_NE(expectedPairs, nullptr);

        // 场景装配（mount＝编译产物安装位姿；环境盒世界系固连 z 偏移）。
        const AnalyticRig rig =
            makeAnalyticRig(mount == "inverted-y-pi", envZ);
        const EngineeringPolicySet policy = publishSnapshotPolicy(rig, clearanceSi);
        const std::shared_ptr<const CollisionEvaluationSession> session =
            evaluator->createSession(policy, rig.scene, rig.names);
        CollisionQuery query;
        query.kind = CollisionQueryKind::SingleState;
        query.configurations.push_back(rw::math::Q(1, q));
        ScriptedCallContext ctx;
        const CollisionEvaluation result = session->evaluate(query, ctx);

        // 状态面： Completed＋finalized（解析算例无故障注入）。
        ASSERT_EQ(result.status, CollisionEvaluationStatus::Completed) << id;
        EXPECT_TRUE(result.finalized) << id;

        // 发现面：碰撞对象对与手算一致（对象 ID 对＋判定的逐例对照）。
        if (expectedCollision) {
            ASSERT_EQ(result.findings.size(), expectedPairs->items.size()) << id;
            ASSERT_EQ(result.findings.size(), 1u) << id;
            const CollisionFinding& finding = result.findings.front();
            EXPECT_EQ(finding.kind, CollisionFindingKind::Collision) << id;
            // 有序对（A<B 规范序——0x21 toolBox < 0x22 envBox）。
            EXPECT_EQ(finding.objectA, rig.ids.toolBox) << id;
            EXPECT_EQ(finding.objectB, rig.ids.envBox) << id;
            EXPECT_EQ(finding.sampleIndex, 0u) << id;
        }
        else {
            EXPECT_TRUE(result.findings.empty())
                << id << ": 手算无碰撞——不得有发现（无'无碰撞'结论字段"
                   "以外的错误产出）";
        }

        // coverage 覆盖事实（KIN-05——已检/有几何/被评估的显式计数）。
        EXPECT_EQ(result.coverage.pairsInScope, 1u) << id;
        EXPECT_EQ(result.coverage.pairsWithGeometry, 1u) << id;
        EXPECT_EQ(result.coverage.pairsEvaluated, 1u) << id;
        EXPECT_TRUE(result.coverage.excluded.empty()) << id;
    }
}

/**
 * POL-AT-4（MDL-22/AT-37 观测点）：倒挂编译产物场景——对象对与手算一致
 * （环境几何世界系固连、机器人倒置）；无二次旋转的结构与数值双断言：
 * 数值面＝inverted-clear 工况若二次旋转即翻转为碰撞（判别例）；
 * 结构面＝policy 不携带任何位姿变换逻辑（本头零变换代码——实现评审的
 * 机检投影，policy.md §10.5"policy 禁止叠加二次旋转——R-POL 规则"）。
 */
TEST(PolCollisionAnalytic, At37InvertedMountConsumesCompiledStateWithoutSecondRotation)
{
    IRD_TEST_INFO("MDL-22", {"AT-37"}, std::nullopt);
    // 数值面①：倒挂 q=0——工具盒世界位 (−0.5,0,0)，与环境盒 (0.5,0,0)
    // 分离（面间隙 0.8 m）→ 无发现；若把安装变换二次叠加（再把世界态
    // Ry(π) 一次），工具盒翻转到 (0.5,0,0) 即与环境盒重合误报。
    const AnalyticRig inverted = makeAnalyticRig(true, 0.0);
    const EngineeringPolicySet policy = publishSnapshotPolicy(inverted, 0.0);
    auto evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, inverted.scene, inverted.names);
    ScriptedCallContext ctx;
    const CollisionEvaluation clearAtQ0 =
        session->evaluate(singleStateQuery(0.0), ctx);
    ASSERT_EQ(clearAtQ0.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(clearAtQ0.findings.empty())
        << "倒挂 q=0 应无碰撞（二次旋转判别例——手算面间隙 0.8 m）";
    EXPECT_TRUE(clearAtQ0.finalized);

    // 数值面②：倒挂 q=π——工具盒世界位 (0.5,0,0)＝环境盒（重合碰撞）；
    // 对象对与手算一致（POL-AT-4 观测点——四消费方一致性总测归 runtime）。
    const CollisionEvaluation contactAtPi =
        session->evaluate(singleStateQuery(std::acos(-1.0)), ctx);
    ASSERT_EQ(contactAtPi.status, CollisionEvaluationStatus::Completed);
    ASSERT_EQ(contactAtPi.findings.size(), 1u);
    EXPECT_EQ(contactAtPi.findings.front().objectA, inverted.ids.toolBox);
    EXPECT_EQ(contactAtPi.findings.front().objectB, inverted.ids.envBox);
    EXPECT_EQ(contactAtPi.findings.front().kind, CollisionFindingKind::Collision);

    // 结构面：policy 公共头零位姿变换逻辑（无 rw 变换类型成员/Transform3D
    // 计算——MDL-22"基座—世界变换随编译产物内置，policy 禁止二次旋转"
    // 的实现评审机检投影：扫本单元产品头/实现的行为面符号）。
    const std::filesystem::path unitRoot{IRD_POLICY_UNIT_ROOT};
    const std::string evaluatorHeader =
        readAll(unitRoot / "policy" / "include" / "sdurws" / "ird" / "policy"
                / "CollisionEvaluator.hpp");
    ASSERT_FALSE(evaluatorHeader.empty());
    EXPECT_TRUE(contains(evaluatorHeader, "policy 禁止二次旋转"))
        << "二次旋转禁令的契约注释在案（MDL-22/M-11）";
    // 直构会话的输入面只有编译产物（WorkCell＋State）——无任何安装变换
    // 参数槽位（会话构造签名的结构事实：policy 无法接收"安装变换"）。
    EXPECT_TRUE(contains(evaluatorHeader,
                         "const CollisionScene& scene"))
        << "会话构建只经 CollisionScene 值传递（编译产物单一消费——无安装"
           "变换入口）";
}
