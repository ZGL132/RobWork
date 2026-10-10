/**
 * @file   GoldenDtTest.cpp
 * @brief  传动映射黄金数据集消费套件（DtGoldenMapping）——WP-18-T04 主
 *         交付面：testdata/golden/dt-mapping-golden 数据集经 testkit 黄金
 *         设施（GoldenFixture＋DatasetManifest 完整性＋ToleranceProfile 档
 *         案容差）消费，期望值来自数据文件（generate/make_dt_mapping_golden
 *         .mjs 独立参考实现产物——与产品实现零共享），另含 AT-38 R1 可验
 *         部分（无耦合链等价对角映射正命题＋R1 阻断反例）。
 *
 * 设计依据：
 *   - units/drivetrain.md §14.1（黄金数据集口径——testdata/golden/dt-*、
 *     解析算例＋独立参考实现对照、附录 D 第 9 项标量相对 1×10⁻⁹）、
 *     §14.2（故障注入矩阵 DT-G/B 组——用例编号在注释首行标注）、§6.2/§8.1/
 *     §9/§10（黄金期望值的公式权威）、§8.4（黄金对照归测试侧；P-DT-3
 *     运行时阈值不引入——本套件容差全部经档案通道，只服务对照）
 *   - units/testkit.md §6.1（GoldenFixture 六步生命周期）、§5.3/§5.4
 *     （Check/SetCheck 断言设施——黄金数值断言全部经 TK-T05 设施执行）
 *   - 需求 DYN-04（M-12 精确虚功映射；黄金数据：双向效率/反射惯量/摩擦不
 *     重复计入）、MDL-21/AT-38（R1 阻断反例＋R1 可验部分）、NFR-COR-01
 *     （解析算例＋独立参考实现）、SEL-10（转子字段独立计入）
 *   - 任务契约 tasks/foundation/WP-18-T04.json acceptance 1/2
 *
 * 测试策略（黄金数据独立性——analytic-case 类的 lint 义务）：期望值全部由
 * 数据集 generate/ 脚本闭式推导（与产品实现零共享），本文件只做"装载→驱动
 * 产品入口→经档案容差对照"三件事（不书写参考数值公式——手算锚点除外，
 * 锚点独立于参考实现双路防"两路同错"）。驱动入口＝产品路径
 * （IDriveTrainMappingEvaluator/ICouplingMatrixValidator/
 * IReflectedInertiaEvaluator 接口——经基类接口消费钉扎交付面，
 * NFR-MNT-01 计算库可直调；WP-20-T03 接口路径覆盖教训的正面落实）。
 *
 * 线程约束：gtest 用例天然串行；映射核心纯函数可重入（卡 §13.9）。
 */

#include <sdurws/ird/drivetrain/DiagCodes.hpp>
#include <sdurws/ird/drivetrain/MappingCore.hpp>
#include <sdurws/ird/drivetrain/MappingTypes.hpp>
#include <sdurws/ird/drivetrain/Series.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dt = sdurws::ird::drivetrain;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;
// IRD_EXPECT_* 宏展开为无命名空间限定的 checkCloseWithin 等标识符
// （AssertMacros 契约——宏仅在"已开 testkit 可见性"的消费 TU 使用）。
using namespace sdurws::ird::testkit;

// =====================================================================
// 黄金 JSON 读取脚手架（数据集装载/完整性已由 GoldenFixture 校验；此处
// 只做字段提取——字段缺失记失败并回退安全值，防越界崩溃掩盖首因）
// =====================================================================

namespace {

/// 读数据集文本文件（inputs/expected 侧相对路径经 dataset 解析——路径必须
/// 登记于 manifest，越界抛 TestKitError）。
std::string readDatasetText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    const auto path = inputSide ? ds.resolveInput(rel) : ds.resolveExpected(rel);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
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

/// 布尔字段读取（JsonLite Bool 类别——boolean 成员；缺失记失败回退 false）。
bool boolOf(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isBool()) {
        ADD_FAILURE() << "黄金数据缺布尔字段: " << key;
        return false;
    }
    return v->boolean;
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
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isArray()) {
        ADD_FAILURE() << "黄金数据缺数组字段: " << key;
        return out;
    }
    for (const tk::JsonValue& item : v->items) {
        if (item.isNumber()) {
            out.push_back(item.number);
        } else {
            ADD_FAILURE() << "黄金数据数组含非数值元素: " << key;
        }
    }
    return out;
}

// =====================================================================
// 测试工具（id 派生自 inputs 中的逻辑名字符串——id 不进入黄金数值断言，
// 与既有 MappingGoldenTest 的固定种子派生同口径）
// =====================================================================

/// 从固定种子派生 16 字节强类型 id（测试值——确定性，非随机）。
template <typename Id>
Id idFrom(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize(); // SHA-256（core 唯一算法面）
    Id id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.bytes[i] = digest[i];
    }
    return id;
}

/// 效率条目来源词表（inputs 词 token → SourcedValueTag——只映射黄金数据
/// 实际使用的子集；未知词记失败并回退 ModelingField）。
dt::SourcedValueTag sourceFromToken(const tk::JsonValue& v)
{
    if (v.text == "ModelingField") {
        return dt::SourcedValueTag::ModelingField;
    }
    if (v.text == "CatalogBackfill") {
        return dt::SourcedValueTag::CatalogBackfill;
    }
    if (v.text == "UserProvided") {
        return dt::SourcedValueTag::UserProvided;
    }
    ADD_FAILURE() << "黄金数据未知来源词: " << v.text;
    return dt::SourcedValueTag::ModelingField;
}

