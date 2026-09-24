/**
 * @file   DhConvertEquivalenceTest.cpp
 * @brief  DH↔显式转换器的编译链对照与权威切换命令面用例组
 *         （MdlDhEquivalence）——acceptance 4/5 的具名自证：
 *
 *   ACC4 等价验证（§7.6）：verifyEquivalent 对双权威参数化（同链）各构
 *        Description→经注入 CompileProbe 走 S1～S5 同构只读分段装配
 *        （不发布快照、不产生修订；S5 段走 runtime 公共构造器——探针
 *        装配取舍见 RuntimeCompileProbe 注）→FK 对照 ≤1×10⁻⁹
 *        m/rad（附录 D 第 4 项）；roundtrip 双重一致（DH→展开显式→再求
 *        DH：参数级 ≤第 5 项容差＋FK 级 ≤第 4 项容差）——黄金样本随
 *        WP-13-T16 mdl-dh-equivalence 收口，本文件用程序化构造样本
 *   ACC5 权威切换为独立领域命令（§7.6/C-6）：切换判定与验证在命令
 *        prepare 内执行（T08 处理器协作面）；C-6 先决断既有编辑（载荷
 *        形状投影）；验证失败不产生修订（RejectedHardAssert＋计划清空）；
 *        C-3 链结构不满足→NotExpressible 拒绝切换＋终判诊断
 *
 * 设计依据：units/modeling.md §7.6/§9.4.7/§9.3、units/runtime.md §5.2、
 * REQUIREMENTS.md 附录 D 第 4/5 项；shared 夹具＝test/CommandFixtures.hpp。
 *
 * 集成模式专属（TARGET sdurw_kinematics gating——本文件消费 runtime 公共
 * 构造器与 policy 行程评估器等框架可达面；冒烟模式不编入，
 * 目标注册＋include 路径的冒烟口径由 DhConvertTest 承载）。
 */

#include "CommandFixtures.hpp"

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/modeling/DhConvert.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Sources.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::modeling::testfixture;
namespace modeling = ::sdurws::ird::modeling;
namespace core = ::sdurws::ird::core;
namespace runtime = ::sdurws::ird::runtime;
namespace project = ::sdurws::ird::project;
namespace policy = ::sdurws::ird::policy;  // policy 侧类型（行程评估器装配）
using namespace modeling;  // NOLINT——被测契约面直用

