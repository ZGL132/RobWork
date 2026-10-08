/**
 * @file   PathPlannerContractTest.cpp
 * @brief  规划器适配面的契约测试（TrjPathPlannerContract，WP-16-T06）——
 *         R-5/R-POL-2 产品面零 proximity（静态扫描，两模式皆跑）＋§15.5
 *         接口面钉扎（词表常量与编排入口的在册断言）。
 *
 * 设计依据：
 *   - units/trajectory.md §3.2（R-5：sdurw_proximity 直链禁止——碰撞唯一
 *     经 policy；sdurw_pathplanners 为 DTB §4.6 登记给 trajectory 的 L1
 *     基线库——不在禁词内）、§10.1（"该口径不得扩大到 proximity——R-5
 *     红线不动"）、§15.5（接口基线——非法示例"直接构造 rw::proximity
 *     检测器注入规划器（R-5 违规）"；family 词表"落位时在黄金算例中
 *     锁定"）、§10.2（参数表"逐键登记"）
 *   - 需求 R-POL-2（碰撞实现唯一归 policy——token 面零命中由契约测试
 *     静态扫描钉住；kinematics R-POL-2 扫描先例）、ARC-05（防旁路）
 *   - 先例：kinematics/contract_test/CollisionConsistencyContractTest.cpp
 *     （R-POL-2 静态扫描同款）、本单元 BuildRedLineTest（产品面扫描域与
 *     行首锚定 include 正则）
 *   - 任务契约 tasks/foundation/WP-16-T06.json（acceptance 2——policy
 *     共享评估器④端口消费的静态面；P-TRJ-2 裁决前口径不扩大到 proximity）
 *
 * 本文件为**非条件源**（纯文本扫描，不消费任何 rw 符号——冒烟模式同样
 * 执行；运行期真实链路用例在 test/PlannerAvoidanceTest.cpp——集成模式
 * 专属）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// trajectory 单元树根（industrialrobot 目录——IRD_TRAJECTORY_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_TRAJECTORY_UNIT_ROOT};
    return dir;
}

/// 收集产品面（include/＋src/）全部 C++ 源文件路径（BuildRedLineTest 同款
/// 扫描域——任一目录缺失即 ADD_FAILURE 显性报出）。
std::vector<fs::path> collectProductFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "trajectory" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "trajectory 产品面目录缺失: " << sub;
            continue;
        }
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            if (iec || !it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".h" || ext == ".cpp" || ext == ".ipp") {
                files.push_back(it->path());
            }
        }
    }
    return files;
}

/// 读取文件全文（不可读显性失败）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 线性单遍注释剥离器（BuildRedLineTest 同款状态机——R-5 判定对象是代码
/// 行为，注释内的文档性提及〔如"零 sdurw_proximity"的设计依据引用〕不属
/// 行为，扫描前剥离——与门禁 F-011 修复后口径一致；不用 regex：块注释的
/// 正则形态在 MSVC std::regex 的 ECMAScript 回溯实现上对长注释会爆栈）。
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
            if (ch == '/' && next == '/') {           // 进入行注释
                state = State::LineComment;
                out += "  ";
                ++i;
            } else if (ch == '/' && next == '*') {    // 进入块注释
                state = State::BlockComment;
                out += "  ";
                ++i;
            } else if (ch == '"') {                   // 进入字符串字面量
                state = State::StrLiteral;
                out += ch;
            } else if (ch == '\'') {                  // 进入字符字面量
                state = State::CharLiteral;
                out += ch;
            } else {
                out += ch;
            }
            break;
        case State::LineComment:
            if (ch == '\n') {                         // 行注释止于换行
                state = State::Code;
            }
            out += (ch == '\n') ? '\n' : ' ';
            break;
        case State::BlockComment:
            if (ch == '*' && next == '/') {           // 块注释止于 */
                state = State::Code;
                out += "  ";
                ++i;
            } else {
                out += (ch == '\n') ? '\n' : ' ';
            }
            break;
        case State::StrLiteral:
            if (ch == '\\') {                         // 转义对整体保留跳读
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
 * R-5/R-POL-2 静态扫描：产品面源码零 proximity 触碰——①零
 * "#include <rw/proximity/…>"（行首锚定——注释内的文档性提及不属行为，
 * 不在匹配域）；②零 "sdurw_proximity" 库名词（链接面禁词——CMakeLists
 * 由配置期守卫与 ird_gates 另行核对，此处钉源码/脚本面）。规划算法面
 * 的 rwlibs/pathplanners 消费是 P-TRJ-2 登记口径的合法形态，不在禁词。
 */
TEST(TrjPathPlannerContract, ProductFaceHasZeroProximity_WP16T06_R5)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-05"},
                  std::vector<std::string>{"AT-19"});

    // 行首锚定的 include 正则（注释行以 "//"、" *"、"/\*" 开头——不匹配）。
    static const std::regex kProximityInclude(
        R"re(^[ \t]*#[ \t]*include[ \t]*[<"]rw/proximity/)re");

    const auto files = collectProductFaceFiles();
    ASSERT_FALSE(files.empty()) << "产品面无源文件（扫描域失真）";
    for (const auto& file : files) {
        // 剥注释后扫描（判定对象＝代码行为——文件头注与设计依据引用不触发）。
        const std::string code = stripComments(readFile(file));
        EXPECT_FALSE(std::regex_search(code, kProximityInclude))
            << "产品面包含 rw/proximity 头（R-5/R-POL-2——碰撞唯一经 policy）: "
            << file.string();
        // 库名词（链接面禁词——源码/脚本面扫描；ird_gates LIB 门禁的对偶）。
        EXPECT_EQ(code.find("sdurw_proximity"), std::string::npos)
            << "产品面出现 sdurw_proximity 库名词（R-5）: " << file.string();
    }
}

