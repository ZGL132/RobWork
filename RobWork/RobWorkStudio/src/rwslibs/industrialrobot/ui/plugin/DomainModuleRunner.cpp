/**
 * @file   DomainModuleRunner.cpp
 * @brief  域模块应用编排器实现——draft.apply 多模块遍历（契约面见
 *         DomainModuleRunner.hpp；四态回执映射与首版 orchestrateApplyDraft
 *         逐字同源——T03b-2 既有行为在多域形态下的语义保持）。
 */

#include "DomainModuleRunner.hpp"

#include <algorithm>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

// ---- DomainApplyReport 聚合查询（调用方呈现/留痕的计数值源）------------

std::size_t DomainApplyReport::noDraftCount() const
{
    // NoDraft＝buildDraftCommand 返回 nullopt 的条目（§8.5 合法形态）。
    return static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [](const DomainApplyEntryReport& e) {
            return e.outcome == DomainApplyEntryReport::Outcome::NoDraft;
        }));
}

std::size_t DomainApplyReport::committedCount() const
{
    return static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(),
        [](const DomainApplyEntryReport& e) { return e.committed; }));
}

std::size_t DomainApplyReport::submittedCount() const
{
    // 提交数＝条目总数−无草稿数（Submitted 两态〔成/败〕都计入——都发生
    // 过对端 submit 调用）。
    return entries.size() - noDraftCount();
}

bool DomainApplyReport::anyCommitted() const noexcept
{
    return committedCount() > 0;
}

// ---- 遍历编排（acceptance 2 本体）--------------------------------------

namespace {

/**
 * @brief 对端 CommandResult → ui 回执投影的四态映射（与首版
 *        orchestrateApplyDraft 的映射逐字同源——T03b-2 既有语义在遍历
 *        形态下的保持；拒绝 token 词表与对端 CommandStatus::Rejection
 *        七值一一对应）。
 *
 * @param result [in] 对端命令结果
 * @return 投影值（status/newRevision/rejectionReason/abortReason——
 *         Stale 冲突定位投影由 DraftController 侧按 reason 组装，此处
 *         只携 token，与首版形态一致）
 */
CommandResultProjection projectCommandResult(const project::CommandResult& result)
{
    CommandResultProjection projection;
    if (result.status.committed()) {
        projection.status = CommandResultProjection::Status::Committed;
        projection.newRevision = result.newRevision;
    } else if (result.status.rejected()) {
        projection.status = CommandResultProjection::Status::Rejected;
        using Rj = project::CommandStatus::Rejection;
        switch (result.status.rejection) {
            case Rj::StaleRevision:           projection.rejectionReason = "stale-revision"; break;
            case Rj::ConfirmationsRejected:   projection.rejectionReason = "confirmations-rejected"; break;
            case Rj::ConfirmationsUnresolved: projection.rejectionReason = "confirmations-unresolved"; break;
            case Rj::HardAssertFailed:        projection.rejectionReason = "hard-assert-failed"; break;
            case Rj::UnknownCommand:          projection.rejectionReason = "unknown-command"; break;
            case Rj::InvalidPayload:          projection.rejectionReason = "invalid-payload"; break;
            case Rj::NotWritable:             projection.rejectionReason = "not-writable"; break;
        }
    } else if (result.status.aborted()) {
        projection.status = CommandResultProjection::Status::Aborted;
        projection.abortReason = result.status.abort == project::CommandStatus::Abort::Canceled
                                     ? "canceled" : "interaction-lost";
    } else {
        projection.status = CommandResultProjection::Status::Failed;
    }
    return projection;
}

}  // namespace

