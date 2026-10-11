/**
 * @file   At06GoldenContractTest.cpp
 * @brief  AT-06 轨迹链四反例黄金数据集的登记契约与解析对照用例组（TrjAt06-
 *         GoldenContract，WP-16-T13）——acceptance 2"testdata/golden/trj-*
 *         按 DatasetManifest（ird-golden-manifest/1）登记：coveredRequire-
 *         ments 逐项 lint、容差档案、producer/edgeCases 字段齐备"的契约自
 *         证面，与 acceptance 1"AT-06 全部反例契约测试通过"的解析半区
 *         （段内碰撞检出/细分预算耗尽/初始路径碰撞重规划成功/限制超标定
 *         位的黄金数值对照——P-06 冻结数值表物化对照）。
 *
 * 设计依据：
 *   - units/trajectory.md §17.1/§17.2（黄金数据集 trj-* 与 AT-06 反例矩阵
 *     V-10/V-11/V-14/V-19 的数据面归属——T13 注册；§11.1/§11.5/§15.6 复检
 *     协议全要素）、REQUIREMENTS 附录 C《P-06 冻结数值表》（2026-10-10 冻
 *     结——inputs.protocol 的唯一权威来源）、AT-06 原文（段内碰撞反例/
 *     预算耗尽反例/重规划反例——REQUIREMENTS 表 4 轨迹行）
 *   - units/testkit.md §4.2.2（DatasetManifest 字段表——登记契约权威词
 *     表）、§4.5（size＋SHA-256 完整性）、§4.3.1/§4.3.2（容差档案 schema
 *     与"只准更严"规则）、§5.1（装载语义——本组用例即其消费面）
 *   - 先例：drivetrain/contract_test/GoldenDatasetContractTest.cpp（WP-18-
 *     T04 dt-* 登记契约同款形态——装载/字段/完整性/交叉一致/档案通道/
 *     数据根布局六面结构）
 *   - 任务契约 tasks/foundation/WP-16-T13.json acceptance 1/2/3
 *
 * 测试策略（两半区分工，防"重复宣称"）：
 *   - 数据资产面（本文件）：GoldenDataset::load 全量校验（schema→integrity
 *     SHA-256→交叉校验）＋manifest 字段契约＋P-06 冻结表物化对照＋四反例
 *     解析量黄金对照（消费测试侧以独立闭式公式重算与 expected 对照——
 *     生成脚本与产品实现零共享，本测试不桩化任何装载器）；
 *   - 运行期真实链路面（test/RecheckTest.cpp——T06/T07 已落位执行）：V-10
 *     重规划闭环/V-14 段内碰撞/V-19 预算耗尽/V-15 限位检出以真实 policy
 *     会话（内置 ProximityStrategyRW）与真实 RRT-Connect 送验（units/
 *     trajectory.md §17.3"需真实 planner/后端"清单——替身不可替代），本
 *     组用例不替代、不重复宣称。
 *
 * 本文件为**非条件源**：被测面＝testkit 装载器（纯 std）＋Recheck.hpp 的
 * inline 值函数（makeP06FrozenRecheckParameters/validateRecheckParameters
 * ——零 rw 符号，冒烟模式同样执行；RecheckContractTest.cpp 同款口径）。
 * 集成与冒烟两模式皆跑（数据资产契约两树一致）。
 *
 * 线程约束：gtest 用例天然串行。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/trajectory/Recheck.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace tk = sdurws::ird::testkit;
namespace trj = sdurws::ird::trajectory;

// IRD_EXPECT_CLOSE 宏展开为对 checkCloseWithin 的无限定调用（gtest TEST
// 体在全局作用域展开——ADL 只查参数类型所属命名空间 core，找不到 testkit
// 的自由函数）——以最小 using 声明引入（GoldenDtTest 先例用整个 using
// namespace；本文件按契约测试"自持最小面"纪律只引入被宏消费的函数）。
using sdurws::ird::testkit::checkCloseWithin;

namespace {

/// 数据集引用（被测契约对象——WP-16-T13 登记的唯一 trj-* 数据集）。
tk::DatasetRef goldenRef()
{
    return {"trj-at06-recheck-golden", "1.0.0"};
}

/// 容差档案引用（manifest.toleranceProfile 指向——契约一致性断言的期望值）。
constexpr const char* kProfileId = "trj-at06";
constexpr const char* kProfileVersion = "1.0.0";

/// 读数据集文本文件（完整性已由 GoldenDataset::load 构造性校验——此处仅解析）。
std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 按 id 在 cases 数组中取算例（缺失/重复均显性失败——契约面的确定性前提）。
const tk::JsonValue* findCase(const tk::JsonValue& casesRoot, const char* id)
{
    const tk::JsonValue* cases = casesRoot.find("cases");
    if (cases == nullptr) {
        return nullptr;
    }
    const tk::JsonValue* hit = nullptr;
    for (const tk::JsonValue& c : cases->items) {
        const tk::JsonValue* cid = c.find("id");
        if (cid != nullptr && cid->text == id) {
            EXPECT_EQ(hit, nullptr) << "算例 id 重复: " << id;
            hit = &c;
        }
    }
    return hit;
}

/// 取子对象（缺失显性失败——数据资产缺陷即失败，不猜测不补默认）。
const tk::JsonValue& mustObject(const tk::JsonValue& v, const char* key,
                                const char* ctx)
{
    const tk::JsonValue* p = v.find(key);
    EXPECT_NE(p, nullptr) << "缺少字段 " << key << "（" << ctx << "）";
    return (p != nullptr) ? *p : v;  // 失败时返回自身避免空引用——后续断言继续报错
}

/// 取数值（同上——缺失即失败；非数值同败——类型面缺陷不猜测）。
double mustNumber(const tk::JsonValue& v, const char* key, const char* ctx)
{
    const tk::JsonValue* p = v.find(key);
    EXPECT_NE(p, nullptr) << "缺少字段 " << key << "（" << ctx << "）";
    if (p != nullptr) {
        EXPECT_TRUE(p->isNumber()) << "字段 " << key << " 非数值（" << ctx << "）";
        return p->number;
    }
    return 0.0;
}

}  // namespace

// =====================================================================
// 登记契约：GoldenDataset::load 全量校验通过＋manifest 字段落值
// =====================================================================

/**
 * DatasetManifest 登记契约（acceptance 2 的机器断言面）：
 *   - 装载本身即全量校验（schema→integrity SHA-256/size→交叉校验）——
 *     load 返回即"登记完整且未被篡改"的构造性证明；
 *   - kind=analytic-case（解析算例——NFR-COR-01 独立正确性依据类）；
 *   - 登记要求覆盖（coveredRequirements 逐项含 TRJ-01~06——任务契约
 *     requirements 字段的六需求全集，逐项 lint 在装载时已对
 *     testdata/requirements-ids.json 字典执行）与 AT 锚点（coveredAt 含
 *     AT-06——四反例的验收锚）；
 *   - 档案引用指向 trj-at06@1.0.0；
 *   - 独立性声明（referenceSource.independentOfProductionImpl——closed-
 *     form 独立参考实现纪律的登记面）；
 *   - 边角样例三布尔（附录 D C4 lint 强制——零值/近零/正负抵消）且
 *     sampleRefs 覆盖三类锚点；
 *   - 生成器入库声明（generator.committed——期望可复现）。
 */
