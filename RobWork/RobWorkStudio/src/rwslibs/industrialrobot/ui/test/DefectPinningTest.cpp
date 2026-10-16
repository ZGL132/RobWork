/**
 * @file   DefectPinningTest.cpp
 * @brief  E2 审计批缺陷钉扎用例组（F-554/F-557/F-558/F-714＋E2-UI-B 批
 *         F-559/F-560/F-561＋E2-UI-C 批 F-562/F-563）——宿主装配面接线
 *         形态、框架补丁面、三维投影层清除簇、区域框棱表角序契约与编译
 *         端口摘要字母表守卫的常驻自证。
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
 *   - findings.json F-714（框架示例插件 RWSimulatorPlugin 在 fresh 树
 *     全量构建下编译失败——其 TU include 链经 OpenGL/宿主头在 rw/math
 *     与 Eigen 头之前引入 <windows.h>，MSVC 的 min/max 函数式宏使
 *     rw/math/Math.hpp 的 std::min/std::max 调用展开成非法 token 序列
 *     （warning C4003 → error C2589/C2059，acc/ui-t77/1 §14.1 实测 71
 *     处错误）；修复＝SA-02 补丁 0002：该 TU 序言区定义 NOMINMAX 宏
 *     守卫——windows.h 随之不再定义 min/max，rw/math 与 Eigen 两族
 *     污染点同时免疫）；
 *   - findings.json F-559（宿主三维投影三层缓存 m_reqMarkers/m_reqBox/
 *     m_reqGrid 不在任何项目关闭拍清理——ird 项目关闭经
 *     UiSessionController Draining→teardownSharedSurfacesForClose，不经
 *     宿主框架 WorkCell 关闭拍→onSceneCleared→removePreview 不触发，
 *     A 项目缓存残值被 B 项目首个标记合并投递重挂＝跨项目串呈现；修复
 *     ＝UI-T77 teardown 第九类收口：三缓存清零＋网关整组摘除，先清后
 *     摘——摘除失败时缓存已归零，新项目首投递不携带残值）；
 *   - findings.json F-560（删除最后一个区域后三维框/格层永不清除——面
 *     板 renderRegionPage 空集合分支只清标签文本不投递，宿主缓存残值被
 *     后续标记合并投递重挂；修复＝UI-T77：RegionPreviewSink 签名改
 *     optional（nullopt＝空态投递），面板空集合投 nullopt，宿主空态分
 *     支两缓存清零后合并投递（boxOutline/sampleGrid 均 nullopt＝两层同
 *     拍清除）——面板生产侧已有 gui 行为用例
 *     RegionPreviewSink_EmptySetDeliversNullopt_UI_T77 钉扎，本批钉宿主
 *     消费侧形态）；
 *   - findings.json F-561（区域参考系不可解析分支只清框层不清格层——
 *     refResolved=false 时 m_reqGrid 保持上一区域旧值合并投递，「半新半
 *     旧」在原子替换语义自家分支内复活，且 logDev 文本自称「框层清除」
 *     与实际行为不符；修复＝UI-T77：失败分支同拍 m_reqGrid.reset()，
 *     logDev 文本更正「框/格层清除」——文本与行为一致）；
 *   - units/ui.md §12.3 静态扫描口径；同构先例：ui/test/BuildRedLineTest.cpp
 *     （运行期源码扫描，IRD_UI_UNIT_ROOT 注入、注释剥离、正向控制——
 *     EX-T01 P-EX-1"常驻自证"形态）。
 *
 * 为什么是源码形态扫描：前三条缺陷的本体都在 IrdWorkbenchHostPlugin /
 * WorkbenchContentImpl 的 Qt 装配面（构造依赖 RobWorkStudio 宿主注入链
 * ——模型/GUI 测试面均无法无头实例化完整装配序），域侧行为（就绪检查
 * 器、拾取写回裁决）已有 ReadinessTest/RequirementsView3DFlowTest 以正
 * 确闭包全绿覆盖；宿主侧的"接线正确性"（五处计算点传实上下文、缝延迟
 * 现取、拒绝反馈空键守卫）在本仓库的既定钉扎手法即运行期源码扫描
 * （BuildRedLineTest 的 SA-16/R-4/O-31 三先例）。F-557 的行为半区另由
 * gui_test/WorkbenchContentGuiTest.cpp 具名用例承载（submitCommand 可达
 * 面）——本文件诸用例钉形态在位，行为用例钉语义正确，两层互证。
 * F-714 则是编译期缺陷（TU 编不过＝任何运行时断言在该 TU 上都不可达），
 * 运行期可钉扎的只有修复形态的在位性——序言区 NOMINMAX 守卫＋
 * patches/ 补丁登记承载；构建级红绿（缺陷版编译失败/修复版通过）由
 * 集成模式 RWSimulatorPlugin 目标的验收证据承载（E2-FW 批留痕）。
 *
 * 判定口径：注释剥离后扫描（判定对象是代码行为——注释散文中的缺陷形态
 * 字样属文档性提及，如 UiPlugin.cpp 缝构造处的 F-558 修复说明注释；与
 * ird_gates/BuildRedLineTest 同口径，避免双口径漂移）。
 *
 * E2-UI-B 批（F-559/F-560/F-561）为何同为形态扫描：三缺陷的修复本体都
 * 落在 IrdWorkbenchHostPlugin 的 Qt 装配面（teardownSharedSurfacesForClose
 * 与 bindRegionPreviewSink 闭包——构造依赖 RobWorkStudio 宿主注入链，
 * 无法无头实例化完整装配序；findings 条目登记的修复均落在 UI-T77 提交
 * 7d7ade8e，经亲读确认缺陷已消除，故本批按修复纪律④零产品源码改动，
 * 只补 F-id 命名的常驻钉扎）。三层缓存与两分支的清除行为没有域侧替身
 * 可驱动（闭包体捕获网关与诊断管道，均为宿主装配产物）——宿主侧清除
 * 收口的"接线正确性"在本仓库的既定钉扎手法即运行期源码扫描。扫描以
 * 函数体/分支切片定位（锚点均唯一），断言"清除动作在正确的分支内、按
 * 正确的次序、以与日志文本一致的含义发生"。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

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

/// 提取函数体（E2-UI-B 批辅助）：在注释已剥离的文本上定位唯一的函数定
/// 义标记，自首个 '{' 起做花括号深度计数，返回含首尾花括号的完整函数体。
/// 用途＝把"某清除动作必须在某个具体函数内发生"的断言从全文件级收窄到
/// 函数级——这些缓存操作在文件其他位置有合法同形出现（如标记 sink 每次投
/// 递前的 m_reqMarkers.clear()），全文件计数会既漏报又误报。注意：若函数
/// 体内字符串字面量出现不平衡花括号会截断提取窗——截断只会让断言失败
/// （响亮红），不会制造假绿。找不到标记/花括号失配返回空串（调用方
/// ASSERT 拦截——扫描面失真不留假阳性通道）。
std::string functionBody(const std::string& stripped, const std::string& defMarker)
{
    const std::size_t defPos = stripped.find(defMarker);
    if (defPos == std::string::npos) {
        return {};
    }
    const std::size_t bodyStart = stripped.find('{', defPos);
    if (bodyStart == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (std::size_t i = bodyStart; i < stripped.size(); ++i) {
        if (stripped[i] == '{') {
            ++depth;
        } else if (stripped[i] == '}') {
            --depth;
            if (depth == 0) {
                return stripped.substr(bodyStart, i - bodyStart + 1);
            }
        }
    }
    return {};
}

/// 分支切片（E2-UI-B 批辅助）：取 [beginMarker 首次出现, endMarker 首次
/// 出现) 的文本窗——两个锚点都要求唯一（调用方以 countOccurrences 前置
/// 断言锚点唯一性，防锚漂移到别处把窗撑大）；任一锚点缺失返回空串。
/// 用途＝"失败分支/空态分支内必须有某清除动作"的窗口级断言（窗口窄于
/// 全文件、无需完整括号配对）。
std::string sliceBetween(const std::string& stripped, const std::string& beginMarker,
                         const std::string& endMarker)
{
    const std::size_t beginPos = stripped.find(beginMarker);
    if (beginPos == std::string::npos) {
        return {};
    }
    const std::size_t endPos = stripped.find(endMarker, beginPos + beginMarker.size());
    if (endPos == std::string::npos) {
        return {};
    }
    return stripped.substr(beginPos, endPos - beginPos);
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

/**
 * F-714 钉扎（框架示例插件 TU 的 windows.h min/max 宏防护形态）：
 *
 * 缺陷机理（findings.json F-714，实录＝acc/ui-t77/1 §14.1）：示例插件
 * RobWorkSim/src/rwsimlibs/plugins/RWSimulatorPlugin.cpp 的 TU include
 * 链经 OpenGL（glMultMatrixf/GLfloat 消费面）与宿主头在 rw/math 与
 * Eigen 头之前引入 <windows.h>；MSVC 的 min/max 函数式宏
 * （#define min(a,b) …）使 rw/math/Math.hpp 内未括号化的 std::min /
 * std::max 调用（Math.hpp:463/480/543/559 四处）展开为
 * `std::(((a)<(b))?(a):(b))` 这类非法 token 序列——warning C4003 →
 * error C2589/C2059，fresh 树全量构建在该 vcxproj 中断 71 处错误。
 *
 * 修复（SA-02 补丁 0002，PATCHES.md 登记承载）：该 TU 序言区（首个
 * #include 之前）以 #ifndef 守卫定义 NOMINMAX——windows.h 处理时不再
 * 定义 min/max 宏，rw/math 与 Eigen 两族污染点同时免疫。为何选
 * NOMINMAX 而非括号化 Math.hpp：Eigen 头（vcpkg 第三方，不可改）同族
 * 污染，单边括号化 rw/math 覆盖不全；TU 级宏防护是一处改动覆盖该 TU
 * 全部错误的最小修法（findings owner 处方两候选之一）。
 *
 * 为什么只能形态扫描：缺陷本体是编译期宏污染——TU 编不过时任何运行
 * 时断言都不可达，本用例钉「修复形态在位」：
 * ①RWSimulatorPlugin.cpp 序言区（首个 #include 之前）存在 NOMINMAX
 *   宏守卫定义（位置约束是语义本身——windows.h 一经处理宏即生效，
 *   include 之后再 define 为时已晚）；
 * ②patches/PATCHES.md 登记 0002 补丁且补丁文件在位、内容与登记一致
 *   （SA-02 登记承载不漂移——补丁文件被删/登记行被拆即红）。
 *
 * 构建级红绿由集成模式 RWSimulatorPlugin 目标承载：缺陷版编译失败
 * （C2589/C2059）＝复现红，补丁版通过＝修复绿（E2-FW 批 _e2-evidence/
 * 留痕）；本用例与构建证据两层互证。
 *
 * 需求锚：无直接需求条目（构建面缺陷——交付构建完备性，非领域语义），
 * 需求集如实留空；追溯锚＝findings.json F-714＋acc/ui-t77/1 §14.1
 * 实录＋DTB §5.1 fresh 树全框架构建排除面（F-622 同族，本补丁落地后
 * RWSimulatorPlugin 移出该排除面）。
 */
