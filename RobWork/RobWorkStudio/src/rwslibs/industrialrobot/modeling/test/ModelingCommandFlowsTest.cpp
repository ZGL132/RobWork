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

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QString>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <string>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/core/Provenance.hpp>       // SourcedValue/ValueProvenance（写回断言）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // GeometryKind/JointPose（注入夹具）
#include <sdurws/ird/modeling/Template.hpp>     // RobotDesignTemplateFactory（六轴草稿夹具）
#include <sdurws/ird/modeling/Codec.hpp>        // AT-31来源资源随对象编码往返保真。
#include "plugin/ModelingCommandFlows.hpp"      // executeModelingCommand（被测流）
#include "plugin/ModelingUiModule.hpp"          // ModuleSessionState（会话态入参）
#include "plugin/ModelingXacroBridge.hpp"       // WP-13-T24：真实生产展开桥，不能只测内核。

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
    std::optional<QString> openPathAnswer;      ///< openFilePath 应答（nullopt＝取消）
    std::optional<QString> savePathAnswer;      ///< saveFilePath 应答（nullopt＝取消）
    // custom-chain 声明表单应答（UI-T63——nullopt＝取消；nullopt 内值＝六轴
    // 声明预置；declareCalls 计数留痕）。
    std::optional<std::vector<CustomChainJointSpec>> declareAnswer;
    int declareCalls = 0;
    QString lastConfirmText;                    ///< confirmProceed 文本留痕（D1 清单断言）
    std::optional<std::vector<std::pair<std::string,std::string>>> parameterAnswer =
        std::vector<std::pair<std::string,std::string>>{}; ///< 参数应答；nullopt取消。
    std::vector<std::pair<std::string,std::string>> capturedDefaults; ///< 真实源参数默认值。
    std::optional<std::vector<std::pair<std::string,std::string>>> editXacroParameters(
        const std::vector<std::pair<std::string,std::string>>& defaults) override
    {
        capturedDefaults=defaults;
        return parameterAnswer;
    }
    int showInfoCount = 0;                      ///< showInfo 调用计数
    QStringList showInfoTexts;                  ///< showInfo 文本留痕
    QStringList chosenItems;                    ///< F13：核对真实权威保留键选择，不接受旧别名。

    std::optional<QString> openFilePath(const QString&, const QString&) override
    {
        return openPathAnswer;
    }
    std::optional<QString> saveFilePath(const QString&, const QString&,
                                        const QString&) override
    {
        return savePathAnswer;
    }
    std::optional<int> chooseItem(const QString&, const QString&,
                                  const QStringList& items) override
    {
        chosenItems=items;
        return chooseItemAnswer;
    }
    bool confirmProceed(const QString&, const QString& text) override
    {
        lastConfirmText = text;
        return confirmProceedAnswer;
    }
    bool confirmImport(const QString& text) override
    {
        lastConfirmText = text;
        return confirmImportAnswer;
    }
    void showInfo(const QString&, const QString& text) override
    {
        ++showInfoCount;
        showInfoTexts << text;
    }
    std::optional<std::vector<CustomChainJointSpec>> declareCustomChain(
        const QString&, const CustomChainJointSpec&) override
    {
        ++declareCalls;
        return declareAnswer;
    }
};

/// 会话态＋依赖替身（selectedAnchor 预置＋recompute 计数）。
struct FlowHarness {
    ModuleSessionState session;
    FakeHost host;
    std::optional<core::ObjectId> anchor;
    int recomputeCalls = 0;
    /// WC XML 导出回调替身（UI-T56——deps().exportWorkCellXml 注入面；
    /// 默认未接线＝nullptr，测试按用例覆写 exportAnswer/记录调用）。
    bool exportWired = false;
    bool exportAnswer = true;
    int exportCalls = 0;
    std::string exportLastPath;
    // custom-chain 声明重种子替身状态（UI-T63——deps() 内 lambda 经 this
    // 消费；wired=false＝未接线形态）。
    bool reseedCustomWired = false;
    bool reseedCustomAnswer = true;
    int reseedCustomCalls = 0;
    CustomChainDeclaration reseedCustomLast;

    ModelingFlowDeps deps()
    {
        ModelingFlowDeps d;
        d.reseedTemplate = [] {};
        // custom-chain 声明重种子替身（UI-T63——记录声明＋可编程应答/摘要；
        // 状态在 FlowHarness 成员〔deps 按值返回——捕获 this 防悬垂〕；
        // wired=false＝不设置 deps〔flow 侧 null 检查＝装配缺陷 fail-closed
        // 断言面〕）。
        if (reseedCustomWired) {
        d.reseedCustomChain =
            [this](const CustomChainDeclaration& decls, std::string& summary) {
                ++reseedCustomCalls;
                reseedCustomLast = decls;
                if (reseedCustomAnswer) {
                    summary = "已按声明创建 custom-chain 六轴草稿（替身）";
                } else {
                    summary = "声明非法（替身拒绝态）";
                }
                return reseedCustomAnswer;
            };
        }
        d.recomputeReadiness = [this] { ++recomputeCalls; };
        d.selectedAnchor = [this] { return anchor; };
        if (exportWired) {
            d.exportWorkCellXml =
                [this](const std::string& targetPath, std::string& summary) {
                    ++exportCalls;
                    exportLastPath = targetPath;
                    if (exportAnswer) {
                        summary = "WorkCell/DWC XML 已导出（替身）";
                    } else {
                        summary = "尚无已应用修订的编译产物——替身缺席态";
                    }
                    return exportAnswer;
                };
        }
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

// =====================================================================
// UI-T41 批次D：导出清单确认＋回读校验（D1/D2）＋根身份保留（审核 R1）
// =====================================================================

/// D1+D2：export-package 全链——清单化确认（对象计数在场）→导出→包端口
/// 回读校验通过（manifest/SHA/解码全链）；目标路径来自 saveFilePath 替身
/// （QTemporaryDir 隔离——零真实用户目录触碰）。
TEST(ModelingCommandFlows, ExportPackage_ConfirmListAndRoundTripVerify_UI_T41D)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20"}, std::vector<std::string>{});

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("demo.irdbundle"));

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.host.savePathAnswer = target;

    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.export-package",
                                       fx.session, fx.deps(), fx.host, summary));
    // D1：确认文本清单化（对象计数＋原子替换语义在场）。
    EXPECT_TRUE(fx.host.lastConfirmText.contains(QStringLiteral("根对象")))
        << "确认清单缺根对象条目（D1）";
    EXPECT_TRUE(fx.host.lastConfirmText.contains(QStringLiteral("原子替换")));
    // D2：回读校验通过（诚实回报——运行时加载校验归后续任务链）。
    EXPECT_TRUE(summary.find("回读校验") != std::string::npos) << "摘要未含回读校验结论";
    EXPECT_TRUE(summary.find("运行时加载校验归后续") != std::string::npos)
        << "不得虚构运行时加载成功";
    EXPECT_TRUE(QFile::exists(target)) << "目标包文件未落盘";
}

/// D1 默认否：确认拒绝＝不写盘（用户未明示即不产出文件）。
TEST(ModelingCommandFlows, ExportPackage_ConfirmRejected_NoFileWritten_UI_T41D)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-07"}, std::vector<std::string>{});

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("demo.irdbundle"));

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.host.savePathAnswer = target;
    fx.host.confirmProceedAnswer = false;  // 默认否被采纳

    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.export-package",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_FALSE(QFile::exists(target)) << "确认拒绝后仍写盘（D1 默认否违约）";
}

