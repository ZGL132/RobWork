/**
 * @file   OrientationResolution.cpp
 * @brief  姿态与方向规则解析的实现——五规则浅解析（参数面→引用面→产出
 *         装配的固定短路序）与可定位诊断产出。
 *
 * 设计依据：
 *   - units/requirements.md §5.3（五规则表＋隔离声明——"规则'解析'＝把
 *     规则参数解析为确定性参考姿态/方向并记录来源 resolution（kind＋
 *     目标 ObjectId＋解析值）"；"求解失败的'失败'指引用/参数错误，不
 *     是 IK 失败"）、§8.1（浅校验边界——仅查闭包 objectRefs 元数据）、
 *     §9.6（REQ-READY-REF-MISSING/REQ-READY-POSE-ILLEGAL 语义——解析
 *     侧同源复用）
 *   - 需求 REQ-09（五规则、应用时解析、解析失败可定位）、AT-23（V-03
 *     观测点：resolution 留痕＋诊断三要素）、NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-14-T06.json acceptance 1/2
 *
 * 实现要点（对应 OrientationResolution.hpp 执行序说明，此处只记事实）：
 *   ①参数面复用 validateOrientationRule 单点（构造边界/解码链/解析面
 *     三处同一套谓词——NFR-MNT-04）；
 *   ②引用面复用 closureRefViolation 单点（就绪层与解析层同一份浅核对
 *     ——同上；仅读 ObjectRef 的 objectId/objectTypeToken 两字段）；
 *   ③PointAtTarget 的方向归一化为 IEEE754 确定运算（乘加＋std::sqrt
 *     ——同输入同比特产出，NFR-COR-01）；Fixed 原样留痕参数字面（
 *     NFR-COR-03 不归一等效角）。
 *
 * 线程安全：无成员状态，全部辅助为纯函数——并发可重入。
 * 确定性：校验序固定；诊断 context 键值经固定拼装；无 locale/时钟依赖。
 */

#include <sdurws/ird/requirements/OrientationResolution.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace sdurws::ird::requirements {

namespace {

/// 条目名 → 诊断 localName（空名以 optional 空承载——不伪造名称；
/// 名称是语义字段，D-REQ-6）。
std::optional<std::string> anchorLocalName(const OrientationRuleAnchor& anchor)
{
    if (anchor.entryName.empty()) {
        return std::nullopt;
    }
    return anchor.entryName;
}

/// 参数面失败的可定位诊断（REQ-READY-POSE-ILLEGAL——R3 族同源复用）。
/// context 携 "field=<params.field>"：与就绪 R3 产码的同键对齐（面板
/// 定位跳转两处同形——呈现面不因产码方不同而分叉）。
core::DiagnosticRecord makePoseIllegalDiag(const OrientationRuleAnchor& anchor,
                                           const RequirementError& err)
{
    // params 首键约定＝"field"（RequirementTypes.cpp validate* 族）；
    // 缺省 "orientation"（空 params 的错误值——防御面，正常不可达）。
    std::string field = "orientation";
    if (!err.params.empty()) {
        field = err.params.front().second;
    }
    return core::DiagnosticRecord::make(
        std::string{kReqReadyPoseIllegal}, anchor.entryId, anchorLocalName(anchor),
        std::nullopt,
        "field=" + field,
        err.detail.empty() ? std::string{"姿态规则参数非法（§5.3）"} : err.detail,
        "修正该条目的姿态规则参数（欧拉角/目标点/特征/滚转区间——§5.3 五规则参数表）");
}

/// 引用面失败的可定位诊断（REQ-READY-REF-MISSING——R1 族同源复用）。
/// context 携 field/target/expected-token 三键：与就绪 R1 产码同键对齐
/// （acceptance 2"可定位诊断回指需求条目"——subject/localName 即条目）。
core::DiagnosticRecord makeRefMissingDiag(const OrientationRuleAnchor& anchor,
                                          std::string_view field,
                                          const core::ObjectId& target,
                                          std::string_view expectedToken,
                                          std::string_view violation)
{
    return core::DiagnosticRecord::make(
        std::string{kReqReadyRefMissing}, anchor.entryId, anchorLocalName(anchor),
        std::nullopt,
        "field=" + std::string{field} + "; target=" + target.toCanonical()
            + "; expected-token=" + std::string{expectedToken},
        std::string{violation},
        "修正姿态规则目标引用到修订闭包内存在的对象（浅核对——语义级有效性归评估时解析）");
}

}  // namespace

