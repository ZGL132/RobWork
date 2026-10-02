/**
 * @file   PluginRegistrarChainTest.cpp
 * @brief  F-421 真链路回归（UI-T41 A1）——真实 modeling 装配描述符经
 *         ui IPluginUiRegistrar 全程登记（消账"registrar 真链路零测试
 *         覆盖"缺口——WP-24-T03 验收 attempt 1 阻断 B-1 的返工验证面）。
 *
 * 契约锚：ui.md §10.9（校验序四值——白名单→重复→描述符合法性→Ok）；
 * §11.3（失败隔离）；modeling.md §9.7.3（十条连字符命令 id 词表）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>  // createModelingPluginAssembly（真实描述符）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>           // createPluginUiRegistrar/RegistrationOutcome

namespace {

/// 真链路：真实 modeling 描述符→registerPluginUi→Ok＋报告计数（F-421 修复
/// 建议②原文——ok=1/panels=1/commands=10；连字符 id 全部过句法校验）。
TEST(PluginRegistrarChain, ModelingDescriptorRegistersOkWithTenCommands)
{
    auto assembly = sdurws::ird::modeling::createModelingPluginAssembly();
    auto registrar = sdurws::ird::ui::createPluginUiRegistrar({"modeling"});

    const sdurws::ird::ui::RegistrationOutcome outcome = registrar->registerPluginUi(
        assembly.descriptor, *assembly.module);
    EXPECT_EQ(outcome, sdurws::ird::ui::RegistrationOutcome::Ok);

    const std::vector<sdurws::ird::ui::PluginAssemblyReport> reports =
        registrar->assemblyReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports.front().pluginId, "modeling");
    EXPECT_TRUE(reports.front().ok);
    EXPECT_EQ(reports.front().panelsLoaded, 1u);
    EXPECT_EQ(reports.front().commandsRegistered, 10u);
}

}  // namespace
