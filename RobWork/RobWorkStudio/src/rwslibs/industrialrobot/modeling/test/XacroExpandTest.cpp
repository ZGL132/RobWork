/**
 * @file   XacroExpandTest.cpp
 * @brief  Xacro 受控展开语义用例组（MdlXacroExpand）——契约
 *         tasks/foundation/WP-13-T06.json acceptance 逐条具名自证：
 *           ACC1 接口落位/受控子集（IXacroExpandService §9.4.9 签名、
 *                属性/宏/include 声明式替换、绝不执行任意代码——表达式与
 *                $(...) 显式拒绝、P-MDL-4 所有权口径：io 公共面零改动由
 *                提交面承载，本组只证 modeling 侧语义引擎面）
 *           ACC2 未定义宏/参数行列定位诊断（宏名/参数名＋源行列，MDL-19/
 *                AT-31）＋io 护栏码透传（include 环 IO-FORMAT-XML-CYCLE
 *                ——V-09；依赖缺失 IO-RES-MISSING；深度护栏 IO-SEC-BUDGET-
 *                INCLUDE）＋无草稿无产物＋参数列表展示数据
 *           ACC3 展开产物经 mapXacroExpanded 走 mapUrdf 同一映射（草稿
 *                逐字段相等——§6.5"相同映射与安全边界"）＋原始 .xacro
 *                digest 进草稿 externalRefs（Recorded 来源留痕）＋展开
 *                失败不产生草稿（无产物面）
 *           ACC5 确定性（同输入字节＋同 substitutions→同展开字节、同诊断
 *                顺序——NFR-COR-02）＋可重入无共享可变状态（卡 §3.4）
 *
 * 设计依据：units/modeling.md §6.5/§9.4.9/§9.5/§3.4、units/io.md §6.2/
 * §6.5/§10.5；需求 MDL-19、AT-31/V-09、NFR-COR-01/02/03。
 *
 * fixture 口径：全部 inline 字符串 Xacro（Xacro 黄金样例随 WP-13-T16
 * 收口——契约 note ④）；依赖树/快照/字节表按 io 值类型手工构造（模拟
 * io IResourceReader 产物——装配面归向导域侧，本单元不读文件）。带环树/
 * 缺失叶树按 io 契约不应从真实 io 产出——此处为透传面的防御性注入样例
 * （V-09 观测面：modeling 对带环输入给 IO-FORMAT-XML-CYCLE，无草稿）。
 */

#include <sdurws/ird/modeling/XacroExpand.hpp>

#include <sdurws/ird/core/DiagData.hpp>             // DiagnosticRecord 字段断言
#include <sdurws/ird/core/Digest.hpp>               // ContentDigester——展开产物真实摘要
#include <sdurws/ird/io/IoDiagnostics.hpp>          // io::errorCodeToken——IO-* 码面
#include <sdurws/ird/modeling/DiagCodes.hpp>        // MDL-IMPORT-XACRO-UNRESOLVED 常量
#include <sdurws/ird/modeling/Import.hpp>           // ModelImportMapper/ValidatedSource/ImportOptions
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::io::ResourceDependencyTree;
using sdurws::ird::io::ResourceEdge;
using sdurws::ird::io::ResourceEdgeKind;
using sdurws::ird::io::ResourceNode;
using sdurws::ird::io::ResourceSnapshot;
using sdurws::ird::modeling::ExpandOutcome;
using sdurws::ird::modeling::IXacroExpandService;
using sdurws::ird::modeling::ImportOptions;
using sdurws::ird::modeling::ImportOutcome;
using sdurws::ird::modeling::ModelImportMapper;
using sdurws::ird::modeling::ValidatedSource;
using sdurws::ird::modeling::ValidatedXacroSource;
using sdurws::ird::modeling::XacroExpandErrorCode;
using sdurws::ird::modeling::XacroExpandService;
using sdurws::ird::modeling::XacroParameterItem;
using sdurws::ird::modeling::XacroSubstitutionMap;
using sdurws::ird::modeling::kMdlImportXacroUnresolved;
using sdurws::ird::modeling::kXacroMaxExpansionDepth;

namespace {

// =====================================================================
// fixture 设施（io 产物模拟——ValidatedXacroSource 的装配面）
// =====================================================================

/// 字节串助手（string→vector<uint8_t>——io 产物字节形态）。
std::vector<std::uint8_t> asBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

/// 真实摘要（core ContentDigester——SA-12 唯一算法；fixture 与实现同一
/// 摘要路径，避免"占位摘要"弱化来源留痕断言）。
Digest256 digestOf(const std::string& text)
{
    ContentDigester digester;
    digester.update(text.data(), text.size());
    return digester.finalize();
}

/// 构造"存在"的依赖树节点（快照 digest 对字节真实计算——io §8.2 三元组
/// 形态；mtime/size 为提示性占位）。
ResourceNode existingNodeFor(const std::string& relPath, const std::string& content)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = true;
    ResourceSnapshot snapshot;
    snapshot.finalPath = std::string("Z:/fake/") + relPath;  // 追溯提示——不作身份
    snapshot.sizeBytes = content.size();
    snapshot.mtimeUtc = 0;
    snapshot.contentDigest = digestOf(content);
    node.snapshot = snapshot;
    return node;
}

