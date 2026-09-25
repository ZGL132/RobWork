/**
 * @file   GoldenPeerContractTest.cpp
 * @brief  黄金数据集跨单元对端契约测试（MdlGoldenPeerContract）——契约
 *         tasks/foundation/WP-13-T16.json acceptance 5 的具名自证：
 *
 *   io 对端（导入管线——T05/T13 协作面）：golden-6r.urdf 以 io 三产物值
 *     （字节＋快照＋依赖树）装配 ValidatedSource→mapUrdf——缺失叶
 *     （io §6.5）→IO-RES-MISSING 事实诊断＋Reported/Recorded 双面承载
 *     （§6.7"缺失≠不可行"）——io 缺失清单值语义的对端消费契约
 *   project 对端（V-01 收口——AT-01）：mdl-template-6r 黄金参数草稿→
 *     apply-robot-design 命令 prepare→Planned＋恰好 1 个根对象写（V-01
 *     "恰好 1 个新修订"的 prepare 计划面）＋prepare 零编译调用（编译
 *     归 project S5——端口观测）＋写入字节解码回 T-MDL-1 黄金值
 *     （J6 行程恰 4π——模板黄金参数经命令通道不漂移）
 *   AT-37 建模侧输入面（V-12/V-13）：黄金导入草稿 basePlacement 只含
 *     preset/参数（无预乘旋转矩阵——P-RT-4"modeling 只存参数不存矩
 *     阵"）＋未配置路径默认地面（MDL-22/V15-04，导入来源标记入默认补
 *     全清单）＋场景/工具引用为空（worldPose 未被旋转的输入面前提）；
 *     联合观测行只断言 modeling 侧输入面，跨域断言归 runtime/policy
 *     契约测试
 *
 * ★ V-09/AT-31 Xacro 黄金样例面承载于 test/GoldenImportTest.cpp（两模式
 *   均编译）——本 TU 不重复：XacroExpand.hpp 与 DhConvert.hpp 存在
 *   ExpandOutcome 同名类型冲突（存量头面缺陷，findings 登记），本 TU 经
 *   CommandFixtures→CommandHandlers→DhConvert.hpp 链无法再包含
 *   XacroExpand.hpp。
 *
 * 设计依据：units/modeling.md §5.1/§6.1/§6.7/§10.2、units/testkit.md
 * §4.2；需求 AT-01/AT-31/AT-37、MDL-22、CON-03、NFR-SEC-02；runtime/
 * project 对端既有注册面＝CompileBridge/DescriptionBridge/CommandPipeline
 * 三契约测试（T08/T12 在册——本文件不重复其断言）。
 *
 * 集成模式专属（TARGET sdurw_kinematics gating——project 对端面消费
 * policy 行程评估器与 CommandFixtures 替身；io 对端两用例同文件随行，
 * 冒烟口径由 GoldenImportTest 承载）。
 */

#include "../test/CommandFixtures.hpp"

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/ResourceIo.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/modeling/Import.hpp>
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::modeling::testfixture;
namespace modeling = ::sdurws::ird::modeling;
namespace core = ::sdurws::ird::core;
namespace project = ::sdurws::ird::project;
namespace policy = ::sdurws::ird::policy;
namespace runtime = ::sdurws::ird::runtime;
namespace tk = ::sdurws::ird::testkit;
using namespace modeling;  // NOLINT——被测契约面直用

namespace {

// IRD_EXPECT_IDENTICAL 展开的 checkIdentical 以未限定名查找——显式引入。
using tk::checkIdentical;

using sdurws::ird::io::ResourceDependencyTree;
using sdurws::ird::io::ResourceEdge;
using sdurws::ird::io::ResourceEdgeKind;
using sdurws::ird::io::ResourceNode;
using sdurws::ird::io::ResourceSnapshot;

// ---- 通用小工具（GoldenImportTest 同形自持副本——两目标各自独立链接） ----

/// 读取数据集文件全文（读失败显性失败——黄金资产损坏不得静默跳过）。
std::string readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << p.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 字节串助手（string→vector<uint8_t>——io 产物字节形态）。
std::vector<std::uint8_t> asBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

/// 真实摘要（core ContentDigester——SA-12 唯一算法）。
core::Digest256 digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/// JSON 取串（缺字段即失败——数据集域内 schema 契约）。
std::string jStr(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return {};
    }
    return v->text;
}

