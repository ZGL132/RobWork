/**
 * @file   Constraint.cpp
 * @brief  静态硬约束编排实现——R1 约束清单/阶段锁解析/约束事实记录与
 *         静态硬约束先行管线（WP-20-T04；设计依据见 Constraint.hpp 文件头）。
 *
 * 实现口径登记（DTB §5.4——均为卡面语义的显式化，不改语义）：
 *   I-C1 淘汰原因词表仅含两 token（硬约束违例/编译失败）——DataInsufficient
 *        与 EvaluationFailed 是候选状态而非淘汰原因（卡 §7.5 区分表：
 *        状态词不同不互相冒充），不落原因 token；
 *   I-C2 候选编译失败映射 EvaluationFailed（非工程判定）——卡 §6.2"候选
 *        淘汰"的终结语义（不进可行集）由状态与淘汰原因共同达成；分类轨
 *        取 §6.6"OPT-CANDIDATE-COMPILE-FAILED 归 execution-failed（环境
 *        面）"的环境错误轨（§7.5 区分表"评估器失败≠约束失败"同型）；
 *   I-C3 管线自身不新造 core::DiagnosticRecord（稳定码实例产出须经
 *        diagnostics 工厂消费已注册码——DiagCodes.hpp 头注纪律；管线级
 *        语义经 ConstraintFact.detail/RejectionReason 承载，域诊断原样
 *        透传不加工）；
 *   I-C4 两个③端口评估器都返回搜索未果记录时，透传先到达者（执行序：
 *        可达先于覆盖——卡 §6.2 顺序即稳定性依据；汇总层对多域记录的
 *        合并语义归 evidence，管线不合成第二份）；
 *   I-C5 评估输出 proof 与输入外来 proof 并存时取评估输出（③端口产物
 *        绑定本候选评估，因果更近；有效性校验在 aggregateVerdict——D-09）；
 *   I-C6 候选级失败（锁定/补丁/编译/限位评估失败/评估器异常）即终止后续
 *        步骤（硬约束先行：失败即不进可行集，后续约束无评估意义——短路
 *        不改变结论，§6.2 顺序保证的前提）。
 */

#include <sdurws/ird/optimization/Constraint.hpp>

#include <cstdio>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/evidence/Errors.hpp>     // evidence::EvidenceError——③端口异常轨捕获
#include <sdurws/ird/evidence/Verdict.hpp>    // aggregateVerdict/checkFormalPassEligibility
#include <sdurws/ird/optimization/DiagCodes.hpp>  // OPT-* 码值常量（唯一书写点）

