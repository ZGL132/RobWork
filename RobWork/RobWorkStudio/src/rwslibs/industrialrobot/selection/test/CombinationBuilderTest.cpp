/**
 * @file   CombinationBuilderTest.cpp
 * @brief  候选组合构造用例组（SelCombinationBuilder）——兼容过滤、空候选
 *         集合、批预算切分、组合键确定性与调用方契约拒绝（卡 §14.5/§9.5）。
 *
 * 设计依据：
 *   - units/selection.md §14.5（IDeviceCombinationBuilder 契约——兼容性
 *     过滤/去重/批预算切分）、§9.5（组合身份与规模纪律——键为规范序列化
 *     摘要、重复组合去重）、§14.0（调用方错误 fail-fast）
 *   - 需求 SEL-05（组合兼容——兼容表零行语义）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/WP-19-T05.json acceptance 1（组合兼容）
 *
 * ★ 接口消费纪律：全部用例经 IDeviceCombinationBuilder 接口分派消费
 *   （接口契约测试另有分派钉扎）——不留只测实现类的盲区。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include "CombinationTestSupport.hpp"

#include <stdexcept>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;
using namespace sdurws::ird;

namespace {

/// 构造标准 perAxis（J1：M-A/M-B × G-10/G-20；J2：仅 M-A × G-10/G-20
/// ——黄金组合数＝3：兼容过滤掉 (M-B,G-20) 的 J1 对）。
std::vector<AxisCandidateList> goldenPerAxis(const GoldenAxes& axes)
{
    return {
        AxisCandidateList{axes.j1, {"M-A", "M-B"}, {"G-10", "G-20"}},
        AxisCandidateList{axes.j2, {"M-A"}, {"G-10", "G-20"}},
    };
}

/// 黄金批预算（每批 2——3 组合切 [2,1]）。
BatchBudget goldenBudget()
{
    return BatchBudget{2};
}

}  // namespace

// ---------------------------------------------------------------------
// 兼容过滤与展开序
// ---------------------------------------------------------------------

/// 只生成兼容表内登记的型号对（SEL-05 组合兼容；零行语义面），
/// 展开序＝轴清单主序×电机候选序×减速器候选序（确定性，NFR-COR-02）。
TEST(SelCombinationBuilder, BuildsCompatibleOnlyInDeterministicOrder)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——兼容过滤——无记录对不生成组合；展开序确定性
    const GoldenAxes axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const CombinationSet set = builder.build(goldenPerAxis(axes), goldenCompat(),
                                             goldenBudget(), goldenCatalog());
    // J1 三对（(A,G10)/(A,G20)/(B,G10)——(B,G20) 被过滤）×J2 两对
    // ((A,G10)/(A,G20))＝3×2＝6 个兼容组合。
    ASSERT_EQ(set.combinations.size(), std::size_t{6});
    // 首组合＝J1:(M-A,G-10)×J2:(M-A,G-10)（字典序首位）。
    EXPECT_EQ(set.combinations[0].axes[0].motorModelId, "M-A");
    EXPECT_EQ(set.combinations[0].axes[0].gearboxModelId, "G-10");
    EXPECT_EQ(set.combinations[0].axes[1].motorModelId, "M-A");
    EXPECT_EQ(set.combinations[0].axes[1].gearboxModelId, "G-10");
    // 全部组合的每轴指派都在兼容表内（对存在性——零行语义反向面）。
    for (const DeviceCombination& combo : set.combinations) {
        for (const AxisDeviceAssignment& axis : combo.axes) {
            bool found = false;
            for (const CompatibilityRecord& r : goldenCompat()) {
                if (r.motorId == axis.motorModelId && r.gearboxId == axis.gearboxModelId) {
                    found = true;
                }
            }
            EXPECT_TRUE(found) << "组合含兼容表外型号对（构造过滤失守）";
        }
    }
    EXPECT_EQ(set.duplicateDroppedCount, std::size_t{0});  // 防御性去重不触发。
}

/// 兼容表零行语义：空兼容表 ⇒ 零组合（无预声明兼容对——卡 §5.2/§9.3）。
TEST(SelCombinationBuilder, EmptyCompatibilityTableYieldsEmptySet)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——兼容表零行语义——无记录即不兼容
    const GoldenAxes axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const CombinationSet set = builder.build(goldenPerAxis(axes), {},
                                             goldenBudget(), goldenCatalog());
    EXPECT_TRUE(set.combinations.empty());
    EXPECT_EQ(set.batches.size(), std::size_t{1});  // 空集保留一个空批视图。
}

/// 某轴候选全被过滤（该轴无兼容对）⇒ 零组合（radix=0 短路）。
TEST(SelCombinationBuilder, AxisWithoutCompatiblePairYieldsEmptySet)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——单轴零兼容对——组合集为空
    const GoldenAxes axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    // J1 只给 (M-B,G-20)——兼容表无该对。
    const std::vector<AxisCandidateList> perAxis = {
        AxisCandidateList{axes.j1, {"M-B"}, {"G-20"}},
        AxisCandidateList{axes.j2, {"M-A"}, {"G-10"}},
    };
    const CombinationSet set = builder.build(perAxis, goldenCompat(),
                                             goldenBudget(), goldenCatalog());
    EXPECT_TRUE(set.combinations.empty());
}

// ---------------------------------------------------------------------
// 批预算切分（§9.5 分批——NFR-PERF-03）
// ---------------------------------------------------------------------

/// 批预算切分＝全局序切片（批间不重不漏、批内保留全局序）。
TEST(SelCombinationBuilder, BatchesRespectBudget)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——批预算切分——全局序切片
    const GoldenAxes axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const CombinationSet set = builder.build(goldenPerAxis(axes), goldenCompat(),
                                             BatchBudget{4}, goldenCatalog());
    ASSERT_EQ(set.combinations.size(), std::size_t{6});
    // 每批 4 ⇒ [4,2] 两批。
    ASSERT_EQ(set.batches.size(), std::size_t{2});
    EXPECT_EQ(set.batches[0].batchIndex, std::size_t{0});
    EXPECT_EQ(set.batches[0].combinations.size(), std::size_t{4});
    EXPECT_EQ(set.batches[1].batchIndex, std::size_t{1});
    EXPECT_EQ(set.batches[1].combinations.size(), std::size_t{2});
    // 批视图合并＝全量（不重不漏）。
    std::size_t total = 0;
    for (const CombinationBatch& b : set.batches) {
        total += b.combinations.size();
    }
    EXPECT_EQ(total, set.combinations.size());
}

// ---------------------------------------------------------------------
// 组合键（§9.5——规范序列化摘要）
// ---------------------------------------------------------------------

/// 组合键为 64 字符小写 hex；同输入恒同键；轴序/目录任一变更必得新键
/// （候选身份确定可复现——§9.5）。
TEST(SelCombinationBuilder, CombinationIdIsDeterministicDigest)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——组合键——规范序列化摘要确定性
    const GoldenAxes axes = makeGoldenAxes();
    const std::vector<AxisDeviceAssignment> axesOrder1 = {
        AxisDeviceAssignment{axes.j1, "M-A", "G-10"},
        AxisDeviceAssignment{axes.j2, "M-A", "G-10"},
    };
    const DeviceCombinationId id1 = makeDeviceCombinationId(goldenCatalog(), axesOrder1);
    // 同输入同键（NFR-COR-02）。
    EXPECT_EQ(id1, makeDeviceCombinationId(goldenCatalog(), axesOrder1));
    // SHA-256 hex 形态：64 字符、全小写 hex。
    ASSERT_EQ(id1.size(), std::size_t{64});
    for (const char ch : id1) {
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        EXPECT_TRUE(hex) << "组合键含非 hex 字符";
    }
    // 轴序变更 ⇒ 新键（键含轴序——§9.5 键定义）。
    const std::vector<AxisDeviceAssignment> axesOrder2 = {
        axesOrder1[1], axesOrder1[0],
    };
    EXPECT_NE(id1, makeDeviceCombinationId(goldenCatalog(), axesOrder2));
    // 目录版本变更 ⇒ 新键（键含 catalogVersion——候选参数来源版本）。
    CatalogIdentity otherCatalog = goldenCatalog();
    otherCatalog.version = "v6";
    EXPECT_NE(id1, makeDeviceCombinationId(otherCatalog, axesOrder1));
}

// ---------------------------------------------------------------------
// 调用方契约拒绝（fail-fast——卡 §14.5 @throws 注）
// ---------------------------------------------------------------------

/// 空轴清单拒绝（至少一轴）。
TEST(SelCombinationBuilder, RejectsEmptyPerAxis)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——空轴清单——调用方契约违约
    DeviceCombinationBuilder builder;
    EXPECT_THROW((void)builder.build({}, goldenCompat(), goldenBudget(), goldenCatalog()),
                 std::invalid_argument);
}

/// 批预算 0 拒绝（≥1——保留值不入参）。
TEST(SelCombinationBuilder, RejectsZeroBudget)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——批预算 0——保留值拒绝
    const GoldenAxes axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    EXPECT_THROW((void)builder.build(goldenPerAxis(axes), goldenCompat(),
                                     BatchBudget{0}, goldenCatalog()),
                 std::invalid_argument);
}

/// 同轴重复出现拒绝（组合轴表必须逐轴恰一指派——§9.3 行 2 构造侧防线）。
TEST(SelCombinationBuilder, RejectsDuplicateAxis)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——同轴重复——轴映射前提破坏
    const GoldenAxes axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const std::vector<AxisCandidateList> perAxis = {
        AxisCandidateList{axes.j1, {"M-A"}, {"G-10"}},
        AxisCandidateList{axes.j1, {"M-A"}, {"G-10"}},  // 重复轴。
    };
    EXPECT_THROW((void)builder.build(perAxis, goldenCompat(), goldenBudget(),
                                     goldenCatalog()),
                 std::invalid_argument);
}