/// 从 inputs 模型对象构造归一化传动模型（经 makeDiagonalDriveTrainModel
/// 公共工厂——构造入口结构校验即黄金数据的合法性第一道核对；黄金模型
/// 全部为合法形态，工厂抛异常即数据缺陷）。
dt::DriveTrainModel loadModel(const tk::JsonValue& m)
{
    std::vector<dt::JointDriveAxis> joints;
    std::vector<dt::MotorDriveAxis> motors;
    const tk::JsonValue& jArr = field(m, "joints", {});
    for (const tk::JsonValue& j : jArr.items) {
        dt::JointDriveAxis a;
        a.jointId = idFrom<core::ObjectId>(field(j, "id", {}).text);
        a.kind = dt::JointKind::Revolute; // 黄金数据全部旋转轴（R1 承诺面）
        a.localName = field(j, "name", {}).text;
        joints.push_back(a);
    }
    const tk::JsonValue& mArr = field(m, "motors", {});
    for (const tk::JsonValue& mo : mArr.items) {
        dt::MotorDriveAxis a;
        a.motorId = idFrom<core::ObjectId>(field(mo, "id", {}).text);
        a.jointIndex = static_cast<std::size_t>(num(mo, "jointIndex"));
        motors.push_back(a);
    }
    std::vector<dt::TransmissionRatio> ratios;
    {
        const auto cs = numVec(m, "ratios");
        const tk::JsonValue& srcArr = field(m, "ratioSources", {});
        for (std::size_t i = 0; i < cs.size(); ++i) {
            dt::TransmissionRatio r;
            r.c = cs[i]; // 无量纲（c＝Δq_joint/Δθ_motor——D-DT-4）
            r.source = sourceFromToken(srcArr.items[i]);
            ratios.push_back(r);
        }
    }
    std::vector<dt::EfficiencyModel> eta;
    {
        const auto fwd = numVec(m, "etaForward");
        const auto bwd = numVec(m, "etaBackward");
        const tk::JsonValue& srcArr = field(m, "etaSources", {});
        for (std::size_t i = 0; i < fwd.size(); ++i) {
            dt::EfficiencyModel e;
            e.etaForward = fwd[i];  // 无量纲 (0,1]
            e.etaBackward = bwd[i]; // 无量纲 (0,1]
            e.source = sourceFromToken(srcArr.items[i]);
            eta.push_back(e);
        }
    }
    std::vector<dt::RotorInertiaModel> rotor;
    for (const double j : numVec(m, "rotorInertia")) {
        dt::RotorInertiaModel r;
        r.rotorInertia = j; // kg·m²（电机轴系）
        r.source = dt::SourcedValueTag::CatalogBackfill;
        rotor.push_back(r);
    }
    dt::DriveTrainIdentity identity;
    identity.drivetrainObjectCv.bytes = idFrom<core::ContentVersion>("dtg-config-v1").bytes;
    identity.algorithmVersion = 1;
    identity.contractVersion = 1;

    // 额定/参考力矩（N·m；null 条目＝缺失→负载率不适用）。
    std::vector<std::optional<double>> rated;
    const tk::JsonValue* ratedArr = m.find("ratedTorque");
    if (ratedArr != nullptr && ratedArr->isArray()) {
        for (const tk::JsonValue& rt : ratedArr->items) {
            if (rt.isNull()) {
                rated.push_back(std::nullopt);
            } else if (rt.isNumber()) {
                rated.push_back(rt.number);
            } else {
                ADD_FAILURE() << "黄金数据 ratedTorque 条目非数值/null";
                rated.push_back(std::nullopt);
            }
        }
    }

    return dt::makeDiagonalDriveTrainModel(std::move(joints), std::move(motors),
                                           std::move(ratios), numVec(m, "zeroOffset"),
                                           identity, std::move(eta), std::move(rotor),
                                           std::move(rated));
}

/// 从 inputs 算例对象＋模型构造关节侧序列（jointIds 与模型轴表一致——
/// §5.3 轴序纪律的黄金面）。
dt::JointSeriesView loadSeriesWithModel(const tk::JsonValue& cs,
                                        const dt::DriveTrainModel& model)
{
    dt::JointSeriesView s;
    for (const dt::JointDriveAxis& a : model.jointAxes) {
        s.jointIds.push_back(a.jointId);
    }
    s.caseId = idFrom<core::ObjectId>(field(cs, "caseId", {}).text);
    s.upstreamSliceId.bytes = idFrom<core::ContentIdentity>(field(cs, "upstream", {}).text).bytes;
    const tk::JsonValue& sArr = field(cs, "samples", {});
    for (const tk::JsonValue& sm : sArr.items) {
        dt::JointDriveSample sample;
        sample.t = num(sm, "t");               // s
        sample.q = num(sm, "q");               // rad
        sample.qd = num(sm, "qd");             // rad/s
        sample.qdd = num(sm, "qdd");           // rad/s²
        sample.tauJoint = num(sm, "tauJoint"); // N·m（RNEA 权威——黄金前提）
        sample.segmentId = field(sm, "segmentId", {}).text;
        s.samples.push_back(sample);
    }
    return s;
}

/// 按 id 查 inputs/expected 算例（找不到记失败并返回空对象）。
tk::JsonValue caseById(const tk::JsonValue& arr, const char* id)
{
    for (const tk::JsonValue& c : arr.items) {
        if (c.isObject() && c.find("id") != nullptr && c.find("id")->text == id) {
            return c;
        }
    }
    ADD_FAILURE() << "黄金数据缺算例: " << id;
    return tk::JsonValue{};
}

/// 测试侧相对容差断言（能量 J 面——档案通道不承载 J 量纲；附录 D 第 9 项
/// 同口径：|a−e| ≤ 1e-9·|e|，e==0 退化绝对 1e-12）。
::testing::AssertionResult closeRel(const char* actualExpr, const char* expectedExpr,
                                    double actual, double expected)
{
    const double tol = 1e-9 * std::fabs(expected);
    const double diff = std::fabs(actual - expected);
    if (diff <= tol || (expected == 0.0 && diff <= 1e-12)) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "actual（" << actualExpr << "）=" << actual
           << "；expected（" << expectedExpr << "）=" << expected
           << "；diff=" << diff << "（容差 1e-9 相对——附录 D 第 9 项）";
}

/// 消息首码是否为给定 DT-* 码（fail-fast 消息形态——卡 §13.8）。
bool messageStartsWith(const std::invalid_argument& ex, std::string_view code)
{
    return ex.what() == code
           || (std::string(ex.what()).rfind(std::string(code) + "：", 0) == 0);
}

