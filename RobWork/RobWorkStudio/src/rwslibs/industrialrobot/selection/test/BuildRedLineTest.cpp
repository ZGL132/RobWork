/**
 * @file   BuildRedLineTest.cpp
 * @brief  selection 产品面源码红线运行期扫描用例组（SelBuildRedLine）——
 *         零 Qt（R-3/NFR-MNT-01）与 include 面限于两条登记边（R-1/R-2/
 *         卡 §3.2；任务契约 WP-19-T02 acceptance 1/2 的运行期自证面）。
 *
 * 设计依据：
 *   - units/selection.md §2.3（L4 业务域二分结构——sdurws_ird_selection
 *     计算库零 Qt）、§3.2（依赖关系表——编译链接边仅 core＋evidence 两条；
 *     policy/io/project/diagnostics/ui/runtime/drivetrain 七单元列"运行
 *     时注入/端口"列明文不落编译链接边；业务域互链零编译边——R-1）、
 *     §14.0（通用约定——零 Qt 含 Core、只 include core/evidence 公共头、
 *     不暴露 QWidget）
 *   - 需求 NFR-MNT-01（计算内核零 Qt、可由模型测试直接调用）、ARC-02
 *     （R-2 跨单元只经公共头）、ARC-04/NFR-MNT-07（R-4 名称前缀拼接/
 *     剥离禁止）
 *   - 先例：drivetrain/test/BuildRedLineTest.cpp（WP-18-T02 同款——运行
 *     期扫描与 CMakeLists 配置期守卫互为两道防线；kinematics/workflow
 *     更早先例）
 *   - 任务契约 tasks/foundation/WP-19-T02.json acceptance 1（零 Qt）/
 *     acceptance 2（红线扫描——二分结构）
 *
 * 扫描域＝产品面 include/**＋src/**（test/**、contract_test/** 为测试面，
 * plugin/**、assembly/** 为 Qt 例外载体域——三者均不在此列；插件零计算
 * 红线的反向扫描由契约测试 SelectionPluginAssemblyContractTest 以词表
 * 承载；与 ird_gates 第 4 步"产品面"同口径）；扫描经编译定义
 * IRD_SELECTION_UNIT_ROOT 注入的源码树根定位（kinematics/io/workflow/
 * drivetrain 同款）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// selection 单元树根（industrialrobot 目录——IRD_SELECTION_UNIT_ROOT
/// 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_SELECTION_UNIT_ROOT};
    return dir;
}

/// 收集产品面（include/＋src/）全部 C++ 源文件路径；两目录均应存在
/// （T02 落位形态）——任一缺失以 ADD_FAILURE 显性报出并跳过（辅助函数
/// 非 void，不能用 ASSERT_*；不留假阳性通道由非空断言兜底）。
std::vector<fs::path> collectProductFaceFiles()
{
    std::vector<fs::path> files;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot() / "selection" / sub;
        if (!fs::exists(base)) {
            ADD_FAILURE() << "selection 产品面目录缺失: " << sub;
            continue;
        }
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            if (iec || !it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".h" || ext == ".cpp" || ext == ".ipp") {
                files.push_back(it->path());
            }
        }
    }
    return files;
}

/// 读取文件全文；不可读显性失败。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// selection 计算库允许 include 的他单元清单（卡 §3.2 边表——两条登记
/// 编译链接边 core＋evidence，白名单 "selection->…" 两行同源；WP-19-T09
/// 增列 "project"＝§14.8 回填处理器的接口实现形态消费——include project
/// 公共头（CommandService.hpp）实现 ICommandHandler 接口，**零链接边**
/// （产品库不链接 sdurws_ird_project——配置期守卫与本文件链接面扫描
/// 钉住；零 project 实现符号引用的纪律见 Backfill.hpp 文件头"依赖形态
/// 登记"，单元卡 §3.2 增量修订同步）。其余表外单元（policy/io/
/// diagnostics/ui/runtime/drivetrain 等）的 include 仍在此暴露——协作
/// 经端口/注入，零编译边。
constexpr const char* kAllowedUnits[] = {
    "core", "evidence", "project",
};

/// 线性单遍注释剥离器：返回仅含"非注释字符"的文本（注释内容被空格
/// 替换、行结构保留；字符串/字符字面量内容原样保留）。实现取 kinematics
/// 同款状态机（不用 regex：块注释的正则形态在 MSVC std::regex 的
/// ECMAScript 回溯实现上对长注释会爆栈——modeling 同名用例首版实测）。
std::string stripComments(const std::string& src)
{
    enum class State { Code, LineComment, BlockComment, StrLiteral, CharLiteral };
    std::string out;
    out.reserve(src.size());
    State state = State::Code;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char ch = src[i];
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (ch == '/' && next == '/') {           // 进入行注释
                state = State::LineComment;
                out += "  ";
                ++i;
            } else if (ch == '/' && next == '*') {    // 进入块注释
                state = State::BlockComment;
                out += "  ";
                ++i;
            } else if (ch == '"') {                   // 进入字符串字面量
                state = State::StrLiteral;
                out += ch;
            } else if (ch == '\'') {                  // 进入字符字面量
                state = State::CharLiteral;
                out += ch;
            } else {
                out += ch;
            }
            break;
        case State::LineComment:
            if (ch == '\n') {                         // 行注释止于换行
                state = State::Code;
            }
            out += (ch == '\n') ? '\n' : ' ';
            break;
        case State::BlockComment:
            if (ch == '*' && next == '/') {           // 块注释止于 */
                state = State::Code;
                out += "  ";
                ++i;
            } else {
                out += (ch == '\n') ? '\n' : ' ';
            }
            break;
        case State::StrLiteral:
            if (ch == '\\') {                         // 转义对整体保留跳读
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '"') { state = State::Code; }
            }
            break;
        case State::CharLiteral:
            if (ch == '\\') {
                out += ch;
                if (i + 1 < src.size()) { out += src[i + 1]; }
                ++i;
            } else {
                out += ch;
                if (ch == '\'') { state = State::Code; }
            }
            break;
        }
    }
    return out;
}

}  // namespace

