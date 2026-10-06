/**
 * @file   EvaluatorContractTest.cpp
 * @brief  传动映射评估器契约用例组（DtMappingEvalContract）——③端口注册
 *         闭环（真实 EvidenceProfileRegistry＋EvaluatorRegistry：Profile
 *         注册在前、评估器注册在后的接入序）、descriptor 字段落值、切片
 *         依赖声明契约、端到端切片评估（解码→核心映射→payload/证据装配）、
 *         派发违约 fail-fast 与数据类降级（任务契约 WP-18-T03 acceptance 1
 *         的③端口面自证）。
 *
 * 设计依据：
 *   - units/drivetrain.md §13.10（③端口注册面）、§12.1/§12.2（依赖键提议
 *     契约与消费通道）、§12.4（能力声明）、§4.2 contract_test 行（业务对端
 *     契约＝evidence 评估器注册/切片依赖声明 fake 套件——随 WP-18-T03 落地）
 *   - evidence 冻结契约（Evaluator.hpp §9.4/§9.5 注册行为；Slice.hpp 切片
 *     形态；Evidence.hpp Profile 注册与证据项）——全部断言针对真实产品
 *     代码（evidence 注册表不桩化）
 *   - 测试替身边界声明：TestObjectContext（宿主 tryObjectBytes 替身——
 *     按物化锚约定返回编码字节）与 NoCancelContext（取消语义替身）；
 *     注册闭环与评估输出全部走真实实现
 *
 * ★ 评估键词形/profileId 词表偏差（DTB §5.4，见 Evaluator.hpp/Codec.hpp
 *   尾注）：评估键取 "dt-mapping"（evidence isValidEvaluationKey 不含点）；
 *   Profile 绑定消费域 "sel"（isDomainProfileId 五域词表封闭）——本组用例
 *   同时把这两条偏差作为契约钉住（词形闸门用例）。
 */

#include <sdurws/ird/drivetrain/Codec.hpp>
#include <sdurws/ird/drivetrain/DiagCodes.hpp>
#include <sdurws/ird/drivetrain/Evaluator.hpp>
#include <sdurws/ird/drivetrain/MappingTypes.hpp>
#include <sdurws/ird/drivetrain/Series.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dt = sdurws::ird::drivetrain;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;

namespace {

// =====================================================================
// 测试工具（id 派生＋黄金模型/序列＋宿主替身）
// =====================================================================

/// 从固定种子派生 16 字节强类型 id（测试值——确定性，非随机）。
template <typename Id>
Id idFrom(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize();
    Id id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.bytes[i] = digest[i];
    }
    return id;
}

dt::DriveTrainIdentity testIdentity()
{
    dt::DriveTrainIdentity id;
    id.drivetrainObjectCv.bytes = idFrom<core::ContentVersion>("dt-ct-config-v1").bytes;
    id.algorithmVersion = 1;
    id.contractVersion = 1;
    return id;
}

/// 黄金模型（单轴对角——端到端面的最小完备形态）。
dt::DriveTrainModel goldenModel()
{
    dt::JointDriveAxis joint;
    joint.jointId = idFrom<core::ObjectId>("dt-ct-joint-0");
    joint.kind = dt::JointKind::Revolute;
    joint.localName = "joint1";
    dt::MotorDriveAxis motor;
    motor.motorId = idFrom<core::ObjectId>("dt-ct-motor-0");
    motor.jointIndex = 0;
    dt::TransmissionRatio ratio;
    ratio.c = 0.01;
    ratio.source = dt::SourcedValueTag::ModelingField;
    dt::EfficiencyModel eff;
    eff.etaForward = 0.9;
    eff.etaBackward = 0.7;
    dt::RotorInertiaModel rot;
    rot.rotorInertia = 1e-4;
    return dt::makeDiagonalDriveTrainModel({joint}, {motor}, {ratio}, {0.0},
                                           testIdentity(), {eff}, {rot});
}