/// 直接构造一个"绕过工厂"的模型（AT-38 反例需要非法形态——工厂会拒绝）；
/// 阻断面语义＝非法输入必须被映射入口/校验器拒绝而非崩溃或静默放行。
dt::DriveTrainModel rawModelFrom(const tk::JsonValue& rej)
{
    dt::DriveTrainModel m;
    const std::size_t n = static_cast<std::size_t>(num(rej, "nJoints"));
    for (std::size_t i = 0; i < n; ++i) {
        dt::JointDriveAxis a;
        a.jointId = idFrom<core::ObjectId>("dtg-rej-j" + std::to_string(i));
        a.kind = dt::JointKind::Revolute;
        a.localName = "rejJoint" + std::to_string(i + 1);
        m.jointAxes.push_back(a);
        dt::MotorDriveAxis mo;
        mo.motorId = idFrom<core::ObjectId>("dtg-rej-m" + std::to_string(i));
        mo.jointIndex = i;
        m.motorAxes.push_back(mo);
    }
    const tk::JsonValue& mat = field(rej, "chat", {});
    dt::RowMatrix chat;
    chat.rows = static_cast<std::size_t>(num(mat, "rows"));
    chat.cols = static_cast<std::size_t>(num(mat, "cols"));
    chat.data = numVec(mat, "data");
    m.chat = std::move(chat);
    for (const double c : numVec(rej, "ratios")) {
        m.ratios.push_back(dt::TransmissionRatio{c, dt::SourcedValueTag::ModelingField});
    }
    m.zeroOffsetMotor = std::vector<double>(n, 0.0);
    m.identity.drivetrainObjectCv.bytes = idFrom<core::ContentVersion>("dtg-config-v1").bytes;
    m.identity.algorithmVersion = 1;
    m.identity.contractVersion = 1;
    // R2 窗口变体（inputs.window 声明——MDL-21 类型面；R1/R2 能力位同拒）。
    if (const tk::JsonValue* w = rej.find("window"); w != nullptr && w->isObject()) {
        dt::CouplingWindow window;
        const tk::JsonValue& cMat = field(*w, "C", {});
        window.C.rows = static_cast<std::size_t>(num(cMat, "rows"));
        window.C.cols = static_cast<std::size_t>(num(cMat, "cols"));
        window.C.data = numVec(cMat, "data");
        window.conditionNumber = num(*w, "conditionNumber"); // 无量纲（组装方申报传递面）
        for (std::size_t i = 0; i < n; ++i) {
            window.jointRange.push_back(m.jointAxes[i].jointId);
        }
        m.window = std::move(window);
    }
    return m;
}

/// 期望码解析（inputs 声明的 expectCode / expectCodeByR2Capability）。
std::string expectCodeOf(const tk::JsonValue& rej, const char* key)
{
    const tk::JsonValue* v = rej.find(key);
    return (v != nullptr && v->isString()) ? v->text : std::string{};
}

/// 变体配套的最小合法序列（阻断面先于一切数值计算——序列只需结构合法）。
dt::JointSeriesView rejectionSeries(const dt::DriveTrainModel& model)
{
    dt::JointSeriesView s;
    for (const dt::JointDriveAxis& a : model.jointAxes) {
        s.jointIds.push_back(a.jointId);
    }
    s.caseId = idFrom<core::ObjectId>("dtg-case-rej");
    for (std::size_t i = 0; i < 2; ++i) {
        dt::JointDriveSample sample;
        sample.t = static_cast<double>(i); // s
        sample.qd = 1.0;                   // rad/s
        sample.tauJoint = 2.0;             // N·m
        sample.segmentId = "seg-rej";
        s.samples.push_back(sample);
    }
    return s;
}

}  // namespace

// =====================================================================
// 黄金夹具（§6.1 六步生命周期——数据集装载/完整性/档案就绪由基类执行）
// =====================================================================

class DtGoldenMapping : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override
    {
        return {"dt-mapping-golden", "1.0.0"};
    }
};

// =====================================================================
// DT-G1/G4：单轴正传动比解析映射＋零位偏置面（黄金数据文件对照）
// =====================================================================

/**
 * 黄金算例 analytic-single：c=0.01（减速比 100:1）、J_rotor=1e-4 kg·m²、
 * θ_off=0.5 rad、η⁺/η⁻=0.9/0.7、τ_joint=2 N·m 的正弦轨迹——逐样本逐字段
 * 经档案容差对照 expected（独立参考实现）；手算锚点（expected.anchors）
 * 以 EXPECT_NEAR 独立钉扎绝对量级（防"参考实现与产品实现两路同错"）。
 * 断言经基类接口 IDriveTrainMappingEvaluator& 消费（交付面钉扎）。
 */
