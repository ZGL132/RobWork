/**
 * @file   PolicyProofMaterialContractTest.cpp
 * @brief  跨单元契约套件（POL-T11）——碰撞发现→evidence 证明素材的
 *         字段形状对齐（POL-EVAL-3 契约夹具）与 POL-COMPAT-2 的 evidence
 *         联动素材面（本侧供身份＋原因素材）。
 *
 * 设计依据：
 *   - units/policy.md §11 POL-EVAL-3 行（"任务点构型（必经）碰撞发现→
 *     映射为 evidence proof.collisionPairs（契约夹具）→对象 ID 对＋判定＋
 *     采样位置字段齐备可被 validateProof 采信——契约测试字段对齐断言"）、
 *     POL-COMPAT-2 行（"内容身份变化拒复用——本侧供身份＋原因素材"）、
 *     §8.4（findings→证明字段映射——policy 供素材、判定归 evidence）；
 *   - units/evidence.md §6.3（DeterministicInfeasibilityProof/
 *     CollisionPair/MandatoryStateDescriptor 契约——validateProof 校验
 *     清单的 policy 侧预检锚）；§3.4（_contract_test＝跨单元契约面）；
 *   - 任务契约 tasks/foundation/POL-T11.json（§12 产物行"契约夹具
 *     （evidence 证明字段形状对齐）"）。
 *
 * 依赖面声明（R-2/R-1 合规——本文件的对齐机制）：
 *   - 本测试目标对 evidence **零链接边**（T-1 白名单：_contract_test 只可
 *     链同单元产品目标＋testkit＋gtest）；对 evidence 公共头
 *     （Evidence.hpp/Errors.hpp 的值类型）的**头级消费**＝R-2 允许形态
 *     （只允许 include 其他单元公共头）——§3.4 明文设计本测试目标承担
 *     "与 evidence 证明字段形状的对齐断言"；产品目标 sdurws_ird_policy
 *     的链接面不变（§3 依赖表"policy→evidence 文档级对齐（零编译依赖）"
 *     的登记口径不触碰——该口径约束产品链接面，本对齐是其测试侧观测点）；
 *   - validateProof 的**实现**在 evidence 产品库（非 header-only），本测试
 *     不调用之；对齐断言＝(a) evidence 值类型上的字段级映射构建＋
 *     validateProof 校验清单（ProofIssueCode 五段检查序）的逐条预检；
 *     (b) 源码级锚（evidence 头成员名/清单符号在案——任一侧字段漂移
 *     即本套件显性失败）。"可被 validateProof 采信"的真值判定面归
 *     evidence 侧联动测试（evidence 单元对 validateProof 的既有覆盖）。
 *
 * 模式约束：集成模式专属（消费 CollisionQuery.hpp 的 rw include 面）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Compatibility.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <sdurws/ird/evidence/Evidence.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/policy/testdouble/PolicyTestDoubles.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sdurws::ird::policy;
using namespace sdurws::ird::policy::testdoubles;

/// core/evidence/testkit 契约类型短别名。
namespace core = sdurws::ird::core;
namespace evi = sdurws::ird::evidence;

namespace {

/// 手工构造的有效对象身份/内容身份（装置同款工具）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 16/32 字节身份→小写十六进制（源码锚与断言消息的文本形态）。
template <typename ByteContainer>
std::string idToHex(const ByteContainer& bytes)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// 文件全文读取（源码锚扫描的输入）。
std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 子串存在性（源码锚扫描原语）。
bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// =====================================================================
// 映射原语（§8.4 findings→证明素材的 policy 侧组装面——消费方视角）。
// =====================================================================

/**
 * @brief 碰撞发现→evidence CollisionPair 的映射（§8.4 对齐点——对象 ID
 *        对＋判定；A≠B 与判定为真的纪律在预检清单中钉住）。
 *
 * 字段对应（契约夹具的声明面——与 expected 声明逐条对照）：
 *   finding.objectA → CollisionPair.objectIdA（规范序 A<B——评估侧已保证）
 *   finding.objectB → CollisionPair.objectIdB
 *   finding.kind==Collision → inCollision=true（MarginViolation 的"间距
 *   不足非碰撞"不进入碰撞证明素材——§6.3 碰撞主张的证据清单语义）
 *   finding.sampleIndex → 映射记录的采样位置（CollisionPair 无该字段——
 *   采样位置由消费方组装证据明细时承载，§8.1 表 2④"碰撞证据明细"）。
 */
