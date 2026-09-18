/**
 * @file   TaskTypes.cpp
 * @brief  任务身份与记录类型的实现——TaskId 解析/格式化/生成、进度工厂、
 *         能力声明注册表、终态映射（§4.1 纪律＋§5.5/§5.6 规则）。
 *
 * 设计依据：
 *   - units/execution.md §4.1（TaskId："tsk-<32hex>，Id128 强类型，同
 *     core.md §4.1 纪律：tag 区分、全零保留、往返严格"）、§4.2（字段表
 *     合法性列）、§5.5（能力声明最小能力回退）、§5.6（TaskState→
 *     TaskOutcome 终态映射冻结表）
 *   - 任务契约 tasks/foundation/EX-T02.json acceptance 2（三级身份并发
 *     分配唯一、无重复——generate 的 thread_local 引擎即并发安全载体）
 *
 * 实现说明：TaskId 的解析/格式化与 core Identity.cpp 的六类型同纪律，
 * 但**单元内自持实现**——core 的 detail 函数显式登记为非跨单元承诺面
 * （core Identity.hpp detail 注释原文），跨单元消费即把 execution 绑到
 * core 私有细节（R-2 纪律的精神违反）；本文件逐条对照纪律实现。
 */

#include <sdurws/ird/execution/TaskTypes.hpp>

#include <sdurws/ird/execution/Errors.hpp>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::execution {
namespace {

// ---------------------------------------------------------------------
// TaskId 的严格解析/格式化（tag "tsk-"——纪律与 core §4.1 U-1 逐条一致）
// ---------------------------------------------------------------------

/// 规范文本总长＝tag（4 字符）＋32 hex。
constexpr std::size_t kTaskIdTextLength = 36;

/**
 * try 轨解析："tsk-" 逐字符匹配＋恰 32 个 [0-9a-f]（大写拒绝——规范文本
 * 唯一小写）。字节序＝文本序（每两 hex 字符合成一字节，高半字节在前），
 * 与 formatTaskId 对称——保证 parse(format(x))==x 往返。
 */
bool tryParseTaskId(std::string_view text, std::array<std::uint8_t, 16>* out) noexcept
{
    constexpr std::string_view kTag = "tsk-";
    if (text.size() != kTaskIdTextLength) {        // 总长固定 36：tag＋32 hex
        return false;
    }
    for (std::size_t i = 0; i < kTag.size(); ++i) {
        if (text[i] != kTag[i]) {                  // tag 逐字符（类型误用在此即失败）
            return false;
        }
    }
    for (std::size_t i = 0; i < 32; ++i) {
        const char c = text[kTag.size() + i];
        unsigned nibble;
        if (c >= '0' && c <= '9')      { nibble = static_cast<unsigned>(c - '0'); }
        else if (c >= 'a' && c <= 'f') { nibble = static_cast<unsigned>(c - 'a') + 10u; }
        else { return false; }                     // 大写/g 以后/符号/空白一律拒绝
        if ((i & 1u) == 0u) { (*out)[i / 2] = static_cast<std::uint8_t>(nibble << 4); }
        else { (*out)[i / 2] = static_cast<std::uint8_t>((*out)[i / 2] | nibble); }
    }
    return true;
}

/// 格式化 "tsk-<32 小写 hex>"（字节序＝文本序，与解析对称）。
std::string formatTaskId(const std::array<std::uint8_t, 16>& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(kTaskIdTextLength);
    s.append("tsk-");
    for (const std::uint8_t b : bytes) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0Fu]);
    }
    return s;
}

}  // namespace

// ---------------------------------------------------------------------
// TaskId 成员（§4.1：generate 非零、解析抛/try 双轨、往返严格）
// ---------------------------------------------------------------------

TaskId TaskId::generate()
{
    // thread_local 引擎（core generateId128Bytes 同款）：进程内每线程独立
    // mt19937_64、random_device 播种一次——多线程并发生成无锁且不共享
    // 状态（EX-SUB-1 并发唯一性用例的实现基础）。非密码学承诺；唯一性
    // 来自 128 位随机空间的碰撞概率可忽略。
    thread_local std::mt19937_64 engine{std::random_device{}()};
    TaskId id;
    do {
        const std::uint64_t w1 = engine();
        const std::uint64_t w2 = engine();
        for (int i = 0; i < 8; ++i) {              // 大端装配：高字节在低下标（与 hex 序一致）
            id.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(w1 >> (56 - 8 * i));
            id.bytes[static_cast<std::size_t>(i) + 8] = static_cast<std::uint8_t>(w2 >> (56 - 8 * i));
        }
    } while (std::all_of(id.bytes.begin(), id.bytes.end(),
                         [](std::uint8_t b) { return b == 0; }));
    // 保留值纪律：全零重取（零＝"空"语义，generate 不得产出）。
    return id;
}

TaskId TaskId::fromCanonical(std::string_view text)
{
    TaskId id;
    if (!tryParseTaskId(text, &id.bytes)) {
        // 抛出轨迹：调用方传非法文本属契约违约，fail-fast（detail 前缀
        // 稳定 "execution/taskid-parse:"；原文截断 64 字符防日志洪泛——
        // core 同款纪律）。
        const std::size_t n = std::min<std::size_t>(text.size(), 64);
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             std::string{"execution/taskid-parse: 期望 tsk-<32 小写 hex>，实际: \""}
                                 + std::string(text.substr(0, n))
                                 + "\"（长度 " + std::to_string(text.size()) + "）");
    }
    return id;
}

std::optional<TaskId> TaskId::tryFromCanonical(std::string_view text) noexcept
{
    TaskId id;
    if (!tryParseTaskId(text, &id.bytes)) { return std::nullopt; }
    return id;
}