TEST(DefectPinning, SimulatorPluginTU_NomimaxGuardBeforeFirstInclude_F714)
{
    // 需求集空＋findings 追溯：构建面缺陷无直接需求条目（irdTestInfo
    // 签名允许空集——"建议 P0 测试必填"为建议而非强制），不虚构需求锚。
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{"F-714"},
                  std::nullopt);

    // 框架根＝industrialrobot 上溯四级（industrialrobot → rwslibs → src
    // → RobWorkStudio → 框架根，其下 RobWork/RobWorkSim/RobWorkStudio
    // 三子树并列）；插件 TU 挂 RobWorkSim。
    const fs::path pluginTu =
        unitRoot() / ".." / ".." / ".." / ".." / "RobWorkSim" / "src"
        / "rwsimlibs" / "plugins" / "RWSimulatorPlugin.cpp";

    const std::string stripped = stripComments(readFile(pluginTu));
    ASSERT_FALSE(stripped.empty()) << "RWSimulatorPlugin.cpp 读取失败（扫描面失真）";

    // ①序言区判定：首个 #include 之前的文本必须已含 NOMINMAX 宏守卫
    //   定义（#ifndef 守卫形态——防与构建系统/上游的重复定义冲突）。
    //   位置约束＝修复语义本身：windows.h 经传递 include 一经处理即定
    //   义 min/max，首个 include 之后再 define 为时已晚。
    const std::size_t firstInclude = stripped.find("#include");
    ASSERT_NE(firstInclude, std::string::npos)
        << "RWSimulatorPlugin.cpp 无任何 #include（扫描形态失真）";
    const std::string prologue = stripped.substr(0, firstInclude);
    EXPECT_NE(prologue.find("#define NOMINMAX"), std::string::npos)
        << "TU 序言区 NOMINMAX 守卫缺失（F-714 缺陷形态回归——windows.h "
           "min/max 宏将污染 rw/Math.hpp 与 Eigen 头，fresh 树全量构建"
           "在 RWSimulatorPlugin 中断）";

    // ②SA-02 登记承载：PATCHES.md 登记 0002 补丁＋补丁文件在位＋登记
    //   与补丁本体一致（修改文件点名 RWSimulatorPlugin.cpp、防护手段
    //   点名 NOMINMAX）——登记漂移/文件被删即红。
    const fs::path patchesDir = unitRoot() / "patches";
    const std::string patchName = "0002-rwsimulator-plugin-nominmax.patch";
    const std::string patchesMd = readFile(patchesDir / "PATCHES.md");
    ASSERT_FALSE(patchesMd.empty()) << "PATCHES.md 读取失败（登记面失真）";
    EXPECT_NE(patchesMd.find(patchName), std::string::npos)
        << "PATCHES.md 未登记 0002 补丁（SA-02 登记承载缺失——框架改动"
           "与登记册脱钩）";

    const std::string patchBody = readFile(patchesDir / patchName);
    ASSERT_FALSE(patchBody.empty()) << "0002 补丁文件读取失败（登记与文件脱钩）";
    EXPECT_NE(patchBody.find("RWSimulatorPlugin.cpp"), std::string::npos)
        << "0002 补丁未触达 RWSimulatorPlugin.cpp（登记与补丁本体漂移）";
    EXPECT_NE(patchBody.find("NOMINMAX"), std::string::npos)
        << "0002 补丁不含 NOMINMAX 防护（登记与补丁本体漂移——防护手段"
           "与 findings owner 处方不符）";
}

