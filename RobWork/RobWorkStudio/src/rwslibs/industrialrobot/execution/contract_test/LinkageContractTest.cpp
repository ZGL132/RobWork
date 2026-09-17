/**
 * @file   LinkageContractTest.cpp
 * @brief  execution 跨单元契约测试（落位期）——构建图边界契约（EX-BLD-1
 *         后两项＋契约 acceptance 1/2 逐项具名自证）。
 *
 * 设计依据：
 *   - units/execution.md §3.4（`_contract_test`＝跨单元契约面：与 evidence
 *     校验器、与 project 归档端口、真实 worker 进程场景——随 EX-T04/T06/
 *     T09 展开；落位期＝构建图边界；diagnostics 边"当前经注入实现、目标
 *     暂不链接——链接形态按 P-EX-8 裁决"）、§3.2（依赖图四条登记边与
 *     注入形态登记）、§3.3（runtime/policy/diagnostics 注入边界——依赖
 *     白名单的实施形态）、§11 EX-BLD-1（依赖图仅四条登记边〔三链接＋一
 *     注入〕、产品目标零 testkit）、§15.1 D-17（runtime/policy 经三注入
 *     接口消费——ARCH §3.5 白名单硬约束，表外边＝构建失败）；
 *   - 任务契约 tasks/foundation/EX-T01.json acceptance 1（PUBLIC 链
 *     core/evidence/project——§3.4；diagnostics 边已登记但当前经注入实现、
 *     目标暂不链接——P-EX-8）/acceptance 2（仅 execution→core/evidence/
 *     project 三条 ARCH §3.5 登记边；runtime/policy 经 §3.3 最小接口注入
 *     消费零编译依赖，不私建表外边——P-EX-3 处置）；
 *   - 先例：project/test/LinkageContractTest.cpp（PRJ-T01 同款落位形态，
 *     白名单按 execution 卡三条登记边裁剪）。
 *
 * 落位期边界证据由三面共同承载（PRJ-T01 同款）：①本文件的 CMakeLists
 * 文本扫描（目标引用集合＝构建图显式边）；②execution/CMakeLists.txt
 * 文件末尾配置期守卫（链接属性两面读取）；③双模式构建与测试链接成功
 * （链接器实证 sdurws_ird_execution 可解析、可被消费——三条 PUBLIC 边
 * 的传染链随测试目标自动到达）。
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

/// execution 单元树根（industrialrobot 目录——IRD_EXECUTION_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_EXECUTION_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "execution" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "execution/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 execution/CMakeLists.txt";
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

/// include 面扫描：判断单元源码树（include/＋src/）是否存在包含指定单元
/// 路径段的 #include 指令行（注释散文不误报——只认指令行，与 _test 目标
/// 的扫描器同语义）。
bool hasIncludeOfUnit(const std::string& unitPath)
{
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "execution" / sub;
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
            std::istringstream lines(
                std::string{std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>()});
            std::string line;
            while (std::getline(lines, line)) {
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos
                    || line.compare(first, 8, "#include") != 0) {
                    continue;
                }
                if (line.find(unitPath) != std::string::npos) { return true; }
            }
        }
    }
    return false;
}

}  // namespace

/**
 * 链接图契约：execution 的 CMake 目标引用集合仅含 core/evidence/project
 * 三条产品单元边（acceptance 1/2——ARCH §3.5 登记边，零表外同层边，SA-10）。
 *
 * 扫描单元 CMakeLists.txt 文本中出现的全部 sdurws_ird_* 目标引用，与白名单
 * 比对：产品目标链接 core/evidence/project（三条登记边）＋本单元自身三目标
 * （产品/测试/契约测试——同一单元内部引用不构成跨单元边，R-1 判定范围）。
 * 出现任何其他产品单元目标（modeling/requirements/kinematics/trajectory/
 * dynamics/selection/optimization 等业务域，或 runtime/policy/diagnostics/
 * testkit）即构建图越界（R-1：业务域互链禁止；SA-10：表外边＝构建失败）。
 *
 * 与 project 落位期白名单的差异：execution 是三条登记链接边（project 一条
 * ＋diagnostics 注入不落链接——P-EX-8 口径与本文件 DiagnosticsEdge 注入用
 * 例一致）；testkit 条目缺位＝落位期零 testkit 消费（NoTestkitEdge 用例
 * 单独钉住；EX-T09 报告设施接入时按 T-1 允许形态登记并同步本白名单）。
 */
TEST(ExecutionLinkage, UnitEdgesRegisteredOnly_DT_BUILD_R1_T1)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：core/evidence/project（三条允许的产品单元边）＋ execution
    // 本单元三目标（产品/测试/契约测试——单元内部引用不构成跨单元边）。
    const std::set<std::string> allowed = {
        "sdurws_ird_core",
        "sdurws_ird_evidence",
        "sdurws_ird_project",
        "sdurws_ird_execution",
        "sdurws_ird_execution_test",
        "sdurws_ird_execution_contract_test"};
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "execution 构建图出现白名单外目标引用（产品单元边仅 "
               "execution→core/evidence/project 三条，ARCH §3.5；R-1/T-1/"
               "P-EX-3）: " << ref;
    }
}