/// 审核 R1：导入保留已回填的项目根身份（恰一根不变量——下次 apply 走
/// 同根替换而非 allocateNew 重复建根，requirements 域 F-461 同型缺陷预防）。
TEST(ModelingCommandFlows, ImportPackage_PreservesAppliedRootIdentity_R1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, std::vector<std::string>{});

    // 先导出一个真实规范包（回读输入——非伪造字节）。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString packagePath = dir.filePath(QStringLiteral("demo.irdbundle"));
    {
        FlowHarness fx;
        fx.session.draft = makeSixAxisDraft();
        fx.host.savePathAnswer = packagePath;
        std::string summary;
        ASSERT_TRUE(executeModelingCommand("modeling.export-package",
                                           fx.session, fx.deps(), fx.host, summary));
    }

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    // 已回填的项目根身份（noteAppliedRevision 的回执形态）＋一条未应用编辑
    // （D1 确认文本的丢弃警示应随行）。
    fx.session.draft.rootObjectId = core::ObjectId::tryFromCanonical(
        "obj-" + std::string(32, 'a'));
    ASSERT_TRUE(fx.session.draft.rootObjectId.has_value());
    ModelingChangeRecord pending;
    pending.subject = "joints[0]";
    pending.summary = "未应用编辑（将被导入丢弃）";
    fx.session.draft.changes.push_back(pending);
    fx.host.openPathAnswer = packagePath;

    std::string summary;
    const bool imported = executeModelingCommand("modeling.import-package",
                                                 fx.session, fx.deps(), fx.host, summary);
    ASSERT_TRUE(imported) << "import summary: " << summary;
    ASSERT_TRUE(fx.session.draft.rootObjectId.has_value());
    EXPECT_EQ(fx.session.draft.rootObjectId->toCanonical(), "obj-" + std::string(32, 'a'))
        << "导入后项目根身份被清空（R1 恰一根不变量破坏）";
    EXPECT_TRUE(fx.session.draft.design.joints.size() >= std::size_t{1})
        << "导入草稿未落位";
}

// =====================================================================
// UI-T42（F-466 消账）：装配集∧目录集一致性——路由权威判定（flows 层
// isAssembledModelingCommand）与 §9.7.3 卡表目录双向对账：目录十条全部
// 可执行（无"禁用但已交付"的缩水）；路由集无目录外条目（无幽灵路由）。
// =====================================================================

TEST(ModelingCommandFlows, AssembledSetMatchesCatalog_F466)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    const auto catalog = modelingDomainCommands();
    ASSERT_EQ(catalog.size(), std::size_t{12}) << "§9.7.3 卡表目录十二条（契约面——UI-T56 表尾追加第十一号、WP-13-T25/F06 表尾追加第十二号 import-workcell）";

    for (const auto& desc : catalog) {
        EXPECT_TRUE(isAssembledModelingCommand(desc.id))
            << "目录命令未装配（缩水交付——F-466 变异①的断言面）: " << desc.id;
    }

    // 反向：路由集无目录外条目（幽灵路由＝点击后 fail-fast——不可达面）。
    for (const std::string& ghost : {
             "modeling.nonexistent", "modeling.new-from-template-x",
             "modeling.export-package-extra"}) {
        EXPECT_FALSE(isAssembledModelingCommand(ghost))
            << "路由集含目录外条目（幽灵路由）: " << ghost;
    }
}

// =====================================================================
// import-urdf 依赖树装配（WP-13-T21——F-473 修复面；顺带消 F-469 半边：
// 导入流此前零自动化网）。夹具＝临时目录真实文件（流经 readFileBytes
// 真读盘＋io snapshot 真扫描——非内存替身），mesh 资产＝最小合法 ASCII
// STL（io 网格护栏按真实面数统计——1 面远低于限额）。
// =====================================================================

namespace {

/// 写临时 URDF（mesh 引用由 useMesh 开关控制——同一模型面覆盖两形态；
/// 连杆结构两形态恒完整——j1/j2 的父子引用不随开关悬空）。
QString writeUrdf(const QTemporaryDir& dir, bool withMesh)
{
    // withMesh=true：l1 视觉几何＝存在的相对引用（meshes/l1.stl）、l2 视觉
    // 几何＝缺失叶（meshes/ghost.dae——V-08 面）；withMesh=false：两连杆
    // 均无几何（零 mesh 引用面）。
    const QString l1Visual = withMesh
        ? QStringLiteral(
              "    <visual><geometry><mesh filename=\"meshes/l1.stl\"/></geometry></visual>\n")
        : QString();
    const QString l2Visual = withMesh
        ? QStringLiteral(
              "    <visual><geometry><mesh filename=\"meshes/ghost.dae\"/></geometry></visual>\n")
        : QString();
    const QString urdf = QStringLiteral(
        "<?xml version=\"1.0\"?>\n"
        "<robot name=\"flow arm\">\n"
        "  <link name=\"base\"/>\n"
        "  <link name=\"l1\">\n"
        "%1"
        "  </link>\n"
        "  <link name=\"l2\">\n"
        "%2"
        "  </link>\n"
        "  <joint name=\"j1\" type=\"revolute\">\n"
        "    <parent link=\"base\"/><child link=\"l1\"/>\n"
        "    <origin xyz=\"0 0 0.2\"/><axis xyz=\"0 1 0\"/>\n"
        "    <limit lower=\"-1.57\" upper=\"1.57\" effort=\"50\" velocity=\"1.0\"/>\n"
        "  </joint>\n"
        "  <joint name=\"j2\" type=\"revolute\">\n"
        "    <parent link=\"l1\"/><child link=\"l2\"/>\n"
        "    <origin xyz=\"0.1 0 0\"/><axis xyz=\"0 0 1\"/>\n"
        "    <limit lower=\"-3.14\" upper=\"3.14\" effort=\"40\" velocity=\"2.0\"/>\n"
        "  </joint>\n"
        "</robot>\n")
        .arg(l1Visual, l2Visual);
    const QString path = dir.filePath(QStringLiteral("robot.urdf"));
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(urdf.toUtf8());
    return path;
}

/// 写最小合法 ASCII STL（io 流式统计面数——1 面合规）。
void writeStl(const QTemporaryDir& dir)
{
    const QDir meshDir = QDir(dir.path());
    meshDir.mkpath(QStringLiteral("meshes"));
    QFile file(meshDir.filePath(QStringLiteral("meshes/l1.stl")));
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write("solid m\n"
               "facet normal 0 0 1\n"
               " outer loop\n"
               "  vertex 0 0 0\n"
               "  vertex 1 0 0\n"
               "  vertex 0 1 0\n"
               " endloop\n"
               "endfacet\n"
               "endsolid m\n");
}

}  // namespace

/// 含 mesh 引用（一存在一缺失）的 URDF → 导入成功产草稿：修复前本形态
/// 被映射器以 SourceInconsistent 拒绝（bytes 与依赖树非同源——生产流未
/// 装树）；修复后流半区按黄金测试装配纪律补缺失容忍树——存在 mesh＝io
/// 真实快照入清单、缺失 mesh＝V-08 Recorded 缺失（应用可过，警告可见）。
TEST(ModelingCommandFlows, ImportUrdf_WithMeshAndMissingResource_ProducesDraft_WP13_T21)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-19"},
                  std::vector<std::string>{"F-473"});

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeStl(dir);
    const QString urdfPath = writeUrdf(dir, /*withMesh=*/true);

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.host.openPathAnswer = urdfPath;
    fx.host.confirmImportAnswer = true;

    std::string summary;
    const bool executed = executeModelingCommand(
        "modeling.import-urdf", fx.session, fx.deps(), fx.host, summary);

    // 修复点：不再 SourceInconsistent——流程成功、草稿替换、就绪重算触发。
    EXPECT_TRUE(executed) << "summary=" << summary;
    EXPECT_NE(summary.find("URDF 导入完成"), std::string::npos)
        << "summary=" << summary;
    EXPECT_EQ(fx.session.draft.design.joints.size(), std::size_t{2})
        << "识别关节数不符（URDF j1/j2）";
    EXPECT_EQ(fx.session.draft.design.links.size(), std::size_t{3})
        << "识别连杆数不符（base/l1/l2）";
    // 资源清单＝同键去重后的 mesh 引用全集（存在 l1.stl＋缺失 ghost.dae）。
    EXPECT_EQ(fx.session.draft.design.resourceManifest.size(), std::size_t{2})
        << "资源清单未承载两笔 mesh 引用";
    EXPECT_FALSE(fx.session.draft.changes.empty()) << "导入未留编辑记录";
    EXPECT_EQ(fx.recomputeCalls, 1) << "导入后未触发就绪重算";
}