evi::CollisionPair findingToCollisionPair(const CollisionFinding& finding)
{
    evi::CollisionPair pair;
    pair.objectIdA = finding.objectA;
    pair.objectIdB = finding.objectB;
    pair.inCollision = (finding.kind == CollisionFindingKind::Collision);
    return pair;
}

// ---- validateProof 校验清单的 policy 侧预检（§6.3 五段检查序的镜像）----
// 依赖面声明：本组谓词是 ProofIssueCode 清单（evidence/Errors.hpp 枚举
// 注释）在消费侧的预检投影——evidence 侧对 validateProof 本体的字段级
// 行为已有全量覆盖（EV-VER 组）；此处断言"policy 供给的素材满足清单"，
// 镜像与实现的漂移由下方源码锚用例钉住（清单符号变更即显性失败）。

/// CollisionPair 字段纪律（CollisionPairInvalid 的三个触发面）。
bool collisionPairFieldValid(const evi::CollisionPair& pair)
{
    return pair.objectIdA.isValid()            // id 保留值拒绝
        && pair.objectIdB.isValid()
        && !(pair.objectIdA == pair.objectIdB) // A≠B（自身不成对）
        && pair.inCollision;                   // 判定为否不支撑碰撞证明
}

/// MandatoryStateDescriptor 三要素（EV-VER-6 反例面的正向面）。
bool mandatoryStateFieldValid(const evi::MandatoryStateDescriptor& state)
{
    return !state.stateKind.empty()               // 种类 token 非空
        && state.stateKind.find('\0') == std::string::npos
        && !state.nonSelectabilityBasis.empty()   // 必经性依据非空
        && state.objectId.isValid();              // 所属对象非保留值
}

/// 证明素材的清单级预检（validateProof 五段检查序——素材满足则清单空）。
std::vector<std::string> proofMaterialIssues(const evi::DeterministicInfeasibilityProof& proof)
{
    std::vector<std::string> issues;
    // ① 类别合法（三类词表——CategoryInvalid 触发面）。
    if (proof.category != evi::ProofCategory::MandatoryStateCollision) {
        issues.push_back("category-invalid");
    }
    // ② 类别条件字段齐备（MandatoryStateCollision→三要素＋collisionPairs）。
    if (!mandatoryStateFieldValid(proof.mandatoryState)) {
        issues.push_back("mandatory-state-fields");
    }
    if (proof.collisionPairs.empty()) {
        issues.push_back("collision-pairs-missing");
    }
    for (const auto& pair : proof.collisionPairs) {
        if (!collisionPairFieldValid(pair)) {
            issues.push_back("collision-pair-invalid");
        }
    }
    // ③ 产生者（词形＋已注册＋契约版本相符——producer 侧素材）。
    if (proof.producer.empty() || proof.producerContractVersion == 0) {
        issues.push_back("producer-material");
    }
    // ④ 快照/切片绑定（非空身份——绑定核对归 evidence 汇总侧）。
    if (!proof.snapshotId.isValid()) {
        issues.push_back("snapshot-id-missing");
    }
    if (!proof.sliceId.isValid()) {
        issues.push_back("slice-id-missing");
    }
    // ⑤ 覆盖声明（类别规范值——kCoverageClaimMandatoryState）。
    if (proof.coverageClaim != evi::kCoverageClaimMandatoryState) {
        issues.push_back("coverage-claim");
    }
    return issues;
}

}  // namespace

// =====================================================================
// 用例组 1：POL-EVAL-3——发现携带齐备的证明素材字段（契约夹具面）。
// =====================================================================

