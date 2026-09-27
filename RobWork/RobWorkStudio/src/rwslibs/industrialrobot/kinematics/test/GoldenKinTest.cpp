/**
 * @file   GoldenKinTest.cpp
 * @brief  kinematics 黄金数据集契约套件（WP-15-T13）——四套黄金数据集
 *         （kin-fk-analytic / kin-ik-dedup / kin-search-outcomes /
 *         kin-coverage）经 testkit 黄金设施（GoldenFixture＋ToleranceProfile
 *         ＋Check/SetCheck 断言）消费：V-01/V-04/V-05/V-06/V-07/V-08/
 *         V-09/V-13/V-14/V-21 本单元侧的黄金数据载体用例与确定性终验。
 *
 * 设计依据：
 *   - units/kinematics.md §10.1（黄金数据集四套口径＋测试替身总则）、§10.2
 *     （故障注入矩阵——本文件用例名携带 V 编号逐条对应）、§5.4（五类结局
 *     铁律）、§6.3（稳定排序四键）、§7.2（覆盖率分母规则）、§8.4（确定性
 *     ——线程配置等价集合）
 *   - units/testkit.md §6.1（GoldenFixture 六步生命周期）、§5.3/§5.4
 *     （Check/SetCheck 断言设施——黄金数值断言全部经 TK-T05/TK-T06 设施
 *     执行，测试侧不自写容差比较式）
 *   - 需求 AT-03（全部反例——FK 对照/去重/换初值/构型级碰撞/覆盖固定
 *     计数与零样本）；附录 D 第 1/2/3/8/9 项（容差档案锚点）
 *   - 任务契约 tasks/foundation/WP-15-T13.json acceptance 1/2/4/5
 *
 * 测试策略（黄金数据独立性——analytic-case/contract-fixture 类的 lint
 * 义务）：期望值全部由数据集 generate/ 脚本闭式推导（与产品实现零共享），
 * 本文件只做"装载→驱动产品入口→经档案容差对照"三件事，不书写任何参考
 * 几何常量。驱动入口＝产品路径（FkEvaluator/IkSolver/generateSampleSet/
 * runRegionCoverageComputation——NFR-MNT-01 计算库可直调面）。
 *
 * 线程约束：gtest 用例天然串行；V-21 的 8 线程分片由
 * runRegionCoverageComputation(query.threadCount) 承载（§8.4 全序归并，
 * 线程数是执行参数——不入样本集身份）。
 */

#include "KinFkFixture.hpp"   // TestView/NoopContext/idFrom/digestOf（单元内测试设施）

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/kinematics/Coverage.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Fk.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/kinematics/KinTypes.hpp>
#include <sdurws/ird/kinematics/Sampling.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/SetCheck.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace core = sdurws::ird::core;
namespace ev   = sdurws::ird::evidence;
namespace kin  = sdurws::ird::kinematics;
namespace rt   = sdurws::ird::runtime;
namespace tk   = sdurws::ird::testkit;
// IRD_EXPECT_* 宏展开为无命名空间限定的 checkCloseWithin 等标识符
// （AssertMacros 契约——宏仅在"已开 testkit 可见性"的消费 TU 使用）。
using namespace sdurws::ird::testkit;

// =====================================================================
// 黄金 JSON 读取脚手架（数据集装载/完整性已由 GoldenFixture 校验；此处
 // 只做字段提取——字段缺失记失败并回退安全值，防越界崩溃掩盖首因）
// =====================================================================

namespace {

/// 读数据集文本文件（inputs/expected 侧相对路径经 dataset 解析——路径
/// 必须登记于 manifest，越界抛 TestKitError）。
std::string readDatasetText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    const auto path = inputSide ? ds.resolveInput(rel) : ds.resolveExpected(rel);
    std::ifstream in(path, std::ios::binary);
    if (!in) { return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

tk::JsonValue loadJson(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    return tk::parseJson(readDatasetText(ds, rel, inputSide));
}

/// 必填对象字段（缺失记失败并返回空对象——调用方继续走安全路径）。
tk::JsonValue field(const tk::JsonValue& o, const char* key, tk::JsonValue fallback)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr) {
        ADD_FAILURE() << "黄金数据缺字段: " << key;
        return fallback;
    }
    return *v;
}

double num(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isNumber()) {
        ADD_FAILURE() << "黄金数据缺数值字段: " << key;
        return 0.0;
    }
    return v->number;
}

/// 数组字段的数值展平（非数值元素记失败跳过）。
std::vector<double> numVec(const tk::JsonValue& o, const char* key)
{
    std::vector<double> out;
    const tk::JsonValue* v = (key == nullptr) ? &o : o.find(key);
    if (v == nullptr || !v->isArray()) {
        ADD_FAILURE() << "黄金数据缺数组字段: " << (key == nullptr ? "<item>" : key);
        return out;
    }
    for (const auto& e : v->items) { out.push_back(e.number); }
    return out;
}

/// 布尔数组展平（样本可达表——boolean 项）。
std::vector<bool> boolVec(const tk::JsonValue& o, const char* key)
{
    std::vector<bool> out;
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isArray()) {
        ADD_FAILURE() << "黄金数据缺布尔数组字段: " << key;
        return out;
    }
    for (const auto& e : v->items) { out.push_back(e.boolean); }
    return out;
}

// =====================================================================
// 黄金模型构造器（模型参数来自黄金 inputs JSON——测试侧零几何常量）。
// 黄金模型全部为旋转关节平面链（axis z、origin 纯平移）；tcpOffset 可带
// 绕 z 旋转（six-axis 的 Rz(π/2)——黄金 inputs 以 axis/angle 声明）。
// =====================================================================

rt::CanonicalModel makeGoldenModel(const tk::JsonValue& modelJson)
{
    std::vector<rt::CanonicalJoint> joints;
    int i = 0;
    for (const auto& j : field(modelJson, "joints", tk::JsonValue{}).items) {
        const auto o = numVec(j, "origin");
        if (o.size() != 3U) {
            ADD_FAILURE() << "黄金模型 origin 应为三维平移（m）";
            throw std::runtime_error("golden-model-invalid: origin 非三维");
        }
        // 轴向量（黄金声明——基座系零位轴向；planar 全 z，six-axis 混轴）。
        const auto axv = numVec(j, "axis");
        if (axv.size() != 3U) {
            ADD_FAILURE() << "黄金模型 axis 应为三维轴向量";
            throw std::runtime_error("golden-model-invalid: axis 非三维");
        }
        joints.push_back(revolute("gj" + std::to_string(i),
                                  rw::math::Vector3D<double>(axv[0], axv[1], axv[2]),
                                  trans(o[0], o[1], o[2]),
                                  num(j, "lower"), num(j, "upper")));
        ++i;
    }
    rw::math::Transform3D<double> tcp = trans(0, 0, 0);
    const tk::JsonValue off = field(modelJson, "tcpOffset", tk::JsonValue{});
    const auto xyz = numVec(off, "xyz");
    if (xyz.size() != 3U) {
        ADD_FAILURE() << "黄金 tcpOffset.xyz 应为三维平移（m）";
        throw std::runtime_error("golden-model-invalid: tcpOffset.xyz 非三维");
    }
    tcp = trans(xyz[0], xyz[1], xyz[2]);
    if (const tk::JsonValue* ax = off.find("axis"); ax != nullptr && ax->isArray()) {
        // 黄金 tcpOffset 旋转（绕声明轴 angle rad——six-axis Rz(π/2)）。
        const auto a = numVec(*ax, nullptr);
        if (a.size() != 3U) {
            ADD_FAILURE() << "黄金 tcpOffset.axis 应为三维单位轴";
            throw std::runtime_error("golden-model-invalid: axis 非三维");
        }
        const double ang = num(off, "angle");
        tcp = rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(xyz[0], xyz[1], xyz[2]),
            rw::math::Rotation3D<double>(
                // 绕单位轴 a 转 ang 的 Rodrigues 基矩阵（黄金轴恒 ±z/±y/
                // ±x 之一，角度常量——此处独立书写轴向公式，防夹具漂移）。
                std::cos(ang) + (1 - std::cos(ang)) * a[0] * a[0],
                (1 - std::cos(ang)) * a[0] * a[1] - std::sin(ang) * a[2],
                (1 - std::cos(ang)) * a[0] * a[2] + std::sin(ang) * a[1],
                (1 - std::cos(ang)) * a[1] * a[0] + std::sin(ang) * a[2],
                std::cos(ang) + (1 - std::cos(ang)) * a[1] * a[1],
                (1 - std::cos(ang)) * a[1] * a[2] - std::sin(ang) * a[0],
                (1 - std::cos(ang)) * a[2] * a[0] - std::sin(ang) * a[1],
                (1 - std::cos(ang)) * a[2] * a[1] + std::sin(ang) * a[0],
                std::cos(ang) + (1 - std::cos(ang)) * a[2] * a[2]));
    }
    return makeModel(joints, tcp);
}