/// 构造"缺失"的依赖树节点（io §6.5 缺失叶——exists=false）。
ResourceNode missingNode(const std::string& relPath)
{
    ResourceNode node;
    node.relPath = relPath;
    node.exists = false;
    return node;
}

/// include 边（from → to——io §6.5 边形态）。
ResourceEdge includeEdge(const std::string& fromRel, const std::string& toRel)
{
    ResourceEdge edge;
    edge.fromRel = fromRel;
    edge.toRel = toRel;
    edge.kind = ResourceEdgeKind::Include;
    return edge;
}

/**
 * @brief 组装 ValidatedXacroSource（入口字节＋真实摘要快照＋树＋字节表
 *        ——io 产物装配面；digest 为真实 SHA-256，来源留痕断言不被占位
 *        值弱化）。
 */
ValidatedXacroSource makeXacroSource(
    const std::string& entryRel, const std::string& entryText,
    std::vector<ResourceNode> nodes, std::vector<ResourceEdge> edges,
    std::map<std::string, std::vector<std::uint8_t>> includeBytes = {})
{
    ValidatedXacroSource source;
    source.entryBytes = asBytes(entryText);
    source.entrySnapshot.finalPath = std::string("Z:/fake/") + entryRel;
    source.entrySnapshot.sizeBytes = source.entryBytes.size();
    source.entrySnapshot.mtimeUtc = 0;
    source.entrySnapshot.contentDigest = digestOf(entryText);
    ResourceDependencyTree tree;
    tree.rootRel = entryRel;
    tree.nodes.push_back(existingNodeFor(entryRel, entryText));
    for (auto& node : nodes) { tree.nodes.push_back(std::move(node)); }
    tree.edges = std::move(edges);
    // io 稳定序（relPath 字典序——§9.6 确定性行）；边按 (from,to,kind)。
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
    source.dependencyTree = std::move(tree);
    source.includeBytes = std::move(includeBytes);
    return source;
}

/// 展开产物字符串视图（断言助手——expandedBytes→string）。
std::string asText(const ExpandOutcome& outcome)
{
    if (!outcome.expandedBytes.has_value()) { return {}; }
    return std::string(outcome.expandedBytes->begin(), outcome.expandedBytes->end());
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

// =====================================================================
// 基础样例（ACC1/ACC5 主体 fixture——属性＋宏＋调用＋${} 替换）
// =====================================================================

/**
 * 基础样例：全局属性 prefix＋宏 joint_def（参数 name/parent/child）。
 * 行列锚定：第 13 行第 3 列为宏调用元素起点（undefined-macro 用例按同
 * 布局注入失败面）。
 */
const char* kBasicXacro = R"(<?xml version="1.0"?>
<robot name="x" xmlns:xacro="http://www.ros.org/wiki/xacro">
  <xacro:property name="prefix" value="arm"/>
  <xacro:macro name="joint_def" params="name parent child">
    <joint name="${prefix}_${name}" type="revolute">
      <parent link="${parent}"/>
      <child link="${child}"/>
      <axis xyz="0 0 1"/>
    </joint>
  </xacro:macro>
  <link name="base"/>
  <link name="${prefix}_l1"/>
  <xacro:joint_def name="j1" parent="base" child="${prefix}_l1"/>
</robot>
)";

ValidatedXacroSource makeBasicSource()
{
    return makeXacroSource("robot.xacro", kBasicXacro, {}, {});
}

// =====================================================================
// 六轴样例（ACC3 主体 fixture——展开产物与手写 URDF 同草稿等价）
// =====================================================================

/**
 * 六轴样例：substitutions 覆盖属性 step（0.1→0.2——代入优先级面），宏
 * 生成 6 关节 7 连杆链。展开产物与等价手写 URDF（kGen6Urdf）应映射出
 * 逐字段相等的草稿——"展开产物进 URDF 同一映射与安全边界"（§6.5）的
 * 最强观测形态。
 */
