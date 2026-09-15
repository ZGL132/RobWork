/**
 * @file   ScriptedDiagSource.hpp
 * @brief  ScriptedDiagSource——脚本化可控诊断源替身（§11 DIAG-T10 行产物：
 *         "按序列产出预设条目/finding/日志行"）。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 头注（`_contract_test` 面"ScriptedDiagSource
 *     基座"）、§11 DIAG-T10 行（产物定义与替身边界声明要求）、§9.2/§9.3/§9.6
 *     （条目/finding/日志行三形态的产出契约）
 *   - 任务契约 tasks/foundation/DIAG-T10.json acceptance 2（替身就位＋边界
 *     声明在案）、acceptance 3（P-DIAG-8 处置）
 *   - 先例：evidence/test/EvidenceTestDoubles.hpp 的 ScriptedEvaluator
 *     （EV-REG-3 同模式——本文件是其 diagnostics 侧对位物）
 *
 * ★ 替身边界声明（EV-REG-3 同模式；全文见本目录 README.md §1）：
 *   ScriptedDiagSource 的脚本输出**仅验证 diagnostics 契约**（条目/finding/
 *   日志行的产出路径与数据形状），**不构成任何真实业务错误、worker 崩溃、
 *   磁盘故障或碰撞/超限场景的正确性证明**；脚本数据不得冒充真实诊断证据。
 *   本替身只覆盖"可控诊断源"这一个测试设施位——它是 project/execution/ui
 *   等诊断生产方在阶段 A 的测试投影，真实生产方归各单元。
 *
 * 与被测产品对象的关系（EV-REG-3 模式第 3 条"被测产品对象不做替身"）：
 *   替身本身不实现任何产出逻辑——三个回放分派全部走**真实产品路径**：
 *     条目步   → DiagnosticsFactory::create（真实工厂校验）→ IDiagnosticSink::append
 *                （真实目录：去重/容量护栏/通知全生效）；
 *     finding 步 → IConfirmableFindingService::create（真实服务：绑定冻结/
 *                callbackToken 分配全生效）；
 *     日志步   → ILogger::log（真实两级日志管线：脱敏/截断/分流全生效）。
 *   替身只负责"按脚本序列供给预设数据"——因此本头文件**只被测试目标包含**
 *   （产品库 sdurws_ird_diagnostics 的源码面零本替身符号，由
 *   ScriptedDiagSourceTest 的 ScriptedDoubleBoundary 机检面结构性钉住——
 *   T-1 红线的替身侧对偶：替身也绝不进入产品源码面）。
 *
 * 线程约束：非线程安全——仅在测试执行线程使用（替身是单线程测试设施，
 * 无内部互斥；共享状态经回放进度表承载）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_TEST_SCRIPTEDDIAGSOURCE_HPP
#define SDURWS_IRD_DIAGNOSTICS_TEST_SCRIPTEDDIAGSOURCE_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Confirmable.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>

namespace sdurws::ird::diagnostics::testdoubles {

/**
 * @brief finding 步骤的 create 参数包（IConfirmableFindingService::create
 *        §9.3 原文八参中随脚本变化的七参——finding 本身另存）。
 *
 * 为什么打包成结构：脚本登记与回放分派之间要原样保存整组参数（值语义），
 * 逐参转发会退化成八参脚本函数；结构体字段与 create 签名一一对应，回放时
 * 逐字段透传给真实服务（不加工、不补默认——脚本即调用记录）。
 */
struct ScriptedFindingStep {
    core::ConfirmableFinding finding;             ///< 预设可确认诊断（必为比较型——core C-1）
    std::string commandType;                      ///< 来源命令类型 token（透传 create）
    core::ContentIdentity commandDigest{};        ///< 命令载荷摘要（透传 create）
    core::ProjectId project{};                    ///< 绑定项目（透传 create）
    core::BranchId branch{};                      ///< 绑定分支（透传 create）
    core::RevisionId baseRevisionId{};            ///< 绑定输入修订（透传 create）
    std::optional<core::ContentIdentity> policyContentId; ///< 策略来源身份（可空）
    std::vector<core::ObjectId> subjectScope;     ///< 作用对象集（透传 create）
};

