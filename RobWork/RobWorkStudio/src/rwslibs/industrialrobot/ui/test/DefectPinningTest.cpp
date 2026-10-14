/**
 * @file   DefectPinningTest.cpp
 * @brief  E2 审计批缺陷钉扎用例组（F-554/F-557/F-558）——宿主装配面的
 *         接线形态常驻自证。
 *
 * 设计依据：
 *   - findings.json F-554（宿主就绪门控五处计算点以空 CheckContext 调域
 *     检查器——closureRefs 空，首应用后 R0 闭包核对恒误判「悬空」→
 *     applyDraftDisablement 恒禁用＝应用草稿恒灰；修复＝UI-T75
 *     currentRequirementsCheckContext() 现取 HEAD objectRefs 统一五处）；
 *   - findings.json F-558（requirements.pick-feature 会话闭包引用缝在产品
 *     装配序下恒空——assembleView3DGateway 在 initialize 装配段执行，绑
 *     定时点 m_lastStoreAdapter 恒空，按绑定时点判空捕获的缝保持缺省空
 *     function 且无再绑定点→拾取写回 100% 被误判拒绝；修复＝UI-T77 缝改
 *     延迟现取：捕 this→调用时经 currentRequirementsCheckContext() 同源
 *     现取，绑定时机无关）；
 *   - findings.json F-557（命令拒绝反馈以空禁用键调 resolveText——处理
 *     器拒绝无禁用键是合法空态，空键进 resolveText 按 UiText fail-fast
 *     设计即抛，穿透 Qt 事件循环直达顶层 catch＝红叉对话框＋进程退出；
 *     修复＝UI-T76 两处空键守卫：非空才进 resolveText，空键＝诚实通用
 *     文案「命令未执行」）；
 *   - units/ui.md §12.3 静态扫描口径；同构先例：ui/test/BuildRedLineTest.cpp
 *     （运行期源码扫描，IRD_UI_UNIT_ROOT 注入、注释剥离、正向控制——
 *     EX-T01 P-EX-1"常驻自证"形态）。
 *
 * 为什么是源码形态扫描：三条缺陷的本体都在 IrdWorkbenchHostPlugin /
 * WorkbenchContentImpl 的 Qt 装配面（构造依赖 RobWorkStudio 宿主注入链
 * ——模型/GUI 测试面均无法无头实例化完整装配序），域侧行为（就绪检查
 * 器、拾取写回裁决）已有 ReadinessTest/RequirementsView3DFlowTest 以正
 * 确闭包全绿覆盖；宿主侧的"接线正确性"（五处计算点传实上下文、缝延迟
 * 现取、拒绝反馈空键守卫）在本仓库的既定钉扎手法即运行期源码扫描
 * （BuildRedLineTest 的 SA-16/R-4/O-31 三先例）。F-557 的行为半区另由
 * gui_test/WorkbenchContentGuiTest.cpp 具名用例承载（submitCommand 可达
 * 面）——本文件三用例钉形态在位，行为用例钉语义正确，两层互证。
 *
 * 判定口径：注释剥离后扫描（判定对象是代码行为——注释散文中的缺陷形态
 * 字样属文档性提及，如 UiPlugin.cpp 缝构造处的 F-558 修复说明注释；与
 * ird_gates/BuildRedLineTest 同口径，避免双口径漂移）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

/// ui 单元树根（industrialrobot 目录——IRD_UI_UNIT_ROOT 注入；与
/// BuildRedLineTest 的 unitRoot() 同款规范化——消除注入值尾部 ".." 段）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_UI_UNIT_ROOT}.lexically_normal();
    return dir;
}

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道——
/// BuildRedLineTest::readFile 同款纪律）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 剥离行注释与块注释（判定对象是代码行为——BuildRedLineTest::stripComments
/// 同款逐字符状态机，字符串字面量内的注释起始符不误剥离）。
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

/// 子串出现计数（断言消息携带形态说明——失败时读者不需要翻源码即可定位）。
std::size_t countOccurrences(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

}  // namespace

/**
 * F-554 钉扎（宿主就绪上下文接线形态）：UiPlugin 产品码（注释剥离后）
 * ①零 `CheckContext{}` / `CheckContext {}` 空构造字面——缺陷形态（五处计
 *   算点空闭包上下文）的唯一代码级标记，任何回归换装即命中；
 * ②currentRequirementsCheckContext() 现取入口在位且消费者不少于六处
 *   （修复态：五处就绪计算点——applyDraftDisablement 门控/
 *   wireRequirementsSession 首刷/空根初始化/setPostEditAction 编辑后重估/
 *   refreshRequirementsFromSession 重导线——＋F-558 三维缝延迟现取一处，
 *   另加定义行本身一次）；
 * ③正向控制：现取入口定义体确以 HEAD objectRefs 为闭包来源（`query().head()
 *   .objectRefs`——空实现/退化为恒空返回的变异同样命中，防"锚在而语义空"）。
 *
 * 需求锚：P-REQ-6（就绪判定权威＝域检查器，宿主取 check 产出直投——
 * 传空上下文＝宿主侧数据失真）；可见症状＝应用草稿恒灰（§7.4"禁用＋
 * 说明"的禁用面失真）。
 */