/**
 * POL-EVAL-3（§8.1 表 2③/C8）：任务点构型（必经）碰撞发现→映射为
 * evidence proof.collisionPairs——对象 ID 对＋判定＋采样位置字段齐备，
 * validateProof 清单预检零问题（"可被 validateProof 采信"的素材面）。
 */
TEST(PolicyProofMaterialContract, FindingCarriesCompleteProofMaterialFields)
{
    IRD_TEST_INFO("ARC-05", {"AT-03"}, std::nullopt);
    // 装置：最小场景（对象身份打标）＋脚本化评估器产出一条必经构型发现
    // （POL-TD-1 边界内——本用例验证的是字段形状映射，不是几何真值）。
    const core::ObjectId toolBox = taggedObjectId(0x21);
    const core::ObjectId envBox = taggedObjectId(0x22);

    CollisionFinding finding;
    finding.objectA = toolBox;              // 规范序第一端（A<B）
    finding.objectB = envBox;
    finding.runtimeNameA = "Tool";
    finding.runtimeNameB = "EnvBox";
    finding.sampleIndex = 3;                // 采样位置（序列下标，0 基）
    finding.pathParameter = std::nullopt;   // SingleState 恒空
    finding.kind = CollisionFindingKind::Collision;
    finding.measuredClearance = std::nullopt;
    finding.penetrationDepth = std::nullopt;
    finding.level = PolicyRuleLevel::Must;

    // ---- 映射构建：发现→CollisionPair（对象 ID 对＋判定）----
    const evi::CollisionPair pair = findingToCollisionPair(finding);
    EXPECT_EQ(pair.objectIdA, toolBox);
    EXPECT_EQ(pair.objectIdB, envBox);
    EXPECT_TRUE(pair.inCollision) << "Collision 类发现的映射判定须为真";

    // ---- 采样位置字段齐备（CollisionPair 无该字段——由映射记录承载，
    //      消费方组装证据明细时消费 §8.1 表 2④）----
    EXPECT_EQ(finding.sampleIndex, 3u);

    // ---- 证明素材组装＋validateProof 清单预检零问题 ----
    evi::DeterministicInfeasibilityProof proof;
    proof.category = evi::ProofCategory::MandatoryStateCollision;
    proof.claimToken = "policy.mandatory-state-collision";
    proof.subject = taggedObjectId(0x40);            // 任务点对象
    proof.mandatoryState.stateKind = "TaskPointConfig";
    proof.mandatoryState.nonSelectabilityBasis =
        "任务点构型是任务定义强制的末端状态，无替代选择";
    proof.mandatoryState.objectId = taggedObjectId(0x40);
    proof.collisionPairs.push_back(pair);
    proof.preconditions = "快照 S 的碰撞几何与策略 P（内容身份绑定）";
    proof.coverageClaim = std::string(evi::kCoverageClaimMandatoryState);
    proof.snapshotId = [] {
        core::ContentIdentity cid;
        cid.bytes[0] = 0x51;
        return cid;
    }();
    proof.sliceId = [] {
        evi::SliceId sid;
        sid.bytes[0] = 0x52;
        return sid;
    }();
    proof.producer = "kin-batch-ik";       // 产生者评估键（已注册——素材面）
    proof.producerContractVersion = 1;

    const std::vector<std::string> issues = proofMaterialIssues(proof);
    EXPECT_TRUE(issues.empty())
        << "证明素材应通过 validateProof 清单全部预检，实际问题: "
        << (issues.empty() ? std::string{"-"} : issues.front());
}

/**
 * 反例面（字段纪律的失败方向——素材缺项必须被预检清单拦截）：判定为否
 * 的对（inCollision=false）与 A==B 的对不得进入碰撞证明素材。
 */