/**
 * @brief 脚本化可控诊断源替身（§11 DIAG-T10 行产物——见文件头边界声明）。
 *
 * 使用协议：
 *   1. 装配期注入四个真实产品对象（工厂/目录/finding 服务/日志管线——引用
 *      须覆盖替身生命周期，替身不接管所有权）；
 *   2. scriptEntry/scriptFinding/scriptLog 按**期望产出顺序**登记脚本步骤
 *      （三种步骤共用一个序列——"按序列产出"即全局步进序）；
 *   3. replayNext() 步进一条（按步骤种类分派到真实产出路径）；
 *      replayAll() 一次回放到耗尽并返回回放步数；
 *   4. 经 producedEntryIds/producedFindingIds/logsSent 观测产出事实（再经
 *      真实目录/服务的查询面断言内容）。
 *
 * 错误语义：脚本载荷违反产品契约（如未注册码、Dev 码入目录）时，异常从
 * 真实工厂/服务/管线原样传播——替身**不捕获不吞错**（AGENTS.md 禁止吞错；
 * 脚本负例的验证归各行为套件自持，替身只承载正例契约形态数据）。
 */
class ScriptedDiagSource {
public:
    /// 步骤种类（§11 产物行三形态——条目/finding/日志行）。
    enum class StepKind { Entry, Finding, Log };

    /**
     * @brief 构造（装配期注入——四个引用的契约见各成员注释）。
     *
     * @param factory  [in] 真实诊断工厂（条目步经其 create 完成码表校验/
     *                 字段派生——create 非 const（entryId 分配是工厂状态），
     *                 本类相应持非 const 引用；引用生命周期须覆盖本替身）
     * @param sink     [in] 真实诊断目录（条目步 append 目标——容量/去重/
     *                 通知语义全为产品行为）
     * @param findings [in] 真实可确认诊断服务（finding 步 create 目标）
     * @param logger   [in] 真实两级日志管线（日志步 log 目标——异步入队）
     */
    ScriptedDiagSource(DiagnosticsFactory& factory,
                       IDiagnosticSink& sink,
                       IConfirmableFindingService& findings,
                       ILogger& logger)
        : m_factory(factory), m_sink(sink), m_findings(findings), m_logger(logger)
    {
    }

    // -----------------------------------------------------------------
    // 脚本登记（按调用序进入统一序列）
    // -----------------------------------------------------------------

    /**
     * @brief 登记"产出条目"步骤（预设记录＋上下文——回放时经真实工厂
     *        create 后 append 入目录）。
     *
     * @param record  [in] 预设诊断记录（正例契约形态——码须已注册且语义
     *                匹配上下文，违约会在回放时经产品路径抛出）
     * @param context [in] 预设关联身份块（sourceUnit/sourceInterface 必填
     *                ——工厂 Usage 校验面）
     */
    void scriptEntry(core::DiagnosticRecord record, DiagContext context)
    {
        m_entryRecords.push_back(std::move(record));
        m_entryContexts.push_back(std::move(context));
        m_sequence.push_back({StepKind::Entry, m_entryRecords.size() - 1});
    }

    /**
     * @brief 登记"产出 finding"步骤（回放时经真实服务 create——绑定冻结
     *        与 callbackToken 分配在服务端发生）。
     *
     * @param step [in] create 参数包（见 ScriptedFindingStep 字段注释）
     */
    void scriptFinding(ScriptedFindingStep step)
    {
        m_findingSteps.push_back(std::move(step));
        m_sequence.push_back({StepKind::Finding, m_findingSteps.size() - 1});
    }

    /**
     * @brief 登记"产出日志行"步骤（回放时经真实管线 log——异步入队，
     *        内容断言须先 flush，两级分流语义归管线）。
     *
     * @param record [in] 预设日志记录（channel/message 非空等入口校验在
     *                管线侧——违约回放时抛 Usage）
     */
    void scriptLog(LogRecord record)
    {
        m_logRecords.push_back(std::move(record));
        m_sequence.push_back({StepKind::Log, m_logRecords.size() - 1});
    }

    // -----------------------------------------------------------------
    // 回放（按序列步进——分派到真实产品路径）
    // -----------------------------------------------------------------

