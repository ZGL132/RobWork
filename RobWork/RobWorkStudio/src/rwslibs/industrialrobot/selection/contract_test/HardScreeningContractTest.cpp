/**
 * @file   HardScreeningContractTest.cpp
 * @brief  硬筛选契约测试组（SelHardScreeningContract）——公共接口交付面
 *         钉扎（经 IHardConstraintSelector 接口分派消费——不留只测实现
 *         类的盲区）、§10.3 词表封闭性、ERR-01 原因字段完备性、§14.4
 *         取消截断语义与空集语义。
 *
 * 设计依据：
 *   - units/selection.md §14.4（接口契约——签名/异常/取消语义）、
 *     §14.9（ICancellation 复用 evidence 既有形态——本文件落地该裁决：
 *     ctx 参数类型＝evidence::IEvaluationContext，测试以本地替身实现）、
 *     §10.3（词表封闭——"token（示例全表按组登记）"的封闭性合同）、
 *     §10.2（空集语义——空候选/空轴合法非错误）、§14.0（通用约定：
 *     调用方错误 fail-fast）
 *   - 需求 SEL-03/04/06、ERR-01（比较型字段齐备）、NFR-COR-02
 *   - 任务契约 tasks/foundation/WP-19-T04.json（acceptance 1 全维度经
 *     接口消费；"公共接口交付面至少一条经接口消费的用例钉扎"——
 *     WP-20-T03 接口路径零覆盖教训，任务提示 3）
 *
 * 与单元测试（test/ScreeningMotorTest|ScreeningGearboxTest）的分工：
 *   单元测试管黄金值与逐维判定正确性；本契约文件管"合同面"——接口
 *   分派可达性、词表稳定性、字段完备性、取消/空集/异常语义。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;  // 使限定符 core:: 可见（替身与夹具直用）

namespace {

// =====================================================================
// 本地替身（evidence 卡 §11 测试设施形态——替身只验证契约）
// =====================================================================

/// 取消查询替身：第 n 次查询起返回 true（n 可控——截断语义验证）。
class CancelAfterNContext final : public evidence::IEvaluationContext {
public:
    explicit CancelAfterNContext(int cancelAtNth) : cancelAtNth_(cancelAtNth) {}

    bool cancellationRequested() const override
    {
        ++queries_;
        return queries_ >= cancelAtNth_;  // 第 cancelAtNth_ 次起取消
    }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(core::ObjectId,
                                                            core::ContentVersion) const override
    {
        return std::nullopt;  // 硬筛选不消费对象读取——恒空
    }

private:
    int cancelAtNth_;   ///< 第几次查询开始取消（1 起）
    mutable int queries_ = 0;  ///< 查询计数（mutable——const 接口内累加）
};

/// 夹具构造（黄金形态最小集——与单元测试同构，字段值只求可判）。
MotorCatalogEntry makeMotor(const std::string& id, double ratedTorque)
{
    MotorCatalogEntry m;
    m.modelId = id;
    m.ratedTorque = ratedTorque;   // N·m
    m.peakTorque = ratedTorque * 2.0;
    m.ratedSpeed = 100.0;          // rad/s
    m.maxSpeed = 200.0;            // rad/s
    m.ratedPower = 1000.0;         // W
    m.dutyClass = "S1";
    m.rotorInertia = 0.01;         // kg·m²
    m.mass = 5.0;                  // kg
    return m;
}

GearboxCatalogEntry makeGearbox(const std::string& id, double ratedTorque)
{
    GearboxCatalogEntry g;
    g.modelId = id;
    g.ratedOutputTorque = ratedTorque;  // N·m
    g.peakOutputTorque = ratedTorque * 2.0;
    g.maxInputSpeed = 300.0;            // rad/s
    g.ratio = 100.0;
    g.efficiency = 0.95;
    g.mountingOrientation = "any";
    g.mass = 3.0;                       // kg
    return g;
}

CatalogPackageSnapshot snapshotWith(std::vector<MotorCatalogEntry> motors,
                                    std::vector<GearboxCatalogEntry> gearboxes)
{
    CatalogPackageSnapshot s;
    s.manifest.formatVersion = kCatalogFormatVersion;
    s.manifest.identity.catalogId = "cat-ct";
    s.manifest.identity.version = "v1";
    s.motors = std::move(motors);
    s.gearboxes = std::move(gearboxes);
    return s;
}

AxisWorkpointFacts factsOn(const core::ObjectId& axis, double tauRms)
{
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-1";
    f.motorTorqueRms = tauRms;    // N·m（电机维黄金驱动值）
    f.motorTorquePeak = tauRms;   // N·m（契约夹具全供给——零缺口基线，判定只看
                                  //   tauRms 是否超限：M-A 额定 10 时 5.0 通过）
    f.motorSpeedPeak = 1.0;       // rad/s（≤ 200）
    f.motorSpeedRms = 1.0;        // rad/s（≤ 100）
    f.motorPowerPeak = 50.0;      // W（≤ 1000）
    f.motorPowerRms = 50.0;       // W（≤ 1000）
    f.jointTorqueRms = tauRms;    // N·m（减速器维同值复用）
    f.jointTorquePeak = tauRms;
    f.jointSpeedPeak = 1.0;       // rad/s（→ ω_m 换算路径）
    return f;
}

}  // namespace

// =====================================================================
// 公共接口交付面钉扎（虚分派真实执行——接口路径零覆盖教训前移）
// =====================================================================

/** 经 IHardConstraintSelector& 接口分派调用 screenMotors——实现可达。 */
TEST(SelHardScreeningContract, MotorScreeningReachableThroughInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;  // 接口引用——虚分派
    core::ObjectId axis = core::ObjectId::generate();
    CatalogPackageSnapshot snap = snapshotWith({makeMotor("M-A", 10.0)}, {});
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(snap, {factsOn(axis, 5.0)}, ScreeningCriteria{}, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    EXPECT_EQ(recs[0].deviceKind, DeviceKind::Motor);
    EXPECT_EQ(recs[0].verdict, VerdictKind::Feasible);
}

/** 经 IHardConstraintSelector& 接口分派调用 screenGearboxes——实现可达。 */
TEST(SelHardScreeningContract, GearboxScreeningReachableThroughInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    core::ObjectId axis = core::ObjectId::generate();
    CatalogPackageSnapshot snap = snapshotWith({}, {makeGearbox("G-A", 50.0)});
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(snap, {factsOn(axis, 30.0)}, ScreeningCriteria{}, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    EXPECT_EQ(recs[0].deviceKind, DeviceKind::Gearbox);
    EXPECT_EQ(recs[0].verdict, VerdictKind::Feasible);
}

// =====================================================================
// 词表封闭性（§10.3——封闭词表合同：全表可遍历、文本唯一、格式稳定）
// =====================================================================

/**
 * 全表遍历：kReasonTokenCount 个 token 的文本非空、两两不同、格式
 * ^[a-z][a-z0-9-]*$（不以 '-' 结尾、无连续 '--'）——词表文本进入报告
 * 与序列化，格式破坏即呈现契约破坏；词表序（枚举序）锚点行钉住
 * （首行电机连续转矩、T12 批前末行用户优选过滤、现行末行直线功率——
 * 追加只允许表尾的次序契约；T12 批 4 token 为 WP-19-T12 表尾追加）。
 */
TEST(SelHardScreeningContract, ReasonTokenVocabularyIsClosedAndWellFormed)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "ERR-01"},
                  std::vector<std::string>{"NFR-COR-02"});

    std::set<std::string> seen;
    for (int i = 0; i < kReasonTokenCount; ++i) {
        const std::string_view text = reasonTokenText(static_cast<ReasonToken>(i));
        ASSERT_FALSE(text.empty()) << "token " << i << " 文本为空";
        ASSERT_TRUE(seen.insert(std::string(text)).second)
            << "token 文本重复（词表封闭性破坏）：" << text;
        // 格式检查：小写字母开头；只含小写字母/数字/'-'；不以 '-' 结尾。
        ASSERT_TRUE(std::islower(static_cast<unsigned char>(text.front())))
            << "token 首字符须小写字母：" << text;
        ASSERT_NE(text.back(), '-') << "token 不以 '-' 结尾：" << text;
        for (std::size_t c = 0; c < text.size(); ++c) {
            const char ch = text[c];
            ASSERT_TRUE(std::islower(static_cast<unsigned char>(ch))
                        || std::isdigit(static_cast<unsigned char>(ch)) || ch == '-')
                << "token 含非法字符：" << text;
            if (c > 0) {
                ASSERT_FALSE(ch == '-' && text[c - 1] == '-')
                    << "token 含连续 '--'：" << text;
            }
        }
    }
    // 词表序锚点（枚举序＝词表序＝稳定排序键——次序契约钉扎）。
    EXPECT_EQ(reasonTokenText(static_cast<ReasonToken>(0)), "torque-continuous-insufficient");
    // T12 批前末行（边界/偏好组末 token——表尾追加纪律下位置不变，
    // 距词表尾恰 4 个 T12 token——WP-19-T12）。
    EXPECT_EQ(reasonTokenText(static_cast<ReasonToken>(kReasonTokenCount - 5)),
              "user-preference-filtered");
    // 现行末行（T12 批尾 token——直线传动能力组末项）。
    EXPECT_EQ(reasonTokenText(static_cast<ReasonToken>(kReasonTokenCount - 1)),
              "linear-power-insufficient");
    // 越界防御（枚举外整数——不抛、返回占位文本）。
    EXPECT_EQ(reasonTokenText(static_cast<ReasonToken>(kReasonTokenCount)),
              "unknown-reason-token");
}

