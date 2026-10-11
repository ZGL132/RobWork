/**
 * @file   CommandsBoundaryContractTest.cpp
 * @brief  WP-16-T10 契约测试——领域命令面（§14.5.1/§15.11/§16.2）单元边界
 *         契约：七命令 token 词表与零修订会话契约的编译期静态钉扎（§14.5.1
 *         表七 token 原文＋KIN-06/AT-04 四常量同值对账锚）＋handle 运行时
 *         受理语义（受理/拒绝两轨）＋T10 三 TU（Commands/Playback/Export）
 *         零修订结构红线的源码级扫描（代码面零修订/零命令权威/零会话驱动
 *         符号——§15.11"零计算逻辑"与 §16.2"机器可断言"）＋导出格式契约
 *         钉扎（魔数行/joint_type 词表——§15.13 字段字典冻结面）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1（七命令表——token 字面＝跨版本命令契约；
 *     修订列全部"无"）、§15.11（处理器薄适配——"零计算逻辑（计算在评估
 *     器）"；无修订命令集〔AT-04〕）、§16.2（零修订四常量机器可断言——
 *     "View3DSessionPoseContract 四常量同款……适用于轨迹播放会话态"）
 *   - 需求 TRJ-07、AT-04（动画/预览/导出不产生项目修订）、KIN-06（动画播
 *     放只改变会话姿态）
 *   - 先例：dynamics contract_test/CommandsBoundaryContractTest.cpp
 *     （WP-17-T08——词表 static_assert 钉扎＋四常量钉扎＋源码扫描形态；
 *     stripComments/文件读取设施同源拷贝——契约测试间不共享代码，同仓
 *     各契约测试先例口径）＋本单元 RecheckContractTest.cpp（unitRoot/
 *     readFile/stripComments 同款）
 *   - 对账口径（诚实登记）：ui 单元 View3DSessionPoseContract 四常量的 ui
 *     侧钉扎在 sdurws_ird_ui_test（View3DContract 模型用例）；trajectory
 *     依赖白名单（卡 §4.2 七登记边）不含 ui——R-1/R-2 红线禁止本单元
 *     include ui 公共头，故本测试以**同一文档出处**（ui.md/View3DContract.
 *     hpp 的 KIN-06/AT-04 钉住值）字面冻结 trajectory 侧
 *     kTrajectorySessionContract 四值：两侧常量各自被测试钉住、值漂移各自
 *     失败，WP-16-T12 装配层在同一编译单元消费两侧时即自然对账。
 *
 * 双模式皆跑（非条件源）：被测面＝Commands.hpp/.cpp（纯 std）＋T10 头与
 * TU 的源码文本扫描＋Export.hpp 头内常量——零 rw 符号消费，冒烟/集成两模
 * 式皆可编译链接执行。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/trajectory/Commands.hpp>
#include <sdurws/ird/trajectory/Export.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

// =====================================================================
// 编译期静态钉扎（词表字面＋零修订契约四值＋导出格式魔数——改动即本 TU
// 编译失败，语义变更必须走单元卡增量修订，代码不得私改）。
// =====================================================================

namespace sdurws::ird::trajectory {
namespace {

// §14.5.1 表七行 token 字面冻结（跨版本命令契约——ui CommandRegistry 注
// 册面与历史会话的锚；漂移即断链）。
static_assert(kCmdPlanSequence == "trajectory.plan-sequence", "§14.5.1 行 1 token 冻结");
static_assert(kCmdPreviewSegment == "trajectory.preview-segment", "§14.5.1 行 2 token 冻结");
static_assert(kCmdLocalReplan == "trajectory.local-replan", "§14.5.1 行 3 token 冻结");
static_assert(kCmdPlay == "trajectory.play", "§14.5.1 行 4 token 冻结");
static_assert(kCmdPlaybackSeek == "trajectory.playback-seek", "§14.5.1 行 4 token 冻结");
static_assert(kCmdExport == "trajectory.export", "§14.5.1 行 5 token 冻结");
static_assert(kCmdShowCurve == "trajectory.show-curve", "§14.5.1 行 6 token 冻结");
static_assert(kCommandTokens.size() == 7u, "词表封闭——七命令全集");

// 零修订会话契约四值冻结（KIN-06/AT-04 钉住值——与 ui 单元
// View3DSessionPoseContract 同值对账，文件头"对账口径"；任一位翻转即编
// 译失败＝必须走需求修订）。
static_assert(kTrajectorySessionContract.sessionStateOnly, "sessionStateOnly 恒 true——KIN-06");
static_assert(!kTrajectorySessionContract.writesDesignModel, "writesDesignModel 恒 false——ARC-04");
static_assert(!kTrajectorySessionContract.producesRevision, "producesRevision 恒 false——AT-04");
static_assert(!kTrajectorySessionContract.invalidatesResults, "invalidatesResults 恒 false——CON-02");

// §15.13 导出格式魔数行冻结（格式版本契约——读方按此前缀识别；演进须单
// 元卡增量修订＋版本行递进）。
static_assert(std::string_view(kTrajectoryExportFormatMagic) == "#ird-trajectory-export v1",
              "§15.13 魔数行冻结");

}  // namespace
}  // namespace sdurws::ird::trajectory

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

/// 线性单遍注释剥离器（RecheckContractTest 同款状态机——判定对象是代码
/// 行为，注释内的文档性提及不属行为；不用 regex——MSVC 回溯爆栈先例）。
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

// =====================================================================
// handle 运行时受理语义（受理轨＝四语义位＝零修订契约；拒绝轨＝显式
// rejectionToken——两轨确定性，NFR-COR-02）。
// =====================================================================

/**
 * 用例：七命令逐一受理——accepted=true 且四语义位逐位等于
 * kTrajectorySessionContract（producesRevision 恒 false＝AT-04 的机器断
 * 言面）；负载交叉校验位留空（合法缺省不校验）。
 */
