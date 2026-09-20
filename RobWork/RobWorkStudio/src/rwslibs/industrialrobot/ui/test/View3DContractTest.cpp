/**
 * @file   View3DContractTest.cpp
 * @brief  UI-T05 阶段 A 契约钉住用例（模型层，无界面）：UX-11 三维视图
 *         交互清单登记冻结＋KIN-06/AT-04 会话姿态语义红线钉住。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T05.json acceptance 1~2（UX-11 交互
 *     清单登记——九项清单与需求条目序一致；KIN-06 会话姿态语义钉住——
 *     点回写仅改会话姿态，不修改设计模型、不产生修订、不触发结果失效）；
 *   - units/ui.md §4.1/§4.2（阶段 A 占位与清单登记边界）、§14.1（阶段 B
 *     承接——本套件冻结的词表/语义即阶段 B 必须承接的契约面）、§16.7
 *     v0.7（View3DContract.hpp 登记行）；
 *   - 需求 UX-11（三维视图交互清单）、KIN-06（只改变会话姿态）、AT-04
 *     （预览不产生项目修订——显式应用经命令端口）。
 *
 * 为什么用"字面冻结"断言（期望值逐字写死而非读取实现再比对）：钉住
 * （contract pinning）的价值在于让"无声漂移"在测试面即失败——词表增删/
 * 重排/id 改名/语义位翻转任何一项都必须显式改本文件＋升单元卡修订，不可
 * 能被实现侧单独悄悄完成（AGENTS.md §2.5 注释随代码更新同款纪律的测试面
 * 落实）。占位面板的呈现断言在 GUI 层（WorkbenchShellGuiTest）——本套件
 * 只钉契约数据本身。
 *
 * 线程模型：QCoreApplication 级模型测试（TestMainReport 构造——无 GUI
 * 平台插件依赖）；契约访问为编译期固定值，无共享可变状态。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/View3DContract.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

using sdurws::ird::ui::View3DInteractionItem;
using sdurws::ird::ui::View3DSessionPoseContract;

// =====================================================================
// 期望清单（字面冻结——与 REQUIREMENTS UX-11 条目书写序逐项一致）
// =====================================================================

/**
 * 期望的九项交互（id/label 逐字对应 src/View3DPlaceholder.cpp 契约定义；
 * 序＝UX-11 需求条目序）。为什么在测试侧再抄一份：钉住的本质是两处独立
 * 记录互证——实现侧改了清单而单元卡/测试未同步，本表即失配报警。
 */
const std::vector<View3DInteractionItem>& expectedManifest()
{
    static const std::vector<View3DInteractionItem> kExpected = {
        {"view3d.host", u8"三维视图（中央工作区）"},
        {"view3d.standard_camera_views", u8"标准视图（顶/右/前）与相机视图"},
        {"view3d.zoom", u8"缩放"},
        {"view3d.projection_toggle", u8"透视/正交切换"},
        {"view3d.wireframe_transparency", u8"线框/透明切换"},
        {"view3d.render_group_visibility", u8"渲染分组显隐（Virtual/Physical/Drawable/Collision/User）"},
        {"view3d.collision_highlight_mask", u8"碰撞高亮与碰撞组掩码"},
        {"view3d.screenshot_png", u8"视图截图（PNG）"},
        {"view3d.pick_ray_cast", u8"三维拾取（ray-cast 选择 Frame/Drawable）"},
    };
    return kExpected;
}

// =====================================================================
// UX-11 清单登记：九项、序冻结、id 唯一（acceptance 1 登记半区）
// =====================================================================

/**
 * 清单内容与序逐项冻结（acceptance 1：UX-11 交互清单登记——清单即阶段 B
 * 交付范围，任何增删/重排/改名都属词表变更，必须单元卡修订＋本用例同步）。
 */
