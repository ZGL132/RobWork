/**
 * @file   Compatibility.cpp
 * @brief  缓存/检查点兼容判定的实现——judgeCacheHit 六项检查全量收集＋
 *         judgeCheckpointCompatibility 五条件全量收集（§8.2 判定式的
 *         逐条承载）。
 *
 * 设计依据：units/evidence.md §8.2（两判定函数与五结构原文冻结代码）、
 * §11（EV-CPA-1~3/EV-ENV-2 观测点）、§12 EV-T09 行、CON-04/OPT-06；
 * 任务契约 tasks/foundation/EV-T09.json（≙WP-05-T09）。实现口径 C-1～C-5
 * 登记于契约头 Compatibility.hpp 文件头注释与单元卡 v1.0 变更记录。
 *
 * 实现纪律：
 *   - 纯判定：本文件全部函数无 I/O、无全局可变状态、不修改任何入参、
 *     不抛异常（§2.1 try 轨分轨——可恢复查询路径；调用方对 Incompatible
 *     自行转译为 EvidenceErrorCode::CacheIncompatible／EVI-CACHE-INCOMPATIBLE，
 *     码值权威归 diagnostics StableCodeRegistry——PA-1）；
 *   - 全量收集不首错短路（C-1）：reasons 完整列出全部未满足条件，检查序
 *     固定＝枚举声明序（同失配必得同一清单，NFR-COR-02）；
 *   - 身份比较为字节精确等值（ContentIdentity::operator==——附录 D 第 12
 *     项无容差），零浮点、零 locale 依赖；
 *   - 零 Qt、零 rw、仅 core＋标准库（§3.2 依赖红线）。
 */

#include <sdurws/ird/evidence/Compatibility.hpp>