TEST(TrjAt06GoldenContract, ManifestRegistration_WP16T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-01", "TRJ-02", "TRJ-03",
                                              "TRJ-04", "TRJ-05", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});

    // ---- 全量装载（任何校验失败抛 TestKitError——数据资产缺陷即失败）。
    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::DatasetManifest& m = ds.manifest();

    // ---- schema 与定位一致性（datasetId/version 与目录名——§4.2.2）。
    EXPECT_EQ(m.schemaVersion, "ird-golden-manifest/1");
    EXPECT_EQ(m.datasetId, goldenRef().datasetId);
    EXPECT_EQ(m.version, goldenRef().version);

    // ---- kind=analytic-case（登记类别与交付物口径一致——解析算例）。
    ASSERT_TRUE(m.kind == tk::DatasetKind::AnalyticCase);

    // ---- 登记要求逐项覆盖（契约 requirements 字段六需求——轨迹链全集）。
    const char* requiredTrj[] = {"TRJ-01", "TRJ-02", "TRJ-03",
                                 "TRJ-04", "TRJ-05", "TRJ-06"};
    for (const char* req : requiredTrj) {
        EXPECT_NE(std::find(m.coveredRequirements.begin(),
                            m.coveredRequirements.end(), req),
                  m.coveredRequirements.end())
            << "黄金数据集必须登记 " << req << "（契约 requirements 字段——"
               "coveredRequirements 逐项 lint）";
    }
    EXPECT_NE(std::find(m.coveredAt.begin(), m.coveredAt.end(), "AT-06"),
              m.coveredAt.end())
        << "黄金数据集必须登记 AT-06（四反例验收锚）";

    // ---- 档案引用（id/version 与消费用例 datasetRef→profileRef 链一致）。
    EXPECT_EQ(m.toleranceProfileId, std::string(kProfileId));
    EXPECT_EQ(m.toleranceProfileVersion, std::string(kProfileVersion));

    // ---- 独立性与方法声明（closed-form——期望值与产品实现零共享）。
    ASSERT_TRUE(m.referenceSourcePresent);
    EXPECT_TRUE(m.referenceSource.independentOfProductionImpl);
    EXPECT_EQ(m.referenceSource.method, "closed-form");

    // ---- 边角样例三布尔＋三类锚点（附录 D C4——零值/近零/正负抵消）。
    ASSERT_TRUE(m.edgeCasesPresent);
    EXPECT_TRUE(m.edgeCases.zeroValue);
    EXPECT_TRUE(m.edgeCases.nearZero);
    EXPECT_TRUE(m.edgeCases.signCancellation);
    // sampleRefs 逐类核验：三类语义锚点各至少一条（文本含对应字段路径——
    // 引用必须落到 expected 的具体条目，不得空泛声明）。
    bool hasZeroRef = false, hasNearZeroRef = false, hasSignRef = false;
    for (const std::string& ref : m.edgeCases.sampleRefs) {
        if (ref.find("netDisplacement") != std::string::npos
            || ref.find("collisionQueriesExecuted") != std::string::npos
            || ref.find("centerAngleDeviation") != std::string::npos
            || ref.find("zeroMarginAnchor") != std::string::npos) {
            hasZeroRef = true;
        }
        if (ref.find("nearZeroProbe") != std::string::npos
            || ref.find("stepOverDefault") != std::string::npos) {
            hasNearZeroRef = true;
        }
        if (ref.find("netDisplacement") != std::string::npos
            && ref.find("正负抵消") != std::string::npos) {
            hasSignRef = true;
        }
    }
    EXPECT_TRUE(hasZeroRef) << "零值样例引用缺失（sampleRefs 须指向具体条目）";
    EXPECT_TRUE(hasNearZeroRef) << "近零样例引用缺失";
    EXPECT_TRUE(hasSignRef) << "正负抵消样例引用缺失";

    // ---- 生成器与历史（期望值可复现＋首版登记）。
    ASSERT_TRUE(m.generatorPresent);
    EXPECT_TRUE(m.generatorCommitted);
    EXPECT_NE(m.generatorScript.find("generate/"), std::string::npos)
        << "生成脚本必须位于数据集 generate/ 目录";
    ASSERT_FALSE(m.history.empty());
    EXPECT_EQ(m.history.front().version, goldenRef().version);

    // ---- inputs/expected 至少各 1（§4.2.2 下限）。
    EXPECT_GE(m.inputs.size(), 1U);
    EXPECT_GE(m.expected.size(), 1U);
}

// =====================================================================
// 完整性契约：integrity 覆盖 inputs＋expected＋generate 全部文件
// =====================================================================

/**
 * §4.5 完整性登记契约：integrity 条目必须覆盖 manifest 声明的全部 inputs/
 * expected 文件与 generate 脚本（committed=true 时）——防"清单外文件漂移"；
 * 逐文件 SHA-256/size 校验已由 load 构造性执行（篡改即 DatasetInvalid），
 * 本用例断言**覆盖关系**（登记面的完备性——load 只校验已登记条目）。
 */
