/**
 * @file   CommandsBoundaryContractTest.cpp
 * @brief  WP-17-T08 契约测试——领域命令面（§10.7/§9.5）单元边界契约：
 *         命令 token 词表与零修订会话契约的编译期静态钉扎（§9.5 表五
 *         token 原文＋KIN-06/AT-04 四常量同值对账锚）＋命令面零动力学
 *         计算红线的源码级静态防线（§10.7"处理器内零动力学计算"）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作表五命令 token——跨版本命令契约；
 *     replay-at 行 View3DSessionPoseContract 四常量"零修订机器可断言"）
 *   - units/dynamics.md §10.7（IDynamicsCommandHandler 硬边界——"处理
 *     器内零动力学计算（组装快照/提交任务/投影查询）；零修订（全部会
 *     话命令，AT-04）"）、§2.4 O13（适配器非命令权威——注册面归 ui）
 *   - 需求 DYN-08、AT-04（预览类交互不产生项目修订）、KIN-06（三维
 *     交互只改变会话姿态）
 *   - 先例：RneaBoundaryContractTest.cpp（产品面词表扫描＋剥注释形态）、
 *     ForwardDynamicsBoundaryContractTest.cpp（词表锁定值编译期钉扎）
 *   - 对账口径（诚实登记）：ui 单元 View3DSessionPoseContract 四常量的
 *     ui 侧钉扎在 sdurws_ird_ui_test View3DContractTest（WP-10-T05 落
 *     位）；dynamics 依赖白名单（卡 §3.2 五登记边）不含 ui——R-1/R-2
 *     红线禁止本单元 include ui 公共头，故本测试以**同一文档出处**
 *     （ui.md/View3DContract.hpp 的 KIN-06/AT-04 钉住值）字面冻结
 *     dynamics 侧 kReplaySessionContract 四值：两侧常量各自被测试钉住、
 *     值漂移各自失败，T09 装配层在同一编译单元消费两侧时即自然对账。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/dynamics/Commands.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

// =====================================================================
// 编译期静态钉扎（词表字面＋零修订契约四值——改动即本 TU 编译失败，
// 语义变更必须走单元卡增量修订，代码不得私改）。
// =====================================================================

namespace sdurws::ird::dynamics {
namespace {

// §9.5 表五行 token 字面冻结（跨版本命令契约——ui CommandRegistry 注册
// 面与历史会话的锚；漂移即断链）。
static_assert(kCmdAnalyze == "dynamics.analyze", "§9.5 行 1 token 冻结");
static_assert(kCmdShowCurves == "dynamics.show-curves", "§9.5 行 2 token 冻结");
static_assert(kCmdLocatePeak == "dynamics.locate-peak", "§9.5 行 3 token 冻结");
static_assert(kCmdReplayAt == "dynamics.replay-at", "§9.5 行 4 token 冻结");
static_assert(kCmdExportCurveData == "dynamics.export-curve-data", "§9.5 行 5 token 冻结");
static_assert(kCommandTokens.size() == 5u, "词表封闭——五命令全集");

// 零修订会话契约四值冻结（KIN-06/AT-04 钉住值——与 ui 单元
// View3DSessionPoseContract 同值对账，文件头"对账口径"；任一位翻转即
// 编译失败＝必须走需求修订）。
static_assert(kReplaySessionContract.sessionStateOnly == true, "KIN-06 只改变会话姿态");
static_assert(kReplaySessionContract.writesDesignModel == false, "KIN-06 不修改设计模型");
static_assert(kReplaySessionContract.producesRevision == false, "AT-04 零修订");
static_assert(kReplaySessionContract.invalidatesResults == false, "CON-02 当前性正交");

}  // namespace
}  // namespace sdurws::ird::dynamics

namespace {

/// dynamics 单元树根（IRD_DYNAMICS_UNIT_ROOT 注入——同 BuildGraph 契约
/// 测试口径）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DYNAMICS_UNIT_ROOT};
    return dir;
}

/// 剥离 // 行注释与 /* */ 块注释（词表扫描只针对**代码**——注释里提及
/// 评估器等词不是消费符号；同 RneaBoundaryContractTest 形态）。
std::string stripComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    enum class State { Code, LineComment, BlockComment } state = State::Code;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char n = i + 1 < text.size() ? text[i + 1] : '\0';
        if (state == State::Code) {
            if (c == '/' && n == '/') {
                state = State::LineComment;
                ++i;
            } else if (c == '/' && n == '*') {
                state = State::BlockComment;
                ++i;
            } else {
                out.push_back(c);
            }
        } else if (state == State::LineComment) {
            if (c == '\n') {
                state = State::Code;
                out.push_back(c);
            }
        } else {  // BlockComment
            if (c == '*' && n == '/') {
                state = State::Code;
                ++i;
            }
        }
    }
    return out;
}

/// 读文件全文（二进制安全读——文本扫描用）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};  // 缺文件由调用方 ASSERT 兜底显性失败
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

}  // namespace

// =====================================================================
// DynCommandsBoundary 组——命令面单元边界契约。
// =====================================================================

/**
 * @brief 词表与零修订契约运行期复核（编译期 static_assert 的可读性锚
 *        ——失败信息直接可读；同时钉 CommandOutcome 拒绝形态默认安全：
 *        默认构造＝未受理，不作任何会话效果承诺）。
 */