TEST(PolicyProofMaterialContract, NonCollisionOrSelfPairRejectedByChecklist)
{
    IRD_TEST_INFO("ARC-05", {}, std::nullopt);
    const core::ObjectId toolBox = taggedObjectId(0x21);

    // 反例 1：间距不足发现（MarginViolation）映射的判定为否——不支撑碰撞证明。
    CollisionFinding marginFinding;
    marginFinding.objectA = toolBox;
    marginFinding.objectB = taggedObjectId(0x22);
    marginFinding.kind = CollisionFindingKind::SafetyMarginViolation;
    const evi::CollisionPair marginPair = findingToCollisionPair(marginFinding);
    EXPECT_FALSE(marginPair.inCollision);
    EXPECT_FALSE(collisionPairFieldValid(marginPair))
        << "判定为否的对不得通过清单预检（不伪造碰撞）";

    // 反例 2：A==B（自身与自身不成对）。
    evi::CollisionPair selfPair;
    selfPair.objectIdA = toolBox;
    selfPair.objectIdB = toolBox;
    selfPair.inCollision = true;
    EXPECT_FALSE(collisionPairFieldValid(selfPair)) << "A==B 不构成对象对";

    // 反例 3：必经状态描述缺必经性依据（EV-VER-6 反例方向的素材面）。
    evi::MandatoryStateDescriptor incomplete;
    incomplete.stateKind = "TaskPointConfig";
    incomplete.nonSelectabilityBasis = "";   // 缺失
    incomplete.objectId = taggedObjectId(0x40);
    EXPECT_FALSE(mandatoryStateFieldValid(incomplete));
}

// =====================================================================
// 用例组 2：源码级字段锚——两侧字段漂移的机检防线。
// =====================================================================

/**
 * 源码锚（字段形状对齐的漂移检测）：evidence/Evidence.hpp 的
 * CollisionPair 成员名与 policy/CollisionQuery.hpp 的发现字段名在案——
 * 任一侧字段重命名/删除即本用例显性失败（契约测试的字段对齐断言在
 * 结构变更时不可静默失明）。
 */
TEST(PolicyProofMaterialContract, SourceAnchorsPinBothFieldShapes)
{
    IRD_TEST_INFO("ARC-05", {}, std::nullopt);
    // 锚点根：IRD_POLICY_UNIT_ROOT 的上一级＝industrialrobot/（CMake 注入
    // 与 _test 目标同源——runtime/evidence 单元头路径由此拼出）。
    const std::filesystem::path unitRoot{IRD_POLICY_UNIT_ROOT};

    // ---- evidence 侧锚：CollisionPair/MandatoryStateDescriptor 成员与
    //      validateProof 清单符号（成员名文本在案）----
    const std::string evidenceHeader =
        readAll(unitRoot / "evidence" / "include" / "sdurws" / "ird" / "evidence"
                / "Evidence.hpp");
    ASSERT_FALSE(evidenceHeader.empty()) << "Evidence.hpp 不可读（锚点路径失效）";
    EXPECT_TRUE(contains(evidenceHeader, "struct CollisionPair"))
        << "evidence 侧 CollisionPair 结构在案（字段锚的前提）";
    EXPECT_TRUE(contains(evidenceHeader, "objectIdA")) << "CollisionPair 字段锚：objectIdA";
    EXPECT_TRUE(contains(evidenceHeader, "objectIdB")) << "CollisionPair 字段锚：objectIdB";
    EXPECT_TRUE(contains(evidenceHeader, "inCollision")) << "CollisionPair 字段锚：inCollision";
    EXPECT_TRUE(contains(evidenceHeader, "struct MandatoryStateDescriptor"))
        << "evidence 侧必经状态描述结构在案";
    EXPECT_TRUE(contains(evidenceHeader, "nonSelectabilityBasis"))
        << "MandatoryStateDescriptor 字段锚：nonSelectabilityBasis";
    EXPECT_TRUE(contains(evidenceHeader, "kCoverageClaimMandatoryState"))
        << "validateProof 清单符号锚：覆盖声明规范值";

    // ---- policy 侧锚：CollisionFinding 的素材字段（映射源）----
    const std::string policyHeader =
        readAll(unitRoot / "policy" / "include" / "sdurws" / "ird" / "policy"
                / "CollisionQuery.hpp");
    ASSERT_FALSE(policyHeader.empty()) << "CollisionQuery.hpp 不可读（锚点路径失效）";
    EXPECT_TRUE(contains(policyHeader, "struct CollisionFinding"))
        << "policy 侧发现结构在案";
    EXPECT_TRUE(contains(policyHeader, "core::ObjectId objectA")) << "发现字段锚：objectA";
    EXPECT_TRUE(contains(policyHeader, "core::ObjectId objectB")) << "发现字段锚：objectB";
    EXPECT_TRUE(contains(policyHeader, "std::size_t sampleIndex")) << "发现字段锚：sampleIndex";
}