const char* kGen6Xacro = R"(<?xml version="1.0"?>
<robot name="gen6">
  <xacro:property name="step" value="0.1"/>
  <xacro:macro name="link_def" params="name">
    <link name="${name}"/>
  </xacro:macro>
  <xacro:macro name="joint_def" params="name parent z">
    <joint name="${name}" type="revolute">
      <parent link="${parent}"/>
      <child link="${name}_link"/>
      <origin xyz="0 0 ${z}" rpy="0 0 0"/>
      <axis xyz="0 0 1"/>
      <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
    </joint>
  </xacro:macro>
  <xacro:link_def name="base"/>
  <xacro:joint_def name="j1" parent="base" z="${step}"/>
  <xacro:link_def name="j1_link"/>
  <xacro:joint_def name="j2" parent="j1_link" z="${step}"/>
  <xacro:link_def name="j2_link"/>
  <xacro:joint_def name="j3" parent="j2_link" z="${step}"/>
  <xacro:link_def name="j3_link"/>
  <xacro:joint_def name="j4" parent="j3_link" z="${step}"/>
  <xacro:link_def name="j4_link"/>
  <xacro:joint_def name="j5" parent="j4_link" z="${step}"/>
  <xacro:link_def name="j5_link"/>
  <xacro:joint_def name="j6" parent="j5_link" z="${step}"/>
  <xacro:link_def name="j6_link"/>
</robot>
)";

/// 等价手写 URDF（与 kGen6Xacro 展开产物逐字段同内容——元素序一致）。
const char* kGen6Urdf = R"(<?xml version="1.0"?>
<robot name="gen6">
  <link name="base"/>
  <joint name="j1" type="revolute">
    <parent link="base"/>
    <child link="j1_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <link name="j1_link"/>
  <joint name="j2" type="revolute">
    <parent link="j1_link"/>
    <child link="j2_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <link name="j2_link"/>
  <joint name="j3" type="revolute">
    <parent link="j2_link"/>
    <child link="j3_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <link name="j3_link"/>
  <joint name="j4" type="revolute">
    <parent link="j3_link"/>
    <child link="j4_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <link name="j4_link"/>
  <joint name="j5" type="revolute">
    <parent link="j4_link"/>
    <child link="j5_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <link name="j5_link"/>
  <joint name="j6" type="revolute">
    <parent link="j5_link"/>
    <child link="j6_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="50" velocity="1.0"/>
  </joint>
  <link name="j6_link"/>
</robot>
)";

/// 把展开产物装配回 mapXacroExpanded 的 ValidatedSource（调用方装配面
/// ——头文件契约注：展开字节＋产物摘要快照＋原依赖树）。
ValidatedSource assembleExpandedSource(const ExpandOutcome& outcome,
                                       const ResourceDependencyTree& tree)
{
    ValidatedSource source;
    source.bytes = *outcome.expandedBytes;
    source.entrySnapshot.finalPath = "Z:/fake/robot.xacro.expanded";
    source.entrySnapshot.sizeBytes = source.bytes.size();
    source.entrySnapshot.mtimeUtc = 0;
    source.entrySnapshot.contentDigest =
        digestOf(std::string(source.bytes.begin(), source.bytes.end()));
    source.dependencyTree = tree;
    return source;
}

}  // namespace

// =====================================================================
// ACC1：接口落位与受控子集（§9.4.9/绝不执行任意代码/P-MDL-4）
// =====================================================================

/**
 * 基础展开（acceptance 1——IXacroExpandService §9.4.9 签名落位；经接口
 * 多态调用）：属性替换（${prefix}→arm）、宏展开（调用点属性按签名绑定，
 * 宏体内 ${} 组合）、普通元素原样传递。产物为 URDF 形态（根 <robot>）。
 */
TEST(MdlXacroExpand, BasicPropertyMacroSubstitution_WP13T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    // 经接口引用调用——§9.4.9 签名的多态面（不只是具体类直调）。
    const IXacroExpandService& api = service;
    const ExpandOutcome outcome =
        api.expand(makeBasicSource(), {}, diags);

    ASSERT_TRUE(outcome.expandedBytes.has_value()) << "受控子集内展开应成功";
    EXPECT_TRUE(outcome.error == std::nullopt);
    EXPECT_TRUE(diags.empty()) << "成功展开无阻断诊断（诊断面只承载失败/护栏）";
    const std::string expanded = asText(outcome);
    // 属性替换与宏展开的可观察面（MDL-19：宏/参数/属性替换语义引擎）。
    EXPECT_NE(expanded.find("arm_j1"), std::string::npos)
        << "宏体内 ${prefix}_${name} 应替换为 arm_j1";
    EXPECT_NE(expanded.find("<link name=\"arm_l1\"/>"), std::string::npos)
        << "普通元素属性 ${prefix}_l1 应替换";
    EXPECT_NE(expanded.find("<parent link=\"base\"/>"), std::string::npos)
        << "宏参数 parent 应按调用点绑定";
    // 子集外构造不进产物（xacro:property/macro/include 是指令面）。
    EXPECT_EQ(expanded.find("xacro:property"), std::string::npos);
    EXPECT_EQ(expanded.find("xacro:macro"), std::string::npos);
    // 产物根为 URDF 形态（mapUrdf 可解析——§6.5 同一边界的形态基础）。
    EXPECT_NE(expanded.find("<robot"), std::string::npos);
    // 来源记录（§6.5）：digest＝入口快照摘要、substitutions 透传。
    EXPECT_TRUE(outcome.provenance.sourceDigest == digestOf(kBasicXacro));
    EXPECT_EQ(outcome.provenance.sourceAbsPath, "Z:/fake/robot.xacro");
}

