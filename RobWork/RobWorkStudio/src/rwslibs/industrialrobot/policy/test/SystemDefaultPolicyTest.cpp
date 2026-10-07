/**
 * @file   SystemDefaultPolicyTest.cpp
 * @brief  系统缺省策略集与其 ④ 端口供给器用例组（UI-T73/O-46 裁决出路
 *         ②-scope）——发布门构造、唯一冻结默认构成、保留身份应答与
 *         解析半区 fail-fast 面。
 *
 * 设计依据：
 *   - units/policy.md §4.4（唯一冻结默认＝附录 D 第 11 项 4π，origin=
 *     DefaultAppendixD）、§4.2（PolicyOriginKind::SystemDefault 词表）、
 *     §4.5（发布门）、§9.1（错误矩阵）
 *   - 需求 ARC-05（策略单一权威、唯一默认）；P-POL-2/O-10 保守口径
 *     （未裁决阈值 nullopt＝显式不适用——本套件钉住「不发明数值」）
 *   - 任务契约 tasks/foundation/UI-T73.json（F-536 宿主装配裁决落地）
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/裁决编号（ARC-05/P-POL-2/
 * F-536 等），正文断言处注明验证的条款。
 *
 * 范围声明：本套件覆盖 SystemDefaultPolicy 面的域内契约（工厂构成＋供给
 * 器三方法）。宿主装配后的端到端行程校验行为（apply 提交翻转）由
 * modeling-tour 真机通道承载（builds/ui-t73/ 留痕）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>
#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>

#include <limits>
#include <string>

namespace {

using namespace sdurws::ird;
using namespace sdurws::ird::policy;

/// 附录 D 唯一冻结默认的期望值（4π——与 kDefaultFiniteRotationTravelLimit
/// 同源的测试面独立写法：double π 字面量×4，IEEE754 逐位一致）。
constexpr double kExpectedTravelLimit = 4.0 * 3.141592653589793;

}  // namespace

// =====================================================================
// 工厂构成面：makeSystemDefaultPolicySet（发布门产出＋冻结默认构成）
// =====================================================================

/// 发布门产出＋构成面：Valid 态＋行程上限 4π（DefaultAppendixD）＋其余
/// 阈值 nullopt（显式不适用——P-POL-2 不发明数值）＋碰撞 disabled＋
/// SystemDefault 来源＋保留身份（ARC-05/O-46）。
TEST(SystemDefaultPolicy, FactoryPublishesAppendixDDefaults_F536_O46)
{
    const EngineeringPolicySet set = makeSystemDefaultPolicySet();

    // 发布门形态（§4.5——工厂只产出 Valid 实例）。
    EXPECT_EQ(set.validationState, PolicyValidationState::Valid);
    EXPECT_TRUE(set.policyObject.isValid());
    EXPECT_TRUE(set.contentIdentity.isValid());
    EXPECT_EQ(set.schemaVersion, kPolicySchemaVersionCurrent);

    // 构成面：行程上限＝唯一冻结默认（值字段＋DefaultAppendixD 来源）。
    EXPECT_DOUBLE_EQ(set.jointThresholds.finiteRotationTravelLimit.siValue(),
                     kExpectedTravelLimit)
        << "行程上限偏离附录 D 第 11 项冻结默认（4π rad）";
    EXPECT_EQ(set.jointThresholds.finiteRotationTravelLimit.origin(),
              PolicyValueOrigin::DefaultAppendixD);
    EXPECT_TRUE(set.jointThresholds.travelLimitCheckEnabled)
        << "④行程校验开关被关闭（缺省源必须执行 MDL-06④）";

    // P-POL-2 面：未裁决阈值保持 nullopt（显式不适用——不发明数值）。
    EXPECT_FALSE(set.jointThresholds.nearLimitRatio.has_value())
        << "近限位比出现非冻结数值（P-POL-2 违约）";
    EXPECT_FALSE(set.jointThresholds.conditionNumberWarning.has_value())
        << "条件数警告出现非冻结数值（P-POL-2 违约）";

    // 碰撞面：disabled（碰撞策略无冻结默认——显式不适用）。
    EXPECT_FALSE(set.collision.enabled) << "缺省集不得启用碰撞检查（无冻结间距）";

    // 来源与适用范围：SystemDefault＋空集（全部适用）。
    EXPECT_EQ(set.origin.kind, PolicyOriginKind::SystemDefault);
    EXPECT_TRUE(set.applicability.modes.empty());
    EXPECT_TRUE(set.applicability.modelObjects.empty());

    // 保留身份：policyObject＝systemDefaultPolicyObjectId（非项目对象锚）。
    EXPECT_EQ(set.policyObject, systemDefaultPolicyObjectId());
}

/// 确定性面：同 schema 常量下两次构造的语义闭包恒同（contentIdentity
/// 逐字段相等——CON-05/06；4π 的 IEEE754 逐位一致口径）。
TEST(SystemDefaultPolicy, FactoryDeterministicContentIdentity_NFR_COR_02)
{
    const EngineeringPolicySet a = makeSystemDefaultPolicySet();
    const EngineeringPolicySet b = makeSystemDefaultPolicySet();
    EXPECT_EQ(a.contentIdentity, b.contentIdentity) << "同闭包两次构造身份漂移";
}

// =====================================================================
// 供给器面：SystemDefaultPolicyProvider 三方法
// =====================================================================

/// 解析半区：保留身份应答缺省集（任意 expectedVersion——编译期常量无版
/// 本演进，契约差异面登记于 SystemDefaultPolicy.hpp）。
TEST(SystemDefaultPolicy, ProviderServesDefaultForWellKnownId_F536)
{
    SystemDefaultPolicyProvider provider;
    PolicyResolutionRequest request;
    request.policyObject = systemDefaultPolicyObjectId();
    // 期望版本非空亦应答（差异面——存储背书形态才有版本演进语义；版本
    // 值取固定规范文本——系统缺省集不消费该字段，任意合法值即可）。
    request.expectedVersion = core::ContentVersion::fromCanonical(
        std::string(64, 'a').insert(0, "cv-"));

    const PolicyResolution resolution = provider.resolvePolicy(request);
    ASSERT_TRUE(resolution.policy.has_value()) << "保留身份应答失败（缺省源断链）";
    EXPECT_EQ(resolution.policy->policyObject, systemDefaultPolicyObjectId());
    EXPECT_EQ(resolution.policy->jointThresholds.finiteRotationTravelLimit.siValue(),
              kExpectedTravelLimit);
    EXPECT_TRUE(resolution.diagnostics.empty())
        << "成功应答不得携带诊断（§9.1 不变式——policy 非空时仅 Info 级）";
}

/// 解析半区：非保留身份→空 policy＋POLICY-OBJECT-MISSING（错误矩阵第 3
/// 格同码面；诊断 subject 绑请求对象——ERR-01）。
TEST(SystemDefaultPolicy, ProviderReportsMissingForForeignId_ERR_01)
{
    SystemDefaultPolicyProvider provider;
    PolicyResolutionRequest request;
    request.policyObject = core::ObjectId::generate();

    const PolicyResolution resolution = provider.resolvePolicy(request);
    EXPECT_FALSE(resolution.policy.has_value()) << "外来身份不得伪造应答";
    ASSERT_FALSE(resolution.diagnostics.empty())
        << "空 policy 时诊断必非空（§9.1 不变式）";
    EXPECT_EQ(resolution.diagnostics.front().code, "POLICY-OBJECT-MISSING");
    ASSERT_TRUE(resolution.diagnostics.front().subject.has_value());
    EXPECT_EQ(resolution.diagnostics.front().subject.value(), request.policyObject);
}

/// 解析半区：全零保留身份→fail-fast（请求契约第 1 项——不静默转诊断）。
TEST(SystemDefaultPolicy, ProviderFailsFastOnZeroId_ERR_01)
{
    SystemDefaultPolicyProvider provider;
    PolicyResolutionRequest request;
    request.policyObject = core::ObjectId{};  // 全零保留值
    EXPECT_THROW(provider.resolvePolicy(request), PolicyError);
}

/// 评估半区：collisionEvaluator/collisionBackend 调用即 fail-fast（解析
/// 半区专用——PortAssemblyIncomplete，PolicyPort.hpp 装配契约同码面）。
TEST(SystemDefaultPolicy, ProviderFailsFastOnCollisionHalf_PORT_ASSEMBLY)
{
    SystemDefaultPolicyProvider provider;
    EXPECT_THROW(provider.collisionEvaluator(), PolicyError);
    EXPECT_THROW(provider.collisionBackend(), PolicyError);
}
