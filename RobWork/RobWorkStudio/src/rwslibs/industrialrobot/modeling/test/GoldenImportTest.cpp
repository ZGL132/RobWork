/**
 * @file   GoldenImportTest.cpp
 * @brief  URDF 导入黄金数据集全链用例组（MdlGoldenImport）——契约
 *         tasks/foundation/WP-13-T16.json acceptance 3/4 的具名自证：
 *
 *   V-06 导入黄金链（AT-15）：mdl-urdf-import 数据集 golden-6r.urdf 经
 *     mapUrdf 全链（io 三产物装配→映射→报告/草稿）——报告四清单逐项
 *     计数与数据集期望 expected/import-report.json 逐项一致（映射/默认
 *     补全/忽略/不支持＋待确认/关节状态/资源状态/自碰撞与传动候选），
 *     草稿值面（轴归一化/默认 +X/物性/材料/地面预设）与期望一致
 *   惯量数据质量门（ACC4——档案消费）：draft l1 惯量经档案 mdl-dh 的
 *     inertia.symmetry（相对 1e-12）与 inertia.spd-eigenvalue（零余量）
 *     条目承载的对称性/正定口径核对（附录 D 第 6/7 项；SPD 判定权威＝
 *     I-MDL-5 单一实现——checkInvariants）
 *   V-09/AT-31 Xacro 黄金样例：golden-macro.xacro 受控展开（属性/宏）
 *     →mapXacroExpanded 走 mapUrdf 同一映射（三轴 BeyondTemplateRange
 *     ＋TEMPLATE-RANGE 提示在案＋来源 digest 留痕）；cycle-a/b.xacro
 *     include 相互环→IncludeCycle＋IO-FORMAT-XML-CYCLE 透传、无产物
 *
 * 设计依据：units/modeling.md §6.3/§6.4/§6.5/§10.1/§10.2（V-06/07/08/09
 * 行）、units/testkit.md §4.2/§4.3；需求 MDL-03/04/11/12/19/22、
 * AT-15/17/31、NFR-COR-01（黄金数据集为独立正确性依据）。
 *
 * 两模式均编译：被测面（Import/XacroExpand 映射服务＋testkit）为纯函数
 * 服务，自持 io 产物值装配（ImportTest/XacroExpandTest 同款），零命令/
 * 策略/框架符号依赖。
 */

#include <rw/math/Transform3D.hpp>  // rw::math::Transform3D<double>——JointPose 转换消费

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/io/ResourceIo.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/modeling/Import.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/modeling/XacroExpand.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
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

