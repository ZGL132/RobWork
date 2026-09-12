/**
 * @file   Resource.hpp
 * @brief  资源引用值类型——ResourceState＋ResourceRef（§8.6 原文契约的值类型部分）。
 *
 * 设计依据：
 *   - units/runtime.md §8.6（资源与外部引用——ResourceRef 字段原文、
 *     "路径不作身份"规则总表）、§3.1（模块清单——Resource.hpp 承载
 *     ResourceRef/ResourceManifest/ResourceReadError/IRuntimeResourceProvider）
 *   - 需求 CON-03（资源固化三段边界——Recorded/Solidified）、CON-05
 *     （内容寻址——引用以内容摘要为键）、ARC-04（跨对象引用一律 ObjectId）
 *   - 任务契约 tasks/foundation/RT-T03.json（产物 1：Description.hpp 的
 *     RobotDesignDescription.resourceRefs 字段类型依赖——见下方分阶段说明）
 *
 * ★ 分阶段落位登记（RT-T03，实现与 §12 任务表的偏差按 DTB §5.4 登记）：
 *   §12 任务表未给 Resource.hpp 单列任务行；其值类型（本文件）是 §4.2
 *   RobotDesignDescription.resourceRefs 的字段类型、亦是 RT-T04
 *   CanonicalModel.resourceManifest（§4.3.5）的前置依赖，故随首个消费者
 *   RT-T03 提前落位。§8.6 的其余实体（ResourceBytes/ResourceReadError/
 *   IRuntimeResourceProvider）依赖 provider 注入语境，随资源消费任务
 *   （RT-T07+/阶段 B io 接入）落位——本头届时增量扩充，已落位值类型的
 *   字段与语义不变。
 *
 * 身份与引用纪律（§8.6 规则总表第一行）：ResourceRef 以 resourceId＋
 * contentDigest 为键；sourcePathHint 仅是追溯提示——不作身份、不作引用键
 * （路径变化而内容不变 ⇒ 身份不变、不触发失效；RT-RES-3 钉住）。
 *
 * 线程安全：纯值类型（无共享可变状态）；构造后只读使用。
 * 确定性：字段均为定长/排序无关的值（NFR-COR-02 的可编码面）。
 */

#ifndef SDURWS_IRD_RUNTIME_RESOURCE_HPP
#define SDURWS_IRD_RUNTIME_RESOURCE_HPP

#include <cstdint>
#include <optional>
#include <string>

#include <sdurws/ird/core/Digest.hpp>    // Digest256（内容摘要——SHA-256 32 字节）
#include <sdurws/ird/core/Identity.hpp>  // ObjectId（资源对象身份）
#include <sdurws/ird/runtime/Errors.hpp> // RuntimeErrorCode/Expected（§8.6 错误侧——
                                         //  v0.10 随 RT-T09 增量实体引入）

