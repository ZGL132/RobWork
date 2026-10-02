/**
 * @file   ModelingCommandFlowsTest.cpp
 * @brief  建模域命令流测试（UI-T41 批次C——estimate-properties 与
 *         generate-placeholder-geometry 两流程的替身应答驱动验证；
 *         ModelingDialogHost 测试缝＝FakeHost 预置应答，零真实模态）。
 *
 * 契约锚：modeling.md §9.7.3（两命令语义）、§5.2（占位圆柱）、§5.3
 * （物性估算公式表与来源标记）；段元前置＝驱动关节原点非零（模板种子
 * 恒位姿→零长拒绝的真实语义如实覆盖）。
 */

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <string>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/core/Provenance.hpp>       // SourcedValue/ValueProvenance（写回断言）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // GeometryKind/JointPose（注入夹具）
#include <sdurws/ird/modeling/Template.hpp>     // RobotDesignTemplateFactory（六轴草稿夹具）
#include "plugin/ModelingCommandFlows.hpp"      // executeModelingCommand（被测流）
#include "plugin/ModelingUiModule.hpp"          // ModuleSessionState（会话态入参）

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/// generic-6r 草稿便捷创建（PluginPanelTest 同款夹具）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    return factory
        .createDraft(TemplateId{kTemplateIdGeneric6R},
                     runtime::InstallationPresetToken::Ground, "demo", diags)
        .get();
}

/// 预置应答替身（测试缝——六方法全预置，取消路径＝nullopt/false）。
struct FakeHost final : ModelingDialogHost {
    std::optional<int> chooseItemAnswer;        ///< chooseItem 应答（nullopt＝取消）
    bool confirmProceedAnswer = true;           ///< confirmProceed 应答
    bool confirmImportAnswer = true;            ///< confirmImport 应答
    int showInfoCount = 0;                      ///< showInfo 调用计数
    QStringList showInfoTexts;                  ///< showInfo 文本留痕

    std::optional<QString> openFilePath(const QString&, const QString&) override
    {
        return std::nullopt;
    }
    std::optional<QString> saveFilePath(const QString&, const QString&,
                                        const QString&) override
    {
        return std::nullopt;
    }
    std::optional<int> chooseItem(const QString&, const QString&,
                                  const QStringList&) override
    {
        return chooseItemAnswer;
    }
    bool confirmProceed(const QString&, const QString&) override
    {
        return confirmProceedAnswer;
    }
    bool confirmImport(const QString&) override { return confirmImportAnswer; }
    void showInfo(const QString&, const QString& text) override
    {
        ++showInfoCount;
        showInfoTexts << text;
    }
};

/// 会话态＋依赖替身（selectedAnchor 预置＋recompute 计数）。
struct FlowHarness {
    ModuleSessionState session;
    FakeHost host;
    std::optional<core::ObjectId> anchor;
    int recomputeCalls = 0;

    ModelingFlowDeps deps()
    {
        ModelingFlowDeps d;
        d.reseedTemplate = [] {};
        d.recomputeReadiness = [this] { ++recomputeCalls; };
        d.selectedAnchor = [this] { return anchor; };
        return d;
    }
};

/// 注入非零驱动关节原点（模板种子恒位姿＝零段长的真实拒绝语义；测试
/// 前置＝用户已编辑原点——0.3 m 沿 z）。
void seedNonZeroOrigin(ModelingWorkingSet& ws, std::size_t jointIndex)
{
    ws.design.joints[jointIndex].origin =
        core::SourcedValue<JointPose>::provided(
            JointPose(rw::math::Transform3D<double>(
                rw::math::Vector3D<double>(0.0, 0.0, 0.3),
                rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                             0.0, 1.0, 0.0,
                                             0.0, 0.0, 1.0))),
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
}

}  // namespace

// =====================================================================
// estimate-properties（§5.3——段元推导＋公式表＋GeometricEstimate 写回）
// =====================================================================

/// 选中连杆＋材料应答（钢）→物性三元组写入（Provided＋GeometricEstimate
/// 徽标＋公式表标记）＋changes 记录＋就绪重算触发。
TEST(ModelingCommandFlows, EstimateProperties_WritesBackWithEstimateBadge_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-05"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    seedNonZeroOrigin(fx.session.draft, 1);  // 连杆 1 的驱动关节（joints[1]）
    fx.anchor = fx.session.draft.design.links[1].objectId;
    fx.host.chooseItemAnswer = 0;  // 钢（7850 kg/m³）

    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.estimate-properties",
                                       fx.session, fx.deps(), fx.host, summary));
    const LinkEntry& link = fx.session.draft.design.links[1];
    ASSERT_EQ(link.body.mass.state(), core::FieldState::Provided);
    EXPECT_GT(link.body.mass.value(), 0.0) << "实心圆柱估算质量恒正";
    EXPECT_EQ(link.body.mass.provenance().kind,
              core::ProvenanceKind::GeometricEstimate)
        << "估算写回徽标＝GeometricEstimate（§5.3 规则 1）";
    EXPECT_EQ(link.body.mass.provenance().methodTag,
              std::optional<std::string>{"mdl-property-formula/1"})
        << "公式表标记随行（唯一公式表）";
    ASSERT_EQ(link.body.centerOfMass.state(), core::FieldState::Provided);
    ASSERT_EQ(link.body.inertia.state(), core::FieldState::Provided);
    ASSERT_EQ(fx.session.draft.changes.size(), std::size_t{1});
    EXPECT_EQ(fx.recomputeCalls, 1) << "草稿变更后就绪重算恰一次";
}

