/**
 * @file   Applier.cpp
 * @brief  候选应用组装实现——§10.3 六项前置校验＋两步命令语义组装＋差异
 *         预览（P-OPT-8 警告面）（任务 WP-20-T07）。
 *
 * 设计依据：include/sdurws/ird/optimization/Applier.hpp 文件头（契约面逐
 *   条注明出处——§10.1/§10.2/§10.3/§10.4 与 P-OPT-3/P-OPT-8 处置）；本
 *   实现翻译单元只承载该头的机械落位。
 *
 * 错误语义分轨（头注六项前置处置表——AGENTS §2.5 错误归类纪律）：
 *   ① 调用方契约违约（运行非 Completed／候选不可用／身份归属失配／请求
 *      结构保留值）＝ fail-fast 异常轨（kOptApplyPlanInvalid／
 *      std::invalid_argument）；
 *   ② 能力未装配（物化缝缺失／项目只读）＝ 通道化结构化返回（allowApply
 *      = false ＋ blockedReasons）——**不是错误**，是 P-OPT-3 裁决前的
 *      生产常态；
 *   ③ 环境错误：本翻译单元零环境错误路径（纯函数、无 IO）——物化缝/
 *      预览缝实现内部抛出的异常按调用方错误透传（不吞不改，AGENTS §3）。
 *
 * 确定性（NFR-COR-02）：校验序固定（结构→1→2→3→4→组装→5→6→预览）、
 *   阻断原因/警告按发现序追加——同（请求, 缝行为）⇒ 同计划。
 */

#include <sdurws/ird/optimization/Applier.hpp>

#include <algorithm>
#include <stdexcept>  // std::invalid_argument——请求结构违约的 fail-fast 载体
#include <utility>

#include <sdurws/ird/optimization/DiagCodes.hpp>  // kOptApplyPlanInvalid/
                                                  //  kOptInputInvalid——稳定码
                                                  //  唯一书写点（禁字符串拼码）

