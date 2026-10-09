/**
 * @file   OptPluginPanelTest.cpp
 * @brief  WP-20-T10 用例组——界面链路模型层（OptPanelReadiness/Variables/
 *         Constraints/RunControl/Candidates/Compare/StageLock/Text/
 *         Catalog/Gui 十组）：就绪投影合成、变量表行集、约束页呈现与
 *         阶段锁横幅、进度漏斗黄金、运行控制流（取消协作——AT-34）、
 *         候选表与对比黄金、文案守卫、装配登记面值断言与 GUI 呈现边界
 *         登记（任务契约 acceptance 1/2 的执行证明面）。
 *
 * 设计依据：
 *   - units/optimization.md §9.1（运行控制 R1 形态——取消＋进度为 B 期
 *     承诺〔AT-34/OPT-VER-139 插件半区〕；"UI 只显示进度和取消状态；
 *     不在 UI 线程执行候选评估"红线；§15.0——暂停相关能力归 R2，本面
 *     零其呈现入口）、§6.5（阶段锁呈现边界——阻塞横幅非候选淘汰）、
 *     §6.4（检查发现五元组——横幅素材）、§7.1（八项指标全展示/缺失
 *     "—"）、§3.2（插件零计算红线——模型层只重组/查表）
 *   - 先例：dynamics/test/DynPluginPanelTest.cpp（模型层范式——不启动
 *     GUI；GUI 呈现另行 envUnavailable 登记——WP-17-T09）；本单元
 *     test/EvaluatorPortsTest.cpp（两级编排 fixture——取消协作链式用例
 *     按其真注册表＋脚本化替身形态自持精简）
 *   - 需求 UX-02（工程用语/零哈希进用户文本）、UX-10（七态素材呈现）、
 *     UX-03（正常取消不属于错误、不产生错误诊断）、TASK-01（协作取消）、
 *     TASK-02（取消结果不冒充完整——formalExportAvailable 呈现位）；
 *     任务契约 tasks/foundation/WP-20-T10.json acceptance 1/2/3
 *
 * 测试范围声明：GUI 呈现（widget 渲染/交互点击）不在本目标——无人值
 * 守门禁不做 GUI 运行验证（既有环境事实），呈现归宿主装配批次的开发
 * harness 手动验证通道，本目标内以 GuiPresentation 用例 envUnavailable
 * 登记（AGENTS §4.2；dynamics WP-17-T09 同款）。插件面零计算红线
 * （acceptance 3 的词表扫描半区）归契约测试 OptPluginAssembly（全文
 * 扫描 plugin/＋assembly/）。
 *
 * 测试与被测面的关系：全部模型层流经 plugin/ 呈现模型头消费（公共
 * 形态——WP-20-T03 B-1 教训钉扎）；AT-34 链式用例（取消协作生效）经真
 * 两级编排驱动（真 evidence 注册表＋脚本化替身）后走聚合工厂再投影
 * 候选行——取消协作的"生效"以编排队（运行终态取消＋部分批保留）＋
 * 呈现面（行集保留＋正式导出位 false＋零错误横幅）双半区断言。
 */

#include <sdurws/ird/optimization/OptimizationPluginAssembly.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/Constraint.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>
#include <sdurws/ird/optimization/Run.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 插件面呈现模型半区（plugin/ 私有头——同单元测试目标 PRIVATE include
// 面解析；模型层零 Qt——被测主面）。
#include "OptPanelModel.hpp"
#include "OptPanelModule.hpp"
#include "OptPanelTypes.hpp"

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidateStatus;
using optimization::ConstraintId;
using optimization::ConstraintSpec;
using optimization::MetricId;
using optimization::OptimizationConfiguration;
using optimization::OptimizationStage;
using optimization::OptimizationRunResult;
using optimization::PreflightFinding;
using optimization::PreflightLevel;
using optimization::PreflightReport;
using optimization::RunPhase;
using optimization::TwoStageRunRecord;
using optimization::TwoStageRunResult;
using optimization::VariableBinding;
using optimization::VariableKind;

// 模型层流与词表（被测主面——具名引入，与被测头同一命名空间可读性）。
using sdurws::ird::optimization::kOptPanelPageKeys;
using sdurws::ird::optimization::kOptProgressPhaseTokens;
using sdurws::ird::optimization::candidateComparison;
using sdurws::ird::optimization::candidateTableRows;
using sdurws::ird::optimization::constraintPagePresentation;
using sdurws::ird::optimization::progressFunnelRows;
using sdurws::ird::optimization::readinessProjection;
using sdurws::ird::optimization::requestRunCancel;
using sdurws::ird::optimization::requestRunStart;
using sdurws::ird::optimization::resolvePanelText;
using sdurws::ird::optimization::stageLockBannerItems;
using sdurws::ird::optimization::variableTableRows;

namespace {

// =====================================================================
// 构造辅助（模型测试自持——值类型直构，零环境依赖）
// =====================================================================

/// 非零候选身份（身份对账面——行集与记录的逐位对齐锚）。
CandidateId makeCandidateId(unsigned char seed)
{
    CandidateId id;
    id.bytes.fill(seed);
    return id;
}

/// 连续绑定（DH a，m——R1 §5.3 #2 权威字段；StageB 启用）。
VariableBinding makeLengthBinding(const std::string& id = "mdl.joint[2].dh.a")
{
    VariableBinding b;
    b.bindingId = id;
    b.kind = VariableKind::Continuous;
    b.unit = core::UnitToken::find("m").value();
    b.lowerBound = 0.2;   // m（含端点闭区间下界）
    b.upperBound = 0.8;   // m
    b.step = 0.0;         // 0＝非量化（连续）
    b.defaultValue = 0.5; // m
    b.authorized = true;
    b.locked = false;
    b.authorityFieldPath = "robot-design/joints[2]/dh/a";
    return b;
}

/// 传动比绑定（无量纲——R1 §5.3 #9；StageB/StageD 均启用，V12-02）。
VariableBinding makeRatioBinding()
{
    VariableBinding b;
    b.bindingId = "mdl.drivetrain.ratio[1]";
    b.kind = VariableKind::Continuous;
    b.unit = core::UnitToken::find("1").value();
    b.lowerBound = 60.0;   // 无量纲传动比（c 口径）
    b.upperBound = 120.0;  // 无量纲
    b.defaultValue = 80.0; // 无量纲
    b.authorized = true;
    b.locked = false;
    b.authorityFieldPath = "robot-drivetrain/ratioPerJoint[1]";
    return b;
}

/// 截面类型绑定（枚举——§5.3 #1b；P-OPT-4 裁决前两阶段均不启用——
/// "登记了但阶段不支持"的词表实例）。
VariableBinding makeSectionTypeBinding()
{
    VariableBinding b;
    b.bindingId = "mdl.link[1].section.type";
    b.kind = VariableKind::Enumeration;
    b.lowerBound = 0.0;
    b.upperBound = 0.0;
    b.step = 0.0;
    b.enumValues = {"SolidCylinder", "HollowCylinder", "Box"};
    b.defaultValueIndex = 0;
    b.authorized = true;
    b.locked = true;  // 阶段不支持→初始化面锁定（呈现层只直拷该位）
    b.authorityFieldPath = "robot-design/links[1]/shape/section/type";
    return b;
}

/// 未登记绑定（词表无此 token——"未登记"与"阶段不支持"分列的反例面）。
VariableBinding makeUnregisteredBinding()
{
    VariableBinding b;
    b.bindingId = "mdl.not-a-variable[9]";
    b.kind = VariableKind::Continuous;
    b.unit = core::UnitToken::find("m").value();
    b.authorityFieldPath = "robot-design/unknown";
    return b;
}

/// 八项指标全量记录（槽位值数组直构——nullopt＝"—"素材；valueOf 要求
/// 八项全量形状，缺槽位以 nullopt 承载而非缺条目）。
optimization::StaticMetricResult
makeMetrics(const std::array<std::optional<double>, 8>& values)
{
    optimization::StaticMetricResult result;
    result.metrics.reserve(8);
    for (std::size_t m = 0; m < 8; ++m) {
        optimization::MetricComputation entry;
        entry.metricId = static_cast<MetricId>(m);
        entry.valueSi = values[m];
        if (!values[m].has_value()) {
            entry.gapToken = "opt.gap.source-missing";  // 缺失原因 token
        }
        result.metrics.push_back(std::move(entry));
    }
    return result;
}

/// 单候选运行记录（行集黄金用例的值面直构）。
TwoStageRunRecord makeRecord(bool isBaseline, CandidateStatus status,
                             const optimization::StaticMetricResult& metrics,
                             bool screeningOnly = false,
                             std::vector<std::string> rejectionTokens = {})
{
    TwoStageRunRecord record;
    record.candidateId = makeCandidateId(1);
    record.isBaseline = isBaseline;
    record.status = status;
    record.screeningOnly = screeningOnly;
    record.metrics = metrics;
    for (const std::string& token : rejectionTokens) {
        optimization::RejectionReason reason;
        reason.reasonToken = token;
        record.evaluation.rejections.push_back(reason);
    }
    return record;
}

/// 运行结果聚合直构（行集/对比用例的值面——不经编排，零环境依赖）。
OptimizationRunResult makeRunResult(std::vector<TwoStageRunRecord> candidates,
                                    RunPhase phase = RunPhase::Completed)
{
    OptimizationRunResult result;
    result.runId = optimization::OptimizationRunId::generate();
    result.runPhase = phase;
    result.candidates = std::move(candidates);
    return result;
}

/// 带混合级别发现的检查报告（横幅映射用例素材——§6.4 五元组直构；
/// allowances 按"无阻塞项即放行"的最小语义对齐 §6.4 定义——真 Preflight
/// 服务侧独立计算，替身取其同向值）。
PreflightReport reportWithFindings(
    const std::vector<std::pair<std::uint8_t, PreflightLevel>>& items)
{
    PreflightReport report;
    for (const auto& item : items) {
        PreflightFinding finding;
        finding.checkId = item.first;
        finding.level = item.second;
        finding.subject = "obj-finding-" + std::to_string(item.first);
        finding.basis = "PM-04";
        finding.suggestion = "修正研究定义后重新检查";
        report.findings.push_back(finding);
        if (finding.level == PreflightLevel::Blocking) {
            ++report.blockerCount;
        } else {
            ++report.warningCount;
        }
    }
    report.allowances.allowStart = (report.blockerCount == 0);
    report.allowances.allowQuick = report.allowances.allowStart;
    return report;
}

// =====================================================================
// AT-34 链式用例的编排替身（自持精简——T06 EvaluatorPortsTest 形态：
// 真 evidence 注册表＋脚本化评估器/探针/编译/投影替身）
// =====================================================================

/// 修订闭包来源替身（快照组装协议——全部 (oid,cv) 声明在册）。
class PanelClosureSource final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 第 N 次查询后取消的评估上下文替身（批边界协作取消——TASK-01）。
class CancelAfterNContext final : public evidence::IEvaluationContext {
public:
    explicit CancelAfterNContext(std::uint32_t afterQueries)
        : m_after(afterQueries)
    {
    }
    bool cancellationRequested() const override
    {
        ++m_queries;
        return m_queries > m_after;  // 第 m_after+1 次起返回 true
    }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }

private:
    std::uint32_t m_after;               ///< 放行查询数（之后即取消）
    mutable std::uint32_t m_queries = 0; ///< 已发生查询数
};

