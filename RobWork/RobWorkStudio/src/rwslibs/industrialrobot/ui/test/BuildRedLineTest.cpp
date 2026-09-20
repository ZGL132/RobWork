/**
 * @file   BuildRedLineTest.cpp
 * @brief  ui 落位期构建红线用例组（UI-T02 契约 acceptance 2/5 的具名自证）
 *         ——O-31 include 面边界、R-4 前缀字面量、SA-16 QShortcut 作用域、
 *         R-3 例外登记文本核对。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T02.json acceptance 2（红线门禁含 ui 例外
 *     登记文本；ird_gates/静态扫描零命中：Widgets 范围、QShortcut 作用域、
 *     RobWork 名称前缀拼接/剥离）/ acceptance 5（O-31 处置：交付面仅含
 *     ARCH §3.5 表内边与 R-3 例外登记，不创建指向 project/evidence/
 *     execution/policy/runtime 的链接或 include）；P-UI-5 处置（CF-1 结论
 *     ——注册表类型归 ui 自有头，落位期零接口、无实现影响）；
 *   - units/ui.md §3.1（R-3 例外登记文本与依赖边）、§7.3（GlobalShortcutRegistry
 *     全局快捷键唯一注册点——SA-16）、§12.3 UI-HKY-3 行（QShortcut 作用域
 *     静态扫描口径）；
 *   - 需求 NFR-MNT-01（ui 例外登记）、NFR-MNT-07（静态扫描零命中）、ARC-02
 *     （单元边界）；红线 R-3/R-4；
 *   - 同构先例：execution/test/BuildRedLineTest.cpp（EX-T01 同款落位形态——
 *     运行期源码扫描，对工作目录零依赖，集成/冒烟两模式行为一致）。
 *
 * 运行期源码扫描说明：扫描对象＝源码树（经 IRD_UI_UNIT_ROOT 注入的
 * industrialrobot 根下的 ui/ 与其 cmake/ 目录），因此本用例对工作目录零
 * 依赖。ird_gates（WP-01-T01 构建期门禁）与本组用例互为两道防线：门禁做
 * 全仓静态扫描，本组用例做同口径的运行期常驻自证（"测试对扫描面的复核
 * 即该处置的具名证据"——EX-T01 P-EX-1 处置先例）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// ui 单元树根（industrialrobot 目录——IRD_UI_UNIT_ROOT 注入）。
/// lexically_normal 规范化消除注入值尾部的 ".." 段（编译定义形如
/// ".../industrialrobot/ui/.."）——错误信息中的路径可读、文件系统调用
/// 不依赖 ".." 的逐级解析。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_UI_UNIT_ROOT}.lexically_normal();
    return dir;
}

/// 递归收集 C++ 源/头文件（相对路径、已排序——失败信息确定性，NFR-COR-02 精神）。
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir)) { return files; }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp" || ext == ".ipp") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// 判断一行是否为 #include 预处理指令（跳过行首空白）。红线扫描只认指令行：
/// 注释散文中出现"sdurws/ird/core"等路径字样（如锚点翻译单元文件头的处置
/// 说明）不是 include 行为，按任意文本匹配会把注释误报为越界 include
/// （本项目注释密集且要求建立文档追溯——EX-T01 同款判别，PRJ-T01 实施期
/// 曾真实检出该误报）。
bool isIncludeDirective(const std::string& line)
{
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) { return false; }
    return line.compare(first, 8, "#include") == 0;
}

/// 剥离行注释与块注释（R-4 判定对象是"名称拼接/剥离的代码行为"，注释中的
/// 框架名字样属文档性提及——ird_gates.cmake 第 4c 步 F-011 消账同款口径，
/// 本地扫描与门禁保持一致，避免双口径漂移）。已知边界：字符串字面量内的
/// "http://" 会被行注释规则截断（当前仓库无此形态——门禁同款登记）。
std::string stripComments(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (inLineComment) {
            if (c == '\n') { inLineComment = false; out.push_back(c); }
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') { inBlockComment = false; ++i; out.push_back(' '); }
            continue;
        }
        if (inString) {
            out.push_back(c);
            if (c == '\\' && next != '\0') { out.push_back(next); ++i; }
            else if (c == '"') { inString = false; }
            continue;
        }
        if (c == '/' && next == '/') { inLineComment = true; continue; }
        if (c == '/' && next == '*') { inBlockComment = true; ++i; out.push_back(' '); continue; }
        if (c == '"') { inString = true; out.push_back(c); continue; }
        out.push_back(c);
    }
    return out;
}

/// 产品面文件全集＝单元树内 include/ 与 src/ 两子树的扫描结果之和（R-2
/// 纪律：产品面仅此两处——公共头在 include/、私有实现头在 src/，测试面
/// test/ 等不在扫描域；ird_gates 第 4 步扫描域同口径）。
/// 返回相对 ui/ 目录的路径（保留子树前缀——collectCppFiles 相对各自扫描
/// 根返回，直接重锚定会丢失 src/ 段导致读文件失败；UI-T02 首跑实测）。
/// 排序合并保证失败信息确定性（NFR-COR-02 精神）。
std::vector<fs::path> collectProductFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"include", "src"}) {
        const std::vector<fs::path> subFiles = collectCppFiles(unitRoot() / "ui" / sub);
        for (const auto& item : subFiles) {
            files.push_back(fs::path{sub} / item);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

/**
 * 锚定：源码树可达（公共头保留位 README＋src/ 锚点翻译单元——落位期形态）。
 *
 * "src/ 空起步"的既定先例形态＝仅含注释的锚点翻译单元（CORE-T01/POL-T01/
 * DIAG-T02/PRJ-T01/EX-T01 同款）：CMake 对零源码 STATIC 库在生成期即报
 * "No SOURCES given to target"（实测），锚点文件是"库可生成、无接口预建"
 * （NFR-MNT-04）的最小载体。
 */
