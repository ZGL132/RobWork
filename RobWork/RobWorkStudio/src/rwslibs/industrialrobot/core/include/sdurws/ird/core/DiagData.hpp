/**
 * @file   DiagData.hpp
 * @brief  公共诊断数据契约——DiagnosticRecord/ComparativeFields/ConfirmableFinding。
 *
 * 设计依据：
 *   - units/core.md §4.8（字段表与 C-1~C-3 不变量）、§5.7（签名）、§8 UT-DIAG
 *   - 需求 ERR-01（诊断记录字段）、UX-03（比较型三要素）、SA-15（确认放行流）、
 *     MDL-06（invalid 保留原文/不适用显式标记）
 *   - 任务契约 tasks/foundation/CORE-T07.json（≙WP-03-T07）
 *   - F-618（所有者裁决 2026-10-09；core.md v0.12，2026-10-10）：DiagnosticLevel
 *     词表与 DiagnosticRecord.level 字段增列（尾追加，缺省 Error）
 *
 * 责任边界（§4.8 数据边界）：core 只承载数据结构与工厂不变量（C-1~C-3）——
 * 码值权威归 diagnostics StableCodeRegistry；确认决策记录随命令摘要持久化归
 * project；凭据真伪/重放校验归 project 命令服务；Rejected 后续处置归 project（§7.1）。
 *
 * 依赖：ObjectId（Identity.hpp）/UnitToken＋SourcedValue（Units/Provenance）——
 * 全为 core 内已落位契约。
 * 线程安全：纯值类型。
 */

#ifndef SDURWS_IRD_CORE_DIAGDATA_HPP
#define SDURWS_IRD_CORE_DIAGDATA_HPP

#include <chrono>
#include <optional>
#include <string>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>

namespace sdurws::ird::core {

/// 稳定诊断码 token（§4.8：句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64；core 仅承载——
/// 码值分配与合法性权威＝diagnostics StableCodeRegistry，CR-08）。
using DiagCode = std::string;

/// 比较型三要素的单侧值（UX-03）：数值可 NotApplicable/Invalid（不适用显式标记、
/// 非法保留原串——SourcedValue 语义复用，不另设第二套）。
struct ComparativeValue {
    SourcedValue<double> quantity;   ///< 数值侧（四态承载）
    UnitToken unit;                  ///< 显示单位（已注册 token）

    bool operator==(const ComparativeValue& o) const noexcept
    {
        return quantity == o.quantity && unit == o.unit;
    }
    bool operator!=(const ComparativeValue& o) const noexcept { return !(*this == o); }
};

/// 比较型三要素（实际/期望两侧；阈值由 code/cause 语境承载——§4.8 两字段表）。
struct ComparativeFields {
    ComparativeValue actual;     ///< 实际值侧
    ComparativeValue expected;   ///< 期望值侧