// =====================================================================
// 用例组 3：POL-COMPAT-2——内容身份变化拒复用的本侧素材面。
// =====================================================================

/**
 * POL-COMPAT-2（CON-05/06——与 evidence judgeCacheHit 契约测试联动）：
 * 策略阈值变化→新内容身份（切片不命中的身份素材）；兼容判定给出
 * 数值契约锚失配原因（evidence/execution 侧拒复用的原因素材）。本侧
 * 素材三件＝{新身份, 旧身份, 判定原因}——evidence 侧 judgeCacheHit 消费
 * 这些素材做切片失效判定（联动面，本组钉住素材的供给形状）。
 */
TEST(PolicyProofMaterialContract, ContentIdentityChangeSuppliesSliceInvalidationMaterial)
{
    IRD_TEST_INFO("CON-05", {}, std::nullopt);
    // 双策略：同对象同语义布局、仅安全间距 0.01→0.02（内容身份必不同）。
    ScriptedValidationContext closure;
    closure.existingObjects[taggedObjectId(0x21)] = true;
    closure.roles[taggedObjectId(0x21)] = "RobotLink";
    const auto makeInput = [&closure](double clearance) {
        RawPolicyInput in;
        in.policyObject = taggedObjectId(0x20);
        in.collision.enabled = true;
        in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                       CollisionDomain::Tool};
        in.collision.safetyClearance = RawThresholdInput{clearance, "m"};
        in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
        in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
        return in;
    };
    const PolicyParseResult oldResult = resolvePolicy(makeInput(0.01), closure);
    const PolicyParseResult newResult = resolvePolicy(makeInput(0.02), closure);
    ASSERT_TRUE(oldResult.policy.has_value());
    ASSERT_TRUE(newResult.policy.has_value());
    const EngineeringPolicySet& oldPolicy = *oldResult.policy;
    const EngineeringPolicySet& newPolicy = *newResult.policy;

    // 素材①②：新旧内容身份不同（切片不命中的身份判据——CON-05）。
    EXPECT_FALSE(oldPolicy.contentIdentity == newPolicy.contentIdentity)
        << "阈值变化必须产生新内容身份（失效矩阵 policy 行）";

    // 素材③：兼容判定原因（消费方按旧锚核对新策略→数值契约锚失配；
    // 本侧把锚差异显式供给——判定归消费方，policy 供素材与判定原语）。
    PolicyConsumerRequirements requirements;   // 缺省＝最宽松（只触发锚核对——显式给锚）
    requirements.requiresNumericContract = oldPolicy.numericContractAnchor;
    // 同锚（装置策略同用缺省锚）→ 兼容；身份变化的拒复用素材由①②承载，
    // 锚核对通道由 checkPolicyCompatibility 的 reason 词表供给（七值词表
    // 形状锚——evidence/execution 侧消费的 reasonToken 稳定串）。
    const PolicyCompatibility sameAnchor =
        checkPolicyCompatibility(newPolicy, requirements);
    EXPECT_TRUE(sameAnchor.reasons.empty()) << "同锚策略兼容（素材面不含失配原因）";

    requirements.requiresNumericContract = "appendixD@v99";
    const PolicyCompatibility anchorMismatch =
        checkPolicyCompatibility(newPolicy, requirements);
    ASSERT_FALSE(anchorMismatch.reasons.empty());
    EXPECT_EQ(anchorMismatch.reasons.front(),
              PolicyIncompatibilityReason::NumericContractMismatch)
        << "锚失配原因＝numeric-contract-mismatch（拒复用原因素材的词表面）";
}
