/**
 * @file   ComparisonTest.cpp
 * @brief  方案比较视图编排的模型测试（WP-22-T11——units/workflow.md §8.1/
 *         §8.2 编排面的直调半区；WF-VER-224 方案比较与差异定位的编排核
 *         半区＋WF-VER-225 比较基准不一致拒绝；OPT-07 不可算"—"；P-OPT-8
 *         警告不虚构）。
 *
 * 设计依据：
 *   - units/workflow.md §8.1（方案选择 2~4；八项指标差异高亮——取数为
 *     只读投影、不可算显示"—"；Model Diff 呈现分组＋点击定位锚；§8.2
 *     数据流——基准一致性检查先行，不一致拒绝＋原因提示）、§10.2
 *     （ISchemeComparisonController Draft 签名——buildComparison 逐字）、
 *     §10.3（调用方错误 fail-fast——WorkflowError；buildComparison const
 *     并发安全）、§11.2（WF-VER-224＝契约测试〔本文件为其编排核半区——
 *     端口脚本化桩直调；真实 project 联合面在契约测试〕、WF-VER-225＝
 *     模型测试）
 *   - REQUIREMENTS.md §18 UX-13 原文（2~4 方案八项指标差异高亮；Model
 *     Diff 按结构/参数（DH、轴线、限位）与物性分组呈现、点击定位对象）、
 *     §15.0 OPT-07（八项全部展示——不可算项显示"—"）、EVI-02/RPT-04
 *     （方案比较只能使用一致的需求与工况基准）、MDL-08（数据实体归
 *     modeling——本单元零 diff 实现）、P-OPT-8（范围外给警告不虚构差异）
 *   - 任务契约 tasks/foundation/WP-22-T11.json acceptance 1/2/3
 *
 * 测试形态（§11.0——模型测试＝直调计算库纯函数面）：两取数端口为脚本化
 * 桩（L5 装配桥接的测试等价物——记录调用、返回预置快照），编排核直调；
 * 经 ISchemeComparisonController& / ISchemeMetricPort& / IComparisonDiffPort&
 * 接口引用消费的用例钉扎接口路径（WP-20-T03 首轮漏检教训——公共接口的
 * 每个公共方法至少一条经接口消费的用例）。真实 project store 联合面在
 * 契约测试 ComparisonContractTest.cpp。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Comparison.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（fail-fast 断言）

#include <cmath>     // std::numeric_limits——非有限值渲染断言素材
#include <limits>
#include <optional>  // std::optional/std::nullopt——不可算格断言素材
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::ComparisonDiffBlock;
using workflow::ComparisonMetricRow;
using workflow::ComparisonOutcome;
using workflow::ComparisonRejection;
using workflow::ComparisonViewData;
using workflow::IComparisonDiffPort;
using workflow::ISchemeComparisonController;
using workflow::ISchemeMetricPort;
using workflow::SchemeDiffEntry;
using workflow::SchemeDiffFacts;
using workflow::SchemeMetricFacts;
using workflow::WorkflowError;
using core::BranchId;
using core::ContentIdentity;
using core::ObjectId;

// =====================================================================
// 夹具辅助（确定性身份构造——黄金数据自持，不依赖生成器）
// =====================================================================

/// @brief 由 32 位十六进制词尾构造方案分支身份（brn- 规范文本）。
BranchId makeBranch(const std::string& hex32)
{
    return BranchId::fromCanonical("brn-" + hex32);
}

/// @brief 由 64 位十六进制词尾构造内容身份（cid- 规范文本——evidence
///        ComparisonBaseline 的基准身份字段构造素材）。
ContentIdentity makeCid(const std::string& hex64)
{
    return ContentIdentity::fromCanonical("cid-" + hex64);
}

/// @brief 由 32 位十六进制词尾构造对象身份（obj- 规范文本——diff 条目
///        点击定位锚构造素材）。
ObjectId makeObject(const std::string& hex32)
{
    return ObjectId::fromCanonical("obj-" + hex32);
}

/// @brief 构造一份基准身份（project/branch 相同、inputBaselineId 可控——
///        WF-VER-225 的基准注入面）。
evidence::ComparisonBaseline makeBaseline(const BranchId& branch,
                                          const std::string& cidHex)
{
    evidence::ComparisonBaseline b;
    b.inputBaselineId = makeCid(cidHex);
    b.requiredCaseSetId = makeCid(std::string(60, 'a') + "0001");
    b.sampleSetIds = {makeCid(std::string(60, 'b') + "0001")};
    (void)branch;  // branch 字段不参与"维度差异"注入（本夹具只动基准身份）
    return b;
}

/// @brief 构造单方案指标快照（指标列三件套＋值/stale 逐格可控）。
SchemeMetricFacts makeFacts(const BranchId& branch, const std::string& label,
                            bool available,
                            const std::vector<SchemeMetricFacts::Metric>& metrics,
                            const std::string& baselineCid)
{
    SchemeMetricFacts f;
    f.branch = branch;
    f.available = available;
    f.label = label;
    f.metrics = metrics;
    f.baseline = makeBaseline(branch, baselineCid);
    return f;
}

/// @brief 构造一个指标格（列头三件套＋值）。
SchemeMetricFacts::Metric metric(const std::string& key,
                                 const std::optional<double>& v)
{
    SchemeMetricFacts::Metric m;
    m.metricKey = key;
    m.labelKey = "metric." + key + ".label";   // 列头键形（值归 ui 文案表）
    m.unitToken = "1";                          // 无量纲占位（词形不参与断言处可忽略）
    m.value = v;
    m.stale = false;
    return m;
}

// =====================================================================
// 脚本化端口桩（L5 装配桥接的测试等价物——记录调用、返回预置快照）
// =====================================================================

/// @brief 指标端口桩：返回预置快照表（与请求序对齐），记录每次调用。
struct ScriptedMetricPort final : ISchemeMetricPort {
    /// collect() 的脚本化返回（按调用序消费；用例预置）。
    mutable std::vector<std::vector<SchemeMetricFacts>> scripted;
    /// 已收到的请求清单（断言"取数与编排入参一致"——只读投影纪律）。
    mutable std::vector<std::vector<BranchId>> requests;

    std::vector<SchemeMetricFacts> collect(
        const std::vector<BranchId>& schemes) const override
    {
        requests.push_back(schemes);
        // 按调用序弹出脚本（mutable 计数推进——调用序与用例一一对应，
        // 越界＝用例缺陷，直接失败）。
        served++;
        if (served > scripted.size()) {
            ADD_FAILURE() << "指标端口被超脚本调用（" << served << " 次）";
            return {};
        }
        return scripted[served - 1];
    }
    mutable std::size_t served = 0;
};

/// @brief diff 端口桩：按 (baseline, candidate) 查表的脚本化返回。
struct ScriptedDiffPort final : IComparisonDiffPort {
    /// 查表键＝"baselineHex|candidateHex"（用例预置；未命中＝用例缺陷）。
    mutable std::vector<std::pair<std::pair<BranchId, BranchId>, SchemeDiffFacts>> table;
    /// 已收到的请求序（断言"基线＝首方案、逐对成块"的调用序）。
    mutable std::vector<std::pair<BranchId, BranchId>> requests;

    SchemeDiffFacts diff(const BranchId& baseline,
                         const BranchId& candidate) const override
    {
        requests.push_back({baseline, candidate});
        for (const auto& entry : table) {
            if (entry.first.first == baseline && entry.first.second == candidate) {
                return entry.second;
            }
        }
        ADD_FAILURE() << "diff 端口未命中脚本键（用例预置缺失）";
        return {};
    }
};

/// @brief 组装两端口就绪的控制器（经接口引用注入——装配形态同 L5；
///        返回具体类型〔抽象接口不可按值〕，接口路径钉扎由专用用例
///        经 ISchemeComparisonController& 消费承载）。
workflow::SchemeComparisonController makeController(ScriptedMetricPort& m,
                                                    ScriptedDiffPort& d)
{
    return workflow::SchemeComparisonController(m, d);
}

// 常用黄金身份（32/64 hex 常串——确定性、可读、可跨用例复用）。
const BranchId kBrA = makeBranch(std::string(30, '0') + "0a");
const BranchId kBrB = makeBranch(std::string(30, '0') + "0b");
const BranchId kBrC = makeBranch(std::string(30, '0') + "0c");
const BranchId kBrD = makeBranch(std::string(30, '0') + "0d");

}  // namespace

// =====================================================================
// 值渲染（renderMetricValueText——NFR-COR-02 确定性黄金串）
// =====================================================================

/**
 * @brief 值渲染黄金串（NFR-COR-02——17 位有效数字 classic locale）。
 *
 * 依据：%.17g 语义（setprecision(17) defaultfloat）＝去尾零的 17 位有效
 * 数字；1/3 与 0.1 的双精度 17 位展开为黄金事实（0.33333333333333331/
 * 0.10000000000000001）。同 modeling 值摘要渲染口径——跨平台字节稳定。
 */