/// 黄金序列（两样本匀速——统计面成立的最小循环）。
dt::JointSeriesView goldenSeries()
{
    dt::JointSeriesView s;
    s.jointIds = {idFrom<core::ObjectId>("dt-ct-joint-0")};
    s.caseId = idFrom<core::ObjectId>("dt-ct-case");
    s.upstreamSliceId.bytes = idFrom<core::ContentIdentity>("dt-ct-upstream").bytes;
    for (int i = 0; i < 2; ++i) {
        dt::JointDriveSample sample;
        sample.t = static_cast<double>(i); // s
        sample.qd = 1.0;                   // rad/s（匀速；q̈=0）
        sample.tauJoint = 2.0;             // N·m
        sample.segmentId = "seg-ct";
        s.samples.push_back(sample);
    }
    return s;
}

/// 上游切片身份（黄金序列的归属——物化锚派生输入）。
core::ContentIdentity upstreamSlice()
{
    core::ContentIdentity id;
    id.bytes = idFrom<core::ContentIdentity>("dt-ct-upstream-slice").bytes;
    return id;
}

/// 宿主上下文替身：按"物化空间"返回对象字节（模型 id 直查；序列按
/// jointSeriesAnchor 锚查——组装方约定，见 Codec.hpp）。无取消。
/// 物化空间为线性扫描的 id→字节表（对象数为个位数——测试规模）。
class TestObjectContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}

    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId objectId, core::ContentVersion) const override
    {
        // 物化空间直查（id 精确匹配——不校验版本：版本一致性由切片条目
        // 锚定，替身语义最小化）。
        for (const auto& entry : m_objects) {
            if (entry.first == objectId) {
                return entry.second;
            }
        }
        return std::nullopt;
    }

    /// 物化一个对象（组装方动作的替身）。
    void putObject(core::ObjectId id, std::vector<std::uint8_t> bytes)
    {
        m_objects.emplace_back(std::move(id), std::move(bytes));
    }

private:
    std::vector<std::pair<core::ObjectId, std::vector<std::uint8_t>>> m_objects;
};

}  // namespace

// =====================================================================
// descriptor 字段落值（卡 §13.10 行——偏差登记两则的契约钉住面）
// =====================================================================

TEST(DtMappingEvalContract, DescriptorMatchesSection1310_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "SEL-05"},
                  std::vector<std::string>{});

    const evidence::EvaluatorDescriptor d = dt::makeMappingDescriptor();

    // 评估键（kebab 词形——evidence isValidEvaluationKey 闸门；偏差登记：
    // 卡面 "dt.mapping" 记法与词形闸门冲突，按③端口所有者词形权威执行）。
    EXPECT_EQ(d.key, std::string(dt::kMappingEvaluationKey));
    EXPECT_EQ(d.key, "dt-mapping");
    EXPECT_TRUE(evidence::isValidEvaluationKey(d.key));
    // 含点记法必须被词形闸门拒绝（偏差钉住——防未来"改回点形态"回退）。
    EXPECT_FALSE(evidence::isValidEvaluationKey("dt.mapping"));

    // 契约版本（>0——CON-04；进 sliceId）。
    EXPECT_EQ(d.contractVersion, dt::kMappingContractVersion);
    EXPECT_GT(d.contractVersion, 0U);

    // 依赖声明（两条 Required——§12.1 提议键 P-DT-6）。
    ASSERT_EQ(d.inputs.size(), 2U);
    EXPECT_EQ(d.inputs[0].key, std::string(dt::kModelDrivetrainKey));
    EXPECT_EQ(d.inputs[0].kind, evidence::DependencyKind::Object);
    EXPECT_EQ(d.inputs[0].requiredness, evidence::DependencyRequiredness::Required);
    EXPECT_EQ(d.inputs[1].key, std::string(dt::kJointSeriesKey));
    EXPECT_EQ(d.inputs[1].kind, evidence::DependencyKind::UpstreamResult);
    EXPECT_EQ(d.inputs[1].requiredness, evidence::DependencyRequiredness::Required);

    // Profile 绑定（消费域 sel——五域词表封闭的偏差钉住；contentIdentity
    // 保留值＝域不可申报——§9.5 R-3）。
    EXPECT_EQ(d.profile.profileId, "sel");
    EXPECT_EQ(d.profile.version, std::string(dt::kMappingProfileVersion));
    EXPECT_FALSE(d.profile.contentIdentity.isValid());

    // 模式/实例语义（D-DT-14）。
    ASSERT_EQ(d.supportedModes.size(), 2U);
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Verified);
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.threadSafety, evidence::ThreadSafety::SingleThread);
}

