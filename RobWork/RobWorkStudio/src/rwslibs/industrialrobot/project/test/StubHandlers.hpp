/**
 * @file   StubHandlers.hpp
 * @brief  PRJ-T10 测试处理器与测试支撑桩（§12 PRJ-T10 行"test/
 *         StubHandlers.*"）——命令服务用例组的注入面：域处理器桩（断言
 *         判定注入——P-PR-3 处置的测试侧形态）、双编译端口桩（§5.3.6
 *         "阶段 A 以测试桩实现验证编排"）、确认交互桩（SA-15）、事件
 *         记录面与共享夹具。PRJ-T13 增补对称逆命令处理器桩
 *         AppendUndoHandler（撤销/重做用例的可逆命令族——见其类注释）。
 *
 * 设计依据：
 *   - units/project.md §5.3.2（ICommandHandler 契约——prepare 三态与
 *     CommandPlan 声明面）、§5.3.3（ICommandInteraction——回调三态）、
 *     §5.3.6（IModelCompilePort——"阶段 A 以测试桩实现验证编排；真实
 *     编译链随 runtime/modeling 交付"）、§6.1～§6.6（S1～S7 各拒绝/
 *     中止面的注入需求——§11 PRJ-TX 组用例表）、P-PR-3（断言判定由
 *     注入处理器执行——本头即"注入处理器"的测试实现）、§6.9（对称
 *     逆命令声明族——AppendUndoHandler 的声明面依据，PRJ-T13）；
 *   - 任务契约 tasks/foundation/PRJ-T10.json acceptance 3/4（测试处理
 *     器〔断言判定注入〕就位）、tasks/foundation/PRJ-T13.json
 *     acceptance 1～2（可逆命令族的撤销/重做面）。
 *
 * 背景说明（为什么处理器桩用 test- 无点 token——P-PR-9 不越界声明）：
 *   本头全部 token 遵守 §4.4.4 冻结语法 ^[a-z0-9-]{3,64}（不含点）。
 *   §6.5 内置元数据命令族（project.create-branch 等，含点示例）属
 *   P-PR-9 待裁决面——测试桩不代裁决：建支语义经 "test-create-branch"
 *   （无点）走 CommandPlan.metadataChange 声明面验证**机制**（元数据
 *   增量装配/发布校验/查询视图），命令族命名归所有者裁决后的内置处理
 *   器落位任务。
 *
 * 测试域语义约定（本头自有的"域"口径——project 不解释，仅测试内部
 *   自洽）：载荷字节即对象负载原样（append 族）；create-branch 载荷
 *   ＝两行文本（第 1 行＝源分支规范文本，第 2 行＝新分支 label）。
 *   诊断码 TEST-* 为测试域自有码（core C-3 句法合法；码值登记权威在
 *   测试域——生产域码值仍归 diagnostics StableCodeRegistry，CR-08 不
 *   受本头影响）。
 *
 * 线程安全：处理器桩的无状态方法（prepare）可跨线程；带观测状态的桩
 * （StubCompilePort/StubInteraction/RecordingEventSink）由用例自行串行
 * （命令槽保证 prepare 串行——§6.1）；ServiceFixture 不可复制。
 */

#ifndef SDURWS_IRD_PROJECT_TEST_STUBHANDLERS_HPP
#define SDURWS_IRD_PROJECT_TEST_STUBHANDLERS_HPP

#include <gtest/gtest.h>

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "CommandServiceImpl.hpp"
#include "ConfirmationFlow.hpp"
#include "ProjectStoreImpl.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sdurws::ird::project::stub {

// =====================================================================
// 诊断捕获 sink（§5.0 IDiagnosticsSink 测试实现——P-PR-6 断言面）
// =====================================================================

/**
 * @brief 捕获型 sink：用户级记录与开发级消息分列捕获（ProjectStoreTest
 *        同款形态——共享化为本头，供命令服务用例组断言稳定码）。
 */