// =====================================================================
// E2-UI-B 批——三维投影层清除簇钉扎（F-559/F-560/F-561）
// =====================================================================

/**
 * F-559 钉扎（项目关闭拍的三维预览缓存收口形态）：teardownSharedSurfaces
 * ForClose 函数体（注释剥离后、按函数体切片断言——三层缓存在文件其他位
 * 置有合法同形操作，全文件计数会误报）内必须：
 * ①m_reqMarkers.clear() ＋ m_reqBox.reset() ＋ m_reqGrid.reset() 三缓存清
 *   零各至少一处（缺陷形态＝八类收口无第九类——三缓存残值跨项目存活，
 *   被 B 项目首个标记合并投递以 update.boxOutline=m_reqBox 原子替换语义
 *   重挂＝A 项目的框/格/标记呈现在 B 项目复活）；
 * ②m_view3dGateway->removePreview() 网关整组摘除在位（宿主框架 WorkCell
 *   关闭拍的 onSceneCleared→removePreview 对 ird 项目关闭不触发——必须
 *   在 teardown 内显式补拍）；
 * ③次序约束：三缓存清零先于 removePreview（修复语义本身——即使摘除失
 *   败〔场景不可得〕，缓存已归零，新项目首投递携带空区域/空格层，不会
 *   重挂残值；次序颠倒＝摘除失败路径重新暴露串数据窗口）。
 *
 * 变异自证：还原缺陷形态（删除三缓存清零行）→ 本用例红（三计数断言失
 * 败）→ 恢复 → 绿（_e2-evidence/mut-A-F559-*.log）。
 *
 * 需求锚：无直接需求条目（宿主项目关闭拍的会话呈现面缓存生命周期——
 * UI-T23 契约 acceptance 4 关闭清理的第九类扩展，UI-T77 落地；同族纪律
 * ＝F-550②「不残留旧呈现」），需求集如实留空，findings 追溯 F-559。
 */
