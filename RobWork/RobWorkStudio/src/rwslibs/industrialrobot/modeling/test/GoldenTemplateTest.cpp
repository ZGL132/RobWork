/**
 * @file   GoldenTemplateTest.cpp
 * @brief  六轴模板黄金参数全链用例组（MdlGoldenTemplate）——契约
 *         tasks/foundation/WP-13-T16.json acceptance 2/3 的具名自证：
 *
 *   T-MDL-1 表值黄金锁定（V-01 参数入库面——D-MDL-7/AT-20 基线）：
 *     mdl-template-6r 数据集 expected/six-axis-defaults.json（§5.1 表值
 *     手工转写——文档→数据→实现的独立核对方向）反查
 *     sixAxisTemplateDefaults() 逐格位级比对（第 12 项精确等值口径）
 *   V-01 草稿面（AT-01）：createDraft(generic-6r) 产物与数据集草稿期望
 *     逐项一致——7 连杆/6 关节闭合（I-MDL-1）、命名种子、地面预设零位
 *     （MDL-22 V15-04）、J6 行程恰 4π 阈值边界、材料种子钢 7850 kg/m³；
 *     应用（命令 prepare）全链随契约测试面 GoldenProjectPeerContractTest
 *     收口（policy ④端口为集成模式面——本文件保持两模式可编译）
 *   V-02（P-03/O-27/AT-20 建模侧）：七轴模板登记不启用——enabled=false、
 *     创建入口阻止（TemplateDisabled＋MDL-TEMPLATE-DISABLED 提示恰一条）、
 *     无半成品、不静默替换六轴
 *
 * 设计依据：units/modeling.md §5.1/§10.1/§10.2（V-01/V-02 行）、§14.4
 * D-MDL-7；需求 MDL-01/MDL-04/MDL-22、AT-01/AT-20。
 *
 * 两模式均编译：被测面（Template 工厂＋sixAxisTemplateDefaults）为纯函数
 * 服务，零命令/策略依赖（TemplateTest 同款口径）；命令应用面见契约测试。
 */

#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird;            // 嵌套单元名可见（runtime::/core:: 前缀解析——BasePlacementEditTest 同款）
using namespace sdurws::ird::modeling;  // NOLINT——被测契约面直用

// IRD_EXPECT_IDENTICAL 展开的 checkIdentical 以未限定名查找——本 TU 在
// 匿名命名空间内使用，须显式引入（ContractSuiteTest 同款注）。
using tk::checkIdentical;

/// 读取数据集文件全文（读失败显性失败——黄金资产损坏不得静默跳过）。
std::string readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << p.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 装载 mdl-template-6r 黄金数据集（装载失败＝数据资产缺陷，显性失败）。
tk::GoldenDataset loadTemplateDataset()
{
    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-template-6r", "1.0.0"}))
        << "mdl-template-6r 装载失败（数据集非法级——§7.2）";
    return ds;
}

/// 装载并解析期望文件（expected/six-axis-defaults.json——生成脚本产物）。
tk::JsonValue loadTemplateExpected(const tk::GoldenDataset& ds)
{
    return tk::parseJson(readFile(
        ds.resolveExpected("expected/six-axis-defaults.json")));
}

/// JSON 取串（缺字段即失败后返回空——数据集域内 schema 契约）。
std::string jStr(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return {};
    }
    return v->text;
}

/// JSON 取数（缺字段即失败后返回 0——同上）。
double jNum(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return 0.0;
    }
    return v->number;
}

}  // namespace

// =====================================================================
// ACC3（V-01 参数入库面）：T-MDL-1 表值黄金锁定
// =====================================================================

/**
 * @brief T-MDL-1 六行表值逐格锁定（AT-20/D-MDL-7 基线）：数据集期望
 *        （单元卡 §5.1 表值的手工转写——独立核对方向）反查
 *        sixAxisTemplateDefaults()：类型 token／轴线三分量／零位偏置／
 *        限位区间／速度／加速度逐格比对（数值位级一致——设计默认值为
 *        字面常量，附录 D 第 12 项精确等值口径，无容差通道）。
 */