class StubSink : public IDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const core::DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }

    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }

    /// 码值逐字匹配（P-PR-6：码值＝diagnostics.md §4.6 收编清单）。
    [[nodiscard]] bool hasUserCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool devContains(const std::string& needle) const
    {
        for (const auto& d : devs) {
            if (d.second.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// =====================================================================
// 事件记录面（§6.1 S7/§6.8——提交事件 FIFO 与 D-18 观测）
// =====================================================================

/**
 * @brief 事件收集 sink（core IDomainEventSink 测试实现）——记录经总线
 *        投递的全部事件（投递序即调用序——同发布者 FIFO 观测面）。
 */
class RecordingEventSink : public core::IDomainEventSink {
public:
    std::vector<core::DomainEvent> events;

    void onEvent(const core::DomainEvent& event) override
    {
        events.push_back(event);
    }
};

// =====================================================================
// 域处理器桩（§5.3.2 ICommandHandler 实现——断言判定注入）
// =====================================================================

/// 追加对象命令（"test-append-object" v1）：载荷＝对象负载原样。
/// allocateInPrepare＝true 时经 ctx.objectId() 取号（HandlerContext 分配
/// 面），否则留空（S6 装配点分配）——两条"project 分配"路径都覆盖。
/// declareInverse＝true 时声明逆命令（§6.9 可逆性声明面的持久化观测）。
class AppendObjectHandler final : public ICommandHandler {
public:
    bool allocateInPrepare = false;
    bool declareInverse = false;

    [[nodiscard]] std::string commandType() const override
    {
        return "test-append-object";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& ctx, const CommandEnvelope& envelope,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        ObjectWrite write;
        if (allocateInPrepare) {
            write.objectId = ctx.objectId();  // prepare 期取号（载荷可内嵌身份）
        }
        write.objectTypeToken = "TestData";
        write.payloadCanonical = envelope.payloadCanonical;
        out.objectWrites.push_back(std::move(write));
        if (declareInverse) {
            out.inverseCommandType = "test-append-undo";
            out.inversePayloadCanonical = envelope.payloadCanonical;
        }
        out.summary = "test append object";
        return PrepareOutcome::Planned;
    }
};

/// 追加撤销命令（"test-append-undo" v1——PRJ-T13 撤销/重做用例的可逆
/// 命令族对称半边）：与 AppendObjectHandler（declareInverse=true）构成
/// 对称声明族——append 的逆＝本命令（载荷原样），本命令的逆＝append
/// （载荷原样）。测试域语义：执行"撤销追加"＝纯元数据修订（不写对象
/// ——测试域内"移除引用"的最简表达；引用集级回退的业务判定归域处理
/// 器，project 只提交——§6.5/§6.9 可逆性声明制）。undo/redo 用例经它
/// 断言：撤销修订的 inverse＝原始 append 命令（redo 信封的数据源——
/// InverseRecord 拷贝面）；未注册本处理器时 undo 透传 unknown-command
/// （§6.5 注册协议——project 不代行业务逆命令的拒绝面）。
class AppendUndoHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-append-undo";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& envelope,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        // 纯元数据修订（objectWrites 为空——CommitPlan 的合法形态）；
        // 逆声明＝原始 append 命令（载荷原样回写——对称族约定）。
        out.inverseCommandType = "test-append-object";
        out.inversePayloadCanonical = envelope.payloadCanonical;
        out.summary = "test append undo";
        return PrepareOutcome::Planned;
    }
};

/// 硬断言失败命令（"test-hard-assert-fail" v1）：prepare 返回
/// RejectedHardAssert＋定位诊断（§6.3 行 3 注入面）。
class HardAssertFailHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-hard-assert-fail";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& /*envelope*/,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& /*out*/,
                           std::vector<core::DiagnosticRecord>& diags) override
    {
        diags.push_back(core::DiagnosticRecord::make(
            std::string{"TEST-HARD-ASSERT-FAILED"}, std::nullopt,
            std::nullopt, std::nullopt,
            "test: 硬断言失败注入（MDL-06 就地阻止面）", "payload=unacceptable",
            "test: 修正输入后重试"));
        return PrepareOutcome::RejectedHardAssert;
    }
};

/// 非法输入命令（"test-invalid-input" v1）：prepare 返回
/// RejectedInvalidInput＋逐项诊断（§6.3 行 2 后半注入面）。
class InvalidInputHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-invalid-input";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& /*envelope*/,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& /*out*/,
                           std::vector<core::DiagnosticRecord>& diags) override
    {
        diags.push_back(core::DiagnosticRecord::make(
            std::string{"TEST-INVALID-INPUT"}, std::nullopt,
            std::nullopt, std::nullopt,
            "test: 非法输入注入（域口径逐项诊断面）", "payload=malformed",
            "test: 参照处理器受理格式重试"));
        return PrepareOutcome::RejectedInvalidInput;
    }
};

