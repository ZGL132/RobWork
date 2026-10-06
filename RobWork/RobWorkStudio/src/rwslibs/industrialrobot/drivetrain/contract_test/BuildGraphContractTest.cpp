/**
 * @file   BuildGraphContractTest.cpp
 * @brief  drivetrain 跨单元契约测试（落位期）——构建图边界契约（任务
 *         契约 WP-18-T02 acceptance 1/2 的具名自证面）。
 *
 * 设计依据：
 *   - units/drivetrain.md §2.3（L2 共享计算服务——非业务域插件；不要求
 *     plugin/worker 目标，§4.2 目标表明文二者默认禁止创建）、§3.2（依赖
 *     形态与红线——登记依赖边仅两条：drivetrain→core、drivetrain→
 *     evidence；表外同层边含 drivetrain→policy/runtime/diagnostics＝
 *     未登记依赖；R-1 业务目标互链禁止自查行）
 *   - cmake/ird_gates_whitelist.cmake 的 "drivetrain->…" 两行（机器面
 *     同源数据——本测试以文本扫描单元 CMakeLists 复核同一事实，双面
 *     一致；两行为 ARCH §3.5 原始登记边，dependency-graph.json 第 8/9
 *     行同源镜像）
 *   - 先例：workflow/contract_test/BuildGraphContractTest.cpp（WP-22-T02
 *     同款——落位期跨单元边界证据由 CMakeLists 文本扫描＋配置期守卫＋
 *     双模式构建链接成功三面共同承载；kinematics/modeling/requirements
 *     更早先例）
 *   - 任务契约 tasks/foundation/WP-18-T02.json acceptance 1（STATIC 落位
 *     ＋_test/_contract_test 创建——无插件目标）/acceptance 2（红线扫描
 *     零命中——不直链 proximity、无业务域边）
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

/// drivetrain 单元树根（industrialrobot 目录——IRD_DRIVETRAIN_UNIT_ROOT
/// 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DRIVETRAIN_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "drivetrain" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "drivetrain/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 drivetrain/CMakeLists.txt";
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集文本中全部 sdurws_ird_<unit>[<_role>] 目标名（先剥离 "#" 注释——
/// 注释文字如"不链 sdurws_ird_testkit"的处置说明不是构建图引用，参与扫描
/// 会误报）。目标名由单词边界分隔，逐字符扫描稳定实现（workflow/
/// kinematics/modeling 同款）。
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
/// 字样不误报；workflow/kinematics 同款）。
std::set<std::string> collectIncludedUnits()
{
    std::set<std::string> units;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "drivetrain" / sub;
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

/// 两条登记边单元（卡 §3.2 白名单序——与白名单 "drivetrain->…" 两行
/// 同源；两边为 ARCH §3.5 原始登记边〔L2 共享计算服务→L2/L3 公共接口
/// 许可方向〕，dependency-graph.json 第 8/9 行同源）。
constexpr const char* kEdgeUnits[] = {
    "core", "evidence",
};

/// 业务域单元清单（R-1 判定集合——whitelist IRD_BUSINESS_UNITS 及
/// ARCH §3.5；drivetrain 自身不在其内——L2 共享计算服务非业务域插件，
/// 卡 §2.3）。
constexpr const char* kBusinessUnits[] = {
    "modeling", "requirements", "kinematics", "trajectory",
    "dynamics", "selection", "optimization",
};

/// 表外平台单元（两条登记边之外的全部平台/编排单元——卡 §3.2 自查表
/// 点名 drivetrain→policy/runtime/diagnostics＝表外边；其余平台单元同
 /// 禁——SUB 面）。testkit 由 NoTestkitEdge 用例单独钉住产品面。
constexpr const char* kExtraPlatformUnits[] = {
    "diagnostics", "runtime", "policy", "project", "execution",
    "io", "reporting", "ui", "workflow",
};

}  // namespace

/**
 * 构建图封闭性：drivetrain 的 CMake 目标引用集合仅含两条登记边＋本单元
 * 三目标（产品/两测试——T02 契约明文同批创建；单元内部引用不构成跨单元
 * 边）＋testkit（报告设施——T-1 允许形态，仅测试目标）。出现任何其他
 * 目标引用（业务域单元＝R-1 面；表外平台单元＝SUB 面；_plugin/_worker
 * ＝卡 §2.3/§4.2 明文默认禁止创建——如需走 P-DT-8 架构变更通道）即
 * 构建图越界（acceptance 2"无业务域边"的单元内复核面）。
 */