/// 无 mesh 引用的纯 URDF → 照常导入（回归守卫：依赖树装配对零引用面
/// 退化为仅入口节点——原可用形态不因修复面变化）。
TEST(ModelingCommandFlows, ImportUrdf_PlainNoMesh_ProducesDraft_WP13_T21)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03"}, std::vector<std::string>{});

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString urdfPath = writeUrdf(dir, /*withMesh=*/false);

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.host.openPathAnswer = urdfPath;
    fx.host.confirmImportAnswer = true;

    std::string summary;
    const bool executed = executeModelingCommand(
        "modeling.import-urdf", fx.session, fx.deps(), fx.host, summary);

    EXPECT_TRUE(executed) << "summary=" << summary;
    EXPECT_EQ(fx.session.draft.design.joints.size(), std::size_t{2});
    EXPECT_TRUE(fx.session.draft.design.resourceManifest.empty())
        << "无 mesh 引用时资源清单应为空";
}

/// 用户在导入确认框取消 → 不落草稿（SA-15 确认流半区——导入路径的
/// 取消面此前零覆盖，随本批补网）。
TEST(ModelingCommandFlows, ImportUrdf_ConfirmRejected_KeepsSessionDraft_WP13_T21)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-15"}, std::vector<std::string>{});

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeStl(dir);
    const QString urdfPath = writeUrdf(dir, /*withMesh=*/true);

    FlowHarness fx;
    ModelingWorkingSet before = makeSixAxisDraft();
    fx.session.draft = before;
    fx.host.openPathAnswer = urdfPath;
    fx.host.confirmImportAnswer = false;  // 用户拒绝导入报告

    std::string summary;
    const bool executed = executeModelingCommand(
        "modeling.import-urdf", fx.session, fx.deps(), fx.host, summary);

    EXPECT_FALSE(executed) << "确认拒绝应按用户取消处置";
    EXPECT_TRUE(fx.session.draft == before) << "取消后草稿被改动";
}

// =====================================================================
// UI-T56：WC/DWC XML 外供导出命令流（export-workcell-xml——第十一号）
// =====================================================================

/// 全链（接线在位＋路径应答）＝回调恰调一次、路径透传、摘要透传、草稿
/// 零触碰（会话级文件操作——零修订零脏化）。
TEST(ModelingCommandFlows, ExportWorkCellXml_HappyPath_UI_T56)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20"}, std::vector<std::string>{});

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("workcell.xml"));

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.exportWired = true;
    fx.exportAnswer = true;
    fx.host.savePathAnswer = target;

    const ModelingWorkingSet before = fx.session.draft;
    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.export-workcell-xml",
                                       fx.session, fx.deps(), fx.host, summary));
    EXPECT_EQ(fx.exportCalls, 1) << "回调恰调一次";
    EXPECT_EQ(fx.exportLastPath, target.toStdString()) << "路径透传";
    EXPECT_NE(summary.find("导出"), std::string::npos);
    EXPECT_TRUE(fx.session.draft == before) << "会话级文件操作零草稿触碰";
    EXPECT_EQ(fx.recomputeCalls, 0) << "零就绪重算（零修订语义）";
}

/// 快照缺席（宿主回调 false）＝诚实拒绝＋原因透传（不虚构产物）；出口
/// 未接线＝装配缺陷摘要（fail-closed 同 kAssembled 口径）。
TEST(ModelingCommandFlows, ExportWorkCellXml_SnapshotMissingAndNotWired_UI_T56)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20"}, std::vector<std::string>{});

    // 缺席态：回调在位但返回 false。
    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.exportWired = true;
    fx.exportAnswer = false;
    fx.host.savePathAnswer = QStringLiteral("unused.xml");

    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.export-workcell-xml",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_NE(summary.find("缺席"), std::string::npos)
        << "缺席原因透传（诚实缺席不虚构产物）";

    // 未接线态：deps 无回调＝装配缺陷摘要（fail-closed）。
    FlowHarness unwired;
    unwired.session.draft = makeSixAxisDraft();
    unwired.host.savePathAnswer = QStringLiteral("unused.xml");
    std::string unwiredSummary;
    EXPECT_FALSE(executeModelingCommand("modeling.export-workcell-xml",
                                        unwired.session, unwired.deps(),
                                        unwired.host, unwiredSummary));
    EXPECT_NE(unwiredSummary.find("装配缺陷"), std::string::npos);
}

/// 用户取消（saveFilePath 返回 nullopt）＝零回调零副作用；既有文件＋确认
/// 拒绝＝零回调（覆盖知情面——确认文本含原子替换语义）。
TEST(ModelingCommandFlows, ExportWorkCellXml_CancelAndOverwriteConfirm_UI_T56)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20"}, std::vector<std::string>{});

    // 用户取消路径选择。
    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.exportWired = true;
    fx.host.savePathAnswer = std::nullopt;  // 取消
    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.export-workcell-xml",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_EQ(fx.exportCalls, 0) << "取消＝零回调";

    // 既有文件＋确认拒绝（知情面——替换需用户明示）。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString existing = dir.filePath(QStringLiteral("workcell.xml"));
    { QFile f(existing); ASSERT_TRUE(f.open(QIODevice::WriteOnly)); f.write("old"); }
    fx.host.savePathAnswer = existing;
    fx.host.confirmProceedAnswer = false;
    std::string declined;
    EXPECT_FALSE(executeModelingCommand("modeling.export-workcell-xml",
                                        fx.session, fx.deps(), fx.host, declined));
    EXPECT_EQ(fx.exportCalls, 0) << "确认拒绝＝零回调";
    EXPECT_TRUE(fx.host.lastConfirmText.contains(QStringLiteral("原子替换")))
        << "确认文本缺原子替换语义（知情面）";
    // 确认放行＝回调放行。
    fx.host.confirmProceedAnswer = true;
    std::string accepted;
    ASSERT_TRUE(executeModelingCommand("modeling.export-workcell-xml",
                                       fx.session, fx.deps(), fx.host, accepted));
    EXPECT_EQ(fx.exportCalls, 1);
}

// =====================================================================
// UI-T63 从零创建（custom-chain 六轴声明——new-from-template 参数化）
// =====================================================================

/**
 * custom-chain 声明链（UI-T63）：模板选择第二项→声明表单→deps 透传
 * （声明逐字达 reseedCustomChain——flows 零改写的编排面）；取消两态
 * （选单取消/表单取消）静默终止；未接线＝装配缺陷 fail-closed。
 */