namespace {

// kPi 复用 testfixture（CommandFixtures——期望值不重抄实现常量）。

/// 载荷槽装配（既有对象替换——显式基线身份；CommandHandlersTest 同款，
/// 该辅助为其文件局部——本文件自持同形副本）。
PayloadObjectSlot replaceSlot(const core::ObjectId& oid, std::string token,
                              const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = false;
    slot.objectId = oid;
    slot.objectTypeToken = std::move(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

/// 载荷槽装配（新对象——全零身份）。
PayloadObjectSlot newSlot(std::string token, const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = true;
    slot.objectId = core::ObjectId{};
    slot.objectTypeToken = std::move(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

/// SHA-256 摘要（对象字节申报值——与 runtime 编译器同一摘要算法）。
core::Digest256 digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/// 固定种子的 SHA-256（身份块测试值——非零保证）。
core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

// ---- 探针替身（CompileProbe 的测试装配——测试域自持，不入公共面）----

/**
 * @brief 探针替身（CompileProbe 集成模式装配——S5 真实构造路径）：
 *        对注入 Description 经 runtime 公共构造器 CanonicalModelBuilder
 *        装配规范模型（与产品编译器 S5 同一构造承载面——轴规格化/限位/
 *        预设一致性/身份唯一/内容身份等全部 builder 不变量真实执行）。
 *
 * ★ R-2 边界（测试域的装配取舍，登记单元卡 §15 v0.10）：runtime 产品
 *   编译器类（CanonicalModelCompiler）声明于 runtime/src/CompilerImpl.hpp
 *   ——单元私有头，跨单元 include 被红线禁止；本替身跳过 S1～S4 管道
 *   （修订锚定/解析/资源读取——runtime 自身已验面），S5 段走真实公共
 *   builder。CompileProbe 的生产装配归 L5（以产品编译器注入）；本任务
 *   的被测消费面＝verifyEquivalent 对探针的注入点语义（§9.4.7 原文
 *   "编译分段入口由调用方注入"）——探针实现的两个硬契约（只读、确定
 *   性）在本替身同样成立。
 */
class RuntimeCompileProbe final : public CompileProbe {
public:
    runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError> buildCanonicalModel(
        const runtime::RobotDesignDescription& description) const override
    {
        ++calls;
        try {
            // ---- 身份块（探针自持的定位三元组测试值；objectRefs 登记
            // 根＋关节＋连杆——S5 的"∈objectRefs"复核面）----
            runtime::CanonicalModelHeader header;
            header.project = m_project;
            header.branch = m_branch;
            header.revision = m_revision;
            header.revisionSeq = 1;
            header.descriptionContractVersion = description.descriptionContractVersion;
            header.compilerContractVersion = 1;
            header.builtFrom = digestOf(description.robotLocalName);  // 非零测试值
            runtime::ObjectRefEntry robotRef;
            robotRef.objectId = m_robotOid;
            robotRef.contentVersion = m_cv;
            robotRef.objectTypeToken = runtime::kRobotDesignObjectType;
            robotRef.digest = digestOf("dh-eq-robot");
            header.objectRefs.push_back(robotRef);
            for (const runtime::JointDescription& j : description.joints) {
                runtime::ObjectRefEntry e;
                e.objectId = j.objectId;
                e.contentVersion = m_cv;
                e.objectTypeToken = "joint";
                e.digest = digestOf("dh-eq-" + j.localName);
                header.objectRefs.push_back(e);
            }
            for (const runtime::LinkDescription& l : description.links) {
                runtime::ObjectRefEntry e;
                e.objectId = l.objectId;
                e.contentVersion = m_cv;
                e.objectTypeToken = "link";
                e.digest = digestOf("dh-eq-" + l.localName);
                header.objectRefs.push_back(e);
            }

            // ---- 链块（Description→Canonical 同式拷贝——zeroOffset=0，
            // 显式表示已折叠：CompilerImpl S5 v0.12 登记的同一语义）----
            runtime::RobotChain chain;
            chain.robotObjectId = m_robotOid;
            chain.robotLocalName = description.robotLocalName;
            chain.deviceName = description.robotLocalName;
            chain.joints.reserve(description.joints.size());
            for (const runtime::JointDescription& j : description.joints) {
                runtime::CanonicalJoint cj;
                cj.objectId = j.objectId;
                cj.localName = j.localName;
                cj.type = j.type;
                cj.axis = j.axis;      // 规格化在 builder（§4.3.3）
                cj.origin = j.origin;  // 已含零位折叠（Description 契约）
                cj.zeroOffset = 0.0;
                if (j.lower.tryValue().has_value() && j.upper.tryValue().has_value()) {
                    runtime::JointBounds b;
                    b.lower = j.lower.value();
                    b.upper = j.upper.value();
                    cj.bounds = b;
                }
                cj.workingRange = j.workingRange;
                cj.maxVelocity = j.maxVelocity;
                cj.maxAcceleration = j.maxAcceleration;
                chain.joints.push_back(std::move(cj));
            }
            chain.links.reserve(description.links.size());
            for (const runtime::LinkDescription& l : description.links) {
                runtime::CanonicalLink cl;
                cl.objectId = l.objectId;
                cl.localName = l.localName;
                chain.links.push_back(std::move(cl));
            }

            // ---- S5 装配（公共 builder——全部构造不变量真实执行；失败
            // 以 RuntimeError 抛出，转 err 轨）----
            runtime::CanonicalModelBuilder builder;
            builder.setHeader(header);
            builder.setWorld(runtime::WorldPlacement{});
            builder.setChain(chain);
            return runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>::ok(
                builder.build());
        } catch (const runtime::RuntimeError& e) {
            return runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>::err(
                runtime::RuntimeError(e.code(), std::string(e.what())));
        }
    }

    mutable int calls = 0;  ///< 调用观测（单线程用例内）
    core::ProjectId m_project = core::ProjectId::generate();
    core::BranchId m_branch = core::BranchId::generate();
    core::RevisionId m_revision = core::RevisionId::generate();
    core::ObjectId m_robotOid = core::ObjectId::generate();
    core::ContentVersion m_cv{};

private:
    // （无状态——全部装配在调用栈内；探针无共享可变状态）
};

/**
 * @brief 恒失败探针（等价验证第四道门的拒绝面注入——不触达 runtime）。
 */
class FailingProbe final : public CompileProbe {
public:
    runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError> buildCanonicalModel(
        const runtime::RobotDesignDescription&) const override
    {
        return runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>::err(
            runtime::RuntimeError(runtime::RuntimeErrorCode::InputInvalid,
                                  std::string("注入失败（测试）")));
    }
};

// ---- 工作集夹具 ----

/// DH 链条目（CommandFixtures 无 DH 构造——本文件自持）。
DhChainJoint makeDhEntry(const core::ObjectId& oid, const std::string& name,
                         double thetaOffset, double d, double a, double alpha,
                         double zeroOffset)
{
    DhChainJoint entry;
    entry.dh.thetaOffset = thetaOffset;
    entry.dh.d = d;
    entry.dh.a = a;
    entry.dh.alpha = alpha;
    entry.zeroOffset = zeroOffset;
    entry.type = JointType::Revolute;
    entry.objectId = oid;
    entry.localName = name;
    entry.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-kPi, kPi}, userProvenance());
    return entry;
}

LinkEntry makeLink(const core::ObjectId& oid, const std::string& name)
{
    LinkEntry link;
    link.objectId = oid;
    link.localName = name;
    return link;
}

/**
 * 构造显式权威基线工作集（几何由被测展开产出——"DH 可表达"的显式链；
 * 物性缺失＝DataInsufficient 降级面，不触发断言——V15-01）。
 */
ModelingWorkingSet makeExplicitBaseline(const DhChain& chain)
{
    ModelingWorkingSet ws;
    ws.design.displayName = "DH-EQ-BOT";
    ws.design.authority = AuthorityMode::Explicit;
    ws.rootObjectId = core::ObjectId::generate();

    std::vector<core::DiagnosticRecord> diags;
    const DhExplicitConverter converter;
    const ExpandOutcome expanded = converter.dhToExplicit(chain, diags);
    if (!expanded.ok) { ADD_FAILURE() << "夹具展开失败"; }
    ws.design.joints = expanded.joints;
    for (std::size_t i = 0; i <= chain.joints.size(); ++i) {
        ws.design.links.push_back(
            makeLink(core::ObjectId::generate(),
                     "L" + std::to_string(i)));
    }
    return ws;
}

/**
 * 由基线构造候选 DH 权威工作集（authority=StandardDH＋dhDerived 逐关节
 * 落参——axis/origin 语义上为派生（本夹具保留原值不入对照——
 * buildChainDescription 的 DH 侧只读 dhDerived））。
 */
ModelingWorkingSet makeDhCandidate(const ModelingWorkingSet& baseline,
                                   const std::vector<DhParameters>& params)
{
    ModelingWorkingSet candidate = baseline;
    candidate.design.authority = AuthorityMode::StandardDH;
    if (params.size() != candidate.design.joints.size()) {
        ADD_FAILURE() << "夹具参数数量与关节链不一致";
        return candidate;
    }
    for (std::size_t i = 0; i < candidate.design.joints.size(); ++i) {
        candidate.design.joints[i].dhDerived = params[i];
    }
    return candidate;
}

/// 诊断集中是否含指定稳定码。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diags,
                   const std::string& code)
{
    for (const core::DiagnosticRecord& r : diags) {
        if (r.code == code) { return true; }
    }
    return false;
}

/// 两关节非平凡 DH 链（含非零零位偏置——零位折叠对照面）。
DhChain makeTwoJointChain()
{
    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.3, 0.15, 0.40, kPi / 2, 0.10));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       -0.2, 0.25, 0.30, -kPi / 3, -0.05));
    return chain;
}

}  // namespace