TEST(TrjAt06GoldenContract, IntegrityCoversAllFiles_WP16T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::DatasetManifest& m = ds.manifest();

    const auto covered = [&](const std::string& rel) {
        return std::any_of(m.integrity.begin(), m.integrity.end(),
                           [&](const tk::IntegrityEntry& e) { return e.path == rel; });
    };
    for (const std::string& rel : m.inputs) {
        EXPECT_TRUE(covered(rel)) << "inputs 文件未登记完整性: " << rel;
    }
    for (const std::string& rel : m.expected) {
        EXPECT_TRUE(covered(rel)) << "expected 文件未登记完整性: " << rel;
    }
    if (m.generatorPresent && m.generatorCommitted) {
        EXPECT_TRUE(covered(m.generatorScript))
            << "committed 生成脚本未登记完整性: " << m.generatorScript;
    }
    // 数据集登记面收敛：integrity 至少 3 条（inputs＋expected＋generate）。
    EXPECT_GE(m.integrity.size(), 3U);
}

// =====================================================================
// P-06 冻结数值表物化对照（黄金数据面的协议参数锚）
// =====================================================================

/**
 * P-06 冻结数值表物化对照（acceptance 1 提示句"——P-06 冻结数值表物化对
 * 照"的黄金数据面执行点）：inputs.protocol 六项协议数值（步长/上界/预算
 * ——行 9 代表点集规则为生成规则不经数值承载）逐字段等于产品物化点
 * makeP06FrozenRecheckParameters()（＝REQUIREMENTS 附录 C《P-06 冻结数值
 * 表》行 1~8 的唯一产品落点）。三方锚定关系：需求冻结表 ↔ 数据集协议参
 * 数 ↔ 产品物化函数——任一侧修订漂移（例如需求变更调整步长默认而数据集
 * 或实现未跟上）都在本用例显性失败（修订须走需求变更并同步三方）。
 * 同时核对四反例案例 id 词表（inputs.cases 恰四案例、id 与反例语义一
 * 一对应——数据资产的结构完整性）。
 */
TEST(TrjAt06GoldenContract, P06FrozenProtocolMaterialization_WP16T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::JsonValue inputs = tk::parseJson(readText(ds.resolveInput(
        ds.manifest().inputs.front())));
    ASSERT_TRUE(inputs.isObject());
    const tk::JsonValue& protocol = mustObject(inputs, "protocol", "inputs.protocol");

    // ---- 八值对照（数据集协议参数 ↔ 产品物化点——P-06 冻结表行 1~8）。
    const trj::RecheckParameters frozen = trj::makeP06FrozenRecheckParameters();
    EXPECT_DOUBLE_EQ(mustNumber(protocol, "maxJointStepRevolute", "protocol"),
                     frozen.maxJointStepRevolute)
        << "旋转关节步长默认漂移（P-06 行 1——冻结表/数据集/实现三方须一致）";
    EXPECT_DOUBLE_EQ(mustNumber(protocol, "jointSubdivisionCeilingRevolute",
                                "protocol"),
                     frozen.jointSubdivisionCeilingRevolute)
        << "旋转细分上界漂移（P-06 行 3——4×默认封顶）";
    EXPECT_DOUBLE_EQ(mustNumber(protocol, "maxCartesianStep", "protocol"),
                     frozen.maxCartesianStep)
        << "笛卡尔步长默认漂移（P-06 行 5）";
    EXPECT_DOUBLE_EQ(mustNumber(protocol, "cartesianSubdivisionCeiling",
                                "protocol"),
                     frozen.cartesianSubdivisionCeiling)
        << "笛卡尔细分上界漂移（P-06 行 6）";
    EXPECT_DOUBLE_EQ(mustNumber(protocol, "maxSubdivisionDepth", "protocol"),
                     static_cast<double>(frozen.maxSubdivisionDepth))
        << "细分预算层数漂移（P-06 行 7）";
    EXPECT_DOUBLE_EQ(mustNumber(protocol, "maxSubsegments", "protocol"),
                     static_cast<double>(frozen.maxSubsegments))
        << "细分预算子段数漂移（P-06 行 8——2^10）";
    // 冻结表自洽（上界＝4×默认——表行 3~6 括注）与合法性（validate 必过
    // ——协议参数可真实驱动复检）。
    EXPECT_DOUBLE_EQ(frozen.jointSubdivisionCeilingRevolute,
                     4.0 * frozen.maxJointStepRevolute);
    EXPECT_DOUBLE_EQ(frozen.cartesianSubdivisionCeiling,
                     4.0 * frozen.maxCartesianStep);
    EXPECT_NO_THROW(trj::validateRecheckParameters(frozen));

    // ---- 四反例案例 id 词表（恰四案例——与 acceptance 1 四反例一一对应）。
    const tk::JsonValue* cases = inputs.find("cases");
    ASSERT_NE(cases, nullptr);
    ASSERT_EQ(cases->items.size(), 4U) << "AT-06 四反例须恰四个算例";
    const char* expectedIds[] = {"recheck-collision", "budget-exhausted",
                                 "replan-success", "limit-exceeded"};
    for (const char* id : expectedIds) {
        EXPECT_NE(findCase(inputs, id), nullptr) << "缺少算例: " << id;
    }
}

// =====================================================================
// 反例①：段内碰撞检出（AT-06——端点无碰撞、段内碰撞必须被复检检出）
// =====================================================================

/**
 * 黄金对照（反例①）：1DOF 旋转关节装置杆扫掠与障碍盒的碰撞参数区间
 * （α＝atan(w/L) 闭式投影）＋P-06 双上界细分账目＋首检出采样定位，全部
 * 与 expected 闭式重算对照：
 *   - 细分账目精确断言（整数计数——层 7/最终子段 128/检查子段 255：
 *     关节界需 n≥20、笛卡尔界（TCP 单点弧长 0.5 m/步长 0.005 m）需 n≥100
 *     → 双上界取严 n=128=2^7；检查子段＝完全二叉树 2^8−1=255）；
 *   - 解析量经容差档案对照（区间端点/首检出参数——附录 D 第 9 项）；
 *   - 反例语义完整性：端点（s=0/1）无碰撞而段内采样（s=43/128）必检出
 *     ——AT-06"段内碰撞反例"的覆盖性语义（非端点违例）；
 *   - 结论与素材码词表（Collision＋TRJ-RECHECK-COLLISION——与产品面
 *     Recheck.hpp 结论词表/DiagCodes.hpp 码表一致）。
 * 运行期真实链路检出归 test/SmoothRecheckTest.RecheckDetectsMidPath-
 * Collision（V-14，真实 policy 会话）——本用例不重复宣称。
 */
