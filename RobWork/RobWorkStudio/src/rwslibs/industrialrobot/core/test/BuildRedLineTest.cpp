/**
 * @file   BuildRedLineTest.cpp
 * @brief  UT-BUILD 构建红线用例组——core 单元的零依赖门禁自检（CORE-T01）。
 *
 * 设计依据：
 *   - units/core.md §8 测试组 UT-BUILD（构建红线）：core 目标无 Qt、无私有头
 *     出 include/、CMake 依赖图无 industrialrobot 其他单元边。
 *   - 需求 NFR-MNT-01（L2 计算内核零 Qt）、NFR-MNT-02（依赖边界可检查）、
 *     架构红线 R-1/R-2/R-3（AGENTS.md §5；ARCHITECTURE §3.2/§3.5）。
 *   - 任务契约 tasks/foundation/CORE-T01.json acceptance：产品目标零 testkit
 *     链接、零跨单元私有头。
 *
 * 背景说明：全仓正式依赖门禁（依赖图白名单比对）归 WP-01-T01 的 ird_gates；
 * 本用例组是 core 侧的"第一道防线"，在单元测试层面用文件扫描钉住三条
 * 源码级红线（Qt 渗入 / 跨单元头 / 公共头路径布局）。链接级红线由
 * core/CMakeLists.txt 的配置期守卫断言（target_link_libraries 检查），
 * 两层互补。
 *
 * 实现方式：编译期由 CMake 注入 IRD_CORE_SOURCE_DIR（core 源码树绝对路径），
 * 用例在运行期以 std::filesystem 递归扫描源码树逐文件核对。所有用例都
 * 先断言扫描集非空（SourceTreeReachable 锚定）——防止路径配错导致扫描
 * 空集合而恒真的假绿（验收协议 4.5"测试真实失败能力"针对的正是这类风险）。
 */

#include <gtest/gtest.h>

#include <filesystem>  // C++17：源码树递归扫描（CORE-T01 验证 C++17 可用的载体之一）
#include <algorithm>   // std::sort：扫描结果排序，保证失败信息确定可复现
#include <cstring>     // std::strlen：包含前缀字面量长度
#include <fstream>
#include <sstream>     // istringstream：逐行定位违规行号
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// 源码树根（由 core/CMakeLists.txt 以编译定义注入，避免依赖工作目录）。
const fs::path& coreSourceDir()
{
    static const fs::path dir = fs::path{IRD_CORE_SOURCE_DIR};
    return dir;
}

/**
 * @brief 递归收集 dir 下全部 C++ 源/头文件路径（.hpp/.h/.cpp/.cc/.cxx）。
 *
 * 扫描红线时把头与实现一并纳入：Qt 渗入与跨单元 include 在任何文件种类上
 * 出现都算违规。目录不存在时返回空集（由调用方用例断言"非空"暴露配置错误）。
 *
 * @param dir [in] 待扫描目录（源码树子目录）
 * @return 相对 dir 的文件路径列表（已排序，保证失败信息确定可复现——
 *         NFR-COR-02 确定性精神：同一棵树每次扫描输出同一序列）
 */
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;  // 目录级错误（如不存在）不抛异常，交由用例断言暴露
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
 * 红线扫描只需要子串匹配，不解析语法；读失败的文件必须显性暴露而不是
 * 静默跳过（跳过会让"读不出来的违规文件"漏检）。
 *
 * @param file [in] 绝对路径
 * @return 文件全部字节按文本解读的串
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

}  // namespace

// =====================================================================
// 用例组命名：BuildRedLine.<检查项>_<红线/需求编号>——DTB §5.5 要求测试
// 命名带需求/AT 追溯字段，ird-test-report.json 按用例名承载该追溯。
// =====================================================================

/**
 * @brief UT-BUILD 前置锚定：源码树可达且扫描集非空。
 *
 * 追踪：UT-BUILD 组自检（验收协议 4.5 防恒真）。
 *
 * 后续所有扫描用例都以本用例锚定的目录为输入；若 IRD_CORE_SOURCE_DIR
 * 配错（扫描到空目录/不存在的目录），本用例先红——避免空集扫描恒真。
 */
TEST(BuildRedLine, SourceTreeReachable_UT_BUILD)
{
    ASSERT_TRUE(fs::exists(coreSourceDir()))
        << "IRD_CORE_SOURCE_DIR 不存在: " << coreSourceDir().string();
    // 产品实现占位文件是已知锚点：它必须在扫描集内（core/src 随 CORE-T01 建立）
    const auto srcFiles = collectCppFiles(coreSourceDir() / "src");
    ASSERT_FALSE(srcFiles.empty())
        << "core/src 扫描为空——路径配置错误（占位实现 Core.cpp 应存在）";
}