// =====================================================================
// acceptance 4：编译链 FK 对照等价验证（S1～S5 只读分段）
// =====================================================================

/**
 * ACC4 正路径：双权威参数化（同链）经真实 runtime 编译器分段入口对照
 * ——等价成立（全链逐关节位置/姿态偏差 ≤1×10⁻⁹，附录 D 第 4 项）；
 * 探针只读面（不发布快照——产物为 CanonicalModel 值）与调用观测。
 */
TEST(MdlDhEquivalence, VerifyEquivalentAcceptsSameChain_WP13T09_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baseline = makeExplicitBaseline(chain);

    // 候选＝同一几何的 DH 权威参数化（原链参数）。
    std::vector<DhParameters> params;
    for (const DhChainJoint& j : chain.joints) { params.push_back(j.dh); }
    const ModelingWorkingSet candidate = makeDhCandidate(baseline, params);

    RuntimeCompileProbe probe;
    const DhExplicitConverter converter;
    const EquivalenceReport report = converter.verifyEquivalent(baseline, candidate, probe);

    ASSERT_TRUE(report.inputsValid) << report.failureDetail;
    ASSERT_TRUE(report.compileOk) << report.failureDetail;
    EXPECT_TRUE(report.equivalent);
    EXPECT_LE(report.maxPositionDeviation, 1e-9) << "附录 D 第 4 项位置容差";
    EXPECT_LE(report.maxOrientationDeviation, 1e-9) << "附录 D 第 4 项姿态容差";
    EXPECT_EQ(report.jointDeviations.size(), baseline.design.joints.size());
    // 只读分段消费面：每侧各一次编译（2 次——不发布快照的 S1～S5）。
    EXPECT_EQ(probe.calls, 2);
}