TEST(UiBuild, SourceTreeReachable_UI_BUILD)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    ASSERT_TRUE(fs::exists(unitRoot())) << "IRD_UI_UNIT_ROOT 不存在";
    // 公共头保留位：ui.md §3.3 约定十三个公共头随 UI-T03+ 落地，当前仅有
    // README.md（任务卡指向 §13——UI-T02 复核，见留痕）——保留位存在性是
    // 后续头路径布局检查的前提。
    ASSERT_TRUE(fs::exists(unitRoot() / "ui" / "include" / "sdurws" / "ird"
                           / "ui" / "README.md"))
        << "公共头保留位缺失（README.md 应存在）";
    // src/ 空起步：UI-T02 产物形态要求锚点翻译单元存在（STATIC 库可生成的
    // 最小条件），实现文件随 UI-T03+ 填充——扫描非空即锚点在位。
    ASSERT_FALSE(collectCppFiles(unitRoot() / "ui" / "src").empty())
        << "src 扫描为空（Ui.cpp 锚点翻译单元应存在）";
}

/**
 * 落位形态核对（acceptance 1 具名自证）：sdurws_ird_ui 由 INTERFACE 占位
 * 升级 STATIC、父 CMakeLists 挂载、占位循环退位、C++17 显式设定。
 *
 * 以 CMakeLists 文本核对（构建图半区；运行期"库可链接"半区由本测试目标
 * 链接 sdurws_ird_ui 本身承载——PUBLIC 链接传染链同时把 core/diagnostics/Qt
 * 带入本可执行文件，任一动态库解析失败进程即无法启动）。
 */