TEST(WfComparison, RenderValueGoldenTexts_NFR_COR_02)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{});

    // 整值/短值：%g 去尾零（"0.5" 不是 "0.50000000000000000"）。
    EXPECT_EQ(workflow::renderMetricValueText(0.5), "0.5");
    EXPECT_EQ(workflow::renderMetricValueText(2.0), "2");
    EXPECT_EQ(workflow::renderMetricValueText(0.0), "0");
    // 双精度黄金展开（17 位有效数字——往返安全位数）。
    EXPECT_EQ(workflow::renderMetricValueText(1.0 / 3.0),
              "0.33333333333333331");
    EXPECT_EQ(workflow::renderMetricValueText(0.1), "0.10000000000000001");

    // 确定性：同输入双跑同串（NFR-COR-02 同型）。
    EXPECT_EQ(workflow::renderMetricValueText(1.0 / 3.0),
              workflow::renderMetricValueText(1.0 / 3.0));
}

/**
 * @brief 非有限值归"—"占位（OPT-07 不可算面同口径——绝不渲染 nan/inf）。
 */
TEST(WfComparison, RenderValueNonFiniteIsUnavailablePlaceholder_OPT_07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-07"}, std::vector<std::string>{});

    // NaN 与 ±Inf 都不是可呈现的工程量——归不可算占位（kMetricUnavailable
    // Display＝EM DASH），不伪造数值也不显示 "nan"/"inf" 字样。
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(workflow::renderMetricValueText(nan),
              workflow::kMetricUnavailableDisplay);
    EXPECT_EQ(workflow::renderMetricValueText(inf),
              workflow::kMetricUnavailableDisplay);
    EXPECT_EQ(workflow::renderMetricValueText(-inf),
              workflow::kMetricUnavailableDisplay);
    EXPECT_EQ(workflow::kMetricUnavailableDisplay, "\xE2\x80\x94");  // U+2014
}