/**
 * ACC4 拒绝路径：基线原点微扰 1e-6 m（几何不再等价）→FK 对照超差→
 * equivalent=false；最大位置偏差 >1×10⁻⁹（第 4 项）。
 */
TEST(MdlDhEquivalence, VerifyEquivalentRejectsDeviatedChain_WP13T09_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const DhChain chain = makeTwoJointChain();
    ModelingWorkingSet baseline = makeExplicitBaseline(chain);
    // 基线 J1 原点沿 x 平移 1e-6 m（几何偏差注入——超越第 4 项容差）。
    const rw::math::Transform3D<double> moved =
        static_cast<rw::math::Transform3D<double>>(baseline.design.joints[0].origin.value());
    baseline.design.joints[0].origin = core::SourcedValue<JointPose>::provided(
        JointPose(rw::math::Transform3D<double>(
            moved.P() + rw::math::Vector3D<double>(1e-6, 0.0, 0.0), moved.R())),
        userProvenance());

    std::vector<DhParameters> params;
    for (const DhChainJoint& j : chain.joints) { params.push_back(j.dh); }
    const ModelingWorkingSet candidate = makeDhCandidate(makeExplicitBaseline(chain), params);

    RuntimeCompileProbe probe;
    const DhExplicitConverter converter;
    const EquivalenceReport report = converter.verifyEquivalent(baseline, candidate, probe);

    ASSERT_TRUE(report.inputsValid);
    ASSERT_TRUE(report.compileOk);
    EXPECT_FALSE(report.equivalent) << "几何偏差 1e-6 m 必须被 FK 对照识破";
    EXPECT_GT(report.maxPositionDeviation, 1e-9);
}

/**
 * ACC4 roundtrip 双重一致（V-11 主链路）：DH 权威→展开显式→再求 DH
 * ——参数级一致（逐关节逐项 ≤第 5 项容差）＋FK 级一致（以求解参数重组
 * 候选，经编译链对照 ≤第 4 项容差）。
 */
