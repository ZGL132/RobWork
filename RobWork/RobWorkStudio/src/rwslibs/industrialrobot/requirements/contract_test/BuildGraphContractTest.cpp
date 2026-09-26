/**
 * @file   BuildGraphContractTest.cpp
 * @brief  requirements 跨单元契约测试（落位期）——构建图边界契约（任务
 *         契约 WP-14-T02 acceptance 2/4 的具名自证面）。
 *
 * 设计依据：
 *   - units/requirements.md §3.2（五条接口依赖边边表——ARCH §3.5"各业务
 *     域单元→L2/L3 公共接口"许可方向的实例化；"_plugin 随 WP-14-T08 落
 *     位"；"禁止链接任何其他业务域单元（R-1）"）、§3.1（二分结构——
 *     无 _worker）
 *   - cmake/ird_gates_whitelist.cmake 的 "requirements->…" 五行（机器面
 *     同源数据——本测试以文本扫描单元 CMakeLists 复核同一事实，双面
 *     一致）
 *   - 先例：modeling/contract_test/BuildGraphContractTest.cpp（WP-13-T02
 *     同款——落位期跨单元边界证据由 CMakeLists 文本扫描＋配置期守卫＋
 *     双模式构建链接成功三面共同承载）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 2（配置期红线
 *     守卫随文件自持）/acceptance 4（_test/_contract_test 随文件注册、
 *     _plugin 不落位）
 *
 * ★ T08 落位随附同步（合法登记——tasks/foundation/WP-14-T08.json
 *   acceptance 3；登记于 units/requirements.md §14.6 v0.8；modeling T15
 *   同款先例）：原"落位期不得出现 _plugin"断言按 T08 交付翻转——_plugin
 *   目标本任务落位（卡 §3.1/§3.2 明文），其链接面＝本单元计算库＋
 *   sdurws_ird_ui＋Qt（插件分类面 sanctioned；ui 边的行级钉住见
 *   NoBusinessUnitOrExtraPlatformEdge）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

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

/// requirements 单元树根（industrialrobot 目录——IRD_REQUIREMENTS_UNIT_ROOT
/// 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_REQUIREMENTS_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "requirements" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "requirements/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 requirements/CMakeLists.txt";
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集文本中全部 sdurws_ird_<unit>[<_role>] 目标名（先剥离 "#" 注释——
/// 注释文字如"不链 sdurws_ird_testkit"的处置说明不是构建图引用，参与扫描
/// 会误报）。目标名由单词边界分隔，逐字符扫描稳定实现（modeling 同款）。
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
/// sdurws/ird/<unit> 单元段（仅认 #include 指令行——注释散文中的路径
/// 字样不误报；modeling 同款）。
std::set<std::string> collectIncludedUnits()
{
    std::set<std::string> units;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "requirements" / sub;
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
                    continue;  // 仅认 #include 指令行——注释散文不误报
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

/// 五条登记边单元（卡 §3.2 边表序——与白名单 "requirements->…" 五行同源；
/// runtime 暂不登记——T02 零 runtime 公共值类型引用，契约 acceptance 2）。
constexpr const char* kEdgeUnits[] = {
    "core", "diagnostics", "project", "io", "evidence",
};

}  // namespace

/**
 * 构建图封闭性：requirements 的 CMake 目标引用集合仅含五条登记边＋本单元
 * 三目标（产品/两测试——单元内部引用不构成跨单元边；_plugin 随 WP-14-T08
 * 落位，T02 不得出现）＋testkit（报告设施——T-1 允许形态，仅测试目标）。
 * 出现任何其他目标引用（业务域单元 modeling/kinematics/trajectory/
 * dynamics/drivetrain/selection/optimization＝R-1 面；平台单元 runtime/
 * execution/reporting＝SUB 面——runtime 边未登记，卡 §3.2"仅在实际
 * include 其公共值类型时登记"）即构建图越界。
 */
TEST(ReqBuildGraph, UnitEdgesFiveRegisteredAndClosed_WP14T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02", "NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：五条登记边＋本单元四目标（产品/插件/测试/契约测试——
    // _plugin 随 WP-14-T08 落位，卡 §3.1/§3.2 原文）＋sdurws_ird_ui
    // （T08 插件链接面——仅插件目标，行级钉住见 NoBusinessUnitOrExtra
    // PlatformEdge）＋sdurws_ird_testkit（报告设施——T-1 允许形态＝仅
    // 测试目标可链，产品目标由 NoTestkitEdge 钉住）。
    std::set<std::string> allowed = {"sdurws_ird_requirements",
                                     "sdurws_ird_requirements_plugin",
                                     "sdurws_ird_requirements_test",
                                     "sdurws_ird_requirements_contract_test",
                                     "sdurws_ird_ui",
                                     "sdurws_ird_testkit"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(std::string("sdurws_ird_") + u);
    }
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "requirements 构建图出现白名单外目标引用（产品单元边仅 "
               "requirements→core/diagnostics/project/io/evidence 五条＋插件面"
               "本单元计算库/ui——requirements.md §3.2、ARCH §3.5；R-1/SUB/"
               "T-1）: " << ref;
    }
    // 目标形态（卡 §3.1/§3.2）：_plugin 已随 WP-14-T08 落位（出现即卡面
    // 原文；链接面＝本单元计算库＋sdurws_ird_ui＋Qt——插件分类面 sanctioned，
    // modeling WP-13-T15 同款断言翻转先例）；_worker 恒不存在（二分结构
    // 没有 _worker——需求命令在主进程命令执行线程串行，卡 §3.1）。
    EXPECT_NE(refs.find("sdurws_ird_requirements_plugin"), refs.end())
        << "_plugin 目标已随 WP-14-T08 落位（卡 §3.2——T02 期不预建约束已由"
           "本任务兑现翻转，modeling T15 同款随附同步）";
    EXPECT_EQ(refs.find("sdurws_ird_requirements_worker"), refs.end())
        << "requirements 无 _worker 形态（ARCH §3.3 二分结构；卡 §3.1）";
}

