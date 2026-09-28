/**
 * @file   HostMigrationModuleTest.cpp
 * @brief  宿主迁移三接入面的模块接线测试（**集成模式专属 gating TU**）——
 *         ModelingUiModule::sharedSurfaceProviders 惰性构造的对账面。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-13-T20.json acceptance 1（装配缝出线
 *     ——树/页面供给值可注册进共享模型整体重建通过）；
 *   - B1-SPEC §5.1（三接入面）；units/modeling.md §9.7.5（装配缝语义：
 *     惰性构造缓存、须在 createPanel 之后调用——无面板形态＝编辑出口
 *     缺位的诚实降级）。
 *
 * ★ gating 依据（ReadinessTest/PluginModuleT03BTest 先例同款）：被测类型
 *   ModelingUiModule 持有 policy 行程评估器符号（makeJointLimitEvaluator
 *   ——policy/JointLimits.cpp 仅集成模式编译），冒烟模式本 TU 不编入；
 *   三接入面本体的九例见 HostMigrationTest.cpp（无条件编入——冒烟可用）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <memory>

#include "plugin/ModelingUiModule.hpp"             // 被测模块接线面（policy 符号持有者——gating 依据）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp> // ui::ProjectTreeModel（共享树模型）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

/**
 * 契约 WP-13-T20 acceptance 1（装配缝）：ModelingUiModule 的三接入面接线
 * （sharedSurfaceProviders 惰性构造）——供给值与直接构造同语义，可注册
 * 进共享树模型整体重建通过；重复调用返回同一实例（shared_ptr 稳定地址）。
 */
TEST(HostMigrationModuleWiring, SharedSurfaceProvidersFeedSharedTree)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"}, std::vector<std::string>{"B1-ACC-1"});
    ModelingUiModule module;  // 无面板形态（createPanel 未调——诚实降级装配）
    module.seedTemplateSession();  // 会话种子（generic-6r 真实草稿）

    const auto handles = module.sharedSurfaceProviders();
    EXPECT_EQ(handles.treeNodes->domainKey(), "modeling");
    EXPECT_EQ(handles.propertyPages->domainKey(), "modeling");
    // 重复调用返回同一实例（惰性缓存——shared_ptr 稳定地址）。
    const auto again = module.sharedSurfaceProviders();
    EXPECT_TRUE(again.treeNodes == handles.treeNodes);
    EXPECT_TRUE(again.propertyPages == handles.propertyPages);

    // 注册进共享树模型→重建通过（模块工作集现取入口与供给面的对账）。
    ui::ProjectTreeModel model;
    model.addProvider(handles.treeNodes);
    const ui::TreeRebuildReport report = model.rebuild();
    ASSERT_TRUE(report.ok) << "reason=" << report.reason;

    // 关节页问答可用（无面板＝编辑出口缺位的纯呈现形态——诚实降级）。
    {
        // 模块工作集经种子填充——取关节身份经树模型（供给序＝关节序在先）。
        const auto ids = model.nodesInGroup(ui::ProjectTreeGroup::ModelingObjects);
        ASSERT_FALSE(ids.empty());
        const auto page = handles.propertyPages->commonFieldsPage(ids.front());
        ASSERT_TRUE(page.has_value());  // 模板首节点＝关节 1（供给序先关节链）
        EXPECT_EQ(page->title, std::string("关节常用参数"));
        EXPECT_EQ(page->editOutlet, nullptr);  // 无面板＝编辑通道缺位（纯呈现）
    }
}