TEST(MdlDhEquivalence, RoundtripDoubleConsistency_WP13T09_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"});

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baseline = makeExplicitBaseline(chain);

    // 再求 DH（展开产物→五状态求解）。
    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult solved = converter.explicitToDh(baseline.design.joints, diags);
    ASSERT_EQ(solved.determination, DhDetermination::Exact);
    ASSERT_TRUE(diags.empty());

    // 参数级一致：逐关节逐项 ≤第 5 项上界（附录 D C3）。
    ASSERT_EQ(solved.parameters.size(), chain.joints.size());
    for (std::size_t i = 0; i < chain.joints.size(); ++i) {
        EXPECT_NEAR(solved.parameters[i].thetaOffset, chain.joints[i].dh.thetaOffset, 1e-9);
        EXPECT_NEAR(solved.parameters[i].d, chain.joints[i].dh.d, 1e-9);
        EXPECT_NEAR(solved.parameters[i].a, chain.joints[i].dh.a, 1e-9);
        EXPECT_NEAR(solved.parameters[i].alpha, chain.joints[i].dh.alpha, 1e-9);
    }

    // FK 级一致：以求解参数重组候选→编译链对照 ≤第 4 项容差。
    const ModelingWorkingSet candidate = makeDhCandidate(baseline, solved.parameters);
    RuntimeCompileProbe probe;
    const EquivalenceReport report = converter.verifyEquivalent(baseline, candidate, probe);
    ASSERT_TRUE(report.compileOk) << report.failureDetail;
    EXPECT_TRUE(report.equivalent);
    EXPECT_LE(report.maxPositionDeviation, 1e-9);
    EXPECT_LE(report.maxOrientationDeviation, 1e-9);
}

// =====================================================================
// acceptance 5：权威切换域门与命令 prepare 协作面（T08 处理器）
// =====================================================================

/**
 * ACC5 域门正路径：prepareAuthoritySwitch 四道门全过——candidate 为
 * StandardDH 权威＋dhDerived＝判定选定解（逐关节逐项等于原链参数）；
 * diags 无 error 级记录。
 */
TEST(MdlDhEquivalence, AuthoritySwitchDomainGatePreparesCandidate_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baseline = makeExplicitBaseline(chain);

    const DhExplicitConverter converter;
    RuntimeCompileProbe probe;
    std::vector<core::DiagnosticRecord> diags;
    const AuthoritySwitchDecision decision =
        prepareAuthoritySwitch(baseline, baseline, converter, probe, diags);

    ASSERT_TRUE(decision.allowed) << "四道门应全过";
    EXPECT_EQ(decision.determination, DhDetermination::Exact);
    ASSERT_TRUE(decision.equivalence.equivalent);
    // 候选：StandardDH 权威＋dhDerived 落地（判定选定解）＋无未决编辑。
    EXPECT_EQ(decision.candidate.design.authority, AuthorityMode::StandardDH);
    ASSERT_TRUE(decision.candidate.changes.empty());
    ASSERT_EQ(decision.candidate.design.joints.size(), chain.joints.size());
    for (std::size_t i = 0; i < chain.joints.size(); ++i) {
        ASSERT_TRUE(decision.candidate.design.joints[i].dhDerived.has_value());
        EXPECT_NEAR(decision.candidate.design.joints[i].dhDerived->thetaOffset,
                    chain.joints[i].dh.thetaOffset, 1e-9);
        EXPECT_NEAR(decision.candidate.design.joints[i].dhDerived->d,
                    chain.joints[i].dh.d, 1e-9);
    }
    // 诊断面：无 error 级（正路径）。
    for (const core::DiagnosticRecord& r : diags) {
        EXPECT_NE(r.code, std::string(kMdlDhNotExpressible));
        EXPECT_NE(r.code, std::string(kMdlDhAnalysisFailed));
    }
}

/**
 * ACC5 域门 C-6（先决断既有编辑）：draft 携带未决断编辑→拒绝切换
 * （AuthorityViolation 值面）；等价验证未执行（探针零调用——门序短路）。
 */