// =====================================================================
// 维度键词表（comparisonBaselineDimensionKey——唯一映射点黄金值）
// =====================================================================

/**
 * @brief 差异维度 → 明细键黄金串（UX-03 拒绝明细行的键半区；evidence
 *        五维度逐一覆盖——消费侧同步义务：evidence 新增维度时本表漏登记
 *        应由键空串防御性暴露）。
 */
TEST(WfComparison, BaselineDimensionKeysGoldenTable_UX_03)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02", "RPT-04"},
                  std::vector<std::string>{"WF-VER-225"});

    using Dim = evidence::BaselineDifferenceDimension;
    EXPECT_EQ(workflow::comparisonBaselineDimensionKey(Dim::Project),
              "comparison.baseline-mismatch.dim.project");
    EXPECT_EQ(workflow::comparisonBaselineDimensionKey(Dim::Branch),
              "comparison.baseline-mismatch.dim.branch");
    EXPECT_EQ(workflow::comparisonBaselineDimensionKey(Dim::InputBaseline),
              "comparison.baseline-mismatch.dim.input-baseline");
    EXPECT_EQ(workflow::comparisonBaselineDimensionKey(Dim::RequiredCaseSet),
              "comparison.baseline-mismatch.dim.required-case-set");
    EXPECT_EQ(workflow::comparisonBaselineDimensionKey(Dim::SampleSets),
              "comparison.baseline-mismatch.dim.sample-sets");

    // 词表外防御：越界枚举值（如经reinterpret 注入的非法值）返回空串
    // 不伪造键——用 static_cast 构造词表外值（七值封闭枚举的防御面）。
    const auto bogus = static_cast<Dim>(200);
    EXPECT_EQ(workflow::comparisonBaselineDimensionKey(bogus), "");
}

// =====================================================================
// 指标行组装（assembleMetricRows——差异高亮＋"—"＋同构校验）
// =====================================================================

/**
 * @brief 高亮行组装主链（UX-13/OPT-07——八项全部展示：可算行高亮判定、
 *        不可算行"—"、行序＝端口列序不重排）。
 *
 * 三指标夹具：m-diff（三方案值互异→高亮）、m-same（三方案同值→不高亮）、
 * m-unavail（两方案不可算＋一方案 0→不高亮——"'—'语义 null≠0"）。
 */
TEST(WfComparison, MetricRowsHighlightAndPlaceholder_UX_13_OPT_07)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "OPT-07"},
                  std::vector<std::string>{"WF-VER-224"});

    const std::string baseCid = std::string(60, '1') + "0001";
    std::vector<SchemeMetricFacts::Metric> colA = {
        metric("m-diff", 1.0), metric("m-same", 2.0), metric("m-unavail", 0.0)};
    std::vector<SchemeMetricFacts::Metric> colB = {
        metric("m-diff", 3.0), metric("m-same", 2.0),
        metric("m-unavail", std::nullopt)};
    std::vector<SchemeMetricFacts::Metric> colC = {
        metric("m-diff", 5.0), metric("m-same", 2.0),
        metric("m-unavail", std::nullopt)};

    std::vector<SchemeMetricFacts> facts = {
        makeFacts(kBrA, "方案甲", true, colA, baseCid),
        makeFacts(kBrB, "方案乙", true, colB, baseCid),
        makeFacts(kBrC, "方案丙", true, colC, baseCid)};

    const std::vector<ComparisonMetricRow> rows = workflow::assembleMetricRows(facts);

    // 行数＝端口指标列数（3 行全部产出——OPT-07"全部展示"，不可算行
    // 不缺席）。
    ASSERT_EQ(rows.size(), 3u);

    // 第一行：三方案值互异→高亮位 true；单元格逐位对位（displayText 与
    // value 一致——可算值渲染黄金串）。
    EXPECT_EQ(rows[0].metricKey, "m-diff");
    EXPECT_TRUE(rows[0].differs);
    ASSERT_EQ(rows[0].cells.size(), 3u);
    EXPECT_TRUE(rows[0].cells[0].value.has_value());
    EXPECT_EQ(rows[0].cells[0].displayText, "1");
    EXPECT_EQ(rows[0].cells[1].displayText, "3");
    EXPECT_EQ(rows[0].cells[2].displayText, "5");

    // 第二行：三方案同值→零容差精确相等→不高亮。
    EXPECT_EQ(rows[1].metricKey, "m-same");
    EXPECT_FALSE(rows[1].differs);

    // 第三行：仅一方案可算（0.0）、其余不可算（nullopt）→不高亮——
    // 不可算不参与比较、绝不当作 0（"'—'语义 null≠0"）；不可算格的
    // displayText＝"—"且 value 无值（presence 严格一致）。
    EXPECT_EQ(rows[2].metricKey, "m-unavail");
    EXPECT_FALSE(rows[2].differs);
    EXPECT_TRUE(rows[2].cells[0].value.has_value());
    EXPECT_EQ(rows[2].cells[0].displayText, "0");
    EXPECT_FALSE(rows[2].cells[1].value.has_value());
    EXPECT_EQ(rows[2].cells[1].displayText,
              workflow::kMetricUnavailableDisplay);
    EXPECT_FALSE(rows[2].cells[2].value.has_value());
    EXPECT_EQ(rows[2].cells[2].displayText,
              workflow::kMetricUnavailableDisplay);

    // 列头三件套透传（对端权威零加工——指标词表/单位归对端 §8.1 红线）。
    EXPECT_EQ(rows[0].labelKey, "metric.m-diff.label");
    EXPECT_EQ(rows[0].unitToken, "1");
}