TEST(DefectPinning, HostRequirementsContext_NoEmptyClosureLiteral_F554)
{
    IRD_TEST_INFO("P-REQ-6", {"F-554"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "UiPlugin.cpp"));
    ASSERT_FALSE(stripped.empty()) << "UiPlugin.cpp 读取失败（扫描面失真）";

    // ①负向：零空构造字面（缺陷形态标记——修复前五处
    //   `requirements::CheckContext{}`；两种连写/带空格形态都扫）。
    EXPECT_EQ(countOccurrences(stripped, "CheckContext{}"), std::size_t{0})
        << "发现 CheckContext{} 空构造（F-554 缺陷形态回归——就绪计算点"
           "以空闭包上下文调域检查器）";
    EXPECT_EQ(countOccurrences(stripped, "CheckContext {}"), std::size_t{0})
        << "发现 CheckContext {} 空构造（F-554 缺陷形态回归）";

    // ②正向：现取入口定义在位＋消费者计数下限（五处就绪计算点＋三维缝
    //   ＋定义行＝七；取下限断言——合法新增计算点不误红，缺陷换装即跌破）。
    EXPECT_EQ(countOccurrences(
                  stripped, "IrdWorkbenchHostPlugin::currentRequirementsCheckContext() const"),
              std::size_t{1})
        << "currentRequirementsCheckContext() 定义漂移（F-554 修复入口不在位）";
    EXPECT_GE(countOccurrences(stripped, "currentRequirementsCheckContext()"),
              std::size_t{7})
        << "现取入口消费者不足（F-554 五处计算点＋F-558 缝＋定义＝7 下限"
           "——存在计算点被换回空上下文或绕过同源入口）";

    // ③正向控制：闭包来源＝HEAD objectRefs 现取（防锚在而语义空）。
    EXPECT_NE(stripped.find("query().head().objectRefs"), std::string::npos)
        << "现取入口未取 HEAD objectRefs（闭包来源语义空转——F-554 根因面）";
}

/**
 * F-558 钉扎（产品装配序的三维闭包缝形态）：UiPlugin 产品码（注释剥离后）
 * ①`sessionClosureRefs = [this]` 延迟现取形态恰一处——修复后的缝构造捕
 *   this（调用时现取，绑定时机无关；装配段执行时 m_lastStoreAdapter 恒
 *   空的时序事实不再影响缝的可用性）；
 * ②缝体经 currentRequirementsCheckContext().closureRefs 现取（与面板就绪
 *   上下文同源——零第二闭包来源）；
 * ③负向：零按绑定时点判空捕获的旧缺陷形态（`sessionClosureRefs = [adapter]`
 *   ——装配拍 adapter 恒空→缝恒缺省空 function→拾取写回 100% 误拒）。
 *
 * 需求锚：REQ-08（拾取写回浅核对——CheckContext 来源缝）；症状面＝产品
 * 装配序下拾取写回恒被误判「悬空」拒绝（RequirementsView3DFlowTest 的
 * 替身缝用例钉域侧行为，本用例钉产品装配序的缝构造形态）。
 */
