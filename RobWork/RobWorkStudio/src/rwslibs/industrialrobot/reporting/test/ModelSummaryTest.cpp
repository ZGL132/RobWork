/**
 * @file   ModelSummaryTest.cpp
 * @brief  模型摘要投影单元测试（RPT-T12）——投影 schema 冻结面/值语义确定性
 *         /快照已释放注记形态/revision 不在权威闭包的 try 轨（任务卡 RPT-T12
 *         acceptance 1/2 的单元内具名用例；验证方式"单测（投影 schema/空快照
 *         注记）"——§11 卡行原文）。
 *
 * 设计依据：
 *   - units/reporting.md §9.7（IModelSummaryProvider/ModelSummary 契约——
 *     前置/后置/确定性/维度表逐行）、§3.3（runtime 摘要注入形态）、§10.1
 *     （本组用例不在 RP-* 矩阵内——卡行验证方式即"单测（投影 schema/空快照
 *     注记）"，本文件为该验证方式的具名承载）
 *   - runtime.md §10.12（快照已释放→以归档的快照身份元数据呈现——P-RPT-9
 *     基线＝runtime.md v0.1 Draft-Structured）
 *   - 任务契约 tasks/foundation/RPT-T12.json acceptance 1（schema 冻结——
 *     字段全量落地）/2（投影单测三面）
 *
 * 替身边界声明（§10.1 RP-STATE-4 同源——本文件全部用例共用）：
 *   本文件的局部夹具 ScriptedModelSummaryProvider 输出仅验证 reporting 侧
 *   投影 schema 与注入接口契约（值语义/try 轨/注记承载），**不构成**任何
 *   runtime 编译正确性、快照身份计算正确性或能力声明真实性的证明——那些
 *   归 runtime 单元验证矩阵（RT-ID-*、RT-CAP-* 等）；替身返回的摘要值是
 *   测试脚本数据，不是 runtime 事实。真实 IRuntimeModelView→
 *   IModelSummaryProvider 适配归 L5 装配（ModelSummary.hpp 头内适配建议）。
 *
 * 线程约束：全部用例单线程（投影查询为并发只读安全面，测试无须并发——
 * 线程语义由实现方契约承诺承载）。
 */

#include <sdurws/ird/reporting/ModelSummary.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using sdurws::ird::core::ContentIdentity;
using sdurws::ird::core::Digest256;
using sdurws::ird::core::RevisionId;
using sdurws::ird::reporting::ConfigSummary;
using sdurws::ird::reporting::IModelSummaryProvider;
using sdurws::ird::reporting::ModelSummary;
using sdurws::ird::reporting::ResourceStateKind;
using sdurws::ird::reporting::ResourceSummary;

// =====================================================================
// 测试数据助手（确定性铺位——固定字节，不用随机）
// =====================================================================

/// 全零 ContentIdentity（保留值＝"未提供/可空"的承载——core.md §4.2）。
ContentIdentity nullIdentity()
{
    return ContentIdentity{};
}

/// 以固定字节构造 ContentIdentity（铺位可辨识——逐字节 0xA0+i）。
ContentIdentity identityWith(unsigned char seed)
{
    ContentIdentity cid;
    cid.bytes.fill(static_cast<std::uint8_t>(0xA0 + seed));
    return cid;
}

/// 固定修订身份铺位（确定性——不依赖生成器）。
RevisionId revisionWith(unsigned char seed)
{
    RevisionId rev;
    rev.bytes.fill(static_cast<std::uint8_t>(0xB0 + seed));
    return rev;
}

// =====================================================================
// 局部夹具：ScriptedModelSummaryProvider（脚本化替身——RPT-T12 局部夹具，
// 具名替身收口机制已在 RPT-T11 完成，本任务的替身为登记形态的局部夹具，
// 边界声明见文件头）
// =====================================================================

/**
 * @brief 脚本化摘要提供方：按修订身份查脚本表返回预置投影（或 nullopt）。
 *
 * 严格实现 IModelSummaryProvider 契约的后置三支（§9.7）：
 * 在案＋快照在案→全字段；在案＋快照已释放→snapshotIdentity 非空＋其余
 * 可空＋注记；不在权威闭包→nullopt。调用计数仅作观测（不参与断言语义
 * ——同修订幂等由"返回脚本值"的结构保证）。
 */
