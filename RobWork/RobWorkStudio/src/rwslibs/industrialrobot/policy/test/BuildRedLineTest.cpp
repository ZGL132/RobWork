/**
 * @file   BuildRedLineTest.cpp
 * @brief  policy 构建红线用例组（POL-T01）——零 Qt/依赖图 core＋RobWork 边/
 *         R-5 例外钉住/头路径布局自检。
 *
 * 设计依据：
 *   - units/policy.md §3.2（依赖：core＋RobWork 基线，含 proximity 直链）、
 *     §3.4（头包含形式）、§12 POL-T01 行
 *   - DTB §4.5 R-5 例外登记册生效行（policy 产品实现＝proximity 直链唯一许可方）、
 *     §4.6 L1 目标名实测表
 *   - 需求 ARC-05/CON-06；任务契约 tasks/foundation/POL-T01.json acceptance 1
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// policy 单元根（industrialrobot 目录——IRD_POLICY_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_POLICY_UNIT_ROOT};
    return dir;
}

/// 递归收集 C++ 源/头文件（相对路径、已排序——确定性失败信息）。
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir)) { return files; }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 全文读取；读失败显性失败。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

/** 锚定：源码树可达（include 保留位 README＋src 占位 TU——落位期形态）。 */
TEST(PolicyBuild, SourceTreeReachable_POL_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_POLICY_UNIT_ROOT 不存在";
    ASSERT_TRUE(fs::exists(unitRoot() / "policy" / "include" / "sdurws" / "ird"
                           / "policy" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    ASSERT_FALSE(collectCppFiles(unitRoot() / "policy" / "src").empty())
        << "src 扫描为空（Policy.cpp 应存在）";
}

/** 零 Qt：产品面（include/＋src/）零 Q 头（R-3/NFR-MNT-01）。 */
TEST(PolicyBuild, NoQtInclude_POL_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "policy" / sub)) {
            const auto text = readFile(unitRoot() / "policy" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "policy 禁含 Qt 头: " << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零单元边（R-1/R-2）：include 面的 sdurws/ird/* 白名单＝{core, policy}。 */
TEST(PolicyBuild, NoCrossUnitInclude_POL_BUILD_R1_R2)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "policy" / sub)) {
            const auto text = readFile(unitRoot() / "policy" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = line.substr(unitBegin, unitEnd - unitBegin);
                if (unit != "core" && unit != "policy") {
                    ADD_FAILURE() << "policy 仅可依赖 core（ARCH §3.5），发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/**
 * L1 基线库链接清单钉住（acceptance 1"依赖图仅 core＋RobWork 边"的运行期面＋
 * R-5 例外钉住）：集成模式断言清单含 math/kinematics/models/proximity 四库
 * （proximity＝R-5 唯一直链许可——DTB §4.5 生效行）；冒烟模式断言清单为空。
 */
TEST(PolicyBuild, BaselineLibsPinned_POL_BUILD_R5)
{
#if __has_include(<rw/math/Vector3D.hpp>)
    const std::string libs = IRD_POLICY_BASELINE_LIBS;
    EXPECT_NE(libs.find("sdurw_math"), std::string::npos) << "清单: " << libs;
    EXPECT_NE(libs.find("sdurw_kinematics"), std::string::npos) << "清单: " << libs;
    EXPECT_NE(libs.find("sdurw_models"), std::string::npos) << "清单: " << libs;
    EXPECT_NE(libs.find("sdurw_proximity"), std::string::npos)
        << "R-5 例外：proximity 直链必须在清单（DTB §4.5）: " << libs;
    EXPECT_EQ(libs.find("gtest"), std::string::npos) << "清单: " << libs;
#else
    EXPECT_STREQ(IRD_POLICY_BASELINE_LIBS, "");
#endif
}