TEST(DtBuildGraph, UnitEdgesTwoRegisteredAndClosed_WP18T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02", "NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：两条登记边＋本单元三目标（产品/测试/契约测试——T02 契约
    // acceptance 1 明文同批创建）＋sdurws_ird_testkit（报告设施——T-1
    // 允许形态＝仅测试目标可链，产品目标由 NoTestkitEdge 用例钉住）。
    std::set<std::string> allowed = {"sdurws_ird_drivetrain",
                                     "sdurws_ird_drivetrain_test",
                                     "sdurws_ird_drivetrain_contract_test",
                                     "sdurws_ird_testkit"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(std::string("sdurws_ird_") + u);
    }
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "drivetrain 构建图出现白名单外目标引用（产品单元边仅 "
               "drivetrain→core/evidence 两条——drivetrain.md §3.2、"
               "ARCH §3.5；R-1/SUB/T-1）: " << ref;
    }
    // 目标形态（卡 §2.3/§4.2 目标表＋契约 acceptance 1"无插件目标"）：
    // _plugin 与 _worker 恒不存在——drivetrain 无独立界面（工作点呈现由
    // dynamics/selection/reporting 的界面承载）、worker 归 execution 所有
    // （ARCH §4.1：计算内核经评估器注册进入 worker 装配清单，worker 不属
    // 于 drivetrain）；例外唯一通道＝P-DT-8 架构变更登记。
    EXPECT_EQ(refs.find("sdurws_ird_drivetrain_plugin"), refs.end())
        << "drivetrain 无 _plugin 形态（卡 §2.3/§4.2 默认禁止——无独立界面）";
    EXPECT_EQ(refs.find("sdurws_ird_drivetrain_worker"), refs.end())
        << "drivetrain 无 _worker 形态（卡 §2.3/§4.2——worker 归 execution）";
}

/**
 * 两条登记边必须真实存在（acceptance 1——"依赖仅 core＋evidence"的
 * 存在性半区）："封闭性"（第一用例）只证明没有多余边，不证明登记边已
 * 建立——CMake 文本中必须显式引用两个目标（target_link_libraries PUBLIC
 * 形态，卡 §3.2 白名单统一真名——集成/冒烟两模式可解析）。链接器半区
 * 由双模式构建与测试目标链接成功自证（构建日志＝执行证据）。
 */
TEST(DtBuildGraph, TwoRegisteredEdgesPresent_WP18T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    for (const char* u : kEdgeUnits) {
        EXPECT_NE(refs.find(std::string("sdurws_ird_") + u), refs.end())
            << "drivetrain→" << u << " 登记边缺失（drivetrain.md §3.2 白名单"
               "/ARCH §3.5 L2 共享计算服务两条登记边）";
    }
}

/**
 * 业务域单元零互链＋零表外平台边（acceptance 2——R-1 具名自证）：
 * 七个业务域单元（R-1 判定集合）不得出现在 drivetrain 的构建图中——
 * dynamics 关节侧序列经 UpstreamResult 切片条目值传递消费、selection 消费
 * 经③评估器端口（卡 §3.3 端口协作总图/§12），不落编译边；表外平台单元
 * （diagnostics/runtime/policy 等——卡 §3.2 自查表点名 drivetrain→
 * policy/runtime/diagnostics）同禁（SUB 面）。跨单元协作走六类端口——
 * R-1 红线在 drivetrain 侧的构建面形态（ird_gates 零新增命中互为机器
 * 证据）。零 sdurw_proximity 直链（R-5——碰撞唯一实现归 policy）由
 * 封闭性用例的白名单封闭承载（proximity 非目标引用面，ird_gates 第 2c
 * 步专查）。
 */
TEST(DtBuildGraph, NoBusinessUnitOrExtraPlatformEdge_WP18T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    // R-1 面：业务域单元（drivetrain 是 L2 共享计算服务不在业务域清单，
    // 无需自除——卡 §2.3；但反向边同样禁止：业务单元也不得直链
    // drivetrain 之外的登记面外形态，此处钉住的是 drivetrain 的出边）。
    for (const char* business : kBusinessUnits) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + business), refs.end())
            << "业务域互链禁止（R-1 无例外——SA-10；卡 §3.2 自查行）: "
               "drivetrain→" << business;
    }
    // SUB 面：两条登记边之外的平台单元（卡 §3.2 自查表点名 diagnostics/
    // runtime/policy——testkit 由 NoTestkitEdge 用例单独钉住产品面）。
    for (const char* platform : kExtraPlatformUnits) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + platform), refs.end())
            << "表外平台边（白名单外——构建失败面）: drivetrain→" << platform;
    }
}

/**
 * testkit 仅落在测试目标链接语句（T-1 具名自证——kinematics/workflow
 * 同款收窄重述）：testkit 不随产品分发（testkit.md §2.4 T-1）——产品
 * 目标 sdurws_ird_drivetrain 的链接面出现 testkit 即违约；测试目标
 * （_test/_contract_test）的报告设施消费是允许形态。
 */
TEST(DtBuildGraph, NoTestkitEdgeOnProductTarget_WP18T02_ACC1)
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
            = (line.find("sdurws_ird_drivetrain_test") != std::string::npos
               || line.find("sdurws_ird_drivetrain_contract_test") != std::string::npos)
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
 * 的 #include 指令行出现的单元段只允许 drivetrain 自身＋两条登记边单元
 * （与 BuildRedLineTest 的两边界白名单同判据；此处以"集合封闭"口径复核
 * ——两个独立实现的同界断言互为防线）。业务对端面契约（evidence 评估器
 * 注册/切片依赖声明 fake 套件）随 WP-18-T03 在测试面建立，不改产品
 * 编译边。
 */
TEST(DtBuildGraph, IncludeFaceMatchesRegisteredEdges_WP18T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto included = collectIncludedUnits();
    std::set<std::string> allowed = {"drivetrain"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(u);
    }
    for (const auto& unit : included) {
        EXPECT_NE(allowed.find(unit), allowed.end())
            << "产品面 include 越界单元（两条登记边外——R-1/R-2）: " << unit;
    }
}