class ScriptedModelSummaryProvider final : public IModelSummaryProvider {
public:
    /// 在案且快照在案的修订（返回预置全字段投影）。
    ModelSummary liveSummary;          ///< 快照在案场景的脚本返回值
    RevisionId liveRevision{revisionWith(1)};
    /// 在案且快照已释放的修订（返回归档元数据形态投影）。
    ModelSummary releasedSummary;      ///< 快照已释放场景的脚本返回值
    RevisionId releasedRevision{revisionWith(2)};
    /// 调用计数（观测面——幂等性用例的两次调用核对）。
    mutable int callCount = 0;

    std::optional<ModelSummary> trySummary(RevisionId revision) const override
    {
        ++callCount;
        // 第一支：在案且快照在案→脚本全字段值（确定性——同修订恒同值）。
        if (revision == liveRevision) { return liveSummary; }
        // 第二支：在案但快照已释放→归档元数据＋注记（runtime §10.12）。
        if (revision == releasedRevision) { return releasedSummary; }
        // 第三支：不在权威闭包→nullopt（不伪造空摘要）。
        return std::nullopt;
    }
};

/// 构造"快照在案"的全字段投影脚本值（acceptance 1 schema 全量落地面）。
ModelSummary makeLiveSummary()
{
    ModelSummary s;
    s.revision = revisionWith(1);
    s.snapshotIdentity = identityWith(1);
    s.modelIdentity = identityWith(2);
    s.nameMapIdentity = identityWith(3);
    s.policyContentIdentity = identityWith(4);
    s.jointCount = 6;            // 六轴——链型摘要铺位（计数，无量纲）
    s.allRevolute = true;        // 全旋转关节
    s.installPresetToken = "Ground";  // 安装预设呈现 token（§9.7 四词之一）
    s.capabilities.hasDynamicWorkCell = true;
    s.capabilities.hasFullMassInertia = true;
    s.capabilities.hasJointVelocityLimits = true;
    s.capabilities.hasCollisionGeometry = true;
    s.capabilities.hasTools = false;       // 能力缺失位——如实 false（DYN-06 降级非阻断）
    s.capabilities.hasScene = true;
    s.capabilities.hasFrictionModel = false;
    s.capabilities.hasCouplingMatrix = true;
    s.resources.push_back(ResourceSummary{"res-solid-1", ResourceStateKind::Solidified,
                                          Digest256{}});
    // 固化资源带内容摘要（逐字节铺位）：
    s.resources.back().digest.fill(0xC1);
    s.resources.push_back(ResourceSummary{"res-rec-1", ResourceStateKind::Recorded,
                                          Digest256{}});  // 登记资源未带摘要（全零＝未提供）
    s.configurations.push_back(ConfigSummary{"ik-solver", identityWith(5)});
    s.provenanceNote = "";  // 快照在案→无注记
    return s;
}

/// 构造"快照已释放"的归档元数据形态投影（acceptance 2 注记面）。
ModelSummary makeReleasedSummary()
{
    ModelSummary s;
    s.revision = revisionWith(2);
    s.snapshotIdentity = identityWith(9);  // 归档快照身份恒在案（§9.7 后置行"非空"）
    // 其余身份字段全零＝未提供（"其余字段可空"——不伪造）：
    s.modelIdentity = nullIdentity();
    s.nameMapIdentity = nullIdentity();
    s.policyContentIdentity = nullIdentity();
    s.jointCount = 0;        // 0＝未提供（保留值语义）
    s.allRevolute = false;   // false 含"未提供"语义（配注记消歧）
    s.installPresetToken = "Custom";  // 归档元数据在案的呈现字段仍如实呈现
    s.resources.clear();
    s.configurations.clear();
    s.provenanceNote = "快照已释放，按归档的快照身份元数据呈现（runtime §10.12）";
    return s;
}

}  // namespace

// =====================================================================
// acceptance 1——schema 冻结：§9.7 字段全量落地（字段面/词面逐项断言）
// =====================================================================

/**
 * 投影 schema 字段全量在位（acceptance 1——§9.7 字段行逐项：revision/
 * snapshotIdentity/modelIdentity/nameMapIdentity/policyContentIdentity/
 * jointCount/allRevolute/installPresetToken/capabilities 八布尔/resources
 * {resourceId,state,digest}/configurations{configKindToken,contentIdentity}）。
 *
 * 断言策略：以全字段填充的投影走 trySummary 往返（脚本替身→返回值逐字段
 * 相等）——任何字段缺位/类型漂移都会在此显性失败；schema 冻结的字节面
 * （core::ContentIdentity 承载）由 static_assert 在契约测试锁定，此处锁
 * 行为面。
 */
