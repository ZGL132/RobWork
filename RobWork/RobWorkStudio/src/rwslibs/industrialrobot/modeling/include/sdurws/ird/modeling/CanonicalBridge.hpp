/**
 * @file   CanonicalBridge.hpp
 * @brief  CanonicalBridge——建模对象到 runtime 编译输入的桥梁（§9.1/§9.2/
 *         §9.4.5）：ICanonicalModelInputBuilder（修订闭包→
 *         runtime::RobotDesignDescription 的全量构造器，reader 的内核）＋
 *         RobotDesignReader（runtime::IRobotDesignReader 的 modeling 产品
 *         实现，L5 装配注入 runtime 十段编译链 S2）。
 *
 * 设计依据：
 *   - units/modeling.md §9.1（字段映射表——RobotDesign/部件对象→
 *     RobotDesignDescription 十类来源映射，全 SI；身份/版本/快照绑定——
 *     modeling 保证"同输入字节→同 Description"，不重复计算任何内容身份；
 *     资源快照 Recorded/Solidified 声明口径；不完整模型三层防线）、
 *     §9.2（IRobotDesignReader 决议——P-RT-5 交叉核对结论：reader 注入
 *     形态，read(objectBytes) 单根字节输入＋部件对象经闭包域字节源解析，
 *     该源在 L5 组装 CompileRequest 时与 CompileRequest.objects 同源绑定；
 *     单位 SI 化在 reader 生成 Description 前经 core 唯一换算入口完成；
 *     直解方案否决——descriptionContractVersion 单点适配 R-MDL-3）、
 *     §9.4.5（ICanonicalModelInputBuilder 原文签名：build——闭包→
 *     Description；@错误 RefMissing→runtime InputInvalid 预演|UnitIllegal|
 *     DhExpandFailed|SchemaVersionUnsupported；RobotDesignReader final）、
 *     §3.3（公共头表 CanonicalBridge.hpp 行——T12）、§3.4（纯函数服务：
 *     无共享可变状态、可重入、确定性 NFR-COR-02）、§7.4/§7.6（DH 权威先
 *     经 §7.4 展开为显式表示；零位折叠——origin 入几何/限位平移；基座—
 *     世界变换隔离 M-11：不在此计算 R_world_base）
 *   - units/runtime.md §4.2（RobotDesignDescription 中性值类型——字段语义
 *     与单位纪律的接收侧契约）、§4.4（CanonicalModel 内只有 SI 真值——
 *     reader 侧履行的 modeling 落点）、§5.2 S2（reader 在十段链的位置：
 *     reader 失败→S2 归属 InputInvalid 含定位）、§9.1（资源快照行为——
 *     Recorded 每次编译重读＋digest 复核、Solidified 免复查——本单元只做
 *     声明面，行为归 runtime）
 *   - 需求 MDL-14（RuntimeNameMap 输入质量——localName 输入面断言 V-26，
 *     名称解析/前缀拼接/消歧归 runtime，R-4）、MDL-06（编译触发与原子性
 *     ——reader 纯函数是编译确定性 ARC-03 的建模侧保证）、ARC-03（同
 *     修订→同 Description→同 CanonicalModel）、ARC-04（对象 ID 解析不漏/
 *     不重复—— objectId 透传）
 *   - O-36 裁决（2026-09-22，治理会话奉所有者批次授权；P-MDL-1 关闭面）：
 *     objectRefs/CM-0 复核范围＝真实存储对象（根/工具/场景/位姿集/传动/
 *     资源），关节/连杆子 ObjectId 为模型内标识（诊断 subject 与名称映射
 *     锚）——本单元 builder 据此只对真实存储对象做闭包存在性复核
 *   - 任务契约 tasks/foundation/WP-13-T12.json acceptance 1～5
 *
 * 背景说明（第一读者须知——三个类型各自动什么）：
 *   1. ObjectClosureView＝闭包域字节源抽象：按对象类型 token 取根对象、
 *      按 ObjectId 解引用部件对象。它是 §9.2"闭包域字节源"的 modeling 侧
 *      契约面——L5 装配期以与 CompileRequest.objects 同源的存储适配实现
 *      （NFR-MNT-04：适配器归应用壳）；reader/builder 只见本接口，不触达
 *      project/runtime 存储类型（R-1：业务域互不链接）。
 *   2. ICanonicalModelInputBuilder＝全量 Description 构造器（§9.1 字段
 *      映射表的唯一产品实现）：解引用根对象引用表（tools/scene/drivetrain/
 *      资源），DH 权威先经 §7.4 无损展开，零位按 §7.6/T09 同款纪律折叠，
 *      产出全 SI 的 RobotDesignDescription。纯函数——同闭包→同 Description
 *      （确定性是 ARC-03"同修订→同 CanonicalModel"的建模侧前提）。
 *   3. RobotDesignReader＝runtime::IRobotDesignReader 的产品实现（L5 注入
 *      runtime S2）：read(objectBytes, formatVersion) 单根字节输入——解码
 *      根对象后委托 builder 内核，部件对象经构造期绑定的闭包域字节源解析
 *      （§9.2 注入形态）。失败一律转 RuntimeError(InputInvalid)——§5.2 S2
 *      归属表的建模侧履行。
 *
 * ★ 本单元不做的事（红线自查锚，§2.4/§14.5）：
 *   - 不做名称解析/前缀拼接/消歧（R-4——RuntimeNameMap 唯一归 runtime）；
 *     本单元只断言 localName 输入质量（V-26 建模侧观测面），名称原样透传；
 *   - 不计算/缓存/二次叠加基座—世界旋转 R_world_base（M-11）——base 块只
 *     传编辑表示（preset/customEaa/basePosition），矩阵归 runtime 编译产物
 *     （P-RT-4：预设轴向唯一权威产出点＝runtime BaseWorldTransform）；
 *   - 不重复计算任何内容身份：ContentVersion 归 project、builtFrom/
 *     contentIdentity 归 runtime——builder 对资源只透传清单已登记摘要
 *     （CON-05 内容寻址），绝不自算 SHA-256；
 *   - 不假设 runtime 修复输入：无法以合法 SI 真值映射的字段一律值面拒绝
 *     （NFR-COR-03 不静默补默认/不伪造），runtime S3/S5 兜底不归本单元。
 *
 * 线程安全：本头全部服务为无共享可变状态的纯函数服务（§3.4 总约定 1）——
 * 可重入、多线程并发调用安全；RobotDesignReader 持有的闭包视图指针由调用
 * 方保证并发只读安全（runtime §5.5 注入接口同款约定）。
 * 确定性（NFR-COR-01/02）：不读环境变量/时钟/locale/文件系统；同输入→同
 * 输出（含错误值与参数序）。
 */

