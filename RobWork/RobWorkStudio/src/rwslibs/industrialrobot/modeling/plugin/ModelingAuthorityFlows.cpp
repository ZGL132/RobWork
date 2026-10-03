/**
 * @file   ModelingAuthorityFlows.cpp
 * @brief  权威切换流（modeling.switch-authority——L-9 判定先行）。独立
 *         编译单元：DhConvert.hpp 与 XacroExpand.hpp 各定义同名
 *         modeling::ExpandOutcome（值面同_namespace 不可共 TU——拆分纪律
 *         见 ModelingFlowsInternal.hpp）。
 */

#include "ModelingFlowsInternal.hpp"

#include <QString>

#include <algorithm>
#include <utility>

#include <sdurws/ird/modeling/DhConvert.hpp>  // DhExplicitConverter（五状态判定/无损展开）

namespace sdurws::ird::modeling {

bool executeSwitchAuthorityFlow(ModuleSessionState& session,
                                ModelingDialogHost& host,
                                std::string& summary)
{
    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;

    if (session.draft.design.authority == AuthorityMode::Explicit) {
        // 显式→DH：五状态判定（结构适用性先行——NotExpressible 不求解）。
        const DhConversionResult result =
            converter.explicitToDh(session.draft.design.joints, diags);
        QString reportText;
        if (result.determination == DhDetermination::Exact
            || result.determination == DhDetermination::ExactNonUnique) {
            // 仅精确态允许落切换（L-9——Approximate/NotExpressible 阻断呈现）。
            const std::size_t n =
                std::min(result.parameters.size(), session.draft.design.joints.size());
            for (std::size_t i = 0; i < n; ++i) {
                session.draft.design.joints[i].dhDerived = result.parameters[i];
            }
            session.draft.design.authority = AuthorityMode::StandardDH;
            ModelingChangeRecord record;
            record.subject = "design.authority";
            record.summary = "权威参数化切换：显式→DH（判定 Exact/ExactNonUnique）";
            session.draft.changes.push_back(std::move(record));
            reportText = QStringLiteral("判定结论：Exact/ExactNonUnique——已切换为 DH 权威。\n"
                                        "逐关节 DH 参数已写入草稿（轴/原点转为派生只读）。\n"
                                        "误差度量 E=%1")
                             .arg(result.errorMetricE, 0, 'g', 6);
        } else {
            reportText = QStringLiteral("判定结论：%1——切换被阻断。\n"
                                        "仅 Exact/ExactNonUnique 允许发起切换（L-9）。\n"
                                        "误差度量 E=%2")
                             .arg(QString::fromLatin1(dhDeterminationToken(result.determination)))
                             .arg(result.errorMetricE, 0, 'g', 6);
        }
        host.showInfo(QStringLiteral("权威切换判定"), reportText);
        summary = "权威切换判定完成："
                  + std::string(dhDeterminationToken(result.determination));
        return true;
    }

    // DH→显式：无损展开（DH 链只参数化旋转关节——Prismatic/Fixed 链被拒）。
    DhChain chain;
    chain.joints.reserve(session.draft.design.joints.size());
    for (const JointEntry& j : session.draft.design.joints) {
        if (!j.dhDerived.has_value()) {
            summary = "DH 权威数据缺失（关节 " + j.localName + " 无 DH 参数）——展开中止";
            return false;
        }
        DhChainJoint chainJoint;
        chainJoint.dh = *j.dhDerived;
        chainJoint.zeroOffset = j.zeroOffset;
        chainJoint.type = j.type;
        chainJoint.objectId = j.objectId;
        chainJoint.localName = j.localName;
        chainJoint.bounds = j.bounds;
        chainJoint.workingRange = j.workingRange;
        chain.joints.push_back(std::move(chainJoint));
    }
    const auto expanded = converter.dhToExplicit(chain, diags);
    if (!expanded.ok) {
        host.showInfo(QStringLiteral("权威切换判定"),
                      QStringLiteral("展开失败：%1").arg(
                          QString::fromLatin1(dhErrorCodeToken(expanded.errorCode))));
        summary = "DH→显式展开失败：" + std::string(dhErrorCodeToken(expanded.errorCode));
        return false;
    }
    session.draft.design.joints = expanded.joints;  // 展开产物即完整 JointEntry（透传两态权威字段）
    session.draft.design.authority = AuthorityMode::Explicit;
    ModelingChangeRecord record;
    record.subject = "design.authority";
    record.summary = "权威参数化切换：DH→显式（无损展开）";
    session.draft.changes.push_back(std::move(record));
    summary = "权威切换完成：DH→显式（无损展开）——轴/原点恢复可编辑";
    return true;
}

}  // namespace sdurws::ird::modeling
