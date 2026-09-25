/**
 * @file   Capture.cpp
 * @brief  三维拾取/TCP 捕获域侧服务的实现——确认门→参数面→STALE 对账
 *         →构造校验→草稿写入的固定流程序（L-R6/L-R7）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（L-R6/L-R7 数据流原文）、§9.6（REQ-
 *     CAPTURE-STATE-STALE 行——warning；本文件即该码产码消费者）、
 *     §5.1（source 行：UserProvided＋methodTag=captured-tcp）、§8.1
 *     （浅校验边界——拾取目标/refFrame 仅查闭包元数据）、§4.6（草稿
 *     态——写入编辑器工作集，零修订）
 *   - 需求 REQ-08（写回前必须确认）、REQ-10（入草稿、未应用不失效）、
 *     AT-23、NFR-COR-01/02/03、NFR-MNT-04（createPoint 校验单点复用）
 *   - 任务契约 tasks/foundation/WP-14-T06.json acceptance 3/4
 *
 * 实现要点（对应 Capture.hpp 流程序说明，此处只记事实）：
 *   ①确认门在一切校验之前——"取消/关闭拾取态＝零数据变更"负向语义
 *     的确定性保证（未确认连参数诊断都不产——取消是正常流）；
 *   ②STALE 对账只读 draftStatus().baseRevisionId（编辑器状态投影——
 *     不引入第二基线真值）；警告登记后流程继续（知情写回——R-REQ-5
 *     缓解语义）；
 *   ③捕获条目构造复用 TaskPointService.createPoint 单点（I-REQ-3/5
 *     全链校验——与手工创建/导入同源，无第二套条目校验）。
 *
 * 线程安全：服务无成员状态——并发可重入；编辑器调用由调用方保证在
 * UI 线程（§3.4 总约定 2）。
 * 确定性：流程序固定；诊断 context 经固定拼装；objectId 除外同输入
 * 同产出（身份生成本质随机——与 Services 同口径）。
 */

#include <sdurws/ird/requirements/Capture.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/requirements/DiagCodes.hpp>   // kReqCaptureStateStale——产码唯一书写点
#include <sdurws/ird/requirements/Services.hpp>    // TaskPointService——条目构造校验单点

