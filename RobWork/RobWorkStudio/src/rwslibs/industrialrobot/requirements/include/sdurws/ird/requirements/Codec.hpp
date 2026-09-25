/**
 * @file   Codec.hpp
 * @brief  IRequirementCodec——需求对象 canonical 编码/解码（§9.5 行原文
 *         契约的落位）：五对象变体的确定性序列化登记（core.md §6.3 分工
 *         ——需求对象"对什么做摘要"由本头声明，project 对收到的字节计算
 *         ContentVersion，处理器不自行申报）。
 *
 * 设计依据：
 *   - units/requirements.md §3.3（公共头表 Codec.hpp 行——T03）、§9.5
 *     （IRequirementCodec 行原文签名："Expected<vector<uint8_t>,
 *     RequirementError> encode(const RequirementObjectVariant&, uint32_t
 *     formatVersion) const；Expected<RequirementObjectVariant,
 *     RequirementError> decode(bytes, formatVersion) const"）、§4.7
 *     （I-REQ-1 集合字典序——canonical 形态前提）、§4.8（RequirementProfile
 *     派生档不入 canonical 编码）、§6.2（必验 schema 解析确定性——
 *     requiredCaseSetId 由 evidence 对 entries 规范编码计算，本编码是其
 *     输入面）、§8.4（planContentIdentity 的 canonical 输入＝规范化计数
 *     ——D-REQ-2，本编码是其登记落点）
 *   - units/core.md §6.3（"对什么做摘要归各所有者"）、§4.2（SHA-256 唯一
 *     实现＝core::ContentDigester——本单元不实现第二套摘要）
 *   - project.md §4.8 演进口径（schema 主版本破坏性/次版本兼容——与
 *     ObjectTypes.hpp 版本常量注释同源）
 *   - 先例与形态：modeling/Codec.hpp（WP-13-T03 同款落位形态——magic/
 *     定长小端/长度前缀字符串/IEEE754 位模式浮点/decode 四步校验链）
 *   - 任务契约 tasks/foundation/WP-14-T03.json acceptance 2（canonical
 *     编码登记用例：roundtrip 逐字段一致/确定性/schema 主版本拒绝/
 *     RequirementProfile 不入编码/编码登记落位）
 *
 * 背景说明（本编解码在证据链里的角色）：project 提交修订时按对象存储
 * canonical 字节并对字节计算 ContentVersion——同一需求快照在不同会话、
 * 不同进程产出**逐字节相同**的编码，是"同语义必得同版本"（NFR-COR-01/
 * 02）与 evidence 切片可比（requiredCaseSetId/planContentIdentity 均以
 * 本编码字节为输入）的前提。因此本编码不含任何环境/时间/地址量，字段
 * 序固定，浮点按 IEEE754 位模式承载（非文本），集合条目按 ObjectId 规
 * 范文本字典序写出（I-REQ-1——同集合任意输入序必得同字节）。
 *
 * ★ 派生档排除（§4.8，编码身份的 requirements 侧声明点）：
 *   - RequirementProfile（必验清单/Must-Should 计数/覆盖汇总/
 *     contentIdentity）为 DerivedReadOnly 派生视图——**不是编码对象**：
 *     RequirementObjectVariant 五备择之外无该类型，重算即得；
 *   - 诊断 subjectObjectId 锚（O-36）＝条目 objectId 字段本身，随条目
 *     内嵌于集合对象字节——不单独编码、不进 objectRefs（复核范围仅五
 *     个真实存储对象）。
 *
 * ★ 两态载体自持的决策登记（与 modeling 复用 runtime::Expected 不同）：
 * 卡 §3.2 边表 runtime 行明文"无编译依赖……（如需引用其值类型则登记此
 * 边）"——登记该边属治理面（ird_gates_whitelist.cmake），且 T02 红线
 * 测试已把产品面 include 封闭在五边（runtime 不在其列）。故本单元按
 * runtime/Errors.hpp 的 Expected 语义自持同构最小模板（文件尾
 * RequirementExpected）——两单元字节/语义互不识别是各域序列化登记彼此
 * 独立的既定特性（modeling/Codec.cpp 头注同款论断）。若后续治理会话
 * 登记 requirements→runtime 边，可无损切换复用（值语义一致）。
 *
 * 失败语义：
 *   - encode：输入是内部已构造的类型化数据（合法性归构造/编辑边界——
 *     §9.2），本函数忠实编码（集合条目取规范化副本排序写出，I-REQ-1）；
 *     唯一拒绝面＝版本参数不可产出（无降级/升级编码器——NFR-DEP-04）
 *     → SchemaVersionUnsupported；
 *   - decode：输入是外部字节（可能截断/篡改/异版），走查询轨 Expected
 *     值面（不抛）。四步校验链（任一失败整体失败，不产出半成品——
 *     NFR-COR-03）：①magic/版本：未知主版本或超支持次版本 →
 *     SchemaVersionUnsupported（detail 携升级指引）；②结构：长度前缀
 *     逐字段解码，截断/越界/尾随字节/非法枚举/非法 presence/非有限
 *     double/UTF-8 不成形（含 NUL）→ MalformedPayload（detail 携字节
 *     偏移）；③规范化：集合条目字典序与唯一性违者 → MalformedPayload
 *     （canonical 形态是身份前提，I-REQ-1）；④不变量：解码产物经
 *     validateTaskPoint/validateWorkRegion/validateOperatingCondition/
 *     validateSamplingPlan＋集合唯一性全量复核（I-REQ-3/4/5/6 结构面）
 *     → 违例 MalformedPayload（防绕过构造边界的字节）。
 *
 * 线程安全：Codec 无共享可变状态、可重入——多线程并发调用安全（§3.4
 * 总约定 1）。确定性：同对象重复编码逐字节相等；同字节解码同结果
 * （NFR-COR-02）。
 */

