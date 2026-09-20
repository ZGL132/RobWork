/**
 * @file   LinkageContractTest.cpp
 * @brief  reporting 跨单元契约测试（落位期）——构建图边界契约（acceptance
 *         2 逐项具名自证）。
 *
 * 设计依据：
 *   - units/reporting.md §3.4（目标 PUBLIC 链 core/evidence/diagnostics/
 *     project 四条登记边；`_contract_test`＝跨单元契约面：与 core/evidence
 *     值类型往返、与 project 工件汇集座、与 io 写出设施协作——随 RPT-T09/
 *     T11 展开；落位期＝构建图边界）、§3.2（依赖图：四条登记边＋标准库，
 *     零 io/runtime/execution 编译边——P-RPT-1/P-RPT-2 注入形态）、
 *     §3.3（注入边界实施形态）、§3.5 同层依赖表核对（"不新增任何表外
 *     同层编译边——表外边＝构建失败"SA-10）、§11 RPT-T01 行（验证方式
 *     RP-GATE-1；完成条件"红线扫描零命中"）；
 *   - 任务契约 tasks/foundation/RPT-T01.json acceptance 2（编译依赖边仅
 *     reporting→core/evidence/diagnostics/project 四条 ARCH §3.5 登记边、
 *     零 io/runtime/execution 编译边〔P-RPT-1/P-RPT-2 注入形态——公共头
 *     零对端类型〕、产品目标不链 testkit〔T-1〕）/acceptance 5（P-RPT-1/
 *     P-RPT-9 处置）；
 *   - 先例：project/test/LinkageContractTest.cpp（PRJ-T01 同款扫描形态——
 *     非注释行目标引用集合比对＋include 面双向扫描）。
 *
 * P-RPT-9 处置说明（为什么本文件只做构建图扫描、不消费上游头）：core/
 * evidence/diagnostics/project 各卡 v0.1 仍 Draft 未冻结，任务契约明文本
 * 任务"四条边仅链接目标不消费上游公共头"——消费契约类型的行为级自证随
 * RPT-T02（首个上游头消费任务）建立，避免在 Draft 基线上扩大返工面。
 * 因此落位期的跨单元边界证据由三面共同承载：①本文件的 CMakeLists 文本
 * 扫描（目标引用集合＝构建图显式边）；②reporting/CMakeLists.txt 文件
 * 末尾配置期守卫（链接属性两面读取）；③双模式构建与测试链接成功（链接
 * 器实证 sdurws_ird_reporting 可解析、可被消费）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// reporting 单元树根（industrialrobot 目录——IRD_REPORTING_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_REPORTING_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "reporting" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "reporting/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 reporting/CMakeLists.txt";
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集文本中全部 sdurws_ird_<unit>[<_role>] 目标名（先剥离 "#" 注释——
/// 注释文字如"不链 sdurws_ird_testkit"的处置说明不是构建图引用，参与扫描
/// 会误报）。目标名由单词边界分隔，逐字符扫描稳定实现。
std::set<std::string> collectTargetRefs(const std::string& text)
{
    std::set<std::string> refs;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) { line.erase(hashPos); }
        for (std::size_t pos = line.find("sdurws_ird_");
             pos != std::string::npos;
             pos = line.find("sdurws_ird_", pos + 1)) {
            std::size_t end = pos + std::string("sdurws_ird_").size();
            while (end < line.size()
                   && (std::isalnum(static_cast<unsigned char>(line[end]))
                       || line[end] == '_')) {
                ++end;
            }
            // 名字至少含一个单元名字符才计入：守卫正则模式（"^sdurws_ird_"）
            // 中的裸前缀后紧跟引号，不是目标引用（若不排除会把模式误报为边）。
            if (end > pos + std::string("sdurws_ird_").size()) {
                refs.insert(line.substr(pos, end - pos));
            }
        }
    }
    return refs;
}

/// 收集 include/＋src/ 全部 C++ 文件的 #include 指令行中出现的
/// sdurws/ird/<unit> 单元段（与 BuildRedLineTest 的指令行门控同语义——
/// 此处服务于 io/runtime/execution 注入形态的具名双向扫描）。
std::set<std::string> collectIncludedUnits()
{
    std::set<std::string> units;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "reporting" / sub;
        std::error_code ec;
        if (!fs::exists(base)) { continue; }
        for (auto it = fs::recursive_directory_iterator(base, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) continue;
            const auto ext = it->path().extension().string();
            if (ext != ".hpp" && ext != ".h" && ext != ".cpp") continue;
            std::ifstream in(it->path(), std::ios::binary);
            if (!in) {
                ADD_FAILURE() << "无法读取: " << it->path().string();
                continue;
            }
            const std::string text{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos
                    || line.compare(first, 8, "#include") != 0) {
                    continue;  // 仅认 #include 指令行——注释散文中的路径字样不误报
                }
                const auto pos = line.find("sdurws/ird/");
                if (pos == std::string::npos) { continue; }
                const auto unitBegin = pos + std::string("sdurws/ird/").size();
                const auto unitEnd = line.find('/', unitBegin);
                units.insert((unitEnd == std::string::npos)
                    ? line.substr(unitBegin)
                    : line.substr(unitBegin, unitEnd - unitBegin));
            }
        }
    }
    return units;
}

}  // namespace

/**
 * 链接图契约：reporting 的 CMake 目标引用集合仅含四条产品单元边＋testkit
 * （RPT-T11 契约套件报告设施——T-1 允许形态，仅测试目标，见 NoTestkitEdge
 * 用例的收窄断言）。
 *
 * 扫描单元 CMakeLists.txt 文本中出现的全部 sdurws_ird_* 目标引用，与白名单
 * 比对：产品目标链接 core/evidence/diagnostics/project 四条登记边（§3.4
 * 原文 PUBLIC 全链）＋本单元自身三目标（产品/测试/契约测试——同一单元
 * 内部引用不构成跨单元边，R-1 判定范围）＋sdurws_ird_testkit（RPT-T11
 * 登记：RecordListener/TempDir/DeterministicEnv/ContractCheck/IRD_TEST_INFO
 * 等测试设施消费——T-1 允许形态＝仅 `_test`/`_contract_test` 目标可链
 * testkit，产品目标仍禁止）。出现任何其他产品单元目标（io/runtime/
 * execution、modeling/requirements/kinematics/trajectory/dynamics/
 * drivetrain/selection/optimization 等业务域）即构建图越界（R-1：业务域
 * 单元互链禁止；P-RPT-1/P-RPT-2：io/runtime/execution 注入形态零编译边；
 * T-1：testkit 由下一用例具名钉住——DTB §5.3③）。
 */
