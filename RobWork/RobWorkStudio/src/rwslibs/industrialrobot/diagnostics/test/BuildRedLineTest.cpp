/**
 * @file   BuildRedLineTest.cpp
 * @brief  diagnostics 构建红线用例组（DT-BUILD）——零 Qt/仅 core 单元边/
 *         头路径布局（无私有头出 include）自检。
 *
 * 设计依据：
 *   - units/diagnostics.md §3.2（编译依赖仅 core＋std：零 Qt 含 Core、零
 *     Eigen/rw::math 直接包含、零其他单元）、§3.3（头包含形式与 R-2 纪律）、
 *     §10 DT-BUILD 行（脚本扫描＋CMake 依赖图：`#include <Q` 零命中；依赖
 *     仅 core 边；无私有头出 include）；
 *   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界可维护）、
 *     ARCH §3.5（diagnostics→core 唯一既有边）；
 *   - 任务契约 tasks/foundation/DIAG-T02.json acceptance 1/2；
 *   - 同构先例：core/testkit/runtime/evidence/policy 各自的 BuildRedLine
 *     用例组（形态一致，扫描面按单元卡红线裁剪）。
 *
 * 运行期源码扫描说明：扫描对象＝源码树（经 IRD_DIAGNOSTICS_UNIT_ROOT 注入
 * 的 industrialrobot 根下的 diagnostics/），因此本用例对工作目录零依赖、
 * 在集成与独立冒烟两种模式下行为一致。CMake 配置期的链接面守卫（单元边
 * /零 Qt/零 gtest）在 diagnostics/CMakeLists.txt 文件末尾，与本用例互为
 * 两道防线（源码 include 面＋目标链接面）。
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

/// diagnostics 单元树根（industrialrobot 目录——IRD_DIAGNOSTICS_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DIAGNOSTICS_UNIT_ROOT};
    return dir;
}

/// 递归收集 C++ 源/头文件（相对路径、已排序——失败信息确定性，NFR-COR-02 精神）。
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

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
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

/** 锚定：源码树可达（公共头保留位 README＋src/ 锚点翻译单元——落位期形态）。 */
TEST(DiagnosticsBuild, SourceTreeReachable_DT_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_DIAGNOSTICS_UNIT_ROOT 不存在";
    // 公共头保留位：单元卡 §3.1 组成表约定 10 个公共头随 DIAG-T03+ 落地，
    // 当前仅有 README.md（指向 §11）——保留位存在性是头路径布局检查的前提。
    ASSERT_TRUE(fs::exists(unitRoot() / "diagnostics" / "include" / "sdurws" / "ird"
                           / "diagnostics" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    // src/ 空起步：DIAG-T02 产物定义要求锚点翻译单元存在（STATIC 库可生成的
    // 最小条件），实现文件随 DIAG-T03+ 填充——扫描非空即锚点在位。
    ASSERT_FALSE(collectCppFiles(unitRoot() / "diagnostics" / "src").empty())
        << "src 扫描为空（Diagnostics.cpp 锚点翻译单元应存在）";
}

/** 零 Qt（含 Core——单元卡 D-01 比 L3 上限更严）：产品面零 Q 头（NFR-MNT-01）。 */
TEST(DiagnosticsBuild, NoQtInclude_DT_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "diagnostics" / sub)) {
            const auto text = readFile(unitRoot() / "diagnostics" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                // Qt 头只有 <QXxx>/"QXxx" 两种包含形态；`#include <Q` 与
                // `#include "Q` 前缀即可全盖（QtCore/QObject 同以前缀命中）。
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "diagnostics 禁含 Qt 头（含 Core，D-01）: "
                                  << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零 Eigen/rw::math 直接包含（§1.4：本单元契约不含几何数值类型）。 */
TEST(DiagnosticsBuild, NoEigenOrRwMathDirectInclude_DT_BUILD)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "diagnostics" / sub)) {
            const auto text = readFile(unitRoot() / "diagnostics" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                // rw::math/Eigen 只允许经 core 公共头"传递"到达（§3.2 依赖图）；
                // 本单元源码直接 include（#include <rw/...>、"rw/..."、<Eigen...、
                // "Eigen/..." 四种形态）即绕过 core 边＝红线命中。
                if (line.find("#include <rw") != std::string::npos
                    || line.find("#include \"rw") != std::string::npos
                    || line.find("#include <Eigen") != std::string::npos
                    || line.find("#include \"Eigen") != std::string::npos) {
                    ADD_FAILURE() << "diagnostics 禁直接包含 Eigen/rw::math: "
                                  << rel.string() << ":" << lineno << " → " << line;
                }
            }
        }
    }
}

/** 零单元边（R-1/R-2）：include 面的 sdurws/ird/* 白名单＝{core, diagnostics}。 */
TEST(DiagnosticsBuild, NoCrossUnitInclude_DT_BUILD_R1_R2)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "diagnostics" / sub)) {
            const auto text = readFile(unitRoot() / "diagnostics" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                // 提取 sdurws/ird/<unit>/ 的 <unit> 段与白名单比对——
                // ARCH §3.5 中 diagnostics 的出边仅 core 一条；自身头（同单元
                // 内部包含）天然合法。
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = line.substr(unitBegin, unitEnd - unitBegin);
                if (unit != "core" && unit != "diagnostics") {
                    ADD_FAILURE() << "diagnostics 仅可依赖 core（ARCH §3.5 唯一边），发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/** 公共头路径布局（R-2）：include/ 下文件仅位于 sdurws/ird/diagnostics/ 命名空间根。 */
TEST(DiagnosticsBuild, PublicHeaderPathLayout_DT_BUILD_R2)
{
    const auto incDir = unitRoot() / "diagnostics" / "include";
    for (const auto& rel : collectCppFiles(incDir)) {
        // 私有实现头不得混入公共 include 根（R-2）：任何出现在 include/ 的
        // 文件必须精确处于 sdurws/ird/diagnostics/ 之下；实现细节头只能放
        // src/ 以相对路径包含。
        const auto generic = rel.generic_string();
        EXPECT_EQ(generic.substr(0, std::string("sdurws/ird/diagnostics/").size()),
                  "sdurws/ird/diagnostics/")
            << "公共头越出命名空间根（无私有头出 include 红线）: " << generic;
    }
    SUCCEED() << "公共头路径布局合规（当前保留位期零契约头，规则随 DIAG-T03+ 落头自动生效）";
}