namespace {

namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird;  // 嵌套单元名可见（core::/io::/runtime:: 前缀解析）

// IRD_EXPECT_* 宏以未限定名展开 checkCloseWithin/checkAtMost/checkIdentical
// ——本 TU 在匿名命名空间内使用，须显式引入（GoldenTemplateTest 同款注）。
using tk::checkAtMost;
using tk::checkCloseWithin;
using tk::checkIdentical;

using sdurws::ird::io::ResourceDependencyTree;
using sdurws::ird::io::ResourceEdge;
using sdurws::ird::io::ResourceEdgeKind;
using sdurws::ird::io::ResourceNode;
using sdurws::ird::io::ResourceSnapshot;
using sdurws::ird::modeling::ImportOptions;
using sdurws::ird::modeling::ImportOutcome;
using sdurws::ird::modeling::ModelImportMapper;
using sdurws::ird::modeling::ValidatedSource;
using sdurws::ird::modeling::ValidatedXacroSource;
using sdurws::ird::modeling::XacroExpandErrorCode;
using sdurws::ird::modeling::XacroExpandService;

// ---- 通用小工具（GoldenTemplateTest 同形自持副本——测试域局部） ----

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

/// 真实摘要（core ContentDigester——SA-12 唯一算法；黄金树节点的 digest
/// 对资产字节真实计算，"recorded" 状态不被占位值弱化）。
sdurws::ird::core::Digest256 digestBytes(const std::vector<std::uint8_t>& bytes)
{
    sdurws::ird::core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
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

/// 装载 mdl-urdf-import 黄金数据集（装载失败＝数据资产缺陷，显性失败）。
tk::GoldenDataset loadImportDataset()
{
    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-urdf-import", "1.0.0"}))
        << "mdl-urdf-import 装载失败（数据集非法级——testkit §7.2）";
    return ds;
}

/// 装载容差档案 mdl-dh（惯量数据质量门的条目来源）。
tk::ToleranceProfile loadMdlDhProfile()
{
    return tk::ToleranceProfile::load(tk::toleranceProfileDir("mdl-dh")
                                      / "v1.0.0.json");
}

// ---- io 产物装配（ImportTest/XacroExpandTest 同款——依赖树形态真实） ----

/// "存在"的依赖树节点（digest 对给定字节真实计算——io §8.2 三元组形态；
/// finalPath 仅追溯提示，映射器不以路径作身份）。
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

/// "缺失"的依赖树节点（io §6.5 缺失叶——exists=false；V-08 面）。
ResourceNode missingNode(const std::string& relPath)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = false;
    return node;
}

/// mesh 引用边（入口文档 → 相对键）。
ResourceEdge meshEdge(const std::string& fromRel, const std::string& toRel)
{
    ResourceEdge edge;
    edge.fromRel = fromRel;
    edge.toRel = toRel;
    edge.kind = ResourceEdgeKind::Mesh;
    return edge;
}

/// include 引用边（XacroExpandTest 同款——kind=Include）。
ResourceEdge includeEdge(const std::string& fromRel, const std::string& toRel)
{
    ResourceEdge edge;
    edge.fromRel = fromRel;
    edge.toRel = toRel;
    edge.kind = ResourceEdgeKind::Include;
    return edge;
}

/**
 * @brief 装配 golden-6r.urdf 的 ValidatedSource（V-06 全链输入面）：
 *        入口文档＋l1.stl 存在叶（digest＝资产字节真实 SHA-256）＋
 *        ghost.dae 缺失叶（V-08）；package:// 不入树（io 不可达——仅不
 *        支持项承载，与数据集 generate 脚本头注的计数约定一致）。
 */
ValidatedSource makeGoldenSource(const tk::GoldenDataset& ds)
{
    const std::string urdf = readFile(ds.resolveInput("inputs/golden-6r.urdf"));
    const std::vector<std::uint8_t> meshBytes
        = asBytes(readFile(ds.resolveInput("inputs/meshes/l1.stl")));

    ValidatedSource source;
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
    // io 稳定序（relPath 字典序——产物形态真实；映射器不依赖节点序）。
    std::sort(tree.nodes.begin(), tree.nodes.end(),
              [](const ResourceNode& a, const ResourceNode& b) {
                  return a.relPath < b.relPath;
              });
    source.dependencyTree = std::move(tree);
    return source;
}

/// 在诊断清单中查找指定码的首条记录下标（无则返回 npos）。
std::size_t findDiag(const std::vector<sdurws::ird::core::DiagnosticRecord>& diags,
                     std::string_view code)
{
    for (std::size_t i = 0; i < diags.size(); ++i) {
        if (diags[i].code == code) { return i; }
    }
    return static_cast<std::size_t>(-1);
}

/// 组装 ValidatedXacroSource（入口字节＋真实摘要快照＋树＋字节表——
/// XacroExpandTest makeXacroSource 同款装配面；树由调用方装配并复用）。
ValidatedXacroSource makeXacroSource(
    const std::string& entryRel, const std::vector<std::uint8_t>& entryBytes,
    const ResourceDependencyTree& tree,
    std::map<std::string, std::vector<std::uint8_t>> includeBytes = {})
{
    ValidatedXacroSource source;
    source.entryBytes = entryBytes;
    source.entrySnapshot.finalPath = std::string("Z:/fake/") + entryRel;
    source.entrySnapshot.sizeBytes = entryBytes.size();
    source.entrySnapshot.mtimeUtc = 0;
    source.entrySnapshot.contentDigest = digestBytes(entryBytes);
    source.dependencyTree = tree;
    source.includeBytes = std::move(includeBytes);
    return source;
}

/// 构造单节点树（入口文件自含——无 include 的黄金宏样例用）。
ResourceDependencyTree singleNodeTree(const std::string& entryRel,
                                      const std::vector<std::uint8_t>& entryBytes)
{
    ResourceDependencyTree tree;
    tree.rootRel = entryRel;
    tree.nodes.push_back(nodeWithBytes(entryRel, entryBytes));
    return tree;
}

/// 把展开产物装配回 mapXacroExpanded 的 ValidatedSource（调用方装配面
/// ——展开字节＋产物摘要快照＋原依赖树；XacroExpandTest 同款）。
ValidatedSource assembleExpandedSource(
    const sdurws::ird::modeling::ExpandOutcome& outcome,
    const ResourceDependencyTree& tree)
{
    ValidatedSource source;
    source.bytes = *outcome.expandedBytes;
    source.entrySnapshot.finalPath = "Z:/fake/golden-macro-expanded.urdf";
    source.entrySnapshot.sizeBytes = source.bytes.size();
    source.entrySnapshot.mtimeUtc = 0;
    source.entrySnapshot.contentDigest = digestBytes(source.bytes);
    source.dependencyTree = tree;
    return source;
}

}  // namespace

// =====================================================================
// ACC3（V-06 报告面）：八清单逐项计数与数据集期望一致
// =====================================================================

/**
 * @brief V-06 报告面（AT-15/17）：golden-6r.urdf 经 mapUrdf 全链——
 *        报告各清单逐项计数与 expected/import-report.json 的 counts 段
 *        一致：关节状态 6 全 mapped；资源状态 2（l1.stl recorded／
 *        ghost.dae missing——V-08 缺失叶事实）；自碰撞候选恰 base-l1
 *        一对（P-MDL-3 仅报告不写策略对象）；传动回填候选恰 5（j3 无
 *        limit 故无）；待确认 2（j2 缺轴默认 +X／j3 缺限位）；忽略 2
 *        （gazebo/robot 级 material）；不支持 8（transmission 1＋
 *        velocity 无落点 5＋非单位 mesh-scale 1＋package:// ros-uri 1）；
 *        可提交且无命名阻断错误（AT-15 断言承载）。
 */