TEST_F(DtGoldenMapping, AnalyticSingleGolden_WP18T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "NFR-COR-01"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dt-mapping-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dt-mapping-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "analytic-single");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "analytic-single");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    const dt::DriveTrainModel model
        = loadModel(field(field(inputs, "models", {}), inCase.find("model")->text.c_str(), {}));
    const dt::JointSeriesView series = loadSeriesWithModel(inCase, model);

    dt::DriveTrainMappingCore coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl; // 接口消费面
    const dt::DriveTrainMappingOutput out = evaluator.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 1U);
    ASSERT_EQ(out.motorSeries[0].samples.size(), series.samples.size());

    // ---- 逐样本逐字段档案对照（expected.samples[0]＝单轴样本数组——
    // samples 布局为〔轴〕〔样本〕，单轴模型取 items[0]）。
    const tk::JsonValue expSamplesArr = field(expCase, "samples", {}); // 具名（延长临时生命周期）
    const tk::JsonValue& expSamples = expSamplesArr.items[0];
    ASSERT_EQ(expSamples.items.size(), series.samples.size());
    for (std::size_t i = 0; i < expSamples.items.size(); ++i) {
        const tk::JsonValue& e = expSamples.items[i];
        const dt::MotorDriveSample& s = out.motorSeries[0].samples[i];
        const std::string pfx
            = "cases[analytic-single].samples[0][" + std::to_string(i) + "]";
        IRD_EXPECT_CLOSE(pfx + ".t", s.t, num(e, "t"), *profile, "s");
        IRD_EXPECT_CLOSE(pfx + ".theta", s.theta, num(e, "theta"), *profile, "rad");
        IRD_EXPECT_CLOSE(pfx + ".thetaDot", s.thetaDot, num(e, "thetaDot"), *profile, "rad/s");
        IRD_EXPECT_CLOSE(pfx + ".thetaDDot", s.thetaDDot, num(e, "thetaDDot"), *profile,
                         "rad/s^2");
        IRD_EXPECT_CLOSE(pfx + ".tauIdeal", s.tauIdeal, num(e, "tauIdeal"), *profile, "N*m");
        IRD_EXPECT_CLOSE(pfx + ".tauMotor", s.tauMotor, num(e, "tauMotor"), *profile, "N*m");
        IRD_EXPECT_CLOSE(pfx + ".pJoint", s.pJoint, num(e, "pJoint"), *profile, "W");
        IRD_EXPECT_CLOSE(pfx + ".pTransmission", s.pTransmission, num(e, "pTransmission"),
                         *profile, "W");
        IRD_EXPECT_CLOSE(pfx + ".pRotor", s.pRotor, num(e, "pRotor"), *profile, "W");
        IRD_EXPECT_CLOSE(pfx + ".pMotor", s.pMotor, num(e, "pMotor"), *profile, "W");
        // 效率适用性（布尔——精确等值，非数值）。
        EXPECT_EQ(s.efficiencyApplicable, boolOf(e, "efficiencyApplicable"));
    }

    // ---- 手算锚点（独立第三路——inputs.anchors 随算例声明；绝对容差
    // 量级钉扎，防"参考实现与产品实现两路同错"）。
    const tk::JsonValue& anc = field(inCase, "anchors", {});
    const std::size_t ai = static_cast<std::size_t>(num(anc, "sampleIndex"));
    const dt::MotorDriveSample& mid = out.motorSeries[0].samples[ai];
    EXPECT_NEAR(mid.theta, num(anc, "theta"), 1e-9);              // rad
    EXPECT_NEAR(mid.thetaDot, num(anc, "thetaDot"), 1e-12);       // rad/s
    EXPECT_NEAR(mid.thetaDDot, num(anc, "thetaDDot"), 1e-6);      // rad/s²
    EXPECT_NEAR(mid.tauIdeal, num(anc, "tauIdeal"), 1e-12);       // N·m
    EXPECT_NEAR(mid.tauMotor, num(anc, "tauMotor"), 1e-9);        // N·m
}

// =====================================================================
// AT-38 R1 正命题：无耦合链等价对角映射（矩阵语义双路＋虚功对偶）
// =====================================================================

/**
 * 黄金算例 virtualwork-multi（三轴对角 c={0.01,0.005,0.02}）：产品输出须
 * 同时与两路独立参考一致——①逐轴标量路（expected.samples）；②常矩阵 C
 * 语义路（expected.matrixForm：τ_m＝Cᵀ·τ_j、θ̇＝C⁻¹·q̇ 双重循环）——
 * 即 DYN-04/M-12"无耦合链等价于对角传动比映射"（AT-38 R1 可验部分的正
 * 命题面）。另断言理想虚功对偶逐样本逐元素恒等（§8.1——wMotor/wJoint
 * 相对 1e-9；不以总和替代——附录 D C4）。
 */
TEST_F(DtGoldenMapping, VirtualWorkMatrixEquivalence_At38R1_WP18T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "NFR-COR-01", "MDL-21"},
                  std::vector<std::string>{"AT-38"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dt-mapping-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dt-mapping-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "virtualwork-multi");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "virtualwork-multi");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    const dt::DriveTrainModel model
        = loadModel(field(field(inputs, "models", {}), inCase.find("model")->text.c_str(), {}));
    const dt::JointSeriesView series = loadSeriesWithModel(inCase, model);
    const std::size_t n = model.motorAxes.size();
    const std::size_t ns = series.samples.size();

    dt::DriveTrainMappingCore coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl; // 接口消费面
    const dt::DriveTrainMappingOutput out = evaluator.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), n);

    // ---- ①逐轴标量路档案对照（expected.samples[k][i]＝轴×样本）。
    const tk::JsonValue& expSamples = field(expCase, "samples", {});
    ASSERT_EQ(expSamples.items.size(), n);
    for (std::size_t k = 0; k < n; ++k) {
        const tk::JsonValue& axisSamples = expSamples.items[k];
        ASSERT_EQ(axisSamples.items.size(), ns);
        for (std::size_t i = 0; i < ns; ++i) {
            const tk::JsonValue& e = axisSamples.items[i];
            const dt::MotorDriveSample& s = out.motorSeries[k].samples[i];
            const std::string pfx = "cases[virtualwork-multi].samples["
                                    + std::to_string(k) + "][" + std::to_string(i) + "]";
            IRD_EXPECT_CLOSE(pfx + ".thetaDot", s.thetaDot, num(e, "thetaDot"), *profile,
                             "rad/s");
            IRD_EXPECT_CLOSE(pfx + ".tauIdeal", s.tauIdeal, num(e, "tauIdeal"), *profile,
                             "N*m");
        }
    }

    // ---- ②矩阵语义路对照（expected.matrixForm——AT-38 正命题主断言）。
    const tk::JsonValue mf = field(expCase, "matrixForm", {}); // 具名（延长临时生命周期）
    const tk::JsonValue mfThetaDot = field(mf, "thetaDot", {});
    const tk::JsonValue mfTauIdeal = field(mf, "tauIdeal", {});
    std::vector<double> actualThetaDot;
    std::vector<double> goldenThetaDot;
    std::vector<double> actualTauIdeal;
    std::vector<double> goldenTauIdeal;
    for (std::size_t i = 0; i < ns; ++i) {
        // matrixForm 为〔样本〕〔轴〕二维数组——行内元素按 items[k] 取。
        const tk::JsonValue& tdRow = mfThetaDot.items[i];
        const tk::JsonValue& tiRow = mfTauIdeal.items[i];
        for (std::size_t k = 0; k < n; ++k) {
            actualThetaDot.push_back(out.motorSeries[k].samples[i].thetaDot);
            goldenThetaDot.push_back(tdRow.items[k].number);
            actualTauIdeal.push_back(out.motorSeries[k].samples[i].tauIdeal);
            goldenTauIdeal.push_back(tiRow.items[k].number);
        }
    }
    IRD_EXPECT_ALL_CLOSE("cases[virtualwork-multi].matrixForm.thetaDot[*]",
                         actualThetaDot, goldenThetaDot, *profile, "rad/s");
    IRD_EXPECT_ALL_CLOSE("cases[virtualwork-multi].matrixForm.tauIdeal[*]",
                         actualTauIdeal, goldenTauIdeal, *profile, "N*m");

    // ---- 虚功对偶恒等（§8.1 理想口径——wMotor=τ_ideal·θ̇ 与
    // wJoint=τ_j·q̇ 相对 1e-9 相等；逐样本逐元素防正负抵消）。
    const tk::JsonValue vwJoint = field(mf, "wJoint", {}); // 具名（延长临时生命周期）
    for (std::size_t i = 0; i < ns; ++i) {
        const tk::JsonValue& row = vwJoint.items[i]; // 第 i 样本的轴行（〔样本〕〔轴〕）
        for (std::size_t k = 0; k < n; ++k) {
            const double wJoint = row.items[k].number; // W
            const double wMotor = out.motorSeries[k].samples[i].tauIdeal
                                  * out.motorSeries[k].samples[i].thetaDot; // W
            EXPECT_PRED_FORMAT2(closeRel, wMotor, wJoint)
                << "虚功恒等破坏（轴 " << k << " 样本 " << i << "）";
        }
    }
}