/**
 * include 拼接（acceptance 1——受控子集①；P-MDL-4 口径：字节来自调用
 * 方装配的 io 产物，本单元零文件访问）：宏定义在被包含文件，入口 include
 * 后调用——拼接＋登记＋展开全链。
 */
TEST(MdlXacroExpand, IncludeSpliceRegistersMacroFromIncludedFile_WP13T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    const char* entry = R"(<?xml version="1.0"?>
<robot name="x">
  <xacro:include filename="macros.xacro"/>
  <xacro:joint_def name="j1" parent="base" child="l1"/>
</robot>
)";
    const char* macros = R"(<robot name="macros">
  <xacro:macro name="joint_def" params="name parent child">
    <joint name="${name}" type="revolute">
      <parent link="${parent}"/>
      <child link="${child}"/>
      <axis xyz="0 0 1"/>
    </joint>
  </xacro:macro>
</robot>
)";
    std::map<std::string, std::vector<std::uint8_t>> includes;
    includes["macros.xacro"] = asBytes(macros);
    const ValidatedXacroSource source =
        makeXacroSource("robot.xacro", entry,
                        {existingNodeFor("macros.xacro", macros)},
                        {includeEdge("robot.xacro", "macros.xacro")},
                        std::move(includes));

    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);
    ASSERT_TRUE(outcome.expandedBytes.has_value()) << "include 拼接应成功";
    const std::string expanded = asText(outcome);
    EXPECT_NE(expanded.find("<joint name=\"j1\" type=\"revolute\">"),
              std::string::npos)
        << "被包含文件的宏应拼接登记后可调用";
    EXPECT_EQ(expanded.find("xacro:include"), std::string::npos)
        << "include 指令不进产物（指令面）";
}

/**
 * 绝不执行任意代码（acceptance 1——§9.4.9 原文）：${} 内表达式与 $(...)
 * ROS 替换参数一律定位失败——不求值、不触环境/文件系统（NFR-COR-03
 * 不静默猜测）。
 */
TEST(MdlXacroExpand, NoArbitraryCode_ExpressionAndRosArgsRejected_WP13T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19", "NFR-COR-03"},
                  std::vector<std::string>{"AT-31"});

    const XacroExpandService service;
    // ${1+1}——表达式形态显式拒绝（不求值）。
    const ValidatedXacroSource exprSource = makeXacroSource(
        "robot.xacro",
        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
        "  <link name=\"${1 + 1}_l\"/>\n</robot>\n",
        {}, {});
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsExpr;
    const ExpandOutcome exprOutcome = service.expand(exprSource, {}, diagsExpr);
    ASSERT_FALSE(exprOutcome.expandedBytes.has_value())
        << "表达式不求值——无产物（绝不执行任意代码）";
    ASSERT_TRUE(exprOutcome.error.has_value());
    EXPECT_EQ(exprOutcome.error->code, XacroExpandErrorCode::UndefinedSymbol);
    const std::size_t exprDiag = findDiag(diagsExpr, kMdlImportXacroUnresolved);
    ASSERT_NE(exprDiag, static_cast<std::size_t>(-1));
    EXPECT_NE(diagsExpr[exprDiag].cause.find("unsupported-expression"),
              std::string::npos);
    EXPECT_NE(diagsExpr[exprDiag].cause.find("1 + 1"), std::string::npos)
        << "定位诊断携带被拒表达式原文（可观察——不静默）";

    // $(env ...)——ROS 替换参数显式拒绝（不触环境）。
    const ValidatedXacroSource rosSource = makeXacroSource(
        "robot.xacro",
        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
        "  <link name=\"$(env HOME)_l\"/>\n</robot>\n",
        {}, {});
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsRos;
    const ExpandOutcome rosOutcome = service.expand(rosSource, {}, diagsRos);
    ASSERT_FALSE(rosOutcome.expandedBytes.has_value());
    const std::size_t rosDiag = findDiag(diagsRos, kMdlImportXacroUnresolved);
    ASSERT_NE(rosDiag, static_cast<std::size_t>(-1));
    EXPECT_NE(diagsRos[rosDiag].cause.find("unsupported-construct"),
              std::string::npos);
}

// =====================================================================
// ACC2：未定义宏/参数行列定位＋护栏码透传＋参数列表（MDL-19/AT-31/V-09）
// =====================================================================

/**
 * 未定义宏行列定位（acceptance 2——"未定义宏→行列定位诊断（宏名＋源
 * 行列）"）：调用不存在宏→MDL-IMPORT-XACRO-UNRESOLVED（item-kind=
 * undefined-macro）＋宏名＋源行列（fixture 中调用元素在第 3 行第 3 列）
 * ；无展开产物（无草稿面）。
 */