TEST(MdlGoldenTemplate, Tmdl1GoldenParameterLock_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"},
                  std::vector<std::string>{"AT-20"},
                  tk::DatasetRef{"mdl-template-6r", "1.0.0"});

    const tk::GoldenDataset ds = loadTemplateDataset();
    ASSERT_TRUE(ds.manifest().datasetId == "mdl-template-6r");
    const tk::JsonValue expected = loadTemplateExpected(ds);
    const tk::JsonValue* axisDefaults = expected.find("expected");
    ASSERT_NE(axisDefaults, nullptr);
    const tk::JsonValue* rows = axisDefaults->find("axisDefaults");
    ASSERT_NE(rows, nullptr) << "期望文件缺 axisDefaults（数据集结构漂移）";
    ASSERT_EQ(rows->items.size(), std::size_t{6}) << "T-MDL-1 必须六行";

    // 被测面：表 T-MDL-1 载体（每次调用返回同值新数组——纯函数）。
    const std::array<SixAxisJointSpec, 6> actual = sixAxisTemplateDefaults();

    for (std::size_t i = 0; i < 6; ++i) {
        const tk::JsonValue& row = rows->items[i];
        const std::string label = "T-MDL-1 第 " + std::to_string(i + 1) + " 行";
        // 类型：六行全 Revolute（token 精确等值——第 12 项）。
        IRD_EXPECT_IDENTICAL("template.axis[" + std::to_string(i) + "].type",
                             std::string(jointTypeToken(actual[i].type)),
                             jStr(row, "type"));
        // 轴线三分量（连杆系下单位轴，无量纲——位级）。
        const tk::JsonValue* axis = row.find("axis");
        ASSERT_NE(axis, nullptr);
        ASSERT_EQ(axis->items.size(), std::size_t{3});
        for (int c = 0; c < 3; ++c) {
            EXPECT_DOUBLE_EQ(actual[i].axis[c], axis->items[static_cast<std::size_t>(c)].number)
                << label << " 轴线分量 " << c;
        }
        // 零位偏置（rad——表值全 0）。
        EXPECT_DOUBLE_EQ(actual[i].zeroOffset, jNum(row, "zeroOffset"))
            << label << " 零位偏置";
        // 限位 {qmin,qmax}（rad——位级；J6 行程恰 4π 阈值边界在下游用例）。
        const tk::JsonValue* bounds = row.find("bounds");
        ASSERT_NE(bounds, nullptr);
        ASSERT_EQ(bounds->items.size(), std::size_t{2});
        EXPECT_DOUBLE_EQ(actual[i].bounds.first, bounds->items[0].number)
            << label << " qmin";
        EXPECT_DOUBLE_EQ(actual[i].bounds.second, bounds->items[1].number)
            << label << " qmax";
        // 速度/加速度（rad·s⁻¹／rad·s⁻²——SixAxisJointSpec 数值面随本表
        // 锁定；schema 落点说明见 Template.hpp SixAxisJointSpec 注）。
        EXPECT_DOUBLE_EQ(actual[i].maxVelocity, jNum(row, "maxVelocity"))
            << label << " 最大速度";
        EXPECT_DOUBLE_EQ(actual[i].maxAcceleration, jNum(row, "maxAcceleration"))
            << label << " 最大加速度";
    }
}

// =====================================================================
// ACC3（V-01 草稿面）：createDraft 产物与黄金期望一致
// =====================================================================

/**
 * @brief V-01 草稿面（AT-01/MDL-01/MDL-22）：按数据集 inputs 的创建请求
 *        调用 createDraft——产物与草稿期望逐项一致：呈现名/权威模式/
 *        关节连杆计数与命名种子/类型序列/地面预设＋零位（V15-04）/J6 行程
 *        恰 4π（附录 D 第 11 项阈值边界——行程≤阈值含于合规侧）/材料种子
 *        钢 7850 kg/m³（§5.3 密度表单点权威）；I-MDL-1~12 全量通过
 *        （§9.4.2 @post）。
 */