// =====================================================================
// AT-38 R1 反例：R1 阻断面（非对角/耦合窗口输入——不提前放开 R1 阻断）
// =====================================================================

/**
 * inputs.at38R1Rejections 声明的两个反例（AT-38"R1 阻断反例仍给诊断"——
 * M-6）：
 *   - nondiag-2axis：非对角元素 1e-6 非零的 Ĉ（无窗口）——evaluate
 *     fail-fast（DT-MATRIX-NONDIAGONAL-LOCKED）；R2 能力位下同阻（交叉
 *     耦合必须经窗口声明——码与能力位无关，结构语义）。
 *   - coupling-window：对角 Ĉ＋R2 耦合窗口——R1 能力位下能力门控阻断
 *     （DT-COUPLING-STAGE-LOCKED）。★WP-18-T05 起本变体在 R2 能力位下
 *     转为**合法输入**（C＝对角传动比——数值路径落位后经 §7.2 全表检查
 *     通过），并构成"C＝对角时与既有对角路径逐项一致"的黄金回归锚：
 *     R2 映射输出与同一数值的无窗口 R1 基线相对 1×10⁻⁹ 逐项一致（数据
 *     文件中的 expectCodeByR2Capability 字段为本命题的 T04 历史值，已被
 *     T05 语义取代——数据集升版不在本任务 allowedFiles，登记单元卡
 *     §18.3 待同步）。
 * 双路径核对：evaluate（抛 invalid_argument，消息以码开头——卡 §13.8）＋
 * ICouplingMatrixValidator&（accepted=false 且首诊断码匹配）。反例输入
 * 不伪装"传动不可行"——阻断语义由码面承载（判定权在消费域，卡 §6.3）。
 */
TEST_F(DtGoldenMapping, At38R1RejectionCounterexamples_WP18T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "NFR-COR-03"},
                  std::vector<std::string>{"AT-38"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dt-mapping-inputs.json", true);
    const tk::JsonValue& rejections = field(inputs, "at38R1Rejections", {});
    ASSERT_EQ(rejections.items.size(), 2U);

    dt::DriveTrainMappingCore coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl;    // 接口消费面
    dt::ICouplingMatrixValidator& validator = coreImpl;       // 接口消费面

    for (const tk::JsonValue& rej : rejections.items) {
        const std::string id = field(rej, "id", {}).text;
        SCOPED_TRACE("AT-38 反例: " + id);
        const dt::DriveTrainModel model = rawModelFrom(rej);
        const dt::JointSeriesView series = rejectionSeries(model);
        const std::string codeR1 = expectCodeOf(rej, "expectCode");
        ASSERT_FALSE(codeR1.empty());
        const bool hasWindow = (rej.find("window") != nullptr && rej.find("window")->isObject());

        // ---- evaluate 路径：R1 能力下阻断 fail-fast（消息以码开头）。
        try {
            (void)evaluator.evaluate(model, series, nullptr);
            FAIL() << "反例输入必须被阻断（" << codeR1 << "）";
        } catch (const std::invalid_argument& ex) {
            EXPECT_TRUE(messageStartsWith(ex, codeR1)) << ex.what();
        }

        // ---- validator 路径：R1 能力位——accepted=false 且首码匹配。
        const dt::CouplingValidationResult gateR1
            = validator.validate(model, dt::StageCapability::R1Capability);
        EXPECT_FALSE(gateR1.accepted);
        ASSERT_FALSE(gateR1.diagnostics.empty());
        EXPECT_EQ(gateR1.diagnostics.front().code, codeR1);

        if (!hasWindow) {
            // ---- 未声明窗口的非对角变体（nondiag-2axis）：R2 能力位下
            // 同码阻断——交叉耦合必须经窗口声明，非对角阻断属结构语义、
            // 与能力位无关（T05 落位后语义不变）。
            const std::string codeR2 = expectCodeOf(rej, "expectCodeByR2Capability");
            if (!codeR2.empty()) {
                const dt::CouplingValidationResult gateR2
                    = validator.validate(model, dt::StageCapability::R2Capability);
                EXPECT_FALSE(gateR2.accepted);
                ASSERT_FALSE(gateR2.diagnostics.empty());
                EXPECT_EQ(gateR2.diagnostics.front().code, codeR2);
            }
        } else {
            // ---- 窗口变体（coupling-window）：T05 起在 R2 能力位下合法
            // ——§7.2 全表检查通过（C＝diag(0.01,0.02)、κ＝2 深良态），并
            // 构成 R1 等价回归锚：与同一数值的无窗口 R1 基线逐项一致
            // （相对 1×10⁻⁹——矩阵路径与标量路径求值序不同的黄金容差）。
            const dt::CouplingValidationResult gateR2
                = validator.validate(model, dt::StageCapability::R2Capability);
            EXPECT_TRUE(gateR2.accepted)
                << "合法耦合窗口在 R2 能力位下必须被接受（WP-18-T05 数值路径）";
            EXPECT_GT(gateR2.conditionNumber, 0.0);

            dt::DriveTrainModel baseline = model;
            baseline.window.reset(); // 同一数值的对角形态（chat＝diag＝C_w）
            const dt::DriveTrainMappingOutput outR2 = evaluator.evaluate(
                model, series, nullptr, dt::StageCapability::R2Capability);
            const dt::DriveTrainMappingOutput outR1 = evaluator.evaluate(baseline, series, nullptr);
            ASSERT_EQ(outR1.motorSeries.size(), outR2.motorSeries.size());
            for (std::size_t k = 0; k < outR2.motorSeries.size(); ++k) {
                ASSERT_EQ(outR1.motorSeries[k].samples.size(),
                          outR2.motorSeries[k].samples.size());
                for (std::size_t i = 0; i < outR2.motorSeries[k].samples.size(); ++i) {
                    const dt::MotorDriveSample& r1 = outR1.motorSeries[k].samples[i];
                    const dt::MotorDriveSample& r2 = outR2.motorSeries[k].samples[i];
                    EXPECT_PRED_FORMAT2(closeRel, r2.theta, r1.theta);
                    EXPECT_PRED_FORMAT2(closeRel, r2.thetaDot, r1.thetaDot);
                    EXPECT_PRED_FORMAT2(closeRel, r2.thetaDDot, r1.thetaDDot);
                    EXPECT_PRED_FORMAT2(closeRel, r2.tauIdeal, r1.tauIdeal);
                    EXPECT_PRED_FORMAT2(closeRel, r2.tauMotor, r1.tauMotor);
                    EXPECT_PRED_FORMAT2(closeRel, r2.pJoint, r1.pJoint);
                    EXPECT_PRED_FORMAT2(closeRel, r2.pMotor, r1.pMotor);
                }
                // 工作点统计一致（完整循环 RMS——黄金容差内）。
                ASSERT_EQ(outR1.points.size(), outR2.points.size());
                EXPECT_PRED_FORMAT2(closeRel, outR2.points[k].tauRms,
                                    outR1.points[k].tauRms);
                EXPECT_PRED_FORMAT2(closeRel, outR2.points[k].omegaRms,
                                    outR1.points[k].omegaRms);
            }
        }
    }
}