TEST(ModelingCommandFlows, NewFromTemplateCustomChain_UI_T63)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"}, std::vector<std::string>{"AT-20"});

    FlowHarness fx;
    fx.host.chooseItemAnswer = 1;  // 选第二项＝custom-chain
    fx.host.declareAnswer = std::vector<CustomChainJointSpec>(6);
    fx.host.declareAnswer->at(1).z = 0.4;  // 声明变化点（J2 轴 z——透传留痕）
    fx.host.declareAnswer->at(3).x = 0.25;
    fx.host.declareAnswer->at(3).yaw = 0.5;
    fx.reseedCustomWired = true;

    std::string summary;
    const bool executed = executeModelingCommand(
        "modeling.new-from-template", fx.session, fx.deps(), fx.host, summary);
    ASSERT_TRUE(executed);
    EXPECT_EQ(fx.host.declareCalls, 1);
    EXPECT_EQ(fx.reseedCustomCalls, 1);
    ASSERT_EQ(fx.reseedCustomLast.joints.size(), std::size_t{6});
    EXPECT_DOUBLE_EQ(fx.reseedCustomLast.joints[1].z, 0.4) << "声明逐字透传（J2 轴）";
    EXPECT_DOUBLE_EQ(fx.reseedCustomLast.joints[3].x, 0.25) << "声明逐字透传（J4 Origin）";
    EXPECT_DOUBLE_EQ(fx.reseedCustomLast.joints[3].yaw, 0.5) << "声明逐字透传（J4 yaw）";
    EXPECT_NE(summary.find("custom-chain"), std::string::npos);

    // 选单取消＝静默终止（零声明零重种子）。
    fx.host.chooseItemAnswer = std::nullopt;
    summary.clear();
    EXPECT_FALSE(executeModelingCommand("modeling.new-from-template",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_TRUE(summary.empty());
    EXPECT_EQ(fx.host.declareCalls, 1) << "选单取消不进表单";

    // 表单取消＝静默终止（零重种子）。
    fx.host.chooseItemAnswer = 1;
    fx.host.declareAnswer = std::nullopt;
    summary.clear();
    EXPECT_FALSE(executeModelingCommand("modeling.new-from-template",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_TRUE(summary.empty());
    EXPECT_EQ(fx.reseedCustomCalls, 1);

    // 未接线＝装配缺陷 fail-closed（原因就地）。
    fx.host.declareAnswer = std::vector<CustomChainJointSpec>(6);
    fx.reseedCustomWired = false;
    summary.clear();
    const bool executed2 = executeModelingCommand(
        "modeling.new-from-template", fx.session, fx.deps(), fx.host, summary);
    EXPECT_FALSE(executed2);
    EXPECT_NE(summary.find("装配缺陷"), std::string::npos);

    // 域拒绝态：deps 返回 false＋summary 透传（false＝流程失败语义）。
    fx.reseedCustomWired = true;
    fx.reseedCustomAnswer = false;
    summary.clear();
    EXPECT_FALSE(executeModelingCommand("modeling.new-from-template",
                                        fx.session, fx.deps(), fx.host, summary));
    EXPECT_NE(summary.find("替身拒绝态"), std::string::npos);
}

/**
 * generic-6r 路径保持（UI-T63 参数化回归——选单第一项走 reseedTemplate
 * 原行为；模板选择不改变既有重种子语义）。
 */
TEST(ModelingCommandFlows, NewFromTemplateGeneric6RKept_UI_T63)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"}, std::vector<std::string>{});

    FlowHarness fx;
    fx.host.chooseItemAnswer = 0;  // 选第一项＝generic-6r
    bool reseeded = false;
    ModelingFlowDeps d;
    d.reseedTemplate = [&reseeded] { reseeded = true; };
    d.recomputeReadiness = [] {};
    std::string summary;
    const bool executed = executeModelingCommand(
        "modeling.new-from-template", fx.session, d, fx.host, summary);
    ASSERT_TRUE(executed);
    EXPECT_TRUE(reseeded);
    EXPECT_EQ(fx.host.declareCalls, 0) << "generic-6r 路径不进声明表单";
    EXPECT_NE(summary.find("generic-6r"), std::string::npos);
}


namespace {
/**
 * @brief 写入真实离线Xacro夹具；[in] path为临时目录绝对路径，bytes为UTF-8。
 * 仅测试调用线程使用；打开/写入失败立即使测试失败，不把环境错误当产品红例。
 */
void writeXacroFixture(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << path.toStdString();
    ASSERT_EQ(file.write(bytes), bytes.size());
}
} // namespace

TEST(ModelingCommandFlows, ExistingNestedXacroIncludeUsesProductionBridge_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19", {"AT-31"}, std::nullopt);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QDir(dir.path()).mkpath("parts"));
    writeXacroFixture(dir.filePath("parts/definition.xacro"),
        "<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\"><xacro:macro name=\"make_link\" params=\"name\"><link name=\"${name}\"/></xacro:macro></robot>");
    writeXacroFixture(dir.filePath("parts/entry.xacro"),
        "<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\"><xacro:include filename=\"definition.xacro\"/></robot>");
    const QByteArray entry = "<robot name=\"nested\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\"><xacro:include filename=\"parts/entry.xacro\"/><xacro:make_link name=\"from_include\"/></robot>";
    const QString path = dir.filePath("robot.xacro");
    writeXacroFixture(path, entry);
    const auto expanded = expandXacroBytes(
        std::vector<std::uint8_t>(entry.begin(), entry.end()), path);
    // F05：磁盘文件确实存在，生产装配必须把嵌套include交给真实展开服务。
    ASSERT_TRUE(expanded.ok) << expanded.errorDetail;
    const std::string text(expanded.expandedBytes.begin(), expanded.expandedBytes.end());
    EXPECT_NE(text.find("from_include"), std::string::npos);
    EXPECT_EQ(text.find("xacro:make_link"), std::string::npos);
}

TEST(ModelingCommandFlows, XacroBranchesUseSameExplicitSelectionAsUrdf_MDL12_AT17)
{
    IRD_TEST_INFO("MDL-12", {"AT-17"}, std::nullopt);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("branched.xacro");
    writeXacroFixture(path,
        "<robot name=\"branches\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\">"
        "<link name=\"base\"/><link name=\"left\"/><link name=\"right\"/>"
        "<joint name=\"left_axis\" type=\"revolute\"><parent link=\"base\"/><child link=\"left\"/><axis xyz=\"0 0 1\"/><limit lower=\"-1\" upper=\"1\"/></joint>"
        "<joint name=\"right_axis\" type=\"revolute\"><parent link=\"base\"/><child link=\"right\"/><axis xyz=\"0 1 0\"/><limit lower=\"-2\" upper=\"2\"/></joint></robot>");
    FlowHarness fx;
    fx.session.draft=makeSixAxisDraft();
    fx.host.openPathAnswer=path;
    fx.host.chooseItemAnswer=1;
    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.import-xacro", fx.session,
        fx.deps(), fx.host, summary)) << summary;
    ASSERT_EQ(fx.session.draft.design.joints.size(), 1u);
    EXPECT_EQ(fx.session.draft.design.joints.front().localName, "right_axis");
    const auto accepted=fx.session.draft.design;
    // 选链取消/非法应答均不能用不完整候选覆盖已经接受的草稿。
    for(const std::optional<int> choice:{std::optional<int>{},std::optional<int>{-1},std::optional<int>{99}}){
        fx.host.chooseItemAnswer=choice;
        EXPECT_FALSE(executeModelingCommand("modeling.import-xacro",fx.session,fx.deps(),fx.host,summary));
        EXPECT_EQ(fx.session.draft.design,accepted);
    }
}

TEST(ModelingCommandFlows, ImportConfirmationShowsObjectFieldsValuesAndReasons_MDL11_AT15)
{
    IRD_TEST_INFO("MDL-11", {"AT-15"}, std::nullopt);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    FlowHarness fx;
    fx.session.draft=makeSixAxisDraft();
    fx.host.openPathAnswer=writeUrdf(dir, false);
    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.import-urdf", fx.session,
        fx.deps(), fx.host, summary)) << summary;
    // F02：必须出现实际对象/字段与工程值；统计数字不能替代可审阅映射清单。
    EXPECT_TRUE(fx.host.lastConfirmText.contains("j1"));
    EXPECT_TRUE(fx.host.lastConfirmText.contains("bounds"));
    EXPECT_TRUE(fx.host.lastConfirmText.contains("-1.57"));
    EXPECT_TRUE(fx.host.lastConfirmText.contains("axis"));
}