TEST(MdlGoldenTemplate, GoldenDraftMatchesDatasetExpectations_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-04", "MDL-22"},
                  std::vector<std::string>{"AT-01"},
                  tk::DatasetRef{"mdl-template-6r", "1.0.0"});

    const tk::GoldenDataset ds = loadTemplateDataset();
    const tk::JsonValue request
        = tk::parseJson(readFile(ds.resolveInput("inputs/template-request.json")));
    const tk::JsonValue expectedRoot = loadTemplateExpected(ds);
    const tk::JsonValue* expected = expectedRoot.find("expected");
    ASSERT_NE(expected, nullptr);
    const tk::JsonValue* draftExp = expected->find("draftExpectations");
    ASSERT_NE(draftExp, nullptr) << "期望文件缺 draftExpectations";

    // 创建请求（数据集输入面——templateId/preset/localName）。
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        jStr(request, "templateId"), runtime::InstallationPresetToken::Ground,
        jStr(request, "localName"), diags);
    ASSERT_TRUE(outcome.ok()) << "黄金创建请求必须成功（数据集契约）";
    EXPECT_TRUE(diags.empty()) << "成功路径不追加诊断（§9.4.2 @post）";
    const ModelingWorkingSet& ws = outcome.get();

    // 呈现名＝用户基名；权威模式＝模板登记值。
    IRD_EXPECT_IDENTICAL("draft.displayName", ws.design.displayName,
                         jStr(*draftExp, "displayName"));
    IRD_EXPECT_IDENTICAL("draft.authority", std::string(authorityModeToken(ws.design.authority)),
                         jStr(*draftExp, "authority"));
    // 计数关系（I-MDL-1：links==joints+1——7 连杆 6 关节闭合）。
    EXPECT_EQ(ws.design.joints.size(),
              static_cast<std::size_t>(jNum(*draftExp, "jointCount")));
    EXPECT_EQ(ws.design.links.size(),
              static_cast<std::size_t>(jNum(*draftExp, "linkCount")));
    // 命名种子（j<序>/base/l<序>——设计默认值；I-MDL-2 作用域唯一）。
    const tk::JsonValue* jointNames = draftExp->find("jointNames");
    ASSERT_NE(jointNames, nullptr);
    for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
        IRD_EXPECT_IDENTICAL("draft.joints[" + std::to_string(i) + "].localName",
                             ws.design.joints[i].localName,
                             jointNames->items[i].text);
    }
    const tk::JsonValue* linkNames = draftExp->find("linkNames");
    ASSERT_NE(linkNames, nullptr);
    for (std::size_t i = 0; i < ws.design.links.size(); ++i) {
        IRD_EXPECT_IDENTICAL("draft.links[" + std::to_string(i) + "].localName",
                             ws.design.links[i].localName,
                             linkNames->items[i].text);
    }
    // 类型序列（六行全 Revolute）。
    const tk::JsonValue* jointTypes = draftExp->find("jointTypes");
    ASSERT_NE(jointTypes, nullptr);
    for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
        IRD_EXPECT_IDENTICAL("draft.joints[" + std::to_string(i) + "].type",
                             std::string(jointTypeToken(ws.design.joints[i].type)),
                             jointTypes->items[i].text);
    }
    // BasePlacement：预设地面＋零位（MDL-22 V15-04——默认值在模板层填入
    // 并带模板来源标记；customEaa 保持 NotProvided——非 Custom 不适用）。
    const tk::JsonValue* baseExp = draftExp->find("basePlacement");
    ASSERT_NE(baseExp, nullptr);
    IRD_EXPECT_IDENTICAL("draft.basePlacement.preset",
                         ws.design.basePlacement.preset == runtime::InstallationPresetToken::Ground
                             ? std::string("ground") : std::string("non-ground"),
                         jStr(*baseExp, "preset"));
    for (int c = 0; c < 3; ++c) {
        ASSERT_TRUE(ws.design.basePlacement.basePosition.tryValue().has_value());
        EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[c],
                         baseExp->find("basePosition")->items[static_cast<std::size_t>(c)].number)
            << "basePosition 分量 " << c << "（单位 m，世界系）";
    }
    // J6 行程恰 4π（|qmax−qmin|＝附录 D 第 11 项默认阈值——边界用例）。
    const double j6Travel = ws.design.joints[5].bounds.value().second
                            - ws.design.joints[5].bounds.value().first;
    EXPECT_DOUBLE_EQ(j6Travel, jNum(*draftExp, "j6TravelRad"))
        << "J6 行程（单位 rad）";
    // 材料种子：钢 7850 kg/m³（§5.3 密度默认表单点权威——不写第二处
    // 字面量；golden 期望值为表值转写）。
    const tk::JsonValue* materialExp = draftExp->find("materialSeed");
    ASSERT_NE(materialExp, nullptr);
    bool materialFound = false;
    for (const LinkEntry& link : ws.design.links) {
        if (link.body.material.has_value()) {
            materialFound = true;
            IRD_EXPECT_IDENTICAL("draft.material.materialId",
                                 link.body.material->materialId,
                                 jStr(*materialExp, "materialId"));
            ASSERT_TRUE(link.body.material->density.tryValue().has_value());
            EXPECT_DOUBLE_EQ(link.body.material->density.value(),
                             jNum(*materialExp, "densityKgPerM3"))
                << "密度（单位 kg/m^3）";
        }
    }
    EXPECT_TRUE(materialFound) << "连杆材料种子必须存在（§5.1 材料默认句）";
    // workingRange 全行 NotApplicable（Revolute 不适用——表 T-MDL-1 该列）。
    IRD_EXPECT_IDENTICAL("draft.workingRangeState",
                         std::string("not-applicable"),
                         jStr(*draftExp, "workingRangeState"));
    for (const JointEntry& joint : ws.design.joints) {
        EXPECT_EQ(joint.workingRange.state(), core::FieldState::NotApplicable);
    }
    // 不变量全量通过（§9.4.2 @post：产出满足 I-MDL-1~12）。
    EXPECT_TRUE(checkInvariants(ws.design).empty());
}