TEST(ReportingLinkage, UnitEdgesFourRegistered_DT_BUILD_R1_R2)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：四条登记边（core/evidence/diagnostics/project）＋ reporting
    // 本单元三目标（产品/测试/契约测试——单元内部引用不构成跨单元边）＋
    // sdurws_ird_testkit（RPT-T11 报告设施接入——T-1 允许形态，仅测试目标；
    // NoTestkitEdge 用例钉住产品目标零 testkit）。
    const std::set<std::string> allowed = {
        "sdurws_ird_core",
        "sdurws_ird_evidence",
        "sdurws_ird_diagnostics",
        "sdurws_ird_project",
        "sdurws_ird_reporting",
        "sdurws_ird_reporting_test",
        "sdurws_ird_reporting_contract_test",
        "sdurws_ird_testkit"};
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "reporting 构建图出现白名单外目标引用（产品单元边仅 reporting→"
               "core/evidence/diagnostics/project 四条，ARCH §3.5；R-1/T-1/"
               "P-RPT-1/P-RPT-2）: " << ref;
    }
}

/**
 * T-1 红线具名自证（RPT-T11 收窄重述）：产品目标零 testkit 边——testkit
 * 只允许出现在**测试目标**的链接语句上（RP-GATE-1 第 4 项的登记后形态）。
 *
 * RPT-T11 按 T-1 允许形态登记测试目标的 testkit 消费（RecordListener/
 * TempDir/DeterministicEnv/ContractCheck/IRD_TEST_INFO——EX-T09/PRJ-T15
 * 同款节奏）：本用例由落位期"全文零 testkit"收窄为"testkit 引用只落在以
 * _test/_contract_test 目标为主语的链接语句行"——产品目标
 * （sdurws_ird_reporting）的链接面出现 testkit 即 T-1 违约。逐行扫描非
 * 注释文本：含 sdurws_ird_testkit 的行必须同时含测试目标名与
 * target_link_libraries（链接语句形态），且产品目标的链接块不出现该行。
 */
TEST(ReportingLinkage, NoTestkitEdge_DT_BUILD_T1)
{
    const std::string text = readCMakeLists();
    // 剥注释后逐行扫描（注释中的处置说明不是构建图引用——与目标引用
    // 扫描器同纪律）。
    int testkitLines = 0;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) { line.erase(hashPos); }
        if (line.find("sdurws_ird_testkit") == std::string::npos) { continue; }
        ++testkitLines;
        // T-1 允许形态的唯一落点：测试目标的链接语句（目标名与 testkit
        // 同行显式可见——CMakeLists 登记形态的共同约束，避免多行块解析）。
        const bool onTestTargetLine
            = (line.find("sdurws_ird_reporting_test") != std::string::npos
               || line.find("sdurws_ird_reporting_contract_test") != std::string::npos)
              && line.find("target_link_libraries") != std::string::npos;
        EXPECT_TRUE(onTestTargetLine)
            << "testkit 引用必须落在测试目标链接语句行（T-1：产品目标零 "
               "testkit——RPT-T11 收窄重述）: " << line;
    }
    ASSERT_GT(testkitLines, 0)
        << "RPT-T11 已登记测试目标 testkit 消费——链接语句应存在（本断言防"
           "扫描失效）；产品目标零 testkit 由上行逐行钉住";
}