TEST(TrjAt06GoldenContract, CaseRecheckCollision_WP16T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::JsonValue inputs = tk::parseJson(readText(ds.resolveInput(
        ds.manifest().inputs.front())));
    const tk::JsonValue expected = tk::parseJson(readText(ds.resolveExpected(
        ds.manifest().expected.front())));
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::goldenDataRoot() / "tolerance" / kProfileId
        / (std::string{"v"} + kProfileVersion + ".json"));

    // ---- 输入面（唯一数据源——场景参数直接从 inputs 读取重算）。
    const tk::JsonValue* csPtr = findCase(inputs, "recheck-collision");
    ASSERT_NE(csPtr, nullptr) << "inputs 缺少算例 recheck-collision";
    const tk::JsonValue& cs = *csPtr;
    const double travel = mustNumber(mustObject(cs, "path", "case1"), "endQ", "case1.path")
                          - mustNumber(mustObject(cs, "path", "case1"), "startQ", "case1.path");
    const double thetaC = mustNumber(mustObject(cs, "obstacle", "case1"), "centerAngle", "case1.obstacle");
    const double radialDistance = mustNumber(mustObject(cs, "obstacle", "case1"), "radialDistance", "case1.obstacle");
    const double halfWidth = mustNumber(mustObject(cs, "obstacle", "case1"), "halfWidth", "case1.obstacle");
    const double tcpRadius = mustNumber(mustObject(cs, "device", "case1"), "tcpRadius", "case1.device");

    // ---- 期望面。
    const tk::JsonValue* ecPtr = findCase(expected, "recheck-collision");
    ASSERT_NE(ecPtr, nullptr) << "expected 缺少算例 recheck-collision";
    const tk::JsonValue& ec = *ecPtr;
    const tk::JsonValue& plan = mustObject(ec, "recheck", "case1.recheck");
    const tk::JsonValue& interval = mustObject(ec, "collisionInterval", "case1");

    // 碰撞参数区间重算：α＝atan(w/L)（障碍盒对原点的角半径——闭式），
    // 路径 q(s)=s·travel 线性插值 → s 区间＝角区间/travel。
    const double alpha = std::atan(halfWidth / radialDistance);
    IRD_EXPECT_CLOSE("cases[recheck-collision].collisionInterval.lower",
                     (thetaC - alpha) / travel, interval.find("lower")->number,
                     profile, "1");
    IRD_EXPECT_CLOSE("cases[recheck-collision].collisionInterval.upper",
                     (thetaC + alpha) / travel, interval.find("upper")->number,
                     profile, "1");

    // 细分账目精确断言（整数计数无容差——P-06 双上界取严的闭式结果）。
    // 关节界：n ≥ travel/0.05 = 20；笛卡尔界：TCP 单点弧长 R·travel=0.5 m，
    // n ≥ 0.5/0.005 = 100 → 取严 n=128=2^7。
    EXPECT_EQ(plan.find("subdivisionDepthUsed")->number, 7.0) << "细分层数（2^7=128 覆盖双上界）";
    EXPECT_EQ(plan.find("finalSubsegments")->number, 128.0);
    EXPECT_EQ(plan.find("subsegmentsExamined")->number, 255.0)
        << "检查子段数＝完全二叉树节点累计 2^8−1（含初始与全部细分产物——§11.5）";
    EXPECT_EQ(plan.find("budgetExhausted")->boolean, false);

    // 实际步长（双上界下的最严值——关节 1/128 rad、笛卡尔弧长 0.5/128 m）
    // 重算对照（附录 D 第 9 项容差——双实现浮点路径差吸收）。
    IRD_EXPECT_CLOSE("cases[recheck-collision].recheck.actualMaxJointStep",
                     travel / 128.0, plan.find("actualMaxJointStep")->number,
                     profile, "rad");
    IRD_EXPECT_CLOSE("cases[recheck-collision].recheck.actualMaxCartesianStep",
                     tcpRadius * travel / 128.0,
                     plan.find("actualMaxCartesianStep")->number, profile, "m");

    // 首检出采样定位（段内定位面——TRJ-06）：采样集 s_i=i/128 中首个落入
    // 碰撞区间的下标＝ceil(lower·128)。
    const double sLower = (thetaC - alpha) / travel;
    const double sUpper = (thetaC + alpha) / travel;
    const int firstHit = static_cast<int>(std::ceil(sLower * 128.0));
    EXPECT_EQ(ec.find("firstHitSampleIndex")->number, static_cast<double>(firstHit))
        << "首检出采样下标（ceil 协议的闭式结果）";
    IRD_EXPECT_CLOSE("cases[recheck-collision].firstHitPathParameter",
                     firstHit / 128.0, ec.find("firstHitPathParameter")->number,
                     profile, "1");
    // 检出样本数与末检出下标（区间完整性——采样集对碰撞区间的完全覆盖）。
    const int lastHit = static_cast<int>(std::floor(sUpper * 128.0));
    EXPECT_EQ(ec.find("lastHitSampleIndex")->number, static_cast<double>(lastHit));
    EXPECT_EQ(ec.find("hitSamplesCount")->number, static_cast<double>(lastHit - firstHit + 1));

    // 反例语义完整性：端点无碰撞（s=0/1 不在碰撞区间内）而段内采样必检出
    // （首检出参数严格落入区间）——AT-06"段内碰撞"的覆盖性反例（非端点违例）。
    EXPECT_GT(sLower, 0.0) << "起点 s=0 须在碰撞区间外（端点无碰撞）";
    EXPECT_LT(sUpper, 1.0) << "终点 s=1 须在碰撞区间外（端点无碰撞）";
    EXPECT_GT(ec.find("firstHitPathParameter")->number, interval.find("lower")->number);
    EXPECT_LT(ec.find("firstHitPathParameter")->number, interval.find("upper")->number);

    // 零值样例锚：路径中点 s=0.5（采样点 64/128）与障碍区间中心角重合——
    // 角偏差精确零（检出记录中的零距离锚点——附录 D C4 零值覆盖）。
    EXPECT_EQ(ec.find("centerSampleIndex")->number, 64.0);
    IRD_EXPECT_CLOSE("cases[recheck-collision].centerAngleDeviation", 0.0,
                     ec.find("centerAngleDeviation")->number, profile, "rad");

    // 结论与素材码词表（与产品面词表一致——Recheck.hpp 三态/DiagCodes.hpp 码表）。
    EXPECT_EQ(ec.find("conclusion")->text, "Collision");
    EXPECT_EQ(ec.find("materialCode")->text, "TRJ-RECHECK-COLLISION");
}

