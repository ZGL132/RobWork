/**
 * @file   BuildGraphContractTest.cpp
 * @brief  kinematics 跨单元契约测试（落位期）——构建图边界契约（任务
 *         契约 WP-15-T02 acceptance 2/3/5 的具名自证面）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.2（六条接口依赖边边表——ARCH §3.5"各业务
 *     域单元→L2/L3 公共接口"许可方向的实例化；"_plugin 随 WP-15-T12"；
 *     "禁止链接任何其他业务域单元（R-1）"）、§3.1（二分结构——无
 *     _worker 形态："worker 进程复用本计算库"）
 *   - cmake/ird_gates_whitelist.cmake 的 "kinematics->…" 六行（机器面
 *     同源数据——本测试以文本扫描单元 CMakeLists 复核同一事实，双面
 *     一致）
 *   - 先例：requirements/contract_test/BuildGraphContractTest.cpp
 *     （WP-14-T02 同款——落位期跨单元边界证据由 CMakeLists 文本扫描＋
 *     配置期守卫＋双模式构建链接成功三面共同承载；modeling 更早先例）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 2（_test/
 *     _contract_test 随文件注册、_plugin 不落位）/acceptance 3（六边
 *     登记，表外边检出即失败）/acceptance 5（R-1 机器证据）
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

/// kinematics 单元树根（industrialrobot 目录——IRD_KINEMATICS_UNIT_ROOT
/// 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_KINEMATICS_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "kinematics" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "kinematics/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 kinematics/CMakeLists.txt";
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集文本中全部 sdurws_ird_<unit>[<_role>] 目标名（先剥离 "#" 注释——
/// 注释文字如"不链 sdurws_ird_testkit"的处置说明不是构建图引用，参与扫描
/// 会误报）。目标名由单词边界分隔，逐字符扫描稳定实现（modeling/
/// requirements 同款）。
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
/// 字样不误报；modeling/requirements 同款）。
std::set<std::string> collectIncludedUnits()
{
    std::set<std::string> units;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "kinematics" / sub;
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

/// 六条登记边单元（卡 §3.2 边表序——与白名单 "kinematics->…" 六行同源；
/// 契约 acceptance 3 六边全登记——均属 ARCH §3.5"业务域→L2/L3"既有许可
/// 方向实例化）。
constexpr const char* kEdgeUnits[] = {
    "core", "diagnostics", "runtime", "policy", "evidence", "execution",
};

}  // namespace

/**
 * 构建图封闭性：kinematics 的 CMake 目标引用集合仅含六条登记边＋本单元
 * 三目标（产品/两测试——单元内部引用不构成跨单元边；_plugin 随 WP-15-
 * T12 落位，T02 不得出现）＋testkit（报告设施——T-1 允许形态，仅测试
 * 目标）。出现任何其他目标引用（业务域单元 modeling/requirements/
 * trajectory/dynamics/drivetrain/selection/optimization＝R-1 面；表外
 * 平台单元 reporting＝SUB 面）即构建图越界（acceptance 3"表外边检出即
 * 失败"的单元内复核面）。
 */
TEST(KinBuildGraph, UnitEdgesSixRegisteredAndClosed_WP15T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02", "NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：六条登记边＋本单元三目标（产品/测试/契约测试）＋
    // sdurws_ird_testkit（报告设施——T-1 允许形态＝仅测试目标可链，产品
    // 目标由 NoTestkitEdge 钉住）。
    std::set<std::string> allowed = {"sdurws_ird_kinematics",
                                     "sdurws_ird_kinematics_test",
                                     "sdurws_ird_kinematics_contract_test",
                                     "sdurws_ird_testkit"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(std::string("sdurws_ird_") + u);
    }
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "kinematics 构建图出现白名单外目标引用（产品单元边仅 "
               "kinematics→core/diagnostics/runtime/policy/evidence/"
               "execution 六条——kinematics.md §3.2、ARCH §3.5；R-1/SUB/"
               "T-1）: " << ref;
    }
    // 目标形态（卡 §3.1/§3.2/契约 acceptance 2）：_plugin 本任务不落位
    // （随 WP-15-T12）；_worker 恒不存在（卡 §3.1 明文"不新建——worker
    // 进程复用本计算库"——ARCH §4.1 WorkerLauncher 派发面）。
    EXPECT_EQ(refs.find("sdurws_ird_kinematics_plugin"), refs.end())
        << "_plugin 目标随 WP-15-T12 落位（契约 acceptance 2——T02 不预建）";
    EXPECT_EQ(refs.find("sdurws_ird_kinematics_worker"), refs.end())
        << "kinematics 无 _worker 形态（卡 §3.1——worker 复用计算库）";
}