/**
 * §15.5 接口面钉扎：Planner.hpp 承诺的公共交付面逐项在册——注入接口
 * （IPathPlannerAdapter＋search）、唯一构造入口（makeRobWorkPathPlanner-
 * Adapter）、§10.4 编排入口（planPtpWithObstacleAvoidance）、family 词表
 * 常量（黄金算例锁定值）与三参数键（§10.2 逐键登记）。公共接口的每项
 * 交付至少被一条经接口消费的用例钉扎（接口路径零覆盖教训——WP-20-T03
 * 首轮漏检同源），本用例钉"承诺存在性"，运行期行为归 _test 套件。
 */
TEST(TrjPathPlannerContract, PlannerHeaderPinsDeliveredFace_WP16T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"},
                  std::vector<std::string>{});

    const fs::path header =
        unitRoot() / "trajectory" / "include" / "sdurws" / "ird" / "trajectory"
        / "Planner.hpp";
    ASSERT_TRUE(fs::exists(header)) << "Planner.hpp 缺失（交付面失实）";
    const std::string text = readFile(header);

    // 注入接口与唯一实现入口（§15.5 形态行）。
    EXPECT_NE(text.find("class IPathPlannerAdapter"), std::string::npos)
        << "IPathPlannerAdapter 接口缺失";
    EXPECT_NE(text.find("virtual PlanSearchResult search(const PlannerSearchRequest&"),
              std::string::npos)
        << "search 纯虚签名缺失";
    EXPECT_NE(text.find("makePathPlannerAdapter"), std::string::npos)
        << "唯一构造入口缺失";
    // §10.4 编排入口（acceptance 2 的执行点）。
    EXPECT_NE(text.find("planPtpWithObstacleAvoidance"), std::string::npos)
        << "避障重规划编排入口缺失";
    // family 词表（§10.2——黄金算例锁定值 rrt-connect）。
    EXPECT_NE(text.find("kTrjPlannerFamilyRrtConnect"), std::string::npos)
        << "family 词表常量缺失";
    // 参数键白名单（§10.2 逐键登记）。
    EXPECT_NE(text.find("kTrjPlannerParamExtend"), std::string::npos)
        << "参数键 extend 缺失";
    EXPECT_NE(text.find("kTrjPlannerParamEdgeResolution"), std::string::npos)
        << "参数键 edge-resolution 缺失";
    EXPECT_NE(text.find("kTrjPlannerParamCandidateAttempts"), std::string::npos)
        << "参数键 candidate-attempts 缺失";
    // 候选复核三态（C8 语义的域内词表——枚举成员按声明序在册；顺序锚
    // 防误配其他同名标识）。
    const std::size_t passedAt = text.find("CandidateReviewStatus");
    const std::size_t collisionAt = text.find("Passed,", passedAt);
    const std::size_t insufficientAt = text.find("Collision,", collisionAt);
    ASSERT_NE(passedAt, std::string::npos) << "复核三态词表缺失";
    EXPECT_NE(collisionAt, std::string::npos) << "复核通过态缺失";
    EXPECT_NE(insufficientAt, std::string::npos) << "候选碰撞淘汰态缺失（C8）";
    EXPECT_NE(text.find("DataInsufficient,", insufficientAt), std::string::npos)
        << "不采信态缺失（KIN-05）";
}