// =====================================================================
// 反例②：细分预算耗尽（AT-06/R9——达预算仍不满足步长 → DataInsufficient）
// =====================================================================

/**
 * 黄金对照（反例②）：60 rad 大行程纯关节界驱动细分至预算上限（10 层/
 * 1024 子段）仍 0.05859375 rad ＞ 0.05 rad → DataInsufficient(Budget-
 * Exhausted) 附实际步长与预算占用，**不默认接受**（不执行碰撞查询——零
 * 查询账目即零值样例）；净位移恒等式 ＋30＋(−30)=0（正负抵消样例——行程
 * 度量 60 rad 不受抵消掩盖）。全部闭式重算对照：
 *   - 关节界需 n≥1200、笛卡尔界（弧长 30 m）需 n≥6000——两界同超预算
 *     上限 1024，取严者生效的账目停在预算处（expected.budgetDominance 注）；
 *   - actualMaxJointStep＝60/1024（精确二分幂——位级可断言）；
 *   - R9 语义断言：实际步长严格大于默认步长（stepOver＞0——"仍不满足"
 *     的显式数值面）。
 * 运行期真实链路反例归 test/SmoothRecheckTest.RecheckBudgetExhaustedData-
 * Insufficient（V-19，真实 policy 会话）——本用例不重复宣称。
 */
TEST(TrjAt06GoldenContract, CaseBudgetExhausted_WP16T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::JsonValue inputs = tk::parseJson(readText(ds.resolveInput(
        ds.manifest().inputs.front())));
    const tk::JsonValue expected = tk::parseJson(readText(ds.resolveExpected(
        ds.manifest().expected.front())));
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::goldenDataRoot() / "tolerance" / kProfileId
        / (std::string{"v"} + kProfileVersion + ".json"));

    // ---- 输入面（往返两腿：＋30 rad 与 −30 rad——总行程 60 rad）。
    const tk::JsonValue* csPtr = findCase(inputs, "budget-exhausted");
    ASSERT_NE(csPtr, nullptr) << "inputs 缺少算例 budget-exhausted";
    const tk::JsonValue& cs = *csPtr;
    const double legUp = mustNumber(mustObject(cs, "path", "case2"), "legUp", "case2.path");
    const double legDown = mustNumber(mustObject(cs, "path", "case2"), "legDown", "case2.path");
    const double totalTravel = std::fabs(legUp) + std::fabs(legDown);

    // ---- 期望面。
    const tk::JsonValue* ecPtr = findCase(expected, "budget-exhausted");
    ASSERT_NE(ecPtr, nullptr) << "expected 缺少算例 budget-exhausted";
    const tk::JsonValue& ec = *ecPtr;
    const tk::JsonValue& plan = mustObject(ec, "recheck", "case2.recheck");

    // 预算耗尽账目精确断言（预算上限处停账——P-06 行 7/8）。
    EXPECT_EQ(plan.find("subdivisionDepthUsed")->number, 10.0)
        << "细分层数耗尽（P-06 行 7——最大层数 10）";
    EXPECT_EQ(plan.find("finalSubsegments")->number, 1024.0)
        << "最终子段数（P-06 行 8——2^10）";
    EXPECT_EQ(plan.find("subsegmentsExamined")->number, 2047.0)
        << "检查子段数＝完全二叉树 2^11−1（§11.5 实际检查口径）";
    EXPECT_EQ(plan.find("budgetExhausted")->boolean, true);

    // 实际步长＝60/1024（精确二分幂——位级断言）且严格大于默认步长
    // （R9"仍不满足步长"的语义面）。
    EXPECT_DOUBLE_EQ(plan.find("actualMaxJointStep")->number, totalTravel / 1024.0);
    EXPECT_GT(plan.find("actualMaxJointStep")->number, 0.05)
        << "预算耗尽时实际步长必须仍超默认步长（否则不该判 DataInsufficient）";
    // 超出量重算对照（近零边际——显式数值面）。
    IRD_EXPECT_CLOSE("cases[budget-exhausted].stepOverDefault",
                     totalTravel / 1024.0 - 0.05,
                     ec.find("stepOverDefault")->number, profile, "rad");
    EXPECT_GT(ec.find("stepOverDefault")->number, 0.0);

    // 零值＋正负抵消样例：净位移恒等式（＋30＋(−30)=0——C4 正负抵消覆盖；
    // 零参考退化为 eps_abs 分量）与零碰撞查询（预算耗尽不执行碰撞查询——
    // R9 不默认接受的账目起点）。
    IRD_EXPECT_CLOSE("cases[budget-exhausted].netDisplacement", legUp + legDown,
                     ec.find("netDisplacement")->number, profile, "rad");
    EXPECT_DOUBLE_EQ(ec.find("netDisplacement")->number, 0.0)
        << "正负抵消恒等式（往返路径净位移精确零）";
    EXPECT_DOUBLE_EQ(ec.find("totalTravel")->number, totalTravel);
    EXPECT_EQ(ec.find("collisionQueriesExecuted")->number, 0.0)
        << "预算耗尽不得执行碰撞查询（R9——未充分验证段不默认接受）";

    // 结论词表（DataInsufficient 细分原因 BudgetExhausted＋素材码——与产品
    // 面 Recheck.hpp 细分原因词表/DiagCodes.hpp 码表一致）。
    EXPECT_EQ(ec.find("conclusion")->text, "DataInsufficient");
    EXPECT_EQ(ec.find("insufficientReason")->text, "BudgetExhausted");
    EXPECT_EQ(ec.find("materialCode")->text, "TRJ-RECHECK-BUDGET-EXHAUSTED");
}