/// "存在"的依赖树节点（digest 对给定字节真实计算——io §8.2 三元组形态）。
ResourceNode nodeWithBytes(const std::string& relPath,
                           const std::vector<std::uint8_t>& bytes)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = true;
    ResourceSnapshot snapshot;
    snapshot.finalPath = std::string("Z:/fake/") + relPath;
    snapshot.sizeBytes = bytes.size();
    snapshot.mtimeUtc = 0;
    snapshot.contentDigest = digestBytes(bytes);
    node.snapshot = snapshot;
    return node;
}

/// "缺失"的依赖树节点（io §6.5 缺失叶——exists=false）。
ResourceNode missingNode(const std::string& relPath)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = false;
    return node;
}

/// mesh 引用边。
ResourceEdge meshEdge(const std::string& fromRel, const std::string& toRel)
{
    ResourceEdge edge;
    edge.fromRel = fromRel;
    edge.toRel = toRel;
    edge.kind = ResourceEdgeKind::Mesh;
    return edge;
}

/**
 * @brief 装配 golden-6r.urdf 的 ValidatedSource（io 三产物值契约——
 *        字节＋快照＋依赖树；l1.stl 存在叶＋ghost.dae 缺失叶）。
 */
modeling::ValidatedSource makeGoldenSource(const tk::GoldenDataset& ds)
{
    const std::string urdf = readFile(ds.resolveInput("inputs/golden-6r.urdf"));
    const std::vector<std::uint8_t> meshBytes
        = asBytes(readFile(ds.resolveInput("inputs/meshes/l1.stl")));

    modeling::ValidatedSource source;
    source.bytes = asBytes(urdf);
    source.entrySnapshot.finalPath = "Z:/fake/golden-6r.urdf";
    source.entrySnapshot.sizeBytes = source.bytes.size();
    source.entrySnapshot.mtimeUtc = 0;
    source.entrySnapshot.contentDigest = digestBytes(source.bytes);

    ResourceDependencyTree tree;
    tree.rootRel = "golden-6r.urdf";
    tree.nodes.push_back(nodeWithBytes("golden-6r.urdf", source.bytes));
    tree.nodes.push_back(nodeWithBytes("meshes/l1.stl", meshBytes));
    tree.nodes.push_back(missingNode("meshes/ghost.dae"));
    tree.edges.push_back(meshEdge("golden-6r.urdf", "meshes/l1.stl"));
    tree.edges.push_back(meshEdge("golden-6r.urdf", "meshes/ghost.dae"));
    std::sort(tree.nodes.begin(), tree.nodes.end(),
              [](const ResourceNode& a, const ResourceNode& b) {
                  return a.relPath < b.relPath;
              });
    source.dependencyTree = std::move(tree);
    return source;
}

/// 在诊断清单中查找指定码的首条记录下标（无则返回 npos）。
std::size_t findDiag(const std::vector<core::DiagnosticRecord>& diags,
                     std::string_view code)
{
    for (std::size_t i = 0; i < diags.size(); ++i) {
        if (diags[i].code == code) { return i; }
    }
    return static_cast<std::size_t>(-1);
}

/// 命令面夹具（CommandPipelineContractTest PipelineFixture 同形——服务集
/// ＋空基线闭包＝首应用场景）。
struct GoldenApplyFixture {
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator
        = policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    TestQueryPort query;
    MockCompilePort compile;
    HandlerServices services{
        AssertionSuite::Ports{evaluator.get(), &names}, &provider, makeOid(),
        std::nullopt};

    void bindNames(const RobotDesign& design)
    {
        for (const JointEntry& j : design.joints) {
            names.byId[j.objectId] = "Robot/" + j.localName;
        }
    }
};

}  // namespace

// =====================================================================
// ACC5（io 对端——导入管线）：io 缺失清单值语义的对端消费契约
// =====================================================================

/**
 * @brief io 对端导入管线（T05/T13 协作面，V-06/V-08 golden 面）：io 三产物
 *   值（ValidatedSource 契约——字节/快照/依赖树）进 mapUrdf 后——缺失叶
 *   ghost.dae 产出 IO-RES-MISSING 事实诊断（io 稳定码经 modeling 侧呈现）
 *   ＋报告资源行 "missing"（digestHex 空——io §6.5 缺失清单值语义）＋草稿
 *   resourceManifest 以零摘要 Recorded 携带（§6.7"缺失≠不可行"）；存在叶
 *   l1.stl 全程 recorded（真实字节摘要贯通报告与清单）。
 */
