/**
 * @file   CommandFixtures.hpp
 * @brief  WP-13-T08 命令/就绪测试共享夹具（test/ 内部——project/test/
 *         StubHandlers.hpp 同款先例：测试域替身不入公共面）。
 *
 * 内容：
 *   - 值模型构造辅助（关节/连杆/工具/设计——合法实例，经得起 Codec 解码
 *     门 I-MDL 全量复核）；
 *   - policy 侧替身：TestNameContext（名称映射表——ARC-04 不猜测）、
 *     TestPolicyProvider（④端口替身——返回构造好的 EngineeringPolicySet
 *     并观测 resolvePolicy 调用）；
 *   - project 侧替身：TestQueryPort（②端口内存实现——基线闭包字节表，
 *     数据形状与 project 公共契约一致）；MockCompilePort（双编译端口
 *     观测桩——调用计数＋故障注入＋应答诊断）。
 *
 * 设计依据：units/modeling.md §9.3/§9.4.4、units/policy.md §9.4、
 * units/project.md §5.2/§5.3/§5.3.6、§6.6（CompileRequest 装配形态——
 * V-20 接缝测试的编排面）；任务契约 WP-13-T08 acceptance 1～5。
 *
 * 线程安全：替身均单线程用例内使用（命令槽语义——串行）。
 */

#ifndef IRD_MODELING_TEST_COMMANDFIXTURES_HPP
#define IRD_MODELING_TEST_COMMANDFIXTURES_HPP

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/modeling/Codec.hpp>       // RobotDesignCodec（对象编码——载荷槽/基线字节）
#include <sdurws/ird/modeling/CommandHandlers.hpp>
#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/modeling/Readiness.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/policy/JointLimits.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::modeling::testfixture {

// π 字面量（测试内独立抄写——期望值不引实现常量，TemplateTest 同款纪律）。
inline constexpr double kPi = 3.141592653589793;

// ---------------------------------------------------------------------
// 值模型构造辅助（全部产出解码门可过的合法实例）
// ---------------------------------------------------------------------

/// 测试用对象身份（随机生成——排序断言以同次运行内相对序为准）。
inline core::ObjectId makeOid()
{
    return core::ObjectId::generate();
}

/// 用户输入来源标记（methodTag 语法 [a-z0-9./_-]——"test-fixture" 合规）。
inline core::ValueProvenance userProvenance()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 非全零内容身份/内容版本（发布门与存储键仅核对 isValid——测试不模拟
/// 真实摘要；语义身份真实性归 POL-T03/T04 与 project 对象库）。
inline core::ContentIdentity testContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}
inline core::ContentVersion testContentVersion(std::uint8_t tag)
{
    core::ContentVersion cv;
    cv.bytes[0] = tag;
    return cv;
}

/// 旋转关节（Explicit 权威：axis/origin/bounds 全 Provided——合法实例）。
inline JointEntry makeRevoluteJoint(const core::ObjectId& oid, const std::string& name,
                                    double lowerRad, double upperRad)
{
    JointEntry joint;
    joint.objectId = oid;
    joint.localName = name;
    joint.type = JointType::Revolute;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 1.0), userProvenance());
    joint.origin = core::SourcedValue<JointPose>::provided(JointPose{}, userProvenance());
    joint.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{lowerRad, upperRad}, userProvenance());
    return joint;
}

/// continuous 关节（工程工作范围可选——"未确认"面测试用）。
inline JointEntry makeContinuousJoint(const core::ObjectId& oid, const std::string& name,
                                      bool withConfirmedRange)
{
    JointEntry joint;
    joint.objectId = oid;
    joint.localName = name;
    joint.type = JointType::Continuous;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 1.0), userProvenance());
    joint.origin = core::SourcedValue<JointPose>::provided(JointPose{}, userProvenance());
    if (withConfirmedRange) {
        joint.workingRange = core::SourcedValue<JointLimits>::provided(
            JointLimits{-kPi, kPi}, userProvenance());
    }
    return joint;
}

/// 连杆（物性可空——缺失走 DataInsufficient 预告面，不触发断言）。
inline LinkEntry makeLink(const core::ObjectId& oid, const std::string& name)
{
    LinkEntry link;
    link.objectId = oid;
    link.localName = name;
    return link;
}

/// 最小合法根对象：1 个旋转关节＋2 连杆（I-MDL-1 计数关系）。
inline RobotDesign makeSingleJointDesign(double lowerRad, double upperRad)
{
    RobotDesign design;
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1", lowerRad, upperRad));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    return design;
}