// =====================================================================
// DT-G6：双向效率与再生（能量恒等式＋方向折算——黄金数据文件对照）
// =====================================================================

/**
 * 黄金算例 energy-bidirectional：η⁺=0.9/η⁻=0.7 混合循环（正功段 2 s＋再
 * 生段 2 s，匀速 q̈=0——转子项恰零）。断言三面：①逐样本方向折算
 * pTransmission 经档案对照；②能量分项（J——档案通道不承载）以测试侧独立
 * 梯形积分重算对照 expected 能量值＋恒等式断言（E_joint=E_pos−E_regen、
 * E_motor=E_joint+E_loss+E_rotor、E_loss>0 恒成立——§10.5）；③四象限
 * 计数/占比经档案、象限能量（J）测试侧对照。
 */
TEST_F(DtGoldenMapping, BidirectionalEnergyGolden_WP18T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dt-mapping-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dt-mapping-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "energy-bidirectional");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "energy-bidirectional");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    const dt::DriveTrainModel model
        = loadModel(field(field(inputs, "models", {}), inCase.find("model")->text.c_str(), {}));
    const dt::JointSeriesView series = loadSeriesWithModel(inCase, model);

    dt::DriveTrainMappingCore coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl;
    const dt::DriveTrainMappingOutput out = evaluator.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 1U);
    ASSERT_EQ(out.points.size(), 1U);

    // ---- ①逐样本方向折算（档案通道；samples 布局〔轴〕〔样本〕）。
    const tk::JsonValue expSamplesArr = field(expCase, "samples", {}); // 具名（延长临时生命周期）
    const tk::JsonValue& expSamples = expSamplesArr.items[0];
    ASSERT_EQ(expSamples.items.size(), series.samples.size());
    for (std::size_t i = 0; i < expSamples.items.size(); ++i) {
        const tk::JsonValue& e = expSamples.items[i];
        const dt::MotorDriveSample& s = out.motorSeries[0].samples[i];
        const std::string pfx
            = "cases[energy-bidirectional].samples[0][" + std::to_string(i) + "]";
        IRD_EXPECT_CLOSE(pfx + ".pTransmission", s.pTransmission, num(e, "pTransmission"),
                         *profile, "W");
        IRD_EXPECT_CLOSE(pfx + ".pMotor", s.pMotor, num(e, "pMotor"), *profile, "W");
        EXPECT_EQ(s.efficiencyApplicable, boolOf(e, "efficiencyApplicable"));
    }

    // ---- ②能量分项（J）：测试侧独立梯形积分重算（输入仅取黄金 inputs
    // 序列＋η——与 generate 脚本、产品实现三路互证）；恒等式断言。
    const auto pJointOf = [&](std::size_t i) {
        return series.samples[i].tauJoint * series.samples[i].qd; // W
    };
    const double etaF = model.efficiency[0].etaForward;  // 无量纲
    const double etaB = model.efficiency[0].etaBackward; // 无量纲
    auto pTransOf = [&](std::size_t i) {
        const double p = pJointOf(i);
        return p > 0.0 ? p / etaF : (p < 0.0 ? p * etaB : 0.0); // W（§10.2）
    };
    const std::size_t n = series.samples.size();
    auto trapezoid = [&](std::vector<double> ys) {
        double sum = 0.0;
        for (std::size_t i = 1; i < n; ++i) {
            sum += 0.5 * (ys[i - 1] + ys[i]) * (series.samples[i].t - series.samples[i - 1].t);
        }
        return sum; // J
    };
    std::vector<double> transV;
    std::vector<double> jointV;
    std::vector<double> posV;
    std::vector<double> regenV;
    for (std::size_t i = 0; i < n; ++i) {
        const double pj = pJointOf(i);
        const double pt = pTransOf(i);
        transV.push_back(pt);
        jointV.push_back(pj);
        posV.push_back(std::max(0.0, pj));
        regenV.push_back(std::max(0.0, -pj));
    }
    const double eJointRef = trapezoid(jointV);   // J
    const double ePosRef = trapezoid(posV);       // J
    const double eRegenRef = trapezoid(regenV);   // J
    const double eLossRef = trapezoid(transV) - eJointRef; // J（∫(pTrans−pJoint)）
    const double eMotorRef = trapezoid(transV);   // J（q̈=0 → pRotor 恒 0）

    const auto& en = out.points[0].energy;
    EXPECT_PRED_FORMAT2(closeRel, en.eJoint, eJointRef);
    EXPECT_PRED_FORMAT2(closeRel, en.ePos, ePosRef);
    EXPECT_PRED_FORMAT2(closeRel, en.eRegen, eRegenRef);
    EXPECT_PRED_FORMAT2(closeRel, en.eLoss, eLossRef);
    EXPECT_PRED_FORMAT2(closeRel, en.eMotor, eMotorRef);
    // 恒等式（§10.5——黄金算例②"能量守恒恒等式核对"的精确语义）。
    EXPECT_PRED_FORMAT2(closeRel, en.eJoint, en.ePos - en.eRegen);
    EXPECT_PRED_FORMAT2(closeRel, en.eMotor, en.eJoint + en.eLoss + en.eRotor);
    EXPECT_GT(en.eLoss, 0.0); // 损耗恒正（两方向折算均消耗）
    // 与 expected 数据文件能量值双向对照（第三路——生成侧参考实现）。
    const tk::JsonValue expPointsArr = field(expCase, "points", {}); // 具名（延长临时生命周期）
    const tk::JsonValue& eExp = field(expPointsArr.items[0], "energy", {});
    EXPECT_PRED_FORMAT2(closeRel, en.eJoint, num(eExp, "eJoint"));
    EXPECT_PRED_FORMAT2(closeRel, en.ePos, num(eExp, "ePos"));
    EXPECT_PRED_FORMAT2(closeRel, en.eRegen, num(eExp, "eRegen"));
    EXPECT_PRED_FORMAT2(closeRel, en.eLoss, num(eExp, "eLoss"));
    EXPECT_PRED_FORMAT2(closeRel, en.eMotor, num(eExp, "eMotor"));

    // ---- ③四象限（§10.6——ω>0 恒：正功段 Q1、再生段 Q2；占比经档案、
    // 象限能量 J 测试侧对照）。
    const tk::JsonValue& pt = expPointsArr.items[0]; // 与 eExp 同源（具名延长）
    const auto& p = out.points[0];
    IRD_EXPECT_CLOSE("cases[energy-bidirectional].points[0].q1.timeShare", p.q1.timeShare,
                     num(field(pt, "q1", {}), "timeShare"), *profile, "1");
    IRD_EXPECT_CLOSE("cases[energy-bidirectional].points[0].q2.timeShare", p.q2.timeShare,
                     num(field(pt, "q2", {}), "timeShare"), *profile, "1");
    EXPECT_EQ(p.q1.sampleCount,
              static_cast<std::size_t>(num(field(pt, "q1", {}), "sampleCount")));
    EXPECT_EQ(p.q2.sampleCount,
              static_cast<std::size_t>(num(field(pt, "q2", {}), "sampleCount")));
    EXPECT_PRED_FORMAT2(closeRel, p.q1.energy, num(field(pt, "q1", {}), "energy"));
    EXPECT_PRED_FORMAT2(closeRel, p.q2.energy, num(field(pt, "q2", {}), "energy"));
}

