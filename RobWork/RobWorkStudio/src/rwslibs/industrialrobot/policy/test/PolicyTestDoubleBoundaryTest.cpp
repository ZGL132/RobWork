/**
 * @file   PolicyTestDoubleBoundaryTest.cpp
 * @brief  替身边界机检用例组（POL-T11/POL-TD-1）——替身只存在于测试面的
 *         源码扫描钉住＋边界声明两面在案断言（ev 前例 EV-REG-3 的
 *         ScriptedDoubleBoundary 同款双面机制；文书面＝test/README.md §1，
 *         替身头＝test/sdurws/ird/policy/testdouble/PolicyTestDoubles.hpp）。
 *
 * 设计依据：
 *   - units/policy.md §11 POL-TD-1 行（"测试文档显式声明替身输出不构成
 *     碰撞算法正确性证明"——本套件为该声明的机检面）＋§11 测试设施行
 *     （替身落位 policy/test/sdurws/ird/policy/testdouble/）；
 *   - 任务契约 tasks/foundation/POL-T11.json acceptance 2（POL-TD-1 边界
 *     声明——替身不冒充碰撞算法验证）；
 *   - 机制先例：evidence/test/EvaluatorPortSuiteTest.cpp 的
 *     ScriptedDoubleBoundary（产品源码替身符号零命中扫描＋README 声明
 *     在案断言——EV-T11 交付的失败能力自证面）。
 *
 * 为什么需要机检（文书面之外的第三道防线）：替身一旦被产品路径引用，
 * 其脚本输出就会以"测试形状"污染产品语义（POL-TD-1 边界失守的第一步）；
 * 源码扫描让"产品面出现替身符号"在构建期显性失败，而不是依赖 review
 * 记忆。扫描是文本级的（grep 语义）——它不阻止语义等价的新名字，但把
 * 既定替身符号的误用变成必检项（与 evidence 机检同强度）。
 *
 * 模式约束：纯 std 文件扫描（零 policy/rw 依赖）——冒烟＋集成两模式
 * 编译运行。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// 单元源码树根（CMake 注入——industrialrobot/policy/ 的上一级＝policy/）。
/// 与 sdurws_ird_policy_test 同款定义（BuildRedLineTest 的扫描输入同源）。
#ifndef IRD_POLICY_UNIT_ROOT
#error "IRD_POLICY_UNIT_ROOT 未定义（CMake target_compile_definitions 缺失）"
#endif

/// 递归收集 root 下指定扩展名的文件相对路径（稳定序——排序后输出）。
std::vector<std::filesystem::path>
collectFiles(const std::filesystem::path& root, const std::string& extension)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;   // 遍历错误（权限等）——已收集部分仍可断言，不静默吞
        }
        if (it->is_regular_file() && it->path().extension().string() == extension) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());   // 稳定序（NFR-COR-02 同源纪律）
    return out;
}

/// 文件全文读取（扫描输入；不可读返回空串——用例内断言拦截）。
std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 子串存在性（文本级扫描的原语——大小写敏感，替身符号按声明精确匹配）。
bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// 产品源码面（policy 单元的 src/＋include/——替身不得进入的两个目录）。
/// 注意 IRD_POLICY_UNIT_ROOT＝industrialrobot 根（与 _test 目标同源——
/// 编译定义为 "<policy 目录>/.."），policy 单元路径须带 "policy" 分量。
std::filesystem::path policyUnitRoot()
{
    return std::filesystem::path{IRD_POLICY_UNIT_ROOT} / "policy";
}

/// 规范替身头路径（§11 测试设施行的落位——存在性本身是契约面）。
std::filesystem::path doubleHeader()
{
    return policyUnitRoot() / "test" / "sdurws" / "ird" / "policy" / "testdouble"
        / "PolicyTestDoubles.hpp";
}

/// 测试面说明文档（POL-TD-1 文书面载体——README §1 全文声明）。
std::filesystem::path testReadme()
{
    return policyUnitRoot() / "test" / "README.md";
}

}  // namespace

// =====================================================================
// 机检 1：产品源码面替身符号零命中（POL-TD-1 的结构防线）。
// =====================================================================

/**
 * POL-TD-1（机检面①）：policy 产品源码（src/＋include/ 递归）中，规范
 * 替身符号零命中——ScriptedCollisionEvaluator/StubPolicyProvider（§11
 * 命名替身）、PolicyTestDoubles（替身头名）、testdouble 目录名、以及
 * TU 内脚本后端类名 ScriptedCollisionBackend/ScriptedDistanceBackend
 * 全部不得出现在产品面。任何命中＝替身进入产品路径＝边界失守，显性
 * 失败（对已关闭上下文的调用走异常 fail-fast 同款纪律——边界违约不是
 * 诊断轨问题，是结构性问题）。
 */