/// 对象变体编码（canonical 字节——载荷槽/基线字节表共用）。
inline std::vector<std::uint8_t> encodeVariant(const ObjectVariant& object)
{
    RobotDesignCodec codec;
    auto encoded = codec.encode(object, kCurrentFormatVersion);
    if (!encoded.ok()) {
        throw std::logic_error(std::string("fixture: 对象编码失败: ")
                               + encoded.error().detail);
    }
    return encoded.get();
}

// ---------------------------------------------------------------------
// policy 侧替身
// ---------------------------------------------------------------------

/// 构造已发布策略（碰撞关闭——发布门最小合法实例；行程阈值可配置；
/// nearLimitRatio/conditionNumber 未设置＝显式不适用——P-POL-2）。
inline policy::EngineeringPolicySet makePolicy(double travelLimitRad)
{
    return policy::EngineeringPolicySet::make(
        makeOid(),
        policy::kPolicySchemaVersionCurrent,
        testContentIdentity(0xAB),
        policy::CollisionRules::make(false, {}, std::nullopt, true, {}, {}),
        policy::JointThresholds::make(std::nullopt, policy::PolicyValueOrigin::Explicit,
                                      std::nullopt, policy::PolicyValueOrigin::Explicit,
                                      travelLimitRad,
                                      policy::PolicyValueOrigin::Explicit),
        policy::PolicyApplicability{},
        policy::PolicyOrigin{policy::PolicyOriginKind::Template, std::nullopt,
                             std::nullopt},
        policy::PolicyValidationState::Valid);
}

/**
 * @brief 名称映射替身（对象→运行时名应答表；缺席返回 nullopt——ARC-04
 *        不猜测的替身侧契约；R-4：modeling 消费注入面，不自建映射）。
 */
class TestNameContext final : public policy::IPolicyNameContext {
public:
    std::map<core::ObjectId, std::string> byId;  ///< 应答表（用例装配）

    std::optional<core::ObjectId> tryObjectId(const std::string&) const override
    {
        return std::nullopt;  // 正向解析本套件不消费
    }
    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        auto it = byId.find(object);
        if (it == byId.end()) { return std::nullopt; }
        return it->second;
    }
    core::ContentIdentity nameMapContentIdentity() const override
    {
        return testContentIdentity(0x11);
    }
};

/**
 * @brief ④策略端口替身：返回构造好的策略并观测 resolvePolicy 调用
 *        （阈值唯一来源口径的测试面——策略由用例装配，处理器不得自持
 *        阈值常量；policyToReturn 为空＝解析失败面——L11 诊断传导）。
 */
class TestPolicyProvider final : public policy::IPolicyProvider {
public:
    std::optional<policy::EngineeringPolicySet> policyToReturn;  ///< 应答策略
    mutable int resolveCalls = 0;                                ///< 调用观测

    policy::PolicyResolution resolvePolicy(
        const policy::PolicyResolutionRequest&) const override
    {
        resolveCalls++;
        policy::PolicyResolution result;
        // emplace（EngineeringPolicySet 持 const 成员——不可拷贝赋值，
        // optional 的赋值通道不可用；就地构造兼容）。
        if (policyToReturn.has_value()) { result.policy.emplace(*policyToReturn); }
        return result;  // nullopt→空 policy（L11 解析失败面）
    }
    policy::ICollisionEvaluator& collisionEvaluator() const override
    {
        throw std::logic_error("fixture: 碰撞评估器不在本套件装配面");
    }
    policy::CollisionBackendDescriptor collisionBackend() const override
    {
        throw std::logic_error("fixture: 碰撞后端不在本套件装配面");
    }
};

// ---------------------------------------------------------------------
// project 侧替身
// ---------------------------------------------------------------------

/**
 * @brief ②查询端口内存替身：基线闭包（RevisionView 值快照＋对象字节表）。
 *
 * 数据形状与 project 公共契约一致（ObjectRef/RevisionView——PRJ-T10
 * 落地面）；prepare 的基线重建经 object() 取数（强语义——字节表缺项即
 * 抛，与真实存储的引用缺失语义同向）。
 */
class TestQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view;  ///< head()/revision()/tryRevision 应答值
    /// 对象字节表（键＝"<oid 规范文本>|<cv 规范文本>"）。
    std::map<std::string, std::vector<std::uint8_t>> objects;

    /// 登记一个闭包对象（引用进 view.objectRefs＋字节入表——一次装配）。
    void addObject(const core::ObjectId& oid, std::string token,
                   const std::vector<std::uint8_t>& bytes)
    {
        const core::ContentVersion cv = testContentVersion(
            static_cast<std::uint8_t>(view.objectRefs.size() + 1u));
        project::ObjectRef ref;
        ref.objectId = oid;
        ref.contentVersion = cv;
        ref.objectTypeToken = std::move(token);
        ref.digest256 = std::string(64, '0');  // 引用面摘要（本替身不复核）
        view.objectRefs.push_back(std::move(ref));
        objects[oid.toCanonical() + "|" + cv.toCanonical()] = bytes;
    }

    [[nodiscard]] project::RevisionView head() const override { return view; }
    [[nodiscard]] std::optional<project::RevisionView> tryRevision(
        core::RevisionId id) const override
    {
        if (id == view.id) { return view; }
        return std::nullopt;
    }
    [[nodiscard]] project::RevisionView revision(core::RevisionId) const override
    {
        return view;
    }
    [[nodiscard]] project::ProjectMetadataView currentMetadata() const override
    {
        return {};
    }
    [[nodiscard]] std::optional<project::ProjectMetadataView> metadataAt(
        core::RevisionId) const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<project::BranchTip> branchTips() const override
    {
        return {};
    }
    [[nodiscard]] std::vector<project::RevisionView> branchHistory(
        core::BranchId, std::uint32_t) const override
    {
        return {};
    }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId oid, core::ContentVersion cv) const noexcept override
    {
        auto it = objects.find(oid.toCanonical() + "|" + cv.toCanonical());
        if (it == objects.end()) { return std::nullopt; }
        return it->second;
    }
    [[nodiscard]] std::vector<std::uint8_t> object(core::ObjectId oid,
                                                   core::ContentVersion cv) const override
    {
        auto bytes = tryObject(oid, cv);
        if (!bytes.has_value()) {
            throw std::logic_error("fixture: 对象字节缺失（引用断裂）");
        }
        return *bytes;
    }
    [[nodiscard]] std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        return {};
    }
    [[nodiscard]] std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        return {};
    }
    [[nodiscard]] std::filesystem::path runDir(core::RunId) const override
    {
        return {};
    }
};

/**
 * @brief 双编译端口观测桩（§5.3.6"阶段 A 以测试桩实现验证编排"的建模侧
 *        消费面）：调用计数（V-19 未被调用断言）＋故障注入（V-20 编译
 *        失败面）＋计划闭包观测（S5 CompileRequest 数据流验证）。
 */
class MockCompilePort final : public project::IModelCompilePort {
public:
    mutable int callCount = 0;                                ///< 调用计数
    bool failWithRtDiagnostic = false;                        ///< 故障注入开关
    std::vector<project::ObjectWrite> lastRequestWrites;      ///< 末次请求的计划闭包观测

    project::CompileResult compileWorkCellAndDwc(
        const project::CompileRequest& request) override
    {
        callCount++;
        lastRequestWrites = request.plannedWrites;
        project::CompileResult result;
        if (failWithRtDiagnostic) {
            result.ok = false;
            // RT-* 稳定码诊断（句法合法；编译失败定位面——码值登记权威归
            // runtime/registry，本替身仅承载记录形态）。
            result.diagnostics.push_back(core::DiagnosticRecord::make(
                std::string("RT-COMPILE-FAILED"), std::nullopt, std::nullopt,
                std::nullopt, "双编译失败注入（测试）", "WorkCell 编译失败（故障注入）",
                "检查模型输入"));
        } else {
            result.ok = true;
        }
        return result;
    }
};

/// 基线闭包装配（空闭包＝r0 前——首应用场景；open 语义的值替身形态）。
inline project::RevisionView makeBaselineView(core::RevisionId id)
{
    project::RevisionView view;
    view.id = std::move(id);
    view.seq = 1;
    view.branch = core::BranchId::generate();
    view.commandSummary = "fixture-baseline";
    return view;
}

/// 命令信封装配（branch/baseRevision 随基线——S1/S2 形状）。
inline project::CommandEnvelope makeEnvelope(const project::RevisionView& baseline,
                                             std::string commandType,
                                             const CommandPayload& payload)
{
    project::CommandEnvelope envelope;
    envelope.branch = baseline.branch;
    envelope.expectedRevision = baseline.id;
    envelope.commandType = std::move(commandType);
    envelope.payloadFormatVersion = kCommandPayloadVersion;
    envelope.payloadCanonical = encodeCommandPayload(payload);
    return envelope;
}

}  // namespace sdurws::ird::modeling::testfixture

#endif  // IRD_MODELING_TEST_COMMANDFIXTURES_HPP