TEST(UiBuild, PlacementStaticRegistration_UI_BUILD)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    // 父 CMakeLists 挂载点：add_subdirectory(ui)（契约 acceptance 1——
    // "新建 ird/ui/CMakeLists.txt 并在父 CMakeLists 注册"）。
    const std::string parent = readFile(unitRoot() / "CMakeLists.txt");
    ASSERT_NE(parent.find("add_subdirectory(ui)"), std::string::npos)
        << "父 CMakeLists 未注册 ui 子目录";
    // 占位循环退位：IRD_MODULES 集合不再含 ui（INTERFACE 占位循环创建的
    // sdurws_ird_ui 已被本单元真实库取代——同名目标重复定义会配置失败，
    // 该核对是"占位确实移除"的文本面证据）。
    const std::size_t modulesBegin = parent.find("set(IRD_MODULES");
    ASSERT_NE(modulesBegin, std::string::npos) << "父 CMakeLists 缺少 IRD_MODULES 集合";
    const std::size_t modulesEnd = parent.find(')', modulesBegin);
    ASSERT_NE(modulesEnd, std::string::npos);
    const std::string modules = parent.substr(modulesBegin, modulesEnd - modulesBegin);
    // 独立词匹配：避免误伤含 "ui" 子串的其它单元名（当前无，防御性书写）。
    ASSERT_EQ(modules.find(" ui"), std::string::npos)
        << "IRD_MODULES 仍含 ui（占位循环未退位）";
    // 本单元 CMake：STATIC 声明＋C++17（DTB §5.1——落位升级目标名不变、
    // 显式 CXX_STANDARD 17，不用 C++20）。
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    ASSERT_NE(cmake.find("add_library(sdurws_ird_ui STATIC"), std::string::npos)
        << "ui/CMakeLists.txt 未以 STATIC 声明 sdurws_ird_ui";
    ASSERT_NE(cmake.find("CXX_STANDARD 17"), std::string::npos)
        << "ui 目标未显式设定 C++17";
}

/**
 * R-3 例外承载面核对（acceptance 1/2 具名自证）：Qt Core/Gui/Widgets 以
 * 字面目标名出现在 sdurws_ird_ui 的链接声明中；ird_gates 白名单数据文件
 * 含 ui 例外登记文本。
 *
 * 为什么核对字面 Qt 目标名：ird_gates 对链接声明做文本级静态解析（变量
 * 形态会被判"未知目标族"），Qt 字面书写既是门禁可判定的前提，也是"例外
 * 范围仅 sdurws_ird_ui"（NFR-MNT-01）的可见承载——任何其他目标出现 Qt
 * 链接即门禁 R3 命中（构建失败）。
 */
TEST(UiBuild, QtLinkageLiteralAndExceptionText_UI_BUILD)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    // Qt 三件套字面核对（ui.md §3.1 "PUBLIC 链 … Qt Core/Gui/Widgets"）。
    ASSERT_NE(cmake.find("Qt6::Core"), std::string::npos) << "缺 Qt6::Core 链接";
    ASSERT_NE(cmake.find("Qt6::Gui"), std::string::npos) << "缺 Qt6::Gui 链接";
    ASSERT_NE(cmake.find("Qt6::Widgets"), std::string::npos) << "缺 Qt6::Widgets 链接";
    // 两条 §3.5 登记边同面核对（ui→core、ui→diagnostics）。
    ASSERT_NE(cmake.find("sdurws_ird_core"), std::string::npos) << "缺 core 登记边";
    ASSERT_NE(cmake.find("sdurws_ird_diagnostics"), std::string::npos)
        << "缺 diagnostics 登记边";
    // R-3 例外登记文本核对（acceptance 2："红线门禁含 ui 例外登记文本：R-3
    // 例外登记注明「仅 ui」"）。机器消费面＝cmake/ird_gates_whitelist.cmake
    // 的登记注释（DTB §4.5 人工登记册为权威、机器面随登记回填——白名单
    // 文件头约定）；本核对钉住登记文本在位且注明生效任务（WP-10-T02），
    // 精确例外范围（sdurws_ird_ui＋§3.3 ui 测试目标）的机器面条目回填
    // 登记 WP-01-T03（提交件 traceability/wp10-t02-gate-registrations.md）。
    const std::string whitelist = readFile(unitRoot() / "cmake" / "ird_gates_whitelist.cmake");
    ASSERT_NE(whitelist.find("ui 单元界面目标"), std::string::npos)
        << "白名单缺 ui R-3 例外登记文本";
    ASSERT_NE(whitelist.find("WP-10-T02 生效"), std::string::npos)
        << "R-3 例外登记文本未注明生效任务（WP-10-T02 生效）";
}