TEST(DefectPinning, HostProjectCloseTeardown_PreviewCachesClearedBeforeRemovePreview_F559)
{
    // 无直接需求条目（构建/装配面缺陷同 F-714 口径）——findings 追溯登记。
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{"F-559"},
                  std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "UiPlugin.cpp"));
    ASSERT_FALSE(stripped.empty()) << "UiPlugin.cpp 读取失败（扫描面失真）";

    // 函数体切片（定义标记唯一——前置断言锚点在位；标记漂移＝空体断言
    // 失败，不留"扫错地方"的假绿通道）。
    const std::string teardownDef =
        "void IrdWorkbenchHostPlugin::teardownSharedSurfacesForClose()";
    ASSERT_EQ(countOccurrences(stripped, teardownDef), std::size_t{1})
        << "teardownSharedSurfacesForClose 定义标记漂移（扫描锚失真）";
    const std::string body = functionBody(stripped, teardownDef);
    ASSERT_FALSE(body.empty()) << "teardown 函数体提取失败（花括号配对漂移）";

    // ①三缓存清零各至少一处（下限断言——合法新增清理点不误红，任一缓
    //   存被移出关闭拍即跌破）。
    EXPECT_GE(countOccurrences(body, "m_reqMarkers.clear()"), std::size_t{1})
        << "标记缓存未在项目关闭拍清零（F-559 缺陷形态回归——A 项目标记残值"
           "跨项目存活）";
    EXPECT_GE(countOccurrences(body, "m_reqBox.reset()"), std::size_t{1})
        << "框层缓存未在项目关闭拍清零（F-559 缺陷形态回归——A 项目区域框"
           "残值被 B 项目标记投递重挂）";
    EXPECT_GE(countOccurrences(body, "m_reqGrid.reset()"), std::size_t{1})
        << "格层缓存未在项目关闭拍清零（F-559 缺陷形态回归——A 项目采样格"
           "＋着色残值跨项目存活）";

    // ②网关整组摘除在位（ird 项目关闭不经过宿主框架关闭拍——必须在
    //   teardown 内显式补 removePreview）。
    const std::size_t removePreviewPos = body.find("m_view3dGateway->removePreview()");
    EXPECT_NE(removePreviewPos, std::string::npos)
        << "网关 removePreview 未在项目关闭拍补拍（F-559 缺陷形态回归——"
           "旧呈现整组残留在网关侧）";

    // ③次序约束：三缓存清零先于摘除（最后一次清零＝m_reqGrid——摘除失
    //   败路径下缓存已归零的修复语义；用格层清零位置作次序锚）。
    ASSERT_NE(removePreviewPos, std::string::npos)
        << "（次序断言前置——摘除不在位时次序无从谈起，①②已报红）";
    const std::size_t gridResetPos = body.find("m_reqGrid.reset()");
    ASSERT_NE(gridResetPos, std::string::npos)
        << "（次序断言前置——格层清零不在位时次序无从谈起，①已报红）";
    EXPECT_LT(gridResetPos, removePreviewPos)
        << "缓存清零晚于网关摘除（F-559 修复次序语义回归——摘除失败路径下"
           "缓存未归零，串数据窗口重开）";
}