/// 待确认命令（"test-confirm-required" v1）：计划携带一个比较型
/// ConfirmableFinding（SA-15 S4 编排面注入；core C-1——比较型必带
/// comparison 三要素）。
class FindingsHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-confirm-required";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& /*envelope*/,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        // 比较型诊断（M-10 形态的测试面：实际/期望两侧＋单位）。
        core::ComparativeFields comparison;
        comparison.actual.quantity = core::SourcedValue<double>::provided(
            2.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.actual.unit = *core::UnitToken::find("m");
        comparison.expected.quantity = core::SourcedValue<double>::provided(
            1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.expected.unit = *core::UnitToken::find("m");
        core::DiagnosticRecord record = core::DiagnosticRecord::make(
            std::string{"TEST-COMPARE-LIMIT"}, std::nullopt,
            std::nullopt, std::nullopt,
            "test: 超限策略注入（④端口读阈值的域校验面——P-PR-3）",
            "actual=2m expected=1m",
            "test: 确认或修正输入", comparison);
        out.confirmableFindings.push_back(core::ConfirmableFinding::make(record));
        out.summary = "test confirm required";
        return PrepareOutcome::Planned;
    }
};

/// 双编译命令（"test-dual-compile" v1）：声明 requiresDualCompile
/// （§6.6 S5 编排面注入；PRJ-TX-3 载体）。
class DualCompileHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-dual-compile";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& envelope,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        ObjectWrite write;
        write.objectTypeToken = "TestData";
        write.payloadCanonical = envelope.payloadCanonical;
        out.objectWrites.push_back(std::move(write));
        out.requiresDualCompile = true;
        out.summary = "test dual compile";
        return PrepareOutcome::Planned;
    }
};

/// 建支命令（"test-create-branch" v1——P-PR-9 不越界：无点 token 验证
/// 机制面）：载荷两行文本＝源分支规范文本＋新分支 label；经 ctx.query()
/// 预检源分支存在（HandlerContext 查询面覆盖），产出
/// metadataChange.createBranchWithBase（§5.3.2 MetadataChange 声明面）。
/// declareDisplayName＝true 时携带 R2 预留字段（拒绝面注入）。
class CreateBranchHandler final : public ICommandHandler {
public:
    bool declareDisplayName = false;

    [[nodiscard]] std::string commandType() const override
    {
        return "test-create-branch";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& ctx, const CommandEnvelope& envelope,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& diags) override
    {
        // 载荷解析（测试域语义：两行文本）。
        const std::string payload(envelope.payloadCanonical.begin(),
                                  envelope.payloadCanonical.end());
        const auto sep = payload.find('\n');
        if (sep == std::string::npos) {
            diags.push_back(core::DiagnosticRecord::make(
                std::string{"TEST-CREATE-BRANCH-INPUT"}, std::nullopt,
                std::nullopt, std::nullopt,
                "test: 建支载荷格式违约", "payload 缺第二行 label",
                "test: 两行文本（源分支规范文本＋label）"));
            return PrepareOutcome::RejectedInvalidInput;
        }
        const std::string sourceText = payload.substr(0, sep);
        const std::string label = payload.substr(sep + 1);
        const auto source = core::BranchId::tryFromCanonical(sourceText);
        if (!source.has_value() || label.empty()) {
            diags.push_back(core::DiagnosticRecord::make(
                std::string{"TEST-CREATE-BRANCH-INPUT"}, std::nullopt,
                std::nullopt, std::nullopt,
                "test: 建支载荷内容违约", "source/label 解析失败",
                "test: 检查源分支规范文本与 label"));
            return PrepareOutcome::RejectedInvalidInput;
        }
        // 处理器内经 ctx.query() 预检源分支（业务校验的查询面示例——
        // §5.3.2"基于 baseSnapshot 做业务校验"的补充读取形态）。
        bool known = false;
        for (const BranchTip& tip : ctx.query().branchTips()) {
            if (tip.id == *source) {
                known = true;
                break;
            }
        }
        if (!known) {
            diags.push_back(core::DiagnosticRecord::make(
                std::string{"TEST-CREATE-BRANCH-SOURCE"}, std::nullopt,
                std::nullopt, std::nullopt,
                "test: 建支源分支不存在", "branch=" + sourceText,
                "test: 使用权威分支表中的分支"));
            return PrepareOutcome::RejectedHardAssert;
        }
        MetadataChange change;
        change.createBranchWithBase = *source;
        change.label = label;
        if (declareDisplayName) {
            change.newDisplayName = "test-R2-reserved";
        }
        out.metadataChange = std::move(change);
        out.summary = "test create branch";
        return PrepareOutcome::Planned;
    }
};

