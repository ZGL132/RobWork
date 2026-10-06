/**
 * @file   PluginRegistrationContractTest.cpp
 * @brief  workflow 插件"最小可注册"契约用例组（WfPluginRegistration）——
 *         以真实 ui 注册端口（IPluginUiRegistrar）验证装配产物可通过
 *         §10.9 登记校验（任务契约 WP-22-T02 acceptance 1"_plugin 目标
 *         最小可注册实现"的具名自证面）。
 *
 * 设计依据：
 *   - units/ui.md §10.9（registerPluginUi 校验序：白名单→重复→描述符
 *     合法性；四值 RegistrationOutcome；PluginAssemblyReport 字段）、
 *     §11.1（白名单八 token 编译期词表——"workflow" 第 8 token；顺序即
 *     装配序）
 *   - units/workflow.md §3.2（_plugin＝向导/首页/比较视图/确认对话框宿主
 *     面——T02 最小可注册形态：三能力 false、panels/commands 空）
 *   - 先例：ui/src/PluginUiRegistrar.cpp 的校验序实现为判定权威；本测试
 *     是其黑盒消费（ui 单元自测覆盖设施行为，本测试钉 workflow 侧产物
 *     与 §11.1 白名单的对接面）
 *   - 任务契约 tasks/foundation/WP-22-T02.json acceptance 1（创建
 *     _plugin/_test/_contract_test 目标）
 *
 * 线程模型：全部用例单线程（注册端口契约——装配线程调用）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <memory>
#include <string>
#include <vector>

#include <sdurws/ird/ui/ICommandRegistry.hpp>    // ui::CommandDescriptor 完整类型（PluginUiDescriptor.commands 的
                                                 //   vector 析构在本 TU 实例化——见 assembly 门面头同款注）
#include <sdurws/ird/ui/IPluginUiModule.hpp>      // ui::IPluginUiModule（module 成员完整类型）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>   // ui::IPluginUiRegistrar/createPluginUiRegistrar/RegistrationOutcome
#include <sdurws/ird/ui/UiTypes.hpp>              // ui::StageId（七值词表——SA-12 零新增枚举的核对面）
#include <sdurws/ird/workflow/WorkflowPluginAssembly.hpp>  // workflow 装配门面（plugin 目标 PUBLIC 面——同单元消费）

// ---- TU 作用域说明（为什么包进单元命名空间——先读）--------------------
// 全部用例写在 namespace sdurws::ird::workflow 内：非限定名 `ui::` 的查找
// 须经过 ird 层才可解析（sdurws::ird::ui——ui 词表/注册端口的真实落点）；
// 全局作用域的非限定 `ui::` 不可达（标准作用域语义——C2653/C2065 首次
// 构建实测）。modeling/requirements/kinematics 各插件 TU 同先例（自身代码
// 全部位于 namespace sdurws::ird::<unit> 嵌套层）。gtest 的 TEST 宏在
// 命名空间内展开合法（测试类/函数随之落入本单元命名空间，不影响注册）。
namespace sdurws::ird::workflow {

namespace {

/**
 * @brief ui.md §11.1 白名单八 token 编译期词表（顺序即装配序——与本
 *        测试构造的注册端口白名单逐字同源；"workflow" 为第 8 个占位
 *        token，本单元落位使其从占位转为真实注册形态）。
 */
std::vector<std::string> pluginWhitelist()
{
    return {"modeling", "requirements", "kinematics", "trajectory",
            "dynamics", "selection",   "optimization", "workflow"};
}

}  // namespace

/**
 * 最小可注册（acceptance 1）：createWorkflowPluginAssembly 产物经真实
 * 注册端口登记，校验序三查全过（白名单在册→无重复→描述符合法）返回
 * Ok，装配报告恰一条且 ok=true——"可注册"的可执行证明（构建零错误只
 * 证目标成库，本用例证登记契约面成立）。
 */
TEST(WfPluginRegistration, AssemblyRegistersOkOnWhitelist_WP22T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "UX-14"},
                  std::vector<std::string>{});

    // 真实注册端口（实现封闭 ui 库内——工厂创建；白名单由装配层固定，
    // 本测试以 §11.1 八 token 词表充当装配层角色）。
    auto registrar = ui::createPluginUiRegistrar(pluginWhitelist());
    ASSERT_NE(registrar, nullptr) << "注册端口工厂返回空（ui 库装配违约）";

    auto assembly = workflow::createWorkflowPluginAssembly();
    ASSERT_NE(assembly.module, nullptr) << "装配产物缺模块实例";
    EXPECT_EQ(assembly.descriptor.pluginId, "workflow")
        << "pluginId 应为白名单第 8 token（§11.1）";

    // 登记动作：§10.9 校验序（白名单→重复→合法性）走真实实现。
    const auto outcome = registrar->registerPluginUi(assembly.descriptor,
                                                     *assembly.module);
    EXPECT_EQ(outcome, ui::RegistrationOutcome::Ok)
        << "最小可注册失败——校验序三查应全过（pluginId 在白名单＋无重复"
           "＋pluginId/titleKey/stages 非空）";

    // 装配报告（§10.9——UX-14 关于页数据源）：恰一条、pluginId 对位、
    // ok=true、面板/命令计数与描述符空集一致（T02 最小形态）。
    const auto reports = registrar->assemblyReports();
    ASSERT_EQ(reports.size(), 1U) << "装配报告应恰一条（装配期一次）";
    EXPECT_EQ(reports[0].pluginId, "workflow");
    EXPECT_TRUE(reports[0].ok) << "装配报告 ok 应为 true";
    EXPECT_EQ(reports[0].panelsLoaded, 0U)
        << "T02 无面板登记（panels 空——向导/首页随 WP-22-T04+）";
    EXPECT_EQ(reports[0].commandsRegistered, 0U)
        << "T02 无命令登记（commands 空——最小命令集随 WP-22-T12）";
    EXPECT_TRUE(reports[0].failureDiagnostics.empty())
        << "装配成功态不应携带失败诊断";
}

