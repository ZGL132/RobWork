/**
 * @file   Codec.hpp
 * @brief  IRobotDesignCodec——建模对象 canonical 编码/解码（§9.4.9 行原文
 *         契约的落位）：五对象变体的确定性序列化登记（core.md §6.3 分工
 *         ——建模对象"对什么做摘要"由本头声明，project 对收到的字节计算
 *         ContentVersion）。
 *
 * 设计依据：
 *   - units/modeling.md §3.3（公共头表 Codec.hpp 行："IRobotDesignCodec：
 *     建模对象 canonical 编码/解码（确定性序列化登记，core §6.3 分工）"）、
 *     §9.4.9（接口签名行："Expected<Bytes> encode(const ObjectVariant&,
 *     FormatVersion) const；Expected<ObjectVariant> decode(Bytes,
 *     FormatVersion) const；未知主版本→SchemaVersionUnsupported
 *     （NFR-DEP-04）"）、§4.8（"对象字节由 modeling canonical 编码……
 *     确定性序列化：字段定序、小端、UTF-8、无填充、集合按 ObjectId 规范
 *     文本字典序；ContentVersion 由 project 计算——处理器不自行申报"）、
 *     §7.1（派生缓存不入编码身份）、§4.3-B（selfCollisionHints 不入根
 *     对象编码权威语义）、§14.2 D-MDL-5、project.md §4.8（canonical 编码
 *     契约与 schema 演进口径）
 *   - units/core.md §6.3（"对什么做摘要归各所有者"）、§4.2（SHA-256 唯一
 *     实现＝core::ContentDigester——本单元不实现第二套摘要；ContentVersion
 *     由 project 对字节计算，modeling 不产出）
 *   - 需求 NFR-COR-02（同对象→同字节，跨进程一致）、NFR-DEP-04（未知
 *     版本拒绝不猜测）、NFR-COR-03（不产出半成品）、CON-05（内容寻址）
 *   - 先例与形态：runtime/Codec.hpp（IRDCANO——大端系 codec 的同构契约
 *     形态；modeling 卡面 §4.8 明文**小端**，两单元各按卡面登记，互不
 *     混用）
 *   - 任务契约 tasks/foundation/WP-13-T03.json acceptance 1（IRobotDesignCodec
 *     确定性编解码；robot-design token 复用 runtime 字面）/4（canonical
 *     编码确定性＋往返＋SchemaVersionUnsupported）/3（D-MDL-5 编码身份）
 *
 * 背景说明（本编解码在证据链里的角色）：project 提交修订时按对象存储
 * canonical 字节并对字节计算 ContentVersion（cv-<64hex>）——同一模型快照
 * 在不同会话、不同进程产出**逐字节相同**的编码，是"同语义必得同版本"
 * （确定性 NFR-COR-01/02）与证据链可比的前提。因此本编码不含任何
 * 环境/时间/地址量，字段序固定，浮点按 IEEE754 位模式（非文本）承载。
 *
 * ★ 派生字段排除（D-MDL-5，编码身份的 modeling 侧声明点）：
 *   - StandardDH 权威态：joints[].axis/origin 为派生只读——**不编码**；
 *   - Explicit 权威态：joints[].dhDerived 为派生展示缓存——**不编码**；
 *   - LinkEntry.selfCollisionHints：导入中间产物，不入根对象编码权威
 *     语义（§4.3-B 表行原文）——不编码，往返不保真（设计使然）。
 *   派生值在读取时按需确定性重算（重算入口随 T09 IDhExplicitConverter
 *   落位）——重算不改变对象字节＝不改变 ContentVersion（§7.1 原文）。
 *
 * 失败语义：
 *   - encode：输入是内部已构造的类型化数据（合法性归构造/编辑边界——
 *     §4.10），本函数忠实编码；唯一拒绝面＝版本参数不可产出（无降级/
 *     升级编码器——NFR-DEP-04 同口径）→ SchemaVersionUnsupported；
 *   - decode：输入是外部字节（可能截断/篡改/异版），走查询轨 Expected
 *     值面（不抛）——全部校验失败（magic/版本/截断/越界/非法枚举/
 *     非法 presence/非有限 double/UTF-8 不成形/集合未规范化/不变量违例）
 *     → MalformedPayload；版本不可支持 → SchemaVersionUnsupported。
 *     不产出半成品（任一校验失败即整体失败——NFR-COR-03）。
 *
 * 线程安全：Codec 无共享可变状态、可重入——多线程并发调用安全（§3.4
 * 总约定 1 纯函数类服务）。确定性：同对象重复编码逐字节相等；同字节
 * 解码同结果（NFR-COR-02）。
 */