/// 关节限位探针替身（恒合法结果——管线限位步放行）。
class StubJointLimitProbe final : public optimization::IJointLimitProbe {
public:
    optimization::JointLimitProbeResult result;
    optimization::JointLimitProbeResult
    probeJointLimits(const optimization::JointLimitProbeRequest&) const override
    {
        return result;
    }
};

/// 静态碰撞探针替身（恒合法空发现——构型级零过滤）。
class StubCollisionProbe final : public optimization::IStaticCollisionProbe {
public:
    optimization::StaticCollisionProbeResult result;
    optimization::StaticCollisionProbeResult probeSampleSet(
        const std::vector<std::vector<double>>&) const override
    {
        return result;
    }
};

/// 候选编译替身（恒成功——编译步放行）。
class StubCompiler final : public optimization::ICandidateCompiler {
public:
    optimization::CandidateCompileResult result;
    StubCompiler()
    {
        result.ok = true;
    }
    optimization::CandidateCompileResult
    compile(const optimization::CandidatePatch&,
            const std::vector<VariableBinding>&) const override
    {
        return result;
    }
};

/// ③端口脚本化评估器替身（空输出——Feasible 由覆盖完备＋限位合法得到）。
class PanelKinEvaluator final : public evidence::IEngineeringEvaluator {
public:
    explicit PanelKinEvaluator(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        return {};
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

/// 评估器工厂替身（注册面——真注册表登记载体）。
class PanelKinFactory final : public evidence::IEvaluatorFactory {
public:
    explicit PanelKinFactory(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<PanelKinEvaluator>(m_descriptor);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

/// 候选投影替身（基线链型在范围内＋限位合法＋三项静态指标事实——
/// 包络随补丁首个标量值线性变化以产生候选区分度）。
class ScriptedPanelProjector final : public optimization::ICandidateProjector {
public:
    optimization::CandidateProjection
    project(const optimization::CandidatePatch& patch,
            const std::vector<VariableBinding>&) const override
    {
        optimization::CandidateProjection p;
        p.baselineChainInEnabledScope = true;
        optimization::JointLimitSpecRecord joint;
        joint.jointSubject = "obj-probe-joint";
        joint.qmin = -3.14;  // rad（转动关节下限）
        joint.qmax = 3.14;   // rad（转动关节上限）
        p.jointLimits.joints = {joint};
        p.jointLimits.configurations = {{0.0}};  // 关节角 0 rad 单构型
        // 包络随补丁首个标量值线性变化（基线取默认 0.5）：值 v ⇒ 半宽 v，
        // 三向尺寸之和＝6v（m）——候选间包络值互异。
        double v = 0.5;
        if (!patch.items.empty()) {
            v = patch.items.front().scalarValue;
        }
        p.metricFacts.envelope = optimization::EnvelopeFacts{-v, -v, -v, v, v, v};
        optimization::LinkMassFact lm;
        lm.linkSubject = "obj-link";
        lm.massKg = 10.0;  // kg（恒定——质量目标无区分度）
        lm.provenanceToken = "provided";
        p.metricFacts.linkMasses = {lm};
        optimization::PointMarginFact pm;
        pm.caseIdText = "case-margin";
        pm.minMargin = 0.4;  // D-KIN-2 归一化裕量（无量纲）
        pm.unitSymbol = "1";
        p.metricFacts.pointMargins = {pm};
        return p;
    }
};

/// 合法 kin 评估器描述符（契约版本 1；Quick/Verified 两模式）。
evidence::EvaluatorDescriptor makeKinDescriptor(const std::string& key)
{
    evidence::EvaluatorDescriptor d;
    d.key = key;
    d.contractVersion = 1U;
    d.profile.profileId = "kin";
    d.profile.version = "1.0";
    d.supportedModes = {core::EvaluationMode::Quick,
                        core::EvaluationMode::Verified};
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
    return d;
}

}  // namespace

// =====================================================================
// OptPanelReadiness 组——L-O1 就绪投影合成。
// =====================================================================

/**
 * @brief L-O1：会话事实→投影行逐字段透传（UX-10 七态素材面）；缺省
 *        态不伪造可行性；formalExportAvailable 位仅 Completed 直译为
 *        true（TASK-02 呈现面——取消/草稿不呈现正式导出入口）。
 */
TEST(OptPanelReadiness, ReadinessProjectionTransfersSessionFacts_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10", "TASK-02"},
                  std::vector<std::string>{"AT-34"});

    // 缺省会话（零事实注入）→投影行如实呈现"无判定/不完整/Draft/无在途"。
    sdurws::ird::optimization::OptPanelModule module;
    const auto empty = readinessProjection(module.session);
    EXPECT_EQ(empty.domainKey, "optimization") << "域注册键＝§6.5 词表值";
    EXPECT_EQ(empty.verdict, core::EngineeringStatus::NotApplicable)
        << "无判定＝NotApplicable（不伪造可行性）";
    EXPECT_FALSE(empty.inputComplete);
    EXPECT_TRUE(empty.missingItemKeys.empty());
    EXPECT_FALSE(empty.hasActiveTask);
    EXPECT_EQ(empty.runPhase, RunPhase::Draft);
    EXPECT_FALSE(empty.cancelRequested);
    EXPECT_FALSE(empty.formalExportAvailable)
        << "Draft 态不呈现正式导出入口（TASK-02 呈现面）";

    // 事实注入→逐字段透传（判定权威在 Preflight/执行侧——投影零判定）。
    module.session.epoch = 7;
    module.session.inputComplete = true;
    module.session.hasActiveTask = true;
    module.session.cancelRequested = true;
    module.session.verdict = core::EngineeringStatus::DataInsufficient;
    module.session.runPhase = RunPhase::QuickScreening;
    module.session.missingItemKeys = {"panel.optimization.missing.req-points"};
    const auto row = readinessProjection(module.session);
    EXPECT_EQ(row.verdict, core::EngineeringStatus::DataInsufficient);
    EXPECT_TRUE(row.inputComplete);
    EXPECT_TRUE(row.hasActiveTask);
    EXPECT_TRUE(row.cancelRequested);
    EXPECT_EQ(row.runPhase, RunPhase::QuickScreening);
    ASSERT_EQ(row.missingItemKeys.size(), 1u);
    EXPECT_EQ(row.missingItemKeys[0], "panel.optimization.missing.req-points");
    EXPECT_FALSE(row.formalExportAvailable) << "计算中不呈现正式导出入口";

    // Completed 直译（唯一呈现正式导出的状态）。
    module.session.runPhase = RunPhase::Completed;
    EXPECT_TRUE(readinessProjection(module.session).formalExportAvailable);
    // Canceled 直译（取消结果不冒充完整——TASK-02/§10.6）。
    module.session.runPhase = RunPhase::Canceled;
    EXPECT_FALSE(readinessProjection(module.session).formalExportAvailable);
}

// =====================================================================
// OptPanelVariables 组——L-O2 变量表行集。
// =====================================================================

/**
 * @brief L-O2：绑定投影→行集黄金（直拷列＋词表两列）——"登记"与
 *        "阶段启用"两事实分列（AT-09 语义混同防线："登记了但阶段不
 *        支持"≠"未登记"）；单位符号随句柄（枚举无效句柄→空串素材）。
 */
TEST(OptPanelVariables, VariableRowsTransferBindingsAndStageEnable_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "OPT-02"},
                  std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> bindings = {
        makeLengthBinding(),       // DH a——词表命中＋StageB 启用
        makeRatioBinding(),        // 传动比——词表命中＋两阶段启用（V12-02）
        makeSectionTypeBinding(),  // 截面类型——词表命中＋StageB 不启用
        makeUnregisteredBinding(), // 未登记——registered=false
    };
    const std::vector<sdurws::ird::optimization::OptVariableRow> rows =
        variableTableRows(OptimizationStage::StageB, bindings);
    ASSERT_EQ(rows.size(), 4u);

    // 行 0（DH a，m）：直拷列＋启用位。
    EXPECT_EQ(rows[0].bindingId, "mdl.joint[2].dh.a");
    EXPECT_EQ(rows[0].kindToken, "continuous");
    EXPECT_EQ(rows[0].unitSymbol, "m");
    EXPECT_DOUBLE_EQ(rows[0].lowerBound, 0.2);
    EXPECT_DOUBLE_EQ(rows[0].upperBound, 0.8);
    EXPECT_DOUBLE_EQ(rows[0].step, 0.0);
    EXPECT_DOUBLE_EQ(rows[0].defaultValue, 0.5);
    EXPECT_FALSE(rows[0].locked);
    EXPECT_TRUE(rows[0].authorized);
    EXPECT_TRUE(rows[0].registered);
    EXPECT_TRUE(rows[0].stageEnabled) << "DH a 在 StageB 启用（§5.3 #2）";
    EXPECT_EQ(rows[0].authorityFieldPath, "robot-design/joints[2]/dh/a");

    // 行 1（传动比，无量纲"1"）：两阶段启用（V12-02——ratio 不被阶段锁拒）。
    EXPECT_EQ(rows[1].bindingId, "mdl.drivetrain.ratio[1]");
    EXPECT_EQ(rows[1].unitSymbol, "1");
    EXPECT_DOUBLE_EQ(rows[1].lowerBound, 60.0);
    EXPECT_TRUE(rows[1].stageEnabled);

    // 行 2（截面类型枚举）：登记命中但 StageB 不启用——两列如实分列。
    EXPECT_EQ(rows[2].bindingId, "mdl.link[1].section.type");
    EXPECT_EQ(rows[2].kindToken, "enumeration");
    EXPECT_EQ(rows[2].unitSymbol, "") << "枚举无效单位句柄→空串（呈现\"—\"）";
    EXPECT_TRUE(rows[2].registered) << "词表登记命中（§5.3 #1b 条目在册）";
    EXPECT_FALSE(rows[2].stageEnabled)
        << "P-OPT-4 裁决前 StageB 不启用——阶段锁事实列，语义≠未登记";
    EXPECT_TRUE(rows[2].locked) << "锁定位直拷（初始化面语义）";

    // 行 3（未登记）：registered=false＋阶段启用恒 false。
    EXPECT_FALSE(rows[3].registered) << "词表无此 token——未登记如实呈现";
    EXPECT_FALSE(rows[3].stageEnabled);
}

/**
 * @brief L-O2：空绑定→空行集（空态，不伪造行）；行序＝入参序（零重排
 *        ——研究定义侧 canonical 序是唯一权威，NFR-COR-02）。
 */
TEST(OptPanelVariables, VariableRowsEmptyAndOrderStable_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    // 空绑定→空行集。
    EXPECT_TRUE(variableTableRows(OptimizationStage::StageB, {}).empty());

    // 乱序入参→行序＝入参序（本函数零重排——排序唯一在研究定义面）。
    const std::vector<VariableBinding> shuffled = {makeRatioBinding(),
                                                   makeLengthBinding()};
    const auto rows = variableTableRows(OptimizationStage::StageB, shuffled);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].bindingId, "mdl.drivetrain.ratio[1]");
    EXPECT_EQ(rows[1].bindingId, "mdl.joint[2].dh.a");
}