/**
 * 描述符值与单元卡口径一致（acceptance 1——"最小"的精确边界核对）：
 * 三能力声明全 false、panels/commands 空（不预建界面占位——NFR-MNT-04）、
 * stages 恰为七阶段全表且顺序＝StageId 枚举序（UX-12 冻结序；workflow
 * 零新增枚举——SA-12/D-WF-4 词表复用纪律）、titleKey 走 ui §3.5 键族
 * （plugin.<id>.title——零内部插件名呈现，UX-02）。
 */
TEST(WfPluginRegistration, DescriptorMatchesMinimalForm_WP22T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-12", "UX-02"},
                  std::vector<std::string>{});

    const auto assembly = workflow::createWorkflowPluginAssembly();
    const auto& d = assembly.descriptor;

    EXPECT_EQ(d.titleKey, "plugin.workflow.title")
        << "titleKey 应走 ui §3.5 键族 plugin.<id>.title（UI-T10 冻结八键）";

    // 三能力全 false（§10.9"描述性，非判定性"——T02 无面板/投影/命令）。
    EXPECT_FALSE(d.capabilities.providesStagePanel)
        << "T02 无阶段面板（向导/首页宿主面随 WP-22-T04+）";
    EXPECT_FALSE(d.capabilities.providesReadonlyProjection)
        << "T02 无域投影（编排单元恒无 §6.5 域行——各域自报）";
    EXPECT_FALSE(d.capabilities.registersCommands)
        << "T02 无命令（最小命令集随 WP-22-T12——卡 §8.3）";
    EXPECT_TRUE(d.commands.empty()) << "commands 应为空（不预建占位）";
    EXPECT_TRUE(d.panels.empty()) << "panels 应为空（不预建占位）";

    // stages＝七阶段全表、顺序＝枚举序（§6.4 冻结呈现序）。
    ASSERT_EQ(d.stages.size(), 7U) << "stages 应覆盖七阶段全表";
    EXPECT_EQ(d.stages[0], ui::StageId::Modeling);
    EXPECT_EQ(d.stages[1], ui::StageId::Requirements);
    EXPECT_EQ(d.stages[2], ui::StageId::Kinematics);
    EXPECT_EQ(d.stages[3], ui::StageId::TrajectoryDynamics);
    EXPECT_EQ(d.stages[4], ui::StageId::Selection);
    EXPECT_EQ(d.stages[5], ui::StageId::Optimization);
    EXPECT_EQ(d.stages[6], ui::StageId::Reporting);
}

/**
 * 校验序拒绝面（§10.9 四值词表的负路径——黑盒复核注册端口的白名单与
 * 重复判定对本单元装配产物的行为）：白名单外 pluginId → NotWhitelisted
 * （SA-01 静态白名单）；同描述符二次登记 → DuplicatePlugin（§11.1"每
 * 插件恰好一次"）。workflow 侧零特判——全部走 ui 设施通用路径（SA-16
 * 命令/注册设施权威归 ui 的对偶面）。
 */
TEST(WfPluginRegistration, WhitelistAndDuplicateRejected_WP22T02_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-01", "NFR-SEC-04"},
                  std::vector<std::string>{});

    auto registrar = ui::createPluginUiRegistrar(pluginWhitelist());
    ASSERT_NE(registrar, nullptr);

    // 负路径①：白名单外注册（以真实装配产物为基底，仅改 pluginId/
    // titleKey 为 §11.1 词表外的值——stages 等其余字段保持合法，隔离出
    // "白名单判定"这一被测变量）。
    auto outsider = workflow::createWorkflowPluginAssembly();
    outsider.descriptor.pluginId = "not-a-whitelisted-plugin";
    outsider.descriptor.titleKey = "plugin.not-a-whitelisted-plugin.title";
    EXPECT_EQ(registrar->registerPluginUi(outsider.descriptor, *outsider.module),
              ui::RegistrationOutcome::NotWhitelisted)
        << "白名单外 pluginId 应被拒绝（SA-01 静态白名单）";

    // 负路径②：同 pluginId 二次登记（第一次合法登记在前——§11.1 每插件
    // 恰好一次）。
    auto first = workflow::createWorkflowPluginAssembly();
    ASSERT_EQ(registrar->registerPluginUi(first.descriptor, *first.module),
              ui::RegistrationOutcome::Ok)
        << "前置：合法登记应成功（负路径②的前置态）";
    auto second = workflow::createWorkflowPluginAssembly();
    EXPECT_EQ(registrar->registerPluginUi(second.descriptor, *second.module),
              ui::RegistrationOutcome::DuplicatePlugin)
        << "同 pluginId 二次登记应被拒绝（§11.1 每插件恰好一次）";
}

}  // namespace sdurws::ird::workflow
