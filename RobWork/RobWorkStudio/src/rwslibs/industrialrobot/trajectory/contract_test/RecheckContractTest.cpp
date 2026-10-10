/**
 * @file   RecheckContractTest.cpp
 * @brief  复检协议面的契约测试（TrjRecheckContract，WP-16-T07）——P-06 冻
 *         结数值表物化对照（acceptance 1 的数值唯一来源钉扎）＋R-POL-5 结
 *         构防覆盖（acceptance 2"无私有线宽/阈值"）＋AT-06 三反例的素材
 *         词表锁定（acceptance 1"三反例全部被契约测试锁定"的契约半区——
 *         运行期真实链路反例在 test/SmoothRecheckTest.cpp）。
 *
 * 设计依据：
 *   - units/trajectory.md §11.1/§11.2/§11.3/§11.5/§15.6（复检协议全要素；
 *     "本域代码不携带任何默认值字面量"的 P-06 物化口径——文件头注登记）、
 *     REQUIREMENTS 附录 C《P-06 冻结数值表》（八行数值——本测试逐字段对
 *     照冻结表，任何漂移即编译期外显失败）、AT-06（段内碰撞反例/预算耗尽
 *     反例/重规划反例的素材路由）、ARC-05（安全间距阈值唯一来源＝policy
 *     ——请求面零阈值字段）
 *   - 先例：PathPlannerContractTest.cpp（非条件源静态扫描＋接口面钉扎同款
 *     形态；stripComments/文件读取设施同源拷贝——契约测试间不共享代码，
 *     各文件自持）
 *   - 任务契约 tasks/foundation/WP-16-T07.json（acceptance 1/2/3）
 *
 * 本文件为**非条件源**（makeP06FrozenRecheckParameters/validateRecheck-
 * Parameters 为 Recheck.hpp 内 inline 纯值函数，零 rw 符号——冒烟模式同
 * 样执行；文本扫描消费 IRD_TRAJECTORY_UNIT_ROOT 注入的源码树路径）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/trajectory/Recheck.hpp>
#include <sdurws/ird/trajectory/Smooth.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace trj = sdurws::ird::trajectory;

namespace {

/// trajectory 单元树根（industrialrobot 目录——IRD_TRAJECTORY_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_TRAJECTORY_UNIT_ROOT};
    return dir;
}

/// 读取文件全文（不可读显性失败）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 线性单遍注释剥离器（PathPlannerContractTest 同款状态机——判定对象是代
/// 码行为，注释内的文档性提及不属行为；不用 regex——MSVC 回溯爆栈先例）。
std::string stripComments(const std::string& src)
{
    enum class State { Code, LineComment, BlockComment, StrLiteral, CharLiteral };
    std::string out;
    out.reserve(src.size());
    State state = State::Code;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char ch = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (ch == '/' && next == '/') {
                state = State::LineComment;
                out += "  ";
                ++i;
            } else if (ch == '/' && next == '*') {
                state = State::BlockComment;
                out += "  ";
                ++i;
            } else if (ch == '"') {
                state = State::StrLiteral;
                out += ch;
            } else if (ch == '\'') {
                state = State::CharLiteral;
                out += ch;
            } else {
                out += ch;
            }
            break;
        case State::LineComment:
            if (ch == '\n') { state = State::Code; }
            out += (ch == '\n') ? '\n' : ' ';
            break;
        case State::BlockComment:
            if (ch == '*' && next == '/') {
                state = State::Code;
                out += "  ";
                ++i;
            } else {
                out += (ch == '\n') ? '\n' : ' ';
            }
            break;
        case State::StrLiteral:
            if (ch == '\\') {
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '"') { state = State::Code; }
            }
            break;
        case State::CharLiteral:
            if (ch == '\\') {
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '\'') { state = State::Code; }
            }
            break;
        }
    }
    return out;
}

}  // namespace

/**
 * P-06 冻结数值表物化对照（acceptance 1 的数值唯一来源）：makeP06Frozen-
 * RecheckParameters 的八项数值逐字段等于 REQUIREMENTS 附录 C《P-06 冻结数
 * 值表》行 1~8（2026-10-10 需求所有者确认草案，WP-16-T02 执行冻结）——
 * 本测试是冻结表与实现之间的唯一对照点，任何一侧修订漂移都在此显性失败
 * （修订须走需求变更并同步本测试与单元卡）。
 */