TEST(ModelingCommandFlows, XacroParametersReachMappingAndProvenance_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19", {"AT-31"}, std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("parameters.xacro");
    writeXacroFixture(path,
        "<robot name=\"params\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\">"
        "<xacro:property name=\"travel\" value=\"1.25\"/><link name=\"base\"/><link name=\"tip\"/>"
        "<joint name=\"axis\" type=\"revolute\"><parent link=\"base\"/><child link=\"tip\"/>"
        "<axis xyz=\"0 0 1\"/><limit lower=\"-1\" upper=\"${travel}\"/></joint></robot>");
    FlowHarness fx;fx.session.draft=makeSixAxisDraft();fx.host.openPathAnswer=path;
    fx.host.parameterAnswer=std::vector<std::pair<std::string,std::string>>{{"travel","2.75"}};
    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.import-xacro",fx.session,fx.deps(),fx.host,summary))<<summary;
    ASSERT_EQ(fx.host.capturedDefaults.size(),1u);
    EXPECT_EQ(fx.host.capturedDefaults.front().first,"travel");
    EXPECT_EQ(fx.host.capturedDefaults.front().second,"1.25");
    ASSERT_EQ(fx.session.draft.design.joints.size(),1u);
    ASSERT_TRUE(fx.session.draft.design.joints.front().bounds.tryValue().has_value());
    EXPECT_DOUBLE_EQ(fx.session.draft.design.joints.front().bounds.tryValue()->second,2.75);
    EXPECT_TRUE(fx.host.lastConfirmText.contains("xacro-substitution/travel"));
    EXPECT_TRUE(fx.host.lastConfirmText.contains("2.75"));
}

TEST(ModelingCommandFlows, XacroParameterCancelKeepsOriginalDraft_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19", {"AT-31"}, std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("cancel.xacro");
    writeXacroFixture(path,"<robot name=\"cancel\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\"><link name=\"base\"/></robot>");
    FlowHarness fx;fx.session.draft=makeSixAxisDraft();fx.host.openPathAnswer=path;
    const auto before=fx.session.draft.design;fx.host.parameterAnswer=std::nullopt;
    std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.import-xacro",fx.session,fx.deps(),fx.host,summary));
    EXPECT_EQ(fx.session.draft.design,before);EXPECT_EQ(fx.recomputeCalls,0);
}