/**
 * 零 Qt（acceptance 1——计算库二分结构半区，R-3/NFR-MNT-01）：产品面
 * 全部文件不含任何 Qt 模块头（<QtWidgets/…>、<QtCore/…> 等）与 Qt 类头
 * （Q+大写开头约定，如 <QWidget>）——与 ird_gates 第 4b 步同判据，作为
 * 单元内运行期复核（配置期守卫管链接面，本用例管源码面）。计算库的 Qt
 * 例外载体只有 _plugin 目标（卡 §3.2"R-3 例外边界＝插件目标"）——其源
 * 码域 plugin/＋assembly/ 不在本扫描域（ird_gates 第 4 步同口径），插件
 * 面纪律归契约测试。
 */
TEST(SelBuildRedLine, ProductFaceHasZeroQtIncludes_WP19T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    static const std::regex kQtModule(
        R"re(#[ \t]*include[ \t]*[<"][^>"]*Qt)re");
    static const std::regex kQtClassHeader(
        R"re(#[ \t]*include[ \t]*<[A-Za-z_]*Q[A-Z][A-Za-z0-9_]*>)re");

    for (const auto& file : collectProductFaceFiles()) {
        const std::string text = readFile(file);
        EXPECT_FALSE(std::regex_search(text, kQtModule))
            << "产品面包含 Qt 头（R-3/NFR-MNT-01）: " << file.string();
        EXPECT_FALSE(std::regex_search(text, kQtClassHeader))
            << "产品面疑似包含 Qt 类头（Q+大写约定）: " << file.string();
    }
}

/**
 * include 面限于登记集合（acceptance 2——卡 §3.2 及其 T09 增量修订；
 * R-2 跨单元只经公共头）：产品面 #include <sdurws/ird/<unit>/…> 的
 * <unit> 只允许 selection 自身＋两条登记边单元（core/evidence）＋
 * project（WP-19-T09 增列——§14.8 回填处理器的接口实现形态消费，零
 * 链接边）；其他单元一经出现即越界——业务域单元（modeling/requirements/
 * kinematics/trajectory/dynamics/optimization；R-1 面——selection 自身
 * 是业务域单元，不在此列）、卡面"运行时注入/端口"列其余单元（policy/
 * io/diagnostics/ui/runtime/drivetrain；SUB 面——协作经端口/注入不落
 * 编译边）。
 */