TEST(TrjRecheckContract, P06FrozenParametersMatchFrozenTable_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    const trj::RecheckParameters p = trj::makeP06FrozenRecheckParameters();
    // 行 1：关节空间最大步长——旋转关节默认（rad）。
    EXPECT_DOUBLE_EQ(p.maxJointStepRevolute, 0.05);
    // 行 2：关节空间最大步长——移动关节默认（m）。
    EXPECT_DOUBLE_EQ(p.maxJointStepPrismatic, 0.005);
    // 行 3：关节空间细分上界——旋转关节 4×默认封顶（rad，硬上限）。
    EXPECT_DOUBLE_EQ(p.jointSubdivisionCeilingRevolute, 0.2);
    // 行 4：关节空间细分上界——移动关节（m）。
    EXPECT_DOUBLE_EQ(p.jointSubdivisionCeilingPrismatic, 0.02);
    // 行 5：笛卡尔最大步长——默认（m；TCP＋代表点集最大位移口径）。
    EXPECT_DOUBLE_EQ(p.maxCartesianStep, 0.005);
    // 行 6：笛卡尔细分上界（m）。
    EXPECT_DOUBLE_EQ(p.cartesianSubdivisionCeiling, 0.02);
    // 行 7：细分预算——最大层数（二分）。
    EXPECT_EQ(p.maxSubdivisionDepth, 10U);
    // 行 8：细分预算——单段最大子段数（2¹⁰）。
    EXPECT_EQ(p.maxSubsegments, 1024U);
    // 来源标记（审计可辨——policy 优先的读取顺序由调用方编排）。
    EXPECT_EQ(p.source, trj::RecheckParameters::Source::P06FrozenDefault);
    // 冻结表自洽：上界＝4×默认（表行 3~6 括注"4×默认值封顶"）。
    EXPECT_DOUBLE_EQ(p.jointSubdivisionCeilingRevolute, 4.0 * p.maxJointStepRevolute);
    EXPECT_DOUBLE_EQ(p.jointSubdivisionCeilingPrismatic, 4.0 * p.maxJointStepPrismatic);
    EXPECT_DOUBLE_EQ(p.cartesianSubdivisionCeiling, 4.0 * p.maxCartesianStep);
}

/**
 * 参数校验 fail-fast 面（§15.6"调用方私传步长阈值覆盖 policy→fail-fast"的
 * 执行点）：越顶步长（P-06 行 3~6 封顶语义）、非正步长、零预算三组非法配
 * 置全部拒绝；冻结默认集合法通过。
 */
TEST(TrjRecheckContract, ParameterValidationFailsFast_WP16T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{});

    // 合法基准（冻结默认——必过 validate）。
    trj::RecheckParameters good = trj::makeP06FrozenRecheckParameters();
    EXPECT_NO_THROW(trj::validateRecheckParameters(good));

    // 越顶：policy 放宽步长超过细分上界（封顶语义——硬上限）。
    trj::RecheckParameters overCeiling = good;
    overCeiling.maxJointStepRevolute = 0.21;  // > 行 3 上界 0.2 rad
    EXPECT_THROW(trj::validateRecheckParameters(overCeiling), trj::TrajectoryError);

    // 非正步长（0 与负值——细分不可终止/语义非法）。
    trj::RecheckParameters zeroStep = good;
    zeroStep.maxCartesianStep = 0.0;
    EXPECT_THROW(trj::validateRecheckParameters(zeroStep), trj::TrajectoryError);
    trj::RecheckParameters negativeStep = good;
    negativeStep.maxJointStepPrismatic = -0.005;
    EXPECT_THROW(trj::validateRecheckParameters(negativeStep), trj::TrajectoryError);

    // 零预算（层数/子段数——0 使协议不可终止）。
    trj::RecheckParameters zeroDepth = good;
    zeroDepth.maxSubdivisionDepth = 0;
    EXPECT_THROW(trj::validateRecheckParameters(zeroDepth), trj::TrajectoryError);
    trj::RecheckParameters zeroSegments = good;
    zeroSegments.maxSubsegments = 0;
    EXPECT_THROW(trj::validateRecheckParameters(zeroSegments), trj::TrajectoryError);
}

