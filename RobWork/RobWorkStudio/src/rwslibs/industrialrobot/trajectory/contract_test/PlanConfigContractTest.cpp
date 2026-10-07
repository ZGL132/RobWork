/**
 * @file   PlanConfigContractTest.cpp
 * @brief  config.trj 配置条目与 evidence::ConfigEntry 的跨单元契约用例组
 *         （TrjPlanConfigContract）——§5.5 身份路径"canonical 编码（magic
 *         IRDCFGTR1）→configDigest＝SHA-256→切片 config.trj Configuration
 *         条目→进 sliceId"的执行面自证（任务契约 WP-16-T04 acceptance 2
 *         ——路径数据模型与身份绑定与 §7 一致）。
 *
 * 设计依据：
 *   - units/trajectory.md §5.5（身份路径原文；D-04"求解配置改变不改变
 *     样本基准"——trajectory 仅 sliceId）、§14.2.1（descriptor 依赖行
 *     config.trj(Configuration,Required)）
 *   - units/evidence.md §4.1.2（ConfigEntry——配置引用的不透明 canonical
 *     承载；SnapshotBuilder 冻结期一致性校验：contentIdentity 必须等于
 *     对 canonicalBytes 的 SHA-256）
 *   - 需求 CON-05（内容寻址）、AT-27（求解配置分层——配置进 sliceId 断言
 *     的数据基础）、AT-28 同源（内容身份一致性）
 *   - 先例：kinematics contract/test 面（WP-15-T10 makeConfigurationRefEntry
 *     消费同款 evidence::ConfigEntry 形态——跨单元公共值类型协作经七条
 *     登记边，R-1/R-2 合规）
 *
 * 契约面（对端＝evidence 公共值类型，非对端内部状态）：本组只断言
 * trajectory 侧产出的 ConfigEntry **满足 evidence 冻结期校验的输入契约**
 * ——条目三要素（kindToken/canonicalBytes/contentIdentity）与一致性；
 * evidence 侧的快照组装/校验行为归 evidence 单元自身测试。
 */

#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/trajectory/PlanConfig.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::ContentIdentity;
using sdurws::ird::core::ObjectId;
using sdurws::ird::trajectory::DwellPolicy;
using sdurws::ird::trajectory::StartStateKind;
using sdurws::ird::trajectory::TrajectoryPlanConfigCodec;
using sdurws::ird::trajectory::TrajectoryPlanConfiguration;
using sdurws::ird::trajectory::kConfigTrjKindToken;
using sdurws::ird::trajectory::kTimeParamMethodQuinticSplineC2;
using sdurws::ird::trajectory::makeTrajectoryPlanConfigRefEntry;
using sdurws::ird::trajectory::trajectoryPlanConfigurationDigest;

namespace {

/// 全字段合法配置（与单元测试基线同构——跨组独立自持，不共享 fixture）。
TrajectoryPlanConfiguration makeConfig()
{
    TrajectoryPlanConfiguration c;
    c.startStateKind = StartStateKind::TaskPoint;
    c.startTaskPoint = ObjectId::fromCanonical(
        "obj-0000000000000000000000000000a001");
    c.plannerFamilyToken = "rrt-connect";
    c.plannerParams = {{"timeout-s", "2.0"}};
    c.planningSeed = 42ULL;
    c.cartesianSampleStep = 0.01;                          // m
    c.ikContinuityThreshold = 1e-6;                        // rad|m 逐轴
    c.smoothToleranceJoint = 1e-3;                         // rad
    c.smoothToleranceTcp = 1e-3;                           // m
    c.timeParamMethod = kTimeParamMethodQuinticSplineC2;
    c.limitsScaleFactor = 1.0;
    c.dwellPolicy = DwellPolicy::HonorEvents;
    return c;
}

}  // namespace

/** 条目三要素契约：kindToken=="config.trj"、canonicalBytes 非空、
 *  contentIdentity==对 canonicalBytes 的 SHA-256（evidence SnapshotBuilder
 *  冻结期一致性校验的输入契约——CON-05 内容寻址）。 */
TEST(TrjPlanConfigContract, EntryMatchesSnapshotFreezeContract_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"}, std::vector<std::string>{"AT-27"});
    const auto entry = makeTrajectoryPlanConfigRefEntry(makeConfig());

    // kindToken：evidence 卡 §4.1.2"配置种类 token（归域；非空＋无 NUL）"
    // ＋trajectory 卡 §5.5/§14.2.1 登记值 config.trj。
    EXPECT_EQ(entry.configKindToken, std::string("config.trj"));
    EXPECT_EQ(entry.configKindToken, std::string(kConfigTrjKindToken));

    // canonicalBytes 非空且可被本域解码（不透明承载的字节面仍受本域
    // codec 管辖——解码回配置＝字节完整性自证）。
    ASSERT_FALSE(entry.canonicalBytes.empty());
    const auto decoded = TrajectoryPlanConfigCodec{}.decode(entry.canonicalBytes);
    EXPECT_EQ(decoded, makeConfig());

    // contentIdentity＝对字节的 SHA-256（独立重算对照——快照冻结期校验
    // 将执行同一计算，此处预先钉死一致性）。
    ContentDigester digester;
    digester.update(entry.canonicalBytes.data(), entry.canonicalBytes.size());
    const ContentIdentity expected{digester.finalize()};
    EXPECT_TRUE(entry.contentIdentity == expected);
    EXPECT_TRUE(entry.contentIdentity == trajectoryPlanConfigurationDigest(makeConfig()));
    EXPECT_TRUE(entry.contentIdentity.isValid());   // 非全零（CON-06 非空纪律）
}

/** 身份敏感契约：配置任一变化⇒条目身份变化（缓存键 config.trj 条目→
 *  sliceId 失效重算的数据基础——AT-27/CON-05）。 */
TEST(TrjPlanConfigContract, EntryIdentitySensitiveToConfigChange_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "AT-27"}, std::vector<std::string>{});
    const auto base = makeTrajectoryPlanConfigRefEntry(makeConfig());

    auto changed = makeConfig();
    changed.planningSeed = 43ULL;                       // 种子变化（进身份字段）
    const auto other = makeTrajectoryPlanConfigRefEntry(changed);

    EXPECT_NE(base.contentIdentity, other.contentIdentity);
    EXPECT_FALSE(base.canonicalBytes == other.canonicalBytes);
}

/** decode 侧违约不产条目：非法配置在 makeTrajectoryPlanConfigRefEntry
 *  入口被拒（fail-fast——非法配置无身份可言，NFR-COR-03）。 */
TEST(TrjPlanConfigContract, EntryRejectedForIllegalConfig_WP16T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});
    auto bad = makeConfig();
    bad.limitsScaleFactor = 2.0;                        // ∉(0,1]——保守缩放违约
    EXPECT_THROW(makeTrajectoryPlanConfigRefEntry(bad),
                 sdurws::ird::trajectory::TrajectoryError);
}