// =====================================================================
// 反例③：初始路径碰撞、重规划成功（AT-06/C8——候选碰撞不判任务不可行）
// =====================================================================

/**
 * 黄金对照（反例③）：2DOF 平面装置候选直线与 C-space 障碍投影带的交叠
 * 区间（线性插值闭式映射）＋绕行构型存在性（q1 落带外＋闭式 FK 锚＋
 * C-space 裕量）→ 任务可行（不判任务不可行——碰撞作用域＝路径级，C8）。
 * 全部闭式重算对照（FK 用 std::cos/sin 独立重算与 expected 对照——附录
 * D 第 9 项容差）。种子化 RRT-Connect 重规划与逐点确定性重放归 test/
 * SmoothRecheckTest.RecheckCollisionThenReplanSucceeds（V-10，真实
 * planner/后端——§17.3 清单）——本用例只锁定数据资产面。
 */
TEST(TrjAt06GoldenContract, CaseReplanSuccess_WP16T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03", "TRJ-02"},
                  std::vector<std::string>{"AT-06"});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::JsonValue inputs = tk::parseJson(readText(ds.resolveInput(
        ds.manifest().inputs.front())));
    const tk::JsonValue expected = tk::parseJson(readText(ds.resolveExpected(
        ds.manifest().expected.front())));
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::goldenDataRoot() / "tolerance" / kProfileId
        / (std::string{"v"} + kProfileVersion + ".json"));

    // ---- 输入面（2R 装置＋候选直线＋q1 轴障碍投影带＋绕行构型）。
    const tk::JsonValue* csPtr = findCase(inputs, "replan-success");
    ASSERT_NE(csPtr, nullptr) << "inputs 缺少算例 replan-success";
    const tk::JsonValue& cs = *csPtr;
    const double q1s = mustNumber(mustObject(cs, "candidatePath", "case3"), "startQ1", "case3.candidatePath");
    const double q1e = mustNumber(mustObject(cs, "candidatePath", "case3"), "endQ1", "case3.candidatePath");
    const double bandLower = mustNumber(mustObject(cs, "obstacleBand", "case3"), "lowerQ", "case3.obstacleBand");
    const double bandUpper = mustNumber(mustObject(cs, "obstacleBand", "case3"), "upperQ", "case3.obstacleBand");
    const double detourQ1 = mustNumber(mustObject(cs, "detourConfig", "case3"), "q1", "case3.detourConfig");
    const double detourQ2 = mustNumber(mustObject(cs, "detourConfig", "case3"), "q2", "case3.detourConfig");
    const double link1 = mustNumber(mustObject(cs, "device", "case3"), "link1", "case3.device");
    const double link2 = mustNumber(mustObject(cs, "device", "case3"), "link2", "case3.device");

    // ---- 期望面。
    const tk::JsonValue* ecPtr = findCase(expected, "replan-success");
    ASSERT_NE(ecPtr, nullptr) << "expected 缺少算例 replan-success";
    const tk::JsonValue& ec = *ecPtr;
    const tk::JsonValue& interval = mustObject(ec, "candidateCollisionInterval", "case3");
    const tk::JsonValue& detour = mustObject(ec, "detour", "case3");

    // 候选路径 q1(s)=q1s+s·Δq1 与带 [lowerQ, upperQ] 的交——纯代数闭式。
    const double dq1 = q1e - q1s;
    IRD_EXPECT_CLOSE("cases[replan-success].candidateCollisionInterval.lower",
                     (bandLower - q1s) / dq1, interval.find("lower")->number,
                     profile, "1");
    IRD_EXPECT_CLOSE("cases[replan-success].candidateCollisionInterval.upper",
                     (bandUpper - q1s) / dq1, interval.find("upper")->number,
                     profile, "1");
    // 段内碰撞语义完整性：交叠区间非平凡且在路径内部（0＜lower≤upper＜1
    // ——候选路径确实"段内穿障"而非端点触障）。
    EXPECT_GT(interval.find("lower")->number, 0.0);
    EXPECT_LT(interval.find("upper")->number, 1.0);
    EXPECT_LT(interval.find("lower")->number, interval.find("upper")->number);

    // 任务可行词表（C8：候选路径碰撞＝该路径淘汰并触发重规划，不判任务
    // 不可行——envelope 语义面）与绕行构型存在性（detour q1 落带外——
    // 替代路径可生成的解析证明）。
    EXPECT_EQ(ec.find("taskFeasible")->boolean, true)
        << "候选碰撞不判任务不可行（C8 碰撞作用域——路径级）";
    EXPECT_EQ(detour.find("insideBand")->boolean, false)
        << "绕行构型须在障碍带外（存在性证明的词面）";
    const double expectedClearance =
        (detourQ1 > bandUpper) ? (detourQ1 - bandUpper) : (bandLower - detourQ1);
    IRD_EXPECT_CLOSE("cases[replan-success].detour.clearanceToBand",
                     expectedClearance, detour.find("clearanceToBand")->number,
                     profile, "rad");
    EXPECT_GT(detour.find("clearanceToBand")->number, 0.0)
        << "C-space 裕量须为正（绕行构型确实避开障碍带）";

    // 绕行构型 TCP 闭式 FK 重算（平面 2R：x=L1·cos(q1)+L2·cos(q1+q2)、
    // y 同理 sin——基座系 {B}，单位 m；与生成脚本同公式双实现对照）。
    const tk::JsonValue& tcp = mustObject(detour, "tcp", "case3.detour");
    const double tcpX = link1 * std::cos(detourQ1) + link2 * std::cos(detourQ1 + detourQ2);
    const double tcpY = link1 * std::sin(detourQ1) + link2 * std::sin(detourQ1 + detourQ2);
    IRD_EXPECT_CLOSE("cases[replan-success].detour.tcp.x", tcpX,
                     tcp.find("x")->number, profile, "m");
    IRD_EXPECT_CLOSE("cases[replan-success].detour.tcp.y", tcpY,
                     tcp.find("y")->number, profile, "m");
}

