/**
 * @file   GateContractTest.cpp
 * @brief  七阶段门控/建议的跨单元契约测试（WF-VER-112 与 acceptance 2/3 的
 *         契约面）：门控零领域调用运行断言、P-WF-2 数据形状对齐、词表零
 *         新增、投影不双权威（units/workflow.md §11.1/§3.2）。
 *
 * 设计依据：
 *   - units/workflow.md §11.1（WF-VER-112：门控零领域调用——evaluate/
 *     mapInvalidation 无领域服务调用，R-1 运行断言；观测点＝依赖断言）、
 *     §3.2（依赖白名单——禁止业务域单元）、§4.3/§5.1（P-WF-2 谈判起点：
 *     词表归 ui 公共头、GateDecision 结构归 workflow 公共头、按 ui §6.5
 *     数据形状消费）、§2.4 N10（零新增枚举词表——SA-12）
 *   - 需求 UX-12（门控判定只消费投影与事件）、UX-10（状态词不发明第八种）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 2（"不重算领域
 *     就绪、无领域阈值——运行断言锁定"）/3（"门控数据形状按 P-WF-2 谈判
 *     起点（ui §6.5）实现，词表零新增"）
 *
 * 断言形态说明：本文件全部用例只依赖 ui/core/evidence/workflow 公共头
 * （跨单元契约测试的许可集——构建图契约同源），扫描对象经
 * IRD_WORKFLOW_UNIT_ROOT 定位源码树（_test 目标红线扫描同款注入）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/workflow/Advice.hpp>
#include <sdurws/ird/workflow/Gate.hpp>
#include <sdurws/ird/workflow/Types.hpp>

#include <sdurws/ird/core/Events.hpp>   // core::DomainEventKind（词表复用断言）
#include <sdurws/ird/ui/UiTypes.hpp>    // ui 词表（P-WF-2 形状断言）

#include <array>       // std::array——GateInputs::snapshots 形状断言
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

// 契约测试内的一级命名空间别名（与模型测试 using namespace sdurws::ird
// 同源意图——测试面便利限定，不进产品面；全限定书写冗长且无收益）。
namespace workflow = sdurws::ird::workflow;
namespace ui        = sdurws::ird::ui;
namespace core      = sdurws::ird::core;
namespace evidence  = sdurws::ird::evidence;

namespace {

/// workflow 单元树根（industrialrobot 目录——IRD_WORKFLOW_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_WORKFLOW_UNIT_ROOT};
    return dir;
}

/// 读取源码文件全文（不可读即失败——扫描失真防线）。
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 线性单遍注释剥离器（与 test/BuildRedLineTest.cpp 同款实现——该函数在
/// 彼文件匿名命名空间，测试面工具代码就地复用不构成产品重复）：返回仅含
/// "非注释字符"的文本。本文件的红线判据对象是**代码行为**（include 面/
/// 字面量/符号），注释中的文档性提及（如对端端口名、章节号"§14.5"——
/// 会被浮点正则误判为 14.5）必须剥离后再扫描，否则文档越丰富误报越多。
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

/// 门控/建议实现面源文件（T03 落位的三翻译单元——扫描对象；头文件经
/// include 面由既有 WfBuildRedLine/BuildGraphContractTest 覆盖，本组扫描
/// 聚焦"判定面不含领域调用/阈值"的实现翻译单元）。
const std::vector<fs::path>& gateAdviceSources()
{
    static const std::vector<fs::path> files = {
        unitRoot() / "workflow" / "src" / "Gate.cpp",
        unitRoot() / "workflow" / "src" / "Advice.cpp",
        unitRoot() / "workflow" / "src" / "Types.cpp",
    };
    return files;
}

/// 门控/建议公共头（词表零新增与形状断言的扫描对象——T03 三头）。
const std::vector<fs::path>& gateAdviceHeaders()
{
    static const std::vector<fs::path> files = {
        unitRoot() / "workflow" / "include" / "sdurws" / "ird" / "workflow" / "Types.hpp",
        unitRoot() / "workflow" / "include" / "sdurws" / "ird" / "workflow" / "Gate.hpp",
        unitRoot() / "workflow" / "include" / "sdurws" / "ird" / "workflow" / "Advice.hpp",
    };
    return files;
}

}  // namespace

// =====================================================================
// WF-VER-112：门控零领域调用（I-WF-2/R-1——运行断言锁定）
// =====================================================================

/**
 * 断言一（类型面，编译期）：门控输入板成员只有"快照板＋事件窗口＋水位"
 * ——acceptance 2"只消费 StageReadinessSnapshot＋事件"的形状锁定；任何
 * 向 GateInputs 塞领域服务端口/回调句柄的演进都会在此失败。
 * 断言二（文本面，运行期）：判定面三翻译单元零业务域单元 include、零
 * 浮点字面量（无"多少算合格"的数值阈值——I-WF-3）、零任务派发面（提示
 * 数据不自动触发重算——UX-12/D-WF-8）。
 */