// =====================================================================
// ACC3（V-02）：七轴模板登记不启用（P-03/O-27——AT-20 建模侧）
// =====================================================================

/**
 * @brief V-02（AT-20 建模侧）：清单中 generic-7r 行 enabled=false（P-03
 *        冻结前仅登记不启用）；创建入口阻止——TemplateDisabled 值面错误
 *        ＋MDL-TEMPLATE-DISABLED 提示诊断恰一条；无半成品（错误态无草稿）、
 *        不静默替换为六轴（诊断不指向 generic-6r、返回值不可达六轴工作集）。
 */
TEST(MdlGoldenTemplate, SevenAxisRegisteredNotEnabled_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-02"},
                  std::vector<std::string>{"AT-20"},
                  tk::DatasetRef{"mdl-template-6r", "1.0.0"});

    const tk::GoldenDataset ds = loadTemplateDataset();
    const tk::JsonValue expectedRoot = loadTemplateExpected(ds);
    const tk::JsonValue* seven = expectedRoot.find("sevenAxisExpectations");
    ASSERT_NE(seven, nullptr) << "期望文件缺 sevenAxisExpectations";
    const tk::JsonValue* descriptor = seven->find("descriptor");
    ASSERT_NE(descriptor, nullptr);
    const tk::JsonValue* createExp = seven->find("createDraftExpected");
    ASSERT_NE(createExp, nullptr);

    const RobotDesignTemplateFactory factory;
    // 清单面：generic-7r 在册且 enabled=false（可展示——创建入口阻止）。
    const std::vector<TemplateDescriptor> catalog = factory.listTemplates();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                 [](const TemplateDescriptor& d) {
                                     return d.templateId == "generic-7r";
                                 });
    ASSERT_NE(it, catalog.end()) << "七轴模板必须登记（P-03 仅登记不启用）";
    IRD_EXPECT_IDENTICAL("template.7r.displayName", it->displayName,
                         jStr(*descriptor, "displayName"));
    EXPECT_FALSE(it->enabled) << "P-03 冻结前 enabled 必须为 false";
    EXPECT_EQ(it->axisCount.has_value() && it->axisCount.value() == 7, true);

    // 创建入口阻止（TemplateDisabled 值面＋提示诊断恰一条）。
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome blocked = factory.createDraft(
        std::string(kTemplateIdGeneric7R),
        runtime::InstallationPresetToken::Ground, "golden7r", diags);
    ASSERT_FALSE(blocked.ok()) << "disabled 模板不得产出草稿（无半成品）";
    IRD_EXPECT_IDENTICAL("template.7r.outcomeError",
                         std::string(modelingErrorCodeToken(blocked.error().code)),
                         jStr(*createExp, "outcomeError"));
    EXPECT_EQ(diags.size(), std::size_t{1});
    IRD_EXPECT_IDENTICAL("template.7r.diagnosticCode",
                         diags.empty() ? std::string{} : diags.front().code,
                         jStr(*createExp, "diagnosticCode"));
    // 不静默替换六轴：返回值未携带任何工作集（错误态语义），诊断不指向
    // generic-6r（无替换轨迹）。
    for (const core::DiagnosticRecord& r : diags) {
        EXPECT_EQ(r.code.find("generic-6r"), std::string::npos)
            << "禁止静默替换六轴（V-02）";
    }
}
