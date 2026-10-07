/**
 * @file   RneaBoundaryContractTest.cpp
 * @brief  WP-17-T03 契约测试——RNEA 评估器面的单元边界契约：DYN-04
 *         "关节侧结果与候选传动无关"的源码级静态防线（产品面零传动映射
 *         消费符号）＋产品面 include 白名单（R-1/R-2——零业务域互链、只
 *         消费登记边公共头）。
 *
 * 设计依据：
 *   - units/dynamics.md §8.2（drivetrain 交接——D-DYN-5"dynamics 不内嵌
 *     映射调用"、§8.2.3 不内嵌映射调用的理由）、§5.6（关节侧结果与候选
 *     传动无关）、§11.4（替身边界——V-28/V-31 的静态扫描精神）、§3.1
 *     （R-1 业务域互链禁止——trajectory/requirements/drivetrain/selection
 *     零编译期依赖；R-2 只消费他单元公共头）
 *   - 需求 DYN-04（输出与候选传动无关；映射唯一实现归 drivetrain——
 *     WP-18-T03）、AT-38 关联边界
 *   - 先例：dynamics/test/BuildRedLineTest.cpp（产品面运行期扫描形态——
 *     剥注释、逐文件遍历）；本测试为同款形态的契约侧（对端面）补强，
 *     词表按 DYN-04 语义选取（任务契约 WP-17-T03 acceptance 3）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// dynamics 单元树根（industrialrobot 目录——IRD_DYNAMICS_UNIT_ROOT 注入，
/// 与 BuildGraphContractTest 同源）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DYNAMICS_UNIT_ROOT};
    return dir;
}

/// 收集产品面源码文件（include/**＋src/**——ird_gates 第 4 步"产品面"
/// 同口径；test/contract_test/plugin/assembly 为非产品面不在扫描域）。
std::vector<fs::path> collectProductSources()
{
    std::vector<fs::path> files;
    const std::vector<fs::path> roots = {unitRoot() / "dynamics" / "include",
                                         unitRoot() / "dynamics" / "src"};
    for (const fs::path& root : roots) {
        // 目录缺失＝扫描失效面——ADD_FAILURE 后跳过（辅助函数非 void，
        // 不用 ASSERT 宏〔gtest 断言宏要求 void 返回函数〕；空清单由调用
        // 方的 ASSERT_FALSE(files.empty()) 兜底显性失败）。
        if (!fs::exists(root)) {
            ADD_FAILURE() << "产品面目录不存在：" << root.string();
            continue;
        }
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());  // 确定性遍历序（NFR-COR-02）
    return files;
}

/// 剥离 // 行注释与 /* */ 块注释（词表扫描只针对**代码**——设计注释里
/// 提及"drivetrain 交接"等词不是消费符号；与 BuildGraphContractTest 的
/// "#" 剥离同理由）。字符串字面量按注释近似处理（本单元产品面代码不在
/// 字符串中书写词表词——若误伤会在白名单用例以同等机制暴露）。
std::string stripComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    enum class State { Code, LineComment, BlockComment } state = State::Code;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';
        switch (state) {
            case State::Code:
                if (c == '/' && next == '/') {
                    state = State::LineComment;
                    ++i;
                } else if (c == '/' && next == '*') {
                    state = State::BlockComment;
                    ++i;
                } else {
                    out.push_back(c);
                }
                break;
            case State::LineComment:
                if (c == '\n') {
                    out.push_back('\n');
                    state = State::Code;
                }
                break;
            case State::BlockComment:
                if (c == '*' && next == '/') {
                    ++i;
                    state = State::Code;
                }
                break;
        }
    }
    return out;
}

}  // namespace

// =====================================================================
// 用例 1：DYN-04 关节侧边界——产品面零传动映射消费符号（V-28/V-31 静态
// 扫描精神：关节侧序列零电机侧字段、零映射调用、零传动块读取）。
// =====================================================================