namespace sdurws::ird::optimization {

namespace {

// ---------------------------------------------------------------------
// 局部辅助：约束 token 的 slug 形态（淘汰原因 sourceId 复用约束 token——
// 统一从 toToken 取，禁字符串拼码）。
// ---------------------------------------------------------------------

/// 把人读数值格式化为比较型文本（%.17g——NFR-COR-03 位模式保真惯例）。
std::string formatValue(double v)
{
    // 定长缓冲：double 最长表示约 24 字符，32 字节充分；C 标准转换与
    // locale 无关（"C" locale 下 %g 语义固定——确定性 NFR-COR-02）。
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

/// 判定发现级别 token 是否 Must 级（policy PolicyRuleLevel 投影词表）。
bool isMustLevel(const std::string& levelToken)
{
    return levelToken == "must";
}

}  // namespace

// =====================================================================
// 约束词表 token
// =====================================================================

std::string_view toToken(ConstraintId id) noexcept
{
    // 约束稳定 token（"opt.constraint.<slug>"；枚举值序＝§6.2 执行序——
    // 顺序即登记契约，冻结不重排）。
    switch (id) {
    case ConstraintId::InputPrecondition:
        return "opt.constraint.input-precondition";
    case ConstraintId::StageCapability:
        return "opt.constraint.stage-capability";
    case ConstraintId::VariableLock:
        return "opt.constraint.variable-lock";
    case ConstraintId::PatchValidity:
        return "opt.constraint.patch-validity";
    case ConstraintId::CandidateCompile:
        return "opt.constraint.candidate-compile";
    case ConstraintId::Topology:
        return "opt.constraint.topology";
    case ConstraintId::JointLimit:
        return "opt.constraint.joint-limit";
    case ConstraintId::MustStationReachability:
        return "opt.constraint.must-station-reachability";
    case ConstraintId::MustRegionCoverage:
        return "opt.constraint.must-region-coverage";
    case ConstraintId::StaticCollision:
        return "opt.constraint.static-collision";
    }
    // 不可达分支（词表封闭——Types.cpp 同款防御式空串，不吞错）。
    return {};
}

std::string_view toToken(JointConstraintFamily f) noexcept
{
    // 联合约束家族 token（引用词表——R1 引用即拒的判定词面；WP-21-T02
    // 冻结联合管线时保持不变，可执行约束另行落 ConstraintId）。
    switch (f) {
    case JointConstraintFamily::Trajectory:
        return "opt.constraint.trajectory";
    case JointConstraintFamily::Dynamics:
        return "opt.constraint.dynamics";
    case JointConstraintFamily::DrivetrainPerformance:
        return "opt.constraint.drivetrain-performance";
    case JointConstraintFamily::DeviceJoint:
        return "opt.constraint.device-joint";
    }
    return {};
}

std::string_view toToken(ConstraintVerdict v) noexcept
{
    // 约束判定 token（§8.3 第 7 步四值——候选表约束列/导出的书写词面）。
    switch (v) {
    case ConstraintVerdict::Satisfied:
        return "satisfied";
    case ConstraintVerdict::Violated:
        return "violated";
    case ConstraintVerdict::NotApplicable:
        return "not-applicable";
    case ConstraintVerdict::DataInsufficient:
        return "data-insufficient";
    }
    return {};
}

// =====================================================================
// R1 内置约束执行清单（§6.2 全序）
// =====================================================================

std::vector<ConstraintSpec> stageBConstraintPlan()
{
    // §6.2 R1 管线十步——顺序即执行序（本函数是唯一清单书写点，管线与
    // 编排服务共用；评估键栏：③端口消费项填注册键，④端口/本地项填依据
    // 标识或空——ConstraintSpec.evaluationKey 字段注）。
    return {
        {ConstraintId::InputPrecondition, {}},                        // ① 输入前置（Preflight 归 T08）
        {ConstraintId::StageCapability, {}},                          // ② 阶段锁（§5.7）
        {ConstraintId::VariableLock, {}},                             // ③ 变量边界和锁定（§5.4）
        {ConstraintId::PatchValidity, {}},                            // ④ 补丁合法性（§5.5）
        {ConstraintId::CandidateCompile, {}},                         // ⑤ 候选编译（runtime 链）
        {ConstraintId::Topology, {}},                                 // ⑥ 拓扑/模型合法性
        {ConstraintId::JointLimit, {}},                               // ⑦ 关节限位（policy）
        {ConstraintId::MustStationReachability, std::string(kKinTaskPointsBatchKey)}, // ⑧ Must 可达（③端口）
        {ConstraintId::MustRegionCoverage, std::string(kKinRegionCoverageKey)},       // ⑨ Must 覆盖（③端口）
        {ConstraintId::StaticCollision,
         "policy.collision-session"},                                 // ⑩ 静态碰撞（④端口会话）
    };
}

// =====================================================================
// 约束编排服务（阶段锁解析——OPT-STAGE-LOCKED 的执行点）
// =====================================================================

std::vector<ConstraintSpec>
OptimizationConstraintProvider::constraintsFor(OptimizationStage stage) const
{
    // 阶段锁（卡 §5.7/§6.5）：StageD 联合约束清单随 WP-21-T02 增量修订
    // 登记（P-04 冻结＋联合评估器落位为前置，卡 §13.2 表头）——本任务
    // 不预建清单（DOPT-4 同口径），请求即运行启动阻塞拒绝。
    if (stage == OptimizationStage::StageD) {
        throw OptimizationError(
            kOptStageLocked,
            "阶段 stage-d 的联合约束清单尚未登记（P-04 冻结＋WP-21-T02 "
            "联合管线落位为前置）——运行启动阻塞（§6.5：不静默降级，"
            "不呈现为候选淘汰）");
    }
    // StageB → §6.2 R1 全序内置清单（纯函数）。
    return stageBConstraintPlan();
}

std::vector<ConstraintSpec>
OptimizationConstraintProvider::resolveConstraintPlan(
    OptimizationStage stage, const std::vector<std::string>& requestedTokens) const
{
    // 第一步：联合约束家族引用检查（OPT-VER-109~111 的执行点——StageB
    // 引用联合约束即 OPT-STAGE-LOCKED，**不静默降级**：不是从清单剔除、
    // 不是返回空表，是运行启动阻塞——§6.5 呈现边界）。
    if (stage == OptimizationStage::StageB) {
        for (const auto& token : requestedTokens) {
            for (const auto family :
                 {JointConstraintFamily::Trajectory, JointConstraintFamily::Dynamics,
                  JointConstraintFamily::DrivetrainPerformance,
                  JointConstraintFamily::DeviceJoint}) {
                if (token == toToken(family)) {
                    throw OptimizationError(
                        kOptStageLocked,
                        "阶段 stage-b 引用联合约束族 " + token
                            + "（轨迹/动力学/drivetrain 性能/器件联合归 OPT-D，"
                              "OPT-03 §15.0 分期）——运行启动阻塞（§6.5）");
                }
            }
        }
    }

    // 第二步：R1 词表内子集解析（未知 token → OPT-INPUT-INVALID——与
    // "登记了但阶段不支持"语义分立，防 AT-09 同型语义混同）。
    const auto full = stageBConstraintPlan();
    if (requestedTokens.empty()) {
        // 空激活集＝缺省全启用（研究定义未显式声明约束时的保守全序）。
        return full;
    }
    std::vector<ConstraintSpec> resolved;
    resolved.reserve(full.size());
    for (const auto& spec : full) {
        const auto token = toToken(spec.constraintId);
        for (const auto& requested : requestedTokens) {
            if (requested == token) {
                resolved.push_back(spec);
                break;  // 集合语义：同一约束只出现一次（保持 §6.2 序）
            }
        }
    }
    // 词表覆盖核对：requested 中未命中 R1 词表的 token——要么联合家族
    // （StageD 语境放行语用检查由 WP-21 承接，本实现 StageD 亦拒——
    // 联合清单未登记），要么未知（书写错误）。
    for (const auto& requested : requestedTokens) {
        bool known = false;
        for (const auto& spec : resolved) {
            if (requested == toToken(spec.constraintId)) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        // 未命中：区分"联合家族 token"（StageD 语境下语义合法但清单未登记
        // ——仍 OPT-STAGE-LOCKED）与"未知 token"（OPT-INPUT-INVALID）。
        bool isFamily = false;
        for (const auto family :
             {JointConstraintFamily::Trajectory, JointConstraintFamily::Dynamics,
              JointConstraintFamily::DrivetrainPerformance,
              JointConstraintFamily::DeviceJoint}) {
            if (requested == toToken(family)) {
                isFamily = true;
                break;
            }
        }
        if (isFamily) {
            // StageD 引用联合家族：约束词表本身合法，但联合执行清单未随
            // 本任务登记（WP-21-T02）——同一阶段锁码面（§5.7 StageD 行
            // 的前置未就绪形态）。
            throw OptimizationError(
                kOptStageLocked,
                "联合约束族 " + requested
                    + " 的执行清单尚未登记（WP-21-T02 增量修订前置）——"
                      "运行启动阻塞（§6.5）");
        }
        throw OptimizationError(
            kOptInputInvalid,
            "未知约束 token：\"" + requested
                + "\"（R1/联合家族词表均未登记——研究定义书写错误，"
                  "ERR-01 调用方可修复）");
    }
    return resolved;
}

// =====================================================================
// 静态硬约束先行管线
// =====================================================================

StaticHardConstraintPipeline::StaticHardConstraintPipeline(OptimizationStage stage,
                                                           StaticHardConstraintDeps deps)
    : m_stage(stage), m_deps(deps)
{
    // 装配校验（构造期 fail-fast——装配违约不是运行期错误）：③端口注册表/
    // 限位探针/候选编译/两注册表投影为必填；碰撞探针可空（V13-01：策略
    // 未启用碰撞＝会话不在场，是合法装配态不是违约）。
    if (m_deps.evaluators == nullptr || m_deps.jointLimitProbe == nullptr
        || m_deps.compiler == nullptr || m_deps.producers == nullptr
        || m_deps.profiles == nullptr) {
        throw std::invalid_argument(
            "StaticHardConstraintPipeline 装配违约：deps 必填项为空"
            "（evaluators/jointLimitProbe/compiler/producers/profiles 非空；"
            "collisionProbe 可空＝策略未启用碰撞——V13-01）");
    }
}

CandidateEvaluationRecord
StaticHardConstraintPipeline::evaluate(const StaticHardConstraintInput& input,
                                       evidence::IEvaluationContext& ctx) const
{
    // ---- 第 1 步：阶段能力检查（§6.2 第 2 步；构造定型阶段的阶段锁）----
    // StageD 联合管线未随本任务实现（WP-21-T03/T04）——请求即运行启动
    // 阻塞（OPT-STAGE-LOCKED，§6.5：不是候选淘汰、不静默降级）。
    if (m_stage != OptimizationStage::StageB) {
        throw OptimizationError(
            kOptStageLocked,
            "静态硬约束管线仅实现 stage-b（R1）；stage-d 联合管线归 "
            "WP-21（P-04 冻结前置）——运行启动阻塞（§6.5）");
    }
    // 调用方契约校验（fail-fast 轨——候选身份保留值不可评估）。
    if (!input.candidateId.isValid()) {
        throw std::invalid_argument(
            "evaluate 契约违约：candidateId 为全零保留值（§4.2 内容寻址身份"
            "不可为空——调用方错误）");
    }

    // 记录骨架：约束事实按 §6.2 顺序填充；提前终止路径（I-C6）也保证
    // 已执行步骤的事实完整落表（审计对账面）。
    CandidateEvaluationRecord record;
    record.candidateId = input.candidateId;
    record.isBaseline = input.isBaseline;
    auto& facts = record.constraintFacts;
    facts.reserve(10);

    // 判定素材累积（§7.5 判定流的域输入面——最终交 evidence 汇总）。
    evidence::DomainVerdictInputs domain;      // Must/Should 违例（管线收集＋③端口透传）
    std::optional<evidence::SearchExhaustedRecord> searchRecord;  // 搜索未果凭据（I-C4）
    std::optional<evidence::DeterministicInfeasibilityProof> proof; // 任务级证明（I-C5）

    // ---- 第 1 步：输入前置（§6.2 第 1 步的管线内承载——完整 Preflight
    // 归 WP-20-T08）与第 2 步阶段能力（已由入口阶段锁确认）----
    // 两步到达即通过（前置事实声明——汇总事实面/快照由调用方冻结供给，
    // 阶段锁在 evaluate 入口已执行）：事实表与 §6.2 执行序一一对应（审计
    // 对账面——十步各一条事实，缺步即事实表漂移）。
    {
        ConstraintFact fact;
        fact.constraintId = ConstraintId::InputPrecondition;
        fact.verdict = ConstraintVerdict::Satisfied;
        fact.detail = "汇总事实面在场（快照/切片/覆盖矩阵由调用方冻结供给——"
                      "完整 Preflight 检查归 WP-20-T08）";
        facts.push_back(std::move(fact));
        ConstraintFact stageFact;
        stageFact.constraintId = ConstraintId::StageCapability;
        stageFact.verdict = ConstraintVerdict::Satisfied;
        stageFact.detail = "管线阶段 stage-b 与请求阶段一致（入口阶段锁已确认——"
                           "StageD 请求为运行启动阻塞，不到达本步）";
        facts.push_back(std::move(stageFact));
    }

    // ---- 第 2 步：变量边界和锁定检查＋第 4 步补丁合法性（§6.2 第 3/4 步）----
    // 复用 T03 补丁校验面（锁定→OPT-VAR-LOCKED；值域/未知/重复→
    // OPT-PATCH-ILLEGAL；StageB 引用阶段外变量→OPT-STAGE-LOCKED——注意
    // 补丁面命中阶段锁（如 StageB 补丁引用电机型号绑定）同样是**候选级**
    // 拒绝：该候选引用了本阶段不可评估的变量，属该候选生成缺陷；而研究
    // 定义级阶段锁（constraintsFor/管线 stage）才是启动阻塞——两级语义
    // 分立（T03 落位登记②同口径），不混同。
    const auto patchReport = validatePatchItems(input.bindings, m_stage, input.patch.items);
    if (!patchReport.ok()) {
        for (const auto& issue : patchReport.issues) {
            // 逐问题产出淘汰原因（比较型定位：绑定 token＋对象——§5.4）。
            RejectionReason rejection;
            rejection.stage = m_stage;
            rejection.sourceId = (issue.code == kOptVarLocked)
                                     ? std::string(toToken(ConstraintId::VariableLock))
                                     : std::string(toToken(ConstraintId::PatchValidity));
            rejection.reasonToken = std::string(kRejectHardConstraintViolated);
            rejection.subject = issue.bindingId;
            rejection.mode = input.mode;
            record.rejections.push_back(std::move(rejection));
        }
        // 约束事实：两步各一条 Violated（事实表完整性——哪一步拒了哪类问题
        // 由 rejections 的 sourceId 区分）。
        for (const auto id : {ConstraintId::VariableLock, ConstraintId::PatchValidity}) {
            ConstraintFact fact;
            fact.constraintId = id;
            fact.verdict = ConstraintVerdict::Violated;
            fact.subject = patchReport.issues.front().bindingId;
            fact.detail = "候选补丁未通过变量锁定/合法性检查（首个问题："
                          + patchReport.issues.front().code + "——"
                          + patchReport.issues.front().detail + "）";
            facts.push_back(std::move(fact));
        }
        record.status = CandidateStatus::Infeasible;  // 不进可行集（AT-09）
        return record;  // I-C6：硬约束先行——前置失败即终止（生成阶段拒绝语义）
    }
    {
        // 两步各落一条 Satisfied 事实（事实表与执行序一一对应——审计面）。
        for (const auto id : {ConstraintId::VariableLock, ConstraintId::PatchValidity}) {
            ConstraintFact fact;
            fact.constraintId = id;
            fact.verdict = ConstraintVerdict::Satisfied;
            fact.detail = "候选补丁通过变量锁定与合法性检查（I-OPT-9 不截断口径）";
            facts.push_back(std::move(fact));
        }
    }

    // ---- 第 3 步：候选编译（§6.2 第 5 步——runtime 同一编译链接缝）----
    {
        const auto compiled = m_deps.compiler->compile(input.patch, input.bindings);
        ConstraintFact fact;
        fact.constraintId = ConstraintId::CandidateCompile;
        if (!compiled.ok) {
            // 编译失败＝环境错误轨（I-C2）：EvaluationFailed＋OPT-CANDIDATE-
            // COMPILE-FAILED 包装＋RT-* 码透传（cause 链保留——§6.6）。
            fact.verdict = ConstraintVerdict::NotApplicable;  // 约束未执行（前置失败）
            fact.detail = "候选编译失败（" + compiled.stableCode + "）：" + compiled.detail
                          + "——后续约束未执行";
            facts.push_back(std::move(fact));
            RejectionReason rejection;
            rejection.stage = m_stage;
            rejection.sourceId = std::string(toToken(ConstraintId::CandidateCompile))
                                 + " (" + compiled.stableCode + ")";  // RT-* 透传定位
            rejection.reasonToken = std::string(kRejectCandidateCompileFailed);
            rejection.subject = input.candidateId.toCanonical();
            rejection.mode = input.mode;
            record.rejections.push_back(std::move(rejection));
            record.status = CandidateStatus::EvaluationFailed;  // 非工程判定（§7.5）
            return record;  // I-C6
        }
        fact.verdict = ConstraintVerdict::Satisfied;
        fact.detail = "候选编译成功（同一编译链——§6.2 第 5 步）";
        facts.push_back(std::move(fact));
    }

    // ---- 第 4 步：拓扑/模型合法性（§6.2 第 6 步——补丁结构不变式已在
    // 第 2 步词表校验达成"只覆盖已登记绑定"；本步执行基线链型事实检查：
    // "链型能力以快照事实为准，补丁不改拓扑 ⇒ 候选拓扑＝基线拓扑"）----
    if (!input.baselineChainInEnabledScope) {
        ConstraintFact fact;
        fact.constraintId = ConstraintId::Topology;
        fact.verdict = ConstraintVerdict::Violated;
        fact.detail = "基线链型超出 R1 启用范围（prismatic/mimic/闭环等——§2.1 "
                      "支持矩阵；OPT-TOPOLOGY-REJECTED；补丁不改拓扑 ⇒ 候选"
                      "同样超出）";
        facts.push_back(std::move(fact));
        evidence::DomainVerdictViolation violation;
        violation.itemId = std::string(toToken(ConstraintId::Topology));
        violation.detail = "基线链型超出当前启用范围（OPT-TOPOLOGY-REJECTED）";
        domain.mustViolations.push_back(std::move(violation));
    } else {
        ConstraintFact fact;
        fact.constraintId = ConstraintId::Topology;
        fact.verdict = ConstraintVerdict::Satisfied;
        fact.detail = "基线链型在启用范围内；补丁不改拓扑 ⇒ 候选拓扑＝基线拓扑";
        facts.push_back(std::move(fact));
    }

    // ---- 第 5 步：关节限位（§6.2 第 7 步——policy IJointLimitEvaluator
    // 值域/区间有效经探针投影；行程上限策略校验的消费面，SA-15 确认流
    // 归命令边界——管线只记录发现不执行放行）----
    {
        const auto probeResult = m_deps.jointLimitProbe->probeJointLimits(input.jointLimits);
        ConstraintFact fact;
        fact.constraintId = ConstraintId::JointLimit;
        if (!probeResult.completed) {
            // policy 评估级失败（名称不可解析/位置非有限——环境面）：评估
            // 失败≠约束失败（§7.5），候选 EvaluationFailed 终止（I-C6）。
            fact.verdict = ConstraintVerdict::NotApplicable;
            fact.detail = "关节限位评估未完成（policy 评估级失败——环境错误轨）";
            facts.push_back(std::move(fact));
            record.status = CandidateStatus::EvaluationFailed;
            return record;
        }
        bool mustViolated = false;
        std::string shouldNote;
        for (const auto& finding : probeResult.findings) {
            if (isMustLevel(finding.levelToken)) {
                // Must 级发现（区间非法/限位违例/行程超限/工程范围非法）
                // ——硬约束违例素材（§6.2：候选 bounds 值域/区间有效）。
                mustViolated = true;
                evidence::DomainVerdictViolation violation;
                violation.itemId = std::string(toToken(ConstraintId::JointLimit));
                violation.detail = finding.jointSubject + " " + finding.kindToken
                                   + "（实际 " + formatValue(finding.actualValue) + " "
                                   + finding.unitSymbol + "，要求 "
                                   + formatValue(finding.thresholdValue) + " "
                                   + finding.unitSymbol + "）";
                domain.mustViolations.push_back(std::move(violation));
                // 比较型事实定位（ERR-01：实际/要求/单位三元组）。
                fact.subject = finding.jointSubject;
                fact.actualText = formatValue(finding.actualValue);
                fact.requiredText = formatValue(finding.thresholdValue);
                fact.unitSymbol = finding.unitSymbol;
            } else {
                // Should 级（近限位警告）——不阻断（Feasible＋警告素材）。
                shouldNote += finding.jointSubject + " " + finding.kindToken + "; ";
            }
        }
        fact.verdict = mustViolated ? ConstraintVerdict::Violated
                                    : ConstraintVerdict::Satisfied;
        fact.evaluationKey = "policy.joint-limit-evaluator";  // 依据标识（④端口策略面）
        if (!shouldNote.empty()) {
            fact.detail = "近限位警告（Should 级，不阻断）：" + shouldNote;
        } else if (!mustViolated) {
            fact.detail = "候选关节限位全部有效（区间/值域/行程——policy 阈值口径）";
        }
        facts.push_back(std::move(fact));
    }

    // ---- ③端口评估器调用辅助（第 6/7 步共用——§6.7 内部消费链：
    // find→create→evaluate，域判定素材透传）----
    // 返回值语义：nullopt＝调用方应终止（评估器异常——EvaluationFailed）；
    // 否则约束事实判定已写入 fact 引用、判定素材已并入 domain/searchRecord/proof。
    auto invokeKinEvaluator =
        [&](std::string_view evaluationKey, ConstraintFact& fact) -> std::optional<bool> {
        // 装配一致性检查：阶段必需评估器未注册（§6.4 #7 同源码面）——
        // 装配期就该拦截的一致性问题在运行点 fail-fast（不静默跳过约束）。
        const evidence::IEvaluatorFactory* factory
            = m_deps.evaluators->find(evaluationKey);
        if (factory == nullptr) {
            throw OptimizationError(
                kOptEvaluatorMissing,
                "阶段必需评估器未注册：" + std::string(evaluationKey)
                    + "（§6.4 检查项 #7 同源——装配不一致，fail-fast）");
        }
        auto evaluator = factory->create();  // 实例独占（O-37 无参签名）
        evidence::EvaluationRequest request;
        request.task = input.task;
        request.mode = input.mode;
        request.snapshot = input.snapshot;
        request.slice = input.slice;
        request.caseSubset = input.caseSubset;  // 本批工况子集（覆盖矩阵跨批汇总）
        evidence::EvaluationOutput output;
        try {
            output = evaluator->evaluate(request, ctx);  // 协作取消在评估器批边界生效
        } catch (const evidence::EvidenceError& e) {
            // 评估器异常≠约束失败（§8.3 第 5 步）：候选 EvaluationFailed，
            // 附域诊断 cause（token(e.code())——evidence 稳定错误码）。
            fact.verdict = ConstraintVerdict::NotApplicable;
            fact.detail = "评估器 " + std::string(evaluationKey)
                          + " 异常（" + std::string(evidence::token(e.code())) + "）："
                          + e.what();
            record.status = CandidateStatus::EvaluationFailed;
            return std::nullopt;  // 终止信号（I-C6）
        }
        // 域判定素材透传（PA-1：违例语义归 kin 域——管线只搬运不加工）。
        for (auto&& v : output.verdictInputs.mustViolations) {
            domain.mustViolations.push_back(std::move(v));
        }
        for (auto&& v : output.verdictInputs.shouldViolations) {
            domain.shouldViolations.push_back(std::move(v));
        }
        // 搜索未果（C5/C8）：DataInsufficient 凭据——绝不判确定性不可行
        // （§6.2："不把搜索未果判为任务级确定性不可行"）。I-C4：先到者
        // 透传（执行序＝稳定性依据）。
        if (output.searchRecord.has_value() && !searchRecord.has_value()) {
            searchRecord = output.searchRecord;
        }
        // 任务级不可行证明（③级素材）：评估输出优先（I-C5）。
        if (output.proof.has_value()) {
            proof = output.proof;
        }
        // 域诊断透传（不加工——I-C3）。
        for (auto&& d : output.diagnostics) {
            record.diagnostics.push_back(std::move(d));
        }
        // 约束事实判定：Must 违例 → Violated；搜索未果 → DataInsufficient
        // （不读作通过）；仅 Should 警告/无违例 → Satisfied。
        if (!output.verdictInputs.mustViolations.empty()) {
            fact.verdict = ConstraintVerdict::Violated;
        } else if (output.searchRecord.has_value()) {
            fact.verdict = ConstraintVerdict::DataInsufficient;
        } else {
            fact.verdict = ConstraintVerdict::Satisfied;
        }
        return true;
    };

    // ---- 第 6 步：Must 工位可达（③端口 kin.task-points-batch——KIN-03
    // Must 点三态分别报告：违例/数据不足/满足由 kin 判定面产出）----
    {
        ConstraintFact fact;
        fact.constraintId = ConstraintId::MustStationReachability;
        fact.evaluationKey = std::string(kKinTaskPointsBatchKey);
        const auto outcome =
            invokeKinEvaluator(kKinTaskPointsBatchKey, fact);
        facts.push_back(std::move(fact));
        if (!outcome.has_value()) {
            return record;  // 评估器异常——EvaluationFailed（I-C6）
        }
    }

    // ---- 第 7 步：Must 区域覆盖（③端口 kin.region-coverage——KIN-04
    // 冻结样本集、分母＝计划样本总数；覆盖不足 Must 违例/零样本数据不足
    // 由 kin 判定面＋区域证据形状〔aggregateVerdict 对快照对账〕产出）----
    {
        ConstraintFact fact;
        fact.constraintId = ConstraintId::MustRegionCoverage;
        fact.evaluationKey = std::string(kKinRegionCoverageKey);
        const auto outcome =
            invokeKinEvaluator(kKinRegionCoverageKey, fact);
        facts.push_back(std::move(fact));
        if (!outcome.has_value()) {
            return record;  // 评估器异常——EvaluationFailed（I-C6）
        }
    }

    // ---- 第 8 步：静态碰撞（§6.2 第 10 步——策略启用时；构型级过滤
    // 记录，不上升为任务不可行——C8 作用域规则）----
    if (m_deps.collisionProbe == nullptr) {
        // 策略未启用碰撞（V13-01：无布尔开关——会话不在场即未启用）：
        // 约束不在范围（NotApplicable），非"无碰撞"（KIN-05 防伪口径的
        // 编排侧对应——绝不把未评估读作满足）。
        ConstraintFact fact;
        fact.constraintId = ConstraintId::StaticCollision;
        fact.verdict = ConstraintVerdict::NotApplicable;
        fact.detail = "策略未启用碰撞（会话不在场——V13-01），约束不在评估范围";
        facts.push_back(std::move(fact));
    } else {
        // SampleSet 查询形态（④端口唯一实现——接缝生产适配器薄转发）：
        // 构型样本直传限位查询投影的构型集（rad|m 逐轴——同一候选构型面；
        // rw::math::Q 的机械转换归生产适配器——Constraint.hpp 探针注）。
        const auto probeResult
            = m_deps.collisionProbe->probeSampleSet(input.jointLimits.configurations);
        ConstraintFact fact;
        fact.constraintId = ConstraintId::StaticCollision;
        fact.evaluationKey = "policy.collision-session";
        if (!probeResult.completed) {
            // 碰撞评估失败/取消（非终态——CON-04 不入正式证据）：环境轨。
            fact.verdict = ConstraintVerdict::NotApplicable;
            fact.detail = "静态碰撞评估未完成（非终态输出不入正式证据——CON-04）";
            facts.push_back(std::move(fact));
            record.status = CandidateStatus::EvaluationFailed;
            return record;  // I-C6
        }
        if (!probeResult.findings.empty()) {
            // 构型级碰撞发现：**只记录过滤**（§6.2 C8——"构型级过滤记录，
            // 不上升为任务不可行"；REQUIREMENTS §8.1：某 IK 解碰撞＝该解
            // 被硬过滤，均不直接上升为任务不可行）。不进 mustViolations。
            fact.verdict = ConstraintVerdict::Satisfied;
            fact.subject = "samples[" + std::to_string(probeResult.findings.front().sampleIndex)
                           + "]等 " + std::to_string(probeResult.findings.size()) + " 例";
            fact.detail = "构型级碰撞 " + std::to_string(probeResult.findings.size())
                          + " 例已过滤（C8 构型级作用域——不上升任务不可行；首例 "
                          + probeResult.findings.front().pairText + "）";
        } else {
            fact.verdict = ConstraintVerdict::Satisfied;
            fact.detail = "静态碰撞评估完成，无构型级碰撞（SampleSet 三入口一致口径）";
        }
        facts.push_back(std::move(fact));
    }

    // ---- 第 9 步：判定汇总（§7.5 判定流——消费 evidence 五级决策表，
    // P-EV-3 字面顺序引用其现状；本管线不复制判定逻辑——DOPT-1）----
    evidence::VerdictInput verdictInput;
    verdictInput.outcome = core::TaskOutcome::Completed;  // 管线级失败已提前返回
    verdictInput.mode = input.mode;
    // 输入就绪与快照门禁：到达本步＝Preflight 前置已通过（WP-20-T08 面）、
    // 快照为 builder 冻结产物（身份完整性在冻结期已验证）——两门禁在本
    // 管线的通过是前置事实的如实声明，非重复校验（完整 Preflight 归 T08）。
    verdictInput.readiness.valid = true;
    verdictInput.snapshotGate.complete = true;
    verdictInput.coverage = input.coverage;                 // EVI-02 分母事实（编排方汇总）
    verdictInput.regionCoverages = input.regionCoverages;   // KIN-04 形状事实
    verdictInput.proof = std::move(proof);                  // ③级素材（有效性校验在 evidence）
    verdictInput.evidence = input.manifest;                 // 清单绑定面
    verdictInput.domain = std::move(domain);                // ⑤级判定素材（管线收集＋③端口透传）
    verdictInput.searchRecord = std::move(searchRecord);    // ④级凭据（C5/C8）

    const evidence::VerdictResult verdict = evidence::aggregateVerdict(
        verdictInput, input.snapshot, *m_deps.producers, *m_deps.profiles);
    const evidence::EligibilityCheck eligibility = evidence::checkFormalPassEligibility(
        verdictInput, input.snapshot, verdict, *m_deps.profiles);

    // 工程判定 → 候选状态映射（§7.5 判定流终态；NotApplicable 意外出现
    // 属管线不变量破坏——outcome 恒 Completed 时五级表不可能输出
    // NotApplicable，防御式 fail-fast 不吞错）。
    switch (verdict.status) {
    case core::EngineeringStatus::EngineeringInfeasible: {
        record.status = CandidateStatus::Infeasible;  // 不进可行集（AT-09）
        // 淘汰原因：Must 违例/有效证明——逐条定位（O10）。
        for (const auto& violation : verdictInput.domain.mustViolations) {
            RejectionReason rejection;
            rejection.stage = m_stage;
            rejection.sourceId = violation.itemId;  // 约束 token（违例来源定位）
            rejection.reasonToken = std::string(kRejectHardConstraintViolated);
            rejection.subject = violation.detail;
            rejection.mode = input.mode;
            record.rejections.push_back(std::move(rejection));
        }
        if (verdictInput.proof.has_value()) {
            RejectionReason rejection;
            rejection.stage = m_stage;
            rejection.sourceId = verdictInput.proof->claimToken;  // 证明溯源 token
            rejection.reasonToken = std::string(kRejectHardConstraintViolated);
            rejection.subject = "task-level-proof";
            rejection.mode = input.mode;
            record.rejections.push_back(std::move(rejection));
        }
        break;
    }
    case core::EngineeringStatus::DataInsufficient:
        // 数据不足（漏验门禁/搜索未果/覆盖降级/空必验集——P-EV-7 保守）：
        // 不是淘汰（I-C1 无原因 token），限定语导出（§7.5 区分表）。
        record.status = CandidateStatus::DataInsufficient;
        break;
    case core::EngineeringStatus::Feasible:
        record.status = CandidateStatus::Feasible;  // 进入可行集（Pareto 前置）
        break;
    case core::EngineeringStatus::NotApplicable:
    default:
        // 管线不变量破坏（见上）——fail-fast，不静默映射。
        throw std::logic_error(
            "管线判定不变量破坏：outcome==Completed 的汇总输出 NotApplicable"
            "（§6.4.1 五级表⓪级前置不成立——实现缺陷）");
    }
    record.engineeringStatus = verdict.status;
    record.verdictTrace = verdict.trace;
    // 汇总诊断透传（evidence 建议码＋定位——命中级的补充语义，I-C3 透传
    // 纪律；候选表"未完成附缺项列表"呈现的素材面）。
    for (auto&& d : verdict.diagnostics) {
        record.diagnostics.push_back(std::move(d));
    }
    record.formalPassEligible = eligibility.eligible;
    record.formalPassUnmetConditions = eligibility.unmetConditions;
    return record;
}

}  // namespace sdurws::ird::optimization