/**
 * @brief 单可算值不高亮＋双可算异值高亮（高亮判定的边界——没有可比
 *        对偶的行不亮灯，两可算即判定）。
 */
TEST(WfComparison, MetricRowsHighlightNeedsTwoComputable_UX_13)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    const std::string baseCid = std::string(60, '1') + "0002";
    // 两方案：一可算一不可算→不高亮；再补第三方案异值→高亮（对偶出现）。
    std::vector<SchemeMetricFacts::Metric> colA = {metric("m-x", 1.5)};
    std::vector<SchemeMetricFacts::Metric> colB = {metric("m-x", std::nullopt)};
    std::vector<SchemeMetricFacts::Metric> colC = {metric("m-x", 1.5)};

    // 两方案（A 可算、B 不可算）：高亮 false。
    std::vector<ComparisonMetricRow> rows2 = workflow::assembleMetricRows(
        {makeFacts(kBrA, "甲", true, colA, baseCid),
         makeFacts(kBrB, "乙", true, colB, baseCid)});
    ASSERT_EQ(rows2.size(), 1u);
    EXPECT_FALSE(rows2[0].differs);

    // 三方案（C 与 A 同值）：可算值 {1.5, 1.5} 全等→仍不高亮（精确相等）。
    std::vector<ComparisonMetricRow> rows3 = workflow::assembleMetricRows(
        {makeFacts(kBrA, "甲", true, colA, baseCid),
         makeFacts(kBrB, "乙", true, colB, baseCid),
         makeFacts(kBrC, "丙", true, colC, baseCid)});
    ASSERT_EQ(rows3.size(), 1u);
    EXPECT_FALSE(rows3[0].differs);

    // 三方案（C 改异值 9.75）：可算值不全相等→高亮 true。
    colC[0].value = 9.75;
    std::vector<ComparisonMetricRow> rows4 = workflow::assembleMetricRows(
        {makeFacts(kBrA, "甲", true, colA, baseCid),
         makeFacts(kBrB, "乙", true, colB, baseCid),
         makeFacts(kBrC, "丙", true, colC, baseCid)});
    ASSERT_EQ(rows4.size(), 1u);
    EXPECT_TRUE(rows4[0].differs);
}

/**
 * @brief 组装前置 fail-fast 三面（空集/不可用快照/指标列不同构——调用方
 *        与对端契约违约，§10.3 错误语义行）。
 */
TEST(WfComparison, MetricRowsFailFastContract)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    const std::string baseCid = std::string(60, '1') + "0003";

    // 空集（调用方组装违约——编排器已保证 ≥2，直调同受 @pre 约束）。
    EXPECT_THROW(workflow::assembleMetricRows({}), WorkflowError);

    // 不可用快照（@pre"方案分支存在且可读"违约——fail-fast 不产缺列表）。
    std::vector<SchemeMetricFacts::Metric> col = {metric("m", 1.0)};
    EXPECT_THROW(
        workflow::assembleMetricRows(
            {makeFacts(kBrA, "甲", true, col, baseCid),
             makeFacts(kBrB, "乙", false, col, baseCid)}),
        WorkflowError);

    // 列长度不同构（对端契约违约——高亮矩阵会错位）。
    std::vector<SchemeMetricFacts::Metric> colLong = {metric("m", 1.0),
                                                      metric("m2", 2.0)};
    EXPECT_THROW(
        workflow::assembleMetricRows(
            {makeFacts(kBrA, "甲", true, col, baseCid),
             makeFacts(kBrB, "乙", true, colLong, baseCid)}),
        WorkflowError);

    // 列键错位（同长度不同键——同构性校验的键面）。
    std::vector<SchemeMetricFacts::Metric> colShift = {metric("m-other", 1.0)};
    EXPECT_THROW(
        workflow::assembleMetricRows(
            {makeFacts(kBrA, "甲", true, col, baseCid),
             makeFacts(kBrB, "乙", true, colShift, baseCid)}),
        WorkflowError);
}

// =====================================================================
// diff 分组块组装（assembleDiffBlock——三组分拣/降级/P-OPT-8 透传）
// =====================================================================

/**
 * @brief 分组分拣主链（UX-13/MDL-08——结构/参数（DH、轴线、限位）/物性
 *        三组呈现分块；组内序＝对端序直通不重排；条目全字段透传零增殖）。
 */