TEST(ModelingCommandFlows, XacroIncludeIdentitySurvivesImport_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19",{"AT-31"},std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    const QByteArray dependency="<robot><link name=\"base\"/><link name=\"tip\"/>"
        "<joint name=\"axis\" type=\"revolute\"><parent link=\"base\"/><child link=\"tip\"/>"
        "<axis xyz=\"0 0 1\"/><limit lower=\"-1\" upper=\"1\"/></joint></robot>";
    writeXacroFixture(dir.filePath("included.xacro"),dependency);
    const auto path=dir.filePath("main.xacro");
    writeXacroFixture(path,"<robot name=\"provenance\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\">"
        "<xacro:include filename=\"included.xacro\"/></robot>");
    FlowHarness fx;fx.session.draft=makeSixAxisDraft();fx.host.openPathAnswer=path;
    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.import-xacro",fx.session,fx.deps(),fx.host,summary))<<summary;
    // AT-31离线来源必须覆盖实际消费的include，不能仅保留入口文件哈希。
    const auto& resources=fx.session.draft.design.resourceManifest;
    const auto found=std::find_if(resources.begin(),resources.end(),[](const auto& resource){
        return resource.resourceId=="xacro-include/included.xacro";
    });
    ASSERT_NE(found,resources.end());
    core::ContentDigester digest;digest.update(dependency.constData(),dependency.size());
    EXPECT_EQ(found->contentDigest,digest.finalize());
    EXPECT_EQ(found->state,ResourceState::Recorded);
    ASSERT_TRUE(found->externalRecord.has_value());
    EXPECT_EQ(found->externalRecord->recordedDigest,found->contentDigest);
    EXPECT_TRUE(fx.host.lastConfirmText.contains("included.xacro"));
    EXPECT_TRUE(QString::fromStdString(found->externalRecord->absPath).contains("included.xacro"));
    const RobotDesignCodec codec;
    const auto encoded=codec.encode(ObjectVariant{fx.session.draft.design},kCurrentFormatVersion);
    ASSERT_TRUE(encoded.ok());
    const auto decoded=codec.decode(encoded.get(),kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    // canonical按resourceId规范排序；比较完整字段，不能把输入构造序当持久化契约。
    auto canonicalResources=resources;
    std::sort(canonicalResources.begin(),canonicalResources.end(),[](const auto& a,const auto& b){
        return a.resourceId<b.resourceId;
    });
    EXPECT_EQ(std::get<RobotDesign>(decoded.get()).resourceManifest,canonicalResources);
}

TEST(ModelingCommandFlows, XacroIncludeMissingCycleEscapeAndMalformedXmlFailClosed_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19", {"AT-31"}, std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QDir(dir.path()).mkpath("root"));
    writeXacroFixture(dir.filePath("outside.xacro"),"<robot name=\"outside\"/>");
    const auto path=dir.filePath("root/main.xacro");
    // F05反例：缺文件、自环、越界和坏XML不能借生产装配绕开io/展开安全边界。
    for(const QByteArray target:{QByteArray("missing.xacro"),QByteArray("main.xacro"),QByteArray("../outside.xacro")}){
        const QByteArray entry="<robot name=\"bad\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\"><xacro:include filename=\""+target+"\"/></robot>";
        writeXacroFixture(path,entry);
        const auto input=loadXacroInput(std::vector<std::uint8_t>(entry.begin(),entry.end()),path);
        EXPECT_FALSE(input.ok)<<target.constData();EXPECT_FALSE(input.errorDetail.empty());
    }
    const QByteArray dtd="<robot name=\"broken\"><unclosed></robot>";
    writeXacroFixture(path,dtd);
    EXPECT_FALSE(loadXacroInput(std::vector<std::uint8_t>(dtd.begin(),dtd.end()),path).ok);
}

TEST(ModelingCommandFlows, FrozenXacroInputDoesNotRereadAndMissingMeshRemainsEditable_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19", {"AT-31"}, std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("frozen.xacro");
    const QByteArray entry="<robot name=\"frozen\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\"><link name=\"base\"><visual><geometry><mesh filename=\"missing.stl\"/></geometry></visual></link><link name=\"tip\"/><joint name=\"axis\" type=\"revolute\"><parent link=\"base\"/><child link=\"tip\"/><axis xyz=\"0 0 1\"/><limit lower=\"-1\" upper=\"1\"/></joint></robot>";
    writeXacroFixture(path,entry);
    const auto input=loadXacroInput(std::vector<std::uint8_t>(entry.begin(),entry.end()),path);
    ASSERT_TRUE(input.ok)<<input.errorDetail;
    writeXacroFixture(path,"<robot name=\"changed_after_dialog\"/>");
    const auto expanded=expandXacroInput(input,{});
    EXPECT_TRUE(expanded.ok)<<expanded.errorDetail;
    // 重读路径显式检测变化；冻结路径按已展示输入继续，不读取后来替换的内容。
    EXPECT_FALSE(loadXacroInput(std::vector<std::uint8_t>(entry.begin(),entry.end()),path).ok);
    writeXacroFixture(path,entry);
    FlowHarness fx;fx.session.draft=makeSixAxisDraft();fx.host.openPathAnswer=path;
    std::string summary;
    ASSERT_TRUE(executeModelingCommand("modeling.import-xacro",fx.session,fx.deps(),fx.host,summary))<<summary;
    EXPECT_EQ(fx.session.draft.design.displayName,"frozen");
    EXPECT_TRUE(fx.host.lastConfirmText.contains("missing.stl"));
}


TEST(ModelingCommandFlows, XacroIncludeBudgetMeasuresDepthNotBranchWidth_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19",{"AT-31"},std::nullopt);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    QByteArray entry="<robot name=\"wide\" xmlns:xacro=\"http://www.ros.org/wiki/xacro\">";
    for(int i=0;i<20;++i){
        const QString name=QString("leaf%1.xacro").arg(i);
        writeXacroFixture(dir.filePath(name),"<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\"/>");
        entry+="<xacro:include filename=\""+name.toUtf8()+"\"/>";
    }
    entry+="</robot>";
    const auto path=dir.filePath("wide.xacro");writeXacroFixture(path,entry);
    const auto wide=loadXacroInput(std::vector<std::uint8_t>(entry.begin(),entry.end()),path);
    ASSERT_TRUE(wide.ok)<<wide.errorDetail;
    EXPECT_EQ(wide.includeBytes.size(),20u);
    for(int i=0;i<17;++i){
        const QByteArray next=i==16?QByteArray():"<xacro:include filename=\"deep"+QByteArray::number(i+1)+".xacro\"/>";
        const QByteArray text="<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\">"+next+"</robot>";
        writeXacroFixture(dir.filePath(QString("deep%1.xacro").arg(i)),text);
    }
    QFile first(dir.filePath("deep0.xacro"));ASSERT_TRUE(first.open(QIODevice::ReadOnly));
    const auto raw=first.readAll();
    const auto deep=loadXacroInput(std::vector<std::uint8_t>(raw.begin(),raw.end()),first.fileName());
    EXPECT_FALSE(deep.ok);EXPECT_NE(deep.errorDetail.find("IO-SEC-BUDGET-INCLUDE"),std::string::npos);
}

/** @brief F13/MDL-17：无真实会话写入出口时不得用showInfo报告复位成功。 */
TEST(ModelingCommandFlows, HomeZeroWithoutWriterRejectsInsteadOfInfo_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    ModuleSessionState session;session.draft=makeSixAxisDraft();
    PoseSet set;set.objectId=core::ObjectId::generate();
    set.entries.push_back(PoseSetEntry{"homeConfiguration",std::vector<double>(6,0.3),""});
    set.entries.push_back(PoseSetEntry{"zeroConfiguration",std::vector<double>(6,0.0),""});
    session.draft.poseSetObject=set;const auto before=session.draft;
    FakeHost host;host.chooseItemAnswer=0;std::string summary;
    EXPECT_FALSE(executeModelingCommand("modeling.reset-home-zero",session,{},host,summary));
    EXPECT_EQ(host.showInfoCount,0)<<"恢复必须改变真实会话或明确拒绝，信息弹窗不算完成";
    EXPECT_EQ(session.draft,before);
}

/** @brief MDL-17：权威保留键选择/取消/非法下标，零草稿改写。 */
TEST(ModelingCommandFlows, HomeZeroChoosesExactReservedKeyAndCancelsAtomically_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    ModuleSessionState session;session.draft=makeSixAxisDraft();
    PoseSet set;set.objectId=core::ObjectId::generate();
    for(const auto* key:{"zero","home","zeroConfiguration","homeConfiguration"}){
        set.entries.push_back(PoseSetEntry{key,std::vector<double>(6,0.0),""});
    }
    session.draft.poseSetObject=set;const auto before=session.draft;
    FakeHost host;ModelingFlowDeps deps;std::string chosen,summary;int called=0;
    deps.restoreNamedPose=[&](const std::string& key,std::string& why){++called;chosen=key;why="已恢复";return true;};
    host.chooseItemAnswer=0;
    EXPECT_TRUE(executeModelingCommand("modeling.reset-home-zero",session,deps,host,summary));
    EXPECT_EQ(chosen,"homeConfiguration");
    EXPECT_EQ(host.chosenItems,(QStringList{"homeConfiguration","zeroConfiguration"}));
    host.chooseItemAnswer=1;
    EXPECT_TRUE(executeModelingCommand("modeling.reset-home-zero",session,deps,host,summary));
    EXPECT_EQ(chosen,"zeroConfiguration");
    host.chooseItemAnswer=99;
    EXPECT_FALSE(executeModelingCommand("modeling.reset-home-zero",session,deps,host,summary));
    host.chooseItemAnswer.reset();
    EXPECT_FALSE(executeModelingCommand("modeling.reset-home-zero",session,deps,host,summary));
    EXPECT_TRUE(summary.empty());EXPECT_EQ(called,2);EXPECT_EQ(session.draft,before);
    session.draft.poseSetObject->entries.resize(2); // 只有旧home/zero，必须拒绝而非当别名。
    host.chooseItemAnswer=0;
    EXPECT_FALSE(executeModelingCommand("modeling.reset-home-zero",session,deps,host,summary));
    EXPECT_EQ(called,2);EXPECT_EQ(host.showInfoCount,0);
}

// =====================================================================
// import-workcell（WP-13-T25/F06——MDL-18/AT-33：外部 .wc.xml 有损反向
// 导入的正式命令流。提取引擎 mapWorkCellXml 已随 WP-13-T17 落位，本批补
// 命令目录入口＋生产流装配：真实磁盘读取（io openGenericXml 良构校验＋
// 真实字节摘要对账）→引擎提取→多设备显式选链→逐项报告确认→确认才替换
// 草稿。夹具＝临时目录真实文件（非内存替身），与 import-urdf 流测试同款
// 纪律（writeStl 复用最小合法 ASCII STL——io 网格护栏真实面数统计）。
// =====================================================================

namespace {

/// 六轴全旋转单设备 WorkCell（F06 流夹具）：j1 带 RPY 90°（deg→rad 独立
/// 核对面）＋PosLimit ±170°＋VelLimit 1.5（本批 schema 边界＝未保留逐项
/// 列出面；F01 落地后转映射）；一存在一缺失两笔 Polytope（V-08 面）；
/// 首命名构型 Home（非零 Q→逐关节 zeroOffset）＋备选构型 Alt（未保留面）。
QString writeWorkCellSixAxis(const QTemporaryDir& dir)
{
    const QDir meshDir(dir.path());
    meshDir.mkpath(QStringLiteral("meshes"));
    QFile file(dir.filePath(QStringLiteral("cell.wc.xml")));
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<WorkCell name=\"flow cell\">\n"
        "  <SerialDevice name=\"arm\">\n"
        "    <Joint name=\"j1\" type=\"Revolute\">\n"
        "      <Pos>0 0 0.2</Pos><RPY>0 0 90</RPY>\n"
        "      <PosLimit min=\"-170\" max=\"170\"/><VelLimit max=\"1.5\"/>\n"
        "      <Drawable name=\"l1d\"><Polytope file=\"meshes/l1.stl\"/></Drawable>\n"
        "    </Joint>\n"
        "    <Joint name=\"j2\" type=\"Revolute\"><Pos>0 0 0.4</Pos>"
        "<PosLimit min=\"-120\" max=\"120\"/></Joint>\n"
        "    <Joint name=\"j3\" type=\"Revolute\"><Pos>0 0 0.3</Pos>"
        "<PosLimit min=\"-170\" max=\"170\"/></Joint>\n"
        "    <Joint name=\"j4\" type=\"Revolute\"><Pos>0 0.3 0</Pos>"
        "<PosLimit min=\"-120\" max=\"120\"/></Joint>\n"
        "    <Joint name=\"j5\" type=\"Revolute\"><Pos>0.3 0 0</Pos>"
        "<PosLimit min=\"-170\" max=\"170\"/></Joint>\n"
        "    <Joint name=\"j6\" type=\"Revolute\">\n"
        "      <Pos>0 0 0.15</Pos><PosLimit min=\"-360\" max=\"360\"/>\n"
        "      <CollisionModel name=\"l6c\"><Polytope file=\"meshes/ghost.stl\"/></CollisionModel>\n"
        "    </Joint>\n"
        "    <Q name=\"Home\"> 0.5 -0.1 0.2 0.3 -0.4 0.6 </Q>\n"
        "    <Q name=\"Alt\"> 1 2 3 4 5 6 </Q>\n"
        "  </SerialDevice>\n"
        "</WorkCell>\n").toUtf8());
    return dir.filePath(QStringLiteral("cell.wc.xml"));
}