namespace sdurws::ird::optimization {

// =====================================================================
// 构造（缝装配期定型）
// =====================================================================

OptimizationCandidateApplier::OptimizationCandidateApplier(Deps deps) noexcept
    : m_deps(deps)
{
    // 缝均可空（P-OPT-3 通道化是设计内常态）——构造零校验零抛出。
}

// =====================================================================
// buildApplyPlan——六项前置校验与组装（执行序见类注处置表）
// =====================================================================

CandidateApplyPlan OptimizationCandidateApplier::buildApplyPlan(
    const ApplyCandidateRequest& request) const
{
    // ---- 第 0 步：请求结构校验（std::invalid_argument——调用方把保留值
    //      喂给了组装面；与"候选不可应用"（域异常）区分：前者连合法请求
    //      都构不成）。
    if (!request.run.runId.isValid()) {
        throw std::invalid_argument(
            "optimization/applier: 请求的运行身份为全零保留值——"
            "候选应用只接受 assembleRunResult 产出的聚合");
    }
    if (!request.candidateId.isValid()) {
        throw std::invalid_argument(
            "optimization/applier: 目标候选身份为全零保留值");
    }
    if (!request.baseline.branch.isValid() || !request.baseline.tip.isValid()) {
        throw std::invalid_argument(
            "optimization/applier: 基线状态面存在保留值（branch/tip 须 "
            "isValid——②端口投影值）");
    }
    // 运行聚合自身的输入身份也须非保留值（第 3 项归属核对要拿它算
    // CandidateId——保留值进 SHA 公式得全零身份，必然失配，提前定位）。
    if (!request.run.revision.isValid() || !request.run.baselineRoot.isValid()
        || !request.run.baselineCv.isValid()) {
        throw std::invalid_argument(
            "optimization/applier: 运行聚合的输入身份面存在保留值"
            "（revision/baselineRoot/baselineCv——归属核对锚，§10.3 第 3 项）");
    }

    // ---- §10.3 第 1 项：源运行 Completed（取消/失败/中断结果不可应用
    //      ——TASK-02；"已中断"不伪装完整结果，§10.6）。
    if (request.run.runPhase != RunPhase::Completed) {
        throw OptimizationError(
            kOptApplyPlanInvalid,
            std::string("optimization/applier: 源运行非 Completed（实际 ")
            + std::string(toToken(request.run.runPhase))
            + "）——取消/失败/中断结果不可应用（TASK-02/§10.3 第 1 项）");
    }

    // ---- §10.3 第 2 项：目标候选可用性（检索＋记录效力＋状态＋正式资格）。
    //  检索（F-630 两段式）：聚合的 candidates 是 Quick＋Verified 合并序
    //  （Run.cpp 第 5 步投影纪律：quickRecords 在前、verifiedRecords 在后），
    //  且 Verified 复核批复用 Quick 批的 candidateId（同补丁同基线 ⇒ 同身份
    //  ——§4.2 内容寻址的天然复用形态）。旧实现"首见即用"在合并形态下必先
    //  命中 Quick 记录，随后被 screeningOnly 效力闸拒绝——Verified 候选
    //  永远无法采用（OPT-08 采用 API 对合并形态必然失败）。修复后分两段：
    //  第一段只找 candidateId 匹配**且 screeningOnly==false** 的首条记录
    //  （Verified 记录优先）；线性扫描即可（R1 预算 ≤256 候选，无性能顾虑）。
    const TwoStageRunRecord* record = nullptr;
    for (const TwoStageRunRecord& c : request.run.candidates) {
        if (c.candidateId == request.candidateId && !c.screeningOnly) {
            record = &c;
            break;  // Verified 首见即用——同身份重复 Verified 记录（理论上
                    //  去重保证唯一）取首见，与 Pareto 去重"保留首次评估"
                    //  口径一致（§7.4）
        }
    }
    if (record == nullptr) {
        //  第二段：Verified 记录缺位时回退同身份任意记录——让下方既有
        //  效力闸产出**精确**的拒绝语义：仅有 Quick 记录 ⇒ screeningOnly
        //  拒绝（"Quick 筛选记录"消息，守卫语义不回退）；全无同身份记录
        //  ⇒ 保持既有「候选不在源运行结果中」错误（消息不变）。若在此
        //  直接抛"不在运行中"，Quick-only 形态会被错报为"候选不存在"，
        //  丢失 ERR-01 比较型定位精度（调用方无法区分"没复核"与"异运行"）。
        for (const TwoStageRunRecord& c : request.run.candidates) {
            if (c.candidateId == request.candidateId) {
                record = &c;
                break;
            }
        }
        if (record == nullptr) {
            throw OptimizationError(
                kOptApplyPlanInvalid,
                "optimization/applier: 目标候选不在源运行结果中（CandidateId="
                + request.candidateId.toCanonical()
                + "）——候选只能从其归属运行采用（OPT-08 归属语义）");
        }
    }
    //  记录效力：Quick 记录（screeningOnly==true）绝不支撑采用——
    //  "Quick 不得单独支撑正式通过"（EVI-01/P-EV-8）在采用面的最后一道闸。
    //  F-630 后本闸只在"检索回退命中 Quick 记录"（Quick-only 形态——该
    //  候选无任何 Verified 记录）时触发；合并形态（Quick＋Verified 并存）
    //  已由两段式检索优先选中 Verified 记录，不进本分支。
    if (record->screeningOnly) {
        throw OptimizationError(
            kOptApplyPlanInvalid,
            "optimization/applier: 目标候选为 Quick 筛选记录"
            "（screening-only）——保守筛选产物不支撑采用，须 Verified 复核"
            "（§10.3 第 2 项/§8.4）");
    }
    //  状态：Feasible 系（Feasible 或其非支配子集标记 ParetoNondominated）。
    //  Infeasible/DataInsufficient/EvaluationFailed/ScreenedOut/Pending 一概
    //  不可应用（§7.5 正交表——非可行状态没有"设为当前方案"语义）。
    if (record->status != CandidateStatus::Feasible
        && record->status != CandidateStatus::ParetoNondominated) {
        throw OptimizationError(
            kOptApplyPlanInvalid,
            std::string("optimization/applier: 目标候选状态不可应用（实际 ")
            + std::string(toToken(record->status))
            + "）——仅 Feasible/ParetoNondominated 可采用（§10.3 第 2 项）");
    }
    //  正式资格：Verified 证据经 FormalPassEligibility 通过（RPT-05 五条件
    //  同源——判定唯一归 evidence，本面只消费 T06 编排已算的投影位，零
    //  第二套判定复制，N7/N9）。
    if (!record->formalPassEligible) {
        throw OptimizationError(
            kOptApplyPlanInvalid,
            "optimization/applier: 目标候选无正式通过资格"
            "（formalPassEligible==false）——Verified 证据须过 FormalPass"
            "Eligibility 五条件方可采用（§10.3 第 2 项/RPT-05）");
    }

    // ---- §10.3 第 3 项：候选身份归属核对（内容寻址重算——候选身份公式
    //  CandidateId = SHA-256(baselineRootOid‖baselineRootCv‖CandidatePatchId)；
    //  同一补丁作用于同一基线必得同身份。重算失配＝候选记录与运行聚合的
    //  基线锚不一致（记录被篡改或聚合组装错位）——采用前必须拦截，
    //  防止"把别的运行的结果应用到此基线"的静默错位）。
    const CandidateId recomputed =
        candidateIdOf(request.run.baselineRoot, request.run.baselineCv,
                      record->patch);
    if (!(recomputed == request.candidateId)) {
        throw OptimizationError(
            kOptApplyPlanInvalid,
            "optimization/applier: 候选身份归属核对失配（重算 "
            + recomputed.toCanonical() + " ≠ 请求 "
            + request.candidateId.toCanonical()
            + "）——候选与运行基线锚不对应（§10.3 第 3 项：结果归属修订＝"
              "基线修订）");
    }

    // ---- 组装 plan 骨架（以下各步不再抛异常——通道化/标记面）。
    CandidateApplyPlan plan;

    // ---- §10.3 第 4 项：当前基线 tip 与运行输入修订核对（**不阻塞组装**
    //  但标记预期拒绝——§10.3 原文；用户仍可强行尝试，提交期 project S2
    //  以 expectedRevision 失配兜底拒绝＝PRJ-STALE-REVISION-REJECTED，
    //  候选保留——OPT-VER-146 观测点）。
    plan.expectedStaleBaseline =
        !(request.baseline.tip == request.run.revision);

    // ---- step1 组装：建支语义（前置 1~3 已过即组装；与通道化无关——
    //  语义面完整，消费与否由 allowApply 把关）。
    plan.createBranch.baseRevisionId = request.run.revision;  // 建支基＝运行
    // 输入修订（分支表 baseRevisionId 记录、不复制对象——OPT-VER-145）。
    if (request.branchLabel.empty()) {
        // 默认命名规则（实现口径，DTB §5.4）：显示名＝"方案 "＋候选 id
        // 前 12 位十六进制——可辨识且不参与任何身份（P-PR-8：label 一次
        // 写入无改名入口，故在此一次定名）。分支显示名是 UTF-8 人读文本。
        plan.createBranch.label =
            "方案 " + request.candidateId.toCanonical().substr(4, 12);
    } else {
        plan.createBranch.label = request.branchLabel;
    }

    // ---- §10.3 第 5 项：补丁→候选设计 canonical 物化可用（P-OPT-3 通道
    //  化——缝未注入＝应用功能不启用，候选只导出；组装仍成功返回）。
    if (m_deps.materializer == nullptr) {
        plan.blockedReasons.emplace_back(kApplyBlockMaterializationUnavailable);
    } else {
        // 物化产出 step2 payload（缝实现抛出的异常按调用方错误透传——
        // 物化失败＝补丁/研究定义非法，不带病组装）。注意绑定集来自
        // run.config.variables（运行冻结的配置——不是调用方现配的绑定：
        // 候选语义绑定在运行输入上，I-OPT-1 输入冻结）。
        ApplyPlanStepApplyDesign step2;
        step2.commandType = std::string(kApplyRobotDesignCommandToken);
        step2.payloadCanonical = m_deps.materializer->materialize(
            record->patch, request.run.config.variables);
        step2.expectedRevision = request.run.revision;  // 新分支 tip＝建支基
        // ＝运行输入修订（project §4.5.1：建支型 tip 一次写入源 tip——
        // step2 的 S2 并发校验锚）。
        step2.requiresDualCompile = true;  // MDL-06 双编译（§10.1 REV：
        // 任一失败不提交——原子性由命令边界保证）。
        plan.applyDesign = std::move(step2);
    }

    // ---- §10.3 第 6 项：项目可写（PM-07——只读时应用入口禁用＋诊断，
    //  §10.2 红线；通道化同第 5 项）。
    if (!request.baseline.writable) {
        plan.blockedReasons.emplace_back(kApplyBlockProjectReadOnly);
    }

    // ---- 通道化总闸：全部阻断原因已收集，无原因即可应用（第 1~3 项
    //  违约早已抛出——能到这里说明语义面成立，能力面由本位承载）。
    plan.allowApply = plan.blockedReasons.empty();
    // 完整复算提示（§10.1 RECALC）：采用必然引入新设计——依赖失效级联后
    // 须完整复算，复核完成前不沿用原通过结论（§10.5）。恒 true。
    plan.recalcRequired = true;

    // ---- 差异预览（§10.4——Model Diff 消费；P-OPT-8 警告面）。
    //  预览是应用的前置呈现而非前提：缝未装配或基线字节缺失时降级为
    //  警告（如实告知"差异可能不全"），不阻断组装、不虚构差异条目。
    if (m_deps.diffSource == nullptr || request.baselineDesignCanonical.empty()
        || !plan.applyDesign.has_value()
        || plan.applyDesign->payloadCanonical.empty()) {
        // 通道不可用四形态：缝缺失／基线字节未提供／step2 未物化（连带
        //  无候选字节）／候选字节为空——统一如实警告（kDiffWarning*
        //  token，呈现面转限定语；不虚构空差异清单冒充"无差异"）。
        plan.diffPreview.warnings.emplace_back(kDiffWarningSourceUnavailable);
    } else {
        // 预览缝可用：基线字节（②端口事实）×候选字节（物化产出）→
        // 差异条目（modeling diff 实际产出——本面零重解释零重排序）。
        plan.diffPreview = m_deps.diffSource->diff(
            request.baselineDesignCanonical,
            plan.applyDesign->payloadCanonical);

        // P-OPT-8 范围检查：候选补丁触及传动比（绑定权威字段路径含
        // ratioPerJoint——词表 mdl.drivetrain.ratio[j] 的实例化绑定）而
        // 差异条目中无任何传动比字段 ⇒ 追加范围外警告。**不虚构条目**：
        // 只加警告，条目表保持 diff 实际产出（modeling diff 范围声明为
        // 根对象、ratioPerJoint 随 robot-drivetrain 对象——P-OPT-8 裁决
        // 前的如实降级）。
        bool patchTouchesRatio = false;
        for (const PatchItem& item : record->patch.items) {
            // 经绑定表把补丁项映射到权威字段路径（绑定量＝研究维度数，
            // 线性查找无性能顾虑）。
            for (const VariableBinding& b : request.run.config.variables) {
                if (b.bindingId == item.bindingId
                    && b.authorityFieldPath.find("ratioPerJoint")
                        != std::string::npos) {
                    patchTouchesRatio = true;
                    break;
                }
            }
            if (patchTouchesRatio) {
                break;
            }
        }
        if (patchTouchesRatio) {
            bool diffHasRatioEntry = false;
            for (const CandidateDiffEntry& e : plan.diffPreview.entries) {
                if (e.field == "ratioPerJoint"
                    || e.subjectPath.find("ratioPerJoint") != std::string::npos) {
                    diffHasRatioEntry = true;
                    break;
                }
            }
            if (!diffHasRatioEntry) {
                plan.diffPreview.warnings.emplace_back(
                    kDiffWarningRatioOutOfScope);
            }
        }
    }

    return plan;
}

}  // namespace sdurws::ird::optimization
