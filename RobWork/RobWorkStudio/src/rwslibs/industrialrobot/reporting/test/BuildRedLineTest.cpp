/**
 * @file   BuildRedLineTest.cpp
 * @brief  reporting 构建红线用例组（落位期最小构建冒烟用例——单元内源码
 *         include 面扫描，RP-GATE-1 的单元内半区）。
 *
 * 设计依据：
 *   - units/reporting.md §3.2（依赖图：core/evidence/diagnostics/project
 *     四条登记单元边＋C++17 标准库——零 Qt 含 Core〔D-01：报告渲染为纯
 *     字节生成〕、零 io/runtime/execution/业务单元编译边〔P-RPT-1/P-RPT-2
 *     注入形态〕、零 testkit〔T-1〕、零 Eigen/rw::math 直接包含）、§3.4
 *     （头包含形式与 R-2 纪律：私有实现头不入 include/）、§1.4（显式
 *     cxx_std_17、零 Qt 含 Core、零 rw::math/Eigen 直含）、§11 RPT-T01 行
 *     （验证方式：独立冒烟＋集成双模式构建＋RP-GATE-1；完成条件：两模式
 *     零错误＋红线扫描零命中）、§3.1（公共头保留位 README.md——契约头
 *     随 RPT-T02+ 落地）；
 *   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界可维护）；
 *   - 任务契约 tasks/foundation/RPT-T01.json acceptance 2（红线扫描零
 *     命中：零 Qt 含 Core、无私有头出 include、零 io/runtime/execution
 *     编译边、零 Eigen/rw::math 直接包含、产品目标不链 testkit——链接面
 *     各项的具名自证在 contract_test/LinkageContractTest.cpp，本文件承载
 *     include 面）；
 *   - 同构先例：core/testkit/runtime/evidence/policy/diagnostics/project/
 *     io/execution 各自的 BuildRedLine 用例组（形态一致，扫描面按单元卡
 *     红线裁剪）。
 *
 * 运行期源码扫描说明：扫描对象＝源码树（经 IRD_REPORTING_UNIT_ROOT 注入的
 * industrialrobot 根下的 reporting/），因此本用例对工作目录零依赖、在集成
 * 与独立冒烟两种模式下行为一致。CMake 配置期的链接面守卫（四条登记边/
 * 零 Qt/零 gtest）在 reporting/CMakeLists.txt 文件末尾，与本用例互为两道
 * 防线（源码 include 面＋目标链接面）；构建图边（io/runtime/execution/
 * testkit/业务单元）的 CMakeLists 文本扫描在 _contract_test 目标
 * （LinkageContractTest.cpp——§3.4 测试目标分工：`_contract_test`＝跨单元
 * 契约面）。
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

/// reporting 单元树根（industrialrobot 目录——IRD_REPORTING_UNIT_ROOT 注入；
/// 单元子目录 reporting/ 在此之下，与 project/execution 落位同款注入口径）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_REPORTING_UNIT_ROOT};
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
/// 密集且要求建立文档追溯，必须把"文档提及"与"编译期包含"区分开——
/// PRJ-T01 落位时曾以任意文本行扫描观察到一次真实误报并修正为指令行门控）。
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
 * DIAG-T02/PRJ-T01/EX-T01 同款）：CMake 对零源码 STATIC 库在生成期即报
 * "No SOURCES given to target"（实测），锚点文件是"库可生成、无接口预建"
 * （NFR-MNT-04）的最小载体。
 */
TEST(ReportingBuild, SourceTreeReachable_DT_BUILD)
{
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_REPORTING_UNIT_ROOT 不存在";
    // 公共头保留位：单元卡 §3.1 组成表约定 12 个契约头随 RPT-T02+ 落地，
    // 当前仅有 README.md（指向 §11——RPT-T01 复核结论，见留痕与该文件
    // 2026-09-19 行）——保留位存在性是头路径布局检查的前提。
    ASSERT_TRUE(fs::exists(unitRoot() / "reporting" / "include" / "sdurws" / "ird"
                           / "reporting" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    // src/ 空起步：RPT-T01 产物形态要求锚点翻译单元存在（STATIC 库可生成的
    // 最小条件），实现文件随 RPT-T02+ 填充——扫描非空即锚点在位。
    ASSERT_FALSE(collectCppFiles(unitRoot() / "reporting" / "src").empty())
        << "src 扫描为空（Reporting.cpp 锚点翻译单元应存在）";
}

/** 零 Qt（含 Core——单元卡 D-01 比 L3 上限更严）：产品面零 Q 头（NFR-MNT-01）。 */
TEST(ReportingBuild, NoQtInclude_DT_BUILD_NFR_MNT_01)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "reporting" / sub)) {
            const auto text = readFile(unitRoot() / "reporting" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // Qt 头只有 <QXxx>/"QXxx" 两种包含形态；`#include <Q` 与
                // `#include "Q` 前缀即可全盖（QtCore/QObject 同以前缀命中）。
                // reporting 是 L3 平台服务但采用比层规则更严的 D-01：报告
                // 渲染为纯字节生成（HTML/JSON/CSV 文本），预览宿主归 ui
                // ——零 Qt 含 Core 是设计决策而非仅层规则底线（§1.4），
                // 最大化模型测试直调能力（NFR-MNT-01 精神）。
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "reporting 禁含 Qt 头（含 Core，D-01）: "
                                  << rel.string() << ":" << lineno;
                }
            }
        }
    }
}