std::string TaskId::toCanonical() const { return formatTaskId(bytes); }

bool TaskId::isValid() const noexcept
{
    return std::any_of(bytes.begin(), bytes.end(),
                       [](std::uint8_t b) { return b != 0; });
}

// ---------------------------------------------------------------------
// CheckpointId / ProgressReport / TaskRecord / TaskSnapshot（§4.1/§4.2）
// ---------------------------------------------------------------------

std::string CheckpointId::toCanonical() const
{
    // "chk-<run>-<seq>"（§4.1 概念表规范文本列）：run 用其 core 规范文本
    // （run-<32hex>），seq 十进制。诊断/日志承载用，不设逆解析（本单元
    // 内无消费者——NFR-MNT-04 不预建）。
    return "chk-" + run.toCanonical() + "-" + std::to_string(sequence);
}

ProgressReport ProgressReport::make(int percent, std::string phaseToken,
                                    std::uint64_t batchesDone, std::uint64_t batchesTotal)
{
    // §4.2 progress 行合法实例列：percent≤100（下限 0 同理）、phaseToken
    // 非空；batchesDone≤batchesTotal（§8.1 completed≤total 同型边界）。
    // 违约＝worker 上报通道组装错误（调用方契约），fail-fast 不静默钳位。
    if (percent < 0 || percent > 100) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/progress: percent 越界 [0,100]，实际 "
                             + std::to_string(percent));
    }
    if (phaseToken.empty()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/progress: phaseToken 为空（UX-10 阶段判读面不可缺）");
    }
    if (batchesDone > batchesTotal) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/progress: batchesDone > batchesTotal（"
                             + std::to_string(batchesDone) + " > "
                             + std::to_string(batchesTotal) + "）");
    }
    ProgressReport r;
    r.percent = percent;
    r.phaseToken = std::move(phaseToken);
    r.batchesDone = batchesDone;
    r.batchesTotal = batchesTotal;
    return r;
}

bool TaskRecord::operator==(const TaskRecord& o) const
{
    return taskId == o.taskId && submission == o.submission && capability == o.capability
        && state == o.state && run == o.run && attempt == o.attempt
        && progress == o.progress && termination == o.termination
        && archivePhase == o.archivePhase && diagnostics == o.diagnostics;
}

bool TaskSnapshot::operator==(const TaskSnapshot& o) const noexcept
{
    return taskId == o.taskId && state == o.state && run == o.run
        && attempt == o.attempt && progress == o.progress
        && termination == o.termination && archivePhase == o.archivePhase;
}

// ---------------------------------------------------------------------
// EvaluatorRuntimeCapabilities（§5.5——P-EX-7 的 execution 侧落点）
// ---------------------------------------------------------------------

bool EvaluatorRuntimeCapabilities::declare(const evidence::EvaluationKey& key,
                                           TaskCapability capability)
{
    // 互斥进入：声明（L5 装配线程）与查询（调度线程）可能并发；锁内
    // 完成"查重＋插入"避免检查-后-行动竞争。
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_entries) {
        if (entry.first == key) {
            return false;   // 同键重复声明＝装配清单漂移，拒绝不覆盖
        }
    }
    m_entries.emplace_back(key, capability);
    return true;
}

std::optional<TaskCapability>
EvaluatorRuntimeCapabilities::tryLookup(const evidence::EvaluationKey& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_entries) {
        if (entry.first == key) { return entry.second; }
    }
    return std::nullopt;
}

TaskCapability EvaluatorRuntimeCapabilities::lookupOrMinimal(const evidence::EvaluationKey& key) const
{
    // §5.5："evidence EvaluatorDescriptor 无能力字段时按最小能力处理"——
    // 未声明评估器回退 TaskCapability 缺省值（不支持暂停/无检查点边界；
    // 强杀代价取中性 Moderate——仅呈现不影响协议）。
    if (auto found = tryLookup(key)) { return *found; }
    return TaskCapability{};
}

// ---------------------------------------------------------------------
// 终态判定与映射（§5.6——core.md §10.3 交接项承接）
// ---------------------------------------------------------------------

bool isTerminalTaskState(core::TaskState state) noexcept
{
    // 四终态（§5.1 表"可重试性"列的终态行）；其余五态（Queued/Preparing/
    // Running/Paused/Canceling）非终态。switch 全枚举＋防御分支。
    switch (state) {
    case core::TaskState::Canceled:
    case core::TaskState::Completed:
    case core::TaskState::Failed:
    case core::TaskState::Interrupted:
        return true;
    case core::TaskState::Queued:
    case core::TaskState::Preparing:
    case core::TaskState::Running:
    case core::TaskState::Paused:
    case core::TaskState::Canceling:
        return false;
    }
    return false;   // 防御分支（枚举封闭，正常路径不可达）
}

std::optional<core::TaskOutcome> terminalOutcome(core::TaskState state) noexcept
{
    // §5.6 冻结映射表：四终态同名直映；非终态无 outcome（nullopt——
    // 不伪造结果，TASK-02/NFR-REL-03 的执行侧表达）。
    switch (state) {
    case core::TaskState::Completed:   return core::TaskOutcome::Completed;
    case core::TaskState::Canceled:    return core::TaskOutcome::Canceled;
    case core::TaskState::Failed:      return core::TaskOutcome::Failed;
    case core::TaskState::Interrupted: return core::TaskOutcome::Interrupted;
    case core::TaskState::Queued:
    case core::TaskState::Preparing:
    case core::TaskState::Running:
    case core::TaskState::Paused:
    case core::TaskState::Canceling:
        return std::nullopt;
    }
    return std::nullopt;   // 防御分支（枚举封闭，正常路径不可达）
}

}  // namespace sdurws::ird::execution