// =====================================================================
// DT-G7：零功率/驻留＋近零功率（效率不适用标记、RMS 含驻留）
// =====================================================================

/**
 * 黄金算例 dwell-zero（c=1 直驱、额定力矩 10 N·m）：驻留 2 s（q̇=0——
 * P_joint 精确零，效率不适用显式标记）＋近零样本（q̇=1e-9 rad/s——方向
 * 精确判据仍经 η⁺ 折算，附录 D C4 近零样例）＋运动段。断言：RMS（含驻留）
 * 与负载率经档案；效率适用性/pTransmission 经档案（布尔面精确）；零速桶
 * 统计经档案。
 */
TEST_F(DtGoldenMapping, DwellZeroGolden_WP18T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dt-mapping-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dt-mapping-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "dwell-zero");
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "dwell-zero");
    ASSERT_TRUE(inCase.isObject());
    ASSERT_TRUE(expCase.isObject());

    const dt::DriveTrainModel model
        = loadModel(field(field(inputs, "models", {}), inCase.find("model")->text.c_str(), {}));
    const dt::JointSeriesView series = loadSeriesWithModel(inCase, model);

    dt::DriveTrainMappingCore coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl;
    const dt::DriveTrainMappingOutput out = evaluator.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 1U);
    ASSERT_EQ(out.points.size(), 1U);

    // ---- 逐样本（档案）：效率适用性显式标记＋pTransmission（零值样本
    // 精确 0——{rel 1e-9, abs 1e-12} 档案容差自然覆盖）。
    const tk::JsonValue expSamplesArr = field(expCase, "samples", {}); // 具名（延长临时生命周期）
    const tk::JsonValue& expSamples = expSamplesArr.items[0];
    for (std::size_t i = 0; i < expSamples.items.size(); ++i) {
        const tk::JsonValue& e = expSamples.items[i];
        const dt::MotorDriveSample& s = out.motorSeries[0].samples[i];
        const std::string pfx
            = "cases[dwell-zero].samples[0][" + std::to_string(i) + "]";
        IRD_EXPECT_CLOSE(pfx + ".pTransmission", s.pTransmission, num(e, "pTransmission"),
                         *profile, "W");
        EXPECT_EQ(s.efficiencyApplicable, boolOf(e, "efficiencyApplicable"));
        // 零功率样本：pTransmission 必须精确零（不伪造数值——ERR-01）。
        if (!s.efficiencyApplicable) {
            EXPECT_EQ(s.pTransmission, 0.0);
        }
    }
    // 近零样本（i=2，q̇=1e-9）：效率适用（符号精确判据——正号→η⁺ 折算）。
    EXPECT_TRUE(out.motorSeries[0].samples[2].efficiencyApplicable);

    // ---- RMS（§10.4 完整循环含驻留——驻留力矩保持计入热负载）与负载率
    //（参考值——额定 10 N·m 来自黄金模型 ratedTorque）。
    const tk::JsonValue expPointsArr = field(expCase, "points", {}); // 具名（延长临时生命周期）
    const tk::JsonValue& pt = expPointsArr.items[0];
    const auto& p = out.points[0];
    IRD_EXPECT_CLOSE("cases[dwell-zero].points[0].tauRms", p.tauRms, num(pt, "tauRms"),
                     *profile, "N*m");
    IRD_EXPECT_CLOSE("cases[dwell-zero].points[0].omegaRms", p.omegaRms, num(pt, "omegaRms"),
                     *profile, "rad/s");
    ASSERT_TRUE(pt.find("loadRatio") != nullptr && pt.find("loadRatio")->isNumber());
    ASSERT_TRUE(p.loadRatio.has_value());
    IRD_EXPECT_CLOSE("cases[dwell-zero].points[0].loadRatio", *p.loadRatio,
                     num(pt, "loadRatio"), *profile, "1");

    // ---- 零速桶（§10.6——零速/零功率只计时间占比与计数，不计象限能量）
    // 与 Q1 计数/占比（档案）＋象限能量（J——测试侧对照）。
    IRD_EXPECT_CLOSE("cases[dwell-zero].points[0].zeroDwell.timeShare", p.zeroDwell.timeShare,
                     num(field(pt, "zeroDwell", {}), "timeShare"), *profile, "1");
    EXPECT_EQ(p.zeroDwell.sampleCount,
              static_cast<std::size_t>(num(field(pt, "zeroDwell", {}), "sampleCount")));
    EXPECT_EQ(p.zeroDwell.energy, 0.0); // J（零速桶不计象限能量——精确零）
    IRD_EXPECT_CLOSE("cases[dwell-zero].points[0].q1.timeShare", p.q1.timeShare,
                     num(field(pt, "q1", {}), "timeShare"), *profile, "1");
    EXPECT_PRED_FORMAT2(closeRel, p.q1.energy, num(field(pt, "q1", {}), "energy"));
}