/** 零 Eigen/rw::math 直接包含（§1.4：本单元契约不含几何数值类型——数值经 SourcedValue/Quantity 承载）。 */
TEST(ReportingBuild, NoEigenOrRwMathDirectInclude_DT_BUILD)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "reporting" / sub)) {
            const auto text = readFile(unitRoot() / "reporting" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (!isIncludeDirective(line)) { continue; }
                // rw::math/Eigen 只允许经 core 公共头"传递"到达（§1.4 实测
                // 行）；本单元源码直接 include（#include <rw/...>、"rw/..."、
                // <Eigen...、"Eigen/..." 四种形态）即绕过 core 边＝红线命中。
                // reporting 报告数值呈现一律经 core SourcedValue/Quantity
                // 承载（§1.4"本文决定"行），渲染层零几何类型需求。
                if (line.find("#include <rw") != std::string::npos
                    || line.find("#include \"rw") != std::string::npos
                    || line.find("#include <Eigen") != std::string::npos
                    || line.find("#include \"Eigen") != std::string::npos) {
                    ADD_FAILURE() << "reporting 禁直接包含 Eigen/rw::math: "
                                  << rel.string() << ":" << lineno << " → " << line;
                }
            }
        }
    }
}

/**
 * 零表外单元边（R-1/R-2/P-RPT-9）：include 面的 sdurws/ird/* 白名单＝
 * {core, evidence, diagnostics, project, reporting}。
 *
 * ARCH §3.5 中 reporting 的出边仅 core/evidence/diagnostics/project 四条
 * （§3.2 依赖图原文）；io/runtime/execution 不在白名单即 acceptance 2
 * "零 io/runtime/execution 编译边"的 include 面证据——三者的能力经注入
 * 式最小接口消费（IReportIoFactory/IModelSummaryProvider/ITaskStatusSource
 * ——§3.3，公共头零对端类型），注入接口归 reporting 自有头（随 RPT-T05+
 * 落地），届时本扫描自动约束其 include 面。业务域单元（modeling 等）经
 * IReportSectionProvider 域注册协作（§9.2），编译期同样零边（R-1）。
 * 本任务自身另受 P-RPT-9 处置约束：连四条登记边的上游头也零消费（仅链接
 * 边），白名单放行四单元是为登记边的后续消费任务（RPT-T02+）预留的合法面。
 */
TEST(ReportingBuild, NoCrossUnitInclude_DT_BUILD_R1_R2)
{
    // 白名单：四条 ARCH §3.5 登记边＋本单元自身（同单元内部包含天然合法）。
    const std::vector<std::string> allowedUnits = {
        "core", "evidence", "diagnostics", "project", "reporting"};
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "reporting" / sub)) {
            const auto text = readFile(unitRoot() / "reporting" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                if (!isIncludeDirective(line)) { continue; }
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                // 提取 sdurws/ird/<unit>/ 的 <unit> 段与白名单比对——
                // 段后无 '/'（如行尾直接结束）时取到行尾——真实 include
                // 路径必含 "/<file>.hpp"，此分支仅兜底畸形行，取出的整段
                // 不匹配白名单即照常报错。
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                const auto unit = (unitEnd == std::string::npos)
                    ? line.substr(unitBegin)
                    : line.substr(unitBegin, unitEnd - unitBegin);
                const bool allowed = std::find(allowedUnits.begin(), allowedUnits.end(), unit)
                                     != allowedUnits.end();
                if (!allowed) {
                    ADD_FAILURE() << "reporting 仅可依赖 core/evidence/diagnostics/"
                                     "project（ARCH §3.5 四条登记编译链接边；io/runtime/"
                                     "execution 经注入消费——P-RPT-1/P-RPT-2），发现: "
                                  << rel.string() << " → " << line;
                }
            }
        }
    }
}

/** 公共头路径布局（R-2/NFR-MNT-02）：include/ 下文件仅位于 sdurws/ird/reporting/ 命名空间根。 */
TEST(ReportingBuild, PublicHeaderPathLayout_DT_BUILD_R2)
{
    const auto incDir = unitRoot() / "reporting" / "include";
    for (const auto& rel : collectCppFiles(incDir)) {
        // 私有实现头不得混入公共 include 根（R-2）：任何出现在 include/ 的
        // 文件必须精确处于 sdurws/ird/reporting/ 之下；实现细节头只能放
        // src/ 以相对路径包含（§3.4"私有实现头不入 include/"原文）。
        const auto generic = rel.generic_string();
        EXPECT_EQ(generic.substr(0, std::string("sdurws/ird/reporting/").size()),
                  "sdurws/ird/reporting/")
            << "公共头越出命名空间根（无私有头出 include 红线）: " << generic;
    }
    SUCCEED() << "公共头路径布局合规（当前保留位期零契约头，规则随 RPT-T02+ 落头自动生效）";
}