/**
 * F-560 钉扎（区域集合空态的宿主消费侧形态）：bindRegionPreviewSink 闭包
 * 的空态分支（`if (!geoOpt.has_value())` 唯一锚到首个 return 的切片）内
 * 必须：
 * ①m_reqBox.reset() ＋ m_reqGrid.reset() 两缓存清零（缺陷形态＝空态只到
 *   面板标签、宿主缓存原值保留——后续标记合并投递以缓存值重挂，删除唯
 *   一区域后三维框/格/着色永残留直到关项目）；
 * ②applyPreview 合并投递在切片内（清除以「空＝清除该层」契约承载——
 *   boxOutline/sampleGrid 均 nullopt 投递；只清缓存不投递则网关侧旧呈现
 *   残留，同族缺陷面）。
 *
 * 面板生产侧（空集合→m_regionPreview(std::nullopt) 投递）已有 gui 行为
 * 用例 RegionPreviewSink_EmptySetDeliversNullopt_UI_T77 钉扎（面板可无头
 * 实例化）；本用例钉宿主消费侧（Qt 装配面，行为不可达）——两层互证。
 *
 * 变异自证：还原缺陷形态（删除空态分支两清零行）→ 本用例红 → 恢复 →
 * 绿（_e2-evidence/mut-B-F560-*.log）。
 *
 * 需求锚：REQ-03（Box 工作区域定义——区域预览的呈现语义随区域集合真值；
 * 与既有 gui 用例同锚）。
 */
TEST(DefectPinning, HostRegionPreviewEmptySet_ClearsCachesAndDeliversNulloptLayers_F560)
{
    IRD_TEST_INFO("REQ-03", {"F-560"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "UiPlugin.cpp"));
    ASSERT_FALSE(stripped.empty()) << "UiPlugin.cpp 读取失败（扫描面失真）";

    // 切片锚点唯一性前置（锚漂移＝窗口失真——先红在锚上，不在断言上）。
    ASSERT_EQ(countOccurrences(stripped, "bindRegionPreviewSink("), std::size_t{1})
        << "bindRegionPreviewSink 接线点漂移（扫描锚失真——区域预览 sink 应"
           "唯一绑定产品点）";
    ASSERT_EQ(countOccurrences(stripped, "if (!geoOpt.has_value())"), std::size_t{1})
        << "空态分支标记漂移（扫描锚失真）";
    const std::string branch =
        sliceBetween(stripped, "if (!geoOpt.has_value())", "return;");
    ASSERT_FALSE(branch.empty()) << "空态分支切片提取失败（return 终点漂移）";

    // ①两缓存清零在空态分支内（下限断言——空态两层同进退，缺一即红）。
    EXPECT_GE(countOccurrences(branch, "m_reqBox.reset()"), std::size_t{1})
        << "空态分支未清框层缓存（F-560 缺陷形态回归——删除唯一区域后框残值"
           "被后续标记投递重挂）";
    EXPECT_GE(countOccurrences(branch, "m_reqGrid.reset()"), std::size_t{1})
        << "空态分支未清格层缓存（F-560 缺陷形态回归——采样格＋着色残值同"
           "面重挂）";

    // ②清除以合并投递承载（「空＝清除该层」契约的宿主侧兑现——nullopt
    //   boxOutline/sampleGrid 抵达网关才真正清屏）。
    EXPECT_GE(countOccurrences(branch, "applyPreview"), std::size_t{1})
        << "空态分支缺合并投递（只清缓存不投递＝网关侧旧呈现残留——F-560"
           "同族缺陷面）";
}

