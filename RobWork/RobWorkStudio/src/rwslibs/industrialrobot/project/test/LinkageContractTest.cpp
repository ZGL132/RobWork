/**
 * @file   LinkageContractTest.cpp
 * @brief  project 跨单元契约测试（落位期）——构建图边界契约（PRJ-TX-14
 *         后两项＋acceptance 2 逐项具名自证）。
 *
 * 设计依据：
 *   - units/project.md §3.3（目标 PUBLIC 链 core；`_contract_test`＝跨单元
 *     契约面：锁双实例、归档协作等——随 PRJ-T10/T14/T15 展开；落位期＝
 *     构建图边界）、§3.2（依赖图：diagnostics 边注入不落链接）、
 *     §11.2 PRJ-TX-14 行（构建红线后两项：CMake 无业务单元边、无 testkit
 *     边——脚本扫描、零命中）、§2.1 O-9/O-14 与 §10.4（io 边待裁决
 *     P-PR-5，阶段 A 零 io 编译期消费）；
 *   - 任务契约 tasks/foundation/PRJ-T01.json acceptance 1（PUBLIC 链 core）
 *     /acceptance 2（仅建立 project→core 编译链接边；diagnostics 边按注入
 *     形态不落链接，ird_gates 白名单预登记；零 io 编译期消费——O-09 处
 *     置：包导入导出/固化归阶段 B WP-04-T17/T18）/acceptance 5（P-PR-1）；
 *   - governance-log.md P-PR-1 行（处置约束："core 冻结时出 diff，project
 *     按影响面增量修订并留痕；不私改 core 语义"）。
 *
 * P-PR-1 处置说明（为什么本文件只做构建图扫描、不消费 core 头）：core.md
 * v0.1 仍为 Draft 未冻结，任务契约明文本任务"仅链接目标不消费头"——消费
 * 契约类型的行为级自证（diagnostics 落位期 PublicCoreExposure 同款）随
 * PRJ-T04（首个 core 头消费任务）建立，避免在 Draft 基线上扩大返工面。
 * 因此落位期的跨单元边界证据由三面共同承载：①本文件的 CMakeLists 文本
 * 扫描（目标引用集合＝构建图显式边）；②project/CMakeLists.txt 文件末尾
 * 配置期守卫（链接属性两面读取）；③双模式构建与测试链接成功（链接器
 * 实证 sdurws_ird_project 可解析、可被消费）。
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

/// project 单元树根（industrialrobot 目录——IRD_PROJECT_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_PROJECT_UNIT_ROOT};
    return dir;
}

/// 读取单元 CMakeLists.txt 全文；不存在/不可读显性失败（不留假阳性通道）。
std::string readCMakeLists()
{
    const auto cmakeFile = unitRoot() / "project" / "CMakeLists.txt";
    EXPECT_TRUE(fs::exists(cmakeFile)) << "project/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 project/CMakeLists.txt";
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

}  // namespace

/**
 * 链接图契约：project 的 CMake 目标引用集合仅含 core 一条产品单元边
 * （acceptance 2——ARCH §3.5 登记边，零表外同层边，SA-10）。
 *
 * 扫描单元 CMakeLists.txt 文本中出现的全部 sdurws_ird_* 目标引用，与白名单
 * 比对：产品目标链接 core（唯一单元边）＋本单元自身三目标（产品/测试/契约
 * 测试——同一单元内部引用不构成跨单元边，R-1 判定范围）＋testkit（PRJ-T15
 * 登记的 T-1 允许形态——**仅测试目标**消费 FaultInterceptor/TempDir/
 * TestProcessRunner 等；产品目标零 testkit 边由 NoTestkitEdge 用例单侧钉
 * 住）。出现任何其他产品单元目标（modeling/…/optimization 业务域，或
 * runtime/evidence 等平台单元）即构建图越界（R-1：业务域单元互链禁止
 * ——DTB §5.3③）。
 *
 * 白名单增量登记（PRJ-T15，2026-09-17）：按本文件 NoTestkitEdge 用例
 * v0 原文预告的路径执行——"契约目标消费随 PRJ-T15 登记后白名单同步增量
 * 修订（不可静默越界）"；本次修订伴随 PRJ-T15 契约测试落地同一提交，
 * 消费面登记于 project/CMakeLists.txt 两个测试目标的链接注释。
 */