TEST(MdlGoldenPeerContract, IoPeerImportPipelineMissingLeafContract_WP13T16_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "CON-03"},
                  std::vector<std::string>{"AT-15"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-urdf-import", "1.0.0"}));

    const ModelImportMapper mapper;
    std::vector<core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeGoldenSource(ds),
                                                 ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());

    // io 缺失叶→IO-RES-MISSING 事实诊断（码面＝io 族稳定码，字面经
    // DiagCodesTest 注册在册——此处对端消费呈现）。
    EXPECT_NE(findDiag(diags, "IO-RES-MISSING"), static_cast<std::size_t>(-1))
        << "缺失叶必须产出 IO-RES-MISSING 事实诊断（io §6.5 对端语义）";

    // 报告资源行：缺失叶 state=missing＋digestHex 空（未取得内容不伪造）。
    bool sawMissingReported = false;
    for (const auto& res : outcome.report.resources) {
        if (res.resourceId == "meshes/ghost.dae") {
            sawMissingReported = true;
            EXPECT_EQ(res.state, "missing");
            EXPECT_TRUE(res.digestHex.empty());
        }
    }
    EXPECT_TRUE(sawMissingReported) << "报告资源表缺缺失叶行";

    // 草稿面：缺失叶以零摘要 Recorded 携带（§6.7——草稿可携带，V-08）。
    bool sawMissingCarried = false;
    for (const auto& res : outcome.draft->resourceManifest) {
        if (res.resourceId == "meshes/ghost.dae") {
            sawMissingCarried = true;
            EXPECT_EQ(res.state, modeling::ResourceState::Recorded);
            EXPECT_TRUE(res.contentDigest == core::Digest256{});
        }
    }
    EXPECT_TRUE(sawMissingCarried) << "缺失叶必须以 Recorded 携带进草稿";
}

// =====================================================================
// ACC5（project 对端——V-01 收口）：黄金模板参数经命令通道不漂移
// =====================================================================

/**
 * @brief project 对端 V-01 收口（AT-01）：mdl-template-6r 黄金创建请求→
 *   createDraft 草稿→apply-robot-design 命令 prepare——Planned＋计划恰
 *   含 1 个根对象写（V-01"恰好 1 个新修订"的 prepare 计划面）＋prepare
 *   零编译调用（编译归 project S5——MockCompilePort 计数观测）＋写入字
 *   节解码回黄金值（7 连杆/6 关节闭合＋J6 行程恰 4π——模板黄金参数经
 *   命令通道不漂移）。修订创建/HEAD 推进归 project S5 已验契约
 *   （CommandPipelineContractTest V-20 同款范围登记）。
 */
TEST(MdlGoldenPeerContract, ProjectPeerGoldenTemplateApplyPlan_WP13T16_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-04"},
                  std::vector<std::string>{"AT-01"},
                  tk::DatasetRef{"mdl-template-6r", "1.0.0"});

    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-template-6r", "1.0.0"}));
    const tk::JsonValue request = tk::parseJson(readFile(
        ds.resolveInput("inputs/template-request.json")));

    // 黄金创建请求→草稿（模板工厂纯函数——V-01 前半）。
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> draftDiags;
    const TemplateOutcome draft = factory.createDraft(
        jStr(request, "templateId"),
        runtime::InstallationPresetToken::Ground, jStr(request, "localName"),
        draftDiags);
    ASSERT_TRUE(draft.ok()) << "黄金创建请求必须成功";
    const ModelingWorkingSet& ws = draft.get();

    // 命令通道（project 对端）：首应用场景（空基线闭包）→新对象槽。
    GoldenApplyFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));
    const project::RevisionView baseline
        = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;
    f.bindNames(ws.design);

    CommandPayload payload;
    PayloadObjectSlot slot;
    slot.allocateNew = true;
    slot.objectId = core::ObjectId{};
    slot.objectTypeToken = std::string(kRobotDesignObjectType);
    slot.objectBytes = encodeVariant(ws.design);
    payload.objects.push_back(std::move(slot));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload),
        baseline, plan, diags);

    // 计划面：Planned＋恰好 1 个根对象写（V-01"恰好 1 个新修订"的
    // prepare 计划投影）＋双编译声明（S5 触发前提）＋零编译调用。
    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{1})
        << "V-01：恰好一个根对象写（修订计划面）";
    EXPECT_EQ(plan.objectWrites[0].objectTypeToken, std::string(kRobotDesignObjectType))
        << "V-01 观测点：对象 token=robot-design";
    EXPECT_TRUE(plan.requiresDualCompile);
    EXPECT_EQ(f.compile.callCount, 0) << "prepare 不触编译（归 project S5）";

    // 写入字节解码回黄金值：模板黄金参数经命令通道不漂移（J6 行程恰
    // 4π＝附录 D 第 11 项阈值边界——D-MDL-7 基线）。
    RobotDesignCodec codec;
    const auto written = codec.decode(plan.objectWrites[0].payloadCanonical,
                                      kCurrentFormatVersion);
    ASSERT_TRUE(written.ok());
    const RobotDesign& writtenDesign = std::get<RobotDesign>(written.get());
    EXPECT_EQ(writtenDesign.displayName, jStr(request, "localName"));
    ASSERT_EQ(writtenDesign.joints.size(), std::size_t{6});
    ASSERT_EQ(writtenDesign.links.size(), std::size_t{7});
    const auto j6Bounds = writtenDesign.joints[5].bounds.tryValue();
    ASSERT_TRUE(j6Bounds.has_value());
    EXPECT_DOUBLE_EQ(j6Bounds->second - j6Bounds->first, 4.0 * kPi)
        << "J6 行程恰 4π（rad——黄金参数不漂移）";
}