/**
 * AT-06 三反例的素材词表锁定（acceptance 1"三反例全部被契约测试锁定"的
 * 契约半区）：①DiagCodes.hpp 在册三码文本与卡面 §14.4 登记值一致（码值
 * 唯一书写点）；②Recheck.cpp 代码面（剥注释）实际消费三码——反例的结论
 * →素材路由在产品面存在（运行期真实链路反例锁定在 test/SmoothRecheckTest
 * ——两半区合起来构成 AT-06 三反例的完整锁定）；③结论词表三态封闭＋
 * DataInsufficient 两细分原因（预算耗尽/验证器缺失）＋顶层取消态（UX-03）。
 */
TEST(TrjRecheckContract, At06CounterExampleMaterialPins_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    // ①码值常量（DiagCodes.hpp——§14.4 登记表行 7/8/9 的连字符串原样）。
    EXPECT_EQ(std::string(trj::kTrjRecheckCollision), "TRJ-RECHECK-COLLISION");
    EXPECT_EQ(std::string(trj::kTrjRecheckBudgetExhausted),
              "TRJ-RECHECK-BUDGET-EXHAUSTED");
    EXPECT_EQ(std::string(trj::kTrjRecheckDataInsufficient),
              "TRJ-RECHECK-DATA-INSUFFICIENT");
    EXPECT_EQ(std::string(trj::kTrjLimitExceeded), "TRJ-LIMIT-EXCEEDED");

    // ②Recheck.cpp 代码面消费三码（剥注释扫描——反例素材路由的产品面
    // 存在性；phase 词表常量 kPhaseRecheck 同查）。
    const fs::path impl = unitRoot() / "trajectory" / "src" / "Recheck.cpp";
    ASSERT_TRUE(fs::exists(impl)) << "Recheck.cpp 缺失（复检实现失实）";
    const std::string code = stripComments(readFile(impl));
    EXPECT_NE(code.find("kTrjRecheckCollision"), std::string::npos)
        << "段内碰撞检出素材码未被复检实现消费（AT-06 反例①路由缺失）";
    EXPECT_NE(code.find("kTrjRecheckBudgetExhausted"), std::string::npos)
        << "预算耗尽素材码未被复检实现消费（AT-06 反例②路由缺失）";
    EXPECT_NE(code.find("kTrjRecheckDataInsufficient"), std::string::npos)
        << "验证器缺失素材码未被复检实现消费（AT-06 反例③半/KIN-05 路由缺失）";
    EXPECT_NE(code.find("kPhaseRecheck"), std::string::npos)
        << "复检 phase token 未被实现消费（TRJ-06 定位载体缺失）";

    // ③词表封闭性（结论三态无第四态——§15.6；细分原因两值；顶层二态）。
    const fs::path header = unitRoot() / "trajectory" / "include" / "sdurws" / "ird"
        / "trajectory" / "Recheck.hpp";
    ASSERT_TRUE(fs::exists(header)) << "Recheck.hpp 缺失";
    const std::string headerCode = stripComments(readFile(header));
    EXPECT_NE(headerCode.find("enum class RecheckConclusion"), std::string::npos)
        << "结论词表缺失";
    EXPECT_NE(headerCode.find("Passed,"), std::string::npos) << "通过态缺失";
    EXPECT_NE(headerCode.find("Collision,"), std::string::npos) << "检出态缺失";
    EXPECT_NE(headerCode.find("DataInsufficient,"), std::string::npos)
        << "数据不足态缺失";
    EXPECT_NE(headerCode.find("enum class RecheckDataInsufficientReason"),
              std::string::npos) << "细分原因词表缺失";
    EXPECT_NE(headerCode.find("BudgetExhausted,"), std::string::npos)
        << "预算耗尽原因缺失（R9）";
    EXPECT_NE(headerCode.find("EvidenceUnavailable,"), std::string::npos)
        << "验证器缺失原因缺失（KIN-05）";
    EXPECT_NE(headerCode.find("enum class RecheckStatus"), std::string::npos)
        << "顶层取消态词表缺失（UX-03 承载）";
}