/// 平面目标位姿（黄金 expected 的 {x,y,phi} 声明 → 基座系 Transform3D；
/// 黄金场景全部为平面 Rz(phi) 姿态——phi 单位 rad）。
rw::math::Transform3D<double> goldenTarget(double x, double y, double phi)
{
    const double c = std::cos(phi);
    const double s = std::sin(phi);
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(x, y, 0.0),
        rw::math::Rotation3D<double>(c, -s, 0, s, c, 0, 0, 0, 1));
}

// =====================================================================
// 替身碰撞评估器（脚本化判定——acceptance 3 的测试替身落点；替身仅测试
// 面：定义于测试翻译单元，产品目标零替身/零 testkit 链接——T-1）。
// 黄金 inputs.collisionScripts 的 token → 判定谓词。
// =====================================================================

class ScriptedCollisionSession final : public kin::IKinCollisionSession {
public:
    using Predicate = std::function<bool(const std::vector<double>&)>;

    ScriptedCollisionSession(Predicate p, std::vector<core::ObjectId> pairs)
        : m_pred(std::move(p)), m_pairs(std::move(pairs))
    {
    }

    kin::IkCollisionVerdict evaluate(const std::vector<double>& q) const override
    {
        kin::IkCollisionVerdict v;
        v.state = kin::IkCollisionEvaluationState::Evaluated;   // 脚本恒完成评价
        v.inCollision = m_pred(q);
        if (v.inCollision) { v.objectIdPairs = m_pairs; }       // 成对展平明细
        return v;
    }

private:
    Predicate m_pred;                       ///< 脚本化判定谓词（黄金 inputs 声明）
    std::vector<core::ObjectId> m_pairs;    ///< 碰撞对象对（[a,b] 一对）
};

/// 黄金 collision token → 替身（"none" 返回空——策略未启用语义）。
std::unique_ptr<ScriptedCollisionSession> scriptedSession(const std::string& token)
{
    const core::ObjectId a = idFrom<core::ObjectId>("kin-obs-a");
    const core::ObjectId b = idFrom<core::ObjectId>("kin-obs-b");
    if (token == "collide-all") {
        return std::make_unique<ScriptedCollisionSession>(
            [](const std::vector<double>&) { return true; },
            std::vector<core::ObjectId>{a, b});
    }
    if (token == "collide-when-q2-positive") {
        return std::make_unique<ScriptedCollisionSession>(
            [](const std::vector<double>& q) { return q.size() > 1 && q[1] > 0.0; },
            std::vector<core::ObjectId>{a, b});
    }
    return nullptr;
}

/// 双精度逐位一致（位模式比较——含 ±0/NaN 位级；V-04 逐位终验实现面）。
bool bitwiseEq(double a, double b)
{
    if (std::isnan(a) && std::isnan(b)) { return true; }
    return a == b && std::signbit(a) == std::signbit(b);
}

bool bitwiseEqVec(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size()) { return false; }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!bitwiseEq(a[i], b[i])) { return false; }
    }
    return true;
}

// =====================================================================
// TK-T06 集合断言 traits 特化——黄金 q 向量（逐轴）与黄金样本点（xyz）。
// fieldPath 底层字符串为 static 存储（NumericFieldView 持 string_view）。
// =====================================================================

/// 黄金 q 向量数值字段路径（档案条目 ik[*].solution[*].q[*]）。
std::string goldenQPath(std::size_t axis)
{
    static const std::string kBase = "ik[0].solution";
    return kBase + "[*].q[" + std::to_string(axis) + "]";
}

/// 黄金样本点数值字段路径（档案条目 coverage[*].sample.position.{x,y,z}）。
std::string goldenPointPath(std::size_t axis)
{
    static const char* kAxis[3] = {"x", "y", "z"};
    return std::string{"coverage[*].sample.position."} + kAxis[axis];
}

}  // namespace

namespace sdurws::ird::testkit {

/// q 向量（解构型）的黄金匹配 traits——数值一一配对（无身份键）。
template <>
struct SetMatchTraits<std::vector<double>> {
    static std::optional<std::string> identity(const std::vector<double>&)
    {
        return std::nullopt;
    }
    static std::vector<NumericFieldView> numerics(const std::vector<double>& q)
    {
        static std::array<std::string, 8> paths = [] {
            std::array<std::string, 8> p;
            for (std::size_t k = 0; k < p.size(); ++k) { p[k] = goldenQPath(k); }
            return p;
        }();
        std::vector<NumericFieldView> out;
        for (std::size_t k = 0; k < q.size() && k < paths.size(); ++k) {
            out.push_back({paths[k], q[k]});
        }
        return out;
    }
};

/// 黄金样本点（三维 xyz）的匹配 traits。
struct GoldenPoint {
    double x = 0.0;   ///< 格心 x（m，基座系）
    double y = 0.0;   ///< 格心 y（m，基座系）
    double z = 0.0;   ///< 格心 z（m，基座系）
};

template <>
struct SetMatchTraits<GoldenPoint> {
    static std::optional<std::string> identity(const GoldenPoint&)
    {
        return std::nullopt;
    }
    static std::vector<NumericFieldView> numerics(const GoldenPoint& p)
    {
        static const std::array<std::string, 3> paths = [] {
            std::array<std::string, 3> p;
            for (std::size_t k = 0; k < 3; ++k) { p[k] = goldenPointPath(k); }
            return p;
        }();
        return {{paths[0], p.x}, {paths[1], p.y}, {paths[2], p.z}};
    }
};

}  // namespace sdurws::ird::testkit

namespace {

/// 从黄金 solution JSON 条目提取 q 向量。
std::vector<double> qOf(const tk::JsonValue& sol) { return numVec(sol, "q"); }

}  // namespace

// =====================================================================
// 1) kin-fk-analytic——V-01 FK 解析对照与正反变换（AT-03 FK 对照组）
// =====================================================================

class KinGoldenFk : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override { return {"kin-fk-analytic", "1.0.0"}; }
};