// =====================================================================
// OptPanelConstraints 组——L-O3 约束页呈现。
// =====================================================================

/**
 * @brief L-O3：StageB＋真执行清单（stageBConstraintPlan——§6.2 全序
 *        十步）→dataState=ok＋行集 §6.2 序透传（序号 1 基＋token/依据
 *        列黄金）；横幅空（本函数零自产横幅）。
 */
TEST(OptPanelConstraints, StageBPlanRowsTransferInExecutionOrder_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"},
                  std::vector<std::string>{});

    sdurws::ird::optimization::OptModuleSessionState session;
    session.stage = OptimizationStage::StageB;
    // 真清单（计算库纯函数——§6.2 十步唯一清单书写点；呈现面零复制）。
    const std::vector<ConstraintSpec> plan = optimization::stageBConstraintPlan();
    ASSERT_EQ(plan.size(), 10u) << "§6.2 全序十步";

    const auto page = constraintPagePresentation(session, plan);
    EXPECT_EQ(page.dataState, "ok");
    EXPECT_TRUE(page.banners.empty())
        << "清单在位无横幅（阻塞横幅唯一来源是检查报告映射——L-O8）";
    ASSERT_EQ(page.planRows.size(), 10u);
    // 执行序黄金（首行＋两处③端口行＋尾行——序号 1 基＋依据评估键）。
    EXPECT_EQ(page.planRows[0].ordinal, 1);
    EXPECT_EQ(page.planRows[0].constraintToken,
              std::string(optimization::toToken(ConstraintId::InputPrecondition)));
    // Must 工位可达（第 8 步）——依据＝③端口评估键（T04 词表常量）。
    EXPECT_EQ(page.planRows[7].constraintToken,
              std::string(optimization::toToken(
                  ConstraintId::MustStationReachability)));
    EXPECT_EQ(page.planRows[7].evaluationKey,
              std::string(optimization::kKinTaskPointsBatchKey));
    // Must 区域覆盖（第 9 步）。
    EXPECT_EQ(page.planRows[8].evaluationKey,
              std::string(optimization::kKinRegionCoverageKey));
    // 静态碰撞（第 10 步）。
    EXPECT_EQ(page.planRows[9].constraintToken,
              std::string(optimization::toToken(ConstraintId::StaticCollision)));
    EXPECT_EQ(page.planRows[9].ordinal, 10);
}