/// 无选中＝诚实拒绝（false＋指引摘要；不抛——调用侧状态错误面）。
TEST(ModelingCommandFlows, EstimateProperties_NoSelection_HonestRefusal_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.host.chooseItemAnswer = 0;

    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.estimate-properties",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_TRUE(summary.find("选中") != std::string::npos) << "拒绝摘要含操作指引";
    EXPECT_TRUE(fx.session.draft.changes.empty()) << "拒绝路径零草稿变更";
}

/// 用户手填物性的覆盖确认拒绝＝流程取消（原值保留——破坏性覆盖须用户
/// 明示，SA-15 确认流最小形态）。
TEST(ModelingCommandFlows, EstimateProperties_OverwriteUserValuesNeedsConfirm_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-07"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    seedNonZeroOrigin(fx.session.draft, 1);
    fx.anchor = fx.session.draft.design.links[1].objectId;
    fx.host.chooseItemAnswer = 0;
    fx.host.confirmProceedAnswer = false;  // 覆盖确认＝否

    LinkEntry& link = fx.session.draft.design.links[1];
    link.body.mass = core::SourcedValue<double>::provided(
        7.5, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));

    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.estimate-properties",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_EQ(link.body.mass.value(), 7.5) << "取消后用户手填值原样保留";
}

// =====================================================================
// generate-placeholder-geometry（§5.2——占位圆柱＋schema 边界诚实留痕）
// =====================================================================

/// 选中连杆→visual 写入占位圆柱引用（Primitive 类别＋会话作用域键）；资源
/// 清单不登记（schema 边界——清单登记形态随 schema 澄清落位，流程如实留痕）。
TEST(ModelingCommandFlows, PlaceholderGeometry_WritesVisualRef_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-04"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    seedNonZeroOrigin(fx.session.draft, 2);  // 连杆 2 的驱动关节（joints[2]）
    fx.anchor = fx.session.draft.design.links[2].objectId;

    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.generate-placeholder-geometry",
                                       fx.session, fx.deps(), fx.host, summary));
    const LinkEntry& link = fx.session.draft.design.links[2];
    ASSERT_TRUE(link.visual.has_value()) << "visual 几何引用未写入";
    EXPECT_EQ(link.visual->kind, GeometryKind::Primitive) << "占位＝原语类别";
    EXPECT_EQ(link.visual->resourceRefId,
              "placeholder-" + link.localName) << "会话作用域键按连杆名派生";
    EXPECT_TRUE(fx.session.draft.design.resourceManifest.empty()
                || std::none_of(fx.session.draft.design.resourceManifest.begin(),
                                fx.session.draft.design.resourceManifest.end(),
                                [&link](const ResourceRef& r) {
                                    return r.resourceId == link.visual->resourceRefId;
                                }))
        << "占位原语不入资源清单（schema 边界——不伪造条目）";
    ASSERT_EQ(fx.session.draft.changes.size(), std::size_t{1});
    EXPECT_TRUE(fx.session.draft.changes[0].summary.find("schema") != std::string::npos)
        << "变更记录留痕 schema 边界";
    EXPECT_EQ(fx.recomputeCalls, 1);
}

/// 末端法兰连杆（无驱动关节参考）＝诚实拒绝（法兰几何归后续任务）。
TEST(ModelingCommandFlows, PlaceholderGeometry_FlangeLink_Refused_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    const std::size_t flange = fx.session.draft.design.joints.size();  // links 末元素
    fx.anchor = fx.session.draft.design.links[flange].objectId;

    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.generate-placeholder-geometry",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_TRUE(summary.find("法兰") != std::string::npos);
    EXPECT_FALSE(fx.session.draft.design.links[flange].visual.has_value());
}

/// 段元零长（模板种子恒位姿）＝诚实拒绝（零长度圆柱无几何意义——内核
/// makeLinkPlaceholderCylinder 同款前置的流程侧呈现）。
TEST(ModelingCommandFlows, EstimateProperties_ZeroLengthSegment_Refused_UI_T41C)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();  // 恒位姿种子——不注入非零原点
    fx.anchor = fx.session.draft.design.links[0].objectId;
    fx.host.chooseItemAnswer = 0;

    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.estimate-properties",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_TRUE(summary.find("零长度") != std::string::npos)
        << "种子态（恒位姿）拒绝摘要含几何原因";
}
