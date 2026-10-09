/**
 * @file   BuildGraphContractTest.cpp
 * @brief  dynamics 跨单元契约测试（落位期）——构建图边界契约（任务
 *         契约 WP-17-T02 acceptance 1/2 的具名自证面）。
 *
 * 设计依据：
 *   - units/dynamics.md §3.1（依赖形态——计算库 L2 零 Qt；接口依赖
 *     core/evidence/runtime/execution/project/diagnostics 公共头；R-1
 *     业务域互链禁止——trajectory/requirements/drivetrain/selection 零
 *     编译期依赖）、§3.2（目标布局——T02 落位边集＝五条 CMake 行登记边；
 *     L1 基线 sdurw_math/kinematics/models＋sdurwsim＋Eigen3）、§3.3
 *     （不创建 sdurws_ird_dynamics_worker——D-DYN-10，共享 worker 唯一）
 *   - cmake/ird_gates_whitelist.cmake 的 "dynamics->…" 五行（机器面
 *     同源数据——本测试以文本扫描单元 CMakeLists 复核同一事实，双面
 *     一致；五边随本任务登记 IRD_EXTRA_EDGE_REFS 并同步刷新
 *     dependency-graph.json）
 *   - 先例：selection/contract_test/BuildGraphContractTest.cpp（WP-19-T02
 *     同款——落位期跨单元边界证据由 CMakeLists 文本扫描＋配置期守卫＋
 *     双模式构建链接成功三面共同承载；drivetrain/workflow/kinematics
 *     更早先例）
 *   - 任务契约 tasks/foundation/WP-17-T02.json acceptance 1（STATIC 落位
 *     ＋_plugin/_test/_contract_test 创建）/acceptance 2（二分结构扫描
 *     通过——ird_gates 零命中的单元内复核面）
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

/// dynamics 单元树根（industrialrobot 目录——IRD_DYNAMICS_UNIT_ROOT
/// 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DYNAMICS_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "dynamics" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "dynamics/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 dynamics/CMakeLists.txt";
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 收集文本中全部 sdurws_ird_<unit>[<_role>] 目标名（先剥离 "#" 注释——
/// 注释文字如"不链 sdurws_ird_testkit"的处置说明不是构建图引用，参与扫描
/// 会误报）。目标名由单词边界分隔，逐字符扫描稳定实现（selection/
/// drivetrain/workflow/kinematics 同款）。
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
/// 字样不误报；selection/kinematics 同款）。
std::set<std::string> collectIncludedUnits()
{
    std::set<std::string> units;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "dynamics" / sub;
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

/// 五条登记边单元（卡 §3.2 CMake 行边集——与白名单 "dynamics->…" 五行
/// 同源；五边属 ARCH §3.5"各业务域单元→L2/L3 公共接口"许可方向的
/// 实例化〔O-07 的 dynamics 侧承接＝卡 §3.1/§3.2 明文 runtime 接口依赖
/// ——WP-17-T01 卡面登记〕，dependency-graph.json 随本任务同步刷新）。
constexpr const char* kEdgeUnits[] = {
    "core", "evidence", "runtime", "execution", "diagnostics",
};

/// 业务域单元清单（R-1 判定集合——whitelist IRD_BUSINESS_UNITS 及
/// ARCH §3.5；dynamics 自身在其内——业务域单元，故本表不含 dynamics：
/// 同单元插件自边由插件行另钉）。
constexpr const char* kBusinessUnits[] = {
    "modeling", "requirements", "kinematics", "trajectory",
    "selection", "optimization",
};

/// 表外平台单元（五条登记边之外的全部平台/编排单元——policy/io/project/
/// ui/reporting/workflow 均零编译边〔SUB 面〕；testkit 由 NoTestkitEdge
/// 用例单独钉住产品面）。
constexpr const char* kExtraPlatformUnits[] = {
    "policy", "io", "project", "ui", "reporting", "workflow",
};

}  // namespace

/**
 * 构建图封闭性：dynamics 的 CMake 目标引用集合仅含五条登记边＋本单元
 * 五目标（产品/插件/两测试/harness——T02 契约 acceptance 1 明文同批
 * 创建＋WP-17-T09 harness 载体；单元内部引用不构成跨单元边）＋testkit
 * （报告设施——T-1 允许形态，仅测试目标）。出现任何其他目标引用（业务
 * 域单元＝R-1 面；表外平台单元＝SUB 面；_worker＝卡 §3.3 明文不创建
 * ——如需走架构变更通道）即构建图越界（acceptance 2"二分结构扫描"
 * 的单元内复核面）。
 */