TEST(MdlDhEquivalence, AuthoritySwitchDomainGateRefusesPendingEdits_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"},
                  std::vector<std::string>{"AT-16"});

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baseline = makeExplicitBaseline(chain);
    ModelingWorkingSet draft = baseline;
    draft.changes.push_back(ModelingChangeRecord{"joints[1]", "编辑 J2 轴向（未提交）"});

    const DhExplicitConverter converter;
    RuntimeCompileProbe probe;
    std::vector<core::DiagnosticRecord> diags;
    const AuthoritySwitchDecision decision =
        prepareAuthoritySwitch(draft, baseline, converter, probe, diags);

    EXPECT_FALSE(decision.allowed);
    ASSERT_TRUE(decision.error.has_value());
    EXPECT_EQ(decision.error->code, ModelingErrorCode::AuthorityViolation);
    EXPECT_NE(decision.error->detail.find("先提交或撤销"), std::string::npos);
    EXPECT_EQ(probe.calls, 0) << "门②拒绝后不得进入等价验证（门序短路）";
}

/**
 * ACC5 域门第四道（验证失败不产生修订）：探针注入编译失败→切换被拒
 * （DhExpandFailed 值面）＋候选清空（拒绝态不可消费）。
 */
TEST(MdlDhEquivalence, AuthoritySwitchRefusedWhenVerificationFails_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baseline = makeExplicitBaseline(chain);

    const DhExplicitConverter converter;
    FailingProbe failingProbe;
    std::vector<core::DiagnosticRecord> diags;
    const AuthoritySwitchDecision decision =
        prepareAuthoritySwitch(baseline, baseline, converter, failingProbe, diags);

    EXPECT_FALSE(decision.allowed);
    ASSERT_TRUE(decision.error.has_value());
    EXPECT_EQ(decision.error->code, ModelingErrorCode::DhExpandFailed);
    EXPECT_TRUE(decision.candidate.design.joints.empty())
        << "拒绝态候选不可消费（清空）";
    EXPECT_FALSE(decision.equivalence.equivalent);
    EXPECT_FALSE(decision.equivalence.compileOk);
}

// =====================================================================
// acceptance 5：命令 prepare 协作面（apply-robot-design 权威切换变体）
// =====================================================================

/// 命令面夹具：T08 HandlerFixture 形态＋DH 服务注入（converter/probe）。
struct SwitchFixture {
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator =
        policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    TestQueryPort query;
    MockCompilePort compile;
    RuntimeCompileProbe probe;
    const DhExplicitConverter converter;
    HandlerServices services{
        AssertionSuite::Ports{evaluator.get(), &names}, &provider, makeOid(),
        std::nullopt, &converter, &probe};

    void bindNames(const RobotDesign& design)
    {
        for (const JointEntry& j : design.joints) {
            names.byId[j.objectId] = "Robot/" + j.localName;
        }
    }
};

/**
 * ACC5 命令正路径：基线闭包（Explicit 根）→apply-robot-design 权威切换
 * 变体载荷→prepare 内完成转换判定＋等价验证（探针被调用）→Planned 且
 * 根写入字节解码后为 StandardDH＋dhDerived＝判定选定解（prepare 计算，
 * 不信任载荷声明）。
 */
