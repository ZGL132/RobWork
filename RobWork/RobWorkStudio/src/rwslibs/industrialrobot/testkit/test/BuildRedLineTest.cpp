/**
 * @file   BuildRedLineTest.cpp
 * @brief  TK-BUILD 构建红线用例组（前两条）——testkit 的依赖边界自检（TK-T01）。
 *
 * 设计依据：
 *   - units/testkit.md §8 TK-BUILD 组前两用例："testkit 目标链接清单仅
 *     core＋std（无产品单元/无 Qt/无 gtest 库级链接）"与"T-1 门禁用例
 *     （产品目标零 testkit 边）"；TK-T01 验证方式列明"TK-BUILD 前两个用例"
 *   - units/testkit.md §2.4（测试侧红线 T-1/T-2）与 §3.5（零 Qt 纪律）；
 *     DTB §4.5 红线例外登记册"T-1/T-2 无例外"
 *   - 待裁决项 O-21（P-TK-4）：ARCH §3.5 依赖表未列 testkit，全仓依赖
 *     门禁数据源暂不含测试侧红线——T-1/T-2 在门禁建成（WP-01-T01）前
 *     仅靠本单元自律，本用例组即自律载体。
 *
 * 背景说明：与 core 的 BuildRedLineTest.cpp（CORE-T01 先例）同构的双层
 * 防线中的源码扫描层——链接级红线由 testkit/CMakeLists.txt 的配置期守卫
 * 断言（target_link_libraries 检查），本文件以文件扫描钉住 include 面。
 * 两层互补：配置期守卫拦"链接了不该链的目标"，源码扫描拦"包含了不该
 * 包含的头"（头包含不必经链接边即可引入编译依赖污染）。
 *
 * 实现方式：编译期由 testkit/CMakeLists.txt 注入 IRD_TESTKIT_UNIT_ROOT
 * （industrialrobot 目录绝对路径），用例在运行期以 std::filesystem 递归
 * 扫描。所有用例先断言扫描集非空（SourceTreeReachable 锚定）——防路径
 * 配错导致扫描空集合而恒真的假绿（验收协议 4.5 针对的风险）。
 */

#include <gtest/gtest.h>

#include <filesystem>  // C++17：源码树递归扫描
#include <algorithm>   // std::sort：扫描结果排序（确定性输出）
#include <cstring>     // std::strlen：include 前缀字面量长度
#include <fstream>
#include <sstream>     // istringstream：逐行定位违规行号
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// industrialrobot 根目录（由 testkit/CMakeLists.txt 以编译定义注入；
/// testkit 位于其下 testkit/ 子目录，T-1 用例需扫描全部兄弟单元）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_TESTKIT_UNIT_ROOT};
    return dir;
}

/// testkit 自身源码树根（产品面＝include/＋src/，不含 test/）。
const fs::path& testkitDir()
{
    static const fs::path dir = unitRoot() / "testkit";
    return dir;
}

/**
 * @brief 递归收集 dir 下全部 C++ 源/头文件（.hpp/.h/.cpp/.cc/.cxx）。
 *
 * 头与实现一并纳入：Qt 渗入与跨单元 include 在任何文件种类上出现都算
 * 违规。目录不存在时返回空集（由调用方用例断言"非空"暴露配置错误）。
 *
 * @param dir [in] 待扫描目录
 * @return 相对 dir 的已排序文件路径列表（排序保证失败信息确定可复现——
 *         NFR-COR-02 确定性精神：同一棵树每次扫描同一序列）
 */
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;  // 目录级错误不抛异常，交由用例断言暴露
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp" || ext == ".cc"
            || ext == ".cxx") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/**
 * @brief 读文本文件全文；无法读取时返回空串并附 gtest 非致命失败。
 *
 * 读失败的文件必须显性暴露而不是静默跳过（跳过会让"读不出来的违规
 * 文件"漏检——与 core 先例同一纪律）。
 */
std::string readFileOrFail(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取待扫描文件（权限/编码问题不得静默跳过）: "
                      << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/**
 * @brief 列出文件中全部 `#include <sdurws/ird/<unit>/...>` 的单元名。
 *
 * @param file [in] 待扫描文件
 * @param hits [out] 命中的 (行号, 单元名, 整行) 列表（顺序遍历，确定性）
 */
void collectCrossUnitIncludes(const fs::path& file,
                              std::vector<std::string>* hits)
{
    const std::string prefix = "sdurws/ird/";
    const auto text = readFileOrFail(file);
    std::istringstream lines(text);
    std::string line;
    int lineno = 0;
    while (std::getline(lines, line)) {
        ++lineno;
        // 尖括号与引号两种包含形式都要覆盖
        for (const auto* form : {"#include <", "#include \""}) {
            auto pos = line.find(form);
            while (pos != std::string::npos) {
                const auto arg = pos + std::strlen(form);
                if (line.compare(arg, prefix.size(), prefix) == 0) {
                    const auto unitEnd = line.find('/', arg + prefix.size());
                    const auto unit = line.substr(
                        arg + prefix.size(),
                        unitEnd - arg - prefix.size());
                    hits->push_back(std::to_string(lineno) + ":" + unit
                                    + " → " + line);
                }
                pos = line.find(form, pos + 1);
            }
        }
    }
}

}  // namespace

// =====================================================================
// 用例组命名：TkBuild.<检查项>_<红线/需求编号>——DTB §5.5 测试命名带
// 追溯字段；ird-test-report.json 按用例名承载该追溯。
// =====================================================================

