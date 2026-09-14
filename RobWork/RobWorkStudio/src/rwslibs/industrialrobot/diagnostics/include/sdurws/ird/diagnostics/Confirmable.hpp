/**
 * @file   Confirmable.hpp
 * @brief  可确认诊断（ConfirmableFinding）服务端设施——FindingId/绑定四元组/
 *         FindingRecord/服务状态机与失效条件（SA-15 确认放行流的 diagnostics 半区）。
 *
 * 设计依据：
 *   - units/diagnostics.md §5 全节（§5.1 分工冻结、§5.2 FindingRecord 字段表、
 *     §5.3 绑定四元组与失效条件、§5.4 服务端状态机、§5.5/§5.6 时序与 UI 关闭
 *     流程）、§9.3（IConfirmableFindingService 接口契约——五方法签名逐字）
 *   - 需求 MDL-06④（M-10 可确认诊断：确认不豁免编译）、ERR-01、CON-06（策略
 *     内容身份进确认绑定）；架构决策 SA-15（确认只在命令边界；worker 禁令）
 *   - 任务契约 tasks/foundation/DIAG-T05.json（≙WP-09-T03/T04 设施面）；
 *     验证矩阵 DT-CFM-1~10
 *
 * 背景说明（SA-15 分工——本文件只做"设施"，不做"编排"）：
 *   ConfirmableFinding 是 diagnostics 的比较型诊断扩展，携带确认状态与确认凭据；
 *   命令服务（project）在断言阶段收集待确认项集合，经命令上下文回调（ui 实现）
 *   呈现确认对话，凭确认凭据继续或终止；确认决策记录归命令摘要（project 持久化
 *   CommandRecord.confirmations[]），ui 不持有判定权。本单元职责＝FindingId 分配、
 *   绑定四元组计算、状态机（§5.4）、失效条件复核、确认记录形状——**放行编排与
 *   提交归 project**（§5.3/§6.7 project.md）。
 *
 * 陷阱处置锚点（契约 knownPitfalls 逐项）：
 *   - P-DIAG-4（acceptance 3）：FindingId 规范文本 fnd-<32hex> 的句法/解析由
 *     **本单元自持实现**（下方 FindingId）——句法与 core Id128 严格一致（tag+
 *     32 小写 hex、大写拒绝、无前后缀/空白）；不 include core 的 detail 层
 *     （非公共契约——R-2 纪律），**不私改 core**（core 收编 fnd- tag 待其
 *     下次修订，登记待裁决）。
 *   - P-DIAG-6（acceptance 4）：服务端扩展态 Invalidated/Expired 不进 core
 *     枚举（core 三态投影经 FindingRecord::coreProjection 承载）；expiresAtUtc
 *     字段保留但**默认 nullopt**（无限期——project §5.3.4"无超时自动确认"；
 *     启用超时＝需求变更，本设施不私裁）。
 *   - CR-02/P-DIAG-6（acceptance 4）：findingDigest 摘要**只经**
 *     core::ContentDigester（SHA-256 单一算法路径——CR-02），canonical 编码
 *     见 src/Confirmable.cpp 头部登记；本单元不私设第二哈希实现。
 *   - P-DIAG-1：内嵌 core::ConfirmableFinding/ConfirmationCredential 以 core.md
 *     v0.1 为基线（Draft 未冻结）；core 冻结 diff 后增量同步，不私改 core。
 *
 * 线程约束：服务实现（ConfirmableService）并发安全——状态转移内部互斥（短临界
 * 区，无 I/O，§9.3 契约表"线程"行）；查询（tryFind/pendingFor）并发安全。本头
 * 其余实体为纯值类型（并发只读安全）。
 *
 * 生命周期：服务为进程级（会话级记录——命令终结后经目录清理周期回收，§5.4；
 * 阶段 A 不做自动清理——§11 DIAG-T09 行）；FindingRecord 一律值返回（服务端
 * 持有原件，调用方改拷贝不影响状态机）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_CONFIRMABLE_HPP
#define SDURWS_IRD_DIAGNOSTICS_CONFIRMABLE_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // core::ConfirmableFinding/ConfirmationCredential（P-DIAG-1 基线）
#include <sdurws/ird/core/Digest.hpp>       // core::ContentIdentity/Digest256/ContentDigester（CR-02 唯一摘要路径）
#include <sdurws/ird/core/Identity.hpp>     // core::ProjectId/BranchId/RevisionId/ObjectId
#include <sdurws/ird/diagnostics/Catalog.hpp>  // DiagEntryId/IClock（时间注入——DT-CFM-3）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // IDiagnosticRegistry（create 前置 confirmable 校验）
#include <sdurws/ird/diagnostics/Errors.hpp>     // DiagnosticsError（本单元唯一异常类型）

namespace sdurws::ird::diagnostics {

// =====================================================================
// FindingId（§5.2 字段表首行——P-DIAG-4 处置：diagnostics 层类型，本单元
// 自持解析，句法与 core Id128 严格一致）
// =====================================================================

/**
 * @brief 一次待确认事项的稳定身份（128 位随机；规范文本 fnd-<32hex>）。
 *
 * P-DIAG-4 处置口径（acceptance 3）：core Id128 体系现冻结 7 个 tag
 * （obj/prj/brn/rev/run/evt＋att 十进制），无 fnd-；core 收编建议已登记
 * （待裁决，影响面＝core 新增独立类型）。收编前本单元自持解析——**句法规则
 * 与 core Id128 严格一致**（严格解析 "fnd-"+32 个小写十六进制：tag 逐字符
 * 匹配、长度恰 32、字符集仅 [0-9a-f]、大写拒绝、无前后缀/空白），保证未来
 * core 收编时规范文本逐字节兼容（交换面不破坏）。
 *
 * 语义约束（§5.2 findingId 行）：日志/目录关联的会话内稳定身份；**不持久化
 * 入修订**——修订内的确认留痕用 findingDigest（内容摘要，跨会话可复算）。
 *
 * 线程安全：generate() 使用 thread_local 引擎（并发安全）；其余纯值操作。
 */