TEST(MdlDhCommand, AuthoritySwitchPreparesInsideCommandPrepare_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    SwitchFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baselineWs = makeExplicitBaseline(chain);
    const core::ObjectId rootOid = core::ObjectId::generate();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType),
                      encodeVariant(baselineWs.design));
    f.bindNames(baselineWs.design);

    // 切换变体载荷：同身份根，authority=StandardDH（dhDerived 留空——
    // 由 prepare 判定计算，不信任载荷声明）。
    RobotDesign switchRoot = baselineWs.design;
    switchRoot.authority = AuthorityMode::StandardDH;
    for (JointEntry& j : switchRoot.joints) { j.dhDerived.reset(); }
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), switchRoot));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyRobotDesign), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{1});
    // 根写入为 prepare 计算的候选：解码后 authority=StandardDH＋dhDerived
    // ＝判定选定解（与原链参数逐项 ≤1e-9）。
    RobotDesignCodec codec;
    const auto written = codec.decode(plan.objectWrites[0].payloadCanonical,
                                      kCurrentFormatVersion);
    ASSERT_TRUE(written.ok());
    const RobotDesign& writtenDesign = std::get<RobotDesign>(written.get());
    EXPECT_EQ(writtenDesign.authority, AuthorityMode::StandardDH);
    ASSERT_EQ(writtenDesign.joints.size(), chain.joints.size());
    for (std::size_t i = 0; i < chain.joints.size(); ++i) {
        ASSERT_TRUE(writtenDesign.joints[i].dhDerived.has_value());
        EXPECT_NEAR(writtenDesign.joints[i].dhDerived->thetaOffset,
                    chain.joints[i].dh.thetaOffset, 1e-9);
        EXPECT_NEAR(writtenDesign.joints[i].dhDerived->d, chain.joints[i].dh.d, 1e-9);
        EXPECT_NEAR(writtenDesign.joints[i].dhDerived->a, chain.joints[i].dh.a, 1e-9);
        EXPECT_NEAR(writtenDesign.joints[i].dhDerived->alpha,
                    chain.joints[i].dh.alpha, 1e-9);
    }
    // 切换判定/验证真实发生在 prepare 内：探针被消费（S1～S5 分段编译）。
    EXPECT_GE(f.probe.calls, 2);
    // 诊断面无 error 级 DH 终判（正路径）。
    EXPECT_FALSE(hasDiagnostic(diags, std::string(kMdlDhNotExpressible)));
}

/**
 * ACC5 C-3：基线链含 prismatic（结构前提不满足）→切换被拒
 * （RejectedHardAssert）＋终判诊断 MDL-DH-NOT-EXPRESSIBLE＋计划清空
 * （验证失败不产生修订——拒绝即无修订，project 不消费拒绝态计划）。
 */
TEST(MdlDhCommand, AuthoritySwitchRejectsNotExpressibleWithTerminalDiag_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"});

    SwitchFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));

    // 基线：2 关节链（可过基线断言）＋切换载荷声称 StandardDH——但链上
    // J2 为 prismatic（C-3 拒绝面）。
    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.2, 0.10, 0.30, kPi / 2, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       0.0, 0.20, 0.25, 0.0, 0.0));
    ModelingWorkingSet baselineWs = makeExplicitBaseline(chain);
    baselineWs.design.joints[1].type = JointType::Prismatic;  // C-3 反例注入

    const core::ObjectId rootOid = core::ObjectId::generate();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType),
                      encodeVariant(baselineWs.design));
    f.bindNames(baselineWs.design);

    RobotDesign switchRoot = baselineWs.design;
    switchRoot.authority = AuthorityMode::StandardDH;
    for (JointEntry& j : switchRoot.joints) { j.dhDerived.reset(); }
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), switchRoot));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyRobotDesign), payload),
        f.query.view, plan, diags);

    // 拒绝：硬域门轨＋终判诊断＋计划清空（不产生修订）。
    EXPECT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
    EXPECT_TRUE(plan.objectWrites.empty()) << "拒绝态计划不可消费";
    EXPECT_TRUE(hasDiagnostic(diags, std::string(kMdlDhNotExpressible)));
}

/**
 * ACC5 C-6 命令面投影：切换载荷混入部件槽（既有编辑未决断）→
 * RejectedInvalidInput；计划清空；探针零调用（门在判定之前短路）。
 */
