/**
 * @file   EntryDetail.hpp
 * @brief  私有实现头：条目组装共用原语——码小写形、五元组规范串、去重键
 *         派生（§6.4）。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.2（dedupKey"构造时计算"——计算点＝工厂）、
 *     §6.4（DedupKey = {code, subject, scopeKind, scopeId}；"不同作用对象
 *     绝不合并"）、§4.5/P-DIAG-9（文案键冻结约定 diag.<code-lower>.*）
 *   - 任务契约 tasks/foundation/DIAG-T04.json（≙WP-09-T04）：工厂（Factory.
 *     cpp）创建条目时计算 dedupKey，目录投影（Catalog.cpp）导出安全摘要/
 *     文案键投影——两侧共用同一派生与派生辅助，避免同逻辑两实现漂移
 *     （单元内私有共享，不跨单元暴露——R-2：本头位于 src/，不入公共 include）
 *
 * 纯函数集合；线程安全（无共享状态）；确定性（NFR-COR-02）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_SRC_ENTRYDETAIL_HPP
#define SDURWS_IRD_DIAGNOSTICS_SRC_ENTRYDETAIL_HPP

#include <string>
#include <string_view>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>

namespace sdurws::ird::diagnostics::detail {

/// 码小写形（文案键材料——P-DIAG-9 冻结约定 diag.<code-lower>.title/detail）。
/// 注册边界拒绝偏离约定的键（Usage）——凡已注册码，按本函数派生必与其登记
/// 键一致（投影侧派生的无漂移依据）。
inline std::string codeLower(std::string_view code)
{
    std::string lower;
    lower.reserve(code.size());
    for (const char ch : code) {
        // 码句法保证仅 A-Z/0-9/'-'——ASCII 位移无 locale 依赖面
        lower.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
    }
    return lower;
}

/// 五元组规范串（Task scopeId 材料——各字段既有规范文本按序拼接，'|' 分隔；
/// 仅作会话态去重键材料，不持久化为身份）。
inline std::string taskScopeId(const core::TaskIdentity& task)
{
    std::string id = task.project.toCanonical();
    id += '|';
    id += task.branch.toCanonical();
    id += '|';
    id += task.revision.toCanonical();
    id += '|';
    id += task.run.toCanonical();
    id += '|';
    id += task.attempt.toCanonical();
    return id;
}

/**
 * @brief 由上下文与条目字段派生去重键（§6.4——派生口径＝单元卡 v0.5 登记）：
 *        context.task 存在→Task（scopeId＝五元组规范串）；否则 subject 存在→
 *        Object（scopeId＝subject 规范串）；二者皆无→None（用户级条目受
 *        subject 边界强制实际不可达，Dev 码不入目录）。
 *
 * 键含 subject ⇒ 不同作用对象绝不同键（DT-DUP-2 的键面保证）；键含任务
 * 五元组 ⇒ 跨任务不合并（§6.4"跨任务不聚合"）。
 */
inline DedupKey deriveDedupKey(const core::DiagnosticRecord& record, const DiagContext& context)
{
    DedupKey key;
    key.code = record.code;
    if (context.task.has_value()) {
        // 任务运行锚定优先：同任务内同码同对象的重复发现折叠计数；跨任务
        // 天然不同键（§6.4"跨任务不聚合"的键面保证）。
        key.scopeKind = ScopeKind::Task;
        key.scopeId = taskScopeId(*context.task);
    } else {
        // 对象锚定：无任务上下文的条目按作用对象分键。
        key.scopeKind = ScopeKind::Object;
        key.scopeId = record.subject.has_value() ? record.subject->toCanonical()
                                                 : std::string{};
    }
    key.subject = record.subject.has_value() ? *record.subject : core::ObjectId{};
    return key;
}

}  // namespace sdurws::ird::diagnostics::detail

#endif  // SDURWS_IRD_DIAGNOSTICS_SRC_ENTRYDETAIL_HPP