/**
 * @brief L-O3：StageD→阶段锁横幅（OPT-STAGE-LOCKED——运行启动阻塞）
 *        ＋行集恒空；**缝返回非空清单也不被消费**（清单权威在约束编排
 *        面——StageD 拒绝语义见 §12.2，防宿主漂移静默入画）。
 */
TEST(OptPanelConstraints, StageDShowsStageLockBannerNotElimination_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03", "UX-10"},
                  std::vector<std::string>{"AT-35"});

    sdurws::ird::optimization::OptModuleSessionState session;
    session.stage = OptimizationStage::StageD;
    // 缝漂移防御面：StageD 下即使宿主误供非空清单，呈现仍按"未登记"。
    const std::vector<ConstraintSpec> driftedPlan =
        optimization::stageBConstraintPlan();
    const auto page = constraintPagePresentation(session, driftedPlan);
    EXPECT_EQ(page.dataState, "stage-locked");
    ASSERT_EQ(page.banners.size(), 1u);
    EXPECT_EQ(page.banners[0].diagToken,
              std::string(optimization::kOptStageLocked))
        << "阶段锁横幅＝OPT-STAGE-LOCKED 目录呈现（§6.5 呈现边界）";
    EXPECT_TRUE(page.planRows.empty())
        << "行集恒空——阶段锁绝不呈现为候选淘汰或清单行";
}

/**
 * @brief L-O3：StageB＋清单缝缺（nullopt）→not-assembled 空态＋空行集
 *        ＋空横幅（不伪造行、不伪造阻塞）。
 */
TEST(OptPanelConstraints, NotAssembledPlanShowsEmptyState_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"}, std::vector<std::string>{});

    sdurws::ird::optimization::OptModuleSessionState session;
    session.stage = OptimizationStage::StageB;
    const auto page = constraintPagePresentation(session, std::nullopt);
    EXPECT_EQ(page.dataState, "not-assembled");
    EXPECT_TRUE(page.planRows.empty());
    EXPECT_TRUE(page.banners.empty()) << "未装配≠阻塞——不伪造阻塞项";
}

// =====================================================================
// OptPanelRunControl 组——L-O4 进度漏斗＋L-O5 运行控制流（AT-34）。
// =====================================================================

/**
 * @brief L-O4：§9.1 八阶段词表钉扎＋漏斗三态黄金（evaluate-quick 为
 *        当前段——前两段 Done、后五段 Pending）＋标题键键族派生。
 *        AT-34"进度漏斗显示"的模型半区。
 */
TEST(OptPanelRunControl, ProgressFunnelEightPhasesGolden_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "UX-10"},
                  std::vector<std::string>{"AT-34"});

    // 词表钉扎（§9.1 phaseToken 八值——顺序即漏斗呈现序＝执行推进序）。
    ASSERT_EQ(kOptProgressPhaseTokens.size(), 8u);
    EXPECT_STREQ(kOptProgressPhaseTokens[0], "preflight");
    EXPECT_STREQ(kOptProgressPhaseTokens[1], "generate");
    EXPECT_STREQ(kOptProgressPhaseTokens[2], "compile");
    EXPECT_STREQ(kOptProgressPhaseTokens[3], "hard-constraints");
    EXPECT_STREQ(kOptProgressPhaseTokens[4], "evaluate-quick");
    EXPECT_STREQ(kOptProgressPhaseTokens[5], "evaluate-verified");
    EXPECT_STREQ(kOptProgressPhaseTokens[6], "pareto");
    EXPECT_STREQ(kOptProgressPhaseTokens[7], "export");

    // 样本（Quick 筛选执行中——批计数 2/5、40%）：漏斗八行三态黄金。
    sdurws::ird::optimization::OptProgressSample sample;
    sample.percent = 40;  // %（执行侧节流回报值直拷）
    sample.phaseToken = "evaluate-quick";
    sample.batchesDone = 2;
    sample.batchesTotal = 5;
    const std::vector<sdurws::ird::optimization::OptFunnelRow> rows =
        progressFunnelRows(sample);
    ASSERT_EQ(rows.size(), 8u);
    // 段态黄金：词表序 <4 Done、==4 Active、>4 Pending。
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto expected =
            i < 4
                ? sdurws::ird::optimization::OptFunnelState::Done
                : (i == 4 ? sdurws::ird::optimization::OptFunnelState::Active
                          : sdurws::ird::optimization::OptFunnelState::Pending);
        EXPECT_EQ(rows[i].state, expected) << "漏斗段 " << i;
        EXPECT_EQ(rows[i].phaseToken, std::string(kOptProgressPhaseTokens[i]));
        // 标题键键族（§3.5——plugin.optimization.phase.<token>.title）。
        EXPECT_EQ(rows[i].titleKey,
                  "plugin.optimization.phase." + rows[i].phaseToken + ".title");
    }

    // 首段进行中（preflight Active——其余全 Pending）与末段（export
    // Active——其余全 Done）的两端黄金。
    sample.phaseToken = "preflight";
    auto head = progressFunnelRows(sample);
    EXPECT_EQ(head[0].state, sdurws::ird::optimization::OptFunnelState::Active);
    EXPECT_EQ(head[1].state,
              sdurws::ird::optimization::OptFunnelState::Pending);
    EXPECT_EQ(head[7].state,
              sdurws::ird::optimization::OptFunnelState::Pending);
    sample.phaseToken = "export";
    auto tail = progressFunnelRows(sample);
    EXPECT_EQ(tail[6].state, sdurws::ird::optimization::OptFunnelState::Done);
    EXPECT_EQ(tail[7].state, sdurws::ird::optimization::OptFunnelState::Active);
}

/**
 * @brief L-O4：无在途任务（nullopt）→空漏斗（"无在途任务"空态）；
 *        词表外 token＝调用方契约违约 fail-fast（kOptInputInvalid——
 *        防呈现面静默容忍宿主漂移）。
 */
TEST(OptPanelRunControl, FunnelEmptyStateAndUnknownTokenFailFast_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"},
                  std::vector<std::string>{"AT-34"});

    // nullopt＝无在途任务→空集。
    EXPECT_TRUE(progressFunnelRows(std::nullopt).empty());

    // 词表外 token→OptimizationError(kOptInputInvalid)。
    sdurws::ird::optimization::OptProgressSample drifted;
    drifted.phaseToken = "not-a-phase";
    bool thrown = false;
    try {
        static_cast<void>(progressFunnelRows(drifted));
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    EXPECT_TRUE(thrown) << "词表外进度 token 必须 fail-fast";
}

/**
 * @brief L-O5：启动门控检查序（首错即返——确定性）＋缝转发受理；
 *        会话态零触（启动受理后的事实推进由装配层刷新注入）。
 */