/**
 * F-561 钉扎（参考系不可解析分支的格层对称清除＋日志诚实形态）：两半—
 * ①行为半区：失败分支切片（成功支尾部 `m_reqGrid = grid;` 唯一锚 → 日
 *   志文本锚「区域参考系不可解析」）内必须有 m_reqGrid.reset()——缺陷形
 *   态＝该分支只清框层（box 保持 nullopt 随 m_reqBox=box 合并投递）而
 *   m_reqGrid 保持上一区域旧值，「半新半旧」在原子替换语义自家分支内
 *   复活（格层骨架线＋着色采样点跨区域/跨参考系漂浮）；
 * ②文本半区：logDev 文本含「框/格层清除」恰一处（全文件计数——该文本
 *   是失败分支可观测行为的自述，自述与行为一致是本条 findings 的显式
 *   验收面；缺陷态文本自称「框层清除」掩盖格层残留）。
 *
 * 变异自证：C1 还原缺陷形态（删除失败分支 m_reqGrid.reset()）→ 行为半
 * 区红；C2 文本回退「框层清除」→ 文本半区红；各自恢复 → 绿
 * （_e2-evidence/mut-C1-F561-*.log／mut-C2-F561-*.log）。
 *
 * 需求锚：REQ-03（Box 工作区域定义——区域预览几何随参考系真值解析；
 * 参考系不可解析＝诚实空态，不呈现半新半旧）。
 */
TEST(DefectPinning, HostRegionRefUnresolved_GridClearedSameTickAsBox_F561)
{
    IRD_TEST_INFO("REQ-03", {"F-561"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "UiPlugin.cpp"));
    ASSERT_FALSE(stripped.empty()) << "UiPlugin.cpp 读取失败（扫描面失真）";

    // 切片锚点唯一性前置。成功支尾部锚 `m_reqGrid = grid;` 全文件恰一；
    // 日志文本锚在字符串字面量内（stripComments 保留字面量）全文件恰一。
    ASSERT_EQ(countOccurrences(stripped, "m_reqGrid = grid;"), std::size_t{1})
        << "成功支格层赋值锚漂移（扫描锚失真）";
    ASSERT_EQ(countOccurrences(stripped, "区域参考系不可解析"), std::size_t{1})
        << "失败分支日志锚漂移（扫描锚失真）";

    // ①行为半区：失败分支切片内格层清零在位（窗口＝成功支尾部到日志
    //   文本——恰好覆盖 else if 失败支的清除段；格层清零缺位即红）。
    const std::string failureBranch =
        sliceBetween(stripped, "m_reqGrid = grid;", "区域参考系不可解析");
    ASSERT_FALSE(failureBranch.empty()) << "失败分支切片提取失败（锚点漂移）";
    EXPECT_GE(countOccurrences(failureBranch, "m_reqGrid.reset()"), std::size_t{1})
        << "参考系不可解析分支未同拍清格层（F-561 缺陷形态回归——m_reqGrid"
           "保持上一区域旧值合并投递，格层骨架＋着色点跨区域/跨参考系漂浮）";

    // ②文本半区：日志自述「框/格层清除」恰一处（文本与实际行为一致——
    //   缺陷态自称「框层清除」掩盖格层残留；全文件计数防别处同文案稀释）。
    EXPECT_EQ(countOccurrences(stripped, "框/格层清除"), std::size_t{1})
        << "失败分支日志文本与实际行为不一致（F-561 文本半区回归——自述应"
           "为「框/格层清除」两层同拍；回退「框层清除」＝文本掩盖格层残留）";
}

// =====================================================================
// E2-UI-C 批——区域框棱表角序契约钉扎（F-562）＋编译端口摘要解析
// 字母表守卫钉扎（F-563）
// =====================================================================

