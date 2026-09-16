/**
 * @file   BuildRedLineTest.cpp
 * @brief  project 构建红线用例组（PRJ-TX-14 前两项＋§1.4 直接包含禁令）
 *         ——零 Qt/头路径布局（无私有头出 include）自检。
 *
 * 设计依据：
 *   - units/project.md §11.2 PRJ-TX-14 行（构建红线：目标无 Qt 头、无私有
 *     头出 include——卡行验证方式"PRJ-TX-14 前两项"；脚本扫描、零命中）、
 *     §3.2（依赖图：core＋标准库＋Win32——零 Qt 含 Core〔D-01〕、零 Eigen/
 *     rw::math 直接包含、零其他单元）、§3.3（头包含形式与 R-2 纪律）、
 *     §12 PRJ-T01 行（涉及文件 src/（空起步）——锚点翻译单元形态）；
 *   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界可维护）、
 *     ARCH §3.5（project→core 唯一登记编译链接边）；
 *   - 任务契约 tasks/foundation/PRJ-T01.json acceptance 1（落位形态）/
 *     acceptance 2（红线扫描零命中）；
 *   - 同构先例：core/testkit/runtime/evidence/policy/diagnostics 各自的
 *     BuildRedLine 用例组（形态一致，扫描面按单元卡红线裁剪）。
 *
 * 运行期源码扫描说明：扫描对象＝源码树（经 IRD_PROJECT_UNIT_ROOT 注入的
 * industrialrobot 根下的 project/），因此本用例对工作目录零依赖、在集成
 * 与独立冒烟两种模式下行为一致。CMake 配置期的链接面守卫（单元边/零 Qt/
 * 零 gtest）在 project/CMakeLists.txt 文件末尾，与本用例互为两道防线
 * （源码 include 面＋目标链接面）；构建图边（业务单元/testkit/io）的
 * CMakeLists 文本扫描在 _contract_test 目标（LinkageContractTest.cpp——
 * §3.3 测试目标分工）。
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

/// project 单元树根（industrialrobot 目录——IRD_PROJECT_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_PROJECT_UNIT_ROOT};
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

/// 判断一行是否为 #include 预处理指令（跳过行首空白）。红线扫描只认指令行：
/// 注释散文中出现"sdurws/ird/core"等路径字样（如文件头的处置说明）不是
/// include 行为，按任意文本匹配会把注释误报为越界 include（本项目注释
/// 密集且要求建立文档追溯，必须把"文档提及"与"编译期包含"区分开）。
bool isIncludeDirective(const std::string& line)
{
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) { return false; }
    return line.compare(first, 8, "#include") == 0;
}

}  // namespace

/**
 * 锚定：源码树可达（公共头保留位 README＋src/ 锚点翻译单元——落位期形态）。
 *
 * "src/ 空起步"的既定先例形态＝仅含注释的锚点翻译单元（CORE-T01/POL-T01/
 * DIAG-T02 同款）：CMake 对零源码 STATIC 库在生成期即报
 * "No SOURCES given to target"（实测），锚点文件是"库可生成、无接口预建"
 * （NFR-MNT-04）的最小载体。
 */