// =====================================================================
// ERR-01 原因字段完备性（数值类四要素／文本类双文本分轨）
// =====================================================================

/**
 * 多维失败记录的每条原因按 token 语义分轨完备：
 *   - 数值类（转矩/转速/效率/回隙/寿命/外载荷/输入转速）：unit 非空、
 *     thresholdSource 非空、actual/required 已填；
 *   - 文本类（工作制/安装）：actualText/requiredText 非空、unit 为空。
 * 淘汰原因"比较型字段齐备"是 SEL-06/ERR-01 的验收面（acceptance 2
 * "每个淘汰项含实际值和阈值"的契约级复验）。
 */
TEST(SelHardScreeningContract, RejectionReasonFieldsCompleteByTrack)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "ERR-01"}, std::vector<std::string>{"AT-08"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    core::ObjectId axis = core::ObjectId::generate();

    // 电机面：连续转矩失败（数值类）＋工作制失败（文本类）。
    CatalogPackageSnapshot msnap = snapshotWith({makeMotor("M-A", 1.0)}, {});
    ScreeningCriteria mcrit;
    mcrit.requiredDutyClass = "S3";
    AxisWorkpointFacts mf = factsOn(axis, 5.0);
    mf.motorTorquePeak = 1.0;  // N·m（≤ 峰值 2.0——峰值维不参差，本用例只钉两维）
    const std::vector<FeasibilityRecord> mrecs
        = selector.screenMotors(msnap, {mf}, mcrit, nullptr);
    ASSERT_EQ(mrecs.size(), 1U);
    ASSERT_EQ(mrecs[0].reasons.size(), 2U);  // 转矩＋工作制（词表序：torque→duty）
    const RejectionReason& torque = mrecs[0].reasons[0];
    EXPECT_EQ(torque.token, ReasonToken::TorqueContinuousInsufficient);
    EXPECT_FALSE(torque.unit.empty());
    EXPECT_FALSE(torque.thresholdSource.empty());
    EXPECT_TRUE(torque.actualText.empty());   // 数值类无文本侧
    const RejectionReason& duty = mrecs[0].reasons[1];
    EXPECT_EQ(duty.token, ReasonToken::DutyMismatch);
    EXPECT_TRUE(duty.unit.empty());           // 文本类无数值单位
    EXPECT_FALSE(duty.actualText.empty());
    EXPECT_FALSE(duty.requiredText.empty());
    EXPECT_FALSE(duty.thresholdSource.empty());

    // 减速器面：额定转矩（数值）＋效率（数值）；峰值工作点压在峰值能力
    // 域内（5.0 ＞ 2.0 会额外触发峰值维——本用例只钉两维的字段分轨）。
    CatalogPackageSnapshot gsnap = snapshotWith({}, {makeGearbox("G-A", 1.0)});
    ScreeningCriteria gcrit;
    gcrit.minEfficiency = 0.99;
    AxisWorkpointFacts gf = factsOn(axis, 5.0);
    gf.jointTorquePeak = 1.0;  // N·m（≤ 峰值 2.0——峰值维通过）
    const std::vector<FeasibilityRecord> grecs
        = selector.screenGearboxes(gsnap, {gf}, gcrit, nullptr);
    ASSERT_EQ(grecs.size(), 1U);
    ASSERT_EQ(grecs[0].reasons.size(), 2U);  // rated-torque → efficiency（词表序）
    for (const RejectionReason& r : grecs[0].reasons) {
        EXPECT_FALSE(r.unit.empty()) << "数值类原因缺单位（token="
                                     << std::string(reasonTokenText(r.token)) << "）";
        EXPECT_FALSE(r.thresholdSource.empty()) << "原因缺阈值来源（ERR-01）";
    }
}