TEST(TrajectoryCommandsBoundary, AcceptAllSevenTokensWithZeroRevisionContract)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TrajectoryCommandHandler handler;
    for (const std::string_view token : trj::kCommandTokens) {
        const trj::TrajectoryCommandPayload payload;  // 交叉校验位空＝不校验
        const trj::TrajectoryCommandOutcome outcome =
            handler.handle(token, payload);
        EXPECT_TRUE(outcome.accepted) << "token: " << token;
        EXPECT_EQ(outcome.sessionStateOnly,
                  trj::kTrajectorySessionContract.sessionStateOnly)
            << "token: " << token;
        EXPECT_EQ(outcome.writesDesignModel,
                  trj::kTrajectorySessionContract.writesDesignModel)
            << "token: " << token;
        // AT-04 零修订断言（核心位——七命令任一受理结果不得产生修订）。
        EXPECT_FALSE(outcome.producesRevision) << "token: " << token;
        EXPECT_EQ(outcome.producesRevision,
                  trj::kTrajectorySessionContract.producesRevision)
            << "token: " << token;
        EXPECT_EQ(outcome.invalidatesResults,
                  trj::kTrajectorySessionContract.invalidatesResults)
            << "token: " << token;
        EXPECT_TRUE(outcome.rejectionToken.empty()) << "token: " << token;
    }
}

/**
 * 用例：词表外 token 拒绝（大小写敏感——命令 token 是机器契约非自由文
 * 本）；拒绝结果四语义位全 false＋rejectionToken="unknown-token"。
 */
TEST(TrajectoryCommandsBoundary, RejectUnknownTokenCaseSensitive)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TrajectoryCommandHandler handler;
    const trj::TrajectoryCommandPayload payload;

    // 表外 token（未收录命令）。
    const trj::TrajectoryCommandOutcome unknown =
        handler.handle("trajectory.replan-all", payload);
    EXPECT_FALSE(unknown.accepted);
    EXPECT_EQ(unknown.rejectionToken, "unknown-token");
    EXPECT_FALSE(unknown.producesRevision);

    // 大小写变体（同词形不同大小写＝不同 token——精确匹配纪律）。
    const trj::TrajectoryCommandOutcome upperCase =
        handler.handle("Trajectory.Export", payload);
    EXPECT_FALSE(upperCase.accepted);
    EXPECT_EQ(upperCase.rejectionToken, "unknown-token");

    // 前缀/空白变体（无归一化容错——NFR-COR-03）。
    const trj::TrajectoryCommandOutcome trailing =
        handler.handle("trajectory.export ", payload);
    EXPECT_FALSE(trailing.accepted);
}

/**
 * 用例：负载交叉校验位与命令不一致拒绝（"payload-token-mismatch"——防
 * UI 侧组装错位）；一致时受理不受影响。
 */
TEST(TrajectoryCommandsBoundary, RejectPayloadTokenMismatch)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const trj::TrajectoryCommandHandler handler;

    trj::TrajectoryCommandPayload mismatch;
    mismatch.commandToken = "trajectory.play";  // 与命令错配
    const trj::TrajectoryCommandOutcome rejected =
        handler.handle("trajectory.export", mismatch);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(rejected.rejectionToken, "payload-token-mismatch");
    EXPECT_FALSE(rejected.producesRevision);

    trj::TrajectoryCommandPayload consistent;
    consistent.commandToken = "trajectory.export";  // 与命令一致
    const trj::TrajectoryCommandOutcome accepted =
        handler.handle("trajectory.export", consistent);
    EXPECT_TRUE(accepted.accepted);
    EXPECT_FALSE(accepted.producesRevision);
}