#ifndef IRD_MODELING_CANONICALBRIDGE_HPP
#define IRD_MODELING_CANONICALBRIDGE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // DiagnosticRecord（builder 诊断输出参数）
#include <sdurws/ird/core/Identity.hpp>      // ObjectId（部件对象解引用键）
#include <sdurws/ird/modeling/Errors.hpp>    // ModelingError/ModelingErrorCode（builder 错误面）
#include <sdurws/ird/runtime/Description.hpp>  // RobotDesignDescription/IRobotDesignReader（接收侧契约）
#include <sdurws/ird/runtime/Errors.hpp>     // Expected/RuntimeError（reader 返回轨）

namespace sdurws::ird::modeling {

// =====================================================================
// ObjectClosureView——闭包域字节源（§9.2 注入形态的 modeling 侧契约面）
// =====================================================================

/**
 * @brief 闭包内对象的一次取回产出（类型 token＋canonical 字节）。
 *
 * 语义：objectTypeToken 是该对象在修订闭包中的存储登记类型（project
 * ObjectRef.objectTypeToken 同源值）——builder 以它做 token 复核（RefMissing
 * 的"token 不匹配"分支，§9.4.5 @错误 行）；bytes 是对象 canonical 字节
 * （Codec.decode 的输入面）。值语义纯结构；线程安全。
 */
struct ClosureObject {
    std::string objectTypeToken;             ///< 存储登记的对象类型 token（路由/复核面）
    std::vector<std::uint8_t> bytes;         ///< 对象 canonical 字节（Codec.decode 输入）
};

/**
 * @brief 闭包域字节源抽象（§9.2——"部件对象由 reader 内部经闭包域字节源
 *        解析；该源在 L5 组装 CompileRequest 时与 CompileRequest.objects
 *        同源绑定（同一修订闭包）"）。
 *
 * 为什么是 modeling 自持抽象而不是直接复用 runtime::IObjectBytesSource：
 * runtime 源按 (ObjectId, ContentVersion) 定址，而 modeling 根对象引用表
 * （toolRefs/sceneRefs/drivetrainRef）只持 ObjectId 不持 cv（§4.2 修订闭包
 * 与引用——版本由修订闭包锁定）；"闭包域"意味着实现体在构造期已绑定目标
 * 修订，按键取字节即可。L5 以同一存储同时适配两个接口（同源绑定的落点）。
 *
 * 实现方约束（与 runtime §5.5 注入接口同款）：
 *   - 并发只读安全（reader 纯函数的前提）；
 *   - 确定性：同键重复取回同字节或稳定 nullopt（ARC-03 同闭包→同
 *     Description 的来源侧前提）；
 *   - 闭包域纪律：只应答目标修订闭包内的对象（越界取回＝防混入 CM-0 的
 *     实现侧违约，runtime S2 另有 objectInRevision 全量复核兜底）；
 *   - tryObjectByToken 对同 token 多对象的歧义应由实现拒绝（返回 nullopt
 *     或实现自定义的确定性取位——runtime S2"恰一个 robot-design"语义的
 *     建模侧预演；builder 侧对 nullopt 一律 RefMissing）。
 *
 * 生命周期：非 owning——实现由 L5/调用方持有并保证 builder/reader 调用期
 * 存活（CompileRequest 注入指针同款约定，runtime §3.3）。
 */
class ObjectClosureView {
public:
    virtual ~ObjectClosureView() = default;