/// V-01 前半：逐解析算例 FK 对照（位置/旋转/雅可比/指标——全部经档案
/// 容差与 IRD 断言设施；acceptance 1/5——黄金断言经 TK-T05 设施执行）。
TEST_F(KinGoldenFk, V01_AnalyticCasesCloseWithinProfile_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "NFR-COR-01"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/fk-cases.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/fk-expected.json", false);
    const tk::JsonValue models = field(inputs, "models", tk::JsonValue{});
    const tk::JsonValue inCases = field(inputs, "cases", tk::JsonValue{});
    const tk::JsonValue expCases = field(expected, "cases", tk::JsonValue{});
    ASSERT_EQ(inCases.items.size(), expCases.items.size());

    const kin::FkEvaluator fk;
    for (std::size_t index = 0; index < inCases.items.size(); ++index) {
        const tk::JsonValue& inCase = inCases.items[index];
        const tk::JsonValue& exp = expCases.items[index];
        SCOPED_TRACE(std::string("黄金算例: ") + inCase.find("id")->text);
        const rt::CanonicalModel model = makeGoldenModel(
            *models.find(inCase.find("model")->text));
        TestView view(model);
        const auto q = numVec(inCase, "q");
        const auto r = fk.evaluate(view, TcpRef{idFrom<core::ObjectId>("kin-tool"), ""}, q);
        ASSERT_TRUE(r.ok()) << "FK 评估意外失败";

        const std::string pfx = "fk[" + std::to_string(index) + "]";
        const tk::JsonValue pos = field(field(exp, "tcp", {}), "position", {});
        const tk::JsonValue rot = field(field(exp, "tcp", {}), "rotation", {});
        // TCP 位置（m——档案 appendixD-fixed 第 4 项、数据集更严 1e-12）。
        IRD_EXPECT_CLOSE(pfx + ".tcp.position.x", r.get().tcpInBase.P()[0],
                         num(pos, "x"), *profile, "m");
        IRD_EXPECT_CLOSE(pfx + ".tcp.position.y", r.get().tcpInBase.P()[1],
                         num(pos, "y"), *profile, "m");
        IRD_EXPECT_CLOSE(pfx + ".tcp.position.z", r.get().tcpInBase.P()[2],
                         num(pos, "z"), *profile, "m");
        // 旋转矩阵九元素（无量纲——行列序与黄金声明一致）。
        const char* elems[3][3] = {
            {"xx", "xy", "xz"}, {"yx", "yy", "yz"}, {"zx", "zy", "zz"}};
        for (int ri = 0; ri < 3; ++ri) {
            for (int cj = 0; cj < 3; ++cj) {
                IRD_EXPECT_CLOSE(pfx + ".tcp.rotation." + elems[ri][cj],
                                 r.get().tcpInBase.R()(static_cast<std::size_t>(ri),
                                                       static_cast<std::size_t>(cj)),
                                 num(rot, elems[ri][cj]), *profile, "1");
            }
        }
        // 雅可比（产品行优先 6·n：前 3n＝平移行 m、后 3n＝转动行无量纲
        // ——D-KIN-2 分族对照）。
        const tk::JsonValue jac = field(exp, "jacobian", {});
        const std::size_t n = q.size();
        std::vector<double> actualTrans(r.get().jacobian.begin(),
                                        r.get().jacobian.begin() + 3 * n);
        std::vector<double> actualRot(r.get().jacobian.begin() + 3 * n,
                                      r.get().jacobian.end());
        IRD_EXPECT_ALL_CLOSE(pfx + ".jacobian.trans[*]", actualTrans,
                             numVec(jac, "trans"), *profile, "m");
        IRD_EXPECT_ALL_CLOSE(pfx + ".jacobian.rot[*]", actualRot,
                             numVec(jac, "rot"), *profile, "1");
        // 指标（附录 D 第 9 项解析对照——相对 1e-9＋逐例 1e-12）。
        const tk::JsonValue met = field(exp, "metrics", {});
        IRD_EXPECT_CLOSE(pfx + ".metrics.manipulability", r.get().manipulability,
                         num(met, "manipulability"), *profile, "1");
        // 奇异位形（黄金 null——σmin≈0 的 0/0 型导出量无第 9 项意义：
        // 参考精确零 vs 产品 1e-17 级噪声比值不可比）跳过比值断言，
        // 奇异事实经 singularValues（含 σmin≈0）断言承载。
        const tk::JsonValue* goldenCond = met.find("conditionNumber");
        ASSERT_NE(goldenCond, nullptr);
        if (!goldenCond->isNull()) {
            IRD_EXPECT_CLOSE(pfx + ".metrics.conditionNumber", r.get().conditionNumber,
                             goldenCond->number, *profile, "1");
        }
        IRD_EXPECT_ALL_CLOSE(pfx + ".metrics.singularValues[*]", r.get().singularValues,
                             numVec(met, "singularValues"), *profile, "1");
    }
}

/// V-01 正反变换：FK(q)→平面 2R 位置闭式逆（双分支）→FK 复算——位置
/// 残差 ≤ 黄金声明的闭环容差（1e-6 m——去重容差语境，附录 D 第 3 项）。
TEST_F(KinGoldenFk, V01_ForwardInverseClosedLoop_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "KIN-02"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/fk-cases.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/fk-expected.json", false);
    const tk::JsonValue rtG = field(expected, "roundTrip", tk::JsonValue{});
    const tk::JsonValue models = field(inputs, "models", tk::JsonValue{});
    const tk::JsonValue inCases = field(inputs, "cases", tk::JsonValue{});

    // roundTrip.caseId → inputs 侧算例（模型引用）。
    const std::string caseId = rtG.find("caseId")->text;
    const tk::JsonValue* inCase = nullptr;
    for (const auto& c : inCases.items) {
        if (c.find("id")->text == caseId) { inCase = &c; break; }
    }
    ASSERT_NE(inCase, nullptr) << "roundTrip 引用的算例不存在";
    const rt::CanonicalModel model = makeGoldenModel(*models.find(inCase->find("model")->text));
    TestView view(model);
    const kin::FkEvaluator fk;
    const TcpRef tcp{idFrom<core::ObjectId>("kin-tool"), ""};

    // 正向：FK(forwardQ) 的 TCP 位置与黄金闭式值逐轴容差内（1e-12）。
    const auto forwardQ = numVec(rtG, "forwardQ");
    const auto fwd = fk.evaluate(view, tcp, forwardQ);
    ASSERT_TRUE(fwd.ok());
    const tk::JsonValue refPos = field(rtG, "tcpPosition", {});
    const double refX = num(refPos, "x");
    const double refY = num(refPos, "y");
    IRD_EXPECT_AT_MOST("roundtrip.forward.position.x",
                       std::fabs(fwd.get().tcpInBase.P()[0] - refX), 1e-12, "m");
    IRD_EXPECT_AT_MOST("roundtrip.forward.position.y",
                       std::fabs(fwd.get().tcpInBase.P()[1] - refY), 1e-12, "m");

    // 反向→复算：双分支闭式逆各自 FK 复算，位置回到黄金值（残差 ≤ 1e-6
    // m——正反变换闭环含于去重容差，V-01 预期结果列）。
    double worst = 0.0;
    for (const auto& b : field(rtG, "inverseBranches", tk::JsonValue{}).items) {
        const auto q = numVec(b, nullptr);
        ASSERT_EQ(q.size(), forwardQ.size());
        const auto rec = fk.evaluate(view, tcp, q);
        ASSERT_TRUE(rec.ok());
        worst = std::max(worst, std::hypot(rec.get().tcpInBase.P()[0] - refX,
                                           rec.get().tcpInBase.P()[1] - refY));
    }
    IRD_EXPECT_AT_MOST("roundtrip.closedLoop.position", worst,
                       num(rtG, "closedLoopToleranceM"), "m");
}

/// 边角样例载体（附录 D C4——零值/近零/正负抵消；acceptance 1 的
/// edgeCases 黄金断言面）：零位解析常数、近零位级响应、正负抵消恒等姿态。
TEST_F(KinGoldenFk, V01_EdgeCaseCarriersZeroNearCancellation_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-03"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue expected = loadJson(*dataset, "expected/fk-expected.json", false);
    const tk::JsonValue& expCases = field(expected, "cases", tk::JsonValue{});
    ASSERT_GE(expCases.items.size(), 4U);
    // cases[0]=fk-zero（零位）：TCP 位置解析常数 (1.8,0,0)、姿态恒等。
    const tk::JsonValue& zero = expCases.items[0];
    IRD_EXPECT_IDENTICAL("fk[0].caseId", zero.find("caseId")->text, std::string{"fk-zero"});
    IRD_EXPECT_CLOSE("fk[0].tcp.position.x", 1.8,
                     num(field(field(zero, "tcp", {}), "position", {}), "x"), *profile, "m");
    IRD_EXPECT_CLOSE("fk[0].tcp.position.y", 0.0,
                     num(field(field(zero, "tcp", {}), "position", {}), "y"), *profile, "m");
    // 近零算例（cases[1]=fk-nearzero）：1e-9 rad 输入的 y 响应恰为近零量
    // （位级可观测——不静默截断，NFR-COR-03）。
    const tk::JsonValue& nearZero = expCases.items[1];
    const double yNear = num(field(field(nearZero, "tcp", {}), "position", {}), "y");
    IRD_EXPECT_CLOSE("fk[1].tcp.position.y", 1.1e-9, yNear, *profile, "m");
    EXPECT_GT(yNear, 0.0) << "近零输入的位置响应应为正（截断即静默）";
    // 正负抵消算例（cases[2]=fk-cancellation）：q1+q2=0 → 旋转非对角元
    // 为零（正负抵消语义的黄金面）。
    const tk::JsonValue& cancel = expCases.items[2];
    IRD_EXPECT_CLOSE("fk[2].tcp.rotation.xy", 0.0,
                     num(field(field(cancel, "tcp", {}), "rotation", {}), "xy"),
                     *profile, "1");
    IRD_EXPECT_CLOSE("fk[2].tcp.rotation.yx", 0.0,
                     num(field(field(cancel, "tcp", {}), "rotation", {}), "yx"),
                     *profile, "1");
}

