/**
 * @file   CombinationCheckCodecTest.cpp
 * @brief  组合校核 canonical 编解码用例组（SelCombinationCheckCodec）——
 *         六面往返等值、非有限拒绝（NFR-COR-03）、magic/残余违约、物化
 *         锚派生与候选传动参数构造黄金值（c＝1/n 单点换算）。
 *
 * 设计依据：
 *   - units/selection.md §9.2（候选组合集载荷——候选传动参数构造）、
 *     §14.0（NFR-COR-03 非有限拒绝）
 *   - 需求 NFR-COR-02/03（确定性编码/非有限不进入编码链）
 *   - 任务契约 tasks/foundation/WP-19-T05.json acceptance 1（候选传动
 *     参数构造——SEL-05 允许的三类动作之一）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include "CombinationTestSupport.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;
using namespace sdurws::ird;

namespace {

/// 本地摘要工具（SHA-256——core ContentDigester 唯一算法面；测试侧自持
/// 快捷封装，与产品实现无共享符号）。
core::ContentIdentity digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity digest;
    digest.bytes = digester.finalize();
    return digest;
}

/// 黄金组合集（两轴三组合——与核心测试同源）。
GoldenCombos makeCombos()
{
    GoldenCombos g;
    g.axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const std::vector<AxisCandidateList> perAxis = {
        AxisCandidateList{g.axes.j1, {"M-A", "M-B"}, {"G-10", "G-20"}},
        AxisCandidateList{g.axes.j2, {"M-A"}, {"G-10", "G-20"}},
    };
    // 组合级目录身份与快照身份同源（构造入口核对组合 catalog＝快照身份
    // ——传动参数构造的来源版本一致性前提；本夹具回填身份后 build）。
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    snapshot.contentIdentity = computePackageContentIdentity(snapshot);
    snapshot.manifest.identity.contentIdentity = snapshot.contentIdentity;
    const CombinationSet set = builder.build(perAxis, goldenCompat(),
                                             BatchBudget{64},
                                             snapshot.manifest.identity);
    g.combos = set.combinations;
    return g;
}

}  // namespace

// ---------------------------------------------------------------------
// 往返等值（NFR-COR-02——同值对象必得同字节、同字节必得等值对象）
// ---------------------------------------------------------------------

/// 目录锁定引用往返。
TEST(SelCombinationCheckCodec, CatalogLockPayloadRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——目录锁定引用——canonical 往返
    CatalogLockPayload payload;
    payload.identity = goldenCatalog();
    payload.lockObjectId = core::ObjectId::generate();
    const CatalogLockPayload decoded =
        decodeCatalogLockPayload(encodeCatalogLockPayload(payload));
    EXPECT_EQ(decoded, payload);
}

/// 筛选条件往返（含 optional 全量形态）。
TEST(SelCombinationCheckCodec, ScreeningCriteriaRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——筛选条件——canonical 往返
    ScreeningCriteria c = goldenCriteria();
    c.requiredDutyClass = "S1";
    c.requiredVoltage = 220.0;      // V
    c.voltageRelativeTolerance = 0.05;
    c.ambientTemp = 40.0;           // 档位值（v1 冻结口径）
    c.maxBacklash = 0.002;          // rad
    c.requiredLife = 10000.0;       // 循环数
    c.minEfficiency = 0.85;
    c.ratioRange = RatioRange{0.005, 0.2};
    const ScreeningCriteria decoded = decodeScreeningCriteria(encodeScreeningCriteria(c));
    EXPECT_EQ(decoded, c);
    // 空条件形态（全不适用）同往返。
    const ScreeningCriteria emptyDecoded =
        decodeScreeningCriteria(encodeScreeningCriteria(goldenCriteria()));
    EXPECT_EQ(emptyDecoded, goldenCriteria());
}

/// 轴工作点事实包往返（关节侧＋需求侧全字段）。
TEST(SelCombinationCheckCodec, AxisFactsBundleRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——轴事实包——canonical 往返
    const GoldenAxes axes = makeGoldenAxes();
    AxisFactsBundle bundle;
    AxisWorkpointFacts f = makeJointFacts(axes.j1, "case-A", true);
    f.requiredHoldingTorque = 12.0;  // N·m（保持需求）
    f.externalLoad = ExternalLoadFacts{1200.0, 500.0, 0.08};  // N/N/m
    f.mountRequirement = JointMountRequirement{"flangeA", "shaftB", "flangeA"};
    bundle.push_back(f);
    bundle.push_back(makeJointFacts(axes.j2, "case-A", false));
    const AxisFactsBundle decoded = decodeAxisFactsBundle(encodeAxisFactsBundle(bundle));
    ASSERT_EQ(decoded.size(), bundle.size());
    EXPECT_EQ(decoded[0], bundle[0]);
    EXPECT_EQ(decoded[1], bundle[1]);
}

/// 映射批事实包往返（含组合表＋逐轴事实全量字段）。
TEST(SelCombinationCheckCodec, MappingBatchFactsRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——映射批事实——canonical 往返
    const GoldenCombos golden = makeCombos();
    MappingBatchFacts batch = goldenMappingBatch(golden.combos, golden.axes);
    batch.upstreamSliceId = digestOf(std::vector<std::uint8_t>{1, 2, 3});
    batch.completeness = CompletenessKind::Partial;
    batch.missingItems = {"efficiency[j=0]"};
    batch.diagnosticCodes = {"DT-ROTOR-MISSING"};
    const MappingBatchFacts decoded = decodeMappingBatchFacts(encodeMappingBatchFacts(batch));
    EXPECT_EQ(decoded, batch);
}

/// 组合校核总产出往返（记录＋覆盖矩阵＋身份块）。
TEST(SelCombinationCheckCodec, SelectionCheckResultRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——校核总产出——canonical 往返
    const GoldenCombos golden = makeCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    snapshot.contentIdentity = computePackageContentIdentity(snapshot);
    snapshot.manifest.identity.contentIdentity = snapshot.contentIdentity;
    CombinationCheckCoreInput in;
    in.snapshot = &snapshot;
    in.axisFacts = {makeJointFacts(golden.axes.j1, "case-A", true)};
    in.mappingBatch = goldenMappingBatch({golden.combos[0]}, golden.axes);
    in.criteria = goldenCriteria();
    in.inputSliceId = digestOf(std::vector<std::uint8_t>{9});
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    SelectionCheckResult result;
    for (const CombinationCheckOutcome& o : outcomes) {
        result.records.push_back(o.record);
        result.coverage.insert(result.coverage.end(), o.coverage.begin(),
                               o.coverage.end());
    }
    result.catalog = snapshot.manifest.identity;
    result.mappingSliceId = in.mappingBatch.upstreamSliceId;
    result.inputSliceId = in.inputSliceId;
    const SelectionCheckResult decoded =
        decodeSelectionCheckResult(encodeSelectionCheckResult(result));
    EXPECT_EQ(decoded, result);
}

/// 候选组合集载荷往返。
TEST(SelCombinationCheckCodec, DtComboSetRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——组合集载荷——canonical 往返
    std::vector<CombinationDriveInput> inputs;
    CombinationDriveInput in;
    in.combinationId = "abc";
    in.axes.push_back(AxisDriveInput{core::ObjectId::generate(), 0.1, 0.9, 0.9,
                                     0.01, 1.5});
    in.axes.push_back(AxisDriveInput{core::ObjectId::generate(), 0.02, 0.8, 0.8,
                                     0.02, std::nullopt});
    inputs.push_back(in);
    const std::vector<CombinationDriveInput> decoded =
        decodeDtComboSetPayload(encodeDtComboSetPayload(inputs));
    EXPECT_EQ(decoded, inputs);
}

// ---------------------------------------------------------------------
// 编码拒绝（NFR-COR-03——非有限不进入身份与持久化链）
// ---------------------------------------------------------------------

/// 非有限筛选条件拒绝编码。
TEST(SelCombinationCheckCodec, NonfiniteCriteriaRejectedOnEncode)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{"AT-08"});  // R1——非有限值——编码入口拒绝
    ScreeningCriteria c = goldenCriteria();
    c.minEfficiency = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)encodeScreeningCriteria(c), std::invalid_argument);
}

/// 非有限传动输入拒绝编码。
TEST(SelCombinationCheckCodec, NonfiniteDriveInputRejectedOnEncode)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{"AT-08"});  // R1——传动输入非有限——编码入口拒绝
    std::vector<CombinationDriveInput> inputs(1);
    inputs[0].combinationId = "x";
    inputs[0].axes.push_back(AxisDriveInput{core::ObjectId::generate(),
                                            std::numeric_limits<double>::quiet_NaN(),
                                            0.9, 0.9, 0.01, std::nullopt});
    EXPECT_THROW((void)encodeDtComboSetPayload(inputs), std::invalid_argument);
}

// ---------------------------------------------------------------------
// 解码违约（组装协议破坏——fail-fast）
// ---------------------------------------------------------------------

/// 坏 magic 拒绝解码。
TEST(SelCombinationCheckCodec, BadMagicRejectedOnDecode)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{"AT-08"});  // R1——坏 magic——解码违约
    const std::vector<std::uint8_t> garbage(32, 0xAB);
    EXPECT_THROW((void)decodeCatalogLockPayload(garbage), std::invalid_argument);
    EXPECT_THROW((void)decodeScreeningCriteria(garbage), std::invalid_argument);
    EXPECT_THROW((void)decodeAxisFactsBundle(garbage), std::invalid_argument);
    EXPECT_THROW((void)decodeMappingBatchFacts(garbage), std::invalid_argument);
    EXPECT_THROW((void)decodeSelectionCheckResult(garbage), std::invalid_argument);
    EXPECT_THROW((void)decodeDtComboSetPayload(garbage), std::invalid_argument);
}

/// 截断/残余字节拒绝解码（长度严格校验）。
TEST(SelCombinationCheckCodec, TruncatedAndResidualRejectedOnDecode)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{"AT-08"});  // R1——截断/残余——解码长度校验
    const CatalogLockPayload payload{goldenCatalog(), core::ObjectId::generate()};
    std::vector<std::uint8_t> bytes = encodeCatalogLockPayload(payload);
    // 截断（去尾 8 字节）。
    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 8);
    EXPECT_THROW((void)decodeCatalogLockPayload(truncated), std::invalid_argument);
    // 残余（追加杂散字节）。
    bytes.push_back(0x00);
    EXPECT_THROW((void)decodeCatalogLockPayload(bytes), std::invalid_argument);
}

// ---------------------------------------------------------------------
// 物化锚派生（selection 域内单点——确定性前 16 字节）
// ---------------------------------------------------------------------

/// 锚＝上游切片身份前 16 字节；全零身份拒绝（调用方契约违约）。
TEST(SelCombinationCheckCodec, UpstreamAnchorDerivation)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——物化锚——前 16 字节确定性派生
    core::ContentIdentity sliceId = digestOf(std::vector<std::uint8_t>{7, 7, 7});
    const core::ObjectId anchor = selUpstreamAnchor(sliceId);
    for (std::size_t i = 0; i < anchor.bytes.size(); ++i) {
        EXPECT_EQ(anchor.bytes[i], sliceId.bytes[i]) << "锚第 " << i << " 字节不符";
    }
    // 同身份同锚（确定性）。
    EXPECT_EQ(selUpstreamAnchor(sliceId), anchor);
    // 全零身份拒绝。
    EXPECT_THROW((void)selUpstreamAnchor(core::ContentIdentity{}), std::invalid_argument);
}

// ---------------------------------------------------------------------
// 候选传动参数构造（§9.2——c＝1/n 单点换算的黄金值）
// ---------------------------------------------------------------------

/// 黄金换算：G-10（n=10）→ c=0.1；G-20（n=50）→ c=0.02；η⁺＝η⁻＝目录
/// 效率；转子惯量取电机目录值；负载惯量按轴键控（缺键＝nullopt）。
TEST(SelCombinationCheckCodec, DriveInputConversionGoldenValues)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——候选传动参数构造——c＝1/n 单点换算（drivetrain 卡 §5.3 口径）
    const GoldenCombos golden = makeCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    snapshot.contentIdentity = computePackageContentIdentity(snapshot);
    snapshot.manifest.identity.contentIdentity = snapshot.contentIdentity;
    // 负载惯量只给 J1（J2 缺键——显式缺失面）。
    const std::vector<AxisDriveInput> load = {
        AxisDriveInput{golden.axes.j1, 0.0, 0.0, 0.0, 0.0, 2.5},
    };
    const std::vector<CombinationDriveInput> inputs =
        makeCombinationDriveInputs(snapshot, golden.combos, load);
    ASSERT_EQ(inputs.size(), golden.combos.size());
    // 首组合 K1＝两轴 (M-A,G-10)：c＝0.1、η＝0.9、rotor＝0.01；J1 有负载
    // 惯量 2.5、J2 缺键 nullopt。
    // 按轴面定位（字典序漂移防护——见支撑头 findCombo 注）。
    const DeviceCombination* k1Combo =
        findCombo(golden.combos, "M-A", "G-10", "M-A", "G-10");
    ASSERT_TRUE(k1Combo != nullptr);
    const DeviceCombination* k2Combo =
        findCombo(golden.combos, "M-A", "G-20", "M-A", "G-20");
    ASSERT_TRUE(k2Combo != nullptr);
    const CombinationDriveInput* k1 = nullptr;
    const CombinationDriveInput* k2 = nullptr;
    for (const CombinationDriveInput& drv : inputs) {
        if (drv.combinationId == k1Combo->id) { k1 = &drv; }
        if (drv.combinationId == k2Combo->id) { k2 = &drv; }
    }
    ASSERT_TRUE(k1 != nullptr);
    ASSERT_EQ(k1->axes.size(), std::size_t{2});
    EXPECT_DOUBLE_EQ(k1->axes[0].ratioC, 0.1);
    EXPECT_DOUBLE_EQ(k1->axes[0].etaForward, 0.9);
    EXPECT_DOUBLE_EQ(k1->axes[0].etaBackward, 0.9);
    EXPECT_DOUBLE_EQ(k1->axes[0].rotorInertia, 0.01);
    ASSERT_TRUE(k1->axes[0].loadInertiaJointSide.has_value());
    EXPECT_DOUBLE_EQ(*k1->axes[0].loadInertiaJointSide, 2.5);
    EXPECT_DOUBLE_EQ(k1->axes[1].ratioC, 0.1);
    EXPECT_FALSE(k1->axes[1].loadInertiaJointSide.has_value());
    // K2（G-20）：c＝0.02、η＝0.8。
    ASSERT_TRUE(k2 != nullptr);
    EXPECT_DOUBLE_EQ(k2->axes[0].ratioC, 0.02);
    EXPECT_DOUBLE_EQ(k2->axes[0].etaForward, 0.8);
}

/// 组合目录身份与快照不一致 ⇒ 构造拒绝（候选参数来源版本错配）。
TEST(SelCombinationCheckCodec, DriveInputRejectsForeignCatalog)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——组合目录错配——构造拒绝
    const GoldenCombos golden = makeCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    snapshot.contentIdentity = computePackageContentIdentity(snapshot);
    snapshot.manifest.identity.contentIdentity = snapshot.contentIdentity;
    std::vector<DeviceCombination> combos = golden.combos;
    combos[0].catalog.version = "v4";  // 异版本组合。
    EXPECT_THROW((void)makeCombinationDriveInputs(snapshot, combos, {}),
                 std::invalid_argument);
}