TEST(OptPanelRunControl, RunStartGatesOrderAndSeamForwarding_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "PM-07"},
                  std::vector<std::string>{});

    sdurws::ird::optimization::OptPanelModule module;
    sdurws::ird::optimization::OptPanelServices bare;
    // 1. 缝未装配→"not-assembled"。
    EXPECT_EQ(requestRunStart(module.session, bare).rejectionToken,
              "not-assembled");

    // 2. 只读项目→"read-only"（PM-07 事实直译）。
    sdurws::ird::optimization::OptPanelServices seams;
    int startCalls = 0;
    seams.runStart = [&startCalls]() {
        ++startCalls;
        return true;
    };
    module.session.writable = false;
    EXPECT_EQ(requestRunStart(module.session, seams).rejectionToken,
              "read-only");
    EXPECT_EQ(startCalls, 0) << "门控拒绝不触缝";

    // 3. 在途任务→"active-task"。
    module.session.writable = true;
    module.session.hasActiveTask = true;
    EXPECT_EQ(requestRunStart(module.session, seams).rejectionToken,
              "active-task");

    // 4. 取消协作中→"cancel-pending"。
    module.session.hasActiveTask = false;
    module.session.cancelRequested = true;
    EXPECT_EQ(requestRunStart(module.session, seams).rejectionToken,
              "cancel-pending");
    module.session.cancelRequested = false;

    // 5. 检查报告在位且有阻塞→"blocked"（§6.5——横幅已在界面，零重复
    // 定位）。
    module.session.latestPreflight =
        reportWithFindings({{4, PreflightLevel::Blocking}});
    EXPECT_EQ(requestRunStart(module.session, seams).rejectionToken,
              "blocked");
    // 5b. 报告在位但 allowStart=true（仅警告）→放行到缝。
    module.session.latestPreflight =
        reportWithFindings({{19, PreflightLevel::Warning}});
    EXPECT_TRUE(requestRunStart(module.session, seams).accepted);
    EXPECT_EQ(startCalls, 1) << "放行路径恰一次缝转发";

    // 6. 缝拒绝→"rejected-by-host"（用户可见不受理，非异常）。
    sdurws::ird::optimization::OptPanelServices refusing;
    refusing.runStart = []() { return false; };
    const auto refused = requestRunStart(module.session, refusing);
    EXPECT_FALSE(refused.accepted);
    EXPECT_EQ(refused.rejectionToken, "rejected-by-host");

    // 会话态零触（本函数零写——门控事实源只读）。
    EXPECT_FALSE(module.session.cancelRequested);
    EXPECT_FALSE(module.session.hasActiveTask);
}

/**
 * @brief L-O5：取消门控＋受理置位＋**UX-03 零错误诊断**（取消前后
 *        横幅条目集不变——正常取消不属于错误、不产生错误诊断）。
 *        AT-34"取消协作生效"的呈现半区（状态位＋诊断面）。
 */
TEST(OptPanelRunControl, RunCancelGatesAndNeverErrors_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-01", "UX-03"},
                  std::vector<std::string>{"AT-34"});

    sdurws::ird::optimization::OptPanelModule module;
    sdurws::ird::optimization::OptPanelServices bare;
    // 1. 缝未装配→"not-assembled"。
    EXPECT_EQ(requestRunCancel(module.session, bare).rejectionToken,
              "not-assembled");

    // 2. 无在途任务→"no-active-task"。
    sdurws::ird::optimization::OptPanelServices seams;
    int cancelCalls = 0;
    seams.runCancel = [&cancelCalls]() {
        ++cancelCalls;
        return true;
    };
    EXPECT_EQ(requestRunCancel(module.session, seams).rejectionToken,
              "no-active-task");
    EXPECT_EQ(cancelCalls, 0) << "门控拒绝不触缝";

    // 3. 在途任务＋首次取消→受理＋cancelRequested 置位。
    module.session.hasActiveTask = true;
    module.session.latestPreflight =
        reportWithFindings({{4, PreflightLevel::Blocking}});  // 预置横幅素材
    const auto accepted = requestRunCancel(module.session, seams);
    EXPECT_TRUE(accepted.accepted);
    EXPECT_TRUE(accepted.rejectionToken.empty());
    EXPECT_TRUE(module.session.cancelRequested)
        << "呈现位置位（权威完成以运行状态推进为准）";
    EXPECT_EQ(cancelCalls, 1);

    // 4. 重复取消→"already-requested"（防重复提交——批边界生效前按钮灰）。
    EXPECT_EQ(requestRunCancel(module.session, seams).rejectionToken,
              "already-requested");
    EXPECT_EQ(cancelCalls, 1) << "重复请求不触缝";

    // UX-03 零错误诊断：取消受理后横幅条目集恰为预置检查阻塞一条
    // （取消不追加任何横幅/错误素材——阻塞横幅唯一来源是检查报告）。
    const auto bannersAfter = stageLockBannerItems(module.session);
    ASSERT_EQ(bannersAfter.size(), 1u);
    EXPECT_EQ(bannersAfter[0].checkId, 4);
    EXPECT_EQ(bannersAfter[0].suggestion, "修正研究定义后重新检查");
}

/**
 * @brief AT-34 链式用例（取消协作生效——编排队×呈现面双半区）：真两级
 *        编排＋批边界协作取消上下文 → 运行终态取消＋部分批保留 → 聚合
 *        工厂 → 候选表行集保留＋就绪行取消呈现（正式导出位 false——
 *        TASK-02）＋零错误横幅（UX-03）。
 */
TEST(OptPanelRunControl, CancelCooperationChainPreservesPartialRuns_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "TASK-01", "TASK-02"},
                  std::vector<std::string>{"AT-34"});

    // ---- 编排装配（真注册表＋替身——T06 fixture 形态自持精简）。 -----
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    StubJointLimitProbe jointProbe;
    StubCollisionProbe collisionProbe;
    StubCompiler compiler;
    ScriptedPanelProjector projector;
    optimization::ParetoFrontBuilder paretoBuilder;

    // 真 evidence Profile（kin v1.0 空必需项——完备性核对平凡通过）与
    // 两个脚本化 kin 评估器（③端口消费路径与生产一致）。
    evidence::RequiredEvidenceProfile kinProfile;
    kinProfile.profileId = "kin";
    kinProfile.version = "1.0";
    profiles.registerProfile(kinProfile);
    evaluators.registerEvaluator(
        std::make_unique<PanelKinFactory>(makeKinDescriptor(
            std::string(optimization::kKinTaskPointsBatchKey))),
        {});
    evaluators.registerEvaluator(
        std::make_unique<PanelKinFactory>(makeKinDescriptor(
            std::string(optimization::kKinRegionCoverageKey))),
        {});
    jointProbe.result.completed = true;
    collisionProbe.result.completed = true;

    // T04 管线＋T06 编排依赖面（管线以对象持有——声明序保证依赖先构造）。
    optimization::StaticHardConstraintDeps pipelineDeps;
    pipelineDeps.evaluators = &evaluators;
    pipelineDeps.jointLimitProbe = &jointProbe;
    pipelineDeps.collisionProbe = &collisionProbe;
    pipelineDeps.compiler = &compiler;
    pipelineDeps.producers = &evaluators;
    pipelineDeps.profiles = &profiles;
    optimization::StaticHardConstraintPipeline pipeline(OptimizationStage::StageB,
                                                        pipelineDeps);
    optimization::TwoStageOrchestratorDeps deps;
    deps.pipeline = &pipeline;
    deps.projector = &projector;
    deps.paretoBuilder = &paretoBuilder;
    deps.cache = nullptr;  // 无缓存会话（缓存链归契约测试——T06 口径）

    // ---- 冻结快照（SnapshotBuilder 唯一合法生产者——T06 形态精简）。 -
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(),
                        core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    evidence::ObjectRefEntry design;
    design.objectId = core::ObjectId::generate();
    design.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000a1");
    design.objectTypeToken = "robot-design";
    design.digest = design.contentVersion.bytes;
    builder.addObjectRef(design);
    evidence::PolicyRef policy;
    policy.policyContentIdentity = core::ContentIdentity{{4}};
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = core::ContentIdentity{{5}};
    builder.setNameMapRef(nameMap);
    for (int c = 0; c < 2; ++c) {
        evidence::CaseEntry entry;
        entry.caseId = core::ObjectId::generate();
        entry.label = "额定工况";
        entry.enabled = true;
        entry.mandatory = true;
        builder.addCase(entry);
    }
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    const evidence::AnalysisSnapshot snapshot =
        builder.build(PanelClosureSource{});

    // ---- 已校验配置（种子/预算/策略/目标——T06 形态）。 ---------------
    OptimizationConfiguration config;
    config.stage = OptimizationStage::StageB;
    config.seed = 7;
    config.budget.maxCandidates = 5;
    config.budget.maxVerifiedCandidates = 2;
    config.strategyId = std::string(optimization::kStrategySeededLhs);
    config.objectives =
        optimization::defaultObjectives(OptimizationStage::StageB);
    config.variables = {makeLengthBinding()};
    optimization::validateConfiguration(config);

    // ---- 冻结请求（身份/覆盖矩阵——两工况全执行）。 -------------------
    optimization::TwoStageRunRequest request;
    request.config = config;
    request.baselineRoot = core::ObjectId::generate();
    request.baselineCv = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000bb");
    request.snapshot = snapshot;
    request.task = {core::ProjectId::generate(), core::BranchId::generate(),
                    core::RevisionId::generate(), core::RunId::generate(),
                    core::AttemptId{1U}};
    request.profile.profileId = "kin";
    request.profile.version = "1.0";
    request.profile.contentIdentity =
        profiles.findProfile("kin", "1.0")->contentIdentity;
    evidence::CaseCoverageMatrix coverage;
    coverage.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const auto& entry : snapshot.caseSet.entries) {
        evidence::CaseCoverageEntry row;
        row.caseId = entry.caseId;
        row.status = evidence::CaseExecutionStatus::Executed;
        row.runId = core::RunId::generate();
        row.resultSliceId = core::ContentIdentity{{6}};
        coverage.entries.push_back(row);
    }
    request.coverage = coverage;
    request.quickCoverage = coverage;

    // ---- 批边界协作取消（放行 3 次查询后取消——TASK-01）。 ------------
    CancelAfterNContext cancelCtx(3);
    optimization::TwoStageEvaluationOrchestrator orchestrator(
        OptimizationStage::StageB, deps);
    const TwoStageRunResult orchestrated = orchestrator.run(request, cancelCtx);
    ASSERT_EQ(orchestrated.runPhase, RunPhase::Canceled)
        << "编排队：批边界协作取消→运行终态取消";
    EXPECT_FALSE(orchestrated.runCompleted);
    const std::size_t partialCount = orchestrated.quickRecords.size();
    EXPECT_GE(partialCount, 1u);
    EXPECT_LT(partialCount, 5u) << "取消后不得派发全部候选（批边界停止）";

    // ---- 聚合（T07 工厂——运行结果载体）。 ----------------------------
    const OptimizationRunResult runResult = optimization::assembleRunResult(
        optimization::OptimizationRunId::generate(),
        core::ProjectId::generate(), core::BranchId::generate(),
        core::RevisionId::generate(), snapshot.snapshotId, request.baselineRoot,
        request.baselineCv, config, orchestrated);
    EXPECT_EQ(runResult.candidates.size(), partialCount)
        << "聚合保留部分批（已回传批不丢——TASK-01）";
    EXPECT_FALSE(runResult.allowFormalExport)
        << "取消结果不具正式资格（TASK-02——资格位推导）";

    // ---- 呈现面（插件模型层——取消后的面板事实投影）。 ----------------
    sdurws::ird::optimization::OptModuleSessionState session;
    session.runPhase = RunPhase::Canceled;
    session.cancelRequested = true;
    session.hasActiveTask = false;
    const auto readiness = readinessProjection(session);
    EXPECT_FALSE(readiness.formalExportAvailable)
        << "取消后不呈现正式导出/应用入口（TASK-02 呈现面）";
    EXPECT_TRUE(readiness.cancelRequested);

    // 候选行集＝部分批保留（行序＝编排产出序透传——零重排；UX-02 标签）。
    const std::vector<sdurws::ird::optimization::OptCandidateRow> rows =
        candidateTableRows(runResult);
    ASSERT_EQ(rows.size(), partialCount);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].candidateIdCanonical,
                  runResult.candidates[i].candidateId.toCanonical())
            << "行序与身份逐位对齐（编排序透传）";
        EXPECT_EQ(rows[i].displayLabel, "候选 " + std::to_string(i + 1))
            << "UX-02 工程用语标签（身份规范文本零进用户文本）";
    }

    // 零错误横幅（UX-03——正常取消不产生错误诊断；无检查报告→空横幅）。
    EXPECT_TRUE(stageLockBannerItems(session).empty());
}

