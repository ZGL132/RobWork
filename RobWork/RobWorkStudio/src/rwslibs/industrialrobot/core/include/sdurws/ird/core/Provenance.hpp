/**
 * @file   Provenance.hpp
 * @brief  值来源与缺失值——ValueProvenance（来源标记）＋SourcedValue<T>（四态字段值）。
 *
 * 设计依据：
 *   - units/core.md §4.3（ProvenanceKind 五类/ValueProvenance 字段表/P-1 不变量/
 *     SourcedValue 四态表）、§5.3（接口签名）、§8 UT-MISS、D-05 语义边界（DYN-06）
 *   - 需求 NFR-COR-03（不静默转 0）、MDL-05/06/16、DYN-06
 *   - 任务契约 tasks/foundation/CORE-T03.json（≙WP-03-T03，UT-MISS 载体）
 *
 * 背景说明（三件事的分工）：
 *   ①ValueProvenance＝"这个值从哪来"的事实记录（用户输入/几何估算/目录回填/
 *     导入映射/派生只读五类——ARCH §6.2）；它是事实，**不是**可信等级、**不构成**
 *     正式通过结论（DYN-06：降级判定归 dynamics/evidence）。
 *   ②SourcedValue<T>＝字段级四态（有值/未提供/不适用/非法保留原串）——
 *     MDL-06"缺失不触发断言、走 DataInsufficient 降级"的承载类型；
 *   ③不变量 P-1："sourceVersion 有值 ⇒ sourceObject 有值"（工厂校验，违约抛错）。
 *
 * 确定性：纯值类型，无环境/时钟依赖（NFR-COR-02）。
 * 线程安全：纯值语义（不可变共享安全）。
 */

#ifndef SDURWS_IRD_CORE_PROVENANCE_HPP
#define SDURWS_IRD_CORE_PROVENANCE_HPP

#include <optional>
#include <string>
#include <string_view>

#include <sdurws/ird/core/Digest.hpp>      // ContentVersion（sourceVersion 类型）
#include <sdurws/ird/core/Errors.hpp>      // CoreError
#include <sdurws/ird/core/Identity.hpp>    // ObjectId（sourceObject 类型）

namespace sdurws::ird::core {

/**
 * @brief 值来源五类（§4.3 token 冻结——与 ARCH §6.2 五类一一对应）。
 */
enum class ProvenanceKind {
    UserProvided,       ///< user-provided      用户直接输入
    GeometricEstimate,  ///< geometric-estimate 几何解析估算（MDL-05 唯一公式表）
    CatalogBackfill,    ///< catalog-backfill   器件目录回填（SEL-10）
    ImportMapped,       ///< import-mapped      导入通道字段映射（URDF/CSV/WorkCell）
    DerivedReadOnly,    ///< derived-readonly   派生只读（MDL-02/09 权威互斥的另一侧）
};

/**
 * @brief 枚举→稳定 token（小写连字符，§4.3 token 列——报告/诊断机器判读用）。
 *
 * @param kind [in] 来源类别
 * @return 静态串（无所有权转移）；未知值返回 "unknown"（防御，不可达）
 */
const char* toToken(ProvenanceKind kind) noexcept;

/**
 * @brief token→枚举（严格匹配；try 轨不抛）。
 *
 * @param token [in] 冻结 token（如 "geometric-estimate"）
 * @return 命中返回类别；未命中返回 nullopt
 */
std::optional<ProvenanceKind> provenanceKindFromToken(std::string_view token) noexcept;

/**
 * @brief 值来源记录（四字段；相等＝四字段全等）。
 *
 * 构造只能经 make()（工厂校验 P-1 与 methodTag 语法）——默认构造出的对象
 * 仅供容器占位，kind 未定义不承诺（文档化：勿使用未经验证的默认对象）。
 */
struct ValueProvenance {
    ProvenanceKind kind = ProvenanceKind::UserProvided;   ///< 来源类别（必须显式给出）
    std::optional<ObjectId> sourceObject;                 ///< 来源关联对象（如目录条目/导入记录）
    std::optional<ContentVersion> sourceVersion;          ///< sourceObject 的内容版本（SEL-08 引用基础）
    std::optional<std::string> methodTag;                 ///< 方法短标记（如 MDL-05/hollow-cylinder）

    /**
     * @brief 工厂：校验不变量 P-1 与 methodTag 语法后构造。
     *
     * @param kind          [in] 来源类别
     * @param sourceObject  [in] 来源关联对象（可选）
     * @param sourceVersion [in] 内容版本（可选；**仅当 sourceObject 存在时允许存在**——P-1）
     * @param methodTag     [in] 方法标记（可选；语法 [a-z0-9./_-]、非空、≤64 字符——
     *                           词表归产生单元，core 只做语法校验）
     * @return 校验通过的来源记录
     *
     * @throws CoreError P-1 违约（"core/provenance/p1:"）或 methodTag 语法违约
     *         （"core/provenance/method-tag:"）
     */
    static ValueProvenance make(ProvenanceKind kind,
                                std::optional<ObjectId> sourceObject = {},
                                std::optional<ContentVersion> sourceVersion = {},
                                std::optional<std::string> methodTag = {});