struct FindingId {
    /// 128 位原始字节；全零＝空（保留值纪律——与 core Id128 同源）。字节序＝规范文本序。
    std::array<std::uint8_t, 16> bytes{};

    /// 生成非零随机新值（thread_local mt19937_64，random_device 播种——与
    /// core Id128 generate 同源纪律；零则重取，保留值不可出现）。线程安全。
    static FindingId generate();

    /// 严格解析 "fnd-<32 小写 hex>"；tag 不符/长度/字符集违约抛
    /// DiagnosticsError(Usage)（P-DIAG-4：句法与 core Id128 严格一致——
    /// core 侧为 CoreError("core/identity/parse:")，本单元错误轨归
    /// DiagnosticsError，§9.0）。
    static FindingId fromCanonical(std::string_view text);

    /// try 轨：解析失败返回 nullopt 不抛（日志回读等容错收集场景）。
    static std::optional<FindingId> tryFromCanonical(std::string_view text) noexcept;

    /// 规范文本 "fnd-<32 小写 hex>"（与 parse 构成 parse(format(x))==x 往返）。
    std::string toCanonical() const;

    /// 非全零（保留值恒 false）。
    bool isValid() const noexcept { return bytes != std::array<std::uint8_t, 16>{}; }

    /// 字节精确相等（无容差——身份等值纪律同 core Id128）。
    bool operator==(const FindingId& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const FindingId& o) const noexcept { return !(*this == o); }
    /// 字节字典序（容器键用）。
    bool operator<(const FindingId& o) const noexcept { return bytes < o.bytes; }
};

// =====================================================================
// 服务端状态与原因词表（§5.4 状态机＋§5.3 失效条件——token 冻结）
// =====================================================================

/**
 * @brief 服务端生命周期状态（§5.4——实现承载，不进 core 枚举，P-DIAG-6）。
 *
 * core 数据契约三态（Pending/Confirmed/Rejected）不变；服务端四值＝core
 * Pending/Confirmed 两个直承态＋两个设施扩展态（Invalidated/Expired——
 * "五态"计数含 core 的 Rejected 投影态，见 FindingRecord::coreProjection）。
 *
 * 转移约束（§5.4）：Confirmed/Invalidated/Expired 为终态（不可逆转；重新
 * 确认＝新 finding）；仅 Pending 可携新转移。每次转移写开发级日志
 * （findingId/转移/原因 token——可追溯）。
 */
enum class FindingState {
    Pending,     ///< 待确认（初始态）
    Confirmed,   ///< 已确认（终态——进命令摘要；编译前复核绑定归 project 编排）
    Invalidated, ///< 已失效（终态——rejectionReason 记失效原因 token）
    Expired,     ///< 已过期（终态——仅 expiresAtUtc 显式设置时可达，P-DIAG-6）
};