// =====================================================================
// 反例④：限制超标定位（AT-06/TRJ-06——比较型素材三要素＋段内参数定位）
// =====================================================================

/**
 * 黄金对照（反例④）：平滑鼓包 q̃(s)＝k·s＋A·sin(πs) 的内部极值闭式
 * （d q̃/ds＝0 → cos(πs*)＝−k/(Aπ)，s*∈(0.5,1) 取单调降支解）→ 峰值
 * 越上界 0.0176 rad——违例记录的比较型三要素（实际构型/限界/单位 rad）
 * 与段内定位参数 s* 全部闭式重算对照：
 *   - 零裕度边界语义：基准路径端点 q(1)=0.5 恰在限位上界——裕度精确零
 *     且不触发违例（越限判定＝严格大于——零值样例锚）；
 *   - 近零探针：限位上界压至峰值下方 1e−6 rad——近零越限量仍必须检出
 *     （mustDetect=true——近零违例不得被容差吞掉，附录 D 第 10 项容差是
 *     校验判定容差而非吞没通道）；
 *   - 素材词表（TRJ-LIMIT-EXCEEDED——与产品面 DiagCodes.hpp 码表一致；
 *     phase 定位载体归产品面 kPhaseRecheck——RecheckContractTest 已锁）。
 * 运行期真实链路检出归 test/SmoothRecheckTest.RecheckDetectsLimitViolation
 * （V-15，真实 policy 会话）——本用例不重复宣称。
 */
TEST(TrjAt06GoldenContract, CaseLimitExceeded_WP16T13_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-05", "TRJ-06"},
                  std::vector<std::string>{"AT-06"});

    const tk::GoldenDataset ds = tk::GoldenDataset::load(goldenRef());
    const tk::JsonValue inputs = tk::parseJson(readText(ds.resolveInput(
        ds.manifest().inputs.front())));
    const tk::JsonValue expected = tk::parseJson(readText(ds.resolveExpected(
        ds.manifest().expected.front())));
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::goldenDataRoot() / "tolerance" / kProfileId
        / (std::string{"v"} + kProfileVersion + ".json"));

    // ---- 输入面（基准斜率/鼓包幅值/限位上界/近零探针越限量）。
    const tk::JsonValue* csPtr = findCase(inputs, "limit-exceeded");
    ASSERT_NE(csPtr, nullptr) << "inputs 缺少算例 limit-exceeded";
    const tk::JsonValue& cs = *csPtr;
    const double slope = mustNumber(mustObject(cs, "basePath", "case4"), "slope", "case4.basePath");
    const double amplitude = mustNumber(mustObject(cs, "smoothBulge", "case4"), "amplitude", "case4.smoothBulge");
    const double limitUpper = mustNumber(mustObject(cs, "jointLimit", "case4"), "upper", "case4.jointLimit");
    const double probeExceed = mustNumber(mustObject(cs, "nearZeroProbe", "case4"), "exceed", "case4.nearZeroProbe");

    // ---- 闭式重算（与生成脚本同公式、双实现对照）。
    // 内部极值：cos(πs*)＝−k/(Aπ)（k＜Aπ 时存在——0.5＜0.2π 成立）；
    // s*∈(0.5,1) 取 acos 单调降支解 → sin(πs*)＞0 取正根。
    const double cosPiS = -slope / (amplitude * 3.14159265358979323846);
    const double sStar = std::acos(cosPiS) / 3.14159265358979323846;
    const double sinPiS = std::sqrt(1.0 - cosPiS * cosPiS);
    const double qPeak = slope * sStar + amplitude * sinPiS;
    ASSERT_LT(slope, amplitude * 3.14159265358979323846)
        << "场景前提：k＜Aπ（内部极值存在——鼓包才能越过线性基准）";

    // ---- 期望面。
    const tk::JsonValue* ecPtr = findCase(expected, "limit-exceeded");
    ASSERT_NE(ecPtr, nullptr) << "expected 缺少算例 limit-exceeded";
    const tk::JsonValue& ec = *ecPtr;
    const tk::JsonValue& violation = mustObject(ec, "violation", "case4");
    const tk::JsonValue& peak = mustObject(ec, "peak", "case4");

    // 峰值位置与越限量（解析量——附录 D 第 9 项容差对照）。
    IRD_EXPECT_CLOSE("cases[limit-exceeded].peak.s", sStar, peak.find("s")->number,
                     profile, "1");
    IRD_EXPECT_CLOSE("cases[limit-exceeded].peak.q", qPeak, peak.find("q")->number,
                     profile, "rad");
    IRD_EXPECT_CLOSE("cases[limit-exceeded].peak.exceed", qPeak - limitUpper,
                     peak.find("exceed")->number, profile, "rad");
    EXPECT_GT(peak.find("exceed")->number, 0.0)
        << "平滑鼓包峰值必须真实越限（反例语义——平滑诱发越限）";

    // 比较型素材三要素（ERR-01/UX-03——实际构型/限界/单位逐项齐备）与
    // 段内定位一致性（violation.pathParameter＝peak.s——同一违例单一事实）。
    IRD_EXPECT_CLOSE("cases[limit-exceeded].violation.actualQ", qPeak,
                     violation.find("actualQ")->number, profile, "rad");
    EXPECT_DOUBLE_EQ(violation.find("boundQ")->number, limitUpper);
    EXPECT_EQ(violation.find("unit")->text, "rad");
    EXPECT_EQ(violation.find("axis")->number, 0.0);
    EXPECT_EQ(violation.find("upper")->boolean, true);
    IRD_EXPECT_CLOSE("cases[limit-exceeded].violation.pathParameter", sStar,
                     violation.find("pathParameter")->number, profile, "1");
    // 段内定位一致性：violation.pathParameter 与 peak.s 是同一违例的单一
    // 事实（定位面与极值面同源——两处记录不得漂移）。
    EXPECT_DOUBLE_EQ(violation.find("pathParameter")->number, peak.find("s")->number)
        << "违例定位参数与峰值位置须一致（同一违例单一事实）";
    EXPECT_EQ(violation.find("materialCode")->text, "TRJ-LIMIT-EXCEEDED");

    // 零裕度边界语义（零值样例）：基准端点 q(1)=slope 恰在限位上界——裕度
    // 精确零、不触发违例（严格大于才违例——边界判定语义显式化）。
    const tk::JsonValue& zeroAnchor = mustObject(ec, "zeroMarginAnchor", "case4");
    EXPECT_DOUBLE_EQ(zeroAnchor.find("margin")->number, 0.0);
    EXPECT_EQ(zeroAnchor.find("triggersViolation")->boolean, false)
        << "零裕度不得触发违例（越限判定＝严格大于——边界语义）";

    // 近零探针（近零样例）：限位上界压至峰值下方 1e−6 rad——近零越限量
    // 仍必须检出（mustDetect=true——容差不吞真实违例）。
    const tk::JsonValue& probe = mustObject(ec, "nearZeroProbe", "case4");
    IRD_EXPECT_CLOSE("cases[limit-exceeded].nearZeroProbe.limitUpper",
                     qPeak - probeExceed, probe.find("limitUpper")->number,
                     profile, "rad");
    IRD_EXPECT_CLOSE("cases[limit-exceeded].nearZeroProbe.exceed", probeExceed,
                     probe.find("exceed")->number, profile, "rad");
    EXPECT_GT(probe.find("exceed")->number, 0.0);
    EXPECT_LT(probe.find("exceed")->number, 1e-5)
        << "探针越限量须为近零量级（本用例的近零语义锚）";
    EXPECT_EQ(probe.find("mustDetect")->boolean, true);
}