namespace sdurws::ird::requirements {

// =====================================================================
// CaptureConfirmation——工厂与不变量
// =====================================================================

CaptureConfirmation CaptureConfirmation::confirmed(std::string principal,
                                                   std::chrono::system_clock::time_point confirmedAtUtc)
{
    // 主体非空强制（C-2 同构：Confirmed 必携凭据；空主体＝凭据伪造
    // 未遂——调用方契约违约 fail-fast，AGENTS §3 错误二分）。
    if (principal.empty()) {
        throw std::invalid_argument(
            "requirements/capture: 确认凭据主体为空（SA-15——Confirmed 态"
            "必须携带非空 principal，采集归 ui/project）");
    }
    CaptureConfirmation c;
    c.state = core::ConfirmationState::Confirmed;
    c.credential.principal = std::move(principal);
    c.credential.confirmedAtUtc = confirmedAtUtc;
    return c;
}

CaptureConfirmation CaptureConfirmation::rejected()
{
    // 缺省即 Rejected 且无凭据（C-2 同构：Rejected 不携凭据——结构体
    // 缺省值已满足，显式工厂仅为调用面可读性）。
    return CaptureConfirmation{};
}

bool CaptureConfirmation::wellFormed() const noexcept
{
    // Confirmed ⇔ principal 非空（双向核对：Confirmed 无主体与
    // Rejected 携主体都判违约——凭据面不得半吊子）。
    if (state == core::ConfirmationState::Confirmed) {
        return !credential.principal.empty();
    }
    return true;  // Rejected/Pending：无凭据语义，形态恒合法
}

// =====================================================================
// 产出等值（RequirementError 无 operator==——逐字段手工比较；本类型
// 等值仅供测试断言，语义＝全字段一致）
// =====================================================================

bool CaptureOutcome::operator==(const CaptureOutcome& o) const
{
    if (accepted != o.accepted || rejectedUnconfirmed != o.rejectedUnconfirmed
        || draftEntry != o.draftEntry || diags != o.diags
        || error.code != o.error.code || error.params != o.error.params
        || error.detail != o.error.detail) {
        return false;
    }
    return true;
}

namespace {

// ---------------------------------------------------------------------
// 私有辅助（纯函数）
// ---------------------------------------------------------------------

/// 确认门（acceptance 4 负向语义的执行点）：未确认/凭据违约 → true
/// （调用方立即返回 rejectedUnconfirmed 产出——零数据变更、零诊断：
/// 取消是正常流，Canceled 轴非错误）。
bool confirmationGateFails(const CaptureConfirmation& confirmation)
{
    // 形态违约（Confirmed 无主体）与未决/拒绝同走零数据变更——形态
    // 违约额外是调用方缺陷，但域面仍以"未确认"统一拒绝（不抛：拒绝
    // 写回是本门的正常产出；伪造凭据的 fail-fast 已在 confirmed 工厂）。
    return !confirmation.wellFormed()
           || confirmation.state != core::ConfirmationState::Confirmed;
}

/// STALE 警告诊断（REQ-CAPTURE-STATE-STALE——warning；§9.6 T06 行）。
/// paramSchema 两键（session-revision/draft-baseline——错位两侧值对
/// 照呈现；subject=编辑器基线下的需求集根对象锚不可得，故 subject 取
/// nullopt——集合级状态警告，与 R5/R6 的集合级警告同形）。
core::DiagnosticRecord makeStaleDiag(const std::string& sessionRevisionId,
                                     const std::string& draftBaselineId)
{
    return core::DiagnosticRecord::make(
        std::string{kReqCaptureStateStale}, std::nullopt, std::nullopt,
        std::nullopt,
        "session-revision=" + (sessionRevisionId.empty() ? std::string{"<none>"} : sessionRevisionId)
            + "; draft-baseline=" + (draftBaselineId.empty() ? std::string{"<none>"} : draftBaselineId),
        "TCP 捕获时的会话基线与当前草稿基线不一致（会话状态与草稿基线错位"
        "——R-REQ-5；捕获值可能取自过期场景状态）",
        "重新捕获当前 TCP（消除错位），或知情确认沿用捕获值（写回后以 "
        "methodTag=captured-tcp 追溯重捕）");
}

/// 捕获值有限性核对（参数面——非法即 error：入草稿前的输入值面拒绝，
/// 构造边界纪律；不产诊断码——值面错误经 RequirementError 返回，§9.2）。
std::optional<RequirementError> capturedPoseViolation(const CapturedTcpPose& captured)
{
    const double p[3] = {captured.position[0], captured.position[1], captured.position[2]};
    const double r[3] = {captured.rpy[0], captured.rpy[1], captured.rpy[2]};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(p[i])) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "captured.position");
            e.detail = "requirements/capture: 捕获 TCP 位置含非有限值（m；"
                       "捕获值必须为有限数值——不改写不转零，NFR-COR-03）";
            return e;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(r[i])) {
            RequirementError e;
            e.code = RequirementErrorCode::IllegalTolerance;
            e.params.emplace_back("field", "captured.rpy");
            e.detail = "requirements/capture: 捕获 TCP 姿态含非有限值（rad；"
                       "捕获值必须为有限数值——不改写不转零，NFR-COR-03）";
            return e;
        }
    }
    return std::nullopt;
}

}  // namespace

// =====================================================================
// L-R7：TCP 捕获 → 确认 → Fixed 任务点入草稿
// =====================================================================