/**
 * F-562 钉扎（区域框 12 棱表与八角点二进制角序的契约对齐）：HostView3D-
 * PreviewBackend 产品码（注释剥离后）RegionOutlineRender 的 kEdges 棱表
 * 必须恰为生产者角序契约下的 12 条真棱。契约原文＝PanelRegionModel
 * 「corners 恒 8 点，索引 i 的 bit0→+sx、bit1→+sy、bit2→+sz」（units/
 * requirements 单元卡 §9.8 几何载体——View3DBoxOutline「零重排直投」同
 * 款角序）；该角序下真棱＝恰一位（汉明距离 1）翻转的角点对：
 *   x 向 {0,1}{2,3}{4,5}{6,7}、y 向 {0,2}{1,3}{4,6}{5,7}、
 *   z 向立柱 {0,4}{1,5}{2,6}{3,7}。
 * 缺陷形态＝旧循环序边表（底/顶面按 0→1→2→3 循环连边）：{1,2} 类面对
 * 角线（两位翻转）混入即破坏恰等性、y 向四真棱缺失同样跌破——两条路
 * 都红，集合恰等断言一次钉住两面。
 *
 * 为何是源码形态扫描而非行为用例：kEdges 是 .cpp 内 static const 表（内
 * 部链接——测试面无法链接寻址），draw() 需 OpenGL 上下文（无头测试面
 * 不可达）；「棱表与生产者角序对齐」是纯静态契约——扫描解析棱表并做
 * 集合恰等断言即钉住语义本体（本文件 E2-UI-B 批三钉同手法；同构先例
 * BuildRedLineTest 运行期源码扫描）。
 *
 * 变异自证：还原缺陷形态（kEdges 换回旧循环序边表）→ 本用例红（恰等
 * 断言失败）→ 恢复 → 绿（_e2-evidence/mut-A-F562-*.log 留痕）。
 *
 * 需求锚：REQ-03（Box 工作区域定义——区域三维预览须忠实呈现区域盒；
 * 非方盒线框＝预览几何失真）；findings 追溯 F-562。
 */
TEST(DefectPinning, RegionOutlineEdges_BinaryCornerOrderTwelveTrueEdges_F562)
{
    IRD_TEST_INFO("REQ-03", {"F-562"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "HostView3DPreviewBackend.cpp"));
    ASSERT_FALSE(stripped.empty())
        << "HostView3DPreviewBackend.cpp 读取失败（扫描面失真）";

    // 棱表声明锚唯一性前置（锚漂移＝提取窗失真——先红在锚上，不在断言
    // 上；绘制循环处的 kEdges 裸名不带维度下标，不与本锚撞）。
    ASSERT_EQ(countOccurrences(stripped, "kEdges[12][2]"), std::size_t{1})
        << "kEdges 棱表声明漂移（扫描锚失真——区域框 12 棱表应恰一处）";

    // 提取表体：functionBody 自声明标记后的首个 '{'（＝初始化列表外层
    // 开括号）起做花括号深度计数——4 个内层 {a, b} 对被正确跨越，返回
    // 含外层首尾花括号的完整初始化体。
    const std::string table = functionBody(stripped, "kEdges[12][2]");
    ASSERT_FALSE(table.empty()) << "棱表体提取失败（花括号配对漂移）";

    // 逐字符收集表体内整数（stripComments 已剥注释——表体只余 {a, b}
    // 对序列；顶点索引恒 0..7 单字符，逐字符收集即完整值；索引域两位数
    // 的前提是八角点超过 10 个，几何上不可能）。恰 24 个＝12 对×2。
    std::vector<int> tokens;
    tokens.reserve(24);
    for (const char c : table) {
        if (c >= '0' && c <= '9') {
            tokens.push_back(c - '0');
        }
    }
    ASSERT_EQ(tokens.size(), std::size_t{24})
        << "棱表整数个数异常（12 对×2＝24——表体形态漂移，恰等断言失真）";

    // 无序对归一 (min,max) 后升序排列，与二进制序 12 真棱期望集做恰等
    // 比较——多一条面对角线、缺一条真棱、任何一条棱两端点错位都打破
    // 相等（EXPECT_EQ 失败时 gtest 打印两个序列的逐元素差异，回归者无
    // 需翻源码即可定位错棱）。期望集同样按无序对升序归一——比较的是
    // 棱的集合而非书写次序（边表行内按底面/顶面/立柱分组是注释可读性
    // 布局，不应进入契约）。
    std::vector<std::pair<int, int>> actualEdges;
    actualEdges.reserve(12);
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
        actualEdges.emplace_back(std::min(tokens[i], tokens[i + 1]),
                                 std::max(tokens[i], tokens[i + 1]));
    }
    std::sort(actualEdges.begin(), actualEdges.end());
    std::vector<std::pair<int, int>> binaryOrderEdges = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},  // x 向真棱（bit0 翻转对）
        {0, 2}, {1, 3}, {4, 6}, {5, 7},  // y 向真棱（bit1 翻转对）
        {0, 4}, {1, 5}, {2, 6}, {3, 7},  // z 向立柱（bit2 翻转对）
    };
    std::sort(binaryOrderEdges.begin(), binaryOrderEdges.end());
    EXPECT_EQ(actualEdges, binaryOrderEdges)
        << "区域框棱表偏离二进制角序 12 真棱（F-562 缺陷形态回归——循环序"
           "边表把面对角线当棱画〔上下两面蝴蝶结交叉〕且 y 向四棱缺失，线"
           "框非方盒；契约＝PanelRegionModel 索引 i 的 bit0→+sx、bit1→"
           "+sy、bit2→+sz，actual/expected 逐对对照定位错棱）";
}