TEST(ProjectLinkage, UnitEdgeOnlyCore_DT_BUILD_R1_R2)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：core（唯一允许的产品单元边）＋ project 本单元三目标（产品/
    // 测试/契约测试——单元内部引用不构成跨单元边）＋ testkit（PRJ-T15
    // 登记的仅测试目标消费面——T-1 允许形态）。
    const std::set<std::string> allowed = {
        "sdurws_ird_core",
        "sdurws_ird_project",
        "sdurws_ird_project_test",
        "sdurws_ird_project_contract_test",
        "sdurws_ird_testkit"};
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "project 构建图出现白名单外目标引用（产品单元边仅 project→core "
               "一条，ARCH §3.5；R-1/T-1）: " << ref;
    }
}

/**
 * T-1 红线具名自证（PRJ-T15 增量修订版）：**产品目标**零 testkit 边＋
 * 测试目标消费显式在案（PRJ-TX-14 第 4 项＋任务契约 PRJ-T15 acceptance 2
 * "testkit 消费边界用例化声明在案"）。
 *
 * testkit 是测试侧单元（不随产品分发）；T-1 红线规定产品目标不链 testkit。
 * PRJ-T15 起测试目标按 T-1 允许形态消费 testkit（FaultInterceptor 经
 * IFileOps 接缝／TempDir／TestProcessRunner／ContractCheck／RecordListener
 * ——D-10 形态）。本用例双侧钉住：①产品目标 sdurws_ird_project 的
 * target_link_libraries 块内零 sdurws_ird_testkit（红线面）；②两个测试
 * 目标的链接块内**必须**显式引用 sdurws_ird_testkit（消费面在案——防止
 * "白名单放行了却没人消费"的漂移，也与 §11 头注的 testkit 消费清单互证）。
 */

/// 截取 CMakeLists 中某 target_link_libraries(<target> ...) 命令的括号体。
/// 逐次出现地核对目标名边界（名字后不得紧跟字母/数字/下划线——防
/// "project" 前缀误配 "project_test"），命中首个真实命令即返回。
std::string linkBlockOf(const std::string& text, const std::string& target)
{
    const std::string needle = "target_link_libraries(" + target;
    const auto isNameChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };
    for (auto start = text.find(needle); start != std::string::npos;
         start = text.find(needle, start + 1)) {
        const auto open = text.find('(', start);
        // 名字边界核对：目标名后须为参数分隔符（空白/右括号）。
        const std::size_t nameEnd = open + 1 + target.size();
        if (nameEnd < text.size() && isNameChar(text[nameEnd])) {
            continue;  // 更长目标名的前缀碰撞——找下一次出现
        }
        std::size_t depth = 0;
        std::size_t i = open;
        for (; i < text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
            } else if (text[i] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        return text.substr(open + 1, i - open - 1);
    }
    return {};
}

TEST(ProjectLinkage, NoTestkitEdge_DT_BUILD_T1)
{
    const std::string cmake = readCMakeLists();
    // ① 红线面：产品目标的链接块零 testkit（T-1——产品分发面纯净）。
    // linkBlockOf 的名字边界核对已消除 project_test/contract_test 前缀碰撞。
    const std::string productBlock = linkBlockOf(cmake, "sdurws_ird_project");
    ASSERT_FALSE(productBlock.empty())
        << "未找到产品目标 target_link_libraries 块（扫描失效）";
    EXPECT_EQ(productBlock.find("sdurws_ird_testkit"), std::string::npos)
        << "产品目标出现 testkit 边（T-1 红线：sdurws_ird_project 不链 "
           "testkit——testkit.md §2.4）";
    // ② 消费面：两个测试目标显式登记 testkit 消费（PRJ-T15 acceptance 2
    // ——消费边界用例化声明在案；缺登记＝消费漂移，同样失败）。
    const std::string testBlock
        = linkBlockOf(cmake, "sdurws_ird_project_test");
    const std::string contractBlock
        = linkBlockOf(cmake, "sdurws_ird_project_contract_test");
    EXPECT_NE(testBlock.find("sdurws_ird_testkit"), std::string::npos)
        << "单元测试目标未登记 testkit 消费（PRJ-T15 acceptance 2——"
           "TempDir/IRD_TEST_INFO/IRD_EXPECT_* 接入面缺失）";
    EXPECT_NE(contractBlock.find("sdurws_ird_testkit"), std::string::npos)
        << "契约测试目标未登记 testkit 消费（PRJ-T15 acceptance 2——"
           "FaultInterceptor/TestProcessRunner/EventWatch 接入面缺失）";
}