    /**
     * @brief 按对象类型 token 取唯一对象（根对象路由——§9.4.5 @pre
     *        "closure 含根对象（token=robot-design，runtime
     *        kRobotDesignObjectType 路由）"）。
     * @param objectTypeToken [in] 对象类型 token（如 "robot-design"）
     * @return 命中＝token＋字节；nullopt＝闭包内无该 token 对象（或实现侧
     *         歧义拒绝——见类注）
     *
     * 纯函数；并发只读安全；确定性。
     */
    virtual std::optional<ClosureObject>
        tryObjectByToken(std::string_view objectTypeToken) const = 0;

    /**
     * @brief 按对象身份取对象（部件解引用——tools/scene/drivetrain/固化
     *        资源；§9.2"reader 不读越界/越修订数据"的取回面）。
     * @param objectId [in] 目标对象稳定身份（ARC-04）
     * @return 命中＝token＋字节；nullopt＝不在闭包（builder→RefMissing）
     *
     * 纯函数；并发只读安全；确定性。
     */
    virtual std::optional<ClosureObject>
        tryObject(const core::ObjectId& objectId) const = 0;
};

// =====================================================================
// ICanonicalModelInputBuilder——全量 Description 构造器（§9.4.5 原文抽象）
// =====================================================================

/**
 * @brief 规范输入构造器抽象（§9.4.5 原文契约——"从修订闭包构造 runtime
 *        Description（reader 的内核，纯函数）"）。
 *
 * 实现要求（§3.4 总约定 1）：无共享可变状态、可重入、多线程并发调用安全；
 * 不读环境变量/时钟/locale/文件系统随机性；同闭包→同 Description。
 */
class ICanonicalModelInputBuilder {
public:
    virtual ~ICanonicalModelInputBuilder() = default;