/**
 * 四条登记边必须真实存在：acceptance 2"编译依赖边仅四条登记边"的存在性
 * 半区（§3.4 原文 PUBLIC 全链——与 execution 落位期 diagnostics 边注入
 * 不落链接的差异点，本断言钉住 reporting 侧四边全链口径）。
 *
 * "零表外边"（第一用例）只证明没有多余边，不证明登记边已建立——CMake
 * 文本中必须显式引用四个目标（target_link_libraries PUBLIC 形态，§3.4
 * 原文；别名形态 RWS::ird::* 在集成模式即其 ALIAS，CMake 侧取统一真名
 * 保证冒烟模式可解析——project 落位同款取舍）。链接器半区由双模式构建
 * 与测试目标链接成功自证（构建日志＝执行证据）。
 */
TEST(ReportingLinkage, RegisteredEdgesPresent_DT_BUILD)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_NE(refs.find("sdurws_ird_core"), refs.end())
        << "reporting→core 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链）";
    EXPECT_NE(refs.find("sdurws_ird_evidence"), refs.end())
        << "reporting→evidence 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链）";
    EXPECT_NE(refs.find("sdurws_ird_diagnostics"), refs.end())
        << "reporting→diagnostics 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链——"
           "与 execution 的注入形态不同，reporting 卡 §3.4 原文四边全链）";
    EXPECT_NE(refs.find("sdurws_ird_project"), refs.end())
        << "reporting→project 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链）";
}

/**
 * 零 io/runtime/execution 编译边（acceptance 2——P-RPT-1/P-RPT-2 注入
 * 形态具名自证）。
 *
 * ARCH §3.5 未登记 reporting→io、reporting→runtime、reporting→execution
 * 边（表外同层边＝构建失败，SA-10）。三者的能力经注入式最小接口消费
 * （§3.3）：io 写出设施经 IReportIoFactory（§9.5——公共头零 io 类型，
 * P-RPT-1/P-IO-1/P-PR-5 合并裁决前维持注入、届时签名零改动直连）、
 * runtime 摘要经 IModelSummaryProvider（§9.7）、execution 任务投影经
 * ITaskStatusSource（§3.3）。本断言双向扫描：include 面（include/＋src/
 * 零 sdurws/ird/{io,runtime,execution} 头——公共头零对端类型即注入形态
 * 的源码面判据）与构建图面（CMakeLists 非注释行零 sdurws_ird_{io,runtime,
 * execution} 目标引用）。业务用例级注入协作（fake 写出器等）随 RPT-T09/
 * T11 在测试面建立，不改产品编译边。
 */
TEST(ReportingLinkage, NoIoRuntimeExecutionCompileEdge_DT_BUILD_PRPT1_PRPT2)
{
    // include 面：零对端单元头（注入形态的源码面判据——公共头零对端类型）。
    const auto included = collectIncludedUnits();
    EXPECT_EQ(included.find("io"), included.end())
        << "reporting 零 io 编译边（P-RPT-1：io 写出设施经 IReportIoFactory "
           "注入，公共头零 io 类型）";
    EXPECT_EQ(included.find("runtime"), included.end())
        << "reporting 零 runtime 编译边（P-RPT-2：模型摘要经 "
           "IModelSummaryProvider 注入，§9.7）";
    EXPECT_EQ(included.find("execution"), included.end())
        << "reporting 零 execution 编译边（§3.3：任务投影经 ITaskStatusSource "
           "注入——同层反向边无必要）";
    // 构建图面：CMakeLists 非注释行零对端目标引用。
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_EQ(refs.find("sdurws_ird_io"), refs.end())
        << "reporting 落位期构建图不得出现 io 边（ARCH §3.5 未登记——P-RPT-1，"
           "合并裁决前维持注入）";
    EXPECT_EQ(refs.find("sdurws_ird_runtime"), refs.end())
        << "reporting 落位期构建图不得出现 runtime 边（ARCH §3.5 未登记——"
           "P-RPT-2 注入形态）";
    EXPECT_EQ(refs.find("sdurws_ird_execution"), refs.end())
        << "reporting 落位期构建图不得出现 execution 边（ARCH §3.5 未登记"
           "——§3.3 注入形态）";
}