/**
 * F-563 钉扎（引用图摘要 64 位十六进制的字母表守卫形态）：HostCompile-
 * Port 产品码（注释剥离后）toSummary 的 digest256 装配段必须三件套同在：
 * ①长度守卫 `hex.size() == 64`（第一道——错误长度直接走违约路径）；
 * ②小写十六进制字母表校验循环（第二道——缺陷形态＝只防长度不防字母
 *   表：64 字符含 'g' 以上字符时逐字符 std::stoi(...,16) 抛
 *   std::invalid_argument，draft.apply 处理器链无 try/catch→异常穿透
 *   Qt 事件循环＝红叉对话框＋进程退出〔F-557 家族崩出面〕）；
 * ③违约分支 `digest.fill(0)` 显式置零（「全零摘要如实投递」的承载——
 *   Digest256＝std::array 默认初始化值不确定，缺失该行＝违约路径投递
 *   不确定值，S2 摘要复核的稳定拒绝失去前提）。
 *
 * 为何是源码形态扫描而非行为用例：行为半区已有 UI-T77 随修复交付的
 * CompilePort_CorruptedDigestDoesNotCrash（HostCompilePipelineTest.cpp
 * ——被测 TU 同源编入＋框架链接面，集成树专属门控 if(TARGET sdurws)，
 * 冒烟树不编不可达）；本用例钉字母表守卫与显式置零的形态在位，与行为
 * 用例两层互证（F-557 钉扎同构——形态在位由本用例钉、语义正确由行为
 * 用例钉），且冒烟/集成两模式可跑。
 *
 * 变异自证：还原缺陷形态（摘除字母表校验循环）→ 本用例红（②形态标记
 * 失踪）→ 恢复 → 绿（_e2-evidence/mut-B-F563-*.log 留痕；行为用例的
 * 必红能力＝集成树面，由验收/收尾批在该模式亲证——登记如实例外）。
 *
 * 需求锚：CON-05（内容寻址——digest256 是证据链关联键的载体，透传不
 * 重算的前提是输入良构；违约＝诚实全零投递交给 S2 复核拒绝，不崩出也
 * 不伪造）；findings 追溯 F-563。
 */
TEST(DefectPinning, CompilePortDigest_LowercaseHexAlphabetGuarded_F563)
{
    IRD_TEST_INFO("CON-05", {"F-563"}, std::nullopt);

    const std::string stripped = stripComments(readFile(
        unitRoot() / "ui" / "plugin" / "HostCompilePort.cpp"));
    ASSERT_FALSE(stripped.empty())
        << "HostCompilePort.cpp 读取失败（扫描面失真）";

    // 装配段切片（toSummary 内 ObjectRefEntry 装配循环——digest256 字样
    // 在文件内多处合法出现〔注释/字段声明〕，按守卫标记切片收窄）。
    const std::string guardDef = "hexWellFormed = hex.size() == 64";
    ASSERT_EQ(countOccurrences(stripped, guardDef), std::size_t{1})
        << "digest256 长度守卫标记漂移（扫描锚失真——装配段应恰一处）";

    // ①长度守卫在位（第一道守卫——其本身是 UI-T77 修复的保留部分，
    //   摘除＝守卫链退化回 F-563 之前的更早形态）。
    EXPECT_NE(stripped.find(guardDef), std::string::npos)
        << "digest256 长度守卫缺失（F-563 守卫链第一道退化）";

    // ②字母表校验形态在位：小写十六进制谓词（数字段＋a-f 段——'g' 以
    //   上字符/大写即违约，std::stoi 只在良构分支触达＝异常不可能逃逸）。
    EXPECT_NE(stripped.find("(c >= 'a' && c <= 'f')"), std::string::npos)
        << "digest256 字母表校验缺失（F-563 缺陷形态回归——64 字符含非"
           "hex 字符时逐字符 std::stoi 抛 invalid_argument 穿透事件循环"
           "＝红叉对话框＋进程退出）";

    // ③违约路径显式置零在位（全零摘要如实投递的承载——std::array 默
    //   认初始化值不确定，缺显式 fill(0)＝违约路径投递不确定值）。
    EXPECT_NE(stripped.find("e.digest.fill(0)"), std::string::npos)
        << "违约路径显式置零缺失（F-563 修复半区退化——全零摘要如实投递"
           "口径失去承载，S2 摘要复核的稳定拒绝失去前提）";
}