/// 双设备 WorkCell（目标设备显式选择面——§6.4 同机制：不默认选第一个）。
QString writeWorkCellTwoDevices(const QTemporaryDir& dir)
{
    QFile file(dir.filePath(QStringLiteral("twin.wc.xml")));
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(QStringLiteral(
        "<?xml version=\"1.0\"?>\n"
        "<WorkCell name=\"twin cell\">\n"
        "  <SerialDevice name=\"arm-a\">\n"
        "    <Joint name=\"a1\" type=\"Revolute\"><PosLimit min=\"-90\" max=\"90\"/></Joint>\n"
        "  </SerialDevice>\n"
        "  <SerialDevice name=\"arm-b\">\n"
        "    <Joint name=\"b1\" type=\"Revolute\"><PosLimit min=\"0\" max=\"180\"/></Joint>\n"
        "  </SerialDevice>\n"
        "</WorkCell>\n").toUtf8());
    return dir.filePath(QStringLiteral("twin.wc.xml"));
}

}  // namespace

/// F06/AT-33 主路径：真实 .wc.xml→真实磁盘读取→提取→报告确认→草稿替换。
/// 断言半部（独立核对，不调用被测转换生成期望）：
///   - RPY "0 0 90"（deg）→ origin 旋转＝R_z(π/2)：r(1,0)=1、r(0,1)=-1；
///   - PosLimit ±170（deg）→ bounds ≈ ±170·π/180 rad（1e-12 容差）；
///   - Q Home j1=0.5 → zeroOffset=0.5（rad，源制式即 rad 不换算）；
///   - 一存在一缺失 Polytope → 资源清单恰两笔（缺失≠阻断，V-08）；
///   - 报告文本逐项可见：未保留面（joint-velocity-limit）不得静默。
TEST(ModelingCommandFlows, ImportWorkcell_SingleDevice_RealReadProducesDraft_MDL18_AT33)
{
    IRD_TEST_INFO("MDL-18",{"AT-33"},std::nullopt);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeStl(dir);  // meshes/l1.stl 真实存在（ghost.stl 有意不写——缺失叶面）
    const QString wcPath = writeWorkCellSixAxis(dir);

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    const std::optional<core::ObjectId> previousRoot = core::ObjectId::generate();
    fx.session.draft.rootObjectId = previousRoot;
    fx.host.openPathAnswer = wcPath;
    fx.host.confirmImportAnswer = true;

    std::string summary;
    const bool executed = executeModelingCommand(
        "modeling.import-workcell", fx.session, fx.deps(), fx.host, summary);

    EXPECT_TRUE(executed) << "summary=" << summary;
    EXPECT_NE(summary.find("WorkCell 导入完成"), std::string::npos)
        << "summary=" << summary;
    // 六关节＋七连杆（links[0]=设备名-base 合成规则，§6.6 连杆合成）。
    ASSERT_EQ(fx.session.draft.design.joints.size(), std::size_t{6});
    ASSERT_EQ(fx.session.draft.design.links.size(), std::size_t{7});
    EXPECT_EQ(fx.session.draft.design.joints[0].localName, "j1");
    EXPECT_EQ(fx.session.draft.design.links[0].localName, "arm-base");
    // 独立数值核对（deg→rad 由本断言固定期望，不引用被测换算）。
    const double k170rad = 170.0 * 3.14159265358979323846 / 180.0;
    const auto bounds = fx.session.draft.design.joints[0].bounds.tryValue();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->first, -k170rad, 1e-12);
    EXPECT_NEAR(bounds->second, k170rad, 1e-12);
    const auto origin = fx.session.draft.design.joints[0].origin.tryValue();
    ASSERT_TRUE(origin.has_value());
    EXPECT_NEAR(origin->r()(1, 0), 1.0, 1e-12);   // R_z(π/2)
    EXPECT_NEAR(origin->r()(0, 1), -1.0, 1e-12);
    EXPECT_NEAR(origin->d()[2], 0.2, 1e-12);      // Pos z=0.2 m
    EXPECT_NEAR(fx.session.draft.design.joints[0].zeroOffset, 0.5, 1e-15);
    EXPECT_NEAR(fx.session.draft.design.joints[5].zeroOffset, 0.6, 1e-15);
    // 资源清单：l1.stl（存在，真实 digest）＋ghost.stl（缺失，Recorded）。
    EXPECT_EQ(fx.session.draft.design.resourceManifest.size(), std::size_t{2});
    // 逐项报告可见性（AT-33 字面——未保留内容不得静默丢弃）。F01 复跑
    // 更新：VelLimit 已映射 maxVelocity（报告呈现映射行而非未保留项）；
    // 备选构型仍无落点（q-config-residual 未保留面不变）。
    const std::string reportText = fx.host.lastConfirmText.toStdString();
    EXPECT_NE(reportText.find("maxVelocity"), std::string::npos)
        << "VelLimit 映射行未在报告呈现（F01 复跑口径）";
    EXPECT_EQ(reportText.find("joint-velocity-limit"), std::string::npos)
        << "velocity 已有落点——不得再报未保留（F01）";
    EXPECT_NE(reportText.find("q-config-residual"), std::string::npos)
        << "备选构型未保留项未在报告呈现";
    // 草稿替换纪律：保根（恰一根不变量 R1）＋留痕＋就绪重算。
    EXPECT_EQ(fx.session.draft.rootObjectId, previousRoot);
    EXPECT_FALSE(fx.session.draft.changes.empty());
    EXPECT_EQ(fx.recomputeCalls, 1);
}

/// F06/§6.4：多设备文件必须显式选链——候选清单含全部设备名；取消选链
/// 零草稿修改；选中第二设备按其所辖关节提取（不默认选第一个）。
TEST(ModelingCommandFlows, ImportWorkcell_TwoDevices_ExplicitChoiceNotAutoFirst_MDL18)
{
    IRD_TEST_INFO("MDL-18",{"AT-33"},std::nullopt);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString twinPath = writeWorkCellTwoDevices(dir);

    // 取消选链：草稿原样（与 before 逐字段相等），summary 清空。
    {
        FlowHarness fx;
        fx.session.draft = makeSixAxisDraft();
        const ModelingWorkingSet before = fx.session.draft;
        fx.host.openPathAnswer = twinPath;
        fx.host.chooseItemAnswer.reset();  // 用户取消设备选择
        std::string summary;
        EXPECT_FALSE(executeModelingCommand(
            "modeling.import-workcell", fx.session, fx.deps(), fx.host, summary));
        EXPECT_EQ(fx.session.draft, before) << "取消选链不得改写工作集";
        EXPECT_TRUE(summary.empty());
    }
    // 显式选第二设备（arm-b）：候选两设备、下标 1＝arm-b——提取其关节 b1。
    {
        FlowHarness fx;
        fx.session.draft = makeSixAxisDraft();
        fx.host.openPathAnswer = twinPath;
        fx.host.chooseItemAnswer = 1;
        fx.host.confirmImportAnswer = true;
        std::string summary;
        EXPECT_TRUE(executeModelingCommand(
            "modeling.import-workcell", fx.session, fx.deps(), fx.host, summary));
        // 候选清单＝两设备名（F06 红证锚：不得隐藏第二个设备）。
        EXPECT_EQ(fx.host.chosenItems,
                  (QStringList{QStringLiteral("arm-a"), QStringLiteral("arm-b")}));
        ASSERT_EQ(fx.session.draft.design.joints.size(), std::size_t{1});
        EXPECT_EQ(fx.session.draft.design.joints[0].localName, "b1")
            << "未按显式选择的设备提取（疑似默认选了第一个）";
    }
}