/**
 * T-1 红线具名自证：execution 落位期构建图零 testkit 边（EX-BLD-1 第 4 项）。
 *
 * testkit 是测试侧单元（不随产品分发）；T-1 红线规定产品目标不链 testkit。
 * 落位期更进一步：连 `_contract_test` 也不消费 testkit（FaultInterceptor/
 * TempDir/TestProcessRunner 等消费随 EX-T09 契约套件按 T-1 允许形态
 * `sdurws_ird_execution_contract_test → {被测产品目标, sdurws_ird_testkit,
 * gtest}` 登记）——本断言确保登记 testkit 前本文件白名单扫描先行为零，
 * 届时增链必须伴随本用例白名单的显式增量修订（不可静默越界）。
 */
TEST(ExecutionLinkage, NoTestkitEdge_DT_BUILD_T1)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_EQ(refs.find("sdurws_ird_testkit"), refs.end())
        << "execution 落位期构建图不得出现 testkit 边（T-1：产品目标不链 "
           "testkit；契约目标消费随 EX-T09 登记后白名单同步增量修订）";
}

/**
 * 三条登记边必须真实存在：acceptance 1"PUBLIC 链 core/evidence/project"
 * 的构建图半区。
 *
 * "零表外边"（UnitEdgesRegisteredOnly 用例）只证明没有多余边，不证明登记
 * 边已建立——CMake 文本中必须显式引用三个目标（target_link_libraries
 * PUBLIC 形态，§3.4 原文；别名形态 RWS::ird::* 在集成模式即其 ALIAS，
 * CMake 侧取统一真名保证冒烟模式可解析——project 落位同款取舍）。链接器
 * 半区由双模式构建与测试目标链接成功自证（构建日志＝执行证据）。
 */
TEST(ExecutionLinkage, RegisteredEdgesPresent_DT_BUILD)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_NE(refs.find("sdurws_ird_core"), refs.end())
        << "execution→core 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链）";
    EXPECT_NE(refs.find("sdurws_ird_evidence"), refs.end())
        << "execution→evidence 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链）";
    EXPECT_NE(refs.find("sdurws_ird_project"), refs.end())
        << "execution→project 登记边缺失（ARCH §3.5/§3.4 PUBLIC 链——归档"
           "端口与存储上下文）";
}

/**
 * diagnostics 边注入不落链接（acceptance 1——P-EX-8 处置具名自证）。
 *
 * ARCH §3.5 登记行"execution → core, evidence, diagnostics, project"中的
 * diagnostics 边已随 diagnostics.md 产出收编 EX-* 18 项稳定码（diagnostics.md
 * §4.6），但执行面当前经 IExecutionDiagnosticsSink 注入实现（§3.3——对齐
 * project §5.0 IDiagnosticsSink 形态），目标暂不链接：链接形态按 P-EX-8
 * 裁决，不因 diagnostics.md 已产出而自动消账（ird_gates 白名单预登记
 * execution->diagnostics 不构成链接义务；同边在 io/ui/reporting 侧为直接
 * 链接，口径差异已登记）。本断言钉住"卡存在不自动转链接"的口径：CMake
 * 构建图不得显式引用 sdurws_ird_diagnostics；裁决结论落地时回填并同步本
 * 用例（届时白名单扫描放行该边）。
 */
TEST(ExecutionLinkage, DiagnosticsEdgeInjectionNotLink_DT_BUILD_PEX8)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_EQ(refs.find("sdurws_ird_diagnostics"), refs.end())
        << "execution→diagnostics 边按注入形态不落链接（§3.3——链接形态归 "
           "P-EX-8 裁决，不因 diagnostics.md 已产出自动转链接）";
}

/**
 * P-EX-3 具名自证：runtime/policy 注入零编译依赖（构建图面＋include 面
 * 双向扫描）。
 *
 * 任务指令与 ARCH §3.5 的出入（本卡 §15.3 P-EX-3 登记）：ARCH §3.5 实际
 * 登记 execution 四条边（runtime/policy 不在表内），execution 经 §3.3 最小
 * 注入接口消费 runtime 能力（适配器归 L5）、经快照 PolicyRef 值传递消费
 * policy（CON-06）。本断言双向钉住：构建图面（CMake 目标引用）与 include
 * 面（源码 #include 指令）都不得出现 runtime/policy——出现即私建表外边
 * （SA-10 表外边＝构建失败）；如架构侧补登 execution→runtime 边，§3.3
 * 注入层可原样退役为直连（D-17——接口形状不变），届时本用例随单元卡
 * 增量修订同步放行。
 */
TEST(ExecutionLinkage, RuntimePolicyInjectionNotLink_DT_BUILD_PEX3)
{
    // 构建图面：CMakeLists 非注释行零 runtime/policy 目标引用。
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_EQ(refs.find("sdurws_ird_runtime"), refs.end())
        << "execution 构建图不得出现 runtime 边（ARCH §3.5 未登记——P-EX-3："
           "经 §3.3 IExecutionModelService/ICompileCacheJudge/"
           "INameResolverAdapter 注入消费，适配器归 L5）";
    EXPECT_EQ(refs.find("sdurws_ird_policy"), refs.end())
        << "execution 构建图不得出现 policy 边（ARCH §3.5 未登记——policy 经"
           "快照 PolicyRef 值传递消费，CON-06）";
    // include 面：源码 #include 指令零 sdurws/ird/runtime、sdurws/ird/policy。
    EXPECT_FALSE(hasIncludeOfUnit("sdurws/ird/runtime"))
        << "execution 零 runtime 编译依赖（P-EX-3 处置——include 面命中即"
           "私建表外边）";
    EXPECT_FALSE(hasIncludeOfUnit("sdurws/ird/policy"))
        << "execution 零 policy 编译依赖（P-EX-3 处置——include 面命中即"
           "私建表外边）";
}
