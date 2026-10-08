/**
 * @file   EvidenceBoundaryContractTest.cpp
 * @brief  WP-17-T06 契约测试——dyn 证据面的契约钉扎：dyn Profile 六项
 *         itemId 词表冻结（报告/汇总/缺失清单的关联键——漂移即 RPT-05/
 *         EVI-01 对账断链）＋DYN-06 限定语 token 词表冻结（reporting §6.4
 *         词表 dynamics 侧产出三值——限定语纪律的机器判读面）＋限定语
 *         判定纯函数行为契约（三层互斥呈现、层序确定）。
 *
 * 设计依据：
 *   - units/dynamics.md §8.4（dyn Profile 六项 itemId/itemClass——"实现
 *     落位冻结"列）、§5.5（三层降级与限定语）、§9.6（限定语词表——
 *     RPT-05 不得弱化）、§10.6（装配器契约形态）
 *   - units/evidence.md §6.1（itemId 词形闸门/五域词表）、§6.2（证据项
 *     状态五值——状态映射契约的词表权威）
 *   - 需求 DYN-06（不把估算结果包装成精确结论）、RPT-05（限定语冻结）、
 *     EVI-01（Profile 全局唯一）
 *   - 先例：RneaBoundaryContractTest/ForwardDynamicsBoundaryContractTest
 *     （静态钉扎＋契约形态核对形态）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/EvidenceBuilder.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <string>
#include <string_view>
#include <vector>

using namespace sdurws::ird::dynamics;
namespace evidence = sdurws::ird::evidence;

// =====================================================================
// 用例 1：dyn Profile 六项 itemId/itemClass/替代标志词表静态钉扎（§8.4
//   "实现落位冻结"列——itemId 是证据项与 evidence 汇总/报告限定语对账的
//   关联键，任何改动都是跨域契约破坏；编译期字面量对照＋运行期词形闸门
//   双面）。
// =====================================================================

TEST(DynEvidenceBoundary, DynProfileItemIdsFrozen_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01", "DYN-06"}, std::vector<std::string>{});
    // 本用例验证：六项 itemId 常量与 §8.4 表冻结值逐字一致（编译期
    // static_assert——常量被改动即编译失败，防静默漂移）；词形全部通过
    // evidence isValidProfileItemId 闸门（"<域>.<项>"）；Profile 实例的
    // itemClass/substitutableByInfeasibility 与 §8.4 表逐行一致（Required×4
    // ＋Suggested×2；③④不可替代——非成功产物类）。
    static_assert(kDynItemJointSeries == "dyn.joint-generalized-force-series",
                  "§8.4 必需项① itemId 冻结值漂移");
    static_assert(kDynItemPeakRms == "dyn.peak-rms-statistics",
                  "§8.4 必需项② itemId 冻结值漂移");
    static_assert(kDynItemProvenance == "dyn.property-friction-provenance",
                  "§8.4 必需项③ itemId 冻结值漂移");
    static_assert(kDynItemLoadCondition == "dyn.load-condition-identity",
                  "§8.4 必需项④ itemId 冻结值漂移");
    static_assert(kDynItemPowerEnergy == "dyn.power-energy-split",
                  "§8.4 建议项⑤ itemId 冻结值漂移");
    static_assert(kDynItemForwardCheck == "dyn.forward-dynamics-consistency",
                  "§8.4 建议项⑥ itemId 冻结值漂移");
    static_assert(kDynProfileId == "dyn", "Profile 域 id 冻结值漂移（五域词表成员）");
    static_assert(kDynProfileVersion == "1", "Profile 版本冻结值漂移（§8.4 version=1）");

    // 运行期词形闸门（evidence §6.1 注册期同款校验——双重防线）。
    EXPECT_TRUE(evidence::isValidProfileItemId(kDynItemJointSeries));
    EXPECT_TRUE(evidence::isValidProfileItemId(kDynItemPeakRms));
    EXPECT_TRUE(evidence::isValidProfileItemId(kDynItemProvenance));
    EXPECT_TRUE(evidence::isValidProfileItemId(kDynItemLoadCondition));
    EXPECT_TRUE(evidence::isValidProfileItemId(kDynItemPowerEnergy));
    EXPECT_TRUE(evidence::isValidProfileItemId(kDynItemForwardCheck));

    // Profile 实例分类与替代标志逐行核对。
    const evidence::RequiredEvidenceProfile profile = dynProfile();
    ASSERT_EQ(profile.required.size(), 4u);
    ASSERT_EQ(profile.suggested.size(), 2u);
    EXPECT_EQ(profile.required[2].itemClass, evidence::EvidenceItemClass::Required);
    EXPECT_EQ(profile.required[2].substitutableByInfeasibility, false);  // ③来源标记不豁免
    EXPECT_EQ(profile.required[3].itemClass, evidence::EvidenceItemClass::Required);
    EXPECT_EQ(profile.required[3].substitutableByInfeasibility, false);  // ④负载标识不豁免
    EXPECT_EQ(profile.suggested[0].itemClass, evidence::EvidenceItemClass::Suggested);
    EXPECT_EQ(profile.suggested[1].itemClass, evidence::EvidenceItemClass::Suggested);
}

// =====================================================================
// 用例 2：DYN-06 限定语 token 词表冻结与 trustQualifiers 纯函数契约
//   （reporting §6.4 词表 dynamics 侧产出三值——RPT-05 限定语不得弱化
//   的机器判读面；三层互斥呈现、层序＝§5.5 登记序）。
// =====================================================================

TEST(DynEvidenceBoundary, TrustQualifierTokensFrozenAndLayered_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06", "RPT-05"}, std::vector<std::string>{});
    // 本用例验证：限定语常量与 reporting §6.4 词表冻结值逐字一致（编译期
    // 钉扎）；trustQualifiers 对三层事实的映射逐条成立——frictionMissing→
    // data-insufficient、估算计数>0→estimated、externalValidationPending→
    // external-validation-incomplete；多事实并存时层序固定（缺失→估算→
    // 外部验证）；全干净→空清单（无降级事实不得捏造限定语——不弱化也
    // 不添加）。
    static_assert(kQualifierEstimated == "estimated", "限定语 token 冻结值漂移（reporting §6.4）");
    static_assert(kQualifierDataInsufficient == "data-insufficient",
                  "限定语 token 冻结值漂移（reporting §6.4）");
    static_assert(kQualifierExternalValidationIncomplete == "external-validation-incomplete",
                  "限定语 token 冻结值漂移（reporting §6.4）");

    // 全干净 → 空清单（精确结论不添限定语）。
    {
        DynamicsValidity clean;
        EXPECT_TRUE(trustQualifiers(clean).empty());
    }
    // 单层逐条（层内仅一项——互斥呈现的最小面）。
    {
        DynamicsValidity v;
        v.frictionMissing = true;
        const std::vector<std::string_view> q = trustQualifiers(v);
        ASSERT_EQ(q.size(), 1u);
        EXPECT_EQ(q[0], kQualifierDataInsufficient);
    }
    {
        DynamicsValidity v;
        v.estimatedLinkCount = 1;
        const std::vector<std::string_view> q = trustQualifiers(v);
        ASSERT_EQ(q.size(), 1u);
        EXPECT_EQ(q[0], kQualifierEstimated);
    }
    {
        DynamicsValidity v;
        v.externalValidationPending = true;
        const std::vector<std::string_view> q = trustQualifiers(v);
        ASSERT_EQ(q.size(), 1u);
        EXPECT_EQ(q[0], kQualifierExternalValidationIncomplete);
    }
    // 三层并存 → 层序固定（§5.5 登记序：缺失→估算→外部验证；确定性——
    // NFR-COR-02，消费方可按下标解读）。
    {
        DynamicsValidity v;
        v.frictionMissing = true;
        v.estimatedPayloadCount = 2;
        v.externalValidationPending = true;
        const std::vector<std::string_view> q = trustQualifiers(v);
        ASSERT_EQ(q.size(), 3u);
        EXPECT_EQ(q[0], kQualifierDataInsufficient);
        EXPECT_EQ(q[1], kQualifierEstimated);
        EXPECT_EQ(q[2], kQualifierExternalValidationIncomplete);
    }
}