    /**
     * @brief 回放下一步（脚本耗尽返回 false——正常终态非错误）。
     *
     * @return true＝已回放一步；false＝脚本已耗尽（后续调用同样无害）
     *
     * @throws DiagnosticsError 脚本载荷违反产品契约时从真实工厂/服务/管线
     *         传播（替身不吞错——见类注释错误语义）
     */
    bool replayNext()
    {
        if (m_cursor >= m_sequence.size()) {
            return false;   // 耗尽：有界正常终态（回放进度单调，不回绕）
        }
        const ScriptedStep& step = m_sequence[m_cursor];
        switch (step.kind) {
        case StepKind::Entry: {
            // 条目步：真实工厂 create（码表校验/字段派生）→ 真实目录 append
            // （去重/容量/通知全生效）——记录目录分配的 entryId 供观测。
            const DiagnosticEntry entry =
                m_factory.create(m_entryRecords[step.slot], m_entryContexts[step.slot]);
            m_producedEntryIds.push_back(entry.entryId);
            m_sink.append(entry);
            break;
        }
        case StepKind::Finding: {
            // finding 步：真实服务 create（绑定四元组冻结/callbackToken
            // 分配在服务端）——记录返回的服务端记录供观测。
            const ScriptedFindingStep& payload = m_findingSteps[step.slot];
            const FindingRecord created = m_findings.create(
                payload.finding, payload.commandType, payload.commandDigest,
                payload.project, payload.branch, payload.baseRevisionId,
                payload.policyContentId, payload.subjectScope);
            m_producedFindingIds.push_back(created.findingId);
            break;
        }
        case StepKind::Log:
            // 日志步：真实管线 log（异步入队——返回即入队确认，§9.6）。
            m_logger.log(m_logRecords[step.slot]);
            ++m_logsSent;
            break;
        }
        ++m_cursor;
        return true;
    }

    /**
     * @brief 回放到耗尽。
     *
     * @return 本次调用实际回放的步数（此前已回放的不重复计——每步恰好
     *         执行一次，重复调用 replayAll 只会得到 0）
     */
    std::size_t replayAll()
    {
        const std::size_t before = m_cursor;
        while (replayNext()) {
        }
        return m_cursor - before;
    }

    // -----------------------------------------------------------------
    // 观测面（回放进度与产出事实——内容断言经真实目录/服务查询面进行）
    // -----------------------------------------------------------------

    /// 已回放步数（单调递增——回放进度的唯一权威）。
    std::size_t replayedCount() const { return m_cursor; }
    /// 剩余未回放步数。
    std::size_t remainingCount() const { return m_sequence.size() - m_cursor; }
    /// 登记总步数。
    std::size_t scriptedCount() const { return m_sequence.size(); }

    /// 按回放序产出的目录条目身份列表（entryId 由真实目录分配）。
    const std::vector<DiagEntryId>& producedEntryIds() const { return m_producedEntryIds; }
    /// 按回放序产出的 finding 身份列表（findingId 由真实服务分配）。
    const std::vector<FindingId>& producedFindingIds() const { return m_producedFindingIds; }
    /// 已提交日志行数（日志行内容断言经真实管线 flush 后的文件进行）。
    std::size_t logsSent() const { return m_logsSent; }

private:
    /// 序列步（种类＋载荷表下标——三通道载荷分表存放，序列只存路由信息）。
    struct ScriptedStep {
        StepKind kind;
        std::size_t slot;
    };

    DiagnosticsFactory& m_factory;              ///< 真实工厂（调用方持有，本类不接管）
    IDiagnosticSink& m_sink;                    ///< 真实目录（同上）
    IConfirmableFindingService& m_findings;     ///< 真实 finding 服务（同上）
    ILogger& m_logger;                          ///< 真实日志管线（同上）

    std::vector<ScriptedStep> m_sequence;                 ///< 统一步进序列
    std::vector<core::DiagnosticRecord> m_entryRecords;   ///< 条目步载荷：预设记录
    std::vector<DiagContext> m_entryContexts;             ///< 条目步载荷：预设上下文
    std::vector<ScriptedFindingStep> m_findingSteps;      ///< finding 步载荷
    std::vector<LogRecord> m_logRecords;                  ///< 日志步载荷

    std::size_t m_cursor = 0;                             ///< 回放游标（下一步下标）
    std::vector<DiagEntryId> m_producedEntryIds;          ///< 产出观测：条目身份
    std::vector<FindingId> m_producedFindingIds;          ///< 产出观测：finding 身份
    std::size_t m_logsSent = 0;                           ///< 产出观测：日志行计数
};

}  // namespace sdurws::ird::diagnostics::testdoubles

#endif  // SDURWS_IRD_DIAGNOSTICS_TEST_SCRIPTEDDIAGSOURCE_HPP