#ifndef IRD_MODELING_CODEC_HPP
#define IRD_MODELING_CODEC_HPP

#include <cstdint>
#include <variant>
#include <vector>

#include <sdurws/ird/modeling/ObjectTypes.hpp>  // 五对象 token（robot-design 复用 runtime 字面）
#include <sdurws/ird/modeling/Parts.hpp>        // 四部件值模型
#include <sdurws/ird/modeling/RobotDesign.hpp>  // 根对象值模型＋ModelingError
#include <sdurws/ird/runtime/Errors.hpp>        // runtime::Expected 模板（查询轨两态载体——登记边 runtime）

namespace sdurws::ird::modeling {

// =====================================================================
// 版本与字节载体（§9.4.9 FormatVersion/Bytes；project.md §4.8 演进口径）
// =====================================================================

/**
 * @brief 编解码格式版本（project.md §4.8："schemaVersion 主版本变更＝
 *        破坏性（走升级器）；追加可选字段＝次版本兼容"）。
 *
 * major＝对象 schema 主版本（各对象单一权威常量 kXxxSchemaVersion——
 * ObjectTypes.hpp；编解码头与值模型字段交叉核对）；minor＝编码格式次
 * 版本（当前 0——表尾追加可选字段时 +1，旧 minor 字节仍可读）。
 */
struct FormatVersion {
    std::uint32_t major = 1;  ///< schema 主版本（≥1；破坏性变更递增）
    std::uint32_t minor = 0;  ///< 格式次版本（向后兼容追加递增）

    bool operator==(const FormatVersion& o) const noexcept
    {
        return major == o.major && minor == o.minor;
    }
    bool operator!=(const FormatVersion& o) const noexcept { return !(*this == o); }
};

/// 当前编解码器能产出/完全支持的格式版本（单一权威；升版走单元卡增量
/// 修订登记——破坏性变更另走设计变更评审）。
inline constexpr FormatVersion kCurrentFormatVersion{1, 0};

/// 编码字节载体（§9.4.9 "Bytes"——UTF-8/二进制混合的字节流，见文件头布局）。
using Bytes = std::vector<std::uint8_t>;

/**
 * @brief 查询轨两态结果（§9.4.9 "Expected<...>"的落位形态）。
 *
 * 复用 runtime::Expected 模板（登记边 runtime 的公共契约设施——建模侧
 * 错误侧固定为 ModelingError），不另写第二份两态模板：跨单元两套 Expected
 * 会在错误处理代码里制造无谓的分叉。语义（runtime/Errors.hpp 契约）：
 * 恰持一侧；对错误态 get()／对成功态 error()＝契约违约抛 std::logic_error
 * （fail-fast，不返回默认值静默吞错——NFR-COR-03）。
 */
template <class T>
using Expected = runtime::Expected<T, ModelingError>;

/**
 * @brief 建模对象变体（§4.2 五对象全集；variant 备择序＝表行序——
 *        编码 wireType 依此序编号，表尾追加纪律同对象 token）。
 */
using ObjectVariant = std::variant<RobotDesign, ToolDefinition, SceneObject,
                                   PoseSet, DrivetrainDesign>;

// =====================================================================
// IRobotDesignCodec——接口（§9.4.9 行原文签名）
// =====================================================================

/**
 * @brief 建模对象 canonical 编码/解码接口（版本化；确定性序列化登记——
 *        core.md §6.3 分工的 modeling 侧声明点）。
 *
 * 实现要求（§3.4 总约定 1）：无共享可变状态、可重入、多线程并发安全；
 * 不读环境变量/时钟/locale/文件系统随机性；同输入字节→同输出字节。
 */
class IRobotDesignCodec {
public:
    virtual ~IRobotDesignCodec() = default;