TEST(WfComparison, DiffBlockGrouping_UX_13_MDL_08)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "MDL-08"},
                  std::vector<std::string>{"WF-VER-224"});

    // 三组条目各一（含点击定位锚 ObjectId——V15-02）＋组间交错产出序
    // （分拣后仍按对端序在各自组内保持）。
    SchemeDiffEntry s1;
    s1.group = "structure";
    s1.kind = "added";
    s1.objectId = makeObject(std::string(30, '0') + "01");
    s1.subjectPath = "joints[2]";
    s1.field = "joint";
    s1.valueChanged = true;
    s1.baselineText = "";
    s1.candidateText = "j3";

    SchemeDiffEntry p1;
    p1.group = "parameters";
    p1.kind = "modified";
    p1.objectId = makeObject(std::string(30, '0') + "02");
    p1.subjectPath = "joints[2].axis";
    p1.field = "axis";
    p1.valueChanged = true;
    p1.baselineText = "[0, 0, 1]";
    p1.candidateText = "[1, 0, 0]";

    SchemeDiffEntry pr1;
    pr1.group = "properties";
    pr1.kind = "modified";
    pr1.objectId = makeObject(std::string(30, '0') + "03");
    pr1.subjectPath = "links[1].body";
    pr1.field = "mass";
    pr1.valueChanged = true;
    pr1.baselineText = "5";
    pr1.candidateText = "6.5";

    SchemeDiffFacts facts;
    facts.available = true;
    facts.entries = {s1, p1, pr1};

    const ComparisonDiffBlock block = workflow::assembleDiffBlock(
        facts, kBrA, kBrB, "基线方案", "候选方案");

    // 块头透传（基线＝首方案——编排决定）。
    EXPECT_TRUE(block.baseline == kBrA);
    EXPECT_TRUE(block.candidate == kBrB);
    EXPECT_EQ(block.baselineLabel, "基线方案");
    EXPECT_EQ(block.candidateLabel, "候选方案");

    // 三组分拣到位（组内序＝对端序直通）。
    ASSERT_EQ(block.structure.size(), 1u);
    EXPECT_TRUE(block.structure[0] == s1);
    ASSERT_EQ(block.parameters.size(), 1u);
    EXPECT_TRUE(block.parameters[0] == p1);
    ASSERT_EQ(block.properties.size(), 1u);
    EXPECT_TRUE(block.properties[0] == pr1);

    // 点击定位锚可达（V15-02——条目携带 ObjectId 强类型，直接对接
    // ui::IUiNameResolver::resolveObjectId；根对象条目为全零 id）。
    EXPECT_TRUE(block.parameters[0].objectId.isValid());

    // 无警告通道时 warnings 为空。
    EXPECT_TRUE(block.warnings.empty());
}

/**
 * @brief 未知分组 token fail-fast（对端契约违约——静默丢条目会让用户看到
 *        "无差异"假象；§10.3 错误语义行）。
 */
TEST(WfComparison, DiffBlockUnknownGroupTokenFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{});

    SchemeDiffEntry bad;
    bad.group = "mystery";  // modeling 三值词表外——对端违约
    bad.kind = "modified";
    bad.field = "x";
    SchemeDiffFacts facts;
    facts.available = true;
    facts.entries = {bad};

    EXPECT_THROW(
        workflow::assembleDiffBlock(facts, kBrA, kBrB, "甲", "乙"),
        WorkflowError);
}

/**
 * @brief diff 通道不可用＝诚实降级（空三组＋降级警告——不虚构条目、
 *        不炸宿主；ITitleFactPort"环境失败折叠为安全缺省"同款纪律）。
 */
TEST(WfComparison, DiffBlockUnavailableDegradedNotFabricated)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    SchemeDiffFacts facts;   // available=false（缺省）——entries 必空
    const ComparisonDiffBlock block = workflow::assembleDiffBlock(
        facts, kBrA, kBrB, "甲", "乙");

    // 三组全空（零虚构）＋恰一条降级警告（kComparisonWarningDiffSource
    // Unavailable 词形黄金串）。
    EXPECT_TRUE(block.structure.empty());
    EXPECT_TRUE(block.parameters.empty());
    EXPECT_TRUE(block.properties.empty());
    ASSERT_EQ(block.warnings.size(), 1u);
    EXPECT_EQ(block.warnings[0],
              workflow::kComparisonWarningDiffSourceUnavailable);
    EXPECT_EQ(workflow::kComparisonWarningDiffSourceUnavailable,
              "diff-preview-unavailable");
}

/**
 * @brief P-OPT-8 警告透传（范围外给警告不虚构差异——警告逐条透传，
 *        条目集仍逐条来自端口产出、零补造）。
 */
TEST(WfComparison, DiffBlockPopt8WarningPassThroughNoFabrication)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    // 端口产出：一条参数组条目＋P-OPT-8 传动比范围外警告（警告的产生归
    // 端口实现方——L5 桥接据 modeling diff 实际范围判定；本编排只透传）。
    SchemeDiffEntry p1;
    p1.group = "parameters";
    p1.kind = "modified";
    p1.field = "bounds";
    p1.valueChanged = true;
    SchemeDiffFacts facts;
    facts.available = true;
    facts.entries = {p1};
    facts.warnings = {workflow::kComparisonWarningRatioOutOfScope};

    const ComparisonDiffBlock block = workflow::assembleDiffBlock(
        facts, kBrA, kBrB, "甲", "乙");

    // 警告逐条透传（零加工零吞——词形黄金串与 optimization 侧同串）。
    ASSERT_EQ(block.warnings.size(), 1u);
    EXPECT_EQ(block.warnings[0], "ratio-diff-out-of-scope");
    EXPECT_EQ(workflow::kComparisonWarningRatioOutOfScope,
              "ratio-diff-out-of-scope");

    // 条目集零补造：三组大小＝端口产出的分拣结果（1 条→参数组 1 条、
    // 其余组空）——绝不因警告补造传动比条目（P-OPT-8 红线）。
    ASSERT_EQ(block.parameters.size(), 1u);
    EXPECT_TRUE(block.parameters[0] == p1);
    EXPECT_TRUE(block.structure.empty());
    EXPECT_TRUE(block.properties.empty());
}