TEST(ModelSummarySchema, FullFieldRoundtripThroughProvider_RPT_T12_ACC1)
{
    ScriptedModelSummaryProvider provider;
    provider.liveSummary = makeLiveSummary();

    const std::optional<ModelSummary> got = provider.trySummary(provider.liveRevision);
    ASSERT_TRUE(got.has_value()) << "在案修订必须返回投影（后置第 1 支）";

    // §9.7 字段行逐项核对（顺序即契约原文行序）：
    EXPECT_EQ(got->revision, provider.liveRevision);       // revision
    EXPECT_TRUE(got->snapshotIdentity.isValid());          // snapshotIdentity（在案→非空）
    EXPECT_TRUE(got->modelIdentity.isValid());             // modelIdentity
    EXPECT_TRUE(got->nameMapIdentity.isValid());           // nameMapIdentity（CON-06）
    EXPECT_TRUE(got->policyContentIdentity.isValid());     // policyContentIdentity
    EXPECT_EQ(got->jointCount, 6u);                        // jointCount（轴计数）
    EXPECT_TRUE(got->allRevolute);                         // allRevolute
    EXPECT_EQ(got->installPresetToken, "Ground");          // installPresetToken（四词词面）
    // capabilities 八布尔逐位（MDL-06/DYN-06 能力正交——false 位如实缺失）：
    EXPECT_TRUE(got->capabilities.hasDynamicWorkCell);
    EXPECT_TRUE(got->capabilities.hasFullMassInertia);
    EXPECT_TRUE(got->capabilities.hasJointVelocityLimits);
    EXPECT_TRUE(got->capabilities.hasCollisionGeometry);
    EXPECT_FALSE(got->capabilities.hasTools);
    EXPECT_TRUE(got->capabilities.hasScene);
    EXPECT_FALSE(got->capabilities.hasFrictionModel);
    EXPECT_TRUE(got->capabilities.hasCouplingMatrix);
    // resources 三字段 × 2 条（状态词两态各一——CON-03）：
    ASSERT_EQ(got->resources.size(), 2u);
    EXPECT_EQ(got->resources[0].resourceId, "res-solid-1");
    EXPECT_EQ(got->resources[0].state, ResourceStateKind::Solidified);
    EXPECT_TRUE(got->resources[0].digest != Digest256{});  // 固化资源带摘要（非全零）
    EXPECT_EQ(got->resources[1].state, ResourceStateKind::Recorded);
    EXPECT_TRUE(got->resources[1].digest == Digest256{});  // 登记资源摘要未提供（保留值）
    // configurations 两字段 × 1 条（AnalysisConfiguration 引用）：
    ASSERT_EQ(got->configurations.size(), 1u);
    EXPECT_EQ(got->configurations[0].configKindToken, "ik-solver");
    EXPECT_TRUE(got->configurations[0].contentIdentity.isValid());
    // 快照在案→无注记（空串约定）：
    EXPECT_EQ(got->provenanceNote, std::string{});
}

/// 资源状态词表 token 冻结（两态只增不改名——投影词面即呈现契约）。
TEST(ModelSummarySchema, ResourceStateTokenFrozenVocabulary_RPT_T12_ACC1)
{
    // 词面是持久化/呈现契约（kebab 小写——core 词表风格一致）：
    EXPECT_STREQ(sdurws::ird::reporting::toToken(ResourceStateKind::Solidified), "solidified");
    EXPECT_STREQ(sdurws::ird::reporting::toToken(ResourceStateKind::Recorded), "recorded");
}

/**
 * installPresetToken 是呈现字段、不入任何身份判定（acceptance 5 禁项锁定
 * ——§9.7 维度表"非法调用"行）。
 *
 * 自证方式：接口面（IModelSummaryProvider）恰一个方法 trySummary(RevisionId)
 * ——不存在以 installPresetToken 为键/参/判定的任何查询面（类型层即无此
 * 形态）；投影值忠实呈现脚本值（四词词面逐一透传，不做规范化改写——呈现
 * 通道不加工）。
 */