CaptureOutcome RequirementCaptureService::captureTcpAsFixedPoint(
    IRequirementEditor& editor, const CaptureTcpRequest& request,
    const CheckContext& closure) const
{
    CaptureOutcome out;

    // ---- ①确认门（REQ-08）：任何写路径前强制——未确认＝零数据变更
    // （acceptance 4 负向用例面；"确认前不落草稿"的落实）。
    if (confirmationGateFails(request.confirmation)) {
        out.rejectedUnconfirmed = true;
        return out;
    }

    // ---- ②捕获值参数面：非有限值拒绝（不改写不转零——NFR-COR-03）。
    if (auto e = capturedPoseViolation(request.captured)) {
        out.error = std::move(*e);
        return out;
    }

    // ---- ③refFrame/tcpRef 结构面（forScene/tcp 槽核对——World/DefaultTcp
    // 缺省合法；跨闭包半区在本步对携带目标 id 的引用浅核对）。结构违约＝
    // 请求装配错误，值面拒绝（不产诊断码——入草稿前输入面）。
    if (auto e = validateRequirementReference(request.captured.refFrame, true)) {
        out.error = std::move(*e);
        return out;
    }
    if (request.tcpRef.has_value()) {
        if (auto e = validateRequirementReference(*request.tcpRef, false)) {
            out.error = std::move(*e);
            return out;
        }
        // tcpRef 指向 tool-definition 对象——浅核对（悬空工具引用在此
        // 拦截；语义级有效性归评估，§8.1）。仅 Tool 种携带目标 id
        // （DefaultTcp 为无载荷缺省种——恒合法缺省，无核对面）。
        if (request.tcpRef->kind == RequirementRefKind::Tool) {
            if (auto violation = closureRefViolation(closure, *request.tcpRef->objectId,
                                                     expectedTargetToken(request.tcpRef->kind))) {
                out.error = RequirementError{};
                out.error.code = RequirementErrorCode::IllegalTolerance;
                out.error.params.emplace_back("field", "tcpRef");
                out.error.detail = *violation;
                out.diags.push_back(core::DiagnosticRecord::make(
                    std::string{kReqReadyRefMissing}, std::nullopt, std::nullopt,
                    std::nullopt,
                    "field=tcpRef; target=" + request.tcpRef->objectId->toCanonical()
                        + "; expected-token="
                            + std::string{expectedTargetToken(request.tcpRef->kind)},
                    *violation,
                    "修正捕获请求的工具/TCP 引用到修订闭包内存在的对象"));
                return out;
            }
        }
    }
    // refFrame 非 World 时浅核对（SceneObject/ModelFrame 目标须在闭包）。
    if (request.captured.refFrame.kind == RequirementRefKind::ModelFrame
        || request.captured.refFrame.kind == RequirementRefKind::SceneObject) {
        const std::string_view expected = expectedTargetToken(request.captured.refFrame.kind);
        if (auto violation = closureRefViolation(closure, *request.captured.refFrame.objectId,
                                                 expected)) {
            out.error = RequirementError{};
            out.error.code = RequirementErrorCode::IllegalTolerance;
            out.error.params.emplace_back("field", "captured.refFrame");
            out.error.detail = *violation;
            out.diags.push_back(core::DiagnosticRecord::make(
                std::string{kReqReadyRefMissing}, std::nullopt, std::nullopt,
                std::nullopt,
                "field=refFrame; target=" + request.captured.refFrame.objectId->toCanonical()
                    + "; expected-token=" + std::string{expected},
                *violation,
                "修正捕获请求的参考系引用到修订闭包内存在的对象"));
            return out;
        }
    }

    // ---- ④STALE 对账（R-REQ-5）：会话基线 vs 草稿基线——错位登记
    // warning（不阻断：确认门已过＝知情写回；重捕可消除）。
    const std::string draftBaseline = editor.draftStatus().baseRevisionId;
    if (request.captured.sessionRevisionId != draftBaseline) {
        out.diags.push_back(makeStaleDiag(request.captured.sessionRevisionId,
                                          draftBaseline));
    }

    // ---- ⑤条目构造＋校验（TaskPointService.createPoint 单点——与手工
    // 创建同源全链校验：I-REQ-3 名称唯一/I-REQ-5 位姿容差全链）：
    //   - 姿态规则＝Fixed，参数字面＝捕获 rpy（无换算直存——NFR-COR-03）；
    //   - 约束掩码＝六分量全约束（捕获位姿即完整位姿要求——x/y/z/roll/
    //     pitch/yaw 全 constrained；I-REQ-5 any()==true 满足）；
    //   - source＝UserProvided＋methodTag="captured-tcp"（§5.1 来源标记
    //     行原文——R-REQ-5 缓解第二半：条目自证捕获来源，可追溯重捕）。
    TaskPointSpec spec;
    spec.name = request.pointName;
    spec.level = RequirementLevel::Must;   // 捕获任务点缺省 Must（手工创建同缺省——不私设第二缺省）
    spec.enabled = true;
    spec.source = core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                              std::nullopt, std::nullopt,
                                              std::string{"captured-tcp"});
    spec.refFrame = request.captured.refFrame;
    spec.tcpRef = request.tcpRef;
    spec.pose.constrainedDof = ConstrainedDof{true, true, true, true, true, true};
    spec.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        request.captured.position, spec.source);
    spec.pose.orientation.kind = OrientationRuleKind::Fixed;
    spec.pose.orientation.fixedRpy = request.captured.rpy;
    spec.tolerance = request.tolerance;
    spec.siblingNames = request.siblingNames;

    // diags 追加传入 createPoint（其契约预留位——本服务无额外产码，
    // createPoint 亦不写入；传引用仅为签名一致性）。
    std::vector<core::DiagnosticRecord> constructDiags;
    const CreateOutcome created = TaskPointService{}.createPoint(spec, constructDiags);
    if (!created.ok) {
        out.error = created.error;  // 零数据变更（校验失败——构造边界拒绝）
        return out;
    }

    // ---- ⑥草稿写入（编辑器 upsert——拒绝面由编辑器校验链兜底：跨集合
    // id 冲突等；接受＝工作集 +1 编辑＋局部撤销入栈，零修订——§4.6）。
    const EditOutcome applied = editor.applyEdit(created.point);
    if (!applied.accepted) {
        out.error = applied.error;
        return out;
    }

    out.accepted = true;
    out.draftEntry = created.point;
    return out;
}

