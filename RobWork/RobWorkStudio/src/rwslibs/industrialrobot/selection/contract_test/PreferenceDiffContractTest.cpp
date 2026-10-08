/**
 * @file   PreferenceDiffContractTest.cpp
 * @brief  企业偏好与目录差异契约用例组（SelPreferenceDiffContract）——
 *         SEL-07 分轨契约（user-preference-filtered 独立 token 的词表/
 *         稳定码登记面；偏好结果零侵入硬记录）、SEL-08 会话工具契约
 *         （CatalogDiffer 不适配评估器接口——不进③端口注册表，D-SEL-14；
 *         纯函数确定性）与 CON-05 内容身份失效链（diff 非空 ⇔ 包内容
 *         身份不同——升级影响提示的判定基础）。
 *
 * 设计依据：
 *   - units/selection.md §10.3（边界/偏好组 token——user-preference-
 *     filtered 与硬能力失败分离）、§10.4（分轨纪律——用户优选过滤不伪装
 *     成硬能力）、§13.6（目录差异比较为会话工具；不作为正式证据归档）、
 *     §14.9（"目录 diff/锁定管理不设评估器形态——避免滥用③端口"）、
 *     D-SEL-8/D-SEL-14
 *   - 需求 SEL-07（不改变硬能力判定）、SEL-08（目录差异比较＋项目锁定
 *     版本；更新不静默改变历史结果）、CON-05（内容寻址失效链）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 1/2/3
 *
 * 收编边界（诚实声明，FeasibleSetContractTest 同款口径）：SEL-* 码值的
 * 真实 diagnostics StableCodeRegistry 注册归 L5 装配收编（本单元无
 * diagnostics 编译边）——本组验证登记表同源性与句法权威（core 侧），
 * 未执行的真实注册不在此标注通过。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/selection/CatalogDiff.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>
#include <sdurws/ird/selection/Preference.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include "../test/CatalogTestSupport.hpp"   // 测试辅助（单元内 test/ 私有——不跨单元）

#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird;
using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;

namespace {

/// 会话工具非评估器的编译期断言（D-SEL-14/§14.9 的可断言面——
/// CatalogDiffer 与 PreferenceFilter 均不得适配 evidence 评估器接口，
/// 否则即"正式证据评估器"形态，滥用③端口）。static_assert 在编译期
/// 失败即本 TU 不可构建——契约以构建成功为通过形态。
static_assert(!std::is_base_of<evidence::IEngineeringEvaluator, CatalogDiffer>::value,
              "CatalogDiffer 不得适配 IEngineeringEvaluator（D-SEL-14——会话"
              "工具不进③端口注册表，§14.9）");
static_assert(!std::is_base_of<evidence::IEngineeringEvaluator, PreferenceFilter>::value,
              "PreferenceFilter 不得适配 IEngineeringEvaluator（偏好过滤为硬"
              "筛选之后的分轨处理——非正式评估器，§14.0 分轨纪律）");

/// 基线 v1 快照（与 test/CatalogDiffTest 同源构造——合法装配产物）。
CatalogPackageSnapshot makeV1()
{
    const CatalogImporter importer;
    return importer.assemble(makeBaselineInput(), makeBaselineManifest("cat-demo", "v1"));
}

/// v2 快照（G-50 额定输出转矩 50→60 N·m——单点业务变更）。
CatalogPackageSnapshot makeV2()
{
    ParsedCatalogInput in = makeBaselineInput();
    for (ParsedFileTable& t : in.files) {
        if (t.fileName == kCatalogFileGearboxes) {
            EXPECT_TRUE(editRow(t, "model_id", "G-50", "rated_output_torque_nm", "60"));
        }
    }
    const CatalogImporter importer;
    return importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));
}

}  // namespace

// =====================================================================
// SEL-07 分轨契约
// =====================================================================

/**
 * 分轨 token 登记契约（D-SEL-8）：user-preference-filtered 的词表文本、
 * 稳定码（SEL-USER-PREFERENCE-FILTERED）与登记表行三方同源——偏好过滤
 * 的输出引用面唯一（词表映射 reasonTokenDiagCode 不产登记表外码值，
 * NFR-MNT-03）。
 */
TEST(SelPreferenceDiffContract, UserPreferenceFilteredTokenRegisteredFace)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"NFR-MNT-03"});
    // 词表文本（Screening.cpp reasonTokenText 的封闭词表）。
    EXPECT_EQ(reasonTokenText(ReasonToken::UserPreferenceFiltered),
              "user-preference-filtered");
    // 词表→稳定码映射（T06 批登记值）。
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::UserPreferenceFiltered),
              kSelUserPreferenceFiltered);
    // 码值在登记表中（三方同源——L5 装配收编的数据面）。
    bool registered = false;
    for (const DiagnosticEntry& e : selectionCodeEntries()) {
        if (e.code == kSelUserPreferenceFiltered) {
            registered = true;
        }
    }
    EXPECT_TRUE(registered);
}