// =====================================================================
// OptPanelCandidates 组——L-O6 候选表行集。
// =====================================================================

/**
 * @brief L-O6：运行结果→候选行黄金（编排产出序透传零重排；呈现标签
 *        "候选 N"零哈希；八项指标槽位缺失显示"—"素材〔nullopt 透传〕；
 *        淘汰原因两源合并；命中回放位透传）。
 */
TEST(OptPanelCandidates, CandidateRowsTransferInOrchestrationOrder_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-07", "UX-02"},
                  std::vector<std::string>{"AT-09"});

    // 三候选黄金：基线（三项静态全）＋Quick 淘汰（缺裕量→"—"）＋Verified
    // 非支配（部分指标＋编排追加淘汰原因＋命中回放）。
    std::vector<TwoStageRunRecord> records;
    records.push_back(makeRecord(
        true, CandidateStatus::Feasible,
        makeMetrics({{2.4, 36.5, 0.41, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt}})));
    records.push_back(makeRecord(
        false, CandidateStatus::ScreenedOut,
        makeMetrics({{2.9, 36.5, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, std::nullopt}}),
        true, {"opt.reject.quick-screen-budget"}));
    TwoStageRunRecord third = makeRecord(
        false, CandidateStatus::ParetoNondominated,
        makeMetrics({{2.1, 36.5, 0.52, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt}}));
    third.cacheHit = true;
    optimization::RejectionReason extra;
    extra.reasonToken = "opt.reject.candidate-compile-failed";
    third.extraRejections.push_back(extra);
    records.push_back(std::move(third));

    const OptimizationRunResult result = makeRunResult(std::move(records));
    const std::vector<sdurws::ird::optimization::OptCandidateRow> rows =
        candidateTableRows(result);
    ASSERT_EQ(rows.size(), 3u);

    // 行序＝编排产出序（零重排）＋呈现标签（UX-02——身份规范文本零进
    // 用户文本：displayLabel 恒"候选 N"工程用语）。
    EXPECT_EQ(rows[0].displayLabel, "候选 1");
    EXPECT_EQ(rows[1].displayLabel, "候选 2");
    EXPECT_EQ(rows[2].displayLabel, "候选 3");
    EXPECT_TRUE(rows[0].isBaseline);
    EXPECT_FALSE(rows[1].isBaseline);

    // 状态词 token＋筛选专用位＋资格位。
    EXPECT_EQ(rows[0].statusToken,
              std::string(optimization::toToken(CandidateStatus::Feasible)));
    EXPECT_TRUE(rows[1].screeningOnly) << "Quick 记录筛选专用位透传";
    EXPECT_FALSE(rows[1].formalPassEligible);

    // 八项指标槽位（枚举序对位；缺失＝nullopt"—"素材——绝不按 0 合成，
    // NFR-COR-03；StageB 五项 D-only 恒"—"——OPT-07 全展示）。
    ASSERT_TRUE(rows[0].metricValues[0].has_value());
    EXPECT_DOUBLE_EQ(*rows[0].metricValues[0], 2.4);
    EXPECT_FALSE(rows[1].metricValues[2].has_value())
        << "缺失裕量槽位＝\"—\"素材（nullopt 透传，不冒充 0）";
    ASSERT_TRUE(rows[2].metricValues[2].has_value());
    EXPECT_DOUBLE_EQ(*rows[2].metricValues[2], 0.52);
    for (std::size_t m = 4; m < 8; ++m) {
        EXPECT_FALSE(rows[0].metricValues[m].has_value())
            << "StageB 五项 D-only 指标恒\"—\"（OPT-07 全展示）";
    }

    // 淘汰原因两源合并（记录面 rejections＋编排层 extraRejections）。
    ASSERT_EQ(rows[1].rejectionReasonTokens.size(), 1u);
    EXPECT_EQ(rows[1].rejectionReasonTokens[0],
              "opt.reject.quick-screen-budget");
    ASSERT_EQ(rows[2].rejectionReasonTokens.size(), 1u);
    EXPECT_EQ(rows[2].rejectionReasonTokens[0],
              "opt.reject.candidate-compile-failed");

    // 命中回放位（审计透传——命中≠Current，两套语义不混同）。
    EXPECT_FALSE(rows[0].cacheHit);
    EXPECT_TRUE(rows[2].cacheHit);
}

/**
 * @brief L-O6：阶段锁呈现边界（§6.5——acceptance 2 的机器可断言面）：
 *        候选行淘汰原因取值域**不含阶段锁码**——阶段锁是运行启动阻塞，
 *        绝不进入候选淘汰列（全淘汰原因谱系行集逐行扫描钉扎）。
 */