TEST(MdlDhCommand, AuthoritySwitchRejectsMixedEdits_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"},
                  std::vector<std::string>{"AT-16"});

    SwitchFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baselineWs = makeExplicitBaseline(chain);
    const core::ObjectId rootOid = core::ObjectId::generate();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType),
                      encodeVariant(baselineWs.design));
    f.bindNames(baselineWs.design);

    RobotDesign switchRoot = baselineWs.design;
    switchRoot.authority = AuthorityMode::StandardDH;
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), switchRoot));
    // 混入编辑：追加一个新部件槽（未决断的既有编辑——C-6 拒绝面；
    // allocateNew 工具槽在钩子内可 Planned，拒绝点在 T09 门）。
    ToolDefinition extraTool;
    extraTool.localName = "T1";
    extraTool.tcpList.push_back(TcpEntry{std::string("tcp1"),
                                         rw::math::Transform3D<double>(
                                             rw::math::Vector3D<double>(0, 0, 0.1),
                                             rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1)),
                                         std::string("TCP1")});
    payload.objects.push_back(
        newSlot(std::string(kToolDefinitionObjectType), ObjectVariant(extraTool)));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyRobotDesign), payload),
        f.query.view, plan, diags);

    EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
    EXPECT_TRUE(plan.objectWrites.empty());
    EXPECT_EQ(f.probe.calls, 0) << "C-6 短路——判定与验证未执行";
}

/**
 * ACC5 非切换载荷零影响（门触发的必要条件对普通载荷恒假）：基线
 * Explicit→候选仍 Explicit 的普通替换应用→Planned 且探针零调用——
 * T09 增量门对 T08 既有行为零改变（向后兼容的自证面）。
 */
TEST(MdlDhCommand, NonSwitchPayloadUnaffectedByGate_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"},
                  std::vector<std::string>{"AT-16"});

    SwitchFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baselineWs = makeExplicitBaseline(chain);
    const core::ObjectId rootOid = core::ObjectId::generate();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType),
                      encodeVariant(baselineWs.design));
    f.bindNames(baselineWs.design);

    // 普通替换：仍为 Explicit（仅 displayName 变化——不进编译身份）。
    RobotDesign edited = baselineWs.design;
    edited.displayName = "DH-EQ-BOT-renamed";
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), edited));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyRobotDesign), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    EXPECT_EQ(f.probe.calls, 0) << "非切换载荷不触达等价验证";
    // 权威模式未被翻转。
    RobotDesignCodec codec;
    const auto written = codec.decode(plan.objectWrites[0].payloadCanonical,
                                      kCurrentFormatVersion);
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(std::get<RobotDesign>(written.get()).authority, AuthorityMode::Explicit);
}

/**
 * ACC5 装配缺陷面：切换变体到达而 DH 服务未装配→fail-fast（std::
 * logic_error——装配错误不静默降级；普通载荷不受影响）。
 */
TEST(MdlDhCommand, AuthoritySwitchWithoutServicesFailsFast_WP13T09_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02"},
                  std::vector<std::string>{"AT-16"});

    // 服务集缺 DH 装配（默认空指针）。
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator =
        policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    TestQueryPort query;
    MockCompilePort compile;
    HandlerServices services{
        AssertionSuite::Ports{evaluator.get(), &names}, &provider, makeOid(),
        std::nullopt};

    const DhChain chain = makeTwoJointChain();
    const ModelingWorkingSet baselineWs = makeExplicitBaseline(chain);
    const core::ObjectId rootOid = core::ObjectId::generate();
    query.view = makeBaselineView(core::RevisionId::generate());
    query.addObject(rootOid, std::string(kRobotDesignObjectType),
                    encodeVariant(baselineWs.design));
    names.byId[baselineWs.design.joints[0].objectId] = "Robot/J1";
    names.byId[baselineWs.design.joints[1].objectId] = "Robot/J2";

    RobotDesign switchRoot = baselineWs.design;
    switchRoot.authority = AuthorityMode::StandardDH;
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), switchRoot));

    ApplyRobotDesignHandler handler(services);
    project::HandlerContext ctx(query, &compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    EXPECT_THROW((void)handler.prepare(
                     ctx,
                     makeEnvelope(query.view, std::string(kCmdApplyRobotDesign), payload),
                     query.view, plan, diags),
                 std::logic_error);
}