TEST(SelBuildRedLine, IncludeFaceRestrictedToTwoRegisteredEdges_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01", "ARC-02"},
                  std::vector<std::string>{});

    static const std::regex kIrdInclude(
        R"re(#[ \t]*include[ \t]*[<"]sdurws/ird/([a-z]+)/([A-Za-z0-9_/.]+)[>"])re");

    for (const auto& file : collectProductFaceFiles()) {
        const std::string text = readFile(file);
        for (std::sregex_iterator it(text.begin(), text.end(), kIrdInclude), end;
             it != end; ++it) {
            const std::string unit = (*it)[1].str();
            const std::string header = (*it)[2].str();
            if (unit == "selection") {
                continue;  // 本单元头不受边约束
            }
            const bool allowed = std::any_of(std::begin(kAllowedUnits),
                                             std::end(kAllowedUnits),
                                             [&](const char* u) { return u == unit; });
            EXPECT_TRUE(allowed)
                << "include 越界单元 sdurws/ird/" << unit
                << "（两条登记边外——R-1/R-2，卡 §3.2）: " << file.string();
            if (allowed) {
                // 公共头可达性：对端头必须在其 include/ 公共面下（私有头
                // ——src/、test/——不可达，R-2）。
                const auto pub = unitRoot() / unit / "include" / "sdurws"
                                 / "ird" / unit / fs::path{header};
                EXPECT_TRUE(fs::exists(pub))
                    << "包含他单元非公共头 sdurws/ird/" << unit << "/"
                    << header << "（公共头未在对方 include/ 下命中——R-2）: "
                    << file.string();
            }
        }
    }
}

/**
 * 产品面零 RobWork 字面量（R-4 静态扫描的单元内复核面）：名称拼接/剥离
 * 语义归 runtime（ARC-04/NFR-MNT-07）——selection 产品面字符串字面量
 * 不得出现 "RobWork"（ird_gates 第 4c 步同判据；注释中的文档性提及不属
 * 行为，扫描前剥离——与门禁 F-011 修复后口径一致）。
 */
TEST(SelBuildRedLine, ProductFaceHasNoRobWorkStringLiterals_WP19T02_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-07", "ARC-04"},
                  std::vector<std::string>{});

    for (const auto& file : collectProductFaceFiles()) {
        const std::string text = readFile(file);
        // 剥离注释（F-011 同款口径——R-4 判定对象是代码行为，注释属
        // 文档；字面量内容保留与门禁判据一致——线性状态机实现见上）。
        const std::string code = stripComments(text);
        EXPECT_EQ(code.find("RobWork"), std::string::npos)
            << "产品面出现 RobWork 字面量（疑名称拼接/剥离——R-4）: "
            << file.string();
    }
}

/**
 * 首行字符集完整性抽查（/utf-8 落位面）：产品面文件应为合法 UTF-8——
 * 本用例以"可无 BOM 读入且中文注释按 UTF-8 解码"的最小可验面执行：源
 * 文件含本单元必有的中文注释标记（AGENTS §2 全量执行），经编译定义
 * /utf-8 后 MSVC 可编译即编码面证据（本用例核对关键标识串存在，防扫描
 * 面失真——文件为空/占位时先行暴露）。
 */
TEST(SelBuildRedLine, ProductFaceFilesNonEmpty_WP19T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const auto files = collectProductFaceFiles();
    ASSERT_FALSE(files.empty()) << "产品面无源文件（落位形态缺失）";
    for (const auto& file : files) {
        const std::string text = readFile(file);
        EXPECT_GT(text.size(), 0U) << "空文件: " << file.string();
        // 头注释标记存在（文件头注释规范——AGENTS §2.2；@file 关键字是
        // 全部源文件的最低形态要求）。
        EXPECT_NE(text.find("@file"), std::string::npos)
            << "缺 @file 文件头注释（AGENTS §2.2）: " << file.string();
    }
}