/**
 * 唯一登记边必须真实存在：acceptance 1"PUBLIC 链 core"的构建图半区。
 *
 * "零表外边"（上一用例）只证明没有多余边，不证明登记边已建立——CMake
 * 文本中必须显式引用 sdurws_ird_core（target_link_libraries PUBLIC 形态，
 * §3.3 原文；别名形态 RWS::ird::core 在集成模式即其 ALIAS，CMake 侧取
 * 统一真名保证冒烟模式可解析——diagnostics 落位同款取舍）。链接器半区
 * 由双模式构建与测试目标链接成功自证（构建日志＝执行证据）。
 */
TEST(ProjectLinkage, CoreEdgePresent_DT_BUILD)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_NE(refs.find("sdurws_ird_core"), refs.end())
        << "project→core 登记边缺失（ARCH §3.5/§3.3 PUBLIC 链 core）";
}

/**
 * diagnostics 边注入不落链接（acceptance 2）。
 *
 * ARCH §3.5 登记行"project → core, diagnostics"中的 diagnostics 边按注入
 * 形态实现（IDiagnosticsSink 适配器，§3.2/§5.0——P-PR-6/P-EX-8 裁决链接
 * 形态前不落目标链接；ird_gates 白名单已预登记 project->diagnostics，但
 * 预登记≠要求链接）。本断言钉住"卡存在不自动转链接"的口径：CMake 构建
 * 图不得显式引用 sdurws_ird_diagnostics；PRJ-* 稳定码注册与两级日志的
 * 真实消费随 PRJ-T08+（打开/恢复诊断产出点）按裁决结论回填。
 */
TEST(ProjectLinkage, DiagnosticsEdgeInjectionNotLink_DT_BUILD)
{
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_EQ(refs.find("sdurws_ird_diagnostics"), refs.end())
        << "project→diagnostics 边按注入形态不落链接（§3.2/§5.0——链接形态"
           "归 P-PR-6/P-EX-8 裁决，不因卡存在自动转链接）";
}

/**
 * 零 io 编译期消费（acceptance 2——O-09 处置具名自证）。
 *
 * ARCH §3.5 未登记 project→io 边（表外同层边＝构建失败，P-PR-5 待裁决）；
 * 单元卡 §2.1 消费表 io 行明确"阶段 A 不需要"，包导入导出（PM-05）/外部
 * 资源固化（CON-03）的 io 能力消费归阶段 B WP-04-T17/T18，届时按处置条款
 * 回填、不私裁补边。本断言双向扫描：include 面（sdurws/ird/io 头）与构建
 * 图面（sdurws_ird_io 目标引用）都不得出现。
 */
TEST(ProjectLinkage, NoIoCompileTimeConsumption_DT_BUILD_O09)
{
    // include 面：include/＋src/ 全部 C++ 文件零 sdurws/ird/io 头。
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "project" / sub;
        std::error_code ec;
        ASSERT_TRUE(fs::exists(base)) << "扫描目录缺失: " << base.string();
        for (auto it = fs::recursive_directory_iterator(base, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) continue;
            const auto ext = it->path().extension().string();
            if (ext != ".hpp" && ext != ".h" && ext != ".cpp") continue;
            std::ifstream in(it->path(), std::ios::binary);
            ASSERT_TRUE(static_cast<bool>(in)) << "无法读取: " << it->path().string();
            const std::string text{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
            EXPECT_EQ(text.find("sdurws/ird/io"), std::string::npos)
                << "project 阶段 A 零 io 编译期消费（O-09 处置）: "
                << it->path().string();
        }
    }
    // 构建图面：CMakeLists 非注释行零 sdurws_ird_io 目标引用。
    const auto refs = collectTargetRefs(readCMakeLists());
    EXPECT_EQ(refs.find("sdurws_ird_io"), refs.end())
        << "project 落位期构建图不得出现 io 边（ARCH §3.5 未登记——P-PR-5，"
           "阶段 B WP-04-T17/T18 按处置条款回填）";
}