TEST(WfGateContract, GateConsumesOnlySnapshotAndEvents_WP22T03_ACC2_WFVER112)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-112（AT 集合空——单元验证编号入 TEST 名）

    // ---- 类型面：GateInputs 唯二输入（快照板＋事件）＋水位标注。
    static_assert(std::is_same_v<decltype(workflow::GateInputs::snapshots),
                                 std::array<ui::StageReadinessSnapshot, 7>>,
                  "GateInputs::snapshots 必须是 ui::StageReadinessSnapshot 七板"
                  "（acceptance 2：只消费 StageReadinessSnapshot）");
    static_assert(std::is_same_v<decltype(workflow::GateInputs::events),
                                 std::vector<workflow::GateEvent>>,
                  "GateInputs::events 必须是 GateEvent 窗口（仅身份＋域键关联）");
    static_assert(std::is_same_v<decltype(workflow::GateEvent::kind),
                                 core::DomainEventKind>,
                  "GateEvent::kind 必须复用 core 事件词表（零新增枚举——D-WF-4）");
    // GateEvent 只允许"kind＋域键＋原因"三成员（无回调/无端口句柄）——
    // 契约测试直查成员存在性与聚合性（能聚合初始化＝纯值）。
    const workflow::GateEvent aggregateCheck{
        core::DomainEventKind::ResultArchived, {"kinematics"}, {}};
    EXPECT_EQ(aggregateCheck.domainKeys.size(), 1U);

    // ---- 文本面：判定面实现零业务域依赖（R-1；ird_gates 构建图门禁的
    //      源码级互证）。
    static const std::regex kIrdInclude(
        R"re(#[ \t]*include[ \t]*[<"]sdurws/ird/([a-z]+)/)re");
    static const char* kForbiddenUnits[] = {
        "modeling", "requirements", "kinematics", "trajectory",
        "dynamics", "drivetrain", "selection", "optimization",
    };
    // 零浮点字面量（I-WF-3：门控/建议不含数值阈值——判定全部为布尔/枚举
    // 事实透传；形如 1.5/0.001 的字面量一旦出现即阈值嫌疑）。
    static const std::regex kFloatLiteral(
        R"re([^A-Za-z0-9_"][0-9]+\.[0-9]+)re");
    // 零任务派发/会话写面（UX-12：提示数据不自动触发重算——无调度器
    // 符号；D-WF-8：建议不执行动作——无函数对象成员面）。
    static const std::regex kTaskDispatch(
        R"re(ITaskScheduler|submitTask|requestCancel|std::function)re");

    for (const fs::path& file : gateAdviceSources()) {
        // 剥离注释后扫描（判据对象是代码行为——见 stripComments 注释；
        // 字符串字面量内容保留，与门禁第 4c 步口径一致）。
        const std::string code = stripComments(readFile(file));
        for (std::sregex_iterator it(code.begin(), code.end(), kIrdInclude), end;
             it != end; ++it) {
            const std::string unit = (*it)[1].str();
            const bool forbidden =
                std::string_view(unit) == "modeling"
                || std::string_view(unit) == "requirements"
                || std::string_view(unit) == "kinematics"
                || std::string_view(unit) == "trajectory"
                || std::string_view(unit) == "dynamics"
                || std::string_view(unit) == "drivetrain"
                || std::string_view(unit) == "selection"
                || std::string_view(unit) == "optimization";
            EXPECT_FALSE(forbidden)
                << "判定面 include 业务域单元（R-1/I-WF-2）: " << file.string();
        }
        EXPECT_FALSE(std::regex_search(code, kFloatLiteral))
            << "判定面出现浮点字面量（I-WF-3 零领域阈值）: " << file.string();
        EXPECT_FALSE(std::regex_search(code, kTaskDispatch))
            << "判定面出现任务派发/回调句柄面（提示不自动重算/建议不执行）: "
            << file.string();
    }
}

// =====================================================================
// acceptance 3：词表零新增＋P-WF-2 数据形状对齐
// =====================================================================

/**
 * 断言一（类型面，编译期）：GateDecision.status 就是 ui::StageViewStatus
 * 六值词表（不发明第八种状态——UX-10/D-WF-4）；stage 就是 ui::StageId。
 * 断言二（文本面，运行期）：T03 三个公共头零 "enum class"（本单元零新增
 * 枚举词表——词表唯一权威归 ui/core，SA-12/N10）。
 */