DomainApplyReport runDomainApply(
    const std::vector<DomainModuleEntry>& entries,
    const std::optional<std::pair<core::BranchId, core::RevisionId>>& anchor,
    const DomainCommandSubmitFn& submit,
    project::ICommandInteraction* interaction,
    const std::function<void(const std::string&)>& devLog)
{
    DomainApplyReport report;
    if (!submit) {
        // 提交函数缺失＝装配缺陷：遍历退化空报告＋留痕，不抛不虚构
        // （调用方见零条目报告即知网关未接线）。
        if (devLog) {
            devLog("runDomainApply: submit 未接线——遍历空跑（装配缺陷留痕）");
        }
        return report;
    }

    // 逐条目推进（登记序＝遍历序——NFR-COR-02；任一条目失败不传染其余
    // 条目：§11.3 失败隔离语义在应用编排面的延伸——单域提交异常〔对端
    // 抛 StoreError 等〕按 Failed 投影承载该域，其余域照常）。
    for (const DomainModuleEntry& entry : entries) {
        DomainApplyEntryReport row;
        row.moduleId = entry.moduleId;

        // ①锚同步（组装前必须锚定——T03b-2c 纪律的遍历推广；无锚语义域
        //   跳过本步，nullopt anchor 同样如实跳过）。
        if (entry.bindAnchor && anchor.has_value()) {
            entry.bindAnchor(anchor->first, anchor->second);
        }

        // ②域信封组装（§8.5 域侧半区——域内唯一入口；nullopt＝无草稿可
        //   应用，记行跳过。模块空指针＝装配缺陷，同 NoDraft 处理＋留痕
        //   ——不虚构提交，也不中断其余域）。
        std::optional<project::CommandEnvelope> envelope;
        if (entry.module != nullptr) {
            try {
                envelope = entry.module->buildDraftCommand(entry.moduleId);
            } catch (const std::exception& error) {
                // 域内组装异常＝该域缺陷：按提交失败承载（无提交发生——
                // 记 Submitted+未 committed＋原因），其余域照常（隔离）。
                row.outcome = DomainApplyEntryReport::Outcome::Submitted;
                row.rejectionReason = std::string("build-draft-command-failed: ") + error.what();
                report.entries.push_back(std::move(row));
                if (devLog) {
                    devLog("draft.apply[" + entry.moduleId + "]: 组装异常 "
                           + error.what());
                }
                continue;
            }
        } else if (devLog) {
            devLog("draft.apply[" + entry.moduleId + "]: 模块指针为空（装配缺陷留痕）");
        }
        if (!envelope.has_value()) {
            row.outcome = DomainApplyEntryReport::Outcome::NoDraft;
            report.entries.push_back(std::move(row));
            if (devLog) {
                devLog("draft.apply[" + entry.moduleId + "]: no-draft（跳过提交）");
            }
            continue;
        }

        // ③提交（§5.3.1 submit 唯一写路径——对端权威校验；异常折叠为
        //   Failed 承载，不中断遍历）。
        row.outcome = DomainApplyEntryReport::Outcome::Submitted;
        project::CommandResult result;
        bool submitThrew = false;
        try {
            result = submit(std::move(*envelope), interaction);
        } catch (const std::exception& error) {
            submitThrew = true;
            row.rejectionReason = std::string("submit-failed: ") + error.what();
            if (devLog) {
                devLog("draft.apply[" + entry.moduleId + "]: 提交异常 "
                       + error.what());
            }
        }
        if (!submitThrew) {
            // ④回执（投影映射→DraftController〔可空跳过〕；Committed→域
            //   回执处理〔锚前移/根回填——可空跳过〕）。
            const CommandResultProjection projection = projectCommandResult(result);
            if (entry.onResult) {
                entry.onResult(projection);
            }
            row.committed = result.committed();
            if (row.committed && result.newRevision.has_value()) {
                row.revision = result.newRevision->toCanonical();
            } else if (projection.status == CommandResultProjection::Status::Rejected) {
                row.rejectionReason = projection.rejectionReason;
            } else if (projection.status == CommandResultProjection::Status::Aborted) {
                row.rejectionReason = projection.abortReason;
            } else if (projection.status == CommandResultProjection::Status::Failed) {
                row.rejectionReason = "failed";
            }
            if (row.committed && entry.onCommitted) {
                entry.onCommitted(result);
            }
        }
        report.entries.push_back(std::move(row));
        if (devLog) {
            devLog("draft.apply[" + entry.moduleId + "]: committed="
                   + (row.committed ? "1" : "0")
                   + (row.committed ? " rev=" + row.revision
                                    : " reason=" + row.rejectionReason));
        }
    }
    return report;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
