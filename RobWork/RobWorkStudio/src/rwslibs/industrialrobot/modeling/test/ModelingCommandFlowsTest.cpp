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
    std::optional<QString> openPathAnswer;      ///< openFilePath 应答（nullopt＝取消）
    std::optional<QString> savePathAnswer;      ///< saveFilePath 应答（nullopt＝取消）
    // custom-chain 声明表单应答（UI-T63——nullopt＝取消；nullopt 内值＝六轴
    // 声明预置；declareCalls 计数留痕）。
    std::optional<std::vector<CustomChainJointSpec>> declareAnswer;
    int declareCalls = 0;
    QString lastConfirmText;                    ///< confirmProceed 文本留痕（D1 清单断言）
    int showInfoCount = 0;                      ///< showInfo 调用计数
    QStringList showInfoTexts;                  ///< showInfo 文本留痕

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
                                  const QStringList&) override
    {
        return chooseItemAnswer;
    }
    bool confirmProceed(const QString&, const QString& text) override
    {
        lastConfirmText = text;
        return confirmProceedAnswer;
    }
    bool confirmImport(const QString&) override { return confirmImportAnswer; }
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
    ASSERT_EQ(catalog.size(), std::size_t{11}) << "§9.7.3 卡表目录十一条（契约面——UI-T56 表尾追加）";

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