// =====================================================================
// DT-G8：反射惯量与惯量比（J/c² 与 c²·J_load/J_rotor——黄金数据对照）
// =====================================================================

/**
 * 黄金算例 inertia-golden（双轴 c={0.01,0.02}、J_rotor={1e-4,4e-4}、负载
 * 折算惯量仅轴 0 提供 0.5 kg·m²）：经 IReflectedInertiaEvaluator& 接口
 * 消费（交付面钉扎）——J_reflected=J_rotor/c² 与惯量比经档案对照；轴 1
 * 负载缺失→惯量比不适用（显式 null）；主管线 reflectedInertia 字段同源。
 */
TEST_F(DtGoldenMapping, ReflectedInertiaGolden_WP18T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "SEL-10"},
                  std::vector<std::string>{"AT-07"}, datasetRef());
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/dt-mapping-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/dt-mapping-expected.json", false);
    const tk::JsonValue inCase = caseById(field(inputs, "cases", {}), "inertia-golden");
    const tk::JsonValue expInertia = field(expected, "inertiaCases", {}).items[0];
    ASSERT_TRUE(inCase.isObject());

    const dt::DriveTrainModel model
        = loadModel(field(field(inputs, "models", {}), inCase.find("model")->text.c_str(), {}));
    // 负载折算惯量（kg·m²，关节轴系——组装方值传递；轴 1 缺失＝列表短于
    // 轴数——P-DT-4 保守消费形态的黄金面）。
    std::vector<dt::LoadInertiaEntry> load;
    for (const double v : numVec(inCase, "loadInertia")) {
        dt::LoadInertiaEntry e;
        e.loadInertiaJointSide = v;
        e.source = sourceFromToken(field(inCase, "loadInertiaSource", {}));
        load.push_back(e);
    }

    dt::DriveTrainMappingCore coreImpl;
    dt::IReflectedInertiaEvaluator& inertiaEvaluator = coreImpl; // 接口消费面
    const dt::ReflectedInertiaResult inertia = inertiaEvaluator.evaluate(model, load);
    ASSERT_EQ(inertia.axes.size(), model.motorAxes.size());

    // ---- 档案对照（J_reflected/inertiaRatio；null 比值断言 nullopt）。
    const tk::JsonValue& expAxes = field(expInertia, "axes", {});
    ASSERT_EQ(expAxes.items.size(), inertia.axes.size());
    std::vector<double> actualJ;
    std::vector<double> goldenJ;
    for (std::size_t k = 0; k < expAxes.items.size(); ++k) {
        const tk::JsonValue& e = expAxes.items[k];
        const auto& a = inertia.axes[k];
        EXPECT_EQ(a.jointIndex, static_cast<std::size_t>(num(e, "jointIndex")));
        actualJ.push_back(a.jReflectedJointSide);      // kg·m²（关节轴系）
        goldenJ.push_back(num(e, "jReflected"));
        const tk::JsonValue* ratio = e.find("inertiaRatio");
        if (ratio != nullptr && ratio->isNumber()) {
            ASSERT_TRUE(a.inertiaRatio.has_value());
            IRD_EXPECT_CLOSE("inertiaCases[inertia-golden].axes[" + std::to_string(k)
                                 + "].inertiaRatio",
                             *a.inertiaRatio, ratio->number, *profile, "1");
        } else {
            EXPECT_FALSE(a.inertiaRatio.has_value())
                << "轴 " << k << " 负载缺失——惯量比必须不适用（显式标记）";
        }
    }
    IRD_EXPECT_ALL_CLOSE("inertiaCases[inertia-golden].axes[*].jReflected", actualJ,
                         goldenJ, *profile, "kg*m^2");

    // ---- 主管线 reflectedInertia 字段同源（独立负载输入——惯量比在主管
    // 线保持不适用；§9.2 数值事实面；期望值取 expected 算例 points[0]）。
    const tk::JsonValue expCase = caseById(field(expected, "cases", {}), "inertia-golden");
    const dt::JointSeriesView series = loadSeriesWithModel(inCase, model);
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl;
    const dt::DriveTrainMappingOutput out = evaluator.evaluate(model, series, nullptr);
    ASSERT_EQ(out.points.size(), 2U);
    IRD_EXPECT_CLOSE("cases[inertia-golden].points[0].reflectedInertia",
                     out.points[0].reflectedInertia,
                     num(field(expCase, "points", {}).items[0],
                         "reflectedInertia"),
                     *profile, "kg*m^2");
    EXPECT_FALSE(out.points[0].inertiaRatio.has_value());
}
