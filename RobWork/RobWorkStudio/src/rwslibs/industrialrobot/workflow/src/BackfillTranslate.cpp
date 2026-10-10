/**
 * @file   BackfillTranslate.cpp
 * @brief  P-SEL-9 转译编排核的实现翻译单元（头文件为权威契约——本文件
 *         只承载 SelBackfillTranslateFlow::run 的编排序逐步兑现）。
 *
 * 设计依据：见 BackfillTranslate.hpp 文件头（P-SEL-9 登记原文、R-1 端口
 * 接缝形态、D-WF-7 零归码纪律）。本文件新增语义＝无（全部在头注释）。
 */

#include <sdurws/ird/workflow/BackfillTranslate.hpp>

#include <exception>
#include <utility>

#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope/CommandResult（①端口提交面）
#include <sdurws/ird/project/QueryPort.hpp>       // project::IProjectQueryPort/RevisionView/BranchTip/ObjectRef（查询面）

namespace sdurws {
namespace ird {
namespace workflow {

namespace {

/// 组装 UX-03 三字段失败呈现（Lifecycle.cpp 同名手法的本文件私有复刻
/// ——跨文件共享需要公共头值面，此处失败面字段语义一致，零加工组装）。
NewProjectFailure makeTranslateFailure(std::string context,
                                       std::string cause,
                                       std::string action)
{
    NewProjectFailure failure;
    failure.context = std::move(context);
    failure.cause = std::move(cause);
    failure.recommendedAction = std::move(action);
    return failure;
}

/// 组装 NoRecord 早退结果（零提交——selection 记录面不动的编排兑现）。
SelBackfillTranslateOutcome makeNoRecord(std::string summary)
{
    SelBackfillTranslateOutcome outcome;
    outcome.result = SelBackfillTranslateOutcome::Result::NoRecord;
    outcome.summary = std::move(summary);
    return outcome;
}

/// 组装失败结果（UX-03 三字段——对端事实透传，D-WF-7 零归码）。
SelBackfillTranslateOutcome makeFailed(std::string summary,
                                       std::string context,
                                       std::string cause,
                                       std::string action)
{
    SelBackfillTranslateOutcome outcome;
    outcome.result = SelBackfillTranslateOutcome::Result::Failed;
    outcome.summary = std::move(summary);
    outcome.failure = makeTranslateFailure(std::move(context), std::move(cause),
                                           std::move(action));
    return outcome;
}

/// 组装成功结果（①端口 Committed——新修订即 AT-21 同型观测点）。
SelBackfillTranslateOutcome makeSubmitted(std::string summary,
                                          core::RevisionId revision)
{
    SelBackfillTranslateOutcome outcome;
    outcome.result = SelBackfillTranslateOutcome::Result::Submitted;
    outcome.revisionId = std::move(revision);
    outcome.summary = std::move(summary);
    return outcome;
}

/// 解析编排的基线修订（expectedRevision 有值用之；nullopt＝分支 tip——
/// 从 branchTips 投影取目标分支的 tip，找不到即分支不存在＝数据侧失败）。
///
/// @return nullopt＝分支不存在（调用方已过 @pre，运行期分支表无此 id
///         属环境/数据侧事实——转 Failed 呈现而非 fail-fast）。
std::optional<core::RevisionId> resolveBaseline(project::ProjectStore& store,
                                                const SelBackfillTranslateRequest& request)
{
    if (request.expectedRevision.has_value()) {
        return request.expectedRevision;  // 显式预期——并发校验兜底在提交期 S2
    }
    for (const project::BranchTip& tip : store.query().branchTips()) {
        if (tip.id == request.branch) {
            return tip.tip;  // 分支 tip＝INV-M3 权威元数据的 HEAD 投影
        }
    }
    return std::nullopt;
}

}  // namespace

SelBackfillTranslateOutcome SelBackfillTranslateFlow::run(
    project::ProjectStore& store,
    const SelBackfillTranslateRequest& request,
    ISelBackfillRecordPort& recordPort,
    ISelBackfillTranslatePort& translatePort)
{
    // ---- 第 1 段：前置校验（调用方契约违约 fail-fast——分支无效说明
    // 调用序列有缺陷，修复调用方而不是捕获后继续；§10.3 错误语义行）。
    if (!request.branch.isValid()) {
        throw WorkflowError("转译编排请求分支无效（brn- 全零保留值）");
    }

    // ---- 第 2 段：记录定位（只读）。基线修订解析失败（分支不存在）或
    // 修订视图不可达＝数据侧事实，转 Failed 呈现（环境错误值轨道——
    // §10.3"环境/对端错误走值轨道呈现"行）。
    const std::optional<core::RevisionId> baseline = resolveBaseline(store, request);
    if (!baseline.has_value()) {
        return makeFailed("P-SEL-9 转译编排",
                          request.branch.toCanonical(),
                          "目标分支不存在或已不可达（branchTips 无此分支）",
                          "核对方案分支清单后重试");
    }
    std::optional<project::RevisionView> view;
    try {
        // 修订视图值快照（进程内值拷贝——tryRevision 契约）；闭包外/
        // 不存在为 nullopt（可见性纪律），结构违约抛 StoreError。
        view = store.query().tryRevision(*baseline);
    } catch (const std::exception& e) {
        return makeFailed("P-SEL-9 转译编排",
                          request.branch.toCanonical(),
                          std::string("修订视图查询失败：") + e.what(),
                          "检查项目存储完整性（对端诊断已透传）");
    }
    if (!view.has_value()) {
        return makeFailed("P-SEL-9 转译编排",
                          request.branch.toCanonical(),
                          "基线修订不在可见闭包内",
                          "核对期望基线修订（expectedRevision）是否已被推进");
    }

    // 修订闭包 objectRefs 按记录 token 扫描（词形路由键——零语义解释）。
    // 契约：每分支闭包恰一记录对象（selection"每命令恰一对象＋继承 oid
    // 改版"——多次回填走改版不新增）。零个＝无记录早退；多个＝对端记录
    // 面违约，如实呈现不带病转译。
    const project::ObjectRef* recordRef = nullptr;
    int recordCount = 0;
    for (const project::ObjectRef& ref : view->objectRefs) {
        if (ref.objectTypeToken == kSelBackfillRecordObjectToken) {
            ++recordCount;
            recordRef = &ref;
        }
    }
    if (recordCount == 0) {
        // selection 记录面未落（无回填事实）——零提交早退（"selection
        // 侧记录对象不动"；无记录不是错误，UX-03 诚实业务分支）。
        return makeNoRecord("基线闭包无 sel-device-backfill 记录");
    }
    if (recordRef == nullptr || recordCount > 1) {
        return makeFailed("P-SEL-9 转译编排",
                          request.branch.toCanonical(),
                          "修订闭包存在多份回填记录对象（应恰一）",
                          "检查 selection 回填记录面（对端契约违约）");
    }

    // ---- 第 3 段：记录取数（深拷贝——PA-3 值语义）。tryObject 为
    // noexcept 快照查询：nullopt＝不存在/关闭/损坏（§5.2 错误表）——
    // 环境侧失败，如实呈现（不抛出编排器）。
    std::optional<std::vector<std::uint8_t>> recordBytesOpt =
        store.query().tryObject(recordRef->objectId, recordRef->contentVersion);
    if (!recordBytesOpt.has_value()) {
        return makeFailed("P-SEL-9 转译编排",
                          recordRef->objectId.toCanonical(),
                          "回填记录对象负载不可读（引用缺失或上下文已关闭）",
                          "重试或检查项目存储完整性");
    }
    std::vector<std::uint8_t> recordBytes = std::move(*recordBytesOpt);

    // ---- 第 4 段：解码（缝一——selection 解码权威，零加工透传）。
    const ISelBackfillRecordPort::Decode decoded = recordPort.decode(recordBytes);
    if (!decoded.ok) {
        return makeFailed("P-SEL-9 转译编排",
                          recordRef->objectId.toCanonical(),
                          decoded.detail.empty()
                              ? "回填记录解码失败（selection 解码门拒绝）"
                              : decoded.detail,
                          "检查回填记录数据（对端诊断已透传）");
    }

    // ---- 第 5 段：编排复核（最小防御面——转译前提不成立即拒绝）。空轴
    // 表/参考系词表外＝记录面异常（环境事实），转 Failed 呈现而非
    // fail-fast（不是调用方错误——记录字节来自磁盘，不是调用序列缺陷）。
    std::string summary = "P-SEL-9 转译：";
    summary += std::to_string(decoded.facts.axes.size());
    summary += " 轴";
    if (decoded.facts.axes.empty()) {
        return makeFailed(summary,
                          recordRef->objectId.toCanonical(),
                          "回填记录零轴（转译无事实）",
                          "检查 selection 回填记录面");
    }
    if (decoded.facts.referenceFrameToken != kSelBackfillLinkFrameToken) {
        return makeFailed(summary,
                          recordRef->objectId.toCanonical(),
                          "回填记录参考系词表外（v1 冻结 link-frame）",
                          "检查回填记录参考系声明");
    }

    // ---- 第 6 段：转译（缝二——modeling DrivetrainDesign 组装＋Codec
    // 编码权威；拒绝＝数据侧，cause/action 透传）。
    const ISelBackfillTranslatePort::Translation translated =
        translatePort.translate(decoded.facts);
    if (!translated.ok) {
        return makeFailed(summary,
                          recordRef->objectId.toCanonical(),
                          translated.cause.empty()
                              ? "回填字段转译被对端拒绝"
                              : translated.cause,
                          translated.action.empty()
                              ? "检查权威模型传动面状态"
                              : translated.action);
    }

    // ---- 第 7 段：①端口提交（权威写入——token 为路由键）。信封逐字段
    // 组装（零补造）；requiresDualCompile 的双编译编排归 modeling
    // handler 注册面＋project S5（本编排核零双编译知识——PA-1）。
    project::CommandEnvelope envelope;
    envelope.branch = request.branch;
    envelope.expectedRevision = request.expectedRevision;
    envelope.commandType = std::string(kApplyDrivetrainDesignCommandToken);
    envelope.payloadFormatVersion = translated.draft.payloadFormatVersion;
    envelope.payloadCanonical = translated.draft.payloadCanonical;
    project::CommandResult result;
    try {
        result = store.commands().submit(envelope);
    } catch (const std::exception& e) {
        // submit 契约外的异常（装配违约 invalid_argument 等）＝环境侧
        // 事实，值轨道呈现（环境错误不抛出编排器——SaveAsFlow 同款纪律）。
        return makeFailed(summary,
                          recordRef->objectId.toCanonical(),
                          std::string("权威写入提交异常：") + e.what(),
                          "检查存储上下文状态后重试");
    }

    // ---- 第 8 段：结果折叠（D-WF-7 零加工——对端状态与诊断原样透传，
    // 只做 UX-03 半区组装，零归码零新增 WF- 码〔R1〕）。
    if (result.committed()) {
        // Committed 时 newRevision 唯一非空（§5.3.1 契约）——防御复核
        // 不成立即对端违约（"提交成功必须有修订"——RelinkFlow 同款纪律）。
        if (!result.newRevision.has_value()
            || !result.newRevision->isValid()) {
            return makeFailed(summary,
                              recordRef->objectId.toCanonical(),
                              "提交返回 Committed 但无有效新修订（对端违约）",
                              "登记问题并核查 project 命令服务");
        }
        return makeSubmitted(summary, *result.newRevision);
    }

    // Rejected/Aborted/Failed 三态全部转 Failed 呈现（都不产生修订——
    // §5.3.1 契约；原因组装取最高信息量来源：诊断首条→error→状态词形）。
    std::string cause;
    if (!result.diagnostics.empty()) {
        cause = result.diagnostics.front().code
                + std::string(": ") + result.diagnostics.front().cause;
    } else if (result.error.has_value()) {
        cause = result.error->what();
    } else if (result.status.rejected()) {
        cause = "权威写入被拒绝（提交期校验不过——并发基线/载荷/权限）";
    } else if (result.status.aborted()) {
        cause = "权威写入被中止（取消/上下文关闭）";
    } else {
        cause = "权威写入失败（环境/数据侧错误）";
    }
    return makeFailed(summary,
                      recordRef->objectId.toCanonical(),
                      std::move(cause),
                      "根据透传诊断处理后重试");
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