TEST(View3DContract, ManifestFreezesNineItemsInRequirementOrder_UX11)
{
    IRD_TEST_INFO("UX-11", {}, std::nullopt);
    const std::vector<View3DInteractionItem>& manifest = sdurws::ird::ui::view3DInteractionManifest();
    const std::vector<View3DInteractionItem>& expected = expectedManifest();

    // 九项恒定：少于＝清单缩水（阶段 B 范围被砍）；多于＝清单外扩面
    // （未走单元卡修订的"顺手"扩面——契约头注释明令禁止）。
    ASSERT_EQ(manifest.size(), expected.size())
        << "UX-11 清单项数漂移——须升单元卡修订并同步本用例";
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_STREQ(manifest[i].id, expected[i].id)
            << "第 " << i << " 项 id 漂移（登记序＝需求条目序，禁止重排/改名）";
        EXPECT_STREQ(manifest[i].label, expected[i].label)
            << "第 " << i << " 项 label 漂移（呈现文案随契约冻结）";
    }
}

/**
 * id 词法纪律：非空、点分段＋小写蛇形段（小写字母/数字，段内下划线分词、
 * 点分段——契约头结构体注释登记的词法；与 §7.1 命令 id 同族但独立词表）、
 * 全表唯一（唯一性＝实现/测试追溯键不歧义）。
 */
TEST(View3DContract, ManifestIdsWellFormedAndUnique_UX11)
{
    IRD_TEST_INFO("UX-11", {}, std::nullopt);
    const std::vector<View3DInteractionItem>& manifest = sdurws::ird::ui::view3DInteractionManifest();
    ASSERT_FALSE(manifest.empty()) << "清单为空＝登记面缺失";
    for (std::size_t i = 0; i < manifest.size(); ++i) {
        const std::string id = manifest[i].id;
        EXPECT_FALSE(id.empty()) << "第 " << i << " 项 id 为空";
        EXPECT_FALSE(manifest[i].label == nullptr || manifest[i].label[0] == '\0')
            << "第 " << i << " 项 label 为空（呈现面缺文案）";
        for (const char c : id) {
            // 词法：小写字母/数字＋段内下划线分词＋点分段分隔符（契约头登记）。
            EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_')
                << "id 含非法字符 '" << c << "'（点分段＋小写蛇形段纪律）: " << id;
        }
        EXPECT_NE(id.front(), '.') << "id 以点开头: " << id;
        EXPECT_NE(id.back(), '.') << "id 以点结尾: " << id;
        for (std::size_t j = i + 1; j < manifest.size(); ++j) {
            EXPECT_NE(id, std::string(manifest[j].id))
                << "id 重复（追溯键歧义）: " << id;
        }
    }
}

// =====================================================================
// KIN-06/AT-04 会话姿态语义钉住（acceptance 2）
// =====================================================================

/**
 * 四位语义逐位冻结（acceptance 2：点回写仅改会话姿态——不修改设计模型、
 * 不产生修订、不触发结果失效）。任一位翻转即 KIN-06/AT-04 违例，测试失败
 * ＝必须走需求变更（REQUIREMENTS 修订）而非代码私改——这正是"钉住"语义。
 */
TEST(View3DContract, SessionPoseContractPinned_KIN06_AT04)
{
    IRD_TEST_INFO("KIN-06", {"AT-04"}, std::nullopt);
    const View3DSessionPoseContract contract = sdurws::ird::ui::view3DSessionPoseContract();
    EXPECT_TRUE(contract.sessionStateOnly)
        << "点回写必须只是会话级 UI 状态（KIN-06：只改变会话姿态）";
    EXPECT_FALSE(contract.writesDesignModel)
        << "点回写不得修改设计模型（KIN-06 原文）";
    EXPECT_FALSE(contract.producesRevision)
        << "点回写不得产生修订（AT-04：预览不产生项目修订，显式应用经命令端口）";
    EXPECT_FALSE(contract.invalidatesResults)
        << "点回写不得触发结果失效（KIN-06 原文/CON-02 当前性正交）";
}

}  // namespace