/**
 * @brief R-3/NFR-MNT-01：core 产品源码零 Qt 头包含。
 *
 * 追踪：NFR-MNT-01（L2 计算内核零 Qt）、ARCHITECTURE §3.2 红线 R-3。
 *
 * 扫描范围＝产品编译输入（include/ 公共头＋src/ 实现），不含 test/：
 * 红线约束的是产品目标 sdurws_ird_core 的编译边界。Qt 头命名一律以大写 Q
 * 开头（QWidget、QtCore/...），匹配 `#include <Q` 与 `#include "Q` 即覆盖；
 * C++ 标准库与 rw::math 头均不以大写 Q 开头，无误报面。
 */
TEST(BuildRedLine, NoQtInclude_NFR_MNT_01_R3)
{
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(coreSourceDir() / sub)) {
            const auto file = coreSourceDir() / sub / rel;
            const auto text = readFileOrFail(file);
            // 逐行定位，失败信息可指向具体行（扫描而非解析：include 只要
            // 出现该前缀即违规，无合法场景）
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (line.find("#include <Q") != std::string::npos
                    || line.find("#include \"Q") != std::string::npos) {
                    ADD_FAILURE() << "core 禁含 Qt 头（R-3/NFR-MNT-01）: "
                                  << file.string() << ":" << lineno << " → "
                                  << line;
                }
            }
        }
    }
}

/**
 * @brief R-1/R-2 core 侧：core 源码零跨单元 include（含 testkit，覆盖 T-1 源码面）。
 *
 * 追踪：ARCHITECTURE §3.5（core 零 industrialrobot 单元依赖）、AGENTS.md §5
 * 红线 2/3；任务契约 acceptance"零跨单元私有头、零 testkit 链接"。
 *
 * core 是"任何层都可依赖"的零依赖内核：出现 `#include <sdurws/ird/<其他单元>`
 * 即越界（`sdurws/ird/core/` 自身公共头除外）。链接级的 testkit 红线由
 * core/CMakeLists.txt 配置期守卫断言，本用例覆盖源码 include 面，两层互补。
 */
TEST(BuildRedLine, NoCrossUnitInclude_R1_R2_T1)
{
    const std::string crossUnitPrefix = "sdurws/ird/";
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(coreSourceDir() / sub)) {
            const auto file = coreSourceDir() / sub / rel;
            const auto text = readFileOrFail(file);
            // 同时匹配尖括号与引号两种包含形式；命中点向后取单元名段比对
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                for (const auto* form : {"#include <", "#include \""}) {
                    auto pos = line.find(form);
                    while (pos != std::string::npos) {
                        const auto arg = pos + std::strlen(form);
                        if (line.compare(arg, crossUnitPrefix.size(),
                                         crossUnitPrefix)
                            == 0) {
                            // 单元名＝前缀后到下一个 '/' 的段；core 自身合法
                            const auto unitEnd =
                                line.find('/', arg + crossUnitPrefix.size());
                            const auto unit =
                                line.substr(arg + crossUnitPrefix.size(),
                                            unitEnd - arg
                                                - crossUnitPrefix.size());
                            if (unit != "core") {
                                ADD_FAILURE()
                                    << "core 不得包含 industrialrobot 其他"
                                    << "单元头（R-1/R-2，testkit 亦禁止，T-1）: "
                                    << file.string() << ":" << lineno << " → "
                                    << line << "（单元: " << unit << "）";
                            }
                        }
                        pos = line.find(form, pos + 1);
                    }
                }
            }
        }
    }
}

/**
 * @brief R-2：公共头只存在于规范路径 include/sdurws/ird/core/ 之下。
 *
 * 追踪：units/core.md §3.3（头包含形式 `#include <sdurws/ird/core/…>`）。
 *
 * 约束两层含义：① 公共头不散落在 include/ 根或其他层级（保证包含形式与
 * 命名空间一致，防"顺手加头"）；② 实现细节头不得放进 include/（私有实现
 * 头只能留在 src/）——本用例以"include/ 下一切头文件必须在
 * sdurws/ird/core/ 前缀下"同时钉住两点。
 */
TEST(BuildRedLine, PublicHeaderPathLayout_R2)
{
    for (const auto& rel :
         collectCppFiles(coreSourceDir() / "include")) {
        // generic_string() 统一为 '/' 分隔，规避 Windows 路径分隔符差异
        const auto generic = rel.generic_string();
        const bool ok = generic.rfind("sdurws/ird/core/", 0) == 0;
        if (!ok) {
            ADD_FAILURE()
                << "core 公共头必须位于 include/sdurws/ird/core/ 之下"
                << "（R-2：私有实现头不得置于 include/；包含形式见 units/"
                << "core.md §3.3）: " << generic;
        }
    }
}