namespace sdurws::ird::runtime {

/**
 * @brief 资源在项目内的存在状态（CON-03/PM-01 三段边界的 runtime 侧两态）。
 *
 * 语义（§8.6/CON-03）：
 *   - Recorded    已登记引用、内容仍在外部源（未固化）——编译可用，但
 *                 正式评估前须固化（阻断判定归 evidence 的 Verified 门禁，
 *                 runtime 只产出警告级诊断、不代为阻断——§8.6 规则总表）；
 *   - Solidified  已固化为项目内不可变副本——存储不可变保证，编译期
 *                 资源复查（§5.4）对其免复查。
 * 枚举顺序即 §8.6 原文声明顺序；仅两值，新增走设计变更评审。
 */
enum class ResourceState {
    Recorded,   ///< 已登记、未固化（正式评估前须固化——CON-03）
    Solidified, ///< 已固化（项目内不可变副本——复查豁免）
};

/**
 * @brief CanonicalModel 内的资源引用（§8.6 原文形态；§4.3.5 消费同一类型）。
 *
 * 值语义纯结构；全字段由 reader/建模侧给值，runtime 不解析资源字节本身
 * （解析/安全读取/预算归 io——§8.6 职责边界）。字段含义与单位：
 *   - resourceId     资源对象身份（外部源记录经 CON-03 固化后成为项目对象；
 *                    跨修订稳定——ARC-04，下游持久引用一律 ObjectId）；
 *   - contentDigest  内容摘要（SHA-256，32 字节；io 计算后传入或编译器
 *                    复算——资源以内容入身份，§4.3.5/CON-05）；
 *   - sourcePathHint 源路径提示（仅追溯用——不作身份、不作引用键，可空）；
 *   - state          Recorded | Solidified（见 ResourceState 注释）；
 *   - accessVersion  读取契约版本（io 格式版本，无单位；同一资源的
 *                    读取格式升级以此区分——归 io 语义，runtime 只承载）。
 *
 * 线程安全：纯值。
 */
struct ResourceRef {
    core::ObjectId resourceId;                   ///< 资源对象身份（键之一）
    core::Digest256 contentDigest{};             ///< 内容摘要（键之二——内容寻址）
    std::optional<std::string> sourcePathHint;   ///< 源路径提示（仅追溯——不作身份）
    ResourceState state = ResourceState::Recorded; ///< 存在状态（默认 Recorded）
    std::uint32_t accessVersion = 0;             ///< 读取契约版本（io 格式版本）
};

// =====================================================================
// ResourceBytes/ResourceReadError/IRuntimeResourceProvider（§8.6 原文契约
// ——自 v0.10 起随 RT-T09 落位：Compiler.hpp 的 CompileRequest.resources
// 字段类型依赖本接口，首个消费者原则；io 侧实现仍归阶段 B，本头只冻结
// runtime 侧消费面。见 units/runtime.md §15.4 v0.10 登记）。
// =====================================================================

/**
 * @brief 已验证资源字节视图（§8.6 原文契约——"已验证内容"）。
 *
 * 语义：io 侧在返回前已完成 SafePath/预算/缺失与摘要校验（§8.6 职责边界
 * ——"解析、安全读取、预算、缺失/变化检测归 io"），runtime 只消费并按
 * digest 复核（S4/§5.4 复查归编译链）。★ 生命周期（§8.6/P-IO-2 裁决）：
 * 缓冲由 provider 持有至其析构——调用方（编译链）同步消费，**不跨调用
 * 长期持有、不接管释放权**（data/size 为借持视图）。
 * 值语义纯结构；线程安全（只读借持）。
 */
struct ResourceBytes {
    const std::uint8_t* data = nullptr; ///< 资源字节起始（provider 持有；借持）
    std::size_t size = 0;               ///< 字节数（无单位——字节计数；0＝空资源）
    core::Digest256 digest{};           ///< 内容摘要（SHA-256；与 ResourceRef.contentDigest
                                        ///<  同源——S4 复核的比对值）
};

/**
 * @brief 资源读取失败载荷（§8.6 原文契约——tryResourceBytes 错误侧）。
 *
 * 错误语义：code 取 Errors.hpp 的资源类码（ResourceMissing/ResourceChanged/
 * ResourceBudget——§8.6 规则总表"四类诊断严格区分"的原始检测面；io 负责
 * 检测与原始诊断，runtime 在 S4 就地定位 resourceId 转译）。resourceId
 * 回显请求值（诊断可定位）；detail 为 io 侧开发诊断细节。
 * 值语义纯结构；线程安全。
 */
struct ResourceReadError {
    RuntimeErrorCode code;              ///< 失败码（资源类三码之一）
    core::ObjectId resourceId;          ///< 请求的资源对象身份（原名回显）
    std::string detail;                 ///< 开发诊断细节（io 侧检测依据）
};

/**
 * @brief 资源只读提供者（§8.6 原文契约——io 实现、阶段 B 接入；L5 装配注入）。
 *
 * 语义：按资源对象身份取**已验证**内容字节（SafePath/预算在 io 侧已过——
 * 见方法注释）。注入边界与 IObjectBytesSource 同模式（§3.3：值传递＋最小
 * 注入接口；runtime 零 io 编译依赖——ARCH §3.5）。
 * 实现方约束：并发只读安全（§5.5 注入接口统一约定）；返回字节的缓冲由
 * 实现持有（P-IO-2 生命周期裁决——见 ResourceBytes 注释）；同 id 重复
 * 调用须确定性（同字节或稳定失败——编译期资源复查 §5.4 的前提）。
 */
class IRuntimeResourceProvider {
public:
    virtual ~IRuntimeResourceProvider() = default;

    /**
     * @brief 尝试取资源已验证字节（只读、非抛出——Expected 两态轨）。
     * @param resourceId [in] 资源对象稳定身份（ARC-04）
     * @return ok＝已验证资源字节视图（缓冲归 provider——同步消费）；err＝
     *         读取失败（ResourceReadError——资源类码＋回显定位），不抛
     */
    virtual Expected<ResourceBytes, ResourceReadError>
        tryResourceBytes(core::ObjectId resourceId) const = 0;
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_RESOURCE_HPP