TEST(MdlXacroExpand, UndefinedMacro_LocatableDiag_WP13T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    // 失败面与 kBasicXacro 同布局：第 3 行第 3 列为调用元素起点。
    const ValidatedXacroSource source = makeXacroSource(
        "robot.xacro",
        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
        "  <xacro:nonexistent a=\"1\"/>\n</robot>\n",
        {}, {});
    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);

    ASSERT_FALSE(outcome.expandedBytes.has_value())
        << "展开失败不产生草稿（MDL-19/§6.5）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::UndefinedSymbol);
    ASSERT_EQ(diags.size(), 1U);
    EXPECT_EQ(diags[0].code, kMdlImportXacroUnresolved);
    // 宏名定位面（cause 携带 item-kind/symbol 结构化键值——T05 同款形态）。
    EXPECT_NE(diags[0].cause.find("undefined-macro"), std::string::npos);
    EXPECT_NE(diags[0].cause.find("symbol=nonexistent"), std::string::npos);
    // 源行列定位面（context＝xacro-expand@相对键:行:列——行 3；列为元素
    // 名起点（pugixml offset_debug 语义——'<' 后首字符，字节列）。
    EXPECT_NE(diags[0].context.find("xacro-expand@robot.xacro:3:"),
              std::string::npos)
        << " actual=" << diags[0].context;
    EXPECT_NE(diags[0].context.find(":3:4"), std::string::npos)
        << " actual=" << diags[0].context;
}

/**
 * 未定义参数行列定位（acceptance 2——"未定义参数→行列定位诊断"）：
 * ${nope} 无任何作用域定义→MDL-IMPORT-XACRO-UNRESOLVED（item-kind=
 * undefined-param）＋参数名＋源行列；substitutions 可解同一引用（对照
 * 面——展开环境优先级语义生效）。
 */
TEST(MdlXacroExpand, UndefinedParam_LocatableDiag_SubstitutionResolves_WP13T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    const ValidatedXacroSource source = makeXacroSource(
        "robot.xacro",
        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
        "  <link name=\"${nope}_l\"/>\n</robot>\n",
        {}, {});
    const XacroExpandService service;

    // 反例面：无代入——失败＋定位。
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome failed = service.expand(source, {}, diags);
    ASSERT_FALSE(failed.expandedBytes.has_value());
    ASSERT_EQ(diags.size(), 1U);
    EXPECT_EQ(diags[0].code, kMdlImportXacroUnresolved);
    EXPECT_NE(diags[0].cause.find("undefined-param"), std::string::npos);
    EXPECT_NE(diags[0].cause.find("symbol=nope"), std::string::npos);
    // 源行列定位面（行 3；列为元素名起点——pugixml offset_debug 语义）。
    EXPECT_NE(diags[0].context.find("xacro-expand@robot.xacro:3:4"),
              std::string::npos)
        << " actual=" << diags[0].context;

    // 对照面：substitutions 提供 nope——同一输入成功（展开环境语义）。
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsOk;
    const ExpandOutcome ok =
        service.expand(source, {{"nope", "resolved"}}, diagsOk);
    ASSERT_TRUE(ok.expandedBytes.has_value());
    EXPECT_NE(asText(ok).find("resolved_l"), std::string::npos);
}

/**
 * include 环透传（acceptance 2/V-09——"io 护栏码透传：IO-FORMAT-XML-
 * CYCLE；无草稿、无修订、无对象写入"）：装配面注入带环树（真实 io 契约
 * 无环——防御性透传面），expand 给 IO-FORMAT-XML-CYCLE＋环路径，无产物。
 */
TEST(MdlXacroExpand, IncludeCycle_PassThroughCycleCodeNoProduct_WP13T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19", "NFR-SEC-02"},
                  std::vector<std::string>{"AT-31", "V-09"});

    const char* entry = "<?xml version=\"1.0\"?>\n<robot name=\"x\"/>\n";
    const char* other = "<robot name=\"y\"/>\n";
    // 带环树：robot.xacro→macros.xacro→robot.xacro（节点/字节齐备——失败
    // 只能来自环检测，非缺失/解析面）。
    std::map<std::string, std::vector<std::uint8_t>> includes;
    includes["macros.xacro"] = asBytes(other);
    const ValidatedXacroSource source =
        makeXacroSource("robot.xacro", entry,
                        {existingNodeFor("macros.xacro", other)},
                        {includeEdge("robot.xacro", "macros.xacro"),
                         includeEdge("macros.xacro", "robot.xacro")},
                        std::move(includes));

    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);

    ASSERT_FALSE(outcome.expandedBytes.has_value())
        << "环失败无产物——无草稿面（V-09）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::IncludeCycle);
    const std::size_t idx = findDiag(diags, "IO-FORMAT-XML-CYCLE");
    ASSERT_NE(idx, static_cast<std::size_t>(-1)) << "IO 码透传（V-09）";
    // 环路径清单可观察（io.md §6.2"列出环路径"）。
    EXPECT_NE(diags[idx].cause.find("robot.xacro -> macros.xacro -> robot.xacro"),
              std::string::npos);
}