/// 槽观测命令（"test-gated" v1）：prepare 阻塞直至用例放行——命令执行
/// 槽"第二个等待"（§6.1）的直接观测面。无修订路径（配合 Abort 等用例）。
class GateHandler final : public ICommandHandler {
public:
    std::mutex mutex;
    std::condition_variable gate;
    bool open = false;
    std::atomic<int> prepareEntered{0};

    void release()
    {
        {
            std::lock_guard<std::mutex> guard(mutex);
            open = true;
        }
        gate.notify_all();
    }

    [[nodiscard]] std::string commandType() const override
    {
        return "test-gated";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& /*ctx*/, const CommandEnvelope& /*envelope*/,
                           const RevisionView& /*baseSnapshot*/, CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        ++prepareEntered;
        std::unique_lock<std::mutex> lock(mutex);
        // 有界等待（10 s 上限防用例悬挂——gtest 环境无外部喂放时失败
        // 可见而非卡死）；放行后产出空计划（Planned——提交继续）。
        gate.wait_for(lock, std::chrono::seconds(10), [this] { return open; });
        out.summary = "test gated";
        return PrepareOutcome::Planned;
    }
};

// =====================================================================
// 双编译端口桩（§5.3.6"阶段 A 以测试桩实现验证编排"）
// =====================================================================

/**
 * @brief 双编译端口桩：三路模式（WC 失败/DWC 失败/双成功——PRJ-TX-3）；
 *        记录最近一次请求的观测面（plannedWrites 数/baseRevision/查询
 *        端口可用性——§6.6 编译输入＝计划闭包的编排证据）；可选在指定
 *        目录写一个临时产物文件（模拟 runtime 中间产物落点——D-09/
 *        §6.6 清理断言的注入面）。
 */
class StubCompilePort final : public IModelCompilePort {
public:
    enum class Mode { Ok, FailWorkCell, FailDynamicWorkCell };

    Mode mode = Mode::Ok;
    std::filesystem::path artifactDir;  ///< 非空＝编译时写 artifact.bin（D-09 注入）
    // 最近请求观测（用例串行调用——命令槽保证）。
    std::size_t lastPlannedWrites = 0;
    std::string lastBaseRevision;
    bool queryWasUsable = false;

    explicit StubCompilePort(Mode m = Mode::Ok,
                             std::filesystem::path artifactDirectory = {})
        : mode(m), artifactDir(std::move(artifactDirectory))
    {
    }

    CompileResult compileWorkCellAndDwc(const CompileRequest& request) override
    {
        // 编译输入观测（§6.6"编译输入＝计划闭包"——端口确实拿到查询
        // 端口/计划集/基线三要素；query.head() 可用性＝runtime 只读快照
        // 语义的取数前提）。
        lastPlannedWrites = request.plannedWrites.size();
        lastBaseRevision = request.baseRevision.toCanonical();
        try {
            (void)request.query.head();  // 可用性探测——返回值不消费
            queryWasUsable = true;
        } catch (...) {
            queryWasUsable = false;
        }
        // 中间产物落盘注入（仅当用例给目录——模拟 runtime 写 .staging/tmp）。
        if (!artifactDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(artifactDir, ec);
            std::ofstream out(artifactDir / "artifact.bin", std::ios::binary | std::ios::trunc);
            out << "compile-intermediate";
        }
        CompileResult result;
        switch (mode) {
        case Mode::Ok:
            result.ok = true;
            break;
        case Mode::FailWorkCell:
            result.ok = false;
            result.diagnostics.push_back(core::DiagnosticRecord::make(
                std::string{"TEST-COMPILE-WC-FAILED"}, std::nullopt,
                std::nullopt, std::nullopt,
                "test: WorkCell 半边编译失败注入", "stage=workcell",
                "test: 修正模型后重试"));
            break;
        case Mode::FailDynamicWorkCell:
            result.ok = false;
            result.diagnostics.push_back(core::DiagnosticRecord::make(
                std::string{"TEST-COMPILE-DWC-FAILED"}, std::nullopt,
                std::nullopt, std::nullopt,
                "test: DynamicWorkCell 半边编译失败注入", "stage=dynamic-workcell",
                "test: 修正动力学参数后重试"));
            break;
        }
        return result;
    }
};