/**
 * R-POL-5 结构防覆盖（acceptance 2"安全间距阈值消费 policy 工程策略，无私
 * 有线宽/阈值"）：RecheckRequest/RecheckParameters/SmoothRequest 的成员清
 * 单层面不存在任何间距/线宽/阈值字段——查询与请求不携带阈值（阈值唯一来
 * 源＝会话绑定的 EngineeringPolicySet，ARC-05）；越顶步长由 validate 拒绝
 * （上一用例）。同时钉扎复检请求面不出现 policy 阈值词汇的消费代码
 * （Recheck.cpp 剥注释扫描 safetyClearance 零命中——间距行为完全由会话
 * 内部承载）。
 */
TEST(TrjRecheckContract, NoPrivateThresholdField_WP16T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04", "ARC-05"},
                  std::vector<std::string>{"AT-19"});

    const fs::path header = unitRoot() / "trajectory" / "include" / "sdurws" / "ird"
        / "trajectory" / "Recheck.hpp";
    const fs::path smoothHeader = unitRoot() / "trajectory" / "include" / "sdurws"
        / "ird" / "trajectory" / "Smooth.hpp";
    ASSERT_TRUE(fs::exists(header));
    ASSERT_TRUE(fs::exists(smoothHeader));
    const std::string recheckCode = stripComments(readFile(header));
    const std::string smoothCode = stripComments(readFile(smoothHeader));

    // 请求/参数面零阈值成员（成员清单即契约——出现即结构性越权）。
    EXPECT_EQ(recheckCode.find("safetyClearance"), std::string::npos)
        << "复检头文件出现安全间距字段（ARC-05 越权——阈值唯一来源＝policy）";
    EXPECT_EQ(recheckCode.find("clearance"), std::string::npos)
        << "复检头文件出现间距词汇成员";
    EXPECT_EQ(recheckCode.find("marginWidth"), std::string::npos)
        << "复检头文件出现私有线宽字段";
    EXPECT_EQ(smoothCode.find("safetyClearance"), std::string::npos)
        << "平滑头文件出现安全间距字段（平滑零 policy 消费——§11.4）";

    // 实现面零间距阈值消费（间距判定在 policy 会话内部——AT-19 三入口
    // 一致的结构面）。
    const fs::path impl = unitRoot() / "trajectory" / "src" / "Recheck.cpp";
    const std::string implCode = stripComments(readFile(impl));
    EXPECT_EQ(implCode.find("safetyClearance"), std::string::npos)
        << "复检实现消费安全间距数值（间距判定越权——归 policy 会话）";
}

/**
 * §15.6/§15.7 接口交付面钉扎：Recheck.hpp/Smooth.hpp 承诺的公共交付面逐
 * 项在册——复检管线入口（recheckSegment）、P-06 冻结表物化（makeP06Frozen-
 * RecheckParameters）、代表点集采样器（IRepresentPointSampler::sample）、
 * 被检几何（IPathGeometry::sampleAt）、预算占用六字段（§11.5 结构）、平滑
 * 管线入口（smooth）与折线求值器工厂（makeLinearJointPathGeometry）。公共
 * 接口的每项交付至少被一条经接口消费的用例钉扎（接口路径零覆盖教训——
 * WP-20-T03），本用例钉"承诺存在性"，运行期行为归 _test 套件。
 */