// =====================================================================
// ③端口注册闭环（真实注册表——Profile 先行、评估器在后；manifest）
// =====================================================================

TEST(DtMappingEvalContract, RegistryRegistrationClosesLoop_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "SEL-05"},
                  std::vector<std::string>{"AT-08"});

    // ---- Profile 注册在前（§13 接入序）：sel 域最小必需项——选型域
    // "每组合电机侧工作点"（itemId 词形 "<域>.<项>"，无适用条件）。
    evidence::EvidenceProfileRegistry profiles;
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = std::string(dt::kMappingProfileId);
    profile.version = std::string(dt::kMappingProfileVersion);
    evidence::EvidenceProfileItem item;
    item.itemId = "sel.motor-op-point";
    item.itemClass = evidence::EvidenceItemClass::Required;
    item.description = "每组合电机侧工作点——DriveTrainMappingEvaluator 同一口径"
                       "（需求 §8.1 表 4 选型域必需项的 R1 承载）";
    profile.required.push_back(item);
    profiles.registerProfile(profile);
    // 注册表计算的权威内容身份（域不可申报）。
    const evidence::RequiredEvidenceProfile* stored
        = profiles.findProfile("sel", std::string(dt::kMappingProfileVersion));
    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->contentIdentity.isValid());

    // ---- 评估器注册在后（真实 EvaluatorRegistry——注册期验证全链）。
    evidence::EvaluatorRegistry registry(profiles);
    auto factory = std::make_unique<dt::DriveTrainMappingEvaluatorFactory>();
    registry.registerEvaluator(std::move(factory), {});

    // 注册成功＝descriptor 全部字段落值通过 §9.4 注册期验证（键词形/版本/
    // 模式集/线程安全/Profile 解析/声明闭包）。
    const evidence::IEvaluatorFactory* found = registry.find(dt::kMappingEvaluationKey);
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(registry.isRegistered(dt::kMappingEvaluationKey));
    EXPECT_TRUE(registry.contractVersionMatches(dt::kMappingEvaluationKey,
                                                dt::kMappingContractVersion));

    // manifest 摘要（§9.4 主/worker 一致性——键字典序＋权威 Profile 身份）。
    const evidence::RegistrationManifest manifest = registry.manifest();
    ASSERT_EQ(manifest.entries.size(), 1U);
    EXPECT_EQ(manifest.entries[0].key, std::string(dt::kMappingEvaluationKey));
    EXPECT_EQ(manifest.entries[0].contractVersion, dt::kMappingContractVersion);
    EXPECT_EQ(manifest.entries[0].profileIdentity, stored->contentIdentity);

    // 工厂 create 非空（§9.3 工厂契约）。
    auto instance = registry.create(dt::kMappingEvaluationKey);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->descriptor().key, std::string(dt::kMappingEvaluationKey));
}

// =====================================================================
// 端到端切片评估（解码→核心→payload/证据/诊断装配）
// =====================================================================