// =====================================================================
// 确认交互桩（§5.3.3 ICommandInteraction 实现）
// =====================================================================

/**
 * @brief 确认交互桩：alive/决策/抛出三面可配（§5.3.3 回调三态的注入面
 *        ——S4 决策映射用例组）。
 */
class StubInteraction final : public ICommandInteraction {
public:
    bool alive = true;
    bool rejectAll = false;      ///< true＝requestConfirmations 返回 nullopt
    bool throwInCallback = false; ///< true＝回调内抛出（interaction-lost 面）
    std::vector<core::ConfirmableFinding> lastFindings;  ///< 观测面

    [[nodiscard]] bool isAlive() const override { return alive; }

    std::optional<std::vector<core::ConfirmationCredential>> requestConfirmations(
        const std::vector<core::ConfirmableFinding>& findings) override
    {
        lastFindings = findings;
        if (throwInCallback) {
            throw std::runtime_error("stub: interaction callback failure");
        }
        if (rejectAll) {
            return std::nullopt;  // 整体拒绝（§5.3.3"空 optional＝整体拒绝"）
        }
        // 与输入一一对应的凭据向量（主体＝stub 用户；时刻＝挂钟——测试
        // 域凭据，绑定复核归 PRJ-T11）。
        std::vector<core::ConfirmationCredential> credentials;
        credentials.reserve(findings.size());
        for (std::size_t i = 0; i < findings.size(); ++i) {
            core::ConfirmationCredential credential;
            credential.principal = "stub-user";
            credential.confirmedAtUtc = std::chrono::system_clock::now();
            credentials.push_back(credential);
        }
        return credentials;
    }
};

/**
 * @brief 可门控确认交互桩（PRJ-T11——§5.3.4 确认等待的注入面）：回调在
 *        release() 前一直阻塞，模拟"用户停留在确认对话框"的挂起态；放行
 *        时按配置返回确认/拒绝或抛出（ui 拆除形态——diagnostics.md §5.6
 *        "回调抛出→同 interaction-lost 路径"）。
 *
 * 观测面：entered（回调已进入）、callbackThreadId（回调执行线程——§5.3.3
 * "命令服务在命令执行线程同步调用"的断言面）、lastFindings（确认对话
 * 收到的数据——UX-03 三要素齐备的断言面）。
 *
 * 线程约束：由用例串行驱动（submit 线程阻塞于回调、主线程放行——命令
 * 槽保证同上下文串行）；观测字段在回调进入时写入、放行后由主线程读取。
 */
class GatedInteraction final : public ICommandInteraction {
public:
    std::mutex mutex;
    std::condition_variable gate;
    bool releaseRequested = false;   ///< 放行旗标（release() 置位）
    bool confirmOnRelease = true;    ///< true＝放行时返回确认凭据；false＝nullopt
    bool throwOnRelease = false;     ///< true＝放行时抛出（ui 拆除形态）
    std::atomic<bool> entered{false};
    std::thread::id callbackThreadId{};
    std::vector<core::ConfirmableFinding> lastFindings;

    /// 放行挂起中的回调（主线程调用——唤醒 wait）。
    void release()
    {
        {
            std::lock_guard<std::mutex> guard(mutex);
            releaseRequested = true;
        }
        gate.notify_all();
    }

    [[nodiscard]] bool isAlive() const override { return true; }