/**
 * include 依赖缺失透传（acceptance 2——"io 护栏码透传：IO-RES-MISSING"
 * ；io.md §6.2 离线依赖硬失败）：树声明缺失叶＋字节表缺项→
 * IO-RES-MISSING＋缺失清单，无产物。
 */
TEST(MdlXacroExpand, MissingInclude_PassThroughResMissingWithList_WP13T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    const char* entry = "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
                        "  <xacro:include filename=\"macros.xacro\"/>\n"
                        "</robot>\n";
    // 缺失叶：树声明 exists=false 且字节表无项（io 缺失清单载体形态）。
    const ValidatedXacroSource source =
        makeXacroSource("robot.xacro", entry, {missingNode("macros.xacro")},
                        {includeEdge("robot.xacro", "macros.xacro")});

    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);

    ASSERT_FALSE(outcome.expandedBytes.has_value());
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::DependencyMissing);
    const std::size_t idx = findDiag(diags, "IO-RES-MISSING");
    ASSERT_NE(idx, static_cast<std::size_t>(-1)) << "IO 码透传（V-09 同族）";
    // 缺失清单可观察（io.md §6.2"缺文件→IO-RES-MISSING＋缺失清单"）。
    EXPECT_NE(diags[idx].cause.find("macros.xacro"), std::string::npos);
    EXPECT_NE(diags[idx].cause.find("missing-count=1"), std::string::npos);
}

/**
 * 展开递归深度护栏（acceptance 1/2——io.md §6.2"展开递归深度 ≤
 * IncludeDepth(16)"在语义引擎侧的执行面）：自递归宏→IO-SEC-BUDGET-
 * INCLUDE（io 注册码透传，actual/limit 三要素）＋无产物。
 */
TEST(MdlXacroExpand, MacroRecursionDepth_GuardBudgetInclude_WP13T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19", "NFR-SEC-02"},
                  std::vector<std::string>{"AT-31"});

    const ValidatedXacroSource source = makeXacroSource(
        "robot.xacro",
        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
        "  <xacro:macro name=\"r\">\n    <xacro:r/>\n  </xacro:macro>\n"
        "  <xacro:r/>\n</robot>\n",
        {}, {});
    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);

    ASSERT_FALSE(outcome.expandedBytes.has_value())
        << "护栏拦截——无产物（有界展开）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::ExpansionBudgetExceeded);
    const std::size_t idx = findDiag(diags, "IO-SEC-BUDGET-INCLUDE");
    ASSERT_NE(idx, static_cast<std::size_t>(-1)) << "深度护栏码透传";
    // 比较三要素（actual/limit——io 预算诊断口径；limit=IncludeDepth 16）。
    EXPECT_NE(diags[idx].cause.find("limit=" + std::to_string(kXacroMaxExpansionDepth)),
              std::string::npos);
}

/**
 * 参数列表展示数据（acceptance 2 尾段——"参数列表展示数据"）：清单＝
 * substitutions 在前（输入序）＋文档属性随后（定义序，携定义处行列）；
 * 生效值为代入后字面（替换优先级——substitutions 覆盖同名属性）。
 */
TEST(MdlXacroExpand, ParameterListDisplayData_WP13T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome =
        service.expand(makeBasicSource(), {{"prefix", "robot6"}}, diags);
    ASSERT_TRUE(outcome.expandedBytes.has_value());

    // 清单形态： substitution 条目在前（无定位），property 条目随后（定义
    // 处行列——kBasicXacro 属性定义在第 3 行）。
    ASSERT_GE(outcome.parameters.size(), 2U);
    EXPECT_EQ(outcome.parameters[0].name, "prefix");
    EXPECT_EQ(outcome.parameters[0].source, "substitution");
    EXPECT_EQ(outcome.parameters[0].span.line, 0U) << "代入条目无源定位";
    bool propertyRow = false;
    for (const XacroParameterItem& item : outcome.parameters) {
        if (item.source == "property") {
            propertyRow = true;
            EXPECT_EQ(item.name, "prefix");
            EXPECT_EQ(item.span.line, 3U) << "property 条目携定义处行列";
        }
    }
    EXPECT_TRUE(propertyRow) << "文档属性应入参数列表（MDL-19 参数列表）";
    // 代入优先级：${prefix} 应以 substitution 值展开（robot6_j1）。
    EXPECT_NE(asText(outcome).find("robot6_j1"), std::string::npos);
}

// =====================================================================
// ACC3：展开产物走 mapUrdf 同一映射与 externalRefs 来源留痕（§6.5）
// =====================================================================

