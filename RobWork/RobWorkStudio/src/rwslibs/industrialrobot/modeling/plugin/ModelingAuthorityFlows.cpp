/**
 * @file   ModelingAuthorityFlows.cpp
 * @brief  权威切换流（modeling.switch-authority——L-9 判定先行）。独立
 *         编译单元。显式→DH 必须通过实际 runtime 编译多构型等价门。
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
        if (session.compileProbe == nullptr) {
            summary = "编译验证服务未就绪，保留显式权威与原草稿";
            host.showInfo(QStringLiteral("权威切换判定"), QString::fromStdString(summary));
            return false;
        }
        // 先构造候选并验证真实 runtime 多构型；失败时不写入会话任何字段。
        const auto& baseline = session.baselineSnapshot ? *session.baselineSnapshot : session.draft;
        const auto decision = prepareAuthoritySwitch(session.draft, baseline, converter,
                                                     *session.compileProbe, diags);
        if (!decision.allowed) {
            summary = decision.error ? decision.error->detail : "权威切换验证失败";
            host.showInfo(QStringLiteral("权威切换判定"), QString::fromStdString(summary));
            return false;
        }
        session.draft = decision.candidate;
        ModelingChangeRecord record;
        record.subject = "design.authority";
        record.summary = "权威参数化切换：显式→DH（真实编译多构型等价）";
        session.draft.changes.push_back(std::move(record));
        summary = "权威切换完成：显式→DH（真实编译多构型等价）";
        host.showInfo(QStringLiteral("权威切换判定"), QString::fromStdString(summary));
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
        chainJoint.maxVelocity = j.maxVelocity;
        chainJoint.maxAcceleration = j.maxAcceleration;
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
    session.draft.design.flangeOffset = expanded.flangeOffset * session.draft.design.flangeOffset;
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