/// 确定性（acceptance 5——V-04 逐位一致语义的 FK 面载体）：同输入重复
/// 评估的 canonical 编码输出逐位一致。
TEST_F(KinGoldenFk, ACC5_RepeatEvaluateCanonicalBitwise_WP15T13_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/fk-cases.json", true);
    const rt::CanonicalModel model = makeGoldenModel(
        *field(inputs, "models", {}).find("planar-2r"));
    TestView view(model);
    const kin::FkEvaluator fk;
    const TcpRef tcp{idFrom<core::ObjectId>("kin-tool"), ""};
    const auto a = fk.evaluate(view, tcp, {0.6, 0.9});
    const auto b = fk.evaluate(view, tcp, {0.6, 0.9});
    ASSERT_TRUE(a.ok() && b.ok());
    const auto bytesA = kin::encodePoseMetricsCanonical(a.get());
    const auto bytesB = kin::encodePoseMetricsCanonical(b.get());
    ASSERT_EQ(bytesA.size(), bytesB.size());
    for (std::size_t i = 0; i < bytesA.size(); ++i) {
        ASSERT_EQ(bytesA[i], bytesB[i]) << "canonical 输出第 " << i << " 字节不一致";
    }
}

// =====================================================================
// 2) kin-ik-dedup——V-04 去重/稳定排序黄金（AT-03 去重反例）
// =====================================================================

class KinGoldenIkDedup : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override { return {"kin-ik-dedup", "1.0.0"}; }

    /// 黄金请求组装（inputs.request 全字段——真实 IkSolver 驱动）。
    kin::IkRequest makeRequest(const rt::CanonicalModel& model, TestView& view)
    {
        const tk::JsonValue inputs = loadJson(*dataset, "inputs/ik-dedup-inputs.json", true);
        const tk::JsonValue req = field(inputs, "request", {});
        const tk::JsonValue target = field(req, "targetPose", {});
        kin::IkRequest r;
        r.targetInBase = goldenTarget(num(target, "x"), num(target, "y"), num(target, "phi"));
        r.modelView = &view;
        r.tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
        for (const auto& init : field(req, "initialValues", {}).items) {
            r.initialValues.push_back(numVec(init, nullptr));
        }
        r.iterationLimit = static_cast<std::uint32_t>(num(req, "iterationLimit"));
        r.intervals = kin::evaluationIntervals(view);
        r.referenceQ = numVec(req, "referenceQ");
        r.targetRef.pointOid = idFrom<core::ObjectId>("kin-point-1");
        r.requestIdentity.snapshotId.bytes = digestOf("kin-golden-snap");
        r.requestIdentity.sliceId.bytes = digestOf("kin-golden-slice");
        return r;
    }
};

/// V-04 主面：真实 IkSolver（黄金初值）→ 解集与闭式参考经档案容差一一
/// 配对等价（checkSetEquivalent——TK-T06 设施；AT-03 去重反例：同末端
/// 位姿两构型均保留）＋四计数统计黄金。
TEST_F(KinGoldenIkDedup, V04_SolutionSetEquivalentToClosedForm_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"}, std::vector<std::string>{"AT-03"},
                  datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/ik-dedup-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/ik-dedup-expected.json", false);
    const rt::CanonicalModel model = makeGoldenModel(field(inputs, "model", {}));
    TestView view(model);
    const kin::IkOutcome out = kin::IkSolver().solve(makeRequest(model, view));
    ASSERT_EQ(out.outcomeKind, kin::IkOutcomeKind::SolutionsFound);

    // 解集与闭式双分支参考：集合等价（TK-T06 两阶段匹配——全部声明轴经
    // 档案容差满足才构成配对）。
    std::vector<std::vector<double>> actualQs;
    for (const auto& s : out.solutionSet.solutions) { actualQs.push_back(s.q); }
    std::vector<std::vector<double>> referenceQs;
    for (const auto& s : field(expected, "solutions", {}).items) {
        referenceQs.push_back(qOf(s));
    }
    const tk::SetCheckResult setR =
        tk::checkSetEquivalent(referenceQs, actualQs, *profile, tk::SetMatchTraits<std::vector<double>>{});
    ASSERT_TRUE(setR.equivalent)
        << "解集与闭式参考不等价：missing=" << setR.missingExpected.size()
        << " extra=" << setR.extraActual.size();
    // 歧义（ambiguousMatch=true）不判失败——TK-T06 §5.4.4：多完美匹配只
    // 报告不改判（数值解与闭式参考各在容差内时配对可交换，等价性不变）。

    // AT-03 反例黄金面：同末端位姿两构型均保留（去重不吞并）。
    IRD_EXPECT_IDENTICAL("dedup.retained", std::to_string(actualQs.size()),
                         std::to_string(static_cast<int>(
                             num(field(expected, "asserts", {}), "distinctConfigurationsRetained"))));

    // 四计数统计黄金（raw=converged=deduped=2、filtered=0）。
    const tk::JsonValue stat = field(expected, "statistics", {});
    IRD_EXPECT_IDENTICAL("statistics.rawCount",
                         std::to_string(out.solutionSet.statistics.rawCount),
                         std::to_string(static_cast<int>(num(stat, "rawCount"))));
    IRD_EXPECT_IDENTICAL("statistics.convergedCount",
                         std::to_string(out.solutionSet.statistics.convergedCount),
                         std::to_string(static_cast<int>(num(stat, "convergedCount"))));
    IRD_EXPECT_IDENTICAL("statistics.dedupedCount",
                         std::to_string(out.solutionSet.statistics.dedupedCount),
                         std::to_string(static_cast<int>(num(stat, "dedupedCount"))));
    IRD_EXPECT_IDENTICAL("statistics.filteredCount",
                         std::to_string(out.solutionSet.statistics.filteredCount),
                         std::to_string(static_cast<int>(num(stat, "filteredCount"))));
}

/// V-04 稳定排序：实际解序与黄金参考序（§6.3 四键闭式复算）一致
/// （checkStableOrder——TK-T06 顺序设施）。
TEST_F(KinGoldenIkDedup, V04_StableOrderMatchesReferenceKeys_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-02"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/ik-dedup-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/ik-dedup-expected.json", false);
    const rt::CanonicalModel model = makeGoldenModel(field(inputs, "model", {}));
    TestView view(model);
    const kin::IkOutcome out = kin::IkSolver().solve(makeRequest(model, view));
    ASSERT_EQ(out.outcomeKind, kin::IkOutcomeKind::SolutionsFound);

    // 期望序（expectedOrder 索引 → solutions[i].q——参考四键排序结果）。
    const auto refSols = field(expected, "solutions", {}).items;
    std::vector<std::vector<double>> expectedOrderQs;
    for (const auto& idx : field(expected, "expectedOrder", {}).items) {
        expectedOrderQs.push_back(qOf(refSols[static_cast<std::size_t>(idx.number)]));
    }
    std::vector<std::vector<double>> actualOrderQs;
    for (const auto& s : out.solutionSet.solutions) { actualOrderQs.push_back(s.q); }

    const tk::OrderCheckResult orderR =
        tk::checkStableOrder(expectedOrderQs, actualOrderQs,
                             tk::SetMatchTraits<std::vector<double>>{}, *profile);
    ASSERT_TRUE(orderR.sameOrder)
        << "稳定排序与 §6.3 四键参考序不符（首错位 " << orderR.firstDivergence << "）";
}