    /**
     * @brief 解引用根对象引用表并生成 Description（全 SI；DH 权威先展开——
     *        §9.4.5 原文签名；字段映射逐行见 §9.1 表与实现文件注释）。
     *
     * @pre closure 含根对象（token=robot-design，runtime kRobotDesignObjectType
     *      路由）；部件/资源经同一闭包源解析（§9.4.5 @pre 原文）。
     * @post 同闭包→同 Description 字节（确定性）；单位 SI 化唯一经 core
     *       Units 换算入口（§3.4 总约定 4——SA-12 的 modeling 侧履行点）。
     *
     * @param closure [in] 闭包域字节源（只读；调用期存活——非 owning）
     * @param diags   [out] 诊断输出（追加不清空）。★ 本接口两态都**不产
     *               诊断记录**：全部失败经返回值错误侧携带（ModelingError
     *               值面＋定位参数）；§9.5 无 builder 专属登记码，按
     *               Errors.hpp 阶段纪律"无已登记映射码＝不得产诊断，错误
     *               经值面返回"执行（产码唯一经 IDiagnosticFactory——
     *               §9.5；builder 为无状态纯函数、不持工厂注入，DhConvert
     *               dhToExplicit 同款先例）。参数为 §9.4.5 原文签名形状的
     *               契约预留位。
     * @return ok＝Description（值语义，调用方持有）；err＝ModelingError
     *         （码面：RefMissing——闭包缺对象/token 不匹配/引用不完整，
     *         runtime 侧按 InputInvalid 预演转译｜UnitIllegal——单位/量纲
     *         不合法（含 Invalid 态字段与名称输入质量面 IllegalName）｜
     *         DhExpandFailed——DH 权威展开失败｜SchemaVersionUnsupported——
     *         对象 schema 主版本超出支持；编解码面 MalformedPayload 按其
     *         生产者接口（§9.4.9）原样透传）
     *
     * 纯函数；线程安全；确定性（NFR-COR-02——同闭包重复调用逐字段一致）。
     */
    virtual runtime::Expected<runtime::RobotDesignDescription, ModelingError>
        build(const ObjectClosureView& closure,
              std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief ICanonicalModelInputBuilder 无状态产品实现（§3.4 总约定 1——
 *        Codec/TemplateFactory/DhExplicitConverter 同款"接口＋final 实现"
 *        形态；可默认构造，随处持有，拷贝/移动平凡）。
 *
 * 内部持 DhExplicitConverter 作为 DH 展开的唯一实现（§7.6"同一展开路径
 * ＝语义单一源，NFR-MNT-04"——与 T09 等价验证共用，不写第二套展开）。
 */
class CanonicalModelInputBuilder final : public ICanonicalModelInputBuilder {
public:
    runtime::Expected<runtime::RobotDesignDescription, ModelingError>
        build(const ObjectClosureView& closure,
              std::vector<core::DiagnosticRecord>& diags) const override;
};

// =====================================================================
// RobotDesignReader——runtime::IRobotDesignReader 的 modeling 产品实现
// （§9.4.5 原文契约；L5 装配注入 runtime 十段链 S2——§9.2 决议落地）
// =====================================================================

/**
 * @brief runtime 侧 reader 实现（modeling 提供、L5 注入；闭包域字节源装配
 *        期绑定，§9.2/§9.4.5 原文契约）。
 *
 * read(objectBytes, formatVersion) 的输入是**根对象**字节（runtime S2 以
 * kRobotDesignObjectType 路由取出后传入——§5.2 S2）；部件对象（tools/
 * scene/drivetrain/固化资源）由构造期绑定的闭包域字节源解析。内部委托
 * CanonicalModelInputBuilder（唯一映射实现，NFR-MNT-04）。
 *
 * 生命周期/所有权：闭包视图指针非 owning——由 L5/调用方持有并保证全部
 * read() 调用期间存活（CompileRequest 注入指针同款约定）。
 * 线程安全：const read() 并发安全（runtime §5.5"注入接口并发只读安全"）；
 * 闭包视图实现方须自证并发只读安全（见 ObjectClosureView 类注）。
 * 确定性：纯函数——同字节＋同闭包→同 Description（ARC-03 的建模侧保证）。
 */
class RobotDesignReader final : public runtime::IRobotDesignReader {
public:
    /**
     * @brief 以闭包域字节源构造（装配期绑定——§9.2"同源绑定"落点）。
     *
     * @param closure [in] 闭包域字节源（非 owning——调用方持有，见类注）；
     *                不得为空指针：没有闭包域的 reader 无法解引用部件对象
     *                （§9.2 注入形态的构成性前提），空指针＝装配编程错误，
     *                按调用方契约违约 fail-fast（AGENTS 错误语义）
     * @throws std::invalid_argument closure 为空指针
     */
    explicit RobotDesignReader(const ObjectClosureView* closure);

    /**
     * @brief 解析根对象字节为中性描述值（§9.4.5——内部委托 builder；部件
     *        对象经构造期绑定的闭包域字节源解析）。
     *
     * @param objectBytes             [in] robot-design 根对象 canonical
     *                                字节（只读；非拥有——函数期间有效）
     * @param objectTypeFormatVersion [in] 对象类型格式版本（无单位；当前
     *                                唯一支持值＝kRobotDesignSchemaVersion
     *                                ＝1——runtime S2 固定传 1 的实现层
     *                                登记；不识别的版本拒绝而非尽力猜测，
     *                                NFR-DEP-04）
     * @return ok＝Description（值语义，调用方持有）；err＝RuntimeError
     *         （码恒 InputInvalid——§5.2 S2 归属表"reader 失败→InputInvalid
     *         含定位"；modeling 侧错误 token＋定位参数全量并入 detail，
     *         runtime 侧不二次猜测）；不抛越过单元边界的异常（§9.4 公共
     *         契约前提——调用方装配违约在构造期 fail-fast，见构造函数）
     *
     * 纯函数；线程安全；确定性。
     */
    runtime::Expected<runtime::RobotDesignDescription, runtime::RuntimeError>
        read(const std::vector<std::uint8_t>& objectBytes,
             std::uint32_t objectTypeFormatVersion) const override;

private:
    const ObjectClosureView* m_closure;  ///< 闭包域字节源（非 owning——构造期绑定）
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_CANONICALBRIDGE_HPP