    /**
     * @brief 确定性编码（§9.4.9；布局与排除字段见文件头——字段定序、
     *        小端、UTF-8、无填充、集合按 ObjectId 规范文本字典序）。
     *
     * @param object  [in] 待编码对象（五变体之一；忠实编码——合法性归
     *               构造/编辑边界，见文件头"失败语义"）
     * @param version [in] 请求的编码格式版本：须等于 kCurrentFormatVersion
     *               且 major 与对象 schemaVersion 字段一致（本编解码器无
     *               降级/升级产出能力——NFR-DEP-04 拒绝猜测）
     * @return ok＝canonical 字节（同对象重复调用逐字节相等；调用方持有）；
     *         err＝SchemaVersionUnsupported（params：object-type/
     *         schema-version/supported-major——与 DiagCodes.hpp 同码
     *         paramSchema 对齐）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual Expected<Bytes> encode(const ObjectVariant& object, FormatVersion version) const = 0;

    /**
     * @brief 解码（§9.4.9——外部字节进入类型化世界的唯一 model 侧闸口）。
     *
     * 校验链（任一失败整体失败，不产出半成品——NFR-COR-03）：
     *   ①magic/版本：未知主版本（> 或 < 支持主版本）或超出支持次版本
     *     →SchemaVersionUnsupported（NFR-DEP-04：拒绝而非尽力猜测）；
     *   ②结构：长度前缀逐字段解码，截断/越界/尾随字节/非法枚举值/非法
     *     presence 值/非有限 double/UTF-8 不成形（含 NUL）→MalformedPayload
     *     （detail 携字节偏移定位）；
     *   ③规范化：引用表（toolRefs/sceneRefs 按 ObjectId 规范文本、
     *     resourceManifest 按 resourceId、tcpList/位姿集按 key）的字典序
     *     与唯一性→违者 MalformedPayload（canonical 形态是身份前提）；
     *   ④不变量：解码产物经 checkInvariants 全量复核（I-MDL-1～10 根
     *     对象／I-MDL-5 工具／I-MDL-11+12 传动〔R1 锁定口径〕）→违例
     *     MalformedPayload（防绕过构造边界的字节——同 RT-Codec parse 的
     *     builder 复核先例）。
     *
     * @param bytes     [in] encode() 产出的字节（只读；可为任意来源）
     * @param supported [in] 调用方支持的格式版本（一般传 kCurrentFormatVersion）
     * @return ok＝重建的对象（canonical 形态——派生缓存按 D-MDL-5 不在
     *         字节内：DH 态 axis/origin 为 NotProvided 待重算、显式态
     *         dhDerived 为空）；err＝校验失败（见校验链）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02）。
     */
    virtual Expected<ObjectVariant> decode(const Bytes& bytes, FormatVersion supported) const = 0;
};

/**
 * @brief IRobotDesignCodec 的唯一产品实现（无状态——可默认构造，随处
 *        持有；拷贝/移动平凡）。
 */
class RobotDesignCodec final : public IRobotDesignCodec {
public:
    Expected<Bytes> encode(const ObjectVariant& object, FormatVersion version) const override;
    Expected<ObjectVariant> decode(const Bytes& bytes, FormatVersion supported) const override;
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_CODEC_HPP