/// F06/SA-15：报告确认取消 → 草稿逐字段保持原状、零编辑记录、零就绪重算。
TEST(ModelingCommandFlows, ImportWorkcell_ConfirmRejected_KeepsSessionDraft_MDL18)
{
    IRD_TEST_INFO("SA-15",{},std::nullopt);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeStl(dir);
    const QString wcPath = writeWorkCellSixAxis(dir);

    FlowHarness fx;
    const ModelingWorkingSet before = makeSixAxisDraft();
    fx.session.draft = before;
    fx.host.openPathAnswer = wcPath;
    fx.host.confirmImportAnswer = false;  // 用户在逐项报告页取消

    std::string summary;
    EXPECT_FALSE(executeModelingCommand(
        "modeling.import-workcell", fx.session, fx.deps(), fx.host, summary));
    EXPECT_EQ(fx.session.draft, before) << "报告取消不得替换草稿";
    EXPECT_TRUE(fx.session.draft.changes.empty());
    EXPECT_EQ(fx.recomputeCalls, 0);
}

/// F06 诚实拒绝面：非 WorkCell 根文档（URDF 根元素）→引擎根检查拒绝＋
/// 报告登记"根元素不是 WorkCell"，无草稿、summary 带原因（不静默）。
TEST(ModelingCommandFlows, ImportWorkcell_NotWorkCellRoot_HonestRefusal_MDL18)
{
    IRD_TEST_INFO("MDL-18",{},std::nullopt);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString urdfPath = writeUrdf(dir, /*withMesh=*/false);

    FlowHarness fx;
    const ModelingWorkingSet before = makeSixAxisDraft();
    fx.session.draft = before;
    fx.host.openPathAnswer = urdfPath;
    fx.host.confirmImportAnswer = true;  // 即使确认也不得产出草稿

    std::string summary;
    EXPECT_FALSE(executeModelingCommand(
        "modeling.import-workcell", fx.session, fx.deps(), fx.host, summary));
    EXPECT_NE(summary.find("导入失败"), std::string::npos) << "summary=" << summary;
    EXPECT_EQ(fx.session.draft, before);
}

/// F06/UX-03：文件对话框取消＝正常取消（非错误）——零修改、summary 空。
TEST(ModelingCommandFlows, ImportWorkcell_FileDialogCancel_Silent_MDL18)
{
    IRD_TEST_INFO("UX-03",{},std::nullopt);

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    const ModelingWorkingSet before = fx.session.draft;
    fx.host.openPathAnswer.reset();  // 用户取消选文件

    std::string summary;
    EXPECT_FALSE(executeModelingCommand(
        "modeling.import-workcell", fx.session, fx.deps(), fx.host, summary));
    EXPECT_TRUE(summary.empty());
    EXPECT_EQ(fx.session.draft, before);
}

/// F01/MDL-01：URDF `<limit velocity>` 在 schema 落位（JointEntry.maxVelocity）
/// 后必须真实映射入草稿，不再以"joint-velocity-limit 无落点"谎报不支持
/// （修复前红：Import.cpp 报告面登记 unsupported——本用例断言其缺席）。
TEST(ModelingCommandFlows, ImportUrdfVelocityNotReportedUnsupported_MDL01_WP13T25)
{
    IRD_TEST_INFO("MDL-01",{},std::nullopt);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString urdfPath = writeUrdf(dir, /*withMesh=*/false);

    FlowHarness fx;
    fx.session.draft = makeSixAxisDraft();
    fx.host.openPathAnswer = urdfPath;
    fx.host.confirmImportAnswer = true;

    std::string summary;
    EXPECT_TRUE(executeModelingCommand(
        "modeling.import-urdf", fx.session, fx.deps(), fx.host, summary))
        << "summary=" << summary;
    const std::string reportText = fx.host.lastConfirmText.toStdString();
    EXPECT_EQ(reportText.find("joint-velocity-limit"), std::string::npos)
        << "velocity 已有 schema 落点——不得再报无落点不支持项（F01）";
}

/// F07/MDL-04/P-MDL-3：URDF disable_collisions 候选在导入确认后进入会话
/// 承载（importedSelfCollisionCandidates——策略编辑器预填的移交半区）；
/// 取消确认不携带。修复前（红）：候选只进报告、会话承载恒空＝导入提示
/// 死端（审计 M17 复验口径"能显示导入提示"≠"能进入策略编辑"）。
TEST(ModelingCommandFlows, ImportUrdfSeedsSelfCollisionCandidatesToSession_MDL04_WP13T25)
{
    IRD_TEST_INFO("MDL-04",{"AT-19"},std::nullopt);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // URDF 带两条 disable_collisions（base↔l1、l1↔l2——PR2 风格自碰撞排除）。
    const QString urdfPath = dir.filePath(QStringLiteral("pairs.urdf"));
    {
        QFile f(urdfPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(QStringLiteral(
            "<?xml version=\"1.0\"?>\n"
            "<robot name=\"pair arm\">\n"
            "  <disable_collisions link1=\"base\" link2=\"l1\"/>\n"
            "  <disable_collisions link1=\"l1\" link2=\"l2\"/>\n"
            "  <link name=\"base\"/>\n"
            "  <link name=\"l1\"/>\n"
            "  <link name=\"l2\"/>\n"
            "  <joint name=\"j1\" type=\"revolute\">\n"
            "    <parent link=\"base\"/><child link=\"l1\"/>\n"
            "    <origin xyz=\"0 0 0.2\"/><axis xyz=\"0 1 0\"/>\n"
            "    <limit lower=\"-1.57\" upper=\"1.57\" effort=\"50\" velocity=\"1.0\"/>\n"
            "  </joint>\n"
            "  <joint name=\"j2\" type=\"revolute\">\n"
            "    <parent link=\"l1\"/><child link=\"l2\"/>\n"
            "    <origin xyz=\"0.1 0 0\"/><axis xyz=\"0 0 1\"/>\n"
            "    <limit lower=\"-3.14\" upper=\"3.14\" effort=\"40\" velocity=\"2.0\"/>\n"
            "  </joint>\n"
            "</robot>\n").toUtf8());
    }

    // 取消面：确认前取消→候选不得入会话（不经确认零移交）。
    {
        FlowHarness fx;
        fx.host.openPathAnswer = urdfPath;
        fx.host.confirmImportAnswer = false;
        std::string summary;
        EXPECT_FALSE(executeModelingCommand(
            "modeling.import-urdf", fx.session, fx.deps(), fx.host, summary));
        EXPECT_TRUE(fx.session.importedSelfCollisionCandidates.empty())
            << "取消确认后候选不得入会话承载";
    }
    // 确认面：两对候选按报告序入会话（链路＝策略编辑器预填输入）。
    {
        FlowHarness fx;
        fx.host.openPathAnswer = urdfPath;
        fx.host.confirmImportAnswer = true;
        std::string summary;
        EXPECT_TRUE(executeModelingCommand(
            "modeling.import-urdf", fx.session, fx.deps(), fx.host, summary))
            << "summary=" << summary;
        ASSERT_EQ(fx.session.importedSelfCollisionCandidates.size(), std::size_t{2});
        EXPECT_EQ(fx.session.importedSelfCollisionCandidates[0].first, "base");
        EXPECT_EQ(fx.session.importedSelfCollisionCandidates[0].second, "l1");
        EXPECT_EQ(fx.session.importedSelfCollisionCandidates[1].first, "l1");
        EXPECT_EQ(fx.session.importedSelfCollisionCandidates[1].second, "l2");
    }
}