/**
 * O-31 处置自证（常驻守卫；acceptance 5 具名证据）：ui 产品面零越界
 * include——sdurws/ird/<unit> 形式的包含仅允许 core 与 diagnostics 两条
 * 登记边（ARCH §3.5 表内边）。
 *
 * 落位期公共头未产出（仅锚点翻译单元），本检查当前扫描面为空集＋锚点
 * 文件；UI-T03+ 公共头/实现落地后自动转正为全量 include 面边界守卫
 * （EX-T01 P-EX-1 同款"常驻自证"形态——测试对 include 面的扫描即该
 * 处置的具名证据）。对 project/evidence/execution/policy/runtime 的协作
 * 形态（接口依赖 vs 注入）归 O-31 架构所有者裁决，本用例只钉住"落位
 * 零越界"的现状口径，不构成 O-31 消账（消账权在 DTB §4 登记流程）。
 */
TEST(UiBuild, NoCrossUnitInclude_O31_UI_BUILD)
{
    IRD_TEST_INFO("NFR-MNT-01", {}, std::nullopt);
    // O-31 允许的 include 单元：core、diagnostics（ARCH §3.5 表内边）＋
    // ui 自身（单元内包含不受限）。
    const std::vector<std::string> kAllowedUnits = {"core", "diagnostics", "ui"};
    const auto files = collectProductFaceFiles();
    ASSERT_FALSE(files.empty()) << "ui 产品面扫描为空（锚点翻译单元应存在）";
    for (const auto& rel : files) {
        const std::string src = readFile(unitRoot() / "ui" / rel);
        ASSERT_FALSE(src.empty()) << "读取失败: " << rel.string();
        std::istringstream stream(src);
        std::string line;
        while (std::getline(stream, line)) {
            if (!isIncludeDirective(line)) { continue; }
            const std::size_t pos = line.find("sdurws/ird/");
            if (pos == std::string::npos) { continue; }
            const std::size_t unitBegin = pos + std::string("sdurws/ird/").size();
            const std::size_t unitEnd = line.find('/', unitBegin);
            ASSERT_NE(unitEnd, std::string::npos)
                << rel.string() << ": include 路径形态异常: " << line;
            const std::string unit = line.substr(unitBegin, unitEnd - unitBegin);
            // 越界判定：不在允许集合即 O-31 边界命中（构建面等价于 ird_gates
            // SUB/R-2 命中——acceptance 5 明文禁止的链接或 include）。
            const bool allowed = std::find(kAllowedUnits.begin(), kAllowedUnits.end(), unit)
                              != kAllowedUnits.end();
            EXPECT_TRUE(allowed)
                << rel.string() << ": O-31 越界 include（ui→" << unit
                << " 非表内边，acceptance 5 禁止）: " << line;
        }
    }
}

/**
 * R-4 静态扫描（acceptance 2 具名证据）：ui 产品面（注释剥离后）零
 * "RobWork" 字符串字面量——RobWork 名称前缀拼接/剥离的 ui 侧实现点为零
 * （ARC-04：名称语义归 runtime 的 IRuntimeNameResolver；NFR-MNT-07 静态
 * 扫描零命中；ui.md §3.1"显示名一律经 resolveObjectId 取 localName"）。
 * 判定口径与 ird_gates 第 4c 步一致（字面量级、注释剥离——注释中的框架名
 * 提及属文档而非行为）。
 */