namespace sdurws::ird::evidence {

CacheHitResult judgeCacheHit(const CacheHitQuery& request,
                             const CachedResultSummary& summary)
{
    CacheHitResult result;

    // ---- 身份面四项检查（C-1 检查序：模式→切片→契约→Profile）----
    // 任一失配即不可能 FullHit/DiagnosticOnly（身份面对齐是两档的前提），
    // 但仍继续收集结果面失配——一次判定给出完整差异清单，调用方无需逐项
    // 重试（"缺失全量列出"纪律同源，§6.4）。
    //
    // 模式检查是双向的（D-13）：Quick 摘要配 Verified 请求＝以低效力证据
    // 冒充正式证据（表 1——Quick 不得单独支撑正式通过）；Verified 摘要配
    // Quick 请求＝隐式降级同样拒绝（模式是请求效力属性）。Preview 摘要在
    // 真实数据中不存在（make() 构造边界拒绝 Preview 包络，表 3 行 4），
    // 若调用方手工构造则按普通模式失配处理——判定式不私加特判。
    if (summary.mode != request.requestedMode) {
        result.reasons.push_back(CacheMissReason::ModeMismatch);
    }
    // 切片身份：CON-05 缓存键的字节精确等值。策略/种子/线程/配置/样本/
    // 环境版本已含于 sliceId（D-01/D-11）——此一处相等即覆盖全部输入
    // 要素，无需逐项再查；不等即输入已变（结果过期）。
    if (summary.sliceId != request.requestSliceId) {
        result.reasons.push_back(CacheMissReason::SliceMismatch);
    }
    // 契约版本：算法契约升级后旧结果不再可信（EV-CPA-2——同 sliceId 不同
    // 契约＝模拟算法升级场景，拒绝复用）。
    if (summary.evaluatorContractVersion != request.requestContractVersion) {
        result.reasons.push_back(CacheMissReason::ContractMismatch);
    }
    // Profile 身份：证据效力口径（§6.1）必须一致——Profile 不同＝判定
    // 证据"够不够"的标尺不同，跨标尺复用无语义。
    if (summary.profileContentIdentity != request.requestProfileIdentity) {
        result.reasons.push_back(CacheMissReason::ProfileMismatch);
    }

    // 身份面是否全对齐（在此快照——后续 push 只会追加结果面原因）。
    const bool identityFaceClean = result.reasons.empty();

    // ---- 结果面两项检查（CON-04 核心：部分/失败结果不得正式命中）----
    // 未完成（Canceled/Failed/Interrupted 或部分产物）＝计算没有给出完整
    // 答案——只能读作诊断（DiagnosticOnly），绝不充当正式命中（§8.2 判定
    // 式第二档/EV-ENV-2 反例：Canceled 结果作缓存查询→DiagnosticOnly 且
    // reasons 含 outcome-not-completed）。
    if (summary.outcome != core::TaskOutcome::Completed) {
        result.reasons.push_back(CacheMissReason::OutcomeNotCompleted);
    }
    // 清单未封账＝证据还可能增删（运行未终结）——完整性面不齐，同样只可
    // 诊断性读取。注意与 outcome 检查相互独立：Completed 但清单未封账的
    // 中间态同样不得正式命中（§8.2 判定式的"∨"两支各占一条 reason）。
    if (!summary.manifestFinalized) {
        result.reasons.push_back(CacheMissReason::ManifestIncomplete);
    }

    // ---- 三档裁决（§8.2 判定式逐字）----
    // 全部六项通过 → FullHit；身份面全对齐但结果面有失配 → DiagnosticOnly
    // （诊断性读取）；身份面任一失配 → Incompatible（其余一切）。
    if (result.reasons.empty()) {
        result.verdict = CacheHitResult::FullHit;
    } else if (identityFaceClean) {
        result.verdict = CacheHitResult::DiagnosticOnly;
    } else {
        result.verdict = CacheHitResult::Incompatible;
    }
    return result;
}

CheckpointCompatibilityResult judgeCheckpointCompatibility(
    const CacheHitQuery& request, const CheckpointSummary& summary)
{
    CheckpointCompatibilityResult result;

    // ---- 条件 1：评估键词形（C-2）----
    // "evaluatorKey 相等"的承载：评估键进入切片身份（Slice.hpp——CON-04），
    // 下方条件 2 的 sliceId 相等已隐含键相等，无需请求侧另带键字段。显式
    // 字段做词形校验：键词形非法的记录＝损坏或伪造的检查点头（正常写入
    // 路径产出的键必过 isValidEvaluationKey 词形闸门）——保守拒绝，绝不
    // 带病恢复（NFR-COR-03）。
    if (!isValidEvaluationKey(summary.evaluatorKey)) {
        result.reasons.push_back("evaluator-key-invalid");
    }
    // ---- 条件 2：切片身份（字节精确等值——输入变了就得从头跑）----
    if (summary.sliceId != request.requestSliceId) {
        result.reasons.push_back("slice-mismatch");
    }
    // ---- 条件 3：契约版本（EV-CPA-2 观测 token"contract-mismatch"）----
    // 契约升级后旧进度的求解状态不可信（求解器内部状态布局可能已变）。
    if (summary.evaluatorContractVersion != request.requestContractVersion) {
        result.reasons.push_back("contract-mismatch");
    }
    // ---- 条件 4：格式版本落在支持闭区间（C-3）----
    // 本构建读不懂的存储格式不得恢复（保守方向——宁重跑不可带病续跑）；
    // 区间常量随 execution/project 侧存储格式演进推进并留痕（§8.2 标题
    // 括注：存储归该两单元）。
    if (summary.checkpointFormatVersion < kCheckpointFormatVersionMin
        || summary.checkpointFormatVersion > kCheckpointFormatVersionMax) {
        result.reasons.push_back("checkpoint-format-incompatible");
    }
    // ---- 条件 5：完整性校验通过 ----
    // 存储侧载入校验未通过（或未做）的检查点一律拒绝——完整性标记是
    // "本体未损坏"的唯一凭据，缺凭据视同损坏（NFR-COR-03 不静默通过）。
    if (!summary.integrityVerified) {
        result.reasons.push_back("integrity-not-verified");
    }

    // ---- 两档裁决 ----
    // 五条件全过 → Resumeable；任一失配 → Incompatible。注意本判定**不
    // 消费**请求的 requestedMode/requestProfileIdentity（C-4——检查点恢复
    // 的是运行进度而非正式证据，可恢复性与任务成败/模式效力独立，
    // EV-CPA-3），也不存在 outcome 面（检查点没有"完成/未完成"语义）。
    if (result.reasons.empty()) {
        result.verdict = CheckpointCompatibilityResult::Resumeable;
    } else {
        result.verdict = CheckpointCompatibilityResult::Incompatible;
    }
    return result;
}

}  // namespace sdurws::ird::evidence