// =====================================================================
// ACC5（AT-37 建模侧输入面——V-12/V-13）：basePlacement 参数化输入面
// =====================================================================

/**
 * @brief AT-37 建模侧输入面（V-12/V-13 golden 导入面）：黄金导入草稿的
 *   basePlacement——①V-12：BasePlacement 值只含 preset/customEaa 参数与
 *   basePosition 位置，无任何预乘旋转矩阵字段（P-RT-4"modeling 只存参
 *   数不存矩阵"——R_world_base 的唯一权威产出点在 runtime 编译链；本
 *   断言钉住输入面无可污染的矩阵载荷）；②V-13：未配置安装路径默认地面
 *   （preset=Ground，MDL-22/V15-04）且导入来源标记入默认补全清单（NFR-
 *   COR-03 不静默）＋basePosition 不伪造零位（NotProvided——缺失≠零）；
 *   ③场景/工具引用为空（"场景 worldPose 未被旋转"的输入面前提——无任
 *   何预乘来源）。三维渲染/碰撞/重力读同一 R_world_base 的联合观测归
 *   runtime/policy 契约测试（跨域断言不冒充）。
 */
TEST(MdlGoldenPeerContract, At37BasePlacementParameterInputFace_WP13T16_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22", "MDL-03"},
                  std::vector<std::string>{"AT-37"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-urdf-import", "1.0.0"}));

    const ModelImportMapper mapper;
    std::vector<core::DiagnosticRecord> diags;
    const ImportOutcome outcome = mapper.mapUrdf(makeGoldenSource(ds),
                                                 ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    const RobotDesign& draft = *outcome.draft;
    const BasePlacement& base = draft.basePlacement;

    // ①V-12 输入面：preset 参数在册、customEaa 空（非 Custom 不适用）、
    //   basePosition 不携带矩阵语义（BasePlacement 无矩阵字段——结构面
    //   由本断言的三个字段全量核对钉住：preset/customEaa/basePosition
    //   之外本值无其他数据通道）。
    EXPECT_EQ(base.preset, runtime::InstallationPresetToken::Ground);
    EXPECT_EQ(base.customEaa.state(), core::FieldState::NotProvided)
        << "非 Custom 预设不得携带 EAA（rad）参数";
    EXPECT_EQ(base.basePosition.state(), core::FieldState::NotProvided)
        << "导入不伪造基座位置（缺失≠零——NFR-COR-03）";

    // ②V-13 导入来源标记：默认地面入默认补全清单（可观察——不静默）。
    bool groundDefaultDeclared = false;
    for (const auto& item : outcome.report.defaults) {
        if (item.field == "basePlacement" && item.appliedValue == "ground(preset)") {
            groundDefaultDeclared = true;
        }
    }
    EXPECT_TRUE(groundDefaultDeclared)
        << "未配置安装路径默认地面必须入默认补全清单（MDL-22/V15-04）";

    // ③场景面输入前提：无场景/工具引用（无预乘来源——worldPose 未被
    //   旋转的建模侧输入面）。
    EXPECT_TRUE(draft.sceneRefs.empty());
    EXPECT_TRUE(draft.toolRefs.empty());
    EXPECT_FALSE(draft.poseSetRef.has_value());
    EXPECT_FALSE(draft.drivetrainRef.has_value());
}