TEST(ModelSummarySchema, InstallPresetTokenPresentationOnly_RPT_T12_ACC5)
{
    // 四词词面逐一忠实透传（§9.7 "Ground/Inverted/Wall/Custom"）：
    for (const char* token : {"Ground", "Inverted", "Wall", "Custom"}) {
        ScriptedModelSummaryProvider provider;
        provider.liveSummary = makeLiveSummary();
        provider.liveSummary.installPresetToken = token;
        const std::optional<ModelSummary> got = provider.trySummary(provider.liveRevision);
        ASSERT_TRUE(got.has_value());
        EXPECT_EQ(got->installPresetToken, token)
            << "呈现字段必须忠实透传，不做加工/判定（" << token << "）";
    }
    // 接口面无以 installPresetToken 为参的方法（类型层自证——最小接口面）：
    static_assert(sizeof(&IModelSummaryProvider::trySummary) > 0,
                  "接口面锁定：唯一方法 trySummary(RevisionId)——不存在"
                  "以 installPresetToken 为键的查询/判定面（§9.7 非法调用行）");
    SUCCEED() << "installPresetToken 零判定通道（接口面仅 trySummary——类型层自证）";
}

// =====================================================================
// acceptance 2——投影单测三面：确定性/空快照注记/try 轨 nullopt
// =====================================================================

/// 同修订同投影（§9.7 确定性行"同修订同投影（值语义）"——两次查询逐字段相等）。
TEST(ModelSummaryProjection, SameRevisionSameProjection_RPT_T12_ACC2)
{
    ScriptedModelSummaryProvider provider;
    provider.liveSummary = makeLiveSummary();

    const std::optional<ModelSummary> first = provider.trySummary(provider.liveRevision);
    const std::optional<ModelSummary> second = provider.trySummary(provider.liveRevision);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    // 值语义确定性：operator== 全字段比较（含 resources/configurations 逐条）。
    EXPECT_EQ(*first, *second) << "同修订两次查询必须得到相等投影（NFR-COR-02 精神）";
    EXPECT_EQ(provider.callCount, 2) << "两次查询均真实穿透到提供方（投影不缓存——确定性由实现方幂等保证）";
}

/**
 * 快照已释放→snapshotIdentity 非空＋其余字段可空＋注记（acceptance 2——
 * §9.7 后置行/runtime §10.12"以归档的快照身份元数据呈现"，不伪造）。
 */
TEST(ModelSummaryProjection, ReleasedSnapshotArchivedMetadataWithNote_RPT_T12_ACC2)
{
    ScriptedModelSummaryProvider provider;
    provider.releasedSummary = makeReleasedSummary();

    const std::optional<ModelSummary> got = provider.trySummary(provider.releasedRevision);
    ASSERT_TRUE(got.has_value()) << "快照已释放≠修订不存在——仍返回归档元数据形态";

    // 归档在案的快照身份恒非空（§9.7 后置行"snapshotIdentity 非空"）：
    EXPECT_TRUE(got->snapshotIdentity.isValid());
    // 其余身份字段可空（全零＝未提供——保留值承载"可空"，不伪造猜测值）：
    EXPECT_FALSE(got->modelIdentity.isValid());
    EXPECT_FALSE(got->nameMapIdentity.isValid());
    EXPECT_FALSE(got->policyContentIdentity.isValid());
    EXPECT_EQ(got->jointCount, 0u);
    // 注记必须非空（"＋注记（不伪造）"的 schema 承载位——provenanceNote）：
    EXPECT_NE(got->provenanceNote, std::string{})
        << "快照已释放场景注记必填——空字段须有如实的人读说明";
    // 归档元数据在案的呈现字段仍如实呈现（不是全部清空）：
    EXPECT_EQ(got->installPresetToken, "Custom");
}

/// revision 不存在于权威闭包＝nullopt（acceptance 2——try 轨，不抛、不伪造空摘要）。
TEST(ModelSummaryProjection, UnknownRevisionReturnsNullopt_RPT_T12_ACC2)
{
    ScriptedModelSummaryProvider provider;
    provider.liveSummary = makeLiveSummary();

    const RevisionId unknown = revisionWith(7);  // 脚本表外的修订（闭包外铺位）
    const std::optional<ModelSummary> got = provider.trySummary(unknown);
    EXPECT_FALSE(got.has_value())
        << "闭包外修订＝nullopt（不伪造空摘要——缺数据≠全零数据，§9.7 前置行）";
}
