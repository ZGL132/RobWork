/**
 * @file   FeasibleSetContractTest.cpp
 * @brief  可行集与淘汰原因输出契约用例组（SelFeasibleSetContract）——
 *         ReasonToken→SEL-* 稳定码映射封闭性（词表 34 token 全遍历＋core
 *         句法权威＋登记表同源）、sel 域 RequiredEvidenceProfile 注册闭环
 *         （validateEvidenceProfile＋真实 EvidenceProfileRegistry 注册/
 *         解析/重复拒绝——EVI-01"Profile 注册在前、评估器注册在后"时序的
 *         前提面）与 Profile↔评估器产出完备性核对语义（表 4 选型行）。
 *
 * 设计依据：
 *   - units/selection.md §10.3（淘汰原因词表——SEL-* 稳定码建议值随
 *     WP-19-T06 注册；不使用未经 diagnostics 注册的数字错误码）、§16
 *     （WP-19-T06 行——可行集与淘汰原因输出；EVI-01 选型 Profile 注册）、
 *     §11.2（Quick/Verified——Verified 必需项齐备）、§14.6（词表唯一
 *     实现点）
 *   - REQUIREMENTS §8.1 表 4（选型行必需/建议证据项——Profile 逐行实例
 *     化的内容权威）、EVI-01（RequiredEvidenceProfile 契约）
 *   - units/evidence.md §6.1/§9.5（Profile 数据结构/注册期校验/注册表）、
 *     §6.2（完备性核对语义——requiredGaps 全量列出、Satisfied 满足）
 *   - 任务契约 tasks/foundation/WP-19-T06.json acceptance 1/3
 *
 * 收编边界（诚实声明，DiagCodesTest 同款口径）：本单元依赖白名单仅
 * core＋evidence 编译边——evidence 是登记边（卡 §3.2 编译链接列两行），
 * EvidenceProfileRegistry 属 evidence 公共头（Evaluator.hpp），经真实类
 * 验证注册闭环合法（与 diagnostics StableCodeRegistry 的 L5 装配收编不
 * 同——后者无编译边，以登记表＋句法权威验证）；未执行的真实 StableCode
 * Registry 注册不在此标注通过（DiagCodesTest 文件头注收编确认锚点）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include <string>
#include <vector>

using namespace sdurws::ird;
using namespace sdurws::ird::selection;

namespace {

/// 全部映射码值都在登记表中（复用码与 T06 批新码同源——词表映射不得
/// 产出登记表之外的码值：单一登记面，NFR-MNT-03）。
bool codeRegistered(const std::string& code,
                    const std::vector<DiagnosticEntry>& entries)
{
    for (const DiagnosticEntry& e : entries) {
        if (e.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------
// acceptance 1：词表→稳定码映射封闭性（逐项淘汰原因的 diagRef 面）
// ---------------------------------------------------------------------

/// 词表 34 token 全遍历：映射非空＋core 句法权威校验＋码值全部在登记表
/// ＋复用码锚（不新造同义码——组合不兼容/范围外/身份族三锚）。
TEST(SelFeasibleSetContract, TokenDiagCodeMappingClosed_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"AT-08"});  // R1——词表↔稳定码映射封闭性
    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();
    ASSERT_EQ(entries.size(), std::size_t{45}) << "登记表全表 45 码（T02 批 17＋T06 批 28）";

    // 全遍历（kReasonTokenCount＝词表规模冻结值——封闭词表遍历上界）。
    for (int i = 0; i < kReasonTokenCount; ++i) {
        const auto token = static_cast<ReasonToken>(i);
        const std::string_view code = reasonTokenDiagCode(token);
        EXPECT_FALSE(code.empty())
            << "词表 token " << i << " 无稳定码映射（映射封闭性破坏）";
        // core 句法权威（DiagnosticRecord::make C-3 校验——^
        // [A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64；违约抛 CoreError）。
        EXPECT_NO_THROW({
            core::DiagnosticRecord::make(
                std::string{code}, {}, {}, {},
                "词表→稳定码映射契约校验", "T06 词表映射封闭性用例", "无——句法验证用例");
        }) << "映射码句法违约: " << code;
        // 登记表同源：映射码必须已在登记表（复用码与新增码同面）。
        EXPECT_TRUE(codeRegistered(std::string{code}, entries))
            << "映射码不在登记表（单一登记面破坏）: " << code;
    }

    // 复用码锚（已有卡面具名码的 token 不新造同义码——DiagCodes.hpp
    // T06 批区块头注构造规则）。
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::ComboIncompatible),
              kSelComboIncompatible);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::AxisMappingIncomplete),
              kSelComboAxisMappingIncomplete);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::IdentityMismatch),
              kSelIdentityMismatch);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::CatalogVersionIncompatible),
              kSelIdentityMismatch);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::MappingVersionIncompatible),
              kSelIdentityMismatch);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::AxisOutOfScope),
              kSelInputAxisOutOfScope);
    // 确定性：同 token 两次映射同值（NFR-COR-02）。
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::TorquePeakInsufficient),
              reasonTokenDiagCode(ReasonToken::TorquePeakInsufficient));
}

/// 组装器输出回填：经接口消费的组合级记录，输出侧每条淘汰原因的 diagRef
/// 齐备（§10.3"每条原因包含全部字段"——回填唯一入口在组装器输出装配）。
TEST(SelFeasibleSetContract, BuilderBackfillsDiagRefOnOutput_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-06", "ERR-01"},
                  std::vector<std::string>{});  // R1——输出侧 diagRef 全量回填
    // 手工最小夹具：淘汰记录（一条无 diagRef 的原因）＋全 Pass 格。
    CatalogPackageSnapshot snapshot;
    snapshot.manifest.identity.catalogId = "cat-contract";
    snapshot.manifest.identity.version = "v1";
    IdentityBlock identity;
    identity.catalog = snapshot.manifest.identity;
    identity.contractVersion = 1;

    DeviceCombination combo;
    combo.id = "combo-contract";
    combo.catalog = identity.catalog;
    AxisDeviceAssignment axis;
    axis.jointId = core::ObjectId::generate();  // 轴 ID（测试面生成——值语义持有）
    axis.motorModelId = "M-A";
    axis.gearboxModelId = "G-10";
    combo.axes = {axis};

    FeasibilityRecord rec;
    rec.id = combo.id;
    rec.deviceKind = DeviceKind::Combination;
    rec.verdict = VerdictKind::Rejected;
    RejectionReason reason;
    reason.token = ReasonToken::ComboIncompatible;
    reason.thresholdSource = "compatibility.csv";
    rec.reasons.push_back(reason);  // diagRef 空——组装器回填面。

    CaseCoverageEntry cell;
    cell.combinationId = combo.id;
    cell.caseId = "case-A";
    cell.verdict = VerdictKind::Rejected;  // 格 Fail 与记录一致——不触发降级。

    std::vector<FeasibleSetEntry> entries;
    FeasibleSetEntry entry;
    entry.combination = combo;
    entries.push_back(std::move(entry));

    const IFeasibleSetBuilder& builder = FeasibleSetBuilder{};
    const SelectionRunResult result
        = builder.build({rec}, {cell}, identity, entries);
    ASSERT_EQ(result.set.records.size(), std::size_t{1});
    ASSERT_FALSE(result.set.records[0].reasons.empty());
    ASSERT_TRUE(result.set.records[0].reasons[0].diagRef.has_value());
    EXPECT_EQ(*result.set.records[0].reasons[0].diagRef,
              std::string(kSelComboIncompatible));
}

// ---------------------------------------------------------------------
// acceptance 3：sel 域 Profile 注册闭环（EVI-01——表 4 选型行）
// ---------------------------------------------------------------------

/// Profile 实例化与注册闭环：validateEvidenceProfile 零问题→经真实
/// EvidenceProfileRegistry 注册成功→findProfile 可解析（评估器注册闭包
/// 前提——"Profile 注册在前"）→必需 4 项/建议 2 项逐行对齐表 4 选型行→
/// 重复注册拒绝（ProfileDuplicate）。
TEST(SelFeasibleSetContract, SelProfileValidatesAndRegisters_WP19T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01"},
                  std::vector<std::string>{});  // R1——sel Profile 注册闭环（表 4 选型行）
    const evidence::RequiredEvidenceProfile profile = makeSelRequiredEvidenceProfile();

    // Profile 级身份：域 id/版本与 T05 评估器 descriptor 绑定同值
    // （同一 sel 域 Profile 实例——注册时序前提）。
    EXPECT_EQ(profile.profileId, std::string(kSelProfileId));
    EXPECT_EQ(profile.version, std::string(kSelProfileVersion));
    // contentIdentity 零值＝域不可申报（注册时 evidence 计算回填——§6.1）。
    EXPECT_FALSE(profile.contentIdentity.isValid());

    // 注册期校验（evidence §6.1/§9.5——item 词形/说明非空/替代标志一致性）。
    const std::vector<evidence::ProfileIssue> issues
        = evidence::validateEvidenceProfile(profile);
    EXPECT_TRUE(issues.empty()) << "sel Profile 注册期校验失败（首个问题："
                                << (issues.empty() ? std::string{}
                                                   : issues.front().message)
                                << "）";

    // 表 4 选型行必需项逐行（四行——itemId 词表与常量同源）。
    ASSERT_EQ(profile.required.size(), std::size_t{4});
    EXPECT_EQ(profile.required[0].itemId, std::string(kSelProfileItemCatalogLock));
    EXPECT_EQ(profile.required[1].itemId, std::string(kSelProfileItemMotorOpPoint));
    EXPECT_EQ(profile.required[2].itemId, std::string(kSelProfileItemRejectionReasons));
    EXPECT_EQ(profile.required[3].itemId, std::string(kSelProfileItemComboCompatibility));
    for (const evidence::EvidenceProfileItem& item : profile.required) {
        EXPECT_EQ(item.itemClass, evidence::EvidenceItemClass::Required);
        EXPECT_FALSE(item.substitutableByInfeasibility)
            << "选型必需项不存在'因不可行而无法生成'的豁免面（全淘汰亦有原因记录）";
        EXPECT_FALSE(item.description.empty());
        EXPECT_FALSE(item.applicability.has_value()) << "四必需项恒适用（无条件）";
    }
    // 建议项两行（成本/质量汇总＋优选品牌供应状态——缺失不阻断）。
    ASSERT_EQ(profile.suggested.size(), std::size_t{2});
    EXPECT_EQ(profile.suggested[0].itemId, std::string(kSelProfileItemCostMassSummary));
    EXPECT_EQ(profile.suggested[1].itemId, std::string(kSelProfileItemVendorAvailability));
    for (const evidence::EvidenceProfileItem& item : profile.suggested) {
        EXPECT_EQ(item.itemClass, evidence::EvidenceItemClass::Suggested);
    }

    // 真实注册表闭环（evidence §9.5——L5 装配时序"Profile 注册在前"的
    // 数据面验证； EvidenceProfileRegistry 属 evidence 登记边公共头）。
    evidence::EvidenceProfileRegistry registry;
    registry.registerProfile(profile);  // 首次注册成功（违约即抛 EvidenceError）。
    const evidence::RequiredEvidenceProfile* found
        = registry.findProfile(kSelProfileId, kSelProfileVersion);
    ASSERT_TRUE(found != nullptr) << "注册后必须可解析（评估器注册闭包前提）";
    // 注册表回填内容身份（域不可申报——注册时计算）。
    EXPECT_TRUE(found->contentIdentity.isValid());
    const core::ContentIdentity expectIdentity
        = evidence::computeProfileContentIdentity(profile);
    EXPECT_TRUE(found->contentIdentity == expectIdentity);

    // 重复 (profileId, version) 注册拒绝（ProfileDuplicate——§9.5）。
    EXPECT_THROW({
        try {
            registry.registerProfile(profile);
        } catch (const evidence::EvidenceError& e) {
            EXPECT_EQ(e.code(), evidence::EvidenceErrorCode::ProfileDuplicate);
            throw;
        }
    }, evidence::EvidenceError);

    // 确定性：同 Profile 两次计算同内容身份（NFR-COR-02）。
    EXPECT_TRUE(evidence::computeProfileContentIdentity(profile)
                == evidence::computeProfileContentIdentity(profile));
}

/// Profile↔评估器产出完备性核对语义（表 4 选型行——汇总层消费面预演）：
/// 组合校核评估器产出（必需项①③④＋总证据项）核对后，requiredGaps 恰
/// 含必需项②（每组合电机侧工作点——dt.mapping 上游产物，本批不产，不
/// 伪造）；补齐②后必需项全满足、建议项缺失单独列出（不阻断）。
TEST(SelFeasibleSetContract, SelProfileCompletenessAgainstEvaluatorOutput_WP19T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01", "EVI-02"},
                  std::vector<std::string>{});  // R1——Profile↔产出完备性核对语义
    const evidence::RequiredEvidenceProfile profile = makeSelRequiredEvidenceProfile();

    // 评估器产出清单（CombinationCheckEvaluator 输出装配的 4 项——与
    // 该评估器测试断言同源；Satisfied 必填产物摘要——以非零摘要占位）。
    core::Digest256 digest;
    digest.fill(std::uint8_t{1});
    const auto satisfied = [&](const char* itemId) {
        evidence::EvidenceItem item;
        item.itemId = itemId;
        item.status = evidence::EvidenceItemStatus::Satisfied;
        item.artifactDigest = digest;
        return item;
    };
    evidence::EvidenceManifest manifest;
    manifest.profileId = std::string(kSelProfileId);
    manifest.profileVersion = std::string(kSelProfileVersion);
    manifest.profileContentIdentity
        = evidence::computeProfileContentIdentity(profile);
    manifest.items = {
        satisfied("sel.combination-check"),
        satisfied(std::string(kSelProfileItemCatalogLock).c_str()),
        satisfied(std::string(kSelProfileItemRejectionReasons).c_str()),
        satisfied(std::string(kSelProfileItemComboCompatibility).c_str()),
    };

    // 必需项②缺上游产物 → requiredGaps 恰含该项（全量列出——不因存在
    // 其他证据豁免；EVI-02/表 2 ④"缺失项全量列出"的核对面）。
    const evidence::EvidenceCompletenessResult partial
        = evidence::checkEvidenceCompleteness(profile, manifest);
    ASSERT_EQ(partial.requiredGaps.size(), std::size_t{1});
    EXPECT_EQ(partial.requiredGaps[0].itemId,
              std::string(kSelProfileItemMotorOpPoint));
    // 建议项缺失单独标注（不进 requiredGaps——不阻断）。
    ASSERT_EQ(partial.suggestedGaps.size(), std::size_t{2});

    // 补齐必需项②（dt.mapping 批产出的登记形态）→ 必需项全满足。
    manifest.items.push_back(satisfied(std::string(kSelProfileItemMotorOpPoint).c_str()));
    const evidence::EvidenceCompletenessResult full
        = evidence::checkEvidenceCompleteness(profile, manifest);
    EXPECT_TRUE(full.requiredGaps.empty())
        << "四必需项齐备后不得再报缺口（表 4 选型行承载齐备）";
    EXPECT_EQ(full.suggestedGaps.size(), std::size_t{2});
}

/// 词表→Profile 双面同源：词表映射函数产出的全部码值与 DiagCodes 登记表
/// 失同步即时暴露（换锚抽验——登记行数与首尾码值，与 DiagCodesTest 的
/// 机械比对互为冗余防线）。
TEST(SelFeasibleSetContract, RegistryTableT06BatchAnchors_WP19T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-03"},
                  std::vector<std::string>{});  // R1——T06 批登记锚（双防线冗余）
    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();
    ASSERT_EQ(entries.size(), std::size_t{45});
    // T06 批首行（表尾追加起点——词表电机组首 token 码）。
    EXPECT_EQ(entries[17].code, kSelMotorTorqueContinuousInsufficient);
    // 全表尾行（词表边界/偏好组末 token 码）。
    EXPECT_EQ(entries.back().code, kSelUserPreferenceFiltered);
    // T06 批码数＝28（45−17）。
    EXPECT_EQ(entries.size() - std::size_t{17}, std::size_t{28});
}