/**
 * 五条登记边必须真实存在（acceptance 2——登记边的存在性半区）："封闭
 * 性"（第一用例）只证明没有多余边，不证明登记边已建立——CMake 文本中
 * 必须显式引用五个目标（target_link_libraries PUBLIC 形态，卡 §3.2 边表
 * 统一真名——集成/冒烟两模式可解析）。链接器半区由双模式构建与测试目标
 * 链接成功自证（构建日志＝执行证据）。
 */
TEST(ReqBuildGraph, FiveRegisteredEdgesPresent_WP14T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    for (const char* u : kEdgeUnits) {
        EXPECT_NE(refs.find(std::string("sdurws_ird_") + u), refs.end())
            << "requirements→" << u << " 登记边缺失（requirements.md §3.2 "
               "边表/ARCH §3.5 许可方向实例化）";
    }
}

/**
 * 业务域单元零互链＋零表外平台边（acceptance 2——R-1 具名自证）：
 * modeling/kinematics/trajectory/dynamics/drivetrain/selection/optimization
 * 七个业务域单元（R-1 判定集合，含 drivetrain——whitelist
 * IRD_BUSINESS_UNITS 及 ARCH §3.5）不得出现在 requirements 的构建图中；
 * 平台单元 runtime/execution/reporting（五边之外）同禁（SUB 面）——
 * runtime 边未登记（T02 零引用；实际引用时按卡 §3.2 增登）。跨单元协作
 * 走六类端口（命令/查询/评估器/策略/事件/名称）——R-1 红线在 requirements
 * 侧的构建面形态。
 */
TEST(ReqBuildGraph, NoBusinessUnitOrExtraPlatformEdge_WP14T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    // R-1 面：业务域单元（requirements 自身除外）。
    for (const char* business :
         {"modeling", "kinematics", "trajectory", "dynamics",
          "drivetrain", "selection", "optimization"}) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + business), refs.end())
            << "业务域互链禁止（R-1 无例外——SA-10）: requirements→" << business;
    }
    // SUB 面：五边之外的平台单元（runtime 未登记边——卡 §3.2 注；ui 例外
    // ＝下方行级钉住）。
    for (const char* platform : {"runtime", "execution", "reporting"}) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + platform), refs.end())
            << "表外平台边（白名单外——构建失败面）: requirements→" << platform;
    }

    // ui 边行级钉住（T08 落位随附同步——modeling T15 同款）：剥注释后
    // 逐行扫描——每条含 sdurws_ird_ui 的语句行必须同时含插件目标名与
    // target_link_libraries（即插件链接语句；单行链接语句的登记形态约束
    // 见 CMakeLists 插件链接块注释）。
    int uiLines = 0;
    std::istringstream lines(readCMakeLists());
    std::string line;
    while (std::getline(lines, line)) {
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) { line.erase(hashPos); }
        if (line.find("sdurws_ird_ui") == std::string::npos) { continue; }
        ++uiLines;
        EXPECT_TRUE(line.find("sdurws_ird_requirements_plugin") != std::string::npos
                    && line.find("target_link_libraries") != std::string::npos)
            << "ui 目标引用只允许落在插件目标链接语句行（T08 卡 §3.2 插件"
               "链接面——扩散即越权）: " << line;
    }
    ASSERT_GT(uiLines, 0)
        << "插件链接面应已登记 ui 边（T08 卡 §3.2——缺失即插件面回退）；"
           "计算库/测试目标零 ui 边由上行逐行钉住";
}

/**
 * testkit 仅落在测试目标链接语句（T-1 具名自证——modeling 同款收窄
 * 重述）：testkit 不随产品分发（testkit.md §2.4 T-1）——产品目标
 * sdurws_ird_requirements 的链接面出现 testkit 即违约；测试目标
 * （_test/_contract_test）的报告设施消费是允许形态。
 */
TEST(ReqBuildGraph, NoTestkitEdgeOnProductTarget_WP14T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

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
            = (line.find("sdurws_ird_requirements_test") != std::string::npos
               || line.find("sdurws_ird_requirements_contract_test") != std::string::npos)
              && line.find("target_link_libraries") != std::string::npos;
        EXPECT_TRUE(onTestTargetLine)
            << "testkit 引用必须落在测试目标链接语句行（T-1：产品目标零 "
               "testkit）: " << line;
    }
    ASSERT_GT(testkitLines, 0)
        << "报告设施已登记测试目标 testkit 消费——链接语句应存在（本断言"
           "防扫描失效）；产品目标零 testkit 由上行逐行钉住";
}

/**
 * include 面与构建图同界（acceptance 2——源码面半区）：include/＋src/
 * 的 #include 指令行出现的单元段只允许 requirements 自身＋五条登记边
 * 单元（与 BuildRedLineTest 的五边白名单同判据；此处以"集合封闭"口径
 * 复核——两个独立实现的同界断言互为防线）。业务用例级跨单元契约（fake
 * 对端等）随 T04+ 在测试面建立，不改产品编译边。
 */
TEST(ReqBuildGraph, IncludeFaceMatchesRegisteredEdges_WP14T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto included = collectIncludedUnits();
    std::set<std::string> allowed = {"requirements"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(u);
    }
    for (const auto& unit : included) {
        EXPECT_NE(allowed.find(unit), allowed.end())
            << "产品面 include 越界单元（五条登记边外——R-1/R-2）: " << unit;
    }
}