TEST(DynBuildGraph, UnitEdgesFiveRegisteredAndClosed_WP17T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02", "NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：五条登记边＋本单元五目标（产品/插件/测试/契约测试——T02
    // 契约 acceptance 1 明文同批创建；开发验证 harness _app——WP-17-T09
    // 单元卡 §11.5 GUI 手动验证通道载体，ird_gates 分类面 Qt 合法形态）
    // ＋sdurws_ird_testkit（报告设施——T-1 允许形态＝仅测试目标可链，
    // 产品目标由 NoTestkitEdge 用例钉住）。
    std::set<std::string> allowed = {"sdurws_ird_dynamics",
                                     "sdurws_ird_dynamics_plugin",
                                     "sdurws_ird_dynamics_test",
                                     "sdurws_ird_dynamics_contract_test",
                                     "sdurws_ird_dynamics_app",
                                     "sdurws_ird_testkit"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(std::string("sdurws_ird_") + u);
    }
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "dynamics 构建图出现白名单外目标引用（产品单元边仅五条登记"
               "边——dynamics.md §3.2 CMake 行、ARCH §3.5 业务域→L2/L3 "
               "公共接口；R-1/SUB/T-1）: " << ref;
    }
    // 目标形态（卡 §3.3——不设专属 worker 的差异登记；D-DYN-10）：
    // _worker 恒不存在——共享 worker（sdurws_ird_execution_worker）是
    // 全产品唯一工作进程，dynamics 评估器经 IEvaluatorFactory 链接进其
    // 装配清单；例外唯一通道＝卡 §3.3 触发重审条件＋架构变更登记。
    EXPECT_EQ(refs.find("sdurws_ird_dynamics_worker"), refs.end())
        << "dynamics 无 _worker 形态（卡 §3.3/D-DYN-10——不创建）";
    // _plugin 必须存在（正向断言在 PluginTargetPresent 用例）。
}

/**
 * 插件目标落位（acceptance 1——"_plugin/_test/_contract_test 目标创建"
 * 的存在性半区）：T02 契约明文随落位创建 sdurws_ird_dynamics_plugin
 * （最小可注册实现——卡 §9.5"插件零计算逻辑"红线由
 * DynamicsPluginAssemblyContractTest 钉住）——CMake 文本必须含其
 * add_library 声明（ADD_LIBRARY 目标形态），且引用集合含该目标。
 */
TEST(DynBuildGraph, PluginTargetPresent_WP17T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const std::string text = readCMakeLists();
    // 剥注释后核对 add_library 声明（注释中的目标名字样不是声明）。
    std::string code;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) { line.erase(hashPos); }
        code += line;
        code += '\n';
    }
    EXPECT_NE(code.find("add_library(sdurws_ird_dynamics_plugin"),
              std::string::npos)
        << "sdurws_ird_dynamics_plugin 缺 add_library 声明（T02 契约 "
           "acceptance 1——插件目标随落位创建）";

    const auto refs = collectTargetRefs(text);
    EXPECT_NE(refs.find("sdurws_ird_dynamics_plugin"), refs.end())
        << "dynamics 构建图缺插件目标引用（T02 契约 acceptance 1）";
}

/**
 * 五条登记边必须真实存在（acceptance 1——"STATIC（五条登记边）"的存在
 * 性半区）："封闭性"（第一用例）只证明没有多余边，不证明登记边已建立
 * ——CMake 文本中必须显式引用五个目标（target_link_libraries PUBLIC
 * 形态，卡 §3.2 白名单统一真名——集成/冒烟两模式可解析）。链接器半区
 * 由双模式构建与测试目标链接成功自证（构建日志＝执行证据）。
 */