TEST(UiBuild, NoRobWorkPrefixLiteral_R4_UI_BUILD)
{
    IRD_TEST_INFO("NFR-MNT-07", {}, std::nullopt);
    const auto files = collectProductFaceFiles();
    for (const auto& rel : files) {
        const std::string stripped = stripComments(readFile(unitRoot() / "ui" / rel));
        // 门禁同款模式：双引号字符串字面量内出现框架名即疑似前缀拼接/
        // 剥离（ARC-04 名称所有权——例外须 DTB §4.5 登记）。
        const std::regex literalPattern("\"[^\"]*RobWork[^\"]*\"");
        std::smatch match;
        EXPECT_FALSE(std::regex_search(stripped, match, literalPattern))
            << rel.string() << ": 发现 RobWork 字面量（疑前缀拼接/剥离，R-4）："
            << (match.ready() && !match.empty() ? match.str(0) : std::string{});
    }
}

/**
 * SA-16 QShortcut 作用域扫描（UI-T06 acceptance 2"实际 QShortcut 对象仅由
 * GlobalShortcutRegistry 创建"＋UI-HKY-3 具名自证；ui.md §12.3 UI-HKY-3/
 * §7.3 同口径）：ui 产品面（进而全产品——其余单元本就不许含 Widgets 控件
 * 面）的"QShortcut＋全局作用域"创建形态只允许出现在唯一注册点实现文件
 * src/GlobalShortcutRegistry.cpp（§7.3"实际 Qt 快捷键对象只由本注册点
 * 创建（QShortcut，WindowShortcut 上下文）"），其余任何文件命中即违例
 * （插件不得私占全局快捷键；ui 自身同口径自律）。
 *
 * 判定口径：注释剥离后扫描（R-4 用例同款——头文件的设计注释大量提及
 * "QShortcut/WindowShortcut"字样属文档性提及，不是创建行为；ird_gates
 * 第 4 系列同为注释剥离口径，避免双口径漂移）。正向控制：唯一豁免文件
 * 必须真实同时携带 QShortcut 与全局作用域字样（豁免面失效＝注册点漂移/
 * 被移除，同样失败——防止扫描条件被悄然掏空）。
 */
TEST(UiBuild, NoGlobalShortcutPrivatization_SA16_UI_BUILD)
{
    IRD_TEST_INFO("NFR-MNT-07", {}, std::nullopt);
    // 唯一注册点豁免面（§7.3——相对 ui/ 目录的路径字面）。
    const fs::path kUniqueRegistrationPoint = fs::path{"src"} / "GlobalShortcutRegistry.cpp";
    bool uniquePointHasQShortcut = false;
    bool uniquePointHasGlobalScope = false;

    const auto files = collectProductFaceFiles();
    for (const auto& rel : files) {
        // 注释剥离（R-4 同款 stripComments——判定对象是代码行为，注释散文
        // 中的字样属文档）。
        const std::string stripped = stripComments(readFile(unitRoot() / "ui" / rel));
        if (rel == kUniqueRegistrationPoint) {
            // 唯一创建点：采集正向控制面（两类字样分别登记，文件级核对）。
            if (stripped.find("QShortcut") != std::string::npos) {
                uniquePointHasQShortcut = true;
            }
            if (stripped.find("Qt::WindowShortcut") != std::string::npos) {
                uniquePointHasGlobalScope = true;
            }
            continue;
        }
        std::istringstream stream(stripped);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("QShortcut") == std::string::npos) { continue; }
            const bool globalScope = line.find("WindowShortcut") != std::string::npos
                                  || line.find("ApplicationShortcut") != std::string::npos;
            EXPECT_FALSE(globalScope)
                << rel.string()
                << ": QShortcut 全局作用域创建（SA-16/UI-HKY-3：全局快捷键唯一"
                   "注册点归 GlobalShortcutRegistry——豁免面仅 src/"
                   "GlobalShortcutRegistry.cpp，§7.3）";
        }
    }
    // 正向控制：唯一注册点必须真实携带"QShortcut＋全局作用域"创建形态。
    EXPECT_TRUE(uniquePointHasQShortcut && uniquePointHasGlobalScope)
        << "唯一创建点豁免面未携带 QShortcut＋全局作用域创建形态（注册点漂移"
           "或豁免面失效——核对 src/GlobalShortcutRegistry.cpp 的 attach 实现）";
}