/// 确定性终验（acceptance 5——V-04 逐位一致）：同输入重复求解两次输出
/// 逐位一致（位模式比较——无容差语义）。
TEST_F(KinGoldenIkDedup, ACC5_RepeatSolveBitwiseIdentical_WP15T13_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-03"},
                  datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/ik-dedup-inputs.json", true);
    const rt::CanonicalModel model = makeGoldenModel(field(inputs, "model", {}));
    TestView view(model);
    const kin::IkRequest request = makeRequest(model, view);
    const kin::IkOutcome a = kin::IkSolver().solve(request);
    const kin::IkOutcome b = kin::IkSolver().solve(request);
    ASSERT_EQ(a.solutionSet.solutions.size(), b.solutionSet.solutions.size());
    for (std::size_t i = 0; i < a.solutionSet.solutions.size(); ++i) {
        EXPECT_TRUE(bitwiseEqVec(a.solutionSet.solutions[i].q, b.solutionSet.solutions[i].q))
            << "第 " << i << " 解两次输出非逐位一致（V-04）";
        EXPECT_TRUE(bitwiseEq(a.solutionSet.solutions[i].positionResidual,
                              b.solutionSet.solutions[i].positionResidual));
        EXPECT_TRUE(bitwiseEq(a.solutionSet.solutions[i].manipulability,
                              b.solutionSet.solutions[i].manipulability));
        EXPECT_EQ(a.solutionSet.solutions[i].signature, b.solutionSet.solutions[i].signature);
    }
}

// =====================================================================
// 3) kin-search-outcomes——五类结局黄金＋V-07 换初值反例
//    （AT-03 反例组：V-05/06/07/08/09）
// =====================================================================

class KinGoldenOutcomes : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override { return {"kin-search-outcomes", "1.0.0"}; }

    /// 场景求解（黄金 inputs 场景 → 产品 IkRequest＋脚本碰撞替身）。
    kin::IkOutcome solveScenario(const tk::JsonValue& models,
                                 const tk::JsonValue& scenario,
                                 kin::IkCollisionEvaluationState* stateOut = nullptr)
    {
        const rt::CanonicalModel model = makeGoldenModel(
            *models.find(scenario.find("model")->text));
        TestView view(model);
        const tk::JsonValue target = field(scenario, "target", {});
        kin::IkRequest r;
        r.targetInBase = goldenTarget(num(target, "x"), num(target, "y"), num(target, "phi"));
        r.modelView = &view;
        r.tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
        r.positionTolerance = 1e-6;      // m（附录 D 第 1 项默认）
        r.orientationTolerance = 1e-6;   // rad（第 2 项默认）
        r.dedupThresholdPerAxis = 1e-6;  // rad 逐轴（第 3 项默认）
        const tk::JsonValue* init = scenario.find("initialValues");
        if (init == nullptr) {
            ADD_FAILURE() << "黄金场景缺 initialValues";
            return kin::IkOutcome{};
        }
        if (init->isArray()) {
            for (const auto& v : init->items) { r.initialValues.push_back(numVec(v, nullptr)); }
        } else {
            // 策略物化（黄金声明 {strategy,count,seed}——makeInitialValues
            // 唯一实现点消费；策略 token 与产品词表对齐）。
            const std::string strategy = init->find("strategy")->text;
            const auto count = static_cast<std::uint32_t>(num(*init, "count"));
            const auto seed = static_cast<std::uint64_t>(num(*init, "seed"));
            r.intervals = kin::evaluationIntervals(view);
            r.referenceQ.assign(r.intervals.size(), 0.0);
            if (strategy != "JointGrid") {
                ADD_FAILURE() << "黄金策略词表扩展须同步本映射: " << strategy;
                return kin::IkOutcome{};
            }
            r.initialValues = kin::makeInitialValues(kin::InitialValueStrategy::JointGrid,
                                                     count, seed, r.intervals, r.referenceQ);
        }
        r.iterationLimit = static_cast<std::uint32_t>(num(scenario, "iterationLimit"));
        r.intervals = kin::evaluationIntervals(view);
        r.referenceQ.assign(r.intervals.size(), 0.0);
        r.targetRef.pointOid = idFrom<core::ObjectId>("kin-point-1");
        r.requestIdentity.snapshotId.bytes = digestOf("kin-golden-snap");
        r.requestIdentity.sliceId.bytes = digestOf("kin-golden-slice");
        // 脚本碰撞替身（token → 谓词；生命周期覆盖 solve 调用）。
        m_session = scriptedSession(scenario.find("collision")->text);
        r.collisionSession = m_session.get();
        return kin::IkSolver().solve(r);
    }

    std::unique_ptr<ScriptedCollisionSession> m_session;   ///< 场景内替身生命周期
};

/// V-05＋V-06 黄金：有效解收敛（残差≤容差）与多初值未收敛（搜索记录
/// ＋无不可行）——五类结局黄金面（前两结局）。
TEST_F(KinGoldenOutcomes, V05_V06_FoundAndNoConvergence_WP15T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/search-outcomes-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/search-outcomes-expected.json", false);
    const tk::JsonValue models = field(inputs, "models", {});
    const tk::JsonValue scenarios = field(inputs, "scenarios", {});
    const tk::JsonValue expScen = field(expected, "scenarios", {});
    const auto solveAndCheck = [&](const std::string& id) {
        const tk::JsonValue* scn = nullptr;
        for (const auto& s : scenarios.items) {
            if (s.find("id")->text == id) { scn = &s; break; }
        }
        const tk::JsonValue* exp = expScen.find(id);
        EXPECT_NE(scn, nullptr) << "黄金场景缺失: " << id;
        EXPECT_NE(exp, nullptr) << "黄金期望缺失: " << id;
        if (scn == nullptr || exp == nullptr) { return; }
        SCOPED_TRACE("黄金场景: " + id);
        const kin::IkOutcome out = solveScenario(models, *scn);
        // 结局 token 精确匹配（枚举词表——附录 D 第 12 项语义）。
        const std::string token = exp->find("outcome")->text;
        kin::IkOutcomeKind want = kin::IkOutcomeKind::SolutionsFound;
        if (token == "MultiInitNoConvergence") { want = kin::IkOutcomeKind::MultiInitNoConvergence; }
        else if (token == "AllCandidatesFiltered") { want = kin::IkOutcomeKind::AllCandidatesFiltered; }
        else if (token == "PartialCollision") { want = kin::IkOutcomeKind::PartialCollision; }
        else if (token == "AnalyticBoundExceeded") { want = kin::IkOutcomeKind::AnalyticBoundExceeded; }
        ASSERT_EQ(out.outcomeKind, want) << "结局与黄金不符";
        EXPECT_EQ(out.cancelled, false);
        // 期望解数/搜索记录/证明素材三形态面。
        if (exp->find("solutions") != nullptr) {
            IRD_EXPECT_IDENTICAL("outcome.solutions",
                                 std::to_string(out.solutionSet.solutions.size()),
                                 std::to_string(static_cast<int>(num(*exp, "solutions"))));
        }
        if (exp->find("minSolutions") != nullptr) {
            EXPECT_GE(out.solutionSet.solutions.size(),
                      static_cast<std::size_t>(num(*exp, "minSolutions")));
        }
        ASSERT_EQ(exp->find("hasProof") != nullptr, true);
        EXPECT_EQ(out.proofMaterial.has_value(), exp->find("hasProof")->boolean)
            << "证明素材形态与黄金不符（结局 2/3/4 不得携带 proof）";
        EXPECT_EQ(out.solutionSet.searchRecord.has_value(),
                  exp->find("hasSearchRecord")->boolean);
        if (exp->find("initialGuessesTried") != nullptr && out.solutionSet.searchRecord) {
            IRD_EXPECT_IDENTICAL("searchRecord.initialGuessesTried",
                                 std::to_string(out.solutionSet.searchRecord->initialGuessesTried),
                                 std::to_string(static_cast<int>(num(*exp, "initialGuessesTried"))));
        }
        // 有效解场景：首解残差 ≤ 两容差（V-05 预期结果列）。
        if (exp->find("assertResidualWithinTolerance") != nullptr
            && exp->find("assertResidualWithinTolerance")->boolean
            && !out.solutionSet.solutions.empty()) {
            EXPECT_LE(out.solutionSet.solutions.front().positionResidual, 1e-6);
            EXPECT_LE(out.solutionSet.solutions.front().orientationResidual, 1e-6);
        }
        return;
    };
    solveAndCheck("s1-found");
    solveAndCheck("s2-noconv");
}