TEST(OptPanelCandidates, RejectionDomainDisjointFromStageLock_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"}, std::vector<std::string>{"AT-34"});

    // 全淘汰原因谱系行集（记录面＋编排两源全 token）→行集扫描：任一行
    // 淘汰原因不与阶段锁码相交。
    std::vector<TwoStageRunRecord> records;
    records.push_back(makeRecord(
        false, CandidateStatus::Infeasible,
        makeMetrics({{std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, std::nullopt, std::nullopt}}),
        false,
        {"opt.reject.var-locked", "opt.reject.patch-illegal",
         "opt.reject.joint-limit", "opt.reject.collision-sample",
         "opt.reject.topology-out-of-scope"}));
    TwoStageRunRecord failed = makeRecord(
        false, CandidateStatus::EvaluationFailed,
        makeMetrics({{std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, std::nullopt, std::nullopt}}));
    optimization::RejectionReason compileFailed;
    compileFailed.reasonToken = "opt.reject.candidate-compile-failed";
    failed.extraRejections.push_back(compileFailed);
    records.push_back(std::move(failed));

    const OptimizationRunResult result = makeRunResult(std::move(records));
    for (const auto& row : candidateTableRows(result)) {
        for (const std::string& token : row.rejectionReasonTokens) {
            EXPECT_NE(token, std::string(optimization::kOptStageLocked))
                << "阶段锁码绝不进候选淘汰列（§6.5 呈现边界——启动阻塞"
                   "非候选淘汰）";
        }
    }
}

// =====================================================================
// OptPanelCompare 组——L-O7 候选对比。
// =====================================================================

/**
 * @brief L-O7：两候选八指标对比黄金（方向感知优势位／差值呈现／缺失
 *        不伪造；八行全量——OPT-07 全展示；方向/单位唯一来源＝指标
 *        定义词表对账）。
 */
TEST(OptPanelCompare, ComparisonRowsGoldenDeltaDirectionAndMissing_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "OPT-07"},
                  std::vector<std::string>{});

    // A：包络大、裕量小；B：包络小、裕量大、质量缺失（"—"）。
    std::vector<TwoStageRunRecord> records;
    records.push_back(makeRecord(
        true, CandidateStatus::Feasible,
        makeMetrics({{2.4, 36.5, 0.41, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt}})));
    records.push_back(makeRecord(
        false, CandidateStatus::ParetoNondominated,
        makeMetrics({{2.1, std::nullopt, 0.52, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, std::nullopt}})));
    const OptimizationRunResult result = makeRunResult(std::move(records));

    const std::vector<sdurws::ird::optimization::OptComparisonRow> rows =
        candidateComparison(result, 0, 1);
    ASSERT_EQ(rows.size(), 8u) << "八项全量展示（OPT-07）";

    // 行 0（尺寸包络，最小化）：B 更小→B 优；差值＝B−A＝−0.3；高亮。
    EXPECT_EQ(rows[0].metricToken,
              std::string(optimization::toToken(MetricId::Envelope)));
    EXPECT_TRUE(rows[0].minimize);
    ASSERT_TRUE(rows[0].valueA.has_value());
    ASSERT_TRUE(rows[0].valueB.has_value());
    ASSERT_TRUE(rows[0].delta.has_value());
    EXPECT_DOUBLE_EQ(*rows[0].delta, -0.3) << "差值＝B−A（呈现减法）";
    EXPECT_TRUE(rows[0].differs);
    ASSERT_TRUE(rows[0].bBetter.has_value());
    EXPECT_TRUE(*rows[0].bBetter) << "最小化方向：B 小者优";

    // 行 1（结构质量，kg，最小化）：B 侧缺失→全三态不伪造（"—"/不高亮/
    // 不可判）。
    EXPECT_EQ(rows[1].unitToken, "kg");
    EXPECT_FALSE(rows[1].valueB.has_value());
    EXPECT_FALSE(rows[1].delta.has_value());
    EXPECT_FALSE(rows[1].differs);
    EXPECT_FALSE(rows[1].bBetter.has_value()) << "缺侧不伪造优势结论";

    // 行 2（最小关节裕量，最大化）：B 更大→B 优（方向感知翻转）。
    EXPECT_FALSE(rows[2].minimize);
    ASSERT_TRUE(rows[2].delta.has_value());
    EXPECT_DOUBLE_EQ(*rows[2].delta, 0.11);
    ASSERT_TRUE(rows[2].bBetter.has_value());
    EXPECT_TRUE(*rows[2].bBetter) << "最大化方向：B 大者优";

    // 行 3~7（StageB 恒"—"）：两侧均缺→不可判。
    for (std::size_t m = 3; m < 8; ++m) {
        EXPECT_FALSE(rows[m].valueA.has_value());
        EXPECT_FALSE(rows[m].valueB.has_value());
        EXPECT_FALSE(rows[m].bBetter.has_value());
    }

    // 词表对账（方向/单位唯一来源＝计算库指标定义词表——零复制）。
    const auto definitions = optimization::metricDefinitions();
    ASSERT_EQ(definitions.size(), 8u);
    for (std::size_t m = 0; m < 8; ++m) {
        EXPECT_EQ(rows[m].metricToken,
                  std::string(optimization::toToken(definitions[m].metricId)));
        EXPECT_EQ(rows[m].unitToken, std::string(definitions[m].unitToken));
        EXPECT_EQ(rows[m].minimize,
                  definitions[m].direction
                      == optimization::MetricDirection::Minimize);
    }
}

/**
 * @brief L-O7：越界/同候选下标＝调用方契约违约 fail-fast
 *        （std::invalid_argument——对比自身无意义）。
 */
TEST(OptPanelCompare, ComparisonInvalidIndicesFailFast_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    std::vector<TwoStageRunRecord> records;
    records.push_back(makeRecord(
        true, CandidateStatus::Feasible,
        makeMetrics({{std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, std::nullopt, std::nullopt}})));
    const OptimizationRunResult result = makeRunResult(std::move(records));

    EXPECT_THROW(static_cast<void>(candidateComparison(result, 0, 5)),
                 std::invalid_argument)
        << "越界下标 fail-fast";
    EXPECT_THROW(static_cast<void>(candidateComparison(result, 0, 0)),
                 std::invalid_argument)
        << "同候选对比 fail-fast";
}

// =====================================================================
// OptPanelStageLock 组——L-O8 阶段锁横幅（§6.5 呈现边界）。
// =====================================================================

/**
 * @brief L-O8：检查报告 Blocking 发现→横幅条目逐字透传（checkId/subject/
 *        suggestion；目录呈现码＝OPT-PREFLIGHT-BLOCKED）；Warning 级不进
 *        横幅；报告缺省→空集（"尚未检查"提示，不伪造阻塞项）。
 */
TEST(OptPanelStageLock, BlockingFindingsBecomeBannerItems_WP20T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10", "OPT-11"},
                  std::vector<std::string>{"AT-34"});

    sdurws::ird::optimization::OptModuleSessionState session;

    // 报告缺省→空条目集。
    EXPECT_TRUE(stageLockBannerItems(session).empty());

    // 混合级别报告：#4/#13 Blocking、#19 Warning——横幅恰两条（Warning
    // 走缺项清单区不进横幅）。
    session.latestPreflight = reportWithFindings(
        {{4, PreflightLevel::Blocking}, {13, PreflightLevel::Blocking},
         {19, PreflightLevel::Warning}});
    const auto banners = stageLockBannerItems(session);
    ASSERT_EQ(banners.size(), 2u);
    EXPECT_EQ(banners[0].diagToken,
              std::string(optimization::kOptPreflightBlocked))
        << "目录呈现码（§6.4——按 blockerCount 汇总产出）";
    EXPECT_EQ(banners[0].checkId, 4);
    EXPECT_EQ(banners[0].subject, "obj-finding-4");
    EXPECT_EQ(banners[0].suggestion, "修正研究定义后重新检查");
    EXPECT_EQ(banners[1].checkId, 13);
    // 报告序透传（发现序＝checkId 升序——T08 排序契约）。
    EXPECT_LT(banners[0].checkId, banners[1].checkId);
}

// =====================================================================
// OptPanelText 组——L-O9 文案解析流。
// =====================================================================