// =====================================================================
// 取消截断语义（§14.4 ctx——批次边界＝候选条目边界；落位细化 T04 ④）
// =====================================================================

/**
 * 第 2 次查询（第 2 个候选处理前）取消：两候选单轴 → 仅返回第 1 候选的
 * 记录（截断由调用方以记录数对比 候选数×轴数 感知——诚实截断，不伪造
 * 完整筛选）。
 */
TEST(SelHardScreeningContract, CancellationTruncatesAtCandidateBoundary)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"TASK-01"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    core::ObjectId axis = core::ObjectId::generate();
    CatalogPackageSnapshot snap = snapshotWith({makeMotor("M-A", 10.0), makeMotor("M-B", 10.0)}, {});
    AxisWorkpointFacts f = factsOn(axis, 5.0);
    CancelAfterNContext ctx(/*cancelAtNth=*/2);  // 第 2 次查询＝第 2 候选边界取消

    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(snap, {f}, ScreeningCriteria{}, &ctx);
    ASSERT_EQ(recs.size(), 1U);  // 截断：仅首候选
    EXPECT_EQ(recs[0].candidateModelId, "M-A");
}

/** 不取消（查询恒 false）：全量记录返回——取消通道不误伤正常路径。 */
TEST(SelHardScreeningContract, NoCancellationReturnsFullSet)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"TASK-01"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    core::ObjectId axis = core::ObjectId::generate();
    CatalogPackageSnapshot snap = snapshotWith({makeMotor("M-A", 10.0), makeMotor("M-B", 10.0)}, {});
    CancelAfterNContext ctx(/*cancelAtNth=*/1 << 30);  // 实际不取消
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(snap, {factsOn(axis, 5.0)}, ScreeningCriteria{}, &ctx);
    EXPECT_EQ(recs.size(), 2U);
}