/// V-08＋V-09 黄金：脚本化碰撞替身驱动——一构型碰撞另一构型有效
/// （PartialCollision）与全部候选过滤（AllCandidatesFiltered，不得输出
/// 不可行）。
TEST_F(KinGoldenOutcomes, V08_V09_CollisionScriptOutcomes_WP15T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-05"},
                  std::vector<std::string>{"AT-03", "AT-19"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/search-outcomes-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/search-outcomes-expected.json", false);
    const tk::JsonValue models = field(inputs, "models", {});
    const tk::JsonValue scenarios = field(inputs, "scenarios", {});
    const tk::JsonValue expScen = field(expected, "scenarios", {});

    const auto run = [&](const std::string& id) {
        const tk::JsonValue* scn = nullptr;
        for (const auto& s : scenarios.items) {
            if (s.find("id")->text == id) { scn = &s; break; }
        }
        const tk::JsonValue* exp = expScen.find(id);
        ASSERT_NE(scn, nullptr);
        ASSERT_NE(exp, nullptr);
        SCOPED_TRACE("黄金场景: " + id);
        const kin::IkOutcome out = solveScenario(models, *scn);
        // 结局 3：AllCandidatesFiltered（全部碰撞过滤——DataInsufficient
        // 素材，不得输出不可行）；结局 4：PartialCollision（1 的子形态）。
        const std::string token = exp->find("outcome")->text;
        const kin::IkOutcomeKind want = token == "AllCandidatesFiltered"
                                            ? kin::IkOutcomeKind::AllCandidatesFiltered
                                            : kin::IkOutcomeKind::PartialCollision;
        ASSERT_EQ(out.outcomeKind, want);
        EXPECT_FALSE(out.proofMaterial.has_value())
            << "碰撞结局不得携带解析界限证明素材（铁律：构型级碰撞非不可行）";
        // 过滤记录黄金（filteredCount＋原因全 collision——硬过滤③）。
        if (exp->find("filteredCount") != nullptr) {
            IRD_EXPECT_IDENTICAL("filtered.count",
                                 std::to_string(out.solutionSet.filteredRecords.size()),
                                 std::to_string(static_cast<int>(num(*exp, "filteredCount"))));
        }
        if (exp->find("filterReasons") != nullptr) {
            const auto& reasons = exp->find("filterReasons")->items;
            ASSERT_EQ(reasons.size(), out.solutionSet.filteredRecords.size());
            for (std::size_t i = 0; i < reasons.size(); ++i) {
                const auto actual = out.solutionSet.filteredRecords[i].reason;
                const bool isCollision = actual == kin::SolutionFilterReason::Collision;
                EXPECT_TRUE(isCollision) << "第 " << i << " 条过滤原因应为 collision";
                IRD_EXPECT_IDENTICAL("filtered.reason[" + std::to_string(i) + "]",
                                     std::string{isCollision ? "collision" : "other"},
                                     reasons[i].text);
            }
        }
    };
    run("s3-allfilt");
    run("s4-partial");
}

/// 结局 5 黄金（V-04 组/解析界限）：目标超解析界限——唯一产证明素材的
/// 路径；solutions 空且无搜索记录（静态检查先于迭代）。
TEST_F(KinGoldenOutcomes, V04Group_AnalyticBoundOutcome_WP15T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/search-outcomes-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/search-outcomes-expected.json", false);
    const tk::JsonValue models = field(inputs, "models", {});
    const tk::JsonValue scenarios = field(inputs, "scenarios", {});
    const tk::JsonValue expScen = field(expected, "scenarios", {});

    const tk::JsonValue* scn = nullptr;
    for (const auto& s : scenarios.items) {
        if (s.find("id")->text == "s5-bound") { scn = &s; break; }
    }
    const tk::JsonValue* exp = expScen.find("s5-bound");
    ASSERT_NE(scn, nullptr);
    ASSERT_NE(exp, nullptr);
    const kin::IkOutcome out = solveScenario(models, *scn);
    ASSERT_EQ(out.outcomeKind, kin::IkOutcomeKind::AnalyticBoundExceeded);
    EXPECT_TRUE(out.proofMaterial.has_value()) << "结局 5 必附解析界限证明素材";
    EXPECT_TRUE(out.solutionSet.solutions.empty());
    EXPECT_FALSE(out.solutionSet.searchRecord.has_value())
        << "结局 5 先于迭代——无搜索未果记录";
    // 证明素材绑定（expectedSliceId＝请求切片——§5.6 绑定面；
    // sliceId 经 proof 载体承载——AnalyticBoundMaterial{bound, distance,
    // proof}）。
    EXPECT_EQ(out.proofMaterial->proof.sliceId.bytes, digestOf("kin-golden-slice"));
}

/// V-07 黄金反例对：原初值全未收敛（小预算）→扩大初值后同冻结输入复评
/// 翻转为有效解——搜索未果≠不可行（AT-03 换初值反例）。
TEST_F(KinGoldenOutcomes, V07_RestartFlipGoldenPair_WP15T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "EVI-01"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/search-outcomes-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/search-outcomes-expected.json", false);
    const tk::JsonValue models = field(inputs, "models", {});
    const tk::JsonValue scenarios = field(inputs, "scenarios", {});
    const tk::JsonValue expScen = field(expected, "scenarios", {});

    const auto findScn = [&](const std::string& id) {
        for (const auto& s : scenarios.items) {
            if (s.find("id")->text == id) { return s; }
        }
        ADD_FAILURE() << "黄金场景缺失: " << id;
        return tk::JsonValue{};
    };
    // 前半：单一远离解初值＋迭代上限 1 → MultiInitNoConvergence。
    const kin::IkOutcome narrow = solveScenario(models, findScn("s7a-restart-narrow"));
    ASSERT_EQ(narrow.outcomeKind, kin::IkOutcomeKind::MultiInitNoConvergence);
    ASSERT_TRUE(narrow.solutionSet.searchRecord.has_value());
    IRD_EXPECT_IDENTICAL("s7a.guesses",
                         std::to_string(narrow.solutionSet.searchRecord->initialGuessesTried),
                         std::to_string(static_cast<int>(
                             num(*expScen.find("s7a-restart-narrow"), "initialGuessesTried"))));
    EXPECT_FALSE(narrow.proofMaterial.has_value()) << "搜索未果不得携带不可行素材";
    // 后半：同一冻结输入（同目标/容差/上限）扩大初值集 → SolutionsFound。
    const kin::IkOutcome expanded = solveScenario(models, findScn("s7b-restart-expanded"));
    ASSERT_EQ(expanded.outcomeKind, kin::IkOutcomeKind::SolutionsFound);
    ASSERT_FALSE(expanded.solutionSet.solutions.empty());
    EXPECT_LE(expanded.solutionSet.solutions.front().positionResidual, 1e-6);
    EXPECT_LE(expanded.solutionSet.solutions.front().orientationResidual, 1e-6);
}

// =====================================================================
// 4) kin-coverage——V-13/V-14 覆盖固定计数黄金＋V-21 线程等价终验
//    （AT-03 覆盖反例组）
// =====================================================================

class KinGoldenCoverage : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override { return {"kin-coverage", "1.0.0"}; }

    /// 黄金计划 → 产品 SamplingPlan 投影（盒 center=min+size/2 换算按
    /// RegionBox 契约；计数/碰撞要求逐字段搬运）。
    kin::SamplingPlan toPlan(const tk::JsonValue& planJson, const std::string& seed)
    {
        kin::SamplingPlan plan;
        plan.regionObjectId = idFrom<core::ObjectId>("kin-rg-" + seed);
        core::ContentIdentity pid;
        pid.bytes = digestOf("kin-rp-" + seed);
        plan.planContentIdentity = pid;
        const tk::JsonValue box = field(planJson, "box", {});
        const auto mn = numVec(box, "min");
        const auto sz = numVec(box, "size");
        plan.box.center = rw::math::Vector3D<double>(mn[0] + sz[0] / 2, mn[1] + sz[1] / 2,
                                                     mn[2] + sz[2] / 2);
        plan.box.size = rw::math::Vector3D<double>(sz[0], sz[1], sz[2]);
        plan.position.method = kin::PositionSamplingDefinition::Method::Grid;
        const auto counts = numVec(planJson, "counts");
        plan.position.gridCounts = {static_cast<std::uint32_t>(counts[0]),
                                    static_cast<std::uint32_t>(counts[1]),
                                    static_cast<std::uint32_t>(counts[2])};
        plan.orientation.directionSamples = 1U;
        plan.orientation.rollSamples = 1U;
        const bool collReq = planJson.find("demands") != nullptr
                                 && planJson.find("demands")->find("collisionFreeRequired") != nullptr
                                 && planJson.find("demands")->find("collisionFreeRequired")->boolean;
        plan.demands.collisionFreeRequired = collReq;
        return plan;
    }

    /// 按黄金 variants 计划 id 列表装配查询＋请求（真实内置求解器——
    /// solver 空＝生产路径）。前置：modelHold/view 已由调用方以黄金模型
    /// 构造（view 值持有模型副本——装配只读视图）。
    void assemble(const std::vector<std::string>& planIds, std::uint32_t threadCount,
                  const rt::CanonicalModel& modelHold, const TestView& view,
                  kin::RegionCoverageQuery& q, ev::EvaluationRequest& req)
    {
        const tk::JsonValue inputs = loadJson(*dataset, "inputs/coverage-inputs.json", true);
        std::vector<kin::SamplingPlan> plans;
        for (const auto& id : planIds) {
            for (const auto& p : field(inputs, "plans", {}).items) {
                if (p.find("id")->text == id) { plans.push_back(toPlan(p, id)); }
            }
        }
        ASSERT_EQ(plans.size(), planIds.size()) << "黄金计划 id 未全部命中";
        kin::RegionSamplingBudget budget;
        budget.seed = 20260909U;      // 确定性种子（非 0——I-KIN-4）
        budget.threadCount = 1U;
        q.plans = plans;
        q.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
        q.referenceQ.assign(plans.empty() ? 2U : kin::evaluationIntervals(view).size(), 0.0);
        q.initialStrategy = kin::InitialValueStrategy::SeededRandom;
        q.initialValuesCount = 24U;    // 多初值（种子派生确定性序列）降局部极小
        q.iterationLimit = 400U;
        q.positionTolerance = 1e-6;
        q.orientationTolerance = 1e-6;
        q.dedupThresholdPerAxis = 1e-6;
        q.budget = budget;
        q.threadCount = threadCount;
        // 快照冻结凭据（对账 matched 前置——与评估器重算同源）。
        std::vector<ev::SamplingPlanRef> refs;
        for (const auto& p : plans) {
            ev::SamplingPlanRef ref;
            ref.regionObjectId = p.regionObjectId;
            ref.planContentIdentity = p.planContentIdentity;
            ref.plannedPositionSamples = kin::plannedPositionSampleCount(p);
            ref.plannedPoseSamples = kin::plannedPoseSampleCount(p);
            ref.sampleSetIdentity = kin::sampleSetIdentity(p.planContentIdentity, budget);
            refs.push_back(ref);
        }
        req.mode = core::EvaluationMode::Verified;
        req.snapshot.snapshotId.bytes = digestOf("kin-cov-snap");
        req.snapshot.samplingPlans = refs;
        req.slice.sliceId.bytes = digestOf("kin-cov-slice");
        req.task.attempt.value = 3U;
    }
};