/**
 * 同一映射与安全边界（acceptance 3——"expand 产物经 mapXacroExpanded 走
 * mapUrdf 同一校验"的最强观测：宏生成的六轴链与等价手写 URDF 映射出
 * 逐字段相等草稿——同一 §6.3/§6.4 口径，无 Xacro 旁路）；原始 .xacro 的
 * digest 经 provenance 进草稿 externalRefs（xacro-source Recorded——
 * MDL-19 来源可追溯）。
 */
TEST(MdlXacroExpand, ExpandedProduct_SameUrdfBoundaryAndProvenance_WP13T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19", "MDL-03"},
                  std::vector<std::string>{"AT-31", "AT-15"});

    // ① 受控展开（substitutions 覆盖 step——代入优先级随产物进入映射）。
    const ValidatedXacroSource xacroSource =
        makeXacroSource("robot.xacro", kGen6Xacro, {}, {});
    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> expandDiags;
    const ExpandOutcome expanded =
        service.expand(xacroSource, {{"step", "0.2"}}, expandDiags);
    ASSERT_TRUE(expanded.expandedBytes.has_value())
        << "六轴样例应在受控子集内展开成功";

    // ② 展开产物走 mapUrdf（同一校验的无来源面基线）＋mapXacroExpanded
    //（来源留痕面）——映射行为与手写 URDF 完全一致（无 Xacro 专用映射面）。
    const ModelImportMapper mapper;
    const ValidatedSource expandedSource =
        assembleExpandedSource(expanded, xacroSource.dependencyTree);
    std::vector<sdurws::ird::core::DiagnosticRecord> mapDiags;
    const ImportOutcome fromExpandedUrdf =
        mapper.mapUrdf(expandedSource, ImportOptions{}, mapDiags);
    ASSERT_TRUE(fromExpandedUrdf.draft.has_value());
    std::vector<sdurws::ird::core::DiagnosticRecord> xacroMapDiags;
    const ImportOutcome fromXacro = mapper.mapXacroExpanded(
        expandedSource, expanded.provenance, ImportOptions{}, xacroMapDiags);
    ASSERT_TRUE(fromXacro.draft.has_value()) << "展开产物应映射出草稿";

    // ③ 等价手写 URDF 走 mapUrdf——草稿逐字段相等（同一映射的判据；
    // mapXacroExpanded 在同一草稿上叠加 xacro-source 来源条目——来源面
    // 单独断言，比较面取 mapUrdf 对 mapUrdf）。
    std::vector<sdurws::ird::core::DiagnosticRecord> urdfDiags;
    ValidatedSource plainSource;
    plainSource.bytes = asBytes(kGen6Urdf);
    plainSource.entrySnapshot.finalPath = "Z:/fake/gen6.urdf";
    plainSource.entrySnapshot.sizeBytes = plainSource.bytes.size();
    plainSource.entrySnapshot.contentDigest = digestOf(kGen6Urdf);
    plainSource.dependencyTree = xacroSource.dependencyTree;
    const ImportOutcome fromPlainUrdf =
        mapper.mapUrdf(plainSource, ImportOptions{}, urdfDiags);
    ASSERT_TRUE(fromPlainUrdf.draft.has_value());
    EXPECT_TRUE(*fromExpandedUrdf.draft == *fromPlainUrdf.draft)
        << "展开产物与手写 URDF 应映射出逐字段相等草稿（§6.5 同一映射）";
    // 来源留痕叠加面：mapXacroExpanded 的草稿＝同一草稿＋恰一条 xacro-source
    // 来源条目——剥除后与 mapUrdf 基线逐字段相等（无其他字段漂移的直接证据）。
    sdurws::ird::modeling::RobotDesign stripped = *fromXacro.draft;
    stripped.resourceManifest.erase(
        std::remove_if(stripped.resourceManifest.begin(),
                       stripped.resourceManifest.end(),
                       [](const sdurws::ird::modeling::ResourceRef& r) {
                           return r.resourceId == "xacro-source";
                       }),
        stripped.resourceManifest.end());
    EXPECT_TRUE(stripped == *fromExpandedUrdf.draft)
        << "剥除来源条目后应与 mapUrdf 基线草稿逐字段相等";

    // ④ 来源留痕（externalRefs——原始 .xacro digest 进草稿 Recorded）。
    bool foundSource = false;
    for (const auto& ref : fromXacro.draft->resourceManifest) {
        if (ref.resourceId == "xacro-source") {
            foundSource = true;
            EXPECT_TRUE(ref.contentDigest == expanded.provenance.sourceDigest);
            EXPECT_TRUE(ref.contentDigest == digestOf(kGen6Xacro))
                << "digest 应为原始 .xacro 内容摘要（io 快照透传）";
            ASSERT_TRUE(ref.externalRecord.has_value());
            EXPECT_EQ(ref.externalRecord->absPath, "Z:/fake/robot.xacro");
        }
    }
    EXPECT_TRUE(foundSource) << "原始 .xacro 应以 Recorded 资源进 externalRefs";
    // ⑤ substitutions 随导入报告留痕（§6.5"展开环境随导入报告"）。
    int substitutionNotes = 0;
    for (const auto& mapped : fromXacro.report.mapped) {
        if (mapped.sourcePath.rfind("xacro-substitution/", 0) == 0) {
            ++substitutionNotes;
        }
    }
    EXPECT_EQ(substitutionNotes, 1) << "substitutions（step=0.2）逐条留痕";
}