/**
 * @brief TK-BUILD 前置锚定：源码树可达且扫描集非空。
 *
 * 追踪：TK-BUILD 组自检（验收协议 4.5 防恒真）。
 *
 * 若 IRD_TESTKIT_UNIT_ROOT 配错（扫描空目录/不存在目录），本用例先红。
 * 锚点①：testkit 产品面（include/＋src/）非空——TestPaths 两文件必在；
 * 锚点②：兄弟单元 core 的 src 非空（Core.cpp，CORE-T01 落位）——T-1
 * 用例的扫描对象必须真实存在，否则 T-1 扫描退化成空集恒真。
 */
TEST(TkBuild, SourceTreeReachable_TK_BUILD)
{
    ASSERT_TRUE(fs::exists(testkitDir()))
        << "IRD_TESTKIT_UNIT_ROOT 不存在: " << unitRoot().string();
    const auto ownInc = collectCppFiles(testkitDir() / "include");
    const auto ownSrc = collectCppFiles(testkitDir() / "src");
    ASSERT_FALSE(ownInc.empty())
        << "testkit/include 扫描为空——路径配置错误（TestPaths.hpp 应存在）";
    ASSERT_FALSE(ownSrc.empty())
        << "testkit/src 扫描为空——路径配置错误（TestPaths.cpp 应存在）";
    ASSERT_FALSE(collectCppFiles(unitRoot() / "core" / "src").empty())
        << "core/src 扫描为空——T-1 用例的兄弟单元锚点缺失（Core.cpp 应存在）";
}

/**
 * @brief TK-BUILD 用例一（T-2/R-3 源码面）：testkit 产品源码仅含 core
 * 与自身头，零 Qt。
 *
 * 追踪：units/testkit.md §2.4 T-2（testkit 只依赖 core＋标准库）、
 * §3.2 依赖图、NFR-MNT-01（零 Qt 纪律延伸至 testkit 本体）、DTB §4.5
 * （T-1/T-2 无例外）。
 *
 * TK-BUILD 用例一的原文是"链接清单仅 core＋std（无产品单元/无 Qt/无
 * gtest 库级链接）"——链接清单的机器断言在 testkit/CMakeLists.txt 配置
 * 期守卫（含 gtest 库级链接检查：库本体不链 gtest，宏只在消费方 TU 展开，
 * D-06）；本用例覆盖其源码投影：include 面出现的 sdurws/ird/* 只能是
 * core 与 testkit 自身，且零 Qt 头。两层合并构成用例一的完整证据。
 */
TEST(TkBuild, KitIncludesOnlyCoreAndSelf_T2_NFR_MNT_01_R3)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(testkitDir() / sub)) {
            const auto file = testkitDir() / sub / rel;
            const auto text = readFileOrFail(file);

            // ① 零 Qt（R-3 同款扫描：Qt 头一律大写 Q 开头）
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "testkit 本体禁含 Qt 头（T-2 纪律/"
                                  << "NFR-MNT-01）: " << file.string() << ":"
                                  << lineno << " → " << line;
                }
            }

            // ② 跨单元 include 白名单＝{core, testkit}
            std::vector<std::string> hits;
            collectCrossUnitIncludes(file, &hits);
            for (const auto& hit : hits) {
                // hit 形如 "<行号>:<单元名> → <整行>"
                const auto unitSep = hit.find(':');
                const auto unitEnd = hit.find(' ', unitSep + 1);
                const auto unit = hit.substr(unitSep + 1,
                                             unitEnd - unitSep - 1);
                if (unit != "core" && unit != "testkit") {
                    ADD_FAILURE() << "testkit 仅可依赖 core（T-2），发现"
                                  << "其他单元头: " << file.string() << ":"
                                  << hit;
                }
            }
        }
    }
}

/**
 * @brief TK-BUILD 用例二（T-1 源码面）：全部产品单元源码零 testkit 头。
 *
 * 追踪：units/testkit.md §2.4 T-1（产品目标不得链接/包含 testkit——
 * "不随产品分发"的分发红线）、ARCHITECTURE §3.1（testkit 行）、
 * O-21（P-TK-4：全仓门禁建成前的 testkit 侧自律）。
 *
 * 扫描范围＝industrialrobot 下 20 个单元目录各自的 include/＋src/
 * （**不含各单元 test/**：测试目标允许消费 testkit，被禁的是产品面；
 * testkit 自身目录排除——它含天经地义的自引用）。当前多数单元为空占位
 * （仅 README），扫描集以 core 的产品文件为非空锚（见 SourceTreeReachable）；
 * 各单元随任务落地后本用例自动扩大覆盖，无需修改。
 */
TEST(TkBuild, ProductSourcesZeroTestkitInclude_T1)
{
    std::error_code ec;
    for (auto it = fs::directory_iterator(unitRoot(), ec);
         it != fs::directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_directory(ec)) continue;
        const auto unitName = it->path().filename().string();
        // testkit 自身排除；patches 等非单元目录无 include/src，自然空扫
        if (unitName == "testkit") continue;

        for (const auto* sub : {"include", "src"}) {
            const auto subDir = it->path() / sub;
            if (!fs::exists(subDir)) continue;  // 占位单元无源码目录内容
            for (const auto& rel : collectCppFiles(subDir)) {
                const auto file = subDir / rel;
                std::vector<std::string> hits;
                collectCrossUnitIncludes(file, &hits);
                for (const auto& hit : hits) {
                    const auto unitSep = hit.find(':');
                    const auto unitEnd = hit.find(' ', unitSep + 1);
                    const auto unit = hit.substr(unitSep + 1,
                                                 unitEnd - unitSep - 1);
                    if (unit == "testkit") {
                        ADD_FAILURE() << "产品单元禁含 testkit 头（T-1："
                                      << "testkit 不随产品分发）: "
                                      << file.string() << ":" << hit;
                    }
                }
            }
        }
    }
}