/// 失效/拒绝原因 token 词表（§5.3 失效条件表＋§5.4 状态图＋§9.3 注释的
/// 明文 token 集合；逐值注释＝出处。不在表内的失效语义不得私造新 token
/// ——词表演进走单元卡增量修订）。
namespace finding_reason {
/// 用户拒绝（§5.3"用户拒绝"行：回调返回 rejected——拒绝确认不产生修订，MDL-06④）。
inline constexpr std::string_view kUserRejected = "user-rejected";
/// 输入修订变化（§5.3"输入修订变化"行：baseRevisionId ≠ 当前分支 tip——旧确认不跨修订有效）。
inline constexpr std::string_view kRevisionChanged = "revision-changed";
/// 策略版本变化（§5.3"策略版本变化"行：policyContentId ≠ 当前已解析策略内容身份，CON-06）。
inline constexpr std::string_view kPolicyChanged = "policy-changed";
/// 权限变化（§5.3"权限变化"行：存储上下文失权/只读——无写权限则无放行）。
inline constexpr std::string_view kWriteLost = "write-lost";
/// 回调失效（§5.3"回调失效"行：interaction.isAlive()==false 或回调抛出，project §5.3.3）。
inline constexpr std::string_view kInteractionLost = "interaction-lost";
/// 会话关闭/命令中止（§5.3"会话关闭/命令中止"行：确认等待可被会话关闭取消——无永久等待）。
inline constexpr std::string_view kCanceled = "canceled";
/// 绑定复核不符（§9.3 submitConfirmation 后置：四元组任一与当前值不符——不静默沿用旧确认）。
inline constexpr std::string_view kBindingMismatch = "binding-mismatch";
}  // namespace finding_reason

// =====================================================================
// FindingBinding（§5.3 绑定四元组——project §6.7 确认留痕之形状）
// =====================================================================

/**
 * @brief 确认绑定四元组 {findingDigest, policyContentId, commandDigest,
 *        baseRevisionId}（§5.3——承接 project.md §6.7，本文为计算与复核设施）。
 *
 * 生命周期：创建时计算并随 FindingRecord 冻结；确认提交时（ui 回调返回后、
 * 放行前）与编译前各复核一次——**四者任一与当前实际值不符 ⇒ 确认凭据失效**，
 * 命令按未确认处置（confirmations-unresolved/重新确认）。
 *
 * 成员语义（§5.2 binding 行）：
 *   - findingDigest：SHA-256 over finding.record canonical 编码（本单元计算；
 *     CR-02——只经 core::ContentDigester，编码登记见 src/Confirmable.cpp）。
 *   - policyContentId：产生该 finding 的已解析策略内容身份（CON-06）。optional
 *     承载：非策略来源类 finding 可空（此时复核跳过该成员）；策略来源类
 *     finding 的必填义务归调用方（域处理器经④端口已解析策略后创建——服务端
 *     无"策略来源类"判据字段，不做启发式判定）。
 *   - commandDigest：本次命令载荷摘要（SHA-256 over payloadCanonical）。
 *   - baseRevisionId：确认所针对的输入版本。
 *
 * 确认记录进入命令摘要：Committed 时 {binding 四元组, credential} 写入
 * CommandRecord.confirmations[]（project 持久化；本结构即形状权威——DT-CFM-10）。
 *
 * 线程安全：纯值。
 */
struct FindingBinding {
    core::Digest256 findingDigest{};   ///< finding 内容摘要（SHA-256，canonical 编码见实现登记）
    std::optional<core::ContentIdentity> policyContentId; ///< 已解析策略内容身份（CON-06；可空＝非策略来源）
    core::ContentIdentity commandDigest{};                ///< 命令载荷摘要
    core::RevisionId baseRevisionId{};                    ///< 确认所针对的输入修订

