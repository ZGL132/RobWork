/**
 * @file   PanelEditFlow.cpp
 * @brief  建模面板编辑流实现——L-2/L-8 的调用编排与结果分流（卡 §9.7.2）。
 *
 * 设计依据：units/modeling.md §9.7.2 L-2/L-8、§5.3 规则 2、§9.4.1；
 * 契约 WP-13-T15 acceptance 3。实现纪律：本文件零业务判定——接受/拒绝
 * 唯一由计算库函数裁决（applyJointFieldEdit/applyCentroidEdit）；分流即
 * 全部逻辑（接受→刷新＋脏通知；拒绝→就地出口）。
 */

#include "PanelEditFlow.hpp"

#include <charconv>

namespace sdurws::ird::modeling {
namespace {

// 惯量张量的确定性六分量摘要（"ixx, iyy, izz, ixy, ixz, iyz"——kg·m²；
// 单位标注归行字段，这里只出数值串。to_chars locale 无关——NFR-COR-02）。
std::string formatInertia(const InertiaTensor& t)
{
    const double comps[6] = {t.ixx, t.iyy, t.izz, t.ixy, t.ixz, t.iyz};
    std::string out;
    for (std::size_t i = 0; i < 6; ++i) {
        char buf[32];
        auto res = std::to_chars(buf, buf + sizeof(buf), comps[i],
                                 std::chars_format::fixed, 6);
        out.append(buf, res.ptr);
        if (i + 1 < 6) { out.append(", "); }
    }
    return out;
}

}  // namespace

// =====================================================================
// L-2 字段编辑流
// =====================================================================

EditSubmitOutcome submitJointFieldEdit(ModelingWorkingSet& ws, IPanelEditSink& sink,
                                       std::size_t jointIndex, JointEditField field,
                                       const JointEditValue& value)
{
    // 唯一判定点：域函数裁决接受/拒绝（拒绝时工作集字节不变——域内强保证，
    // T07 落位契约；本层不重复校验、不做回滚——回滚语义归域函数）。
    const std::optional<JointEditError> err =
        applyJointFieldEdit(ws, jointIndex, field, value);

    if (!err.has_value()) {
        // 接受分支（L-2）：增量刷新信号先行（subject 定位供裁剪刷新范围），
        // 脏通知随后（PM-04/PM-11——标题 `*`；会话级标记，零落盘）。
        sink.onEditApplied("joints[" + std::to_string(jointIndex) + "]");
        sink.notifySessionDirty();
        return EditSubmitOutcome::Applied;
    }

    // 拒绝分支（L-2）：就地错误出口（token＋域 detail 直投——UX-03 比较型
    // 原因呈现；无任何模态路径——UX-07 内联非模态）。
    EditRejection rejection;
    rejection.codeToken = std::string(jointEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    sink.onEditRejected(rejection);
    return EditSubmitOutcome::Rejected;
}

// =====================================================================
// L-8 平行轴内联二选一
// =====================================================================

CentroidChoicePreview centroidChoicePreview(const BodyData& body,
                                            const rw::math::Vector3D<double>& newCenterOfMass,
                                            const std::optional<InertiaTensor>& replacementTensor)
{
    CentroidChoicePreview preview;

    // 分支 (a) 预览：域 dry-run（副本上按 MigrateInertia 执行——平行轴
    // 迁移数值唯一由计算库产出，面板零推算）。前置违约（缺基准）时域拒绝，
    // 预览串保持空——内联只呈现可得数值。
    BodyData dryRun = body;  // 值拷贝——dry-run 不触碰工作集（输入 body 只读）
    if (!applyCentroidEdit(dryRun, newCenterOfMass, CentroidEditResolution::MigrateInertia,
                           std::nullopt)
             .has_value()) {
        if (dryRun.inertia.state() == core::FieldState::Provided) {
            preview.migratedText = formatInertia(dryRun.inertia.value());
        }
    }

    // 分支 (b) 预览：覆盖语义＝用户输入张量回显（域 dry-run 同样执行——
    // 前置一致性由同一域函数把关；无输入张量时该分支预览为空）。
    if (replacementTensor.has_value()) {
        BodyData dryRunB = body;
        if (!applyCentroidEdit(dryRunB, newCenterOfMass, CentroidEditResolution::OverwriteInertia,
                               replacementTensor)
                 .has_value()) {
            preview.overwrittenText = formatInertia(*replacementTensor);
        }
    }
    return preview;
}

CentroidSubmitOutcome submitCentroidEdit(BodyData& body, IPanelEditSink& sink,
                                         const rw::math::Vector3D<double>& newCenterOfMass,
                                         std::optional<CentroidEditResolution> choice,
                                         const std::optional<InertiaTensor>& replacementTensor)
{
    // 唯一判定点：域函数裁决三分支（§5.3 规则 2——(a) 迁移/(b) 覆盖/(c)
    // 未决议拒绝；拒绝时 body 不变是域内强保证）。
    const std::optional<ModelingError> err =
        applyCentroidEdit(body, newCenterOfMass, choice, replacementTensor);

    if (!err.has_value()) {
        // 接受分支：选择已随域变更记录留痕（V-17"编辑差值记录选择"——
        // 留痕载体在编辑器/命令层）；增量刷新＋脏通知同 L-2 口径。
        sink.onEditApplied("body.center-of-mass");
        sink.notifySessionDirty();
        return CentroidSubmitOutcome::Applied;
    }

    // 拒绝分支：CentroidEditUnresolved＝分支 (c)——widget 打开内联二选一
    // （非模态）；其余码＝就地拒绝（同 L-2）。token 统一经域错误 token
    // 函数产出（Errors.hpp 映射面——不私写第二码串）。
    EditRejection rejection;
    rejection.codeToken = std::string(modelingErrorCodeToken(err->code));
    rejection.detail = err->detail;
    sink.onEditRejected(rejection);
    return err->code == ModelingErrorCode::CentroidEditUnresolved
               ? CentroidSubmitOutcome::Unresolved
               : CentroidSubmitOutcome::Rejected;
}

}  // namespace sdurws::ird::modeling