TEST(ProjectBuild, SourceTreeReachable_DT_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_PROJECT_UNIT_ROOT 不存在";
    // 公共头保留位：单元卡 §3.1 组成表约定 10 个契约头随 PRJ-T04+ 落地，
    // 当前仅有 README.md（指向 §12——PRJ-T01 复核结论，见留痕）——保留位
    // 存在性是头路径布局检查的前提。
    ASSERT_TRUE(fs::exists(unitRoot() / "project" / "include" / "sdurws" / "ird"
                           / "project" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    // src/ 空起步：PRJ-T01 产物形态要求锚点翻译单元存在（STATIC 库可生成的
    // 最小条件），实现文件随 PRJ-T02+ 填充——扫描非空即锚点在位。
    ASSERT_FALSE(collectCppFiles(unitRoot() / "project" / "src").empty())
        << "src 扫描为空（Project.cpp 锚点翻译单元应存在）";
}

/** 零 Qt（含 Core——单元卡 D-01 比 L3 上限更严）：产品面零 Q 头（NFR-MNT-01）。 */
TEST(ProjectBuild, NoQtInclude_DT_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "project" / sub)) {
            const auto text = readFile(unitRoot() / "project" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // Qt 头只有 <QXxx>/"QXxx" 两种包含形态；`#include <Q` 与
                // `#include "Q` 前缀即可全盖（QtCore/QObject 同以前缀命中）。
                // project 的文件/线程/时间原语用 std＋Win32（§1.4），定时器
                // 归 ui 会话层（ARCH §3.1 DraftController）——零 Qt 是设计
                // 决策 D-01 而非仅层规则底线。
                // 误报排除（PRJ-T09 登记）：单元卡 §3.1 点名的公共头
                // QueryPort.hpp（②查询端口契约——§5.2）及其实现头以
                // "Query" 开头，命中 `#include "Q` 前缀启发式——Qt 头
                // 词表无 "Query*"（QtCore/QtGui/QtWidgets/…），本单元
                // 自有 Q 头以显式白名单排除；白名单随新增 Q 开头的自有
                // 头增量维护（红线判定不受影响——Qt 形态仍全盖）。
                const bool qtLikeInclude
                    = line.find("#include <Q") != std::string::npos
                      || line.find("#include \"Q") != std::string::npos;
                const bool ownQHeader
                    = line.find("#include \"QueryPort") != std::string::npos;
                if (qtLikeInclude && !ownQHeader) {
                    ADD_FAILURE() << "project 禁含 Qt 头（含 Core，D-01）: "
                                  << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零 Eigen/rw::math 直接包含（§1.4：几何数值类型仅经 core 公共头传递）。 */
TEST(ProjectBuild, NoEigenOrRwMathDirectInclude_DT_BUILD)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "project" / sub)) {
            const auto text = readFile(unitRoot() / "project" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // rw::math/Eigen 只允许经 core 公共头"传递"到达（§3.2 依赖
                // 图）；本单元源码直接 include（#include <rw/...>、"rw/..."、
                // <Eigen...、"Eigen/..." 四种形态）即绕过 core 边＝红线命中。
                // 注意 Win32（windows.h 等）不在禁令内——§1.4 明确 kernel32
                // 是 project 的设计内依赖（隔离于 src/win32/）。
                if (line.find("#include <rw") != std::string::npos
                    || line.find("#include \"rw") != std::string::npos
                    || line.find("#include <Eigen") != std::string::npos
                    || line.find("#include \"Eigen") != std::string::npos) {
                    ADD_FAILURE() << "project 禁直接包含 Eigen/rw::math: "
                                  << rel.string() << ":" << lineno << " → " << line;
                }
            }
        }
    }
}

/**
 * 零表外单元边（R-1/R-2）：include 面的 sdurws/ird/* 白名单＝{core, project}。
 *
 * ARCH §3.5 中 project 的出边仅 core 一条（diagnostics 边按注入形态不落
 * 编译期 include——§3.2/§5.0，P-PR-6/P-EX-8 裁决链接形态）；io 与业务域
 * 单元零命中即 acceptance 2"零业务边/零 io 编译期消费"（O-09 处置：阶段 A
 * 零 io，包导入导出/固化归阶段 B WP-04-T17/T18）的 include 面证据。
 * 本任务自身另受 P-PR-1 处置约束：连 core 头也零消费（仅链接边），白名单
 * 放行 core 是为 ARCH §3.5 登记边的后续消费任务（PRJ-T04+）预留的合法面。
 */
TEST(ProjectBuild, NoCrossUnitInclude_DT_BUILD_R1_R2)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "project" / sub)) {
            const auto text = readFile(unitRoot() / "project" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                if (!isIncludeDirective(line)) { continue; }
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                // 提取 sdurws/ird/<unit>/ 的 <unit> 段与白名单比对——
                // ARCH §3.5 中 project 的编译期出边仅 core 一条；自身头
                // （同单元内部包含）天然合法。段后无 '/'（如行尾直接结束）
                // 时取到行尾——真实 include 路径必含 "/<file>.hpp"，此分支
                // 仅兜底畸形行，取出的整段不匹配白名单即照常报错。
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = (unitEnd == std::string::npos)
                    ? line.substr(unitBegin)
                    : line.substr(unitBegin, unitEnd - unitBegin);
                if (unit != "core" && unit != "project") {
                    ADD_FAILURE() << "project 仅可依赖 core（ARCH §3.5 唯一登记"
                                     "编译链接边；diagnostics 经注入不落 include），"
                                     "发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/** 公共头路径布局（R-2）：include/ 下文件仅位于 sdurws/ird/project/ 命名空间根。 */
TEST(ProjectBuild, PublicHeaderPathLayout_DT_BUILD_R2)
{
    const auto incDir = unitRoot() / "project" / "include";
    for (const auto& rel : collectCppFiles(incDir)) {
        // 私有实现头不得混入公共 include 根（R-2）：任何出现在 include/ 的
        // 文件必须精确处于 sdurws/ird/project/ 之下；实现细节头只能放 src/
        // 以相对路径包含（§3.3"私有实现头不入 include/"原文）。
        const auto generic = rel.generic_string();
        EXPECT_EQ(generic.substr(0, std::string("sdurws/ird/project/").size()),
                  "sdurws/ird/project/")
            << "公共头越出命名空间根（无私有头出 include 红线）: " << generic;
    }
    SUCCEED() << "公共头路径布局合规（当前保留位期零契约头，规则随 PRJ-T04+ 落头自动生效）";
}