    std::optional<std::vector<core::ConfirmationCredential>> requestConfirmations(
        const std::vector<core::ConfirmableFinding>& findings) override
    {
        entered = true;
        callbackThreadId = std::this_thread::get_id();
        lastFindings = findings;
        // 挂起：有界等待（10 s 上限防用例悬挂——GateHandler 同款口径；
        // 超时未放行按配置面继续走，用例断言会暴露超时）。
        std::unique_lock<std::mutex> lock(mutex);
        gate.wait_for(lock, std::chrono::seconds(10),
                      [this] { return releaseRequested; });
        if (throwOnRelease) {
            throw std::runtime_error("stub: ui teardown during confirm wait");
        }
        if (!confirmOnRelease) {
            return std::nullopt;  // 用户取消对话框（整体拒绝）
        }
        std::vector<core::ConfirmationCredential> credentials;
        credentials.reserve(findings.size());
        for (std::size_t i = 0; i < findings.size(); ++i) {
            core::ConfirmationCredential credential;
            credential.principal = "stub-user";
            credential.confirmedAtUtc = std::chrono::system_clock::now();
            credentials.push_back(credential);
        }
        return credentials;
    }
};

/**
 * @brief 扰动型绑定复核探针（PRJ-T11 实现口径⑤的测试侧 fake——D-10
 *        "生产窄接口，测试侧 fake"形态的确认流实例）。
 *
 * 行为：**每次咨询（即每个复核点）都注入扰动**——冻结不经探针（S4 冻结
 * 直调 confirm::freezeConfirmation 生产真值），探针只在复核点被命令服务
 * 咨询，因此首次咨询即复核点。选定成员替换为确定性的"必不等"值（摘要
 * 成员翻字节、身份成员换规范文本），驱动 §6.7 绑定复核失配分支。
 *
 * 背景：失配分支在生产执行序内不可达（§6.7"输入变化不可能——槽内无
 * 并发写"），PRJ-TX-2 第四路经本探针在 submit 级注入（IFileOps 故障
 * 注入同款接缝纪律）。
 */
class PerturbingProbe final : public confirm::IConfirmationProbe {
public:
    /// 注入扰动的成员（四元组逐一可注入——acceptance 1"任一不符"）。
    enum class Member {
        FindingDigest,
        PolicyContent,
        CommandDigest,
        BaseRevision,
    };

    Member member = Member::FindingDigest;
    int calls = 0;   ///< 咨询总次数观测面（每复核点每 finding 一次）

    [[nodiscard]] confirm::ConfirmationActuals actuals(
        const CommandEnvelope& envelope,
        const core::ConfirmableFinding& finding,
        const core::RevisionId& currentBaseRevision) override
    {
        ++calls;
        // 真值基底：生产探针同源采集（扰动只替换选定成员——失配定位
        // 精确到该成员，其余成员保持一致面）。
        confirm::ConfirmationActuals actuals
            = confirm::ProductionConfirmationProbe{}.actuals(
                envelope, finding, currentBaseRevision);
        switch (member) {
            case Member::FindingDigest:
                // 摘要成员：首字符翻转（hex 字符集内必不等——64 位摘要
                // 任一字节差异即失配的语义缩影）。
                actuals.findingDigest[0]
                    = (actuals.findingDigest[0] == '0') ? '1' : '0';
                break;
            case Member::PolicyContent:
                // 策略身份：cid- 规范文本尾字符翻转（同上——文本级"必
                // 不等"注入；codec 校验口径下仍为合法 cid- 形态）。
                actuals.policyContentId.back()
                    = (actuals.policyContentId.back() == '0') ? '1' : '0';
                break;
            case Member::CommandDigest:
                actuals.commandDigest[0]
                    = (actuals.commandDigest[0] == '0') ? '1' : '0';
                break;
            case Member::BaseRevision:
                // 输入版本：换成全零保留值文本（rev- + 64 个 0——rev-
                // tag 保持、值必不等；比对是文本级，解析不发生）。
                actuals.baseRevisionId = std::string("rev-") + std::string(64, '0');
                break;
            }
        return actuals;
    }
};

// =====================================================================
// 共享夹具（createNew 上下文＋总线订阅＋缺省处理器注册）
// =====================================================================

/**
 * @brief 命令服务用例组共享夹具：临时项目（createNew——可写上下文）＋
 *        捕获 sink＋参考总线订阅＋内部通道句柄。析构释放上下文并清理
 *        临时目录。
 *
 * 背景说明：处理器注册经 impl->commandService().registry()（同单元
 * 私有头消费——R-2 先例：ProjectStoreTest/TxEngineTest 同款）；参考
 * 总线＝core 测试内参考实现（Events.hpp"测试内参考总线"——生产总线
 * 归 execution/ui，T-1 边界下不进产品链接面）。
 */