TEST(DynRneaBoundary, ProductFaceZeroDrivetrainMappingSymbols)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"}, std::vector<std::string>{});
    // 本用例验证：dynamics 产品面（include/**＋src/**）剥注释后的代码中
    // 不出现任何"传动映射/电机侧"消费符号——映射唯一实现归 drivetrain
    // （WP-18-T03 的 DriveTrainMappingEvaluator）；dynamics 只交付
    // JointSideSeriesPack 数据契约（随 T04/交接任务落位，届时该类型同样
    // 只携带关节侧字段——本词表继续约束它）。
    //
    // 词表来源（§8.2.2 逐条禁止项的符号化；任一命中即 DYN-04 红线）：
    //   - 传动块读取：ratioPerJoint / coupling（CanonicalDrivetrain 字段——
    //     RNEA 不读传动块）；
    //   - 映射调用：DriveTrainMapping（唯一映射评估器名）；
    //   - 电机侧字段：motorSide / tauMotor / motorTorque / reflectedInertia
    //     （反射惯量）/ quadrant（四象限工作制——全部归 drivetrain 侧）。
    const char* forbidden[] = {
        "ratioPerJoint", "coupling", "DriveTrainMapping", "motorSide",
        "tauMotor", "motorTorque", "reflectedInertia", "quadrant",
    };

    const std::vector<fs::path> files = collectProductSources();
    ASSERT_FALSE(files.empty()) << "产品面为空——扫描失去对象";

    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(in)) << "无法读取产品面文件：" << file.string();
        const std::string code = stripComments(
            std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
        for (const char* word : forbidden) {
            EXPECT_EQ(code.find(word), std::string::npos)
                << "DYN-04 红线：产品面代码出现传动映射消费符号 '" << word << "'（"
                << file.string() << "）——映射归 drivetrain，dynamics 零消费";
        }
    }
}

// =====================================================================
// 用例 2：产品面 include 白名单（R-1/R-2——ird 命名空间头只允许五条
// 登记边公共头＋本单元头；trajectory/requirements/drivetrain/selection
// 等业务域与表外单元零编译期依赖）。
// =====================================================================

TEST(DynRneaBoundary, ProductFaceIncludeAllowlist)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "DYN-01"}, std::vector<std::string>{});
    // 本用例验证：产品面 #include 的 sdurws/ird/ 头，其单元段全部落在
    // {core, evidence, runtime, execution, diagnostics, dynamics}——五条
    // CMake 行登记边（core/evidence/runtime/execution/diagnostics）＋本
    // 单元自身。trajectory（上游激励经注入端口数据面）、drivetrain（映射
    // 归对端）等业务域单元零出现（R-1）；std/rw/Qt 等非 ird 头不在本
    // 用例域（零 Qt 由 BuildRedLineTest 承载）。
    const std::vector<std::string> allowedUnits = {
        "core", "evidence", "runtime", "execution", "diagnostics", "dynamics",
    };

    const std::vector<fs::path> files = collectProductSources();
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(in)) << "无法读取产品面文件：" << file.string();
        const std::string code = stripComments(
            std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});

        for (std::size_t pos = code.find("#include");
             pos != std::string::npos;
             pos = code.find("#include", pos + 1)) {
            const std::size_t lineEnd = code.find('\n', pos);
            const std::string line =
                code.substr(pos, (lineEnd == std::string::npos) ? std::string::npos
                                                                : lineEnd - pos);
            const std::size_t irdPos = line.find("sdurws/ird/");
            if (irdPos == std::string::npos) { continue; }  // 非 ird 头——不在本域
            const std::size_t unitBegin = irdPos + std::string("sdurws/ird/").size();
            const std::size_t unitEnd = line.find('/', unitBegin);
            ASSERT_NE(unitEnd, std::string::npos)
                << "include 路径缺单元段：" << line << "（" << file.string() << "）";
            const std::string unit = line.substr(unitBegin, unitEnd - unitBegin);
            EXPECT_TRUE(std::find(allowedUnits.begin(), allowedUnits.end(), unit)
                        != allowedUnits.end())
                << "R-1/R-2 红线：产品面 include 表外单元头 '" << line << "'（"
                << file.string() << "）——只允许五条登记边＋本单元公共头";
        }
    }
}