// =====================================================================
// T10 三 TU 零修订结构红线（§15.11"零计算逻辑"/§16.2"机器可断言"的源
// 码级防线——代码面零修订/零命令权威/零会话驱动符号；注释内文档性提及
// 经 stripComments 剥离不误报）。
// =====================================================================

/**
 * 用例：Commands/Playback/Export 五文件剥注释后零修订/零命令权威/零会话
 * 驱动符号——trajectory 依赖白名单无 ui/project 边，命令/投影/导出域内
 * 面出现任何修订推进/命令注册/会话驱动设施即越界（适配点归 WP-16-T12
 * 装配层，越界前由本扫描拦截）。
 */
TEST(TrajectoryCommandsBoundary, T10FacesZeroRevisionAndNoCommandAuthoritySymbols)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    const std::vector<fs::path> t10Faces{
        unitRoot() / "trajectory/include/sdurws/ird/trajectory/Commands.hpp",
        unitRoot() / "trajectory/src/Commands.cpp",
        unitRoot() / "trajectory/include/sdurws/ird/trajectory/Playback.hpp",
        unitRoot() / "trajectory/src/Playback.cpp",
        unitRoot() / "trajectory/include/sdurws/ird/trajectory/Export.hpp",
        unitRoot() / "trajectory/src/Export.cpp",
    };
    // 禁词表（代码面——英文标识符；中文修订/命令语义的文档性提及在注释
    // 内，剥离后不命中）：
    //   - 共同禁词（命令权威/注册面/会话驱动/project 写路径——适配点归
    //     WP-16-T12 装配层，越界前由本扫描拦截）；
    //   - "Revision" 仅对 Playback/Export 禁（修订推进类型/字段族——零命
    //     中＝零修订结构性证据）；Commands.hpp/.cpp **不**禁本词：其代码
    //     内的 producesRevision 字段恰是零修订契约自身的承载（§16.2 四常
    //     量），禁词会误报合法契约面。
    const std::vector<std::string> commonForbidden{
        "ICommandRegistry", "CommandRegistry", "registerCommand",
        "IView3D", "SessionPose", "IProjectCommand", "ProjectCommandService",
        "IResultArchivePort"};
    for (const fs::path& file : t10Faces) {
        const std::string name = file.filename().string();
        const bool commandsFace = name.find("Commands") != std::string::npos;
        std::vector<std::string> forbidden = commonForbidden;
        if (!commandsFace) {
            forbidden.push_back("Revision");
        }
        const std::string code = stripComments(readFile(file));
        for (const std::string& token : forbidden) {
            EXPECT_EQ(code.find(token), std::string::npos)
                << "越界符号 " << token << " 于 " << name
                << "（T10 域内面零修订/零命令权威/零会话驱动——§15.11/§16.2）";
        }
    }
}

// =====================================================================
// 导出格式契约钉扎（§15.13 字段字典冻结面——魔数行/joint_type 词表/表头
// 列名唯一书写点）。
// =====================================================================

/**
 * 用例：joint_type 列词表两值落盘 token（"revolute"/"prismatic"）＋R1 字
 * 段表列名冻结（6＋2 列序——Export.cpp 列名常量唯一书写点的读回一致）。
 */
TEST(TrajectoryCommandsBoundary, ExportFieldDictionaryFrozen)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-07"},
                  std::vector<std::string>{"AT-04"});

    // joint_type 词表（§15.13 数据列——小写 token，改动即格式演进）。
    EXPECT_EQ(trj::exportJointTypeToken(trj::TrajectoryExportJointType::Revolute),
              "revolute");
    EXPECT_EQ(trj::exportJointTypeToken(trj::TrajectoryExportJointType::Prismatic),
              "prismatic");

    // 表头列名（Export.cpp kCol* 常量的源码级冻结——6 基本列＋2 段映射列
    // 的列名与次序＝字段字典；漂移即外部消费契约破坏）。
    const std::string exportImpl = stripComments(
        readFile(unitRoot()
                 / "trajectory/src/Export.cpp"));
    for (const char* column :
         {"\"t_s\"", "\"joint_index\"", "\"q\"", "\"qd\"", "\"qdd\"",
          "\"joint_type\"", "\"segment_index\"", "\"source_task_point\""}) {
        EXPECT_NE(exportImpl.find(column), std::string::npos)
            << "字段字典列名缺席: " << column << "（§15.13 R1 字段表）";
    }
    // 魔数行消费（Export.cpp 必须消费头内常量而非内联第二字面量——唯一
    // 书写点纪律）。
    EXPECT_NE(exportImpl.find("kTrajectoryExportFormatMagic"), std::string::npos)
        << "魔数行必须经头内常量消费（禁第二处字面量）";
}