#ifndef IRD_REQUIREMENTS_CODEC_HPP
#define IRD_REQUIREMENTS_CODEC_HPP

#include <sdurws/ird/requirements/Errors.hpp>         // RequirementError——两态失败侧
#include <sdurws/ird/requirements/ObjectTypes.hpp>   // 五对象 token＋schema 版本常量
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 五对象值模型＋RequirementObjectVariant

#include <cstdint>
#include <stdexcept>
#include <variant>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// 版本与字节载体（§9.5 formatVersion；project.md §4.8 演进口径）
// =====================================================================

/**
 * @brief 编解码格式版本（project.md §4.8："schemaVersion 主版本变更＝
 *        破坏性（走升级器）；追加可选字段＝次版本兼容"）。
 *
 * major＝对象 schema 主版本（各对象单一权威常量 kReqXxxSchemaVersion——
 * ObjectTypes.hpp；编码头与对象 schemaVersion 字段交叉核对）；minor＝
 * 编码格式次版本（当前 0——表尾追加可选字段时 +1，旧 minor 字节仍可读）。
 */
struct CodecFormatVersion {
    std::uint32_t major = 1;  ///< schema 主版本（≥1；破坏性变更递增）
    std::uint32_t minor = 0;  ///< 格式次版本（向后兼容追加递增）

    bool operator==(const CodecFormatVersion& o) const noexcept
    {
        return major == o.major && minor == o.minor;
    }
    bool operator!=(const CodecFormatVersion& o) const noexcept { return !(*this == o); }
};

/// 当前编解码器能产出/完全支持的格式版本（单一权威；升版走单元卡增量
/// 修订登记——破坏性变更另走设计变更评审）。
inline constexpr CodecFormatVersion kCurrentRequirementFormatVersion{1, 0};

/// 编码字节载体（§9.5 "vector<uint8_t>"；UTF-8/二进制混合的字节流）。
using RequirementBytes = std::vector<std::uint8_t>;

// =====================================================================
// RequirementExpected——查询轨两态结果（§9.5 "Expected<...>"的落位形态；
// 自持决策见文件头"两态载体自持的决策登记"）
// =====================================================================

/**
 * @brief 成功/失败两态的值语义结果（runtime::Expected 同构最小面）。
 *
 * 语义（NFR-COR-03 不静默吞错）：
 *   - 恰持有一侧：ok 态持 T、err 态持 E（std::variant 承载）；
 *   - get()/error() 是带前置的访问器：对错误侧调 get()（或对成功侧调
 *     error()）属调用方契约违约——抛 std::logic_error fail-fast，绝不
 *     返回默认值静默吞错（NFR-COR-03"静默转默认"禁令）；
 *   - 工厂 ok()/err() 为唯一构造入口。
 *
 * @tparam T 成功值类型（值语义）
 * @tparam E 错误值类型（固定 RequirementError——Expected 别名）
 *
 * 线程安全：纯值类型；const 访问器并发只读安全。
 */
template <class T, class E>
struct RequirementExpected {
    /// 两态载体：index 0＝成功（T）、index 1＝错误（E）。
    std::variant<T, E> value;