    bool operator==(const ComparativeFields& o) const noexcept
    {
        return actual == o.actual && expected == o.expected;
    }
    bool operator!=(const ComparativeFields& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 诊断级别（阻断轴词表——F-618，2026-10-10 增列；core.md §4.8 同批 v0.12）。
 *
 * 两值冻结词表：一经交付只允许表尾追加并走单元卡增量修订（持久化契约面
 * 纪律——DiagnosticLevel 与 DiagnosticSeverity 同规则）。语义＝"该诊断发布
 * 时是否阻断"：Error 阻断、Warning 不阻断。置于 core 的依据：runtime
 * builder 按 record.level 拒绝 error 级记录（runtime §4.3.5），而 runtime
 * 不链接 diagnostics、只经 core 契约承载——级别承载必须可达 runtime
 * （与 DiagCode 同一承载逻辑，CR-08：core 只承载词表，不承载判定规则）。
 *
 * 与 diagnostics::DiagnosticSeverity（4 值呈现轴）正交：severity 决定呈现
 * 分流（Error/Warning/Info/Dev），本级别只决定阻断轴；两者的映射
 * （severity==Error ⇒ level==Error，其余 ⇒ Warning）归 diagnostics
 * StableCodeRegistry 注册期验证强制（诊断卡 §4.5），core 不承载映射。
 */
enum class DiagnosticLevel : std::uint8_t {
    Error,   ///< error——阻断级（发布模型诊断块的构造拒绝面；缺省值）
    Warning, ///< warning——警告级（不阻断，随模型诊断块/目录发布）
};

/**
 * @brief 稳定（可持久化/可入报告）诊断项记录。
 *
 * 构造只能经 make()（C-3 校验：code 句法合法＋必填串非空）；默认构造仅供
 * 容器占位（文档化：勿使用未经验证的默认对象）。
 * subject：稳定诊断项必须携带合法 ObjectId；瞬时开发诊断可空——完整性强制
 * 归 diagnostics/reporting 边界（§10.3 交接），core 不越权。
 *
 * level（F-618 增列，字段表尾追加——PA-2：既有字段语义零改写）：
 * 阻断轴级别，缺省 Error。缺省语义取舍＝fail-closed：警告产生路径必须
 * 显式声明 Warning，漏标在 runtime builder 可见地被拒（该发的发不出，
 * 立即可测）——而非静默放行（该拦的没拦，破坏 §4.3.5 不变量）。工厂
 * make() 以尾参缺省承载，既有构造路径（不关心级别的 error 面）源码兼容。
 */
struct DiagnosticRecord {
    DiagCode code;                                  ///< 稳定码（句法合法）
    std::optional<ObjectId> subject;                ///< 主体对象（稳定项必带）
    std::optional<std::string> localName;           ///< 局部名（缺失＝空 optional，不伪造）
    std::optional<std::string> runtimeName;         ///< 运行时名（⑥名称端口取得）
    std::string context;                            ///< 上下文描述（文案权威在 diagnostics/ui）
    std::string cause;                              ///< 原因
    std::string recommendedAction;                  ///< 建议动作
    std::optional<ComparativeFields> comparison;    ///< 比较型三要素（非比较型为空）
    DiagnosticLevel level = DiagnosticLevel::Error; ///< 级别（F-618 尾追加；缺省 Error
                                                    ///  ——警告面须显式声明 Warning）

    /**
     * @brief 工厂（C-3 校验）。
     *
     * @throws CoreError code 句法/长度违约（"core/diag/code:"）或 context/cause/
     *         recommendedAction 为空串（"core/diag/required:"）
     *
     * @param level [in] 阻断轴级别（F-618 尾参；缺省 Error——既有调用方
     *              零改写；警告面调用方显式传 DiagnosticLevel::Warning）
     */
    static DiagnosticRecord make(DiagCode code, std::optional<ObjectId> subject,
                                 std::optional<std::string> localName,
                                 std::optional<std::string> runtimeName,
                                 std::string context, std::string cause,
                                 std::string recommendedAction,
                                 std::optional<ComparativeFields> comparison = {},
                                 DiagnosticLevel level = DiagnosticLevel::Error);

    bool operator==(const DiagnosticRecord& o) const noexcept;
    bool operator!=(const DiagnosticRecord& o) const noexcept { return !(*this == o); }
};

/// 确认状态（SA-15：pending→confirmed/rejected；后续处置归 project）。
enum class ConfirmationState { Pending, Confirmed, Rejected };

/// 确认凭据（SA-15：主体＋UTC 时刻——time_point 无包装类型，D-02）。
struct ConfirmationCredential {
    std::string principal;                                            ///< 确认主体（采集归 ui/project）
    std::chrono::system_clock::time_point confirmedAtUtc{};           ///< UTC 确认时刻

    bool operator==(const ConfirmationCredential& o) const noexcept
    {
        return principal == o.principal && confirmedAtUtc == o.confirmedAtUtc;
    }
    bool operator!=(const ConfirmationCredential& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 可确认诊断（SA-15 比较型子类）。
 *
 * 构造只能经 make()（C-1：record.comparison 必须存在——可确认诊断必为比较型）；
 * 确认状态推进经 confirm()/reject()（C-2：Confirmed ⇔ credential.has_value()，
 * Pending/Rejected 不得携带凭据——工厂与推进方法双重强制）。
 */
struct ConfirmableFinding {
    DiagnosticRecord record;                                   ///< 底层记录（必为比较型）
    ConfirmationState state = ConfirmationState::Pending;      ///< 确认状态（默认 pending）
    std::optional<ConfirmationCredential> credential;          ///< 凭据（仅 Confirmed 态有值）

    /**
     * @brief 工厂（C-1：record.comparison 必须存在，否则抛
     *        CoreError("core/diag/c1:")——不可比较的发现不可确认）。
     */
    static ConfirmableFinding make(DiagnosticRecord record);

    /// 确认（Pending→Confirmed，须带凭据；他态调用抛 CoreError("core/diag/state:")）。
    void confirm(ConfirmationCredential credential);

    /// 否决（Pending→Rejected；他态调用抛 CoreError）。
    void reject();

    bool operator==(const ConfirmableFinding& o) const noexcept;
    bool operator!=(const ConfirmableFinding& o) const noexcept { return !(*this == o); }
};

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_DIAGDATA_HPP