    /// 四元组精确等值（DT-CFM-10 留痕回读比对的面）。
    bool operator==(const FindingBinding& o) const noexcept
    {
        return findingDigest == o.findingDigest && policyContentId == o.policyContentId
            && commandDigest == o.commandDigest && baseRevisionId == o.baseRevisionId;
    }
    bool operator!=(const FindingBinding& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// FindingRecord（§5.2 字段表——服务端记录，构造后仅状态字段按状态机转移）
// =====================================================================

/**
 * @brief 可确认诊断的服务端记录（§5.2 字段表 15 字段全量承载）。
 *
 * 值语义：一律由服务 create 产出并值返回（tryFind/pendingFor 亦值返回拷贝）；
 * 调用方持有的是快照——对快照的修改与服务端状态机无关（不可变历史精神，
 * PA-2：终态记录不因读路径改写）。
 *
 * 状态字段演进（state/rejectionReason/confirmation）只能经服务方法发生
 * （§5.2 表头"构造后仅状态字段按状态机转移"）；expiresAtUtc 的显式设置走
 * ConfirmableService::armExpiration（实现类原语——不在虚接口，P-DIAG-6：
 * 装配默认不调用，启用超时＝需求变更）。
 *
 * 线程安全：纯值（服务端原件由服务互斥保护）。
 */
struct FindingRecord {
    FindingId findingId;                       ///< 一次待确认事项的稳定身份（不持久化入修订）
    core::ConfirmableFinding finding;          ///< 数据契约＝core 基类型（必为比较型；初始 Pending）
    std::string sourceCommandType;             ///< 来源命令类型 token（project 处理器注册 token）
    core::ContentIdentity commandPayloadDigest{}; ///< 命令载荷摘要（经 core ContentDigester）
    core::ProjectId project{};                 ///< 绑定项目（绑定四元组的定位域）
    core::BranchId branch{};                   ///< 绑定分支
    core::RevisionId baseRevisionId{};         ///< 绑定的输入版本（project §6.7 四元组之 baseRevisionId）
    std::optional<core::ContentIdentity> snapshotId;   ///< 输入快照身份（命令路径通常无；存在时参与绑定）
    std::optional<core::ContentIdentity> inputSliceId; ///< 输入切片身份（同上）
    std::optional<core::ContentIdentity> policyContentId; ///< 产生该 finding 的已解析策略内容身份（策略来源类必填——调用方义务）
    std::vector<core::ObjectId> subjectScope;  ///< 作用对象集（确认只覆盖登记的作用对象——不自动放行其他对象）
    std::vector<DiagEntryId> evidenceRefs;     ///< 诊断证据（目录内关联条目）
    std::string confirmTextKey;                ///< 用户确认文案键（值归文案资源/ui；键随码表登记）
    std::vector<std::string> optionKeys;       ///< 确认选项键（同上）
    std::chrono::system_clock::time_point createdAtUtc{};  ///< 创建时间（UTC；注入 IClock——测试可替换）
    std::optional<std::chrono::system_clock::time_point> expiresAtUtc; ///< 过期时间——默认 nullopt（无限期，P-DIAG-6）
    FindingState state = FindingState::Pending;            ///< 服务端生命周期状态（初始 Pending）
    std::uint64_t callbackToken = 0;           ///< 回调令牌：命令服务调回调时携带、返回时回验（一次性）
    std::optional<std::string> rejectionReason;            ///< 拒绝/失效原因（finding_reason 词表 token）
    std::optional<core::ConfirmationCredential> confirmation; ///< 确认记录（principal＋confirmedAtUtc；core 契约）
    FindingBinding binding;                    ///< 绑定四元组（§5.3；创建时冻结）

    /// 字段级精确等值（DT-CFM-10 留痕回读与状态机观测用）。
    bool operator==(const FindingRecord& o) const;
    bool operator!=(const FindingRecord& o) const { return !(*this == o); }

    /**
     * @brief core 三态投影（§5.4——Confirmed/Rejected 落回 core::ConfirmableFinding
     *        三态；Invalidated/Expired 仅存在于服务端记录，P-DIAG-6）。
     *
     * 投影规则：Pending→core Pending；Confirmed→core Confirmed（凭据随附）；
     * Invalidated(user-rejected)→core Rejected（拒绝即否决——阻止应用，MDL-06④）；
     * Invalidated(其他原因)与 Expired→core Pending（设施失效态在 core 契约中
     * 无承载——core 视角保持"未确认"事实，不伪造终局）。
     *
     * 实现口径：服务端在状态转移时已同步推进内嵌 core::ConfirmableFinding
     * （确认走 core confirm()、用户拒绝走 core reject()——core C-2 不变量
     * 自持），本函数仅按 rejectionReason 甄别非拒绝失效的投影。
     */
    core::ConfirmableFinding coreProjection() const;
};

// =====================================================================
// ConfirmOutcome（§9.3 注释原文四值——submitConfirmation 的结果枚举）
// =====================================================================

/// 确认提交结果（§9.3 注释原文："ConfirmOutcome = Confirmed | BindingMismatch
/// | InvalidState | UnknownFinding"；结果枚举优先于异常轨——状态类失败不抛）。
enum class ConfirmOutcome {
    Confirmed,       ///< 复核一致：Confirmed＋confirmation 写入
    BindingMismatch, ///< 复核不符：Invalidated(binding-mismatch)——不静默沿用旧确认
    InvalidState,    ///< 状态/令牌前置不满足（非 Pending／token 不匹配／已过期）
    UnknownFinding,  ///< findingId 未登记
};

// =====================================================================
// IConfirmationEnvironment（实现口径设施——绑定复核"当前值"的提供者）
// =====================================================================

/**
 * @brief 绑定复核环境探针（实现口径登记，DTB §5.4——§9.3 未定义"当前策略/
 *        命令/修订"的服务端来源）。
 *
 * 为什么需要本接口：§9.3 submitConfirmation 后置要求"复核 binding 四元组
 * （当前策略/命令/修订）"，但其签名（契约原文）只携带凭据——服务端必须有
 * "当前实际值"的来源才能复核。本探针由 L5 装配注入（实现者＝project 桥接：
 * tip 归 project 修订记录、已解析策略内容身份归④端口、命令槽内当前命令载荷
 * 摘要归命令服务），与本服务实例同生命周期。同 StableCodeRegistry::seal()
 * 先例：这是实现类构造约定，不在 §9.3 五方法契约上（接口签名逐字承载，
 * 不扩契约面）。
 *
 * 返回值语义：
 *   - currentTipRevision：当前分支 tip（命令槽串行保证检测无竞争——§5.3
 *     "输入修订变化"行；本条件防御跨命令重用旧确认）。
 *   - currentPolicyContentId：当前已解析策略内容身份（④端口复核，CON-06）；
 *     nullopt＝当前会话无已解析策略（此时与"有值绑定"必不符——策略已不可达）。
 *   - currentCommandDigest：命令槽内当前命令的载荷摘要；nullopt＝无活动命令
 *     （跨命令重用场景：原命令已终结）——与绑定必不符。
 *
 * 线程约束：实现方须可在服务内部互斥临界区内调用（实现必须短小、无反向进入
 * 本服务、无阻塞 I/O——§9.3"短临界区，无 I/O"约束的传导）。
 */
struct IConfirmationEnvironment {
    virtual ~IConfirmationEnvironment() = default;

    /// @brief 当前分支 tip 修订（§5.3 失效条件"输入修订变化"的复核面）。
    virtual core::RevisionId currentTipRevision(core::ProjectId project,
                                                core::BranchId branch) const = 0;

    /// @brief 当前已解析策略内容身份（④端口——CON-06；nullopt＝当前无已解析策略）。
    virtual std::optional<core::ContentIdentity> currentPolicyContentId(
        core::ProjectId project, core::BranchId branch) const = 0;

    /// @brief 命令槽内当前命令载荷摘要（nullopt＝命令槽无活动命令——跨命令重用面）。
    virtual std::optional<core::ContentIdentity> currentCommandDigest(
        core::ProjectId project, core::BranchId branch) const = 0;
};

// =====================================================================
// 摘要与复核设施（§5.3——"本文提供 binding 数据，复核时机归 project 编排"）
// =====================================================================

/**
 * @brief 计算 finding 内容摘要（findingDigest——绑定四元组首成员）。
 *
 * 摘要输入＝core::DiagnosticRecord 的 canonical 编码（编码表登记于
 * src/Confirmable.cpp 头注释：magic＋逐字段 u32le 长度前缀）；SHA-256 经
 * core::ContentDigester（CR-02——唯一摘要算法路径）。公开为自由函数的理由：
 * 编译前复核由 project 调 tryFind＋自行比对实现（§9.3 契约表），project
 * 复算当前摘要需要与服务端相同的唯一定义点（双方面各算各的会漂移）。
 *
 * @param record [in] 待摘要的记录（通常取 finding.record）
 * @return 32 字节摘要（同内容同摘要——确定性 NFR-COR-02）
 *
 * 纯函数；线程安全；不抛（除 bad_alloc 传播）。
 */
core::Digest256 findingRecordDigest(const core::DiagnosticRecord& record);

// =====================================================================
// IConfirmableFindingService（§9.3 接口原文——五方法签名逐字承载）
// =====================================================================

/**
 * @brief 可确认诊断服务（§9.3 原文契约——SA-15 设施）。
 *
 * 合法调用面（§9.3 契约表）：project 处理器 create；命令服务 submit/
 * invalidate；ui 经 project 间接读（投影）。
 * 非法调用（§9.3 契约表）：worker 内 create（SA-15：装配不暴露——机械保证
 * 见 Confirmable.cpp 内 static_assert 与 DT-CFM-8 契约测试）；Confirmed 后
 * submitConfirmation（InvalidState）；跨命令重用 callbackToken（token 一次性）。
 *
 * 错误语义（AGENTS.md 错误语义落点，§9.0 总纲）：调用方契约违约 fail-fast 抛
 * DiagnosticsError；状态类失败（结果枚举可表达的）走返回值——submitConfirmation
 * "返回值与异常双轨：结果枚举优先"（§9.3 注释原文）。
 */
class IConfirmableFindingService {
public:
    virtual ~IConfirmableFindingService() = default;

    /// 创建（§9.3 原文签名；project 域处理器在 prepare 阶段调用；worker 装配
    /// 中不暴露本接口——SA-15）。前置/后置/错误见 §9.3 注释与实现类同方法。
    virtual FindingRecord create(const core::ConfirmableFinding& finding,
                                 std::string_view commandType, core::ContentIdentity commandDigest,
                                 core::ProjectId, core::BranchId, core::RevisionId baseRevisionId,
                                 std::optional<core::ContentIdentity> policyContentId,
                                 std::vector<core::ObjectId> subjectScope) = 0;

    /// 提交确认（§9.3 原文签名；project 收到 ui 凭据后调用）。结果枚举优先。
    virtual ConfirmOutcome submitConfirmation(FindingId, std::uint64_t callbackToken,
                                              core::ConfirmationCredential) = 0;

    /// 提交拒绝（§9.3 原文签名；ui 返回 rejected）。
    virtual void submitRejection(FindingId, std::uint64_t callbackToken,
                                 std::string_view reasonToken) = 0;

    /// 使失效（§9.3 原文签名；project 关闭/中止/失权信号——§5.3 失效条件的服务端入口）。
    virtual void invalidate(FindingId, std::string_view reasonToken) = 0;

    /// 查询（§9.3 原文签名；ui 确认对话数据源/命令服务复核）。noexcept 值返回。
    virtual std::optional<FindingRecord> tryFind(FindingId) const noexcept = 0;

    /// 按输入修订列待确认项（§9.3 原文签名；命令服务 S4 收集面）。
    virtual std::vector<FindingRecord> pendingFor(core::RevisionId) const = 0;
};

// =====================================================================
// ConfirmableService（§9.3 实现——IConfirmableFindingService 的阶段 A 唯一实现）
// =====================================================================

/**
 * @brief 可确认诊断服务实现（§5.4 状态机＋§5.3 失效条件＋绑定双复核的承载）。
 *
 * 构造注入（L5 装配期——引用须覆盖服务生命周期，本类不接管所有权）：
 *   - registry：稳定码注册表（create 前置"code 的 confirmable=true"校验面；
 *     运行期只读消费——服务不注册/不废弃码）。
 *   - environment：绑定复核环境探针（当前 tip/策略身份/命令载荷摘要——见
 *     IConfirmationEnvironment 注释；服务端复核义务不可缺，故为必填引用）。
 *   - clock：时间注入（createdAtUtc 与 expiresAtUtc 检查——测试注入手动时钟，
 *     L5 注入 SystemClock，§4.2 同款）。
 *   - devLog：开发日志路由（可空指针——§5.4 转移约束"每次状态转移写入开发级
 *     日志"的承载；两级日志管线随 DIAG-T07 落地，接线登记同 DIAG-T04 先例：
 *     落地前传 nullptr＝转移日志静默跳过，落地后装配必注入）。
 *
 * 行为要点（逐条对应 §5.3/§5.4/§9.3）：
 *   - create：findingId 分配（generate）、绑定四元组计算与冻结、callbackToken
 *     分配（进程内单调，一次性）、状态 Pending；前置校验见 create 注释。
 *   - submitConfirmation：Pending＋token 匹配前置；惰性过期检查；绑定四元组
 *     复核（findingDigest 重算自检＋environment 当前值比对——不符成员归类
 *     rejectionReason：baseRevisionId→revision-changed／policyContentId→
 *     policy-changed／其余→binding-mismatch）；一致则 Confirmed＋confirmation
 *     写入＋token 作废（一次性——成功后同样不可再用）。
 *   - submitRejection：Pending→Invalidated(入参 reasonToken)；core 投影同步
 *     reject()（用户拒绝→core Rejected——阻止应用，MDL-06④）。
 *   - invalidate：§5.3 失效条件的服务端入口（write-lost/interaction-lost/
 *     canceled 等由 project 信号驱动；revision-changed/binding-mismatch 由
 *     project 编译前复核失配后驱动）；Pending/Confirmed→Invalidated(入参
 *     reasonToken)——Confirmed 失效＝编译前复核失配的登记面（DT-CFM-4：凭据
 *     失效按未确认处置）；Invalidated/Expired 再失效→InvalidState（不可逆）。
 *   - 惰性过期：tryFind/pendingFor/submitConfirmation 入口检查 expiresAtUtc
 *     （显式设置时）；到达即转移 Expired（终态）。默认 nullopt 恒不触发
 *     （P-DIAG-6——DT-CFM-3"默认实例恒 Pending"）。
 *   - 每次转移写 Dev 日志（findingId/转移/原因——devLog 注入时；§5.4 可追溯）。
 *
 * 线程安全（§9.3 契约表"线程"行）：并发安全；状态转移内部互斥（std::mutex
 * 短临界区——复核探针调用也在临界区内，IConfirmationEnvironment 实现方受
 * "短小、无阻塞"约束传导）；查询路径同样加锁（惰性过期是状态转移）。
 *
 * 生命周期：进程级服务＋会话级记录（§9.3 契约表——阶段 A 不做自动清理，
 * 回收归 DIAG-T09 目录清理周期）。
 */
class ConfirmableService final : public IConfirmableFindingService {
public:
    /**
     * @brief 构造（L5 装配期注入——见类注释各引用的契约）。
     *
     * @param registry    [in] 稳定码注册表（运行期只读消费；生命周期覆盖服务）
     * @param environment [in] 绑定复核环境探针（实现方须满足临界区约束——见接口注释）
     * @param clock       [in] 时钟（createdAtUtc/expiry 检查的时间来源）
     * @param devLog      [in] 开发日志路由（可空——DIAG-T07 接线登记见类注释）
     *
     * @throws DiagnosticsError Usage（callbackToken 起始值冲突等装配违约不会
     *         发生——构造不抛面目前为空；保留异常子句与 §9.3 错误面一致）
     */
    ConfirmableService(const IDiagnosticRegistry& registry,
                       const IConfirmationEnvironment& environment,
                       const IClock& clock, IDevLogSink* devLog);

    // 禁拷贝/禁移动：注册表/探针引用注入＋互斥成员——进程级服务语义（§9.3
    // 契约表"生命周期/所有权"行）。
    ConfirmableService(const ConfirmableService&) = delete;
    ConfirmableService& operator=(const ConfirmableService&) = delete;

    // ---- IConfirmableFindingService（§9.3 五方法——行为契约见类注释与方法注释）----
    FindingRecord create(const core::ConfirmableFinding& finding,
                         std::string_view commandType, core::ContentIdentity commandDigest,
                         core::ProjectId project, core::BranchId branch,
                         core::RevisionId baseRevisionId,
                         std::optional<core::ContentIdentity> policyContentId,
                         std::vector<core::ObjectId> subjectScope) override;
    ConfirmOutcome submitConfirmation(FindingId findingId, std::uint64_t callbackToken,
                                      core::ConfirmationCredential credential) override;
    void submitRejection(FindingId findingId, std::uint64_t callbackToken,
                         std::string_view reasonToken) override;
    void invalidate(FindingId findingId, std::string_view reasonToken) override;
    std::optional<FindingRecord> tryFind(FindingId findingId) const noexcept override;
    std::vector<FindingRecord> pendingFor(core::RevisionId baseRevisionId) const override;

    /**
     * @brief 显式设置过期时间（实现类原语——不在 §9.3 虚接口上，seal() 先例）。
     *
     * 为什么存在：DT-CFM-3 要求验证"仅当 expiresAtUtc 显式设置时过期"的状态机
     * 分支，而 create 契约签名（§9.3 逐字承载）无该参数、字段表明文"字段保留
     * 供未来需求变更（P-DIAG-6）"。本方法即"显式设置"的唯一起点——**当前
     * 装配清单不调用**（默认 nullopt 无限期）；未来需求引入确认超时时的接线
     * 点（启用＝需求变更＋码表/文案更新，P-DIAG-6 处置原文——本设施不私裁
     * 语义，只承载转移分支的正确性）。
     *
     * @param findingId [in] 目标 finding（未登记→Usage；非 Pending→InvalidState）
     * @param expiresAt [in] 过期时刻（UTC——注入 IClock 的时基）
     *
     * @throws DiagnosticsError Usage（未登记）；InvalidState（终态不可加时）
     */
    void armExpiration(FindingId findingId, std::chrono::system_clock::time_point expiresAt);

private:
    /**
     * @brief 惰性过期检查（须在互斥锁内调用——内部会执行状态转移）。
     *
     * 进入条件：记录 state==Pending 且 expiresAtUtc 显式设置且当前时钟 ≥ 过期
     * 时刻——转移 Expired（终态）＋token 作废＋Dev 日志。返回转移后的当前态
     * （调用方据此继续判定前置）。默认 nullopt 恒不触发（P-DIAG-6）。
     *
     * const 成员：转移对象是传入的记录（查询路径的惰性转移——服务自身
     * 可变状态不被修改，查询方法因此保持 const）。
     */
    void expireIfDue(FindingRecord& record) const;

    /**
     * @brief 复核绑定四元组（§5.3——确认提交时的服务端复核半区）。
     *
     * 须在互斥锁内调用。复核序（§5.3 四元组序，首个不符成员即定性——reason
     * 归类锚点）：
     *   ①findingDigest：重算 record 摘要与冻结值比对（服务端自检——内存记录
     *     未变则恒一致；不一致＝记录损坏，归 binding-mismatch）；
     *   ②commandDigest：与环境 currentCommandDigest 比对（nullopt＝无活动命令
     *     ——跨命令重用场景，必不符；归 binding-mismatch）；
     *   ③policyContentId：绑定有值时与环境 currentPolicyContentId 比对（环境
     *     nullopt 或值不等→policy-changed；绑定 nullopt＝非策略来源，跳过）；
     *   ④baseRevisionId：与环境 currentTipRevision 比对（不等→revision-changed）。
     *
     * @param record [in,out] 目标记录（复核不符时由调用方执行失效转移——本
     *               函数只判定不转移，保持"判定/转移"分离可测）
     * @param reason [out] 首个不符成员的归类 token（finding_reason 词表）
     * @return true＝四元组全部一致
     */
    bool verifyBinding(const FindingRecord& record, std::string_view& reason) const;

    /**
     * @brief 状态转移公共尾（Dev 日志＋token 作废——须在互斥锁内调用）。
     *
     * §5.4 转移约束："每次状态转移写入开发级日志（findingId/转移/原因
     * token——可追溯）"。callbackToken 一次性：任何终态转移后令牌作废。
     * const 成员（理由同 expireIfDue——只演进传入记录）。
     */
    void finishTransition(FindingRecord& record, FindingState to,
                          std::optional<std::string_view> reason) const;

    const IDiagnosticRegistry* m_registry;             ///< 稳定码注册表（非拥有；create confirmable 校验面）
    const IConfirmationEnvironment* m_environment;     ///< 绑定复核环境探针（非拥有；当前值来源）
    const IClock* m_clock;                             ///< 时钟（非拥有；createdAtUtc/expiry 时基）
    IDevLogSink* m_devLog;                             ///< 开发日志路由（可空——DIAG-T07 接线登记）
    std::uint64_t m_nextToken = 1;                     ///< callbackToken 分配器（0＝保留值——无效令牌）
    /// 全部会话级记录（findingId→记录）。std::map：findingId 字典序稳定遍历
    /// （pendingFor 输出确定性——同集合同序，NFR-COR-02 精神）。
    std::map<FindingId, FindingRecord> m_records;
    /// 状态转移互斥（§9.3"状态转移内部互斥（短临界区，无 I/O）"）。
    mutable std::mutex m_mutex;
};

// =====================================================================
// 装配面静态断言（SA-15——DT-CFM-8 的产品侧机械保证半区）
// =====================================================================

/**
 * SA-15 原文：工作进程内的批量计算不产生可确认诊断（确认只发生在命令边界）；
 * §9.3 非法调用行："worker 内 create（装配不暴露）"。
 *
 * 机械保证（类型层面——链接面/装配清单断言的编译期半区，DT-CFM-8）：
 * worker 进程的装配清单中，诊断通道只有 IDiagnosticSink/IDevLogSink（§7.5
 * 回传面）；以下断言钉住"诊断通道与确认服务在类型上互不替代"——worker 即便
 * 错误拿到通道接口，也无法将其当作确认服务使用（无 create 面）；可确认类型
 * 无法经普通诊断通道传递（report 只收 DiagnosticRecord，与 ConfirmableFinding
 * 无任何转换路径——core 强类型纪律）。运行期半区（worker 装配清单不含本
 * 服务、通道帧均为普通诊断）由契约测试 ConfirmableProjectStubTest 钉住。
 */
static_assert(!std::is_convertible_v<IDiagnosticSink*, IConfirmableFindingService*>,
              "SA-15：诊断通道（worker 可得面）不得可转换为确认服务——确认只在命令边界");
static_assert(!std::is_convertible_v<IDevLogSink*, IConfirmableFindingService*>,
              "SA-15：开发日志路由不得可转换为确认服务——worker 不产生可确认诊断");
static_assert(!std::is_convertible_v<core::ConfirmableFinding, core::DiagnosticRecord>,
              "SA-15：可确认诊断不得隐式窄化为普通记录经 report 通道回传（core 强类型纪律）");

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_CONFIRMABLE_HPP