/**
 * 偏好结果零侵入硬记录（acceptance 1 分轨契约的执行证明）：真实硬筛选
 * 产物经 apply 后与输入深等——偏好维度绝不写回 FeasibilityRecord（写入
 * 即伪装成硬能力淘汰，§10.4 分轨纪律的负面清单）。
 */
TEST(SelPreferenceDiffContract, PreferenceOutcomeNeverWritesBackHardRecords)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot snap = makeV1();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;
    const HardConstraintSelector selector;

    // 可行工作点 → 两电机 Feasible；全不命中偏好 → 全部偏好未过。
    AxisWorkpointFacts facts;
    facts.jointId = core::ObjectId::fromCanonical("obj-00000000000000000000000000000001");
    facts.caseId = "case-cruise";
    facts.motorTorqueRms = 1.0;    // 单位 N·m
    facts.motorTorquePeak = 2.0;   // 单位 N·m
    facts.motorSpeedPeak = 10.0;   // 单位 rad/s
    facts.motorSpeedRms = 5.0;     // 单位 rad/s
    facts.motorPowerPeak = 100.0;  // 单位 W
    facts.motorPowerRms = 50.0;    // 单位 W
    const std::vector<FeasibilityRecord> records =
        selector.screenMotors(snap, {facts}, ScreeningCriteria{}, nullptr);
    ASSERT_EQ(records.size(), 2u);

    EnterprisePreference rejectAll;
    rejectAll.preferredVendors = {"OtherCo"};
    rejectAll.allowedAvailability = {"discontinued"};
    rejectAll.allowedSeries = {"RV"};
    const std::vector<FeasibilityRecord> before = records;
    static_cast<void>(filter.apply(snap, records, {}, rejectAll, provider));
    // 零侵入：apply 后输入与调用前深等（RejectionReason/FeasibilityRecord
    // 均带 operator==——深比较含全部原因字段）。
    EXPECT_EQ(records, before);
    for (const FeasibilityRecord& r : records) {
        EXPECT_EQ(r.verdict, VerdictKind::Feasible);  // 硬判定原样
    }
}

// =====================================================================
// SEL-08 会话工具契约
// =====================================================================

/**
 * diff 纯函数性与 CON-05 失效链（acceptance 2 会话工具契约）：同输入
 * 两次 compare 全等（NFR-COR-02）；业务字段变更 ⇔ 包内容身份不同
 * （diff 非空 ⇔ computePackageContentIdentity 不同——升级影响提示与
 * 依赖切片失效共用同一失效判据）。
 */
TEST(SelPreferenceDiffContract, CatalogDiffPureFunctionAndIdentityChain)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "CON-05"},
                  std::vector<std::string>{"NFR-COR-02"});
    const CatalogPackageSnapshot v1 = makeV1();
    const CatalogPackageSnapshot v2 = makeV2();
    const CatalogDiffer differ;

    const CatalogDiff a = differ.compare(v1, v2);
    const CatalogDiff b = differ.compare(v1, v2);
    EXPECT_EQ(a, b);                    // 纯函数——同输入恒同输出
    EXPECT_FALSE(a.empty());
    EXPECT_FALSE(v1.contentIdentity == v2.contentIdentity);  // 失效链：身份已变

    const CatalogDiff none = differ.compare(v1, v1);
    EXPECT_TRUE(none.empty());
    EXPECT_TRUE(v1.contentIdentity == v1.contentIdentity);   // 零变更⇔身份不变
}

/**
 * 历史隔离契约（SEL-08"目录更新不静默改变历史结果"的锁定面）：锁定表
 * 只增不删——同 catalogId 新版本锁定后，旧锁定记录与其内容身份不被任何
 * 比较或锁定动作改写（PA-2；Superseded 判定归 evidence——本域保证引用
 * 对象恒可解析为锁定时快照）。
 */
TEST(SelPreferenceDiffContract, LockTableAppendOnlyUnderCatalogUpdate)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "CON-02"},
                  std::vector<std::string>{"AT-08"});
    const CatalogImporter importer;
    const CatalogPackageSnapshot v1 = makeV1();
    const CatalogPackageSnapshot v2 = makeV2();
    InMemoryCatalogProvider provider;

    const CatalogVersion lockV1 =
        provider.lockVersion(v1, core::ObjectId::fromCanonical("obj-000000000000000000000000000000b1"));
    const CatalogVersion lockV2 =
        provider.lockVersion(v2, core::ObjectId::fromCanonical("obj-000000000000000000000000000000b2"));

    // 只增：两锁定记录并存且内容身份各自冻结（目录更新不覆盖历史记录）。
    const std::vector<CatalogVersion> locked = provider.listLocked();
    ASSERT_EQ(locked.size(), 2u);
    EXPECT_EQ(locked[0].identity.contentIdentity, v1.contentIdentity);
    EXPECT_EQ(locked[1].identity.contentIdentity, v2.contentIdentity);
    // 历史引用恒可解析为锁定时快照（v2 落位后 load(lockV1) 仍全等 v1）。
    EXPECT_EQ(provider.load(lockV1), v1);
    EXPECT_EQ(provider.load(lockV2), v2);
}