namespace {

/// 组装端到端请求（切片条目＝Object 模型＋UpstreamResult 序列引用；
/// **不物化对象**——物化是独立步骤 materialize，供"字节不可得"场景区分）。
evidence::EvaluationRequest makeRequest(const dt::DriveTrainModel& model,
                                        const dt::JointSeriesView& series)
{
    evidence::EvaluationRequest req;
    req.mode = core::EvaluationMode::Verified;
    req.task.project = idFrom<core::ProjectId>("dt-ct-prj");
    req.task.branch = idFrom<core::BranchId>("dt-ct-brn");
    req.task.revision = idFrom<core::RevisionId>("dt-ct-rev");
    req.task.run = idFrom<core::RunId>("dt-ct-run");
    req.task.attempt.value = 1U;

    // 切片（直接构造——SliceBuilder 冻结协议由 evidence 自测覆盖，本测试
    // 只需条目形态正确；评估键/契约版本按登记值）。
    req.slice.evaluationKey = std::string(dt::kMappingEvaluationKey);
    req.slice.evaluatorContractVersion = dt::kMappingContractVersion;

    evidence::DependencyEntry modelEntry;
    modelEntry.key = std::string(dt::kModelDrivetrainKey);
    modelEntry.kind = evidence::DependencyKind::Object;
    evidence::ObjectDependencyPayload modelPayload;
    modelPayload.objectId = model.motorAxes[0].motorId; // 模型物化 id（替身约定）
    modelPayload.contentVersion = model.identity.drivetrainObjectCv;
    modelPayload.objectTypeToken = "model.drivetrain";
    modelEntry.payload = modelPayload;
    req.slice.entries.push_back(modelEntry);

    evidence::DependencyEntry seriesEntry;
    seriesEntry.key = std::string(dt::kJointSeriesKey);
    seriesEntry.kind = evidence::DependencyKind::UpstreamResult;
    evidence::UpstreamResultDependencyPayload seriesRef;
    seriesRef.upstreamKey = std::string(dt::kJointSeriesKey);
    seriesRef.upstreamSliceId = series.upstreamSliceId;
    seriesEntry.payload = seriesRef;
    req.slice.entries.push_back(seriesEntry);

    req.snapshot.snapshotId.bytes = idFrom<core::ContentIdentity>("dt-ct-snap").bytes;
    req.slice.sliceId.bytes = idFrom<core::ContentIdentity>("dt-ct-slice").bytes;
    return req;
}

/// 组装方物化动作（替身）：模型字节挂模型对象 id；序列字节按物化锚约定
/// 物化（jointSeriesAnchor——Codec.hpp）。
void materialize(TestObjectContext& context, const dt::DriveTrainModel& model,
                 const dt::JointSeriesView& series)
{
    context.putObject(model.motorAxes[0].motorId, dt::encodeDriveTrainModel(model));
    context.putObject(dt::jointSeriesAnchor(series.upstreamSliceId),
                      dt::encodeJointSeries(series));
}

/// 无取消宿主替身（TestObjectContext 的别名语义——命名区分用途）。
using HostContext = TestObjectContext;

}  // namespace

TEST(DtMappingEvalContract, EndToEndSliceEvaluation_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{"AT-07"});

    const dt::DriveTrainModel model = goldenModel();
    dt::JointSeriesView series = goldenSeries();
    series.upstreamSliceId = upstreamSlice();

    HostContext context;
    evidence::EvaluationRequest req = makeRequest(model, series);
    materialize(context, model, series); // 组装方物化（模型＋序列锚对象）

    dt::DriveTrainMappingEvaluator evaluator;
    evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // 评估成功：无诊断、payload 就位（token＝域登记值；digest 与字节一致
    // ——CR-02 完整性凭据，消费方可重算比对）。
    EXPECT_TRUE(out.diagnostics.empty());
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_EQ(out.payload->kindToken, std::string(dt::kMappingPayloadToken));

    // payload 解码回读＝与核心直调同算法产出（③端口形态与注入形态同一
    // 算法——D-DT-2 的端到端证据）。
    const dt::DriveTrainMappingOutput decoded
        = dt::decodeMappingOutput(out.payload->canonicalBytes);
    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput direct = coreImpl.evaluate(model, series, nullptr);
    EXPECT_EQ(decoded, direct);

    // 工作点数值锚点（c=0.01、τ=2 N·m、q̇=1 rad/s、q̈=0）：
    // τ_ideal=0.02；θ̇=100 rad/s；P_trans=2/0.9。
    ASSERT_EQ(decoded.points.size(), 1U);
    EXPECT_NEAR(decoded.motorSeries[0].samples[0].tauIdeal, 0.02, 1e-12);
    EXPECT_NEAR(decoded.motorSeries[0].samples[0].thetaDot, 100.0, 1e-9);

    // 证据项（sel 域工作点项——Satisfied＋摘要与 payload 一致）。
    ASSERT_EQ(out.evidence.size(), 1U);
    EXPECT_EQ(out.evidence[0].itemId, "sel.motor-op-point");
    EXPECT_EQ(out.evidence[0].status, evidence::EvidenceItemStatus::Satisfied);
    ASSERT_TRUE(out.evidence[0].artifactDigest.has_value());
    EXPECT_EQ(*out.evidence[0].artifactDigest, out.payload->digest.bytes);
}