TEST(DynCommandsBoundary, CommandVocabularyAndOutcomeDefaults)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{"AT-04", "KIN-06"});

    // 词表运行期复核（编译期已钉——此处为可读失败面＋行序核对）。
    ASSERT_EQ(sdurws::ird::dynamics::kCommandTokens.size(), 5u);
    EXPECT_EQ(sdurws::ird::dynamics::kCommandTokens[0], std::string_view{"dynamics.analyze"});
    EXPECT_EQ(sdurws::ird::dynamics::kCommandTokens[1], std::string_view{"dynamics.show-curves"});
    EXPECT_EQ(sdurws::ird::dynamics::kCommandTokens[2], std::string_view{"dynamics.locate-peak"});
    EXPECT_EQ(sdurws::ird::dynamics::kCommandTokens[3], std::string_view{"dynamics.replay-at"});
    EXPECT_EQ(sdurws::ird::dynamics::kCommandTokens[4],
              std::string_view{"dynamics.export-curve-data"});

    // 零修订契约四值运行期复核（与 ui View3DSessionPoseContract 同值——
    // 文档出处 ui.md/View3DContract.hpp KIN-06/AT-04）。
    EXPECT_TRUE(sdurws::ird::dynamics::kReplaySessionContract.sessionStateOnly);
    EXPECT_FALSE(sdurws::ird::dynamics::kReplaySessionContract.writesDesignModel);
    EXPECT_FALSE(sdurws::ird::dynamics::kReplaySessionContract.producesRevision);
    EXPECT_FALSE(sdurws::ird::dynamics::kReplaySessionContract.invalidatesResults);

    // CommandOutcome 默认形态＝拒绝（未受理的命令不作任何效果承诺——
    // 全 false 安全默认，acceptance 2"零修订"的防御底座）。
    const sdurws::ird::dynamics::CommandOutcome defaulted;
    EXPECT_FALSE(defaulted.accepted);
    EXPECT_FALSE(defaulted.sessionStateOnly);
    EXPECT_FALSE(defaulted.writesDesignModel);
    EXPECT_FALSE(defaulted.producesRevision);
    EXPECT_FALSE(defaulted.invalidatesResults);
}

/**
 * @brief 命令面零动力学计算红线的源码级静态防线（§10.7 硬边界"处理器
 *        内零动力学计算"——Commands 实现 TU 零评估器/统计器/投影器消费
 *        符号；投影数据面属 Replay TU，Commands 面只做词表查表）。
 *
 * 扫描域＝Commands.hpp＋src/Commands.cpp（命令面的头与实现）；词表＝
 * 本单元全部计算/投影消费符号（剥注释后零命中——结构性保证 T09 装配
 * 后 UI 线程经命令面不触发动力学计算）。
 */
TEST(DynCommandsBoundary, CommandFaceHasZeroComputationSymbols)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});

    const std::vector<fs::path> files{
        unitRoot() / "dynamics" / "include" / "sdurws" / "ird" / "dynamics" / "Commands.hpp",
        unitRoot() / "dynamics" / "src" / "Commands.cpp"};
    for (const fs::path& f : files) {
        ASSERT_TRUE(fs::exists(f)) << "命令面文件缺失：" << f.string();
        const std::string code = stripComments(readFile(f));
        ASSERT_FALSE(code.empty()) << "命令面文件读取为空：" << f.string();
        // 零计算词表（本单元计算/投影面消费符号——命令适配器一律不得
        // 出现；含 include 路径与调用符号两级）：
        static const std::vector<const char*> kForbidden = {
            "InverseDynamicsEvaluator", "EnvelopeCalculator", "SeriesBuilder",
            "ForwardDynamics",          "PowerEnergyCalculator", "EvidenceBuilder",
            "ReplayProjector",          "CurveProjector",       "PeakLocator",
            "computePeaks",             "computeRms",           "evaluate",
            "projectCurves",            "buildReplayData",      "sampleAt",
            "RNEA",                     "rnea"};
        for (const char* symbol : kForbidden) {
            EXPECT_EQ(code.find(symbol), std::string::npos)
                << "命令面出现动力学计算/投影消费符号「" << symbol << "」——"
                << f.filename().string() << "（§10.7 零计算硬边界违约）";
        }
    }
}

/**
 * @brief 受理结果与零修订契约逐位恒等（五命令受理面行为契约——任一
 *        命令的受理四语义位必须逐位等于 kReplaySessionContract；
 *        acceptance 2"联动与回放不产生修订"的机器判读面）。
 */
TEST(DynCommandsBoundary, OutcomeMatchesZeroRevisionContractOnAccept)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{"AT-04"});

    const sdurws::ird::dynamics::DynamicsCommandHandler handler;
    const sdurws::ird::dynamics::CommandPayload payload;
    for (const std::string_view token : sdurws::ird::dynamics::kCommandTokens) {
        const sdurws::ird::dynamics::CommandOutcome out = handler.handle(token, payload);
        ASSERT_TRUE(out.accepted) << token;
        // 逐位恒等（非"恰好相同值"而是"契约值"——语义位由契约常量单点
        // 填充，此处钉住实现与契约不漂移）。
        EXPECT_EQ(out.sessionStateOnly,
                  sdurws::ird::dynamics::kReplaySessionContract.sessionStateOnly)
            << token;
        EXPECT_EQ(out.writesDesignModel,
                  sdurws::ird::dynamics::kReplaySessionContract.writesDesignModel)
            << token;
        EXPECT_EQ(out.producesRevision,
                  sdurws::ird::dynamics::kReplaySessionContract.producesRevision)
            << token;
        EXPECT_EQ(out.invalidatesResults,
                  sdurws::ird::dynamics::kReplaySessionContract.invalidatesResults)
            << token;
        EXPECT_TRUE(out.rejectionToken.empty()) << token;
    }
}