    /// 是否成功态（true ⇒ get() 可用，error() 不可用）。
    bool ok() const noexcept { return value.index() == 0; }

    /**
     * @brief 取成功值（前置 ok()）。
     * @return 成功值的 const 引用（生命周期随本对象）
     * @throws std::logic_error 前置违约（对错误态取值——fail-fast）
     */
    const T& get() const
    {
        if (!ok()) {
            throw std::logic_error(
                "requirements/expected/get-on-error: 对错误态调用 get()");
        }
        return std::get<0>(value);
    }

    /**
     * @brief 取错误值（前置 !ok()）。
     * @return 错误值的 const 引用（生命周期随本对象）
     * @throws std::logic_error 前置违约（对成功态取错误）
     */
    const E& error() const
    {
        if (ok()) {
            throw std::logic_error(
                "requirements/expected/error-on-ok: 对成功态调用 error()");
        }
        return std::get<1>(value);
    }

    /// 成功态工厂。
    static RequirementExpected ok(T v) { return RequirementExpected{std::variant<T, E>(std::in_place_index<0>, std::move(v))}; }
    /// 失败态工厂。
    static RequirementExpected err(E e) { return RequirementExpected{std::variant<T, E>(std::in_place_index<1>, std::move(e))}; }
};

/// §9.5 签名的 Expected 别名（错误侧固定为 RequirementError）。
template <class T>
using Expected = RequirementExpected<T, RequirementError>;

// =====================================================================
// IRequirementCodec——接口（§9.5 行原文签名；formatVersion 以
// CodecFormatVersion 承载——major/minor 两段语义，uint32 入参见
// adaptEncode/adaptDecode 便利重载）
// =====================================================================

/**
 * @brief 需求对象 canonical 编码/解码接口（版本化；确定性序列化登记——
 *        core.md §6.3 分工的 requirements 侧声明点）。
 *
 * 实现要求（§3.4 总约定 1）：无共享可变状态、可重入、多线程并发安全；
 * 不读环境变量/时钟/locale/文件系统随机性；同输入→同输出字节。
 */
class IRequirementCodec {
public:
    virtual ~IRequirementCodec() = default;

    /**
     * @brief 确定性编码（布局总表见实现文件头——字段定序、小端、UTF-8、
     *        无填充、集合按 ObjectId 规范文本字典序）。
     *
     * @param object        [in] 待编码对象（五变体之一；忠实编码——合法性
     *                      归构造/编辑边界，见文件头"失败语义"）
     * @param formatVersion [in] 请求的编码格式版本：须等于
     *                      kCurrentRequirementFormatVersion 且 major 与对象
     *                      schemaVersion 字段一致（本编解码器无降级/升级
     *                      产出能力——NFR-DEP-04 拒绝猜测）
     * @return ok＝canonical 字节（同对象重复调用逐字节相等；调用方持有）；
     *         err＝SchemaVersionUnsupported（params：object-type/
     *         schema-version/supported-major——与 DiagCodes.hpp 同码
     *         paramSchema 对齐；detail 携升级指引）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual Expected<RequirementBytes> encode(const RequirementObjectVariant& object,
                                              CodecFormatVersion formatVersion) const = 0;

    /**
     * @brief 解码（外部字节进入类型化世界的唯一 requirements 侧闸口；
     *        四步校验链见文件头"失败语义"）。
     *
     * @param bytes         [in] encode() 产出的字节（只读；可为任意来源）
     * @param formatVersion [in] 调用方支持的格式版本（一般传
     *                      kCurrentRequirementFormatVersion）
     * @return ok＝重建的对象（canonical 形态）；err＝校验失败
     *         （SchemaVersionUnsupported／MalformedPayload，detail 携
     *         定位——升级指引或字节偏移/不变量 token）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual Expected<RequirementObjectVariant> decode(
        const RequirementBytes& bytes, CodecFormatVersion formatVersion) const = 0;
};

/**
 * @brief IRequirementCodec 的唯一产品实现（无状态——可默认构造，随处
 *        持有；拷贝/移动平凡）。
 */
class RequirementCodec final : public IRequirementCodec {
public:
    Expected<RequirementBytes> encode(const RequirementObjectVariant& object,
                                      CodecFormatVersion formatVersion) const override;
    Expected<RequirementObjectVariant> decode(
        const RequirementBytes& bytes, CodecFormatVersion formatVersion) const override;
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_CODEC_HPP