TEST(DynBuildGraph, FiveRegisteredEdgesPresent_WP17T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto refs = collectTargetRefs(readCMakeLists());
    for (const char* u : kEdgeUnits) {
        EXPECT_NE(refs.find(std::string("sdurws_ird_") + u), refs.end())
            << "dynamics→" << u << " 登记边缺失（dynamics.md §3.2 CMake 行"
               "/ARCH §3.5 业务域→L2/L3 公共接口五条登记边）";
    }
}

/**
 * 业务域单元零互链＋零表外平台边（acceptance 2——R-1 具名自证）：其余
 * 六个业务域单元（R-1 判定集合；dynamics 自身是业务域单元故不在本表
 * ——同单元自边由插件行承载）不得出现在 dynamics 的构建图中——
 * trajectory 激励经 ITrajectorySourcePort 注入端口、requirements 工况经
 * 快照 objectClosure、drivetrain 映射唯一实现经③端口、selection 消费经
 * 归档结果＋JointSideSeriesPack（卡 §2.4 消费表；R-1：零编译期依赖），
 * 不落编译边；表外平台单元（policy/io/project/ui/reporting/workflow）
 * 同禁（SUB 面）。跨单元协作走六类端口——R-1 红线在 dynamics 侧的构建
 * 面形态（ird_gates 零新增命中互为机器证据）。零 sdurw_proximity 直链
 * （R-5/N3——碰撞零实现）由封闭性用例的白名单封闭承载（proximity 非
 * 目标引用面，ird_gates 第 2c 步专查）。
 */
TEST(DynBuildGraph, NoBusinessUnitOrExtraPlatformEdge_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{"AT-19"});

    const auto refs = collectTargetRefs(readCMakeLists());
    // R-1 面：其余业务域单元（dynamics 不直链 trajectory/requirements/
    // drivetrain/selection——卡 §3.1"业务域互链禁止（R-1）"原文；上游
    // 结果经注入端口、需求经快照对象，编译边若需建立须先经架构裁决并
    // 登记 dependency-graph，本任务不落）。
    for (const char* business : kBusinessUnits) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + business), refs.end())
            << "业务域互链禁止（R-1 无例外——SA-10；卡 §3.1）: "
               "dynamics→" << business;
    }
    // SUB 面：五条登记边之外的平台/注入列单元（policy＝AT-19 零碰撞零
    // policy 消费；project＝只读修订查询经装配面；ui/reporting/workflow/
    // io＝协作经端口——testkit 由 NoTestkitEdge 用例单独钉住产品面）。
    for (const char* platform : kExtraPlatformUnits) {
        EXPECT_EQ(refs.find(std::string("sdurws_ird_") + platform), refs.end())
            << "表外平台边（白名单外——构建失败面）: dynamics→" << platform;
    }
}

/**
 * testkit 仅落在测试目标链接语句（T-1 具名自证——selection/workflow
 * 同款收窄重述）：testkit 不随产品分发（testkit.md §2.4 T-1）——产品
 * 目标 sdurws_ird_dynamics（及插件目标）的链接面出现 testkit 即违约；
 * 测试目标（_test/_contract_test）的报告设施消费是允许形态。
 */
TEST(DynBuildGraph, NoTestkitEdgeOnProductTarget_WP17T02_ACC1)
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
            = (line.find("sdurws_ird_dynamics_test") != std::string::npos
               || line.find("sdurws_ird_dynamics_contract_test") != std::string::npos)
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
 * 的 #include 指令行出现的单元段只允许 dynamics 自身＋五条登记边单元
 * （与 BuildRedLineTest 的五边界白名单同判据；此处以"集合封闭"口径复核
 * ——两个独立实现的同界断言互为防线）。业务对端面契约（runtime 视图/
 * evidence 评估器注册/execution 任务通道 fake 套件）随 WP-17-T03+ 在
 * 测试面建立，不改产品编译边。
 */
TEST(DynBuildGraph, IncludeFaceMatchesRegisteredEdges_WP17T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    const auto included = collectIncludedUnits();
    std::set<std::string> allowed = {"dynamics"};
    for (const char* u : kEdgeUnits) {
        allowed.insert(u);
    }
    for (const auto& unit : included) {
        EXPECT_NE(allowed.find(unit), allowed.end())
            << "产品面 include 越界单元（五条登记边外——R-1/R-2）: " << unit;
    }
}