/**
 * 展开失败不产生草稿（acceptance 3——"展开失败不产生草稿"）：失败面
 * expandedBytes 为空——装配面无产物可交，草稿无从产生（无修订、无对象
 * 写入由纯函数面保证——不落盘不产生任何持久化副作用）。
 */
TEST(MdlXacroExpand, ExpansionFailure_NoProductNoDraft_WP13T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{"AT-31"});

    const ValidatedXacroSource source = makeXacroSource(
        "robot.xacro",
        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
        "  <xacro:ghost/>\n</robot>\n",
        {}, {});
    const XacroExpandService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);
    EXPECT_FALSE(outcome.expandedBytes.has_value())
        << "失败面无展开产物——调用方无草稿可装配（§6.5）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::UndefinedSymbol);
}

// =====================================================================
// ACC5：确定性（NFR-COR-02）与可重入（卡 §3.4）
// =====================================================================

/**
 * 确定性与可重入（acceptance 5——"同输入字节＋同 substitutions→同展开
 * 字节、诊断顺序稳定；可重入无共享可变状态"）：两实例交错调用（含失败
 * 面触发后再成功——会话态不残留的观测），产物/参数清单/provenance/诊断
 * 序逐项相等。
 */
TEST(MdlXacroExpand, DeterministicAndReentrant_WP13T06_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    const XacroExpandService serviceA;
    const XacroExpandService serviceB;
    const XacroSubstitutionMap subs = {{"prefix", "robot6"}};

    // 交错调用：A 成功→B 失败面→B 成功——失败面不得污染后续调用。
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsA1;
    const ExpandOutcome a1 = serviceA.expand(makeBasicSource(), subs, diagsA1);
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsFail;
    const ExpandOutcome failure = serviceB.expand(
        makeXacroSource("robot.xacro",
                        "<?xml version=\"1.0\"?>\n<robot name=\"x\">\n"
                        "  <xacro:ghost/>\n</robot>\n",
                        {}, {}),
        subs, diagsFail);
    ASSERT_FALSE(failure.expandedBytes.has_value());
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsB1;
    const ExpandOutcome b1 = serviceB.expand(makeBasicSource(), subs, diagsB1);
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsA2;
    const ExpandOutcome a2 = serviceA.expand(makeBasicSource(), subs, diagsA2);

    // 同实例两次调用逐项相等（确定性）。
    EXPECT_EQ(asText(a1), asText(a2)) << "同输入→同展开字节（NFR-COR-02）";
    ASSERT_EQ(diagsA1.size(), diagsA2.size());
    for (std::size_t i = 0; i < diagsA1.size(); ++i) {
        EXPECT_TRUE(diagsA1[i] == diagsA2[i]) << "诊断 " << i << " 序/内容稳定";
    }
    // 跨实例相等（无共享可变状态——卡 §3.4 纯函数服务约定）。
    EXPECT_EQ(asText(a1), asText(b1));
    EXPECT_TRUE(a1.parameters == b1.parameters);
    // XacroProvenance 无 operator==（T05 值聚合）——逐字段相等。
    EXPECT_TRUE(a1.provenance.sourceDigest == b1.provenance.sourceDigest);
    EXPECT_EQ(a1.provenance.sourceAbsPath, b1.provenance.sourceAbsPath);
    EXPECT_EQ(a1.provenance.substitutions, b1.provenance.substitutions);
    ASSERT_EQ(diagsA1.size(), diagsB1.size());
    for (std::size_t i = 0; i < diagsA1.size(); ++i) {
        EXPECT_TRUE(diagsA1[i] == diagsB1[i]);
    }
}

/**
 * 输入契约违约面（acceptance 1 前置——ValidatedXacroSource 契约）：空
 * 字节/树缺入口/零摘要→SourceInconsistent 无产物（不抛异常——值面错误
 * 语义，AGENTS 错误语义纪律）。
 */
TEST(MdlXacroExpand, InputContractViolation_SourceInconsistentNoThrow_WP13T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-19"},
                  std::vector<std::string>{});

    const XacroExpandService service;
    // 树缺入口节点（rootRel 不在节点集）。
    ValidatedXacroSource source =
        makeXacroSource("robot.xacro", kBasicXacro, {}, {});
    source.dependencyTree.nodes.clear();
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const ExpandOutcome outcome = service.expand(source, {}, diags);
    EXPECT_FALSE(outcome.expandedBytes.has_value());
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, XacroExpandErrorCode::SourceInconsistent);
}