// =====================================================================
// 派发违约与数据类降级（错误轨两分法——Evaluator.hpp 文件头约定）
// =====================================================================

TEST(DtMappingEvalContract, DispatchAndDataErrorTracks_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    const dt::DriveTrainModel model = goldenModel();
    dt::JointSeriesView series = goldenSeries();
    series.upstreamSliceId = upstreamSlice();

    // ---- 派发违约：切片评估键与登记值不符＝调用方契约违约 fail-fast。
    {
        HostContext context;
        evidence::EvaluationRequest req = makeRequest(model, series);
        req.slice.evaluationKey = "kin-batch-ik"; // 错键（词形合法但不匹配）
        dt::DriveTrainMappingEvaluator evaluator;
        EXPECT_THROW((void)evaluator.evaluate(req, context), std::invalid_argument);
    }
    // 契约版本不符同轨。
    {
        HostContext context;
        evidence::EvaluationRequest req = makeRequest(model, series);
        req.slice.evaluatorContractVersion = 99;
        dt::DriveTrainMappingEvaluator evaluator;
        EXPECT_THROW((void)evaluator.evaluate(req, context), std::invalid_argument);
    }
    // ---- 必需条目缺失：第二道防线 fail-fast（注册闭包为第一道）。
    {
        HostContext context;
        evidence::EvaluationRequest req = makeRequest(model, series);
        req.slice.entries.pop_back(); // 丢弃 UpstreamResult 条目
        dt::DriveTrainMappingEvaluator evaluator;
        EXPECT_THROW((void)evaluator.evaluate(req, context), std::invalid_argument);
    }
    // ---- 模型字节不可得：数据类——返回诊断＋空 payload（不抛、不伪造）。
    {
        HostContext context; // 空物化空间——模型对象读不到
        evidence::EvaluationRequest req = makeRequest(model, series);
        dt::DriveTrainMappingEvaluator evaluator;
        evidence::EvaluationOutput out = evaluator.evaluate(req, context);
        EXPECT_FALSE(out.payload.has_value());
        // 完整性素材经诊断通道表达（EvaluationOutput 无 completeness 字段
        // ——诊断非空＋payload 不产出即"不完整"的契约形态）。
        EXPECT_FALSE(out.diagnostics.empty());
        ASSERT_FALSE(out.diagnostics.empty());
        EXPECT_EQ(out.diagnostics.front().code, dt::kDtInputSampleMissing);
    }
    // ---- 序列字节不可得：数据类同轨。
    {
        HostContext context;
        evidence::EvaluationRequest req = makeRequest(model, series);
        // 只物化模型、不物化序列锚对象。
        context.putObject(model.motorAxes[0].motorId, dt::encodeDriveTrainModel(model));
        dt::DriveTrainMappingEvaluator evaluator;
        evidence::EvaluationOutput out = evaluator.evaluate(req, context);
        EXPECT_FALSE(out.payload.has_value());
        ASSERT_FALSE(out.diagnostics.empty());
        EXPECT_EQ(out.diagnostics.front().code, dt::kDtInputSampleMissing);
    }
}