/// V-14 前半（AT-03 固定计数样例）：计划 100、可达 60、不可达 40——
/// 覆盖计数黄金（computeCoverage 产物；位置/姿态双口径分离）。
TEST_F(KinGoldenCoverage, V14_FixedCount100of60_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue expected = loadJson(*dataset, "expected/coverage-expected.json", false);
    const tk::JsonValue golden = field(expected, "fixedCount", {});

    const rt::CanonicalModel modelHold = makeGoldenModel(field(
        loadJson(*dataset, "inputs/coverage-inputs.json", true), "model", {}));
    TestView view{modelHold};
    kin::RegionCoverageQuery q;
    ev::EvaluationRequest req;
    assemble({"planA-100"}, 1U, modelHold, view, q, req);
    NoopContext ctx;
    const kin::RegionCoverageComputation comp =
        kin::runRegionCoverageComputation(view, q, req, ctx);

    // 位置轴（AT-03 固定计数主样例）：计划 100、可达 60、不可达 40。
    const tk::JsonValue posAxis = field(golden, "positionAxis", {});
    IRD_EXPECT_IDENTICAL("coverage.position.planned",
                         std::to_string(comp.coverage.position.planned),
                         std::to_string(static_cast<int>(num(posAxis, "planned"))));
    IRD_EXPECT_IDENTICAL("coverage.position.reached",
                         std::to_string(comp.coverage.position.reached),
                         std::to_string(static_cast<int>(num(posAxis, "reached"))));
    IRD_EXPECT_IDENTICAL("coverage.position.unreachable",
                         std::to_string(comp.coverage.position.unreachable),
                         std::to_string(static_cast<int>(num(posAxis, "unreachable"))));
    IRD_EXPECT_IDENTICAL("coverage.position.dataInsufficient",
                         std::to_string(comp.coverage.position.dataInsufficient),
                         std::to_string(static_cast<int>(num(posAxis, "dataInsufficient"))));
    EXPECT_TRUE(comp.coverage.positionDefined);
    // 位姿轴（载体的结构性事实：斐波那契单方向 +X 对纯 z 关节臂的转动
    // 自由度不可达——界内位姿样本恒 DataInsufficient、超界随解析界限
    // Unreachable；黄金如实录面，AT-03 混合三态并存）。
    const tk::JsonValue ornAxis = field(golden, "orientationAxis", {});
    IRD_EXPECT_IDENTICAL("coverage.orientation.planned",
                         std::to_string(comp.coverage.orientation.planned),
                         std::to_string(static_cast<int>(num(ornAxis, "planned"))));
    IRD_EXPECT_IDENTICAL("coverage.orientation.reached",
                         std::to_string(comp.coverage.orientation.reached),
                         std::to_string(static_cast<int>(num(ornAxis, "reached"))));
    IRD_EXPECT_IDENTICAL("coverage.orientation.dataInsufficient",
                         std::to_string(comp.coverage.orientation.dataInsufficient),
                         std::to_string(static_cast<int>(num(ornAxis, "dataInsufficient"))));
    EXPECT_TRUE(comp.coverage.downgraded)
        << "位姿轴结构性数据不足在场 → 整体降级标记（判定归 evidence）";
}

/// V-14 后半（AT-03 数据不足变体）：碰撞要求在场＋检测器缺失 → 4 样本
/// DataInsufficient、分母 104 保留不计分子、整体降级。
TEST_F(KinGoldenCoverage, V14_InsufficientKeepsDenominator_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue expected = loadJson(*dataset, "expected/coverage-expected.json", false);
    const tk::JsonValue golden = field(expected, "insufficientDenominator", {});

    rt::CanonicalModel modelHold{makeGoldenModel(field(
        loadJson(*dataset, "inputs/coverage-inputs.json", true), "model", {}))};
    TestView view{modelHold};
    kin::RegionCoverageQuery q;
    ev::EvaluationRequest req;
    assemble({"planA-100", "planD-4-collision-demand"}, 1U, modelHold, view, q, req);
    NoopContext ctx;
    const kin::RegionCoverageComputation comp =
        kin::runRegionCoverageComputation(view, q, req, ctx);

    IRD_EXPECT_IDENTICAL("coverage.position.planned",
                         std::to_string(comp.coverage.position.planned),
                         std::to_string(static_cast<int>(num(golden, "planned"))));
    IRD_EXPECT_IDENTICAL("coverage.position.reached",
                         std::to_string(comp.coverage.position.reached),
                         std::to_string(static_cast<int>(num(golden, "reached"))));
    IRD_EXPECT_IDENTICAL("coverage.position.unreachable",
                         std::to_string(comp.coverage.position.unreachable),
                         std::to_string(static_cast<int>(num(golden, "unreachable"))));
    IRD_EXPECT_IDENTICAL("coverage.position.dataInsufficient",
                         std::to_string(comp.coverage.position.dataInsufficient),
                         std::to_string(static_cast<int>(num(golden, "dataInsufficient"))));
    EXPECT_TRUE(comp.coverage.downgraded)
        << "数据不足样本在场 → 整体降级（KIN-04 R3）——分母保留不计分子";
}

