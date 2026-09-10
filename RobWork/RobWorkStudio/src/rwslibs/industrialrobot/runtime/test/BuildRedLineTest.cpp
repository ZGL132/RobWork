/**
 * @file   BuildRedLineTest.cpp
 * @brief  runtime 构建红线用例组（RT-T01）——依赖边界自检＋L1 链接清单钉住。
 *
 * 设计依据：
 *   - units/runtime.md §3.2/§3.4（依赖：core＋L1 基线库；零 Qt/零单元边）、
 *     §12 RT-T01 行（验收：零 Qt/零单元边扫描＋基线库链接清单留痕——P-RT-3 消账输入）
 *   - development-task-breakdown.md §4.6（L1 目标名实测表）、§4.5（P-RT-3 登记行）
 *   - core/testkit 的 BuildRedLineTest 同构先例（CORE-T01/TK-T01）
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

/// runtime 单元根（IRD_RUNTIME_UNIT_ROOT 由 CMake 注入——core/testkit 同款）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_RUNTIME_UNIT_ROOT};
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

/// 全文读取；读失败显性失败（不静默跳过——违规漏检防线）。
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

/** 锚定：源码树可达且扫描集非空（防路径配错导致空集恒真——验收协议 4.5）。 */
TEST(RuntimeBuild, SourceTreeReachable_RT_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_RUNTIME_UNIT_ROOT 不存在";
    // runtime 落位期无公共头（仅 README 保留位）——锚定保留位文件而非扫描集非空。
    ASSERT_TRUE(std::filesystem::exists(unitRoot() / "runtime" / "include" / "sdurws" / "ird" / "runtime" / "README.md")) << "公共头保留位缺失（README.md 应存在）";
    ASSERT_FALSE(collectCppFiles(unitRoot() / "runtime" / "src").empty()) << "src 扫描为空";
}

/** 零 Qt：runtime 产品面（include/＋src/）零 Q 头（R-3/NFR-MNT-01）。 */
TEST(RuntimeBuild, NoQtInclude_RT_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "runtime" / sub)) {
            const auto text = readFile(unitRoot() / "runtime" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "runtime 禁含 Qt 头: " << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零单元边（R-1/R-2）：include 面的 sdurws/ird/* 只允许 core 与 runtime 自身。 */
TEST(RuntimeBuild, NoCrossUnitInclude_RT_BUILD_R1_R2)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "runtime" / sub)) {
            const auto text = readFile(unitRoot() / "runtime" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = line.substr(unitBegin, unitEnd - unitBegin);
                if (unit != "core" && unit != "runtime") {
                    ADD_FAILURE() << "runtime 仅可依赖 core（ARCH §3.5），发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/**
 * L1 基线库链接清单钉住（P-RT-3 消账输入——acceptance 1 第三分句）。
 * CMake 按目标存在性组装清单注入 IRD_RUNTIME_BASELINE_LIBS：
 *   集成模式＝"sdurw_kinematics;sdurw_models;sdurwsim"（目标实测在位）；
 *   冒烟模式＝空串（无框架目标，占位实现不引用 rw 头）。
 * 分支均为强断言（防恒真）：集成要求三库齐；冒烟要求为空。
 */
TEST(RuntimeBuild, BaselineLibsPinned_RT_BUILD_P_RT_3)
{
#if __has_include(<rw/math/Vector3D.hpp>)
    // 集成模式：rw 头可达 ⇒ 三基线库必须在链接清单（分号分隔）。
    const std::string libs = IRD_RUNTIME_BASELINE_LIBS;
    EXPECT_NE(libs.find("sdurw_kinematics"), std::string::npos) << "清单: " << libs;
    EXPECT_NE(libs.find("sdurw_models"), std::string::npos) << "清单: " << libs;
    EXPECT_NE(libs.find("sdurwsim"), std::string::npos) << "清单: " << libs;
    // 库面零 gtest（本体的 gtest 仅在测试目标——D-06 同款纪律）。
    EXPECT_EQ(libs.find("gtest"), std::string::npos) << "清单: " << libs;
#else
    // 冒烟模式：无框架目标 ⇒ 清单必为空（占位实现零 rw 引用与之自洽）。
    EXPECT_STREQ(IRD_RUNTIME_BASELINE_LIBS, "");
#endif
}