// =====================================================================
// 档案契约：trj-at06 档案就绪＋fieldPath 覆盖＋附录 D 第 9 项口径
// =====================================================================

/**
 * 容差档案登记契约（acceptance 2"容差档案"面）：
 *   - 档案装载通过（schema ird-tolerance-profile/1——"只准更严"规则由
 *     装载构造性执行：tolerance ≤ allowedMax 逐分量核对）；
 *   - fieldPath 覆盖消费面的具体路径（探针 resolve 逐条命中——黄金数值
 *     断言经档案通道的前提；未命中抛 TestKitError＝失败）；
 *   - 附录 D 第 9 项口径钉扎：全部条目 source=appendixD-fixed 且相对容差
 *     ≤1×10⁻⁹（解析算例对照——黄金算例自带更严值 eps_abs 1e−12 逐例声
 *     明）；档案只服务测试侧黄金对照，不承载任何运行时校验阈值（附录 D
 *     C7——TRJ-05 运行校验容差走第 10 项固定值不经本档案）。
 */
TEST(TrjAt06GoldenContract, ToleranceProfileChannel_WP16T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{"AT-06"});

    // ---- 档案装载（manifest 引用链一致——路径按 TestPaths 规则解析）。
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::goldenDataRoot() / "tolerance" / kProfileId
        / (std::string{"v"} + kProfileVersion + ".json"));
    EXPECT_EQ(profile.profileId, std::string(kProfileId));
    EXPECT_EQ(profile.version, std::string(kProfileVersion));
    EXPECT_GE(profile.entries.size(), 25U)
        << "档案条目须覆盖四反例全部浮点解析量（细分步长/区间/FK/极值/探针）";

    // ---- fieldPath 覆盖面（探针 resolve——具体路径必须命中模板条目）。
    const char* probes[] = {
        "cases[recheck-collision].recheck.actualMaxJointStep",
        "cases[recheck-collision].recheck.actualMaxCartesianStep",
        "cases[recheck-collision].collisionInterval.lower",
        "cases[recheck-collision].firstHitPathParameter",
        "cases[recheck-collision].centerAngleDeviation",
        "cases[budget-exhausted].recheck.actualMaxJointStep",
        "cases[budget-exhausted].stepOverDefault",
        "cases[budget-exhausted].netDisplacement",
        "cases[replan-success].candidateCollisionInterval.lower",
        "cases[replan-success].detour.tcp.x",
        "cases[replan-success].detour.tcp.y",
        "cases[replan-success].detour.clearanceToBand",
        "cases[limit-exceeded].violation.actualQ",
        "cases[limit-exceeded].violation.pathParameter",
        "cases[limit-exceeded].peak.exceed",
        "cases[limit-exceeded].zeroMarginAnchor.margin",
        "cases[limit-exceeded].nearZeroProbe.exceed",
    };
    for (const char* p : probes) {
        EXPECT_NO_THROW((void)profile.resolve(p))
            << "fieldPath 未命中容差条目: " << p;
    }

    // ---- 附录 D 第 9 项口径（解析算例对照——标量相对 1e−9；档案只服务
    // 测试侧黄金对照，不引入运行时校验阈值——C7）。
    for (const tk::ToleranceEntry& e : profile.entries) {
        EXPECT_EQ(e.source, tk::ToleranceSource::AppendixDFixed)
            << "条目 " << e.fieldPath
            << " 来源须为 appendixD-fixed（黄金对照容差——不引入运行时阈值通道）";
        EXPECT_LE(e.tolerance.relative, 1e-9)
            << "条目 " << e.fieldPath << " 相对容差超出附录 D 第 9 项上限";
        EXPECT_GE(e.tolerance.absolute, 0.0);
    }
}

// =====================================================================
// 数据根契约：消费链定位一致（golden/ 与 tolerance/ 同根）
// =====================================================================

/**
 * 消费链定位契约：trj-at06-recheck-golden 数据集与 trj-at06 档案位于同一
 * 数据根（TestPaths 唯一入口——任何单元不得自行拼装数据根路径，testkit
 * §4.6）；本用例钉扎"黄金数据目录布局"（golden/<id>/<version>/manifest.
 * json）的 trajectory 侧落位一致性。
 */
TEST(TrjAt06GoldenContract, DataRootLayout_WP16T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01"},
                  std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    const auto manifestPath = root / "golden" / goldenRef().datasetId
                              / goldenRef().version / "manifest.json";
    EXPECT_TRUE(std::filesystem::exists(manifestPath))
        << "manifest 不在数据根布局位置: " << manifestPath.string();
    // 数据根下单元子目录（golden/tolerance）同根并存——消费链两通道可达。
    EXPECT_TRUE(std::filesystem::exists(root / "golden" / goldenRef().datasetId));
    EXPECT_TRUE(std::filesystem::exists(root / "tolerance" / kProfileId));
}