/**
 * 六条登记边必须真实存在（acceptance 3——登记边的存在性半区）："封闭
 * 性"（第一用例）只证明没有多余边，不证明登记边已建立——CMake 文本中
 * 必须显式引用六个目标（target_link_libraries PUBLIC 形态，卡 §3.2 边表
 * 统一真名——集成/冒烟两模式可解析）。链接器半区由双模式构建与测试目标
 * 链接成功自证（构建日志＝执行证据）。
 */
TEST(KinBuildGraph, SixRegisteredEdgesPresent_WP15T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    for (const char* u : kEdgeUnits) {
        EXPECT_NE(refs.find(std::string("sdurws_ird_") + u), refs.end())
            << "kinematics→" << u << " 登记边缺失（kinematics.md §3.2 "
               "边表/ARCH §3.5 许可方向实例化）";
    }
}

/**
 * 业务域单元零互链＋零表外平台边（acceptance 5——R-1 具名自证）：
 * modeling/requirements/trajectory/dynamics/drivetrain/selection/
 * optimization 七个业务域单元（R-1 判定集合，whitelist IRD_BUSINESS_
 * UNITS 及 ARCH §3.5，kinematics 自身除外）不得出现在 kinematics 的构建
 * 图中；平台单元 reporting（六边之外唯一平台单元）同禁（SUB 面）。跨
 * 单元协作走六类端口（命令/查询/评估器/策略/事件/名称）——R-1 红线在
 * kinematics 侧的构建面形态（ird_gates 零命中互为机器证据）。
 */
TEST(KinBuildGraph, NoBusinessUnitOrExtraPlatformEdge_WP15T02_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    // R-1 面：业务域单元（kinematics 自身除外）。
    for (const char* business :
         {"modeling", "requirements", "trajectory", "dynamics",
          "drivetrain", "selection", "optimization"}) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + business), refs.end())
            << "业务域互链禁止（R-1 无例外——SA-10）: kinematics→" << business;
    }
    // SUB 面：六边之外的平台单元（reporting 唯一表外——runtime/execution
    // 已在六边内）。
    for (const char* platform : {"reporting"}) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + platform), refs.end())
            << "表外平台边（白名单外——构建失败面）: kinematics→" << platform;
    }
}

/**
 * testkit 仅落在测试目标链接语句（T-1 具名自证——modeling/requirements
 * 同款收窄重述）：testkit 不随产品分发（testkit.md §2.4 T-1）——产品
 * 目标 sdurws_ird_kinematics 的链接面出现 testkit 即违约；测试目标
 * （_test/_contract_test）的报告设施消费是允许形态。
 */
TEST(KinBuildGraph, NoTestkitEdgeOnProductTarget_WP15T02_ACC3)
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
            = (line.find("sdurws_ird_kinematics_test") != std::string::npos
               || line.find("sdurws_ird_kinematics_contract_test") != std::string::npos)
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
 * include 面与构建图同界（acceptance 5——源码面半区）：include/＋src/
 * 的 #include 指令行出现的单元段只允许 kinematics 自身＋六条登记边
 * 单元（与 BuildRedLineTest 的六边白名单同判据；此处以"集合封闭"口径
 * 复核——两个独立实现的同界断言互为防线）。业务对端面契约（fake 对端
 * 等）随 T03~T07 在测试面建立，不改产品编译边。
 */
TEST(KinBuildGraph, IncludeFaceMatchesRegisteredEdges_WP15T02_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto included = collectIncludedUnits();
    std::set<std::string> allowed = {"kinematics"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(u);
    }
    for (const auto& unit : included) {
        EXPECT_NE(allowed.find(unit), allowed.end())
            << "产品面 include 越界单元（六条登记边外——R-1/R-2）: " << unit;
    }
}