// =====================================================================
// 编排核（SchemeComparisonController——前置 fail-fast/拒绝/接受/确定性）
// =====================================================================

/// @brief 填充指标端口的默认脚本（n 方案同构三指标列——可算/同值/不可算）。
void primeDefaultMetrics(ScriptedMetricPort& port,
                         const std::vector<BranchId>& branches,
                         const std::vector<std::string>& labels,
                         const std::string& baseCid)
{
    std::vector<SchemeMetricFacts::Metric> col = {
        metric("m-envelope", 1.0), metric("m-mass", 42.0),
        metric("m-cycle", std::nullopt)};
    std::vector<SchemeMetricFacts> facts;
    facts.reserve(branches.size());
    for (std::size_t i = 0; i < branches.size(); ++i) {
        std::vector<SchemeMetricFacts::Metric> c = col;
        if (i == 1) {
            c[0].value = 7.25;  // 第二方案差异值——首行高亮素材
        }
        facts.push_back(makeFacts(branches[i], labels[i], true, c, baseCid));
    }
    port.scripted.push_back(std::move(facts));
}

/**
 * @brief 编排前置 fail-fast（UX-13"2~4 个方案"——1/5 方案越界与重复分支
 *        均拒绝且不触端口——先校验后取数的次序钉扎）。
 */
TEST(WfComparison, ControllerFailFastSchemeCountAndDuplicates)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    ScriptedMetricPort metricPort;
    ScriptedDiffPort diffPort;
    const auto controller =
        makeController(metricPort, diffPort);

    // 1 方案（<2）→ WorkflowError。
    EXPECT_THROW(controller.buildComparison({kBrA}), WorkflowError);
    // 5 方案（>4）→ WorkflowError。
    EXPECT_THROW(controller.buildComparison({kBrA, kBrB, kBrC, kBrD,
                                             makeBranch(std::string(30, '0') + "0e")}),
                 WorkflowError);
    // 重复分支（自比无比较语义）→ WorkflowError。
    EXPECT_THROW(controller.buildComparison({kBrA, kBrA}), WorkflowError);

    // 前置校验先于取数：端口零调用（越界请求根本不该到投影面）。
    EXPECT_EQ(metricPort.requests.size(), 0u);
    EXPECT_EQ(diffPort.requests.size(), 0u);
}

/**
 * @brief 分支不可用 fail-fast（@pre"方案分支存在且可读"——端口折叠
 *        available=false，编排核转 fail-fast）。
 */
TEST(WfComparison, ControllerFailFastUnavailableScheme)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    ScriptedMetricPort metricPort;
    ScriptedDiffPort diffPort;
    const auto controller =
        makeController(metricPort, diffPort);

    const std::string baseCid = std::string(60, '1') + "0010";
    std::vector<SchemeMetricFacts::Metric> col = {metric("m", 1.0)};
    metricPort.scripted.push_back(
        {makeFacts(kBrA, "甲", true, col, baseCid),
         makeFacts(kBrB, "乙", false, col, baseCid)});  // 乙不可用

    EXPECT_THROW(controller.buildComparison({kBrA, kBrB}), WorkflowError);
}

/**
 * @brief WF-VER-225 模型半区：基准不一致拒绝（EVI-02/RPT-04——不产出
 *        混基准比较，拒绝附原因键＋逐维度明细键；检查经 evidence 公共
 *        契约，本编排零基准判定实现）。
 */
TEST(WfComparison, ControllerRejectsInconsistentBaseline_EVI_02_RPT_04)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02", "RPT-04"},
                  std::vector<std::string>{"WF-VER-225"});

    ScriptedMetricPort metricPort;
    ScriptedDiffPort diffPort;
    const auto controller =
        makeController(metricPort, diffPort);

    // 两方案同构指标列，但 inputBaselineId 不同（模型/需求/工况集基准
    // 差异——EVI-02 拒绝面；diff 端口不应被触达——拒绝先于 diff 取数）。
    const std::string cidA = std::string(60, '1') + "0020";
    const std::string cidB = std::string(60, '2') + "0020";
    std::vector<SchemeMetricFacts::Metric> col = {metric("m", 1.0)};
    metricPort.scripted.push_back(
        {makeFacts(kBrA, "甲", true, col, cidA),
         makeFacts(kBrB, "乙", true, col, cidB)});

    const ComparisonOutcome outcome = controller.buildComparison({kBrA, kBrB});

    // 拒绝态：accepted=false、view 无值（互斥契约——不产出混基准比较数据）。
    ASSERT_FALSE(outcome.accepted);
    EXPECT_FALSE(outcome.view.has_value());
    ASSERT_TRUE(outcome.rejection.has_value());

    // 原因三字段（UX-03）：主原因键＋建议动作键恒填充；差异维度＝evidence
    // 检查产出透传（InputBaseline 命中）；逐维度明细键一一对应。
    EXPECT_EQ(outcome.rejection->reasonKey,
              workflow::kComparisonBaselineMismatchKey);
    EXPECT_EQ(outcome.rejection->actionKey,
              workflow::kComparisonBaselineMismatchActionKey);
    ASSERT_EQ(outcome.rejection->dimensions.size(), 1u);
    EXPECT_EQ(outcome.rejection->dimensions[0],
              evidence::BaselineDifferenceDimension::InputBaseline);
    ASSERT_EQ(outcome.rejection->detailKeys.size(), 1u);
    EXPECT_EQ(outcome.rejection->detailKeys[0],
              "comparison.baseline-mismatch.dim.input-baseline");

    // 拒绝先于 diff 取数：diff 端口零调用（不产出任何 diff 块）。
    EXPECT_EQ(diffPort.requests.size(), 0u);
}