// =====================================================================
// L-R6：拾取结果 → 确认 → 目标任务点姿态规则更新入草稿
// =====================================================================

CaptureOutcome RequirementCaptureService::applyPickToOrientation(
    IRequirementEditor& editor, const ApplyPickRequest& request,
    const CheckContext& closure) const
{
    CaptureOutcome out;

    // ---- ①确认门（REQ-08）——同捕获流（取消/关闭拾取态＝零数据变更）。
    if (confirmationGateFails(request.confirmation)) {
        out.rejectedUnconfirmed = true;
        return out;
    }

    // ---- ②拾取目标装配（feature 分支——PickedFeature 注的两分法）。
    OrientationRule rule;
    if (request.picked.feature.has_value()) {
        // 几何特征拾取 → AlignGeometryNormal（MDL-15 场景对象浅引用）。
        if (request.picked.target.kind != RequirementRefKind::SceneObject) {
            // 特征只能挂在场景对象上（§5.3 AlignGeometryNormal 行——
            // targetSceneObject 是 scene-object）；Frame 对象无"几何特征"。
            out.error = RequirementError{};
            out.error.code = RequirementErrorCode::IllegalTolerance;
            out.error.params.emplace_back("field", "picked.target");
            out.error.detail = "requirements/capture: 几何特征拾取目标须为"
                               "场景对象（SceneObject——§5.3 AlignGeometry"
                               "Normal 引用面）";
            return out;
        }
        rule.kind = OrientationRuleKind::AlignGeometryNormal;
        rule.targetSceneObject = request.picked.target.objectId;
        rule.feature = request.picked.feature;
        rule.invertNormal = request.picked.invertNormal;
    } else {
        // Frame 拾取 → AlignFrame（目标＝ModelFrame/SceneObject 参考系）。
        rule.kind = OrientationRuleKind::AlignFrame;
        rule.targetFrame = request.picked.target;
    }

    // ---- ②（续）装配规则结构面核对（validateOrientationRule 单点——
    // 必须先于③的 ObjectId 解引用：结构非法的引用不携带目标 id，浅核对
    // 无从谈起；此处拒绝＝请求装配错误，值面返回）。
    if (auto e = validateOrientationRule(rule)) {
        out.error = std::move(*e);
        return out;
    }

    // ---- ③拾取目标浅核对（§8.1——存在＋token 匹配；过期场景的悬空
    // 拾取结果在此拦截）：可定位诊断回指目标任务点（此时目标条目尚未
    // 定位，先以 picked 目标为 subject——subject=目标任务点见④之后？
    // 不：诊断须回指**需求条目**（acceptance 2），故本核对放在④之后
    // 以取条目名。此处先只做核对语义，诊断在④后统一构造。
    // （执行序说明：②装配→④条目定位→③浅核对→⑤写入——本注释为
    // 阅读导航，实际代码序见下方标注。）

    // ---- ④目标任务点存在性（工作集点集内定位——拾取面板对既有行
    // 发起；引用不存在的条目＝值面拒绝，与编辑器 Remove 缺失同轨）。
    const RequirementWorkingSet& ws = editor.workingSet();
    const TaskPoint* target = nullptr;
    for (const TaskPoint& p : ws.points.entries) {
        if (p.objectId == request.targetPointId) {
            target = &p;
            break;
        }
    }
    if (target == nullptr) {
        out.error = RequirementError{};
        out.error.code = RequirementErrorCode::MalformedPayload;
        out.error.params.emplace_back("object-id", request.targetPointId.toCanonical());
        out.error.detail = "requirements/capture: 拾取写回目标任务点不存在"
                           "（工作集点集内无该 ObjectId——调用方引用违约）";
        return out;
    }

    // ---- ③（续）拾取目标浅核对——诊断回指目标条目（subject=条目 id、
    // localName=条目名——acceptance 2"回指需求条目（objectId＋name）"）。
    {
        const std::string_view expected =
            request.picked.feature.has_value()
                ? expectedTargetToken(RequirementRefKind::SceneObject)
                : expectedTargetToken(request.picked.target.kind);
        const core::ObjectId& pickedOid = *request.picked.target.objectId;
        if (auto violation = closureRefViolation(closure, pickedOid, expected)) {
            out.error = RequirementError{};
            out.error.code = RequirementErrorCode::IllegalTolerance;
            out.error.params.emplace_back("field", "picked.target");
            out.error.detail = *violation;
            out.diags.push_back(core::DiagnosticRecord::make(
                std::string{kReqReadyRefMissing}, target->objectId, target->name,
                std::nullopt,
                "field=pose.orientation; target=" + pickedOid.toCanonical()
                    + "; expected-token=" + std::string{expected},
                *violation,
                "重新拾取修订闭包内存在的对象（过期场景的拾取结果不写回）"));
            return out;
        }
    }

    // ---- ⑤草稿写入：拷贝目标任务点、仅替换姿态规则（其余字段原样
    // 保留——NFR-COR-03 不顺带改写）、upsert 回编辑器。
    TaskPoint updated = *target;
    updated.pose.orientation = rule;
    const EditOutcome applied = editor.applyEdit(updated);
    if (!applied.accepted) {
        out.error = applied.error;  // 编辑器校验链拒绝（零数据变更）
        return out;
    }

    out.accepted = true;
    out.draftEntry = std::move(updated);
    return out;
}

}  // namespace sdurws::ird::requirements