TEST(TrjRecheckContract, RecheckSmoothHeaderPinsDeliveredFace_WP16T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{});

    const fs::path recheckHeader = unitRoot() / "trajectory" / "include" / "sdurws"
        / "ird" / "trajectory" / "Recheck.hpp";
    const fs::path smoothHeader = unitRoot() / "trajectory" / "include" / "sdurws"
        / "ird" / "trajectory" / "Smooth.hpp";
    ASSERT_TRUE(fs::exists(recheckHeader));
    ASSERT_TRUE(fs::exists(smoothHeader));
    const std::string recheckText = readFile(recheckHeader);
    const std::string smoothText = readFile(smoothHeader);

    // 复检管线入口与 P-06 物化（§15.6）。
    EXPECT_NE(recheckText.find("RecheckOutcome recheckSegment(const RecheckRequest&"),
              std::string::npos) << "复检管线入口签名缺失";
    EXPECT_NE(recheckText.find("makeP06FrozenRecheckParameters"), std::string::npos)
        << "P-06 冻结表物化入口缺失";
    EXPECT_NE(recheckText.find("validateRecheckParameters"), std::string::npos)
        << "参数校验入口缺失";
    // 代表点集采样器（P-06 行 9 消费接口——R9 度量对象）。
    EXPECT_NE(recheckText.find("class IRepresentPointSampler"), std::string::npos)
        << "代表点集采样器接口缺失";
    EXPECT_NE(recheckText.find(
                  "virtual std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q& q) const = 0;"),
              std::string::npos) << "采样器纯虚签名缺失";
    // 被检几何（平滑产物交付面——复检输入）。
    EXPECT_NE(recheckText.find("const IPathGeometry* path"), std::string::npos)
        << "被检几何请求字段缺失";
    // §11.5 预算占用六字段（R9"附实际步长与预算占用"的承载）。
    EXPECT_NE(recheckText.find("struct RecheckBudgetUsage"), std::string::npos)
        << "预算占用结构缺失";
    EXPECT_NE(recheckText.find("actualMaxJointStep"), std::string::npos)
        << "实际最大关节步长字段缺失";
    EXPECT_NE(recheckText.find("actualMaxCartesianStep"), std::string::npos)
        << "实际最大笛卡尔步长字段缺失";
    EXPECT_NE(recheckText.find("subdivisionDepthUsed"), std::string::npos)
        << "实际细分层数字段缺失";
    EXPECT_NE(recheckText.find("subdivisionBudget"), std::string::npos)
        << "预算上界字段缺失";
    EXPECT_NE(recheckText.find("subsegmentsExamined"), std::string::npos)
        << "实际检查子段数字段缺失";
    EXPECT_NE(recheckText.find("budgetExhausted"), std::string::npos)
        << "预算耗尽标记字段缺失";
    // 平滑管线（§15.7——第一提交面）。
    EXPECT_NE(smoothText.find("SmoothOutcome smooth(const SmoothRequest&"),
              std::string::npos) << "平滑管线入口签名缺失";
    EXPECT_NE(smoothText.find("makeLinearJointPathGeometry"), std::string::npos)
        << "折线求值器工厂缺失";
    EXPECT_NE(smoothText.find("virtual rw::math::Q sampleAt(double s) const = 0;"),
              std::string::npos) << "段几何求值纯虚签名缺失";
    // 平滑四态词表（§15.7）＋取消态（DTB §5.4 表尾追加登记）。
    EXPECT_NE(smoothText.find("Smoothed,"), std::string::npos) << "平滑成功态缺失";
    EXPECT_NE(smoothText.find("NoChange,"), std::string::npos) << "零变化态缺失";
    EXPECT_NE(smoothText.find("ToleranceViolated,"), std::string::npos)
        << "容差作废态缺失";
    EXPECT_NE(smoothText.find("GiveUpAfterRetries,"), std::string::npos)
        << "重试放弃态缺失";
    EXPECT_NE(smoothText.find("Canceled,"), std::string::npos) << "取消态缺失";
}