/**
 * @brief WF-VER-224 编排核半区：接受态主链（2 方案一致基准——指标高亮
 *        行＋diff 块齐备；基线＝首方案；取数调用序＝编排数据流）。
 */
TEST(WfComparison, ControllerAcceptsConsistentBaseline_WF_VER_224_Core)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"},
                  std::vector<std::string>{"WF-VER-224"});

    ScriptedMetricPort metricPort;
    ScriptedDiffPort diffPort;
    const auto controller =
        makeController(metricPort, diffPort);

    const std::string baseCid = std::string(60, '1') + "0030";
    primeDefaultMetrics(metricPort, {kBrA, kBrB}, {"甲", "乙"}, baseCid);

    // diff 脚本：首对（甲→乙）产出一条结构组条目＋P-OPT-8 警告。
    SchemeDiffEntry s1;
    s1.group = "structure";
    s1.kind = "modified";
    s1.objectId = makeObject(std::string(30, '0') + "10");
    s1.field = "displayName";
    s1.valueChanged = true;
    SchemeDiffFacts diffFacts;
    diffFacts.available = true;
    diffFacts.entries = {s1};
    diffFacts.warnings = {workflow::kComparisonWarningRatioOutOfScope};
    diffPort.table.emplace_back(std::make_pair(kBrA, kBrB), diffFacts);

    const ComparisonOutcome outcome = controller.buildComparison({kBrA, kBrB});

    // 接受态互斥：view 有值、rejection 无值。
    ASSERT_TRUE(outcome.accepted);
    ASSERT_TRUE(outcome.view.has_value());
    EXPECT_FALSE(outcome.rejection.has_value());

    const ComparisonViewData& view = *outcome.view;

    // 指标行：3 行全部展示（OPT-07）；首行高亮（甲 1.0 vs 乙 7.25）；
    // 不可算行"—"。
    ASSERT_EQ(view.metricRows.size(), 3u);
    EXPECT_TRUE(view.metricRows[0].differs);
    EXPECT_EQ(view.metricRows[0].cells[0].displayText, "1");
    EXPECT_EQ(view.metricRows[0].cells[1].displayText, "7.25");
    EXPECT_EQ(view.metricRows[2].cells[0].displayText,
              workflow::kMetricUnavailableDisplay);

    // 方案列头（label 透传）。
    ASSERT_EQ(view.schemeLabels.size(), 2u);
    EXPECT_EQ(view.schemeLabels[0], "甲");
    EXPECT_EQ(view.schemeLabels[1], "乙");

    // diff 块：恰 1 块（n-1）、基线＝首方案、条目与警告透传到位。
    ASSERT_EQ(view.diffs.size(), 1u);
    EXPECT_TRUE(view.diffs[0].baseline == kBrA);
    EXPECT_TRUE(view.diffs[0].candidate == kBrB);
    ASSERT_EQ(view.diffs[0].structure.size(), 1u);
    EXPECT_TRUE(view.diffs[0].structure[0] == s1);
    ASSERT_EQ(view.diffs[0].warnings.size(), 1u);
    EXPECT_EQ(view.diffs[0].warnings[0], "ratio-diff-out-of-scope");

    // 取数调用序＝编排数据流（指标一次取数；diff 基线＝首方案）。
    ASSERT_EQ(metricPort.requests.size(), 1u);
    ASSERT_EQ(diffPort.requests.size(), 1u);
    EXPECT_TRUE(diffPort.requests[0].first == kBrA);
    EXPECT_TRUE(diffPort.requests[0].second == kBrB);
}

/**
 * @brief 空差集＝空块仍产出（n-1 块恒入列——呈现面渲染"无差异"而不是
 *        缺块；4 方案→3 块的方案数上界覆盖）。
 */
TEST(WfComparison, ControllerProducesAllDiffBlocksEvenEmpty_UX_13)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    ScriptedMetricPort metricPort;
    ScriptedDiffPort diffPort;
    const auto controller =
        makeController(metricPort, diffPort);

    const std::string baseCid = std::string(60, '1') + "0040";
    const std::vector<BranchId> branches = {kBrA, kBrB, kBrC, kBrD};
    primeDefaultMetrics(metricPort, branches, {"甲", "乙", "丙", "丁"}, baseCid);

    // 三对 diff 全部空产出（可用通道＋空差集——合法报告）。
    for (std::size_t i = 1; i < branches.size(); ++i) {
        SchemeDiffFacts f;
        f.available = true;   // 空条目表＝无差异
        diffPort.table.emplace_back(
            std::make_pair(branches.front(), branches[i]), f);
    }

    const ComparisonOutcome outcome = controller.buildComparison(branches);
    ASSERT_TRUE(outcome.accepted);
    ASSERT_TRUE(outcome.view.has_value());
    // 4 方案→3 块（全空条目——空块在列，不缺块）。
    ASSERT_EQ(outcome.view->diffs.size(), 3u);
    for (const ComparisonDiffBlock& block : outcome.view->diffs) {
        EXPECT_TRUE(block.structure.empty());
        EXPECT_TRUE(block.parameters.empty());
        EXPECT_TRUE(block.properties.empty());
        EXPECT_TRUE(block.warnings.empty());
    }
}