TEST(MdlGoldenImport, GoldenUrdfReportCounts_V06_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-11", "MDL-12"},
                  std::vector<std::string>{"AT-15", "AT-17"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    const tk::GoldenDataset ds = loadImportDataset();
    const tk::JsonValue expectedRoot = tk::parseJson(readFile(
        ds.resolveExpected("expected/import-report.json")));
    const tk::JsonValue* counts = expectedRoot.find("counts");
    ASSERT_NE(counts, nullptr) << "期望文件缺 counts 段";

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome
        = mapper.mapUrdf(makeGoldenSource(ds), ImportOptions{}, diags);

    // 草稿产出（golden 样例无不可表达类型/分支——无阻断型失败）。
    ASSERT_TRUE(outcome.draft.has_value()) << "黄金导入必须产出草稿";
    EXPECT_FALSE(outcome.error.has_value()) << "黄金样例无命名阻断条件";
    const auto& report = outcome.report;

    // —— 关节状态表：恰 6 条且全 mapped（无零轴/非法轴）——
    ASSERT_EQ(report.jointStatuses.size(),
              static_cast<std::size_t>(jNum(*counts, "jointStatuses")));
    ASSERT_NE(counts->find("jointStatusesAllMapped"), nullptr);
    EXPECT_TRUE(counts->find("jointStatusesAllMapped")->boolean);
    for (const auto& status : report.jointStatuses) {
        IRD_EXPECT_IDENTICAL("import.jointStatus." + status.jointName,
                             status.status, std::string("mapped"));
    }

    // —— 资源状态表：恰 2 条；状态逐项与期望一致（V-08 缺失叶事实）——
    ASSERT_EQ(report.resources.size(),
              static_cast<std::size_t>(jNum(*counts, "resources")));
    const tk::JsonValue* resStates = counts->find("resourceStates");
    ASSERT_NE(resStates, nullptr);
    for (const auto& res : report.resources) {
        const tk::JsonValue* st = resStates->find(res.resourceId);
        ASSERT_NE(st, nullptr) << "期望外资源: " << res.resourceId;
        IRD_EXPECT_IDENTICAL("import.resource." + res.resourceId + ".state",
                             res.state, st->text);
    }
    // 缺失叶的 digestHex 为空串（io §6.5 缺失——未取得内容不伪造）。
    ASSERT_NE(counts->find("missingResourceDigestHexEmpty"), nullptr);
    if (counts->find("missingResourceDigestHexEmpty")->boolean) {
        for (const auto& res : report.resources) {
            if (res.state == "missing") {
                EXPECT_TRUE(res.digestHex.empty())
                    << "缺失资源 digest 必须为空: " << res.resourceId;
            }
        }
    }

    // —— 自碰撞候选：恰 1 对且为 base/l1（P-MDL-3 报告面）——
    ASSERT_EQ(report.selfCollisionCandidates.size(),
              static_cast<std::size_t>(jNum(*counts, "selfCollisionCandidates")));
    const tk::JsonValue* pair = counts->find("selfCollisionPair");
    ASSERT_NE(pair, nullptr);
    ASSERT_FALSE(report.selfCollisionCandidates.empty());
    IRD_EXPECT_IDENTICAL("import.selfCollision.link1",
                         report.selfCollisionCandidates.front().link1,
                         jStr(*pair, "link1"));
    IRD_EXPECT_IDENTICAL("import.selfCollision.link2",
                         report.selfCollisionCandidates.front().link2,
                         jStr(*pair, "link2"));

    // —— 传动回填候选：恰 5 条（effort 关节集合精确一致，按报告序）——
    ASSERT_EQ(report.drivetrainCandidates.size(),
              static_cast<std::size_t>(jNum(*counts, "drivetrainCandidates")));
    const tk::JsonValue* dtJoints = counts->find("drivetrainCandidateJoints");
    ASSERT_NE(dtJoints, nullptr);
    ASSERT_EQ(report.drivetrainCandidates.size(), dtJoints->items.size());
    for (std::size_t i = 0; i < dtJoints->items.size(); ++i) {
        IRD_EXPECT_IDENTICAL("import.drivetrain[" + std::to_string(i) + "]",
                             report.drivetrainCandidates[i].jointName,
                             dtJoints->items[i].text);
    }

    // —— 待确认清单：恰 2 条；kind/subject 逐项一致（j2 缺轴/j3 缺限位）——
    ASSERT_EQ(report.pendingConfirms.size(),
              static_cast<std::size_t>(jNum(*counts, "pendingConfirms")));
    const tk::JsonValue* pendingKinds = counts->find("pendingKinds");
    ASSERT_NE(pendingKinds, nullptr);
    ASSERT_EQ(report.pendingConfirms.size(), pendingKinds->items.size());
    for (std::size_t i = 0; i < pendingKinds->items.size(); ++i) {
        IRD_EXPECT_IDENTICAL("import.pending[" + std::to_string(i) + "].kind",
                             report.pendingConfirms[i].kind,
                             jStr(pendingKinds->items[i], "kind"));
        IRD_EXPECT_IDENTICAL("import.pending[" + std::to_string(i) + "].subject",
                             report.pendingConfirms[i].subject,
                             jStr(pendingKinds->items[i], "subject"));
    }

    // —— 忽略清单：恰 2 条；元素名集合一致（按报告行序＝文档序）——
    ASSERT_EQ(report.ignored.size(),
              static_cast<std::size_t>(jNum(*counts, "ignored")));
    const tk::JsonValue* ignoredElements = counts->find("ignoredElements");
    ASSERT_NE(ignoredElements, nullptr);
    ASSERT_EQ(report.ignored.size(), ignoredElements->items.size());
    for (std::size_t i = 0; i < ignoredElements->items.size(); ++i) {
        IRD_EXPECT_IDENTICAL("import.ignored[" + std::to_string(i) + "]",
                             report.ignored[i].element,
                             ignoredElements->items[i].text);
    }

    // —— 不支持清单：恰 8 条；kind 分布逐项一致且无期望外 kind ——
    ASSERT_EQ(report.unsupported.size(),
              static_cast<std::size_t>(jNum(*counts, "unsupported")));
    std::map<std::string, int> kindCounts;
    for (const auto& item : report.unsupported) { ++kindCounts[item.kind]; }
    const tk::JsonValue* unsupportedKinds = counts->find("unsupportedKinds");
    ASSERT_NE(unsupportedKinds, nullptr);
    int declaredTotal = 0;
    for (const tk::JsonValue& entry : unsupportedKinds->items) {
        const std::string kind = jStr(entry, "kind");
        const int want = static_cast<int>(jNum(entry, "count"));
        declaredTotal += want;
        EXPECT_EQ(kindCounts[kind], want)
            << "不支持类 " << kind << " 计数与期望不符";
    }
    EXPECT_EQ(static_cast<int>(report.unsupported.size()), declaredTotal)
        << "不支持清单总数与期望分布之和一致";

    // —— 映射/默认补全下界（数据集 generate 脚本头注的计数约定：粒度
    // 属实现细化，以 countAtLeast＋关键命中承载）——
    EXPECT_GE(report.mapped.size(),
              static_cast<std::size_t>(jNum(*counts, "mappedCountAtLeast")));
    EXPECT_GE(report.defaults.size(),
              static_cast<std::size_t>(jNum(*counts, "defaultsCountAtLeast")));

    // —— 可提交面：submittable 且无错误项（AT-15 断言收口）——
    ASSERT_NE(counts->find("submittable"), nullptr);
    ASSERT_NE(counts->find("noNamedBlockingError"), nullptr);
    EXPECT_EQ(report.submittable,
              counts->find("submittable")->boolean);
    EXPECT_EQ(report.errors.empty(),
              counts->find("noNamedBlockingError")->boolean);
}

// =====================================================================
// ACC3（V-06 草稿面）：草稿值与数据集期望一致
// =====================================================================

/**
 * @brief V-06 草稿面（AT-15）：导入草稿逐字段与 expected/import-report.json
 *        的 draft 段一致——呈现名保留原文／计数闭合／类型序列／j1 任意轴
 *        归一化（MDL-11）／j1 限位与原点／j2 缺轴默认 +X（待确认入草稿）／
 *        l1 物性与材料（密度 NotProvided 不伪造）／l2 无 inertial＝
 *        NotProvided／未配置路径默认地面（MDL-22/V15-04——V-13 导入来源
 *        面）／资源清单 2 条且缺失叶以零摘要 Recorded 携带（V-08——
 *        "缺失≠不可行"，§6.7）。
 */
TEST(MdlGoldenImport, GoldenUrdfDraftValues_V06_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-11", "MDL-22"},
                  std::vector<std::string>{"AT-15"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    const tk::GoldenDataset ds = loadImportDataset();
    const tk::JsonValue expectedRoot = tk::parseJson(readFile(
        ds.resolveExpected("expected/import-report.json")));
    const tk::JsonValue* draftExp = expectedRoot.find("draft");
    ASSERT_NE(draftExp, nullptr) << "期望文件缺 draft 段";

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome
        = mapper.mapUrdf(makeGoldenSource(ds), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    const sdurws::ird::modeling::RobotDesign& draft = *outcome.draft;

    // 呈现名＝robot name 原文（净化候选入默认补全清单——报告面承载）。
    IRD_EXPECT_IDENTICAL("draft.displayName", draft.displayName,
                         jStr(*draftExp, "displayName"));
    // 计数关系（I-MDL-1：links==joints+1）。
    EXPECT_EQ(draft.joints.size(),
              static_cast<std::size_t>(jNum(*draftExp, "jointCount")));
    EXPECT_EQ(draft.links.size(),
              static_cast<std::size_t>(jNum(*draftExp, "linkCount")));

    // 类型序列（六行全 Revolute）。
    const tk::JsonValue* jointTypes = draftExp->find("jointTypes");
    ASSERT_NE(jointTypes, nullptr);
    ASSERT_EQ(draft.joints.size(), jointTypes->items.size());
    for (std::size_t i = 0; i < draft.joints.size(); ++i) {
        IRD_EXPECT_IDENTICAL(
            "draft.joints[" + std::to_string(i) + "].type",
            std::string(sdurws::ird::modeling::jointTypeToken(draft.joints[i].type)),
            jointTypes->items[i].text);
    }

    // j1 任意有限非零轴归一化（源 "0 2 0"→(0,1,0)——MDL-11）。
    const tk::JsonValue* j1Axis = draftExp->find("j1AxisNormalized");
    ASSERT_NE(j1Axis, nullptr);
    ASSERT_EQ(j1Axis->items.size(), std::size_t{3});
    const auto j1AxisVal = draft.joints[0].axis.tryValue();
    ASSERT_TRUE(j1AxisVal.has_value()) << "j1 轴必须 Provided";
    for (int c = 0; c < 3; ++c) {
        EXPECT_DOUBLE_EQ((*j1AxisVal)[static_cast<std::size_t>(c)],
                         j1Axis->items[static_cast<std::size_t>(c)].number)
            << "j1 轴分量 " << c;
    }
    // j1 限位与原点 z（URDF 字面直映——文本解析无容差通道）。
    const tk::JsonValue* j1Bounds = draftExp->find("j1BoundsRad");
    ASSERT_NE(j1Bounds, nullptr);
    ASSERT_EQ(j1Bounds->items.size(), std::size_t{2});
    const auto j1BoundsVal = draft.joints[0].bounds.tryValue();
    ASSERT_TRUE(j1BoundsVal.has_value());
    EXPECT_DOUBLE_EQ(j1BoundsVal->first, j1Bounds->items[0].number)
        << "j1 qmin（单位 rad）";
    EXPECT_DOUBLE_EQ(j1BoundsVal->second, j1Bounds->items[1].number)
        << "j1 qmax（单位 rad）";
    const auto j1OriginVal = draft.joints[0].origin.tryValue();
    ASSERT_TRUE(j1OriginVal.has_value());
    EXPECT_DOUBLE_EQ(
        static_cast<rw::math::Transform3D<double>>(*j1OriginVal).P()[2],
        jNum(*draftExp, "j1OriginZM"))
        << "j1 原点 z（单位 m，父连杆系下）";

    // j2 缺 axis——默认局部 +X 入待确认草稿（MDL-11；确认流不代决）。
    const tk::JsonValue* j2Axis = draftExp->find("j2AxisDefaultPlusX");
    ASSERT_NE(j2Axis, nullptr);
    ASSERT_EQ(j2Axis->items.size(), std::size_t{3});
    const auto j2AxisVal = draft.joints[1].axis.tryValue();
    ASSERT_TRUE(j2AxisVal.has_value()) << "j2 默认轴必须已补全入草稿";
    EXPECT_DOUBLE_EQ((*j2AxisVal)[0], j2Axis->items[0].number);
    EXPECT_DOUBLE_EQ((*j2AxisVal)[1], j2Axis->items[1].number);
    EXPECT_DOUBLE_EQ((*j2AxisVal)[2], j2Axis->items[2].number);

    // l1 物性/材料：mass/inertia 直映；材料名映射；密度 NotProvided
    // （导入无密度语义——不伪造数值，NFR-COR-03）。
    const auto& l1 = draft.links[1];
    const auto l1Mass = l1.body.mass.tryValue();
    ASSERT_TRUE(l1Mass.has_value());
    EXPECT_DOUBLE_EQ(*l1Mass, jNum(*draftExp, "l1MassKg")) << "l1 质量（单位 kg）";
    const auto l1Inertia = l1.body.inertia.tryValue();
    ASSERT_TRUE(l1Inertia.has_value());
    EXPECT_DOUBLE_EQ(l1Inertia->ixx, jNum(*draftExp, "l1InertiaIxx"))
        << "l1 Ixx（kg·m²）";
    EXPECT_DOUBLE_EQ(l1Inertia->ixy, jNum(*draftExp, "l1InertiaIxy"))
        << "l1 Ixy（kg·m²）";
    ASSERT_TRUE(l1.body.material.has_value());
    IRD_EXPECT_IDENTICAL("draft.links[1].materialId", l1.body.material->materialId,
                         jStr(*draftExp, "l1MaterialId"));
    EXPECT_EQ(l1.body.material->density.state(),
              sdurws::ird::core::FieldState::NotProvided)
        << "密度 NotProvided（导入无密度语义）";
    IRD_EXPECT_IDENTICAL("draft.links[2].bodyState", std::string("not-provided"),
                         jStr(*draftExp, "l2BodyState"));
    EXPECT_FALSE(draft.links[2].body.mass.tryValue().has_value())
        << "l2 无 inertial——物性缺失面";

    // 未配置安装路径默认地面（MDL-22/V15-04）——V-13 导入来源面。
    IRD_EXPECT_IDENTICAL("draft.basePlacement.preset",
                         draft.basePlacement.preset
                                 == sdurws::ird::runtime::InstallationPresetToken::Ground
                             ? std::string("ground") : std::string("non-ground"),
                         jStr(*draftExp, "basePlacementPreset"));

    // 资源清单：恰 2 条均 Recorded；l1.stl digest＝资产字节真实 SHA-256；
    // ghost.dae 缺失叶以零摘要 Recorded 携带（§6.7"缺失≠不可行"——V-08）。
    ASSERT_NE(draftExp->find("resourceManifestSize"), nullptr);
    ASSERT_NE(draftExp->find("missingCarriedAsRecorded"), nullptr);
    EXPECT_EQ(draft.resourceManifest.size(),
              static_cast<std::size_t>(jNum(*draftExp, "resourceManifestSize")));
    const std::vector<std::uint8_t> meshBytes
        = asBytes(readFile(ds.resolveInput("inputs/meshes/l1.stl")));
    bool sawExisting = false;
    bool sawMissing = false;
    for (const auto& res : draft.resourceManifest) {
        ASSERT_TRUE(res.externalRecord.has_value())
            << "Recorded 态必须带 externalRecord（I-MDL-10）: " << res.resourceId;
        IRD_EXPECT_IDENTICAL(
            "draft.resourceManifest." + res.resourceId + ".state",
            std::string(sdurws::ird::modeling::resourceStateToken(res.state)),
            std::string("Recorded"));
        if (res.resourceId == "meshes/l1.stl") {
            sawExisting = true;
            EXPECT_TRUE(res.contentDigest == digestBytes(meshBytes))
                << "存在叶 digest＝资产字节真实摘要";
        } else if (res.resourceId == "meshes/ghost.dae") {
            sawMissing = true;
            EXPECT_TRUE(res.contentDigest == sdurws::ird::core::Digest256{})
                << "缺失叶 digest 为零保留值（未取得内容不伪造）";
        }
    }
    EXPECT_TRUE(sawExisting) << "资源清单缺 meshes/l1.stl";
    if (draftExp->find("missingCarriedAsRecorded")->boolean) {
        EXPECT_TRUE(sawMissing)
            << "V-08 缺失网格必须以 Recorded 携带进资源清单";
    }
}

// =====================================================================
// ACC4（档案消费）：l1 惯量数据质量门（附录 D 第 6/7 项口径）
// =====================================================================

/**
 * @brief 惯量数据质量门（ACC4——黄金数据集的档案条目消费面）：draft l1
 *   惯量经档案 mdl-dh 承载的两条口径核对——
 *   ① inertia.symmetry（相对 1e-12，第 6 项）：映射产物的对称分量与黄金
 *      期望值逐项一致（黄金 URDF 的 ixy=0.002 为手工构造真值——数据集
 *      侧质量门，防期望文件与资产漂移）；
 *   ② inertia.spd-eigenvalue（零余量，第 7 项）：I-MDL-5 SPD 判定经值
 *      模型单一实现（checkInvariants）全量通过＋对角主项严格为正的零
 *      余量上界观测（特征值口径的判定权威＝I-MDL-5 单一实现，本条目以
 *      零容差承载"不设数值余量"——不另设第二份特征值分解，NFR-MNT-04）。
 */
TEST(MdlGoldenImport, GoldenUrdfInertiaDataQualityGate_WP13T16_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-03", "MDL-05"},
                  std::vector<std::string>{"AT-15"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    const tk::GoldenDataset ds = loadImportDataset();
    const tk::ToleranceProfile profile = loadMdlDhProfile();
    const tk::JsonValue expectedRoot = tk::parseJson(readFile(
        ds.resolveExpected("expected/import-report.json")));
    const tk::JsonValue* draftExp = expectedRoot.find("draft");
    ASSERT_NE(draftExp, nullptr);

    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ImportOutcome outcome
        = mapper.mapUrdf(makeGoldenSource(ds), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value());
    const auto inertia = outcome.draft->links[1].body.inertia.tryValue();
    ASSERT_TRUE(inertia.has_value()) << "l1 惯量必须 Provided";

    // ① 对称性门（第 6 项——档案相对 1e-12；黄金值即数据质量参考）。
    IRD_EXPECT_CLOSE("inertia.symmetry", inertia->ixy,
                     jNum(*draftExp, "l1InertiaIxy"), profile, "kg*m^2");
    IRD_EXPECT_CLOSE("inertia.symmetry", inertia->ixx,
                     jNum(*draftExp, "l1InertiaIxx"), profile, "kg*m^2");

    // ② 正定门（第 7 项零余量）：I-MDL-5 单一判定（SPD＋三角不等式）
    // 全量通过＋对角主项严格为正（−min ≤ 0 的零容差上界观测）。
    EXPECT_TRUE(sdurws::ird::modeling::checkInvariants(*outcome.draft).empty())
        << "黄金惯量必须过 I-MDL-5（SPD——特征值口径的单一判定）";
    const double minDiag = std::min({inertia->ixx, inertia->iyy, inertia->izz});
    EXPECT_GT(minDiag, 0.0) << "对角主项严格为正（第 7 项零余量口径）";
    IRD_EXPECT_AT_MOST("inertia.spd-eigenvalue", -minDiag, 0.0, "kg*m^2");
}

// =====================================================================
// ACC3（V-09/AT-31 Xacro 黄金样例）：宏属性展开全链与循环透传
// =====================================================================

/**
 * @brief Xacro 黄金宏样例全链（AT-31/V-09 正半区）：golden-macro.xacro
 *   受控展开（property link_lift_m=0.25＋宏 rev_joint 三次调用）→
 *   mapXacroExpanded 走 mapUrdf 同一映射——三轴 BeyondTemplateRange
 *   （1~3 轴无模板语义，类型保留）＋TEMPLATE-RANGE 提示在案＋限位/原
 *   点期望逐项一致＋substitutions 为空（映射清单零替换留痕条目——确
 *   定性）＋原始 .xacro digest 以 Recorded 资源行进草稿 resourceManifest
 *   （来源留痕，MDL-19）。
 */
TEST(MdlGoldenImport, GoldenXacroMacroMapping_AT31_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19", "MDL-12"},
                  std::vector<std::string>{"AT-31"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    const tk::GoldenDataset ds = loadImportDataset();
    const tk::JsonValue expectedRoot = tk::parseJson(readFile(
        ds.resolveExpected("expected/xacro-samples.json")));
    const tk::JsonValue* goldenExp = expectedRoot.find("goldenMacro");
    ASSERT_NE(goldenExp, nullptr) << "期望文件缺 goldenMacro 段";

    // 第一步：受控展开（无 include——单节点树；无用户 substitutions）。
    const std::vector<std::uint8_t> xacroBytes
        = asBytes(readFile(ds.resolveInput("inputs/xacro/golden-macro.xacro")));
    const ResourceDependencyTree entryTree
        = singleNodeTree("golden-macro.xacro", xacroBytes);
    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> expandDiags;
    const sdurws::ird::modeling::ExpandOutcome expanded = service.expand(
        makeXacroSource("golden-macro.xacro", xacroBytes, entryTree), {},
        expandDiags);
    ASSERT_TRUE(expanded.expandedBytes.has_value()) << "黄金宏样例展开必须成功";

    // 参数列表＝文档属性定义序（无 substitutions——source 全 property）。
    const tk::JsonValue* paramList = goldenExp->find("parameterList");
    ASSERT_NE(paramList, nullptr);
    ASSERT_EQ(expanded.parameters.size(), paramList->items.size());
    for (std::size_t i = 0; i < paramList->items.size(); ++i) {
        IRD_EXPECT_IDENTICAL("xacro.parameters[" + std::to_string(i) + "].name",
                             expanded.parameters[i].name,
                             jStr(paramList->items[i], "name"));
        IRD_EXPECT_IDENTICAL("xacro.parameters[" + std::to_string(i) + "].value",
                             expanded.parameters[i].valueText,
                             jStr(paramList->items[i], "valueText"));
        IRD_EXPECT_IDENTICAL("xacro.parameters[" + std::to_string(i) + "].source",
                             expanded.parameters[i].source,
                             jStr(paramList->items[i], "source"));
    }

    // 第二步：展开产物→mapXacroExpanded（mapUrdf 同一映射与安全边界；
    // 来源记录随草稿/报告留痕——§6.5）。
    const ModelImportMapper mapper;
    std::vector<sdurws::ird::core::DiagnosticRecord> mapDiags;
    const ImportOutcome outcome = mapper.mapXacroExpanded(
        assembleExpandedSource(expanded, entryTree), expanded.provenance,
        ImportOptions{}, mapDiags);
    ASSERT_TRUE(outcome.draft.has_value()) << "展开产物必须可映射到草稿";
    const auto& draft = *outcome.draft;
    const auto& report = outcome.report;

    // 关节链与期望逐项一致（宏三次调用的展开结果）。
    EXPECT_EQ(draft.joints.size(),
              static_cast<std::size_t>(jNum(*goldenExp, "importJointCount")));
    const tk::JsonValue* jointNames = goldenExp->find("importJointNames");
    ASSERT_NE(jointNames, nullptr);
    ASSERT_EQ(draft.joints.size(), jointNames->items.size());
    const tk::JsonValue* boundsExp = goldenExp->find("importBoundsRad");
    ASSERT_NE(boundsExp, nullptr);
    for (std::size_t i = 0; i < draft.joints.size(); ++i) {
        IRD_EXPECT_IDENTICAL("xacro.draft.joints[" + std::to_string(i) + "].name",
                             draft.joints[i].localName,
                             jointNames->items[i].text);
        ASSERT_EQ(boundsExp->items[i].items.size(), std::size_t{2});
        const auto bounds = draft.joints[i].bounds.tryValue();
        ASSERT_TRUE(bounds.has_value());
        EXPECT_DOUBLE_EQ(bounds->first, boundsExp->items[i].items[0].number)
            << "j" << (i + 1) << " qmin（单位 rad）";
        EXPECT_DOUBLE_EQ(bounds->second, boundsExp->items[i].items[1].number)
            << "j" << (i + 1) << " qmax（单位 rad）";
    }
    // 宏体内 ${link_lift_m} 经调用点作用域解析（文档属性）——origin z。
    const auto origin = draft.joints[0].origin.tryValue();
    ASSERT_TRUE(origin.has_value());
    EXPECT_DOUBLE_EQ(
        static_cast<rw::math::Transform3D<double>>(*origin).P()[2],
        jNum(*goldenExp, "importOriginZM"))
        << "宏参数原点 z（单位 m）";

    // 链型能力：三轴 BeyondTemplateRange＋TEMPLATE-RANGE 提示在案
    // （§6.4 行语义澄清——1~3 轴无模板语义，类型保留草稿可编辑）。
    const tk::JsonValue* chainExp = goldenExp->find("chainCapability");
    ASSERT_NE(chainExp, nullptr);
    IRD_EXPECT_IDENTICAL("xacro.chainCapability.kind",
                         report.chainCapability.kind
                                 == sdurws::ird::modeling::ChainCapabilityKind::FullTemplateRange
                             ? std::string("FullTemplateRange")
                             : std::string("BeyondTemplateRange"),
                         jStr(*chainExp, "kind"));
    EXPECT_EQ(report.chainCapability.movableAxes,
              static_cast<std::uint32_t>(jNum(*chainExp, "movableAxes")));
    ASSERT_NE(chainExp->find("containsPrismatic"), nullptr);
    EXPECT_EQ(report.chainCapability.containsPrismatic,
              chainExp->find("containsPrismatic")->boolean);
    ASSERT_NE(goldenExp->find("templateRangeDiagnosticPresent"), nullptr);
    EXPECT_EQ(findDiag(mapDiags, std::string(
                  sdurws::ird::modeling::kMdlImportTemplateRange))
                  != static_cast<std::size_t>(-1),
              goldenExp->find("templateRangeDiagnosticPresent")->boolean)
        << "TEMPLATE-RANGE 提示在案（超出范围才提示）";

    // 可提交＋空 substitutions 的确定性面（映射清单零替换前缀条目）。
    ASSERT_NE(goldenExp->find("submittable"), nullptr);
    ASSERT_NE(goldenExp->find("substitutionMappedNotes"), nullptr);
    EXPECT_EQ(report.submittable, goldenExp->find("submittable")->boolean);
    int substitutionNotes = 0;
    for (const auto& item : report.mapped) {
        if (item.note.find("xacro-substitution/") != std::string::npos) {
            ++substitutionNotes;
        }
    }
    EXPECT_EQ(substitutionNotes,
              static_cast<int>(jNum(*goldenExp, "substitutionMappedNotes")));

    // 来源留痕：provenance.sourceDigest＝原始 .xacro 字节真实摘要，且以
    // Recorded 资源行进草稿 resourceManifest（MDL-19 来源可追溯）。
    EXPECT_TRUE(expanded.provenance.sourceDigest == digestBytes(xacroBytes));
    bool sourceRowRecorded = false;
    for (const auto& res : draft.resourceManifest) {
        if (res.contentDigest == expanded.provenance.sourceDigest) {
            sourceRowRecorded
                = res.state == sdurws::ird::modeling::ResourceState::Recorded;
        }
    }
    ASSERT_NE(goldenExp->find("xacroSourceResourceRowRecorded"), nullptr);
    EXPECT_EQ(sourceRowRecorded,
              goldenExp->find("xacroSourceResourceRowRecorded")->boolean)
        << "原始 .xacro 以 Recorded 资源进草稿（来源留痕）";
}

/**
 * @brief Xacro include 循环透传（V-09/AT-31 负半区）：黄金样例
 *   cycle-a.xacro↔cycle-b.xacro 相互 include——expand 以 IncludeCycle
 *   值面失败＋IO-FORMAT-XML-CYCLE 稳定码透传（io 护栏语义的对端码面），
 *   无展开产物（无草稿面、无对象写入——V-09"无草稿、无修订"）。
 */
TEST(MdlGoldenImport, GoldenXacroCyclePassthrough_V09_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19", "NFR-SEC-02"},
                  std::vector<std::string>{"AT-31"},
                  tk::DatasetRef{"mdl-urdf-import", "1.0.0"});

    const tk::GoldenDataset ds = loadImportDataset();
    const tk::JsonValue expectedRoot = tk::parseJson(readFile(
        ds.resolveExpected("expected/xacro-samples.json")));
    const tk::JsonValue* cycleExp = expectedRoot.find("cycle");
    ASSERT_NE(cycleExp, nullptr) << "期望文件缺 cycle 段";

    // 黄金循环样例装配：入口 cycle-a（include cycle-b）、cycle-b（include
    // cycle-a）——节点/字节齐备，失败只能来自环检测（非缺失/解析面）。
    const std::vector<std::uint8_t> cycleABytes
        = asBytes(readFile(ds.resolveInput("inputs/xacro/cycle-a.xacro")));
    const std::vector<std::uint8_t> cycleBBytes
        = asBytes(readFile(ds.resolveInput("inputs/xacro/cycle-b.xacro")));
    std::map<std::string, std::vector<std::uint8_t>> includes;
    includes["cycle-b.xacro"] = cycleBBytes;

    ResourceDependencyTree tree;
    tree.rootRel = "cycle-a.xacro";
    tree.nodes.push_back(nodeWithBytes("cycle-a.xacro", cycleABytes));
    tree.nodes.push_back(nodeWithBytes("cycle-b.xacro", cycleBBytes));
    tree.edges.push_back(includeEdge("cycle-a.xacro", "cycle-b.xacro"));
    tree.edges.push_back(includeEdge("cycle-b.xacro", "cycle-a.xacro"));
    std::sort(tree.nodes.begin(), tree.nodes.end(),
              [](const ResourceNode& a, const ResourceNode& b) {
                  return a.relPath < b.relPath;
              });
    std::sort(tree.edges.begin(), tree.edges.end(),
              [](const ResourceEdge& a, const ResourceEdge& b) {
                  if (a.fromRel != b.fromRel) { return a.fromRel < b.fromRel; }
                  if (a.toRel != b.toRel) { return a.toRel < b.toRel; }
                  return static_cast<int>(a.kind) < static_cast<int>(b.kind);
              });

    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const sdurws::ird::modeling::ExpandOutcome outcome
        = service.expand(makeXacroSource("cycle-a.xacro", cycleABytes, tree,
                                         std::move(includes)),
                         {}, diags);

    // 无产物（无草稿面——展开失败不进入映射，V-09"无草稿"）。
    ASSERT_NE(cycleExp->find("expandedBytesAbsent"), nullptr);
    EXPECT_EQ(outcome.expandedBytes.has_value(),
              !cycleExp->find("expandedBytesAbsent")->boolean)
        << "环失败必须无展开产物";
    // 值面错误码＋IO 稳定码透传（V-09 码面）。
    ASSERT_TRUE(outcome.error.has_value());
    IRD_EXPECT_IDENTICAL("xacro.cycle.errorCode",
                         std::string(sdurws::ird::modeling::xacroExpandErrorCodeToken(
                             outcome.error->code)),
                         jStr(*cycleExp, "expectedErrorCode"));
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::IncludeCycle);
    const std::size_t idx
        = findDiag(diags, jStr(*cycleExp, "expectedDiagnosticCode"));
    EXPECT_NE(idx, static_cast<std::size_t>(-1))
        << "IO-FORMAT-XML-CYCLE 稳定码透传（V-09）";
}