/**
 * @brief L-O9：缝解析＋空缝键名兜底＋哈希形态回退（UX-02"零哈希进
 *        用户文本"——64 位十六进制串按泄漏处置回退键名）。
 */
TEST(OptPanelText, ResolveTextFallsBackAndGuardsDigestLeak_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, std::vector<std::string>{});

    // 缝解析原值。
    sdurws::ird::optimization::OptPanelServices services;
    services.textResolver = [](const std::string& key) {
        return key == "plugin.optimization.panel.variables.title"
                   ? std::string("变量表")
                   : key;
    };
    bool fellBack = true;
    EXPECT_EQ(resolvePanelText(services,
                               "plugin.optimization.panel.variables.title",
                               &fellBack),
              "变量表");
    EXPECT_FALSE(fellBack);

    // 空缝→键名兜底（开发态可见缺口）。
    sdurws::ird::optimization::OptPanelServices bare;
    fellBack = false;
    EXPECT_EQ(
        resolvePanelText(bare, "plugin.optimization.action.run-start.title",
                         &fellBack),
        "plugin.optimization.action.run-start.title");
    EXPECT_TRUE(fellBack);

    // 解析结果哈希形态→回退键名（泄漏守卫）。
    sdurws::ird::optimization::OptPanelServices leaky;
    leaky.textResolver = [](const std::string&) {
        return std::string(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    };
    fellBack = false;
    EXPECT_EQ(resolvePanelText(leaky,
                               "plugin.optimization.panel.run-control.title",
                               &fellBack),
              "plugin.optimization.panel.run-control.title");
    EXPECT_TRUE(fellBack) << "哈希形态解析值不进用户文本";
}

// =====================================================================
// OptPanelCatalog 组——装配登记面（描述符/门面绑定/键族）。
// =====================================================================

/**
 * @brief 装配面：描述符承载完整登记面（T02 两字段逐字保留＋T10 挂位/
 *        域键/面板记录）；pluginId 落于宿主白名单词表（ui.md §11.1 八
 *        token——本测试自持词表常量对账，零 ui 头包含——P-OPT-10 缺口
 *        的对账半区）。
 */
TEST(OptPanelCatalog, AssemblyDescriptorCarriesRegistrationFace_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10"},
                  std::vector<std::string>{});

    // 白名单词表（ui.md §11.1——编译期常量词表的测试侧对账锚；真实
    // 运行时载体归宿主装配层，本单元零 ui 编译边——P-OPT-10 缺口）。
    constexpr const char* kUiWhitelist[8] = {
        "modeling", "requirements", "kinematics", "trajectory",
        "dynamics", "selection", "optimization", "workflow"};

    const auto bundle =
        sdurws::ird::optimization::createOptimizationPluginAssembly();
    // T02 登记值逐字保留（既有契约用例同口径）。
    EXPECT_EQ(bundle.descriptor.pluginId, "optimization");
    EXPECT_EQ(bundle.descriptor.titleKey, "plugin.optimization.title");
    bool whitelisted = false;
    for (const char* token : kUiWhitelist) {
        whitelisted = whitelisted || bundle.descriptor.pluginId == token;
    }
    EXPECT_TRUE(whitelisted) << "pluginId 落于 §11.1 白名单词表";

    // T10 挂位/域键/面板面。
    EXPECT_EQ(bundle.descriptor.stageToken, "optimization")
        << "StageId::Optimization 的 §6.4 token";
    EXPECT_EQ(bundle.descriptor.readinessDomainKey, "optimization");
    ASSERT_EQ(bundle.descriptor.panels.size(), 1u)
        << "四页合一主面板（恰一条登记记录）";
    const auto& panel = bundle.descriptor.panels[0];
    EXPECT_EQ(panel.stageToken, "optimization");
    EXPECT_EQ(panel.titleKey, "plugin.optimization.panel.title");
    EXPECT_FALSE(panel.advanced) << "主面板位（UX-04 非 advanced）";
    EXPECT_TRUE(static_cast<bool>(panel.factory)) << "工厂闭包非空";
}

/**
 * @brief 装配面：四页标题键键族黄金（页序＝DTB 输出列序：变量表/约束页/
 *        运行控制/候选表与对比）——键族形态 plugin.optimization.panel.
 *        <页>.title（§3.5）。
 */
TEST(OptPanelCatalog, PanelPageKeysFollowKeyFamily_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, std::vector<std::string>{});

    ASSERT_EQ(kOptPanelPageKeys.size(), 4u);
    EXPECT_STREQ(kOptPanelPageKeys[0],
                 "plugin.optimization.panel.variables.title");
    EXPECT_STREQ(kOptPanelPageKeys[1],
                 "plugin.optimization.panel.constraints.title");
    EXPECT_STREQ(kOptPanelPageKeys[2],
                 "plugin.optimization.panel.run-control.title");
    EXPECT_STREQ(kOptPanelPageKeys[3],
                 "plugin.optimization.panel.candidates.title");
}

/**
 * @brief 装配面：bind 系／setServices 转发到模块缝（缝注入的回读验证）
 *        ＋session() 访问与刷新转发（面板未创建＝空操作不崩溃）。
 */
TEST(OptPanelCatalog, AssemblyBindsDriveModuleServices_WP20T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, std::vector<std::string>{});

    auto bundle = sdurws::ird::optimization::createOptimizationPluginAssembly();
    ASSERT_NE(bundle.module(), nullptr);

    // 会话态注入（装配层路径）。
    bundle.session().epoch = 9;
    bundle.session().writable = false;
    EXPECT_EQ(bundle.session().epoch, 9u);

    // 服务缝整体注入（setServices——转发面回读）。
    sdurws::ird::optimization::OptPanelServices services;
    int startCalls = 0;
    int cancelCalls = 0;
    services.runStart = [&startCalls]() {
        ++startCalls;
        return true;
    };
    services.runCancel = [&cancelCalls]() {
        ++cancelCalls;
        return true;
    };
    bundle.setServices(services);
    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.runStart));
    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.runCancel));
    // setServices 注入面生效：模块缝直调计数递增。
    EXPECT_TRUE(bundle.module()->services.runStart());
    EXPECT_TRUE(bundle.module()->services.runCancel());
    EXPECT_EQ(startCalls, 1);
    EXPECT_EQ(cancelCalls, 1);
    // bind 覆盖语义（后绑定替换同缝——dynamics 同纪律）：覆盖后模块缝
    // 为绑定函数，原计数缝不再增长。
    bundle.bindRunStart([]() { return false; });
    bundle.bindRunCancel([]() { return false; });
    EXPECT_FALSE(bundle.module()->services.runStart())
        << "bind 后同缝被覆盖（返回绑定函数值）";
    EXPECT_FALSE(bundle.module()->services.runCancel());
    EXPECT_EQ(startCalls, 1) << "覆盖后原缝不再被调用";
    bundle.bindTextResolver([](const std::string& key) { return "值:" + key; });
    EXPECT_EQ(bundle.module()->services.textResolver("k"), "值:k")
        << "文案缝经 bind 注入生效";

    // 会话刷新（面板未创建＝空操作——不崩溃）。
    bundle.refreshFromSession();
}

// =====================================================================
// OptPanelGui 组——GUI 呈现边界（诚实登记：envUnavailable，非通过）。
// =====================================================================

/**
 * @brief GUI 呈现不在本目标（无人值守门禁不做 GUI 运行验证——既有环
 *        境事实）：widget 渲染/交互点击归宿主装配批次的开发 harness
 *        手动验证通道（单元卡 §13.3 Windows GUI 测试约束——本节只设计
 *        流程，不启动 GUI 程序），本次未启动、未留截图——如实登记为
 *        环境不可用，不标注通过（AGENTS §4.2）。
 */
TEST(OptPanelGui, WidgetPresentationDeferredToHarnessEnvUnavailable)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10"},
                  std::vector<std::string>{});

    GTEST_SKIP()
        << "envUnavailable：无人值守门禁不做 GUI 运行验证——四页主面板"
           "（变量表/约束页/运行控制/候选表与对比）的呈现与交互由宿主"
           "装配批次的开发 harness 手动点验承载（units/optimization.md "
           "§13.3 GUI 约束），本次未启动，如实登记不标注通过";
}