/**
 * @brief 确定性重放（NFR-COR-02 同型——同夹具双跑输出逐字段相等）＋
 *        过期标注透传（stale 位与当前性投影零重算搬运）。
 */
TEST(WfComparison, ControllerDeterministicReplayAndStalePassThrough_NFR_COR_02)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "NFR-COR-02"},
                  std::vector<std::string>{"WF-VER-224"});

    const std::string baseCid = std::string(60, '1') + "0050";

    // 夹具构造器（两次独立装配同脚本——重放的"同输入"面）。
    auto runOnce = [&baseCid]() {
        ScriptedMetricPort metricPort;
        ScriptedDiffPort diffPort;
        const auto controller =
            makeController(metricPort, diffPort);

        // 第二方案带过期标注（指标级 stale＋方案级 Superseded 投影——
        // §8.1"evidence 当前性投影标注过期项"的透传面）。
        std::vector<SchemeMetricFacts::Metric> colA = {metric("m", 1.0)};
        std::vector<SchemeMetricFacts::Metric> colB = {metric("m", 2.0)};
        colB[0].stale = true;
        SchemeMetricFacts fB =
            makeFacts(kBrB, "乙", true, colB, baseCid);
        ui::CurrentnessProjection stale;
        stale.status = ui::CurrentnessProjection::Status::Superseded;
        stale.unevaluableCause = std::nullopt;
        stale.reasons.push_back({"kin.batch-ik", "input-changed", "输入已变化"});
        fB.currentness = stale;
        metricPort.scripted.push_back(
            {makeFacts(kBrA, "甲", true, colA, baseCid), fB});

        SchemeDiffFacts d;
        d.available = true;
        diffPort.table.emplace_back(std::make_pair(kBrA, kBrB), d);

        return controller.buildComparison({kBrA, kBrB});
    };

    const ComparisonOutcome first = runOnce();
    const ComparisonOutcome second = runOnce();

    // 同输入双跑：输出逐字段相等（operator== 全量比较——含当前性投影
    // 逐条原因的透传保真）。
    ASSERT_TRUE(first.accepted);
    ASSERT_TRUE(second.accepted);
    EXPECT_TRUE(first.view == second.view);

    // 过期标注透传（第 2 方案格 stale=true；当前性投影 Superseded＋原因
    // 原样搬运——零重算零加工）。
    ASSERT_TRUE(first.view.has_value());
    ASSERT_EQ(first.view->metricRows.size(), 1u);
    ASSERT_EQ(first.view->metricRows[0].cells.size(), 2u);
    EXPECT_FALSE(first.view->metricRows[0].cells[0].stale);
    EXPECT_TRUE(first.view->metricRows[0].cells[1].stale);
}

// =====================================================================
// 接口消费路径钉扎（WP-20-T03 教训——公共接口经接口引用虚派发消费）
// =====================================================================

/**
 * @brief 接口路径钉扎（ISchemeComparisonController& 虚派发→buildComparison；
 *        端口注入也经 ISchemeMetricPort&/IComparisonDiffPort& 接口引用——
 *        公共接口的每个公共方法至少一条经接口消费的用例）。
 */
TEST(WfComparison, InterfacePathPinning_VirtualDispatch)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    ScriptedMetricPort metricPort;
    ScriptedDiffPort diffPort;

    // 全接口引用装配（L5 形态——产品面只见接口不见具体类型）。
    const ISchemeMetricPort& m = metricPort;
    const IComparisonDiffPort& d = diffPort;
    const workflow::SchemeComparisonController concrete(m, d);
    const ISchemeComparisonController& controller = concrete;

    const std::string baseCid = std::string(60, '1') + "0060";
    primeDefaultMetrics(metricPort, {kBrA, kBrB}, {"甲", "乙"}, baseCid);
    SchemeDiffFacts df;
    df.available = true;
    diffPort.table.emplace_back(std::make_pair(kBrA, kBrB), df);

    // 经接口引用虚派发调用（钉扎 buildComparison 公共方法可达且行为一致）。
    const ComparisonOutcome outcome = controller.buildComparison({kBrA, kBrB});
    ASSERT_TRUE(outcome.accepted);
    ASSERT_TRUE(outcome.view.has_value());
    EXPECT_EQ(outcome.view->metricRows.size(), 3u);
    EXPECT_EQ(outcome.view->diffs.size(), 1u);

    // 经端口接口引用的虚派发同样可达（端口方法的消费路径钉扎）。
    // 注意：桩脚本按调用序消费——上面 buildComparison 已消费第一份脚本，
    // 直调前再补一份同脚本（同输入直调＝与编排内调用同事实）。
    primeDefaultMetrics(metricPort, {kBrA, kBrB}, {"甲", "乙"}, baseCid);
    const std::vector<SchemeMetricFacts> facts = m.collect({kBrA, kBrB});
    EXPECT_EQ(facts.size(), 2u);
    const SchemeDiffFacts got = d.diff(kBrA, kBrB);
    EXPECT_TRUE(got.available);
}