OrientationResolution resolveOrientationRule(const OrientationRule& rule,
                                             const OrientationRuleAnchor& anchor,
                                             const CheckContext& closure)
{
    OrientationResolution out;
    out.kind = rule.kind;  // resolution 来源 kind——无论成败都先留痕种类

    // ---- ①参数面：validateOrientationRule 单点复用（构造边界同源——
    // 非有限角/零向量目标/缺 feature/rollRange 逆序在此短路拒绝；诊断
    // 回指条目（subject=entryId/localName=entryName）＋REQ-READY-POSE-
    // ILLEGAL（R3 族码——不私设第二套参数非法码）。
    if (auto e = validateOrientationRule(rule)) {
        out.error = std::move(*e);
        out.diag = makePoseIllegalDiag(anchor, out.error);
        return out;  // 短路：参数非法时引用面无从谈起（载荷可能缺失）
    }

    // ---- ②引用面（仅引用型规则）：闭包浅核对（§8.1——仅查 objectRefs
    // 元数据，不解码 modeling 对象字节；期望 token 经 expectedTargetToken
    // 匹配表单点取得）。
    if (rule.kind == OrientationRuleKind::AlignFrame) {
        const std::string_view expected = expectedTargetToken(rule.targetFrame.kind);
        if (auto violation = closureRefViolation(closure, *rule.targetFrame.objectId,
                                                 expected)) {
            // 悬空/token 失配：可定位诊断回指条目（acceptance 2）；值面
            // 以 IllegalTolerance 承载（域错误表"引用悬空"的就近族——
            // 与 Readiness 值面同款取舍，机器判别看稳定码）。
            out.error = RequirementError{};
            out.error.code = RequirementErrorCode::IllegalTolerance;
            out.error.params.emplace_back("field", "targetFrame");
            out.error.detail = *violation;
            out.diag = makeRefMissingDiag(anchor, "targetFrame",
                                          *rule.targetFrame.objectId, expected,
                                          *violation);
            return out;
        }
        out.targetObjectId = *rule.targetFrame.objectId;  // 核对通过——留痕目标
        // 参考姿态 ≡ 目标参考系姿态：数值归评估时深度解析（§8.1——本层
        // 不产出数值，隔离声明防第二真值）。
    } else if (rule.kind == OrientationRuleKind::AlignGeometryNormal) {
        const std::string_view expected = expectedTargetToken(RequirementRefKind::SceneObject);
        if (auto violation = closureRefViolation(closure, *rule.targetSceneObject,
                                                 expected)) {
            out.error = RequirementError{};
            out.error.code = RequirementErrorCode::IllegalTolerance;
            out.error.params.emplace_back("field", "targetSceneObject");
            out.error.detail = *violation;
            out.diag = makeRefMissingDiag(anchor, "targetSceneObject",
                                          *rule.targetSceneObject, expected,
                                          *violation);
            return out;
        }
        out.targetObjectId = *rule.targetSceneObject;  // 核对通过——留痕目标
        out.feature = rule.feature;                    // 特征语义（数值法向归评估）
        out.invertNormal = rule.invertNormal;          // 取反标志留痕（解析值的一部分）
    }

    // ---- ③产出装配：按 kind 填充确定性解析值（字段有效性表见
    // OrientationResolution.hpp 注——其余字段保持缺省）。
    switch (rule.kind) {
    case OrientationRuleKind::Fixed:
        // 参考姿态＝参数字面原样留痕（rad，Z-Y-X；等效角不归一——
        // NFR-COR-03：参数编辑即内容变更的保守失效前提）。
        out.referenceRpy = rule.fixedRpy;
        break;
    case OrientationRuleKind::PointAtTarget:
        // 参考方向＝目标单位向量（refFrame 系内归一化——纯参数运算，
        // 非坐标变换；‖v‖ 经 IEEE754 确定运算，同输入同比特产出）。
        // 零向量已在①拒绝，此处范数恒 >0（除法安全）。
        {
            const double norm = std::sqrt(rule.targetPoint[0] * rule.targetPoint[0]
                                          + rule.targetPoint[1] * rule.targetPoint[1]
                                          + rule.targetPoint[2] * rule.targetPoint[2]);
            out.referenceDirection = rw::math::Vector3D<double>(
                rule.targetPoint[0] / norm, rule.targetPoint[1] / norm,
                rule.targetPoint[2] / norm);
        }
        break;
    case OrientationRuleKind::ToolRollFree:
        // 参考值＝滚转自由区间（rad，有序非空——①已核对 wellFormed）；
        // 主方向由伴随规则/固定主轴给出时组合表达（§5.3 第五规则行），
        // 组合解释归评估侧（本层不组合——防第二真值）。
        out.rollSpan = rule.rollRange;
        break;
    case OrientationRuleKind::AlignFrame:
    case OrientationRuleKind::AlignGeometryNormal:
        // 引用型：解析值已留痕目标 ObjectId＋特征语义（②）；数值姿态/
        // 法向归评估时深度解析——本层无更多产出。
        break;
    }

    out.ok = true;
    return out;
}

}  // namespace sdurws::ird::requirements
