/**
 * @file   BuildRedLineTest.cpp
 * @brief  evidence 构建红线用例组（EV-T01）——零 Qt/零单元边/头路径布局自检。
 *
 * 设计依据：
 *   - units/evidence.md §3.2（编译依赖仅 core＋std）、§3.4（头包含形式）、
 *     §12 EV-T01 行（验收：两模式零错误＋红线扫描零命中）、§8（EV-BUILD 形态，
 *     与 core/testkit/runtime 的 BuildRedLine 同构）
 *   - 需求 CON-01/05、EVI-01；任务契约 tasks/foundation/EV-T01.json acceptance 1
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

/// evidence 单元根（industrialrobot 目录——IRD_EVIDENCE_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_EVIDENCE_UNIT_ROOT};
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
TEST(EvidenceBuild, SourceTreeReachable_EV_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_EVIDENCE_UNIT_ROOT 不存在";
    ASSERT_TRUE(fs::exists(unitRoot() / "evidence" / "include" / "sdurws" / "ird"
                           / "evidence" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    ASSERT_FALSE(collectCppFiles(unitRoot() / "evidence" / "src").empty())
        << "src 扫描为空（Evidence.cpp 应存在）";
}

/** 零 Qt：产品面（include/＋src/）零 Q 头（R-3/NFR-MNT-01）。 */
TEST(EvidenceBuild, NoQtInclude_EV_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "evidence" / sub)) {
            const auto text = readFile(unitRoot() / "evidence" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "evidence 禁含 Qt 头: " << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零单元边（R-1/R-2）：include 面的 sdurws/ird/* 白名单＝{core, evidence}。 */
TEST(EvidenceBuild, NoCrossUnitInclude_EV_BUILD_R1_R2)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "evidence" / sub)) {
            const auto text = readFile(unitRoot() / "evidence" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = line.substr(unitBegin, unitEnd - unitBegin);
                if (unit != "core" && unit != "evidence") {
                    ADD_FAILURE() << "evidence 仅可依赖 core（ARCH §3.5），发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/** 公共头路径布局（R-2）：公共头仅位于 include/sdurws/ird/evidence/（随头落地收紧）。 */
TEST(EvidenceBuild, PublicHeaderPathLayout_EV_BUILD_R2)
{
    const auto incDir = unitRoot() / "evidence" / "include";
    for (const auto& rel : collectCppFiles(incDir)) {
        const auto generic = rel.generic_string();
        EXPECT_EQ(generic.substr(0, std::string("sdurws/ird/evidence/").size()),
                  "sdurws/ird/evidence/")
            << "公共头越出命名空间根: " << generic;
    }
    SUCCEED() << "公共头路径布局合规（当前保留位期零头，规则随头落地自动生效）";
}