TEST(DefectPinning, HostView3DSeams_ClosureRefsDeferredThisCapture_F558)
{
    IRD_TEST_INFO("REQ-08", {"F-558"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "UiPlugin.cpp"));
    ASSERT_FALSE(stripped.empty()) << "UiPlugin.cpp 读取失败（扫描面失真）";

    // ①正向：延迟现取形态恰一处（缝唯一绑定产品点＝assembleView3DGateway；
    //   捕 this＝绑定时机无关的机制标记）。
    EXPECT_EQ(countOccurrences(stripped, "sessionClosureRefs = [this]"),
              std::size_t{1})
        << "三维闭包缝未按延迟现取形态绑定（F-558 缺陷形态回归——缝应捕 "
           "this 调用时现取，而非按绑定时点判空捕获）";

    // ②正向：缝体经同源入口现取闭包引用（零第二闭包来源）。
    EXPECT_GE(countOccurrences(stripped, "currentRequirementsCheckContext().closureRefs"),
              std::size_t{1})
        << "缝体未经 currentRequirementsCheckContext() 现取（闭包来源漂移"
           "——与面板就绪上下文不同源）";

    // ③负向：旧缺陷形态（按绑定时点捕获 adapter 裸指针）零命中。
    EXPECT_EQ(countOccurrences(stripped, "sessionClosureRefs = [adapter]"),
              std::size_t{0})
        << "发现按绑定时点的 [adapter] 捕获形态（F-558 缺陷回归——装配拍"
           " adapter 恒空＝缝恒缺省，拾取写回恒误拒）";
}

/**
 * F-557 钉扎（命令拒绝反馈的空禁用键守卫形态）：WorkbenchContent 产品码
 * （注释剥离后）
 * ①`a.disableReasonKey.empty()` 守卫不少于两处——submitCommand 拒绝分支
 *   与 refreshCommandStates 按钮 tooltip 两个呈现面（修复前两处均无条件
 *   resolveText）；
 * ②空键诚实通用文案「命令未执行」在位（守卫的空态出线——不渲染空白、
 *   不虚构原因）。
 *
 * 行为半区（submitCommand 拒绝分支的语义正确性——不抛＋诚实文案＋非空
 * 键正常解析对照）由 gui_test/WorkbenchContentGuiTest.cpp 的具名用例
 * HandlerRejectionEmptyDisableKey_HonestFeedbackNoCrash_F557 承载；本用
 * 例钉两处守卫的形态在位（tooltip 面当前注册域无"禁用＋空键"可达形态，
 * 行为面不可驱动——扫描是唯一钉扎面）。
 *
 * 需求锚：UX-02（ERR-01"禁用必带原因"呈现纪律的空态守卫面——缺键不得
 * 以崩出代偿）。
 */
TEST(DefectPinning, ContentCommandRejection_GuardsResolveTextAgainstEmptyKey_F557)
{
    IRD_TEST_INFO("UX-02", {"F-557"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "src" / "WorkbenchContent.cpp"));
    ASSERT_FALSE(stripped.empty()) << "WorkbenchContent.cpp 读取失败（扫描面失真）";

    // ①正向：两处呈现面的空键守卫在位（下限断言——合法新增守卫点不误红，
    //   任一守卫被拆即跌破）。
    EXPECT_GE(countOccurrences(stripped, "a.disableReasonKey.empty()"),
              std::size_t{2})
        << "空禁用键守卫缺失（F-557 缺陷形态回归——拒绝反馈/tooltip 以空键"
           "进 resolveText，fail-fast 即抛穿透事件循环＝进程退出）";

    // ②正向：空键诚实文案在位（守卫空态出线的唯一承载）。
    EXPECT_NE(stripped.find("命令未执行"), std::string::npos)
        << "空键诚实通用文案缺失（守卫空态无出线——F-557 空态面失真）";
}