class ServiceFixture {
public:
    std::filesystem::path dir;
    StubSink sink;
    core::ReferenceEventBus bus;
    RecordingEventSink events;
    std::unique_ptr<core::IEventSubscription> subscription;
    OpenStoreResult opened;

    ServiceFixture()
    {
        // 临时目录（进程内唯一——递增计数＋时钟后缀，规避并行用例碰撞）。
        static std::atomic<unsigned long long> seq{0};
        dir = std::filesystem::temp_directory_path()
            / ("ird-prj-t10-" + std::to_string(seq.fetch_add(1)) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        opened = ProjectStoreFactory::createNew(dir, "command test project",
                                                &bus, &sink);
        subscription = bus.subscribe(events);
    }

    ~ServiceFixture()
    {
        subscription.reset();
        opened.store.reset();  // 析构释放锁句柄（Active 析构＝静默终局）
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    ServiceFixture(const ServiceFixture&) = delete;
    ServiceFixture& operator=(const ServiceFixture&) = delete;

    /// 存储上下文实现句柄（内部通道——命令服务/注册表访问）。
    [[nodiscard]] ProjectStoreImpl& impl() const
    {
        return *dynamic_cast<ProjectStoreImpl*>(opened.store.get());
    }

    /// 命令端口（公共抽象面——submit 消费形态）。
    [[nodiscard]] ProjectCommandService& commands() const
    {
        return opened.store->commands();
    }

    /// 处理器注册表（装配期注册入口）。
    [[nodiscard]] HandlerRegistry& registry() const
    {
        return impl().commandService().registry();
    }

    /// 注册全部缺省处理器桩（用例按需增补特殊桩）。
    void registerDefaults()
    {
        registry().registerHandler(std::make_unique<AppendObjectHandler>());
        registry().registerHandler(std::make_unique<HardAssertFailHandler>());
        registry().registerHandler(std::make_unique<InvalidInputHandler>());
        registry().registerHandler(std::make_unique<FindingsHandler>());
        registry().registerHandler(std::make_unique<DualCompileHandler>());
        registry().registerHandler(std::make_unique<CreateBranchHandler>());
    }

    /// 主分支身份（createNew 的初始分支——信封 branch 字段来源）。
    [[nodiscard]] core::BranchId primaryBranch() const
    {
        return opened.store->query().currentMetadata().record.primaryBranchId;
    }

    /// 构造追加对象信封（负载＝文本字节；版本 1）。
    [[nodiscard]] CommandEnvelope appendEnvelope(const std::string& payload,
                                                 core::BranchId branch) const
    {
        CommandEnvelope envelope;
        envelope.branch = branch;
        envelope.commandType = "test-append-object";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical.assign(payload.begin(), payload.end());
        return envelope;
    }

    /// 磁盘修订目录计数（地面事实——"无修订落盘"断言的观测点）。
    [[nodiscard]] std::size_t revisionDirCount() const
    {
        return subdirectoryCount(dir / "revisions");
    }

    /// 磁盘对象库 oid 目录计数（"对象库无新对象"断言的观测点——PRJ-TX-3）。
    [[nodiscard]] std::size_t objectDirCount() const
    {
        return subdirectoryCount(dir / "objects");
    }

    /// .staging/tmp 内容计数（D-09 清理断言的观测点）。
    [[nodiscard]] std::size_t stagingTmpCount() const
    {
        return subdirectoryCount(dir / ".staging" / "tmp")
            + regularFileCount(dir / ".staging" / "tmp");
    }

private:
    [[nodiscard]] static std::size_t subdirectoryCount(const std::filesystem::path& root)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec) {
            return 0;
        }
        std::size_t n = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_directory(ec) && !ec) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] static std::size_t regularFileCount(const std::filesystem::path& root)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec) {
            return 0;
        }
        std::size_t n = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec) && !ec) {
                ++n;
            }
        }
        return n;
    }
};

}  // namespace sdurws::ird::project::stub

#endif  // SDURWS_IRD_PROJECT_TEST_STUBHANDLERS_HPP