TEST(PolicyTestDoubleBoundary, ProductSourcesContainNoDoubleSymbols)
{
    // 扫描面：产品 .hpp/.cpp 全量（include/ 公共头＋src/ 实现头）。
    std::vector<std::filesystem::path> files = collectFiles(
        policyUnitRoot() / "include", ".hpp");
    for (const auto& f : collectFiles(policyUnitRoot() / "src", ".cpp")) {
        files.push_back(f);
    }
    for (const auto& f : collectFiles(policyUnitRoot() / "src", ".hpp")) {
        files.push_back(f);
    }
    ASSERT_FALSE(files.empty()) << "产品源码扫描面为空（IRD_POLICY_UNIT_ROOT 配置错误）";

    // 替身符号清单（§11 命名替身＋TU 内后端替身＋落位目录名——逐符号
    // 扫描全部文件；命中即列出定位信息）。
    static const char* kDoubleSymbols[] = {
        "ScriptedCollisionEvaluator",
        "StubPolicyProvider",
        "ScriptedCollisionBackend",
        "ScriptedDistanceBackend",
        "PolicyTestDoubles",
        "testdouble",
    };
    for (const auto& file : files) {
        const std::string text = readAll(file);
        ASSERT_FALSE(text.empty()) << "产品源码不可读: " << file.string();
        for (const char* symbol : kDoubleSymbols) {
            EXPECT_FALSE(contains(text, symbol))
                << "产品源码出现替身符号（POL-TD-1 边界失守）: " << symbol << " @ "
                << file.string();
        }
    }
}

// =====================================================================
// 机检 2：替身头边界声明在案（POL-TD-1 文书面的头侧载体）。
// =====================================================================

/**
 * POL-TD-1（机检面②）：规范替身头存在，且其文件头包含边界声明的关键
 * 语义串（"POL-TD-1"锚点＋"不构成任何碰撞算法正确性证明"核心命题＋
 * "not-a-real-backend"脚本描述符标注纪律）——声明不是可选注释，是
 * 替身交付物的一部分（缺失即验收 fail 项的机检化）。
 */
TEST(PolicyTestDoubleBoundary, DoubleHeaderCarriesBoundaryDeclaration)
{
    const std::string text = readAll(doubleHeader());
    ASSERT_FALSE(text.empty()) << "规范替身头不可读: " << doubleHeader().string();

    // 任务锚点（POL-TD-1 行）与核心命题（§11 POL-TD-1 语义原句关键串）。
    EXPECT_TRUE(contains(text, "POL-TD-1"))
        << "替身头缺 POL-TD-1 锚点（边界声明未随头交付）";
    EXPECT_TRUE(contains(text, "不构成任何碰撞算法正确性证明"))
        << "替身头缺核心边界命题（替身不冒充碰撞算法验证）";
    // 脚本描述符标注纪律（test. 前缀＋not-a-real-backend——替身数据不进
    // 产品复现要素主张的标注面）。
    EXPECT_TRUE(contains(text, "not-a-real-backend"))
        << "替身头缺脚本描述符标注纪律（not-a-real-backend）";
}

// =====================================================================
// 机检 3：测试文档声明在案（POL-TD-1 文书面的 README 载体）。
// =====================================================================

/**
 * POL-TD-1（机检面③）：test/README.md 存在且 §1 为替身边界声明全文
 * （含全部替身清单一并声明的措辞、真实算法正确性的两条独立通道——
 * POL-T07 解析算例＋pol-collision-analytic 数据集）。"随测试文档留痕"
 * 的验收语义由此钉住。
 */
TEST(PolicyTestDoubleBoundary, TestReadmeDeclaresBoundaryFullText)
{
    const std::string text = readAll(testReadme());
    ASSERT_FALSE(text.empty()) << "test/README.md 不可读: " << testReadme().string();

    EXPECT_TRUE(contains(text, "替身边界声明")) << "README 缺替身边界声明节";
    EXPECT_TRUE(contains(text, "不构成任何碰撞算法正确性证明"))
        << "README 缺核心边界命题全文";
    // 声明覆盖全部替身（§2 清单——新替身入册时本断言同步扩展）。
    EXPECT_TRUE(contains(text, "ScriptedCollisionEvaluator"))
        << "README 替身清单缺 ScriptedCollisionEvaluator";
    EXPECT_TRUE(contains(text, "StubPolicyProvider")) << "README 替身清单缺 StubPolicyProvider";
    // 真实正确性通道的指认（解析算例＋analytic-case 数据集——"替身不做
    // 什么"必须同时说清"由谁做什么"）。
    EXPECT_TRUE(contains(text, "pol-collision-analytic"))
        << "README 未指认真实正确性通道（analytic-case 数据集）";
}