TEST(WfGateContract, WordTableZeroAdditionAndPwf2Shape_WP22T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10", "UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-112（AT 集合空——单元验证编号入 TEST 名）

    // ---- P-WF-2 谈判起点：门控产出复用 ui 词表（workflow.md §4.3——
    //      status 枚举归 ui 公共头立场；GateDecision 结构归 workflow）。
    static_assert(std::is_same_v<decltype(workflow::GateDecision::status),
                                 ui::StageViewStatus>,
                  "GateDecision::status 必须是 ui::StageViewStatus（词表零新增）");
    static_assert(std::is_same_v<decltype(workflow::GateDecision::stage),
                                 ui::StageId>,
                  "GateDecision::stage 必须是 ui::StageId（阶段词表归 ui）");
    static_assert(std::is_same_v<decltype(workflow::GateDecision::missingItemKeys),
                                 std::vector<ui::TextKey>>,
                  "缺项键必须是 ui::TextKey 承载（§6.5 缺项键透传——UX-02）");
    static_assert(std::is_same_v<decltype(workflow::StageAdvice::missingItemKeys),
                                 std::vector<ui::TextKey>>,
                  "建议缺项要素同源 ui::TextKey（UX-01 要素②透传）");

    // ---- 文本面：T03 公共头零新增枚举（D-WF-4/N10——发现即越权）。
    // 剥离注释后扫描（与上用例同口径——注释中的词表**讨论**不算新增）。
    static const std::regex kEnumDecl(R"re(\benum[ \t]+(class|struct)\b)re");
    for (const fs::path& file : gateAdviceHeaders()) {
        const std::string code = stripComments(readFile(file));
        EXPECT_FALSE(std::regex_search(code, kEnumDecl))
            << "workflow 公共头新增枚举词表（零新增红线——SA-12）: "
            << file.string();
    }
}

// =====================================================================
// acceptance 3：阶段状态投影归 ui 不双权威（DTB 禁止项）
// =====================================================================

/**
 * workflow 只**消费**汇聚投影、**产出**门控判定——不产投影、不合成呈现、
 * 不回写投影设施（ui §6.4 六态合成/§10.6 投影红线；D-WF-1 四段职责链第
 * 四段归 ui）。文本断言：T03 判定面/公共头无投影设施符号（IStageNavigation
 * Model 构造、StageView 合成、IUiProjectionStore、presentStage 实现）。
 */
TEST(WfGateContract, ProjectionOwnershipStaysWithUi_WP22T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-112（AT 集合空——单元验证编号入 TEST 名）

    // 越权符号表：出现任一即 workflow 侵入 ui 投影/呈现所有权（DTB §2.23
    // WP-22-T03 禁止项"阶段状态投影归 ui（不双权威）"）。
    static const std::regex kUiOwnershipSymbols(
        R"re(createStageNavigationModel|StageView\b|IUiProjectionStore|presentStage|statusWordToken|sevenState)re");

    std::vector<fs::path> scanned = gateAdviceSources();
    for (const fs::path& header : gateAdviceHeaders()) {
        scanned.push_back(header);
    }
    for (const fs::path& file : scanned) {
        // 剥离注释后扫描（注释中对 ui 端口的**文档性提及**——如 P-WF-2
        // 张力登记引用对端端口名——不属行为，判据对象是代码符号）。
        const std::string code = stripComments(readFile(file));
        EXPECT_FALSE(std::regex_search(code, kUiOwnershipSymbols))
            << "workflow 判定面出现 ui 投影/呈现所有权符号（不双权威）: "
            << file.string();
    }
}

// =====================================================================
// acceptance 1：级联提示只到"提示"为止（UX-12 跨单元语义面）
// =====================================================================

/**
 * mapInvalidation 的失效事件仅身份契约（core D-09）：接口接受 core
 * DomainEvent（kind 校验拒非失效事件）＋产出 StaleHint 值（无任务句柄）。
 * 运行面复核提示数据的纯值性与三要素形状（模型测试 WF-VER-105 已钉内容，
 * 本用例钉跨单元契约形状）。
 */
TEST(WfGateContract, InvalidationHintIsPureValueData_WP22T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-112（AT 集合空——单元验证编号入 TEST 名）

    // StaleHint 纯值性：可聚合比较、无回调成员（提示数据不是任务指令）。
    const workflow::StaleHint hint{ui::StageId::Kinematics, "kinematics", {},
                                   workflow::kActionRecomputeDownstream};
    const workflow::StaleHint same = hint;
    EXPECT_EQ(hint, same);
    EXPECT_EQ(hint.actionKey, "recompute-downstream");  // §6.3 R4 词形冻结

    // 建议条目零执行面：NextStepAdvice 无函数指针/std::function 成员
    // （D-WF-8——点击建议＝导航，执行经命令与用户确认）。
    const workflow::NextStepAdvice advice{
        workflow::kActionFixInputs, ui::StageId::Kinematics, {},
        "advice.fix-inputs.title", {}};
    const workflow::NextStepAdvice sameAdvice = advice;
    EXPECT_EQ(advice, sameAdvice);
}