// =====================================================================
// 空集与异常语义（§10.2/§14.0）
// =====================================================================

/** 空候选快照（合法输入）：返回空记录向量——非错误（§10.2 空集语义）。 */
TEST(SelHardScreeningContract, EmptySnapshotYieldsEmptyRecordsNoThrow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03", "SEL-04"}, std::vector<std::string>{"AT-08"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    CatalogPackageSnapshot snap = snapshotWith({}, {});
    core::ObjectId axis = core::ObjectId::generate();
    EXPECT_NO_THROW({
        EXPECT_TRUE(selector.screenMotors(snap, {factsOn(axis, 5.0)}, ScreeningCriteria{}, nullptr)
                        .empty());
        EXPECT_TRUE(selector
                        .screenGearboxes(snap, {factsOn(axis, 5.0)}, ScreeningCriteria{}, nullptr)
                        .empty());
    });
}

/** 空轴 facts：返回空记录向量（无轴可筛——合法）。 */
TEST(SelHardScreeningContract, EmptyAxisFactsYieldEmptyRecords)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    CatalogPackageSnapshot snap = snapshotWith({makeMotor("M-A", 10.0)}, {});
    EXPECT_TRUE(selector.screenMotors(snap, {}, ScreeningCriteria{}, nullptr).empty());
}

/** 调用方契约违约经接口分派仍 fail-fast（异常语义不因接口层吞掉）。 */
TEST(SelHardScreeningContract, ContractViolationFailsFastThroughInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"NFR-COR-03"});

    HardConstraintSelector impl;
    IHardConstraintSelector& selector = impl;
    core::ObjectId axis = core::ObjectId::generate();
    CatalogPackageSnapshot snap = snapshotWith({makeMotor("M-A", 10.0)}, {});
    ScreeningCriteria bad;
    bad.safetyFactor = 0.5;  // <1——调用方契约违约
    EXPECT_THROW((void)selector.screenMotors(snap, {factsOn(axis, 5.0)}, bad, nullptr),
                 std::invalid_argument);
}