/// V-13（AT-03 零样本反例）：counts 乘积 0 计划 → DataInsufficient 降级
/// ＋零样本（不输出 0%/100%——CoverageResult 零比率字段为结构保证）。
TEST_F(KinGoldenCoverage, V13_ZeroSamplePlanDowngraded_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "NFR-COR-03"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    const tk::JsonValue expected = loadJson(*dataset, "expected/coverage-expected.json", false);
    const tk::JsonValue golden = field(expected, "zeroSamples", {});

    rt::CanonicalModel modelHold{makeGoldenModel(field(
        loadJson(*dataset, "inputs/coverage-inputs.json", true), "model", {}))};
    TestView view{modelHold};
    kin::RegionCoverageQuery q;
    ev::EvaluationRequest req;
    assemble({"planZero-0"}, 1U, modelHold, view, q, req);
    NoopContext ctx;
    const kin::RegionCoverageComputation comp =
        kin::runRegionCoverageComputation(view, q, req, ctx);

    IRD_EXPECT_IDENTICAL("coverage.position.planned",
                         std::to_string(comp.coverage.position.planned),
                         std::to_string(static_cast<int>(num(golden, "planned"))));
    EXPECT_FALSE(comp.coverage.positionDefined)
        << "零样本轴覆盖率未定义（比率不存在——绝不输出 0%）";
    EXPECT_TRUE(comp.coverage.downgraded) << "零样本 → DataInsufficient 整体降级（R8）";
    EXPECT_TRUE(comp.coverage.incomplete || comp.coverage.position.planned == 0U);
}

/// 样本级黄金（acceptance 1 的数据载体面）：planA 样本集与黄金 100 格心
/// 经档案容差一一配对等价（checkSetEquivalent——TK-T06 设施）。
TEST_F(KinGoldenCoverage, ACC1_SampleSetMatchesGoldenCenters_WP15T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "NFR-COR-01"},
                  std::vector<std::string>{"AT-03"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/coverage-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/coverage-expected.json", false);
    const auto perSample = boolVec(expected, "perSampleReach");

    rt::CanonicalModel modelHold{makeGoldenModel(field(inputs, "model", {}))};
    TestView view{modelHold};
    kin::RegionCoverageQuery q;
    ev::EvaluationRequest req;
    assemble({"planA-100"}, 1U, modelHold, view, q, req);
    NoopContext ctx;
    const kin::RegionCoverageComputation comp =
        kin::runRegionCoverageComputation(view, q, req, ctx);

    // 样本几何（生成黄金）与黄金格心集合等价（位置样本——z 恒 0 平面）。
    std::vector<tk::GoldenPoint> actual;
    for (const auto& s : comp.samples.samples) {
        if (s.kind == kin::SampleKind::Position) {
            actual.push_back({s.position[0], s.position[1], s.position[2]});
        }
    }
    // 黄金格心由体心公式闭式再生（与生成脚本同式——独立于产品）。
    // plans 数组先拷贝为具名值（range-for 的临时生存期仅延长到循环结束
    // ——元素指针不可出循环使用）。
    tk::JsonValue plansArr = field(inputs, "plans", {});
    const tk::JsonValue* planA = nullptr;
    for (const auto& p : plansArr.items) {
        if (p.find("id")->text == "planA-100") { planA = &p; }
    }
    ASSERT_NE(planA, nullptr);
    const auto mn = numVec(field(*planA, "box", {}), "min");
    const auto sz = numVec(field(*planA, "box", {}), "size");
    const auto counts = numVec(*planA, "counts");
    std::vector<tk::GoldenPoint> reference;
    for (int ix = 0; ix < static_cast<int>(counts[0]); ++ix) {
        for (int iy = 0; iy < static_cast<int>(counts[1]); ++iy) {
            for (int iz = 0; iz < static_cast<int>(counts[2]); ++iz) {
                reference.push_back({mn[0] + (ix + 0.5) * sz[0] / counts[0],
                                     mn[1] + (iy + 0.5) * sz[1] / counts[1],
                                     mn[2] + (iz + 0.5) * sz[2] / counts[2]});
            }
        }
    }
    const tk::SetCheckResult setR =
        tk::checkSetEquivalent(reference, actual, *profile, tk::SetMatchTraits<tk::GoldenPoint>{});
    ASSERT_TRUE(setR.equivalent) << "样本集与黄金格心不等价";
    ASSERT_EQ(actual.size(), perSample.size()) << "样本数与黄金可达表长度不一致";
    // 逐样本状态与黄金解析可达表一致（圆盘闭式——V-14 的样本级载体）。
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const bool wantReached = perSample[i] > 0.5;
        const bool isReached = comp.results.results[i].state == kin::SampleState::Reached;
        ASSERT_EQ(isReached, wantReached)
            << "样本 " << i << " 状态与黄金解析可达表不符";
    }
}

/// V-21 确定性终验（acceptance 5）：1/8 线程双配置等价集合＋稳定排序
/// 一致（数值不承诺逐位——集合等价语义；样本集与结果表全序对照）。
TEST_F(KinGoldenCoverage, ACC5_Thread1vs8EquivalentSetAndStableOrder_WP15T13_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-27"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());

    rt::CanonicalModel modelHold{makeGoldenModel(field(
        loadJson(*dataset, "inputs/coverage-inputs.json", true), "model", {}))};
    TestView view{modelHold};
    kin::RegionCoverageQuery q1;
    ev::EvaluationRequest req1;
    assemble({"planA-100"}, 1U, modelHold, view, q1, req1);
    kin::RegionCoverageQuery q8 = q1;
    ev::EvaluationRequest req8 = req1;
    q8.threadCount = 8U;   // 执行参数（不入样本集身份——§8.4）
    NoopContext ctx;
    const kin::RegionCoverageComputation one =
        kin::runRegionCoverageComputation(view, q1, req1, ctx);
    const kin::RegionCoverageComputation eight =
        kin::runRegionCoverageComputation(view, q8, req8, ctx);

    // 等价集合：覆盖率计数一致（整数精确——identical）。
    EXPECT_EQ(one.coverage.position, eight.coverage.position);
    EXPECT_EQ(one.coverage.orientation, eight.coverage.orientation);
    EXPECT_EQ(one.coverage.downgraded, eight.coverage.downgraded);
    // 稳定排序一致：样本/结果表按 sampleIndex 全序逐位对照（全序归并
    // ——§8.4 线程配置不改变序）。
    ASSERT_EQ(one.samples.samples.size(), eight.samples.samples.size());
    for (std::size_t i = 0; i < one.samples.samples.size(); ++i) {
        EXPECT_EQ(one.samples.samples[i].sampleIndex, eight.samples.samples[i].sampleIndex);
        EXPECT_TRUE(bitwiseEqVec({one.samples.samples[i].position[0],
                                  one.samples.samples[i].position[1],
                                  one.samples.samples[i].position[2]},
                                 {eight.samples.samples[i].position[0],
                                  eight.samples.samples[i].position[1],
                                  eight.samples.samples[i].position[2]}))
            << "样本 " << i << " 几何跨线程配置漂移（确定性生成破坏）";
        ASSERT_EQ(one.results.results.size(), eight.results.results.size());
        EXPECT_EQ(one.results.results[i].state, eight.results.results[i].state)
            << "样本 " << i << " 状态跨线程配置不一致（等价集合破坏）";
    }
}