    bool operator==(const ValueProvenance& o) const noexcept;
    bool operator!=(const ValueProvenance& o) const noexcept { return !(*this == o); }
};

/// 字段四态（§4.3 表：token 冻结——provided/not-provided/not-applicable/invalid）。
enum class FieldState {
    Provided,       ///< 有值
    NotProvided,    ///< 未提供（MDL-06：降级语义，不断言）
    NotApplicable,  ///< 不适用（ERR-01 显式标记）
    Invalid,        ///< 已提供但非法（保留原串）
};

/**
 * @brief 字段级四态值（§4.3 表——"缺失≠零"的类型化承载）。
 *
 * 四态语义（token 冻结）：
 *   - Provided      有值（value()/tryValue()/provenance() 可用）
 *   - NotProvided   未提供（MDL-06：不触发断言，走 DataInsufficient 降级——
 *                   由 dynamics/evidence 判定，本类型只记录事实）
 *   - NotApplicable 不适用（ERR-01：显式标记，不伪造数值）
 *   - Invalid       已提供但非法（**保留原始输入串**——NFR-COR-03：不得静默转 0）
 *
 * @tparam T 值类型（需可默认构造＋可拷贝；数值场景 T=double——Quantity<K> 后续）
 *
 * "无默认值语义"（§4.3 明文）：默认构造为 NotProvided；内部占位值初始化为 T{}
 * 但**契约上不可观测**——NotProvided 态调用 value() 抛错而非返回 0，
 * "缺失＝零" 的通道在类型层面被切断（UT-MISS 钉住）。
 *
 * 线程安全：纯值语义。
 */
template <class T>
class SourcedValue {
public:
    /// 默认构造＝NotProvided（无默认值语义，见类注释）。
    SourcedValue() = default;

    /// 有值态（provenance 必须显式——来源不缺席）。
    static SourcedValue provided(T value, ValueProvenance provenance)
    {
        SourcedValue v;
        v.state_ = FieldState::Provided;
        v.value_ = std::move(value);
        v.provenance_ = std::move(provenance);
        return v;
    }

    /// 未提供态（MDL-06 降级语义的事实记录）。
    static SourcedValue notProvided()
    {
        SourcedValue v;
        v.state_ = FieldState::NotProvided;
        return v;
    }

    /// 不适用态（ERR-01 显式标记）。
    static SourcedValue notApplicable()
    {
        SourcedValue v;
        v.state_ = FieldState::NotApplicable;
        return v;
    }

    /// 非法态：保留原始输入串（raw 非空——空串拒绝，CoreError("core/provenance/invalid:")）。
    static SourcedValue invalid(std::string rawInput)
    {
        if (rawInput.empty()) {
            throw CoreError("core/provenance/invalid: 原始输入串不得为空"
                            "（非法态的存在意义是保留原串）");
        }
        SourcedValue v;
        v.state_ = FieldState::Invalid;
        v.rawInput_ = std::move(rawInput);
        return v;
    }

    /// 当前四态。
    FieldState state() const noexcept { return state_; }

    /**
     * @brief 取值（非抛出）：仅 Provided 返回值，其余返回 nullopt。
     */
    std::optional<T> tryValue() const noexcept
    {
        if (state_ != FieldState::Provided) { return std::nullopt; }
        return value_;
    }

    /**
     * @brief 取值（抛出轨迹）：前置 state()==Provided。
     *
     * @return 值的 const 引用（生命周期随本对象）
     * @throws CoreError 非 Provided 态（消息含四态 token——"缺失＝零"通道被切断）
     */
    const T& value() const
    {
        if (state_ != FieldState::Provided) {
            throw CoreError(std::string{"core/provenance/value: 状态为 "}
                            + stateToken(state_) + "，无值可取（缺失不得转零/伪造）");
        }
        return value_;
    }

    /**
     * @brief 原始输入串（前置 state()==Invalid）。
     *
     * @return 非法输入原文（导入层登记的原串，供诊断/修正回路）
     * @throws CoreError 非 Invalid 态
     */
    const std::string& invalidRawInput() const
    {
        if (state_ != FieldState::Invalid) {
            throw CoreError(std::string{"core/provenance/invalid-raw: 状态为 "}
                            + stateToken(state_) + "，无原始输入串");
        }
        return rawInput_;
    }

    /**
     * @brief 来源记录（仅 Provided 有语义——§4.3 表"无意义"态返回默认对象，
     *        调用方须先查 state()；此处按表承诺返回默认而非抛错）。
     */
    const ValueProvenance& provenance() const noexcept { return provenance_; }

    /// 相等＝状态与对应载荷全等（Provided 比值＋来源；Invalid 比原串；空态互等）。
    bool operator==(const SourcedValue& o) const noexcept
    {
        if (state_ != o.state_) { return false; }
        switch (state_) {
        case FieldState::Provided:
            return value_ == o.value_ && provenance_ == o.provenance_;
        case FieldState::Invalid:
            return rawInput_ == o.rawInput_;
        default:
            return true;   // NotProvided/NotApplicable 无载荷
        }
    }
    bool operator!=(const SourcedValue& o) const noexcept { return !(*this == o); }

private:
    /// 四态 token（§4.3 token 列——错误消息机器判读用；文件内局部）。
    static const char* stateToken(FieldState s) noexcept
    {
        switch (s) {
        case FieldState::Provided:      return "provided";
        case FieldState::NotProvided:   return "not-provided";
        case FieldState::NotApplicable: return "not-applicable";
        case FieldState::Invalid:       return "invalid";
        }
        return "unknown";
    }

    FieldState state_ = FieldState::NotProvided;   ///< 四态（默认 NotProvided）
    T value_{};                                    ///< Provided 态载荷（其余态为占位，不可观测）
    ValueProvenance provenance_{};                 ///< Provided 态来源记录
    std::string rawInput_;                         ///< Invalid 态原始输入串
};

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_PROVENANCE_HPP
