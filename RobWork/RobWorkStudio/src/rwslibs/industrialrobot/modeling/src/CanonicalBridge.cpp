/**
 * @file   CanonicalBridge.cpp
 * @brief  CanonicalBridge 实现——§9.1 字段映射表（RobotDesign/部件对象→
 *         runtime::RobotDesignDescription 十类来源映射）的唯一产品实现：
 *         builder 内核（闭包→Description）＋reader（根字节→委托内核）。
 *
 * 设计依据：见 CanonicalBridge.hpp 文件头（本实现文件只登记实现层细化，
 * 契约语义以头文件与单元卡为准）。
 *
 * ★ 实现层细化登记（单元卡 §15 增量同步面——DTB §5.4）：
 *   ①robotLocalName 来源：§9.1 行"根对象模型 localName"以根对象
 *     displayName（模型名承载字段——T05 导入 §6.3"displayName＋根
 *     localName 候选"、T07 createDraft 基名、T09 等价验证链级构造三条
 *     在案实践同源）实现；空串或含 '/'（runtime §4.3.3 robotLocalName
 *     硬规则）→IllegalName 拒绝——V-26 建模侧输入质量面（不伪造名称、
 *     不假设 runtime 修复）；其余字符原样透传（消歧/合法化归 runtime，R-4）。
 *   ②零位折叠：与 T09 等价验证 buildChainDescription 同款纪律（§15 v0.10
 *     ③登记）——origin_desc = origin·R(axis, zeroOffset)、bounds/
 *     workingRange 平移 −zeroOffset（runtime S5 恒置 CanonicalJoint.
 *     zeroOffset=0——显式表示已折叠进限位/原点，runtime.md §15.4 v0.12）。
 *   ③Fixed 关节 axis 中性填充：Fixed 无可动轴（I-MDL-6 不适用）而
 *     runtime CanonicalJoint.axis 结构必备（零向量→InputInvalid）——
 *     以 +Z 单位向量＋DerivedReadOnly 来源（methodTag "fixed-axis-neutral"）
 *     确定性填充（轴对 Fixed 关节无运动语义，仅满足结构承载）。
 *   ④Recorded 资源声明键：modeling v1 清单条目无 ObjectId（Recorded 只在
 *     外部引用记录层，非对象——§9.6），而 runtime ResourceRef.resourceId
 *     为 ObjectId——以（域串, resourceId）的确定性散列派生 128 位声明键
 *     （与 Import.cpp deriveTempObjectId 同族算法、不经随机源；io 适配器
 *     按此键回查外部引用记录——io.md §8.5 Recorded 分支），路径不入
 *     Description（sourcePathHint 恒空——路径不作身份，§9.1"路径不入身份"）。
 *   ⑤tools[] 次序与 TCP 选取：defaultTcp 引用工具居首（runtime"首项为
 *     默认 TCP"，MDL-13/04/KIN-14），其余按 toolRefs 序（canonical 字典序
 *     ——§4.8 规范化）；tcpOffset ＝ mountInterface ∘ 所选 TCP 条目 offset
 *     （默认工具取 defaultTcp.tcpKey 指向条目，其余工具取 tcpList 首条——
 *     确定性选取，非默认工具的 TCP 选择语义登记为卡面澄清）。
 */

#include <sdurws/ird/modeling/CanonicalBridge.hpp>  // 公共路径自含（Codec.cpp 同款——公共头唯一暴露位）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include <sdurws/ird/core/Units.hpp>          // tryConvert/UnitToken——SI 化唯一换算入口（SA-12）
#include <sdurws/ird/io/IoFwd.hpp>            // io::kAccessVersion——读取契约版本单点（P-RT-6 裁决值）
#include <sdurws/ird/modeling/Codec.hpp>      // RobotDesignCodec——对象字节解码唯一闸口
#include <sdurws/ird/modeling/DhConvert.hpp>  // DhExplicitConverter——DH 展开唯一实现（语义单一源）
#include <sdurws/ird/modeling/ObjectTypes.hpp>  // 五对象 token（robot-design 复用 runtime 字面）
#include <sdurws/ird/modeling/Parts.hpp>      // ToolDefinition/SceneObject/DrivetrainDesign 值模型
#include <sdurws/ird/modeling/RobotDesign.hpp>  // RobotDesign/JointEntry/LinkEntry 值模型

namespace sdurws::ird::modeling {

namespace {

// =====================================================================
// 本地数值辅助（与 DhConvert.cpp 同款纪律：rw 聚合运算符内经 multiply()
// 外联符号——冒烟模式不可链接，矩阵合成一律逐元素算术，两模式数学一致）
// =====================================================================

/// R·R（逐元素三重循环——DhConvert.cpp rotMul 同式，冒烟纪律）。
rw::math::Rotation3D<double> rotMul(const rw::math::Rotation3D<double>& ra,
                                    const rw::math::Rotation3D<double>& rb)
{
    rw::math::Rotation3D<double> out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) { s += ra(i, k) * rb(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

/// R·v（旋转乘向量——逐元素三循环；R·v 运算符同为外联符号面，冒烟纪律，
/// DhConvert.cpp rotVec 同式）。
rw::math::Vector3D<double> rotVec(const rw::math::Rotation3D<double>& r,
                                 const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v[0] + r(0, 1) * v[1] + r(0, 2) * v[2],
        r(1, 0) * v[0] + r(1, 1) * v[1] + r(1, 2) * v[2],
        r(2, 0) * v[0] + r(2, 1) * v[1] + r(2, 2) * v[2]);
}

/// Rodrigues 轴角旋转（axis 须为单位向量、angle 单位 rad）——零位折叠的
/// R(axis, zeroOffset) 因子。逐元素公式 R = I + sinθ·[k]× ＋ (1−cosθ)·[k]×²
/// （与 DhConvert.cpp axisAngleRotation 同式——两侧同规是等价性的前提）。
rw::math::Rotation3D<double> axisAngleRotation(const rw::math::Vector3D<double>& axis,
                                               double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double kx = axis[0];
    const double ky = axis[1];
    const double kz = axis[2];
    return rw::math::Rotation3D<double>(
        c + t * kx * kx,      t * kx * ky - s * kz, t * kx * kz + s * ky,
        t * kx * ky + s * kz, c + t * ky * ky,      t * ky * kz - s * kx,
        t * kx * kz - s * ky, t * ky * kz + s * kx, c + t * kz * kz);
}

// =====================================================================
// 错误工厂（ModelingError 值面——builder 全部失败经此产出；产诊断记录
// 唯一经 IDiagnosticFactory，本单元不持工厂注入，diags 参数为契约预留位）
// =====================================================================

/// 以码＋参数表构造 ModelingError（params 保序——NFR-COR-02 确定性）。
ModelingError bridgeError(ModelingErrorCode code,
                          std::vector<std::pair<std::string, std::string>> params,
                          std::string detail)
{
    ModelingError e;
    e.code = code;
    e.params = std::move(params);
    e.detail = std::move(detail);
    return e;
}

/// RefMissing＋对象定位（闭包缺对象/token 不匹配/引用不完整——§9.4.5
/// @错误 行首分支；runtime 侧按 InputInvalid 预演转译）。
ModelingError refMissing(std::string objectIdText, std::string field,
                         std::string detail)
{
    std::vector<std::pair<std::string, std::string>> params;
    params.emplace_back("object-id", std::move(objectIdText));
    if (!field.empty()) { params.emplace_back("field", std::move(field)); }
    return bridgeError(ModelingErrorCode::RefMissing, std::move(params),
                       std::move(detail));
}

/// UnitIllegal＋字段定位（Invalid 态字段/SI 真值门拒绝——I-MDL-3 履行点）。
ModelingError unitIllegal(std::string field, std::string detail)
{
    std::vector<std::pair<std::string, std::string>> params;
    params.emplace_back("field", std::move(field));
    return bridgeError(ModelingErrorCode::UnitIllegal, std::move(params),
                       std::move(detail));
}

/// IllegalName＋名称定位（V-26 建模侧输入质量面——解析归 runtime，R-4）。
ModelingError illegalName(std::string field, std::string detail)
{
    std::vector<std::pair<std::string, std::string>> params;
    params.emplace_back("field", std::move(field));
    return bridgeError(ModelingErrorCode::IllegalName, std::move(params),
                       std::move(detail));
}

// =====================================================================
// 输入质量面断言（V-26——AT-18 联合观测的建模侧输入面；只断言不加工：
// 名称一律原样透传，合法化/消歧/前缀生成唯一归 runtime，R-4 红线）
// =====================================================================

/// localName 合法字符集（§4.3-A 行原文 [A-Za-z0-9_.-]；非空——导入侧
/// sanitizeLocalName 同一词表，模板 createDraft 步③同判）。
bool isLegalLocalName(std::string_view name)
{
    if (name.empty()) { return false; }
    for (const char ch : name) {
        const bool legal = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-';
        if (!legal) { return false; }
    }
    return true;
}

/// robotLocalName 输入质量面（runtime §4.3.3 硬规则的最小建模侧预演：
/// 非空且不含 '/'；其余字符集不作建模侧拒绝——runtime 设备名合法化为
/// 消歧规则兜底面，R-4）。
bool isLegalRobotLocalName(std::string_view name)
{
    if (name.empty()) { return false; }
    return name.find('/') == std::string_view::npos;
}

// =====================================================================
// SI 真值门（§3.4 总约定 4/runtime §4.4 单位纪律的 modeling 侧履行点）
// =====================================================================

/// 量纲对应的 SI 单位 token（core Units 冻结表内字面——R1 单位表 SI 列）。
std::optional<core::UnitToken> siUnitToken(core::QuantityKind kind)
{
    switch (kind) {
        case core::QuantityKind::Length:              return core::UnitToken::find("m");
        case core::QuantityKind::Angle:               return core::UnitToken::find("rad");
        case core::QuantityKind::Mass:                return core::UnitToken::find("kg");
        case core::QuantityKind::Inertia:             return core::UnitToken::find("kg*m^2");
        case core::QuantityKind::AngularVelocity:     return core::UnitToken::find("rad/s");
        case core::QuantityKind::AngularAcceleration: return core::UnitToken::find("rad/s^2");
        case core::QuantityKind::Dimensionless:       return core::UnitToken::find("1");
        default: return std::nullopt;  // 摩擦系数族（N·m·s/rad）等无 R1 量纲行——走有限性门
    }
}

/**
 * @brief SI 真值门：值经 core Units 唯一换算入口的同量纲恒等换算
 *        （tryConvert(v, si, si)——建模对象本就以 SI 存储，此处履行的是
 *        "一切数值必须过换算入口"的纪律：入口对非有限值拒绝（NaN/±Inf
 *        →nullopt），保证只有 SI 真值可进入 Description）。
 *
 * @param v    [in] 待门控数值（建模对象存储值——SI 或待拒绝）
 * @param kind [in] 量纲（决定 SI 单位 token；无 R1 量纲行的量纲返回
 *             nullopt——调用方须改走有限性门并注明）
 * @return 门控后的 SI 真值（数值不变——同量纲恒等）；非有限/量纲无行→
 *         nullopt（调用方→UnitIllegal）
 *
 * 纯函数；确定性（tryConvert 各一次乘除——core §4.4）。
 */
std::optional<double> siGate(double v, core::QuantityKind kind)
{
    const auto unit = siUnitToken(kind);
    if (!unit.has_value()) { return std::nullopt; }
    return core::tryConvert(v, *unit, *unit);
}

/// 无 R1 量纲行的标量门（摩擦系数族 N·m·s/rad、N·m、N——core Units 冻结表
/// 无此量纲行）：有限性断言履行 SI 真值语义（词表扩展前不私设换算——SA-12
/// 单一入口纪律；量纲行扩展属 core 侧演进）。
std::optional<double> finiteGate(double v)
{
    if (!std::isfinite(v)) { return std::nullopt; }
    return v;
}

// =====================================================================
// Recorded 资源声明键（实现层细化④——确定性散列派生，不经随机源）
// =====================================================================

/**
 * @brief 由（域串, 清单 resourceId）派生 Recorded 资源的确定性声明 ObjectId。
 *
 * 为什么需要：modeling v1 清单条目的 Recorded 态无 ObjectId（§9.6——
 * Recorded 只在外部引用记录层，非对象），而 runtime ResourceRef.resourceId
 * 为 ObjectId 必备。本键是 reader 侧的**声明键**（不是 project 对象身份，
 * 不参与对象库编址）——io 侧 IRuntimeResourceProvider 适配器（io.md §8.5
 * Recorded 分支"读外部引用记录路径"）按 Description 声明的键回查。
 *
 * 算法：与 Import.cpp deriveTempObjectId 同族——两个独立偏移基的 FNV-1a
 * 64 位实例拼合 128 位（core §4.1 std::hash 规定 FNV-1a 128 同族；独立
 * 实现以避免消费 core 私有 detail——R-2；仅散列用途、非密码学承诺）。
 * 域串入散列输入防与其他派生面（导入临时句柄）碰撞；分隔字节防串接歧义。
 * 同输入同键、不经随机源（NFR-COR-02——确定性是 ARC-03 的前提）。
 *
 * @param manifestResourceId [in] 清单条目 resourceId（模型内作用域编址串）
 * @return 非全零确定性声明键（全零碰撞防御性置末字节——保留值纪律）
 *
 * 纯函数；线程安全；确定性。
 */
core::ObjectId deriveRecordedResourceObjectId(std::string_view manifestResourceId)
{
    std::uint64_t hi = 0xcbf29ce484222325ull;  // FNV-1a 64 位偏移基
    std::uint64_t lo = 0x84222325cbf29ce4ull;  // 反转字序第二偏移基（独立序列）
    const auto absorb = [&hi, &lo](unsigned char byte) {
        hi ^= byte;
        hi *= 0x100000001b3ull;  // FNV-1a 64 位素数
        lo ^= byte;
        lo *= 0x100000001b3ull;
    };
    // 域串（"recorded-resource"）——与导入临时句柄（kind=joint/link 等）
    // 的散列输入空间分离，两类派生键不可能同值。
    for (const char ch : std::string_view("recorded-resource")) {
        absorb(static_cast<unsigned char>(ch));
    }
    absorb(0);  // 域/键分隔
    for (const char ch : manifestResourceId) { absorb(static_cast<unsigned char>(ch)); }

    core::ObjectId id;
    for (int i = 0; i < 8; ++i) {
        id.bytes[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((hi >> (8 * i)) & 0xFFu);
        id.bytes[static_cast<std::size_t>(i) + 8] =
            static_cast<std::uint8_t>((lo >> (8 * i)) & 0xFFu);
    }
    bool allZero = true;
    for (const std::uint8_t b : id.bytes) {
        if (b != 0) { allZero = false; break; }
    }
    if (allZero) { id.bytes[15] = 1; }  // 保留值兜底（全零＝空非法）
    return id;
}

// =====================================================================
// 闭包解引用辅助（解码＋token 复核——RefMissing 的两大触发面）
// =====================================================================

/**
 * @brief 从闭包解引用一个部件对象并解码为指定变体类型。
 *
 * 步骤（§9.4.5 @错误 行的落点）：
 *   ① tryObject(oid) 未命中 → RefMissing（闭包缺对象——O-36 裁决口径：
 *      复核范围＝真实存储对象）；
 *   ② token 与期望不符 → RefMissing（token 不匹配——路由失联防混入）；
 *   ③ decode（kCurrentFormatVersion）→ MalformedPayload/SchemaVersion-
 *      Unsupported 原样透传（各为其生产者接口 §9.4.9 的已登记错误面），
 *      并补 object-id 参数定位来源对象；
 *   ④ 变体持类型与期望不符 → RefMissing（token 声称与字节内容不一致）。
 *
 * @tparam T 期望解出的值类型（ToolDefinition/SceneObject/DrivetrainDesign）
 * @param closure     [in] 闭包域字节源
 * @param oid         [in] 部件对象身份
 * @param expectToken [in] 期望的对象类型 token
 * @param field       [in] 定位字段（错误 params 的 field 值——引用来源）
 * @return ok＝解码后的部件对象；err＝ModelingError（定位到对象 ObjectId）
 *
 * 纯函数；确定性。
 */
template <class T>
runtime::Expected<T, ModelingError>
    resolvePart(const ObjectClosureView& closure, const core::ObjectId& oid,
                std::string_view expectToken, std::string_view field)
{
    // ① 闭包存在性（O-36：只对真实存储对象复核——关节/连杆子 ObjectId
    // 不经本函数，它们内嵌根对象字节）。
    const std::optional<ClosureObject> found = closure.tryObject(oid);
    if (!found.has_value()) {
        return runtime::Expected<T, ModelingError>::err(refMissing(
            oid.toCanonical(), std::string(field),
            "引用对象不在闭包（悬空引用——§9.4.5 RefMissing；"
            "runtime 侧按 InputInvalid 预演转译）"));
    }
    // ② token 复核（存储登记类型与引用期望一致——拼写漂移/路由失联面）。
    if (found->objectTypeToken != expectToken) {
        return runtime::Expected<T, ModelingError>::err(refMissing(
            oid.toCanonical(), std::string(field),
            "引用对象 token 不匹配（期望 " + std::string(expectToken)
                + "，实得 " + found->objectTypeToken + "）"));
    }
    // ③ 解码（对象字节进入类型化世界的唯一 model 侧闸口——Codec.decode
    // 校验链：版本/结构/规范化/不变量全量复核）。
    RobotDesignCodec codec;
    const runtime::Expected<ObjectVariant, ModelingError> decoded =
        codec.decode(found->bytes, kCurrentFormatVersion);
    if (!decoded.ok()) {
        ModelingError e = decoded.error();
        // 补对象定位参数（decode 面不知来源对象——桥接面补齐定位链）。
        e.params.emplace_back("object-id", oid.toCanonical());
        return runtime::Expected<T, ModelingError>::err(std::move(e));
    }
    // ④ 变体持类型复核（token 声称与字节内容一致——防错位字节）。
    if (!std::holds_alternative<T>(decoded.get())) {
        return runtime::Expected<T, ModelingError>::err(refMissing(
            oid.toCanonical(), std::string(field),
            "对象字节解码变体与 token 不符（期望 " + std::string(expectToken)
                + "）"));
    }
    return runtime::Expected<T, ModelingError>::ok(std::get<T>(decoded.get()));
}

// =====================================================================
// 单关节映射（§9.1 joints 行——十类映射的最重一行）
// =====================================================================

/// modeling::JointType → runtime::JointType（同词表四值——两枚举独立定义
/// 于各自单元，值序一致；switch 全枚举无 default，DhConvert.cpp 同款）。
runtime::JointType toRuntimeJointType(JointType type)
{
    switch (type) {
        case JointType::Revolute: return runtime::JointType::Revolute;
        case JointType::Continuous: return runtime::JointType::Continuous;
        case JointType::Prismatic: return runtime::JointType::Prismatic;
        case JointType::Fixed: return runtime::JointType::Fixed;
    }
    return runtime::JointType::Revolute;  // 不可达（防御——保持确定性）
}

/**
 * @brief 单关节显式表示 → runtime::JointDescription（§9.1 joints 行；
 *        DH 权威已由调用方先经 §7.4 展开为显式表示——同一展开实现）。
 *
 * 映射与折叠（实现层细化②）：
 *   a) objectId/localName/type 原样透传（身份与名称不改写——R-4）；
 *   b) 零位折叠：origin_desc = origin·R(axis, zeroOffset)（权威零位旋转入
 *      几何——runtime S5 恒置 zeroOffset=0）；bounds/workingRange 平移
 *      −zeroOffset（权威 q→RobWork q，q_authoritative = q_zeroOffset + q_rw）；
 *   c) lower/upper：Revolute/Prismatic 由 bounds 折叠映射（rad/m）；Continuous
 *      必为 NotProvided（§4.2——工作范围另载）；Fixed 无限位→NotProvided；
 *   d) workingRange：仅 Continuous 映射（MDL-12 分析消费属性）；
 *   e) maxVelocity/maxAcceleration：建模 schema 无落点（§4.3-A 未登记——
 *      v0.6 ②a"不发明字段"口径）→NotProvided（能力缺失降级非失败）；
 *   f) axis：Explicit 权威透传；Fixed 无可动轴且缺失→+Z 中性填充（实现层
 *      细化③）；可动关节缺失→RefMissing（必备输入缺失，不伪造）。
 *
 * @param src   [in] 显式表示关节（Explicit 原值或 DH 展开产物）
 * @param index [in] 链序下标（0 基——错误定位）
 * @return ok＝映射后的 JointDescription；err＝ModelingError（定位到关节）
 *
 * 纯函数；确定性。
 */
runtime::Expected<runtime::JointDescription, ModelingError>
    mapJoint(const JointEntry& src, std::size_t index)
{
    const std::string field = "joints[" + std::to_string(index) + "]";

    // ---- 输入质量面：localName 字符集（V-26 建模侧；名称原样透传——R-4）。
    if (!isLegalLocalName(src.localName)) {
        return runtime::Expected<runtime::JointDescription, ModelingError>::err(
            illegalName(field + ".localName",
                        "关节 localName 为空或含合法字符集 [A-Za-z0-9_.-] 之外的字符"
                        "（建模侧输入面断言——合法化/消歧归 runtime，R-4）"));
    }

    runtime::JointDescription jd;
    jd.objectId = src.objectId;  // 模型内标识透传（O-36——S5 唯一性复核输入面）
    jd.localName = src.localName;
    jd.type = toRuntimeJointType(src.type);

    // ---- axis：可动关节必备（Description 无缺失承载）；Fixed 缺失→+Z 中性。
    const bool movable = src.type != JointType::Fixed;
    if (const auto axis = src.axis.tryValue(); axis.has_value()) {
        jd.axis = *axis;  // 单位向量（I-MDL-6 可归一化保证——编译器规格化）
    } else if (!movable) {
        // Fixed 关节无运动轴语义，仅满足 runtime 结构承载（实现层细化③）。
        jd.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    } else {
        // 可动关节权威轴缺失＝必备输入缺失（Explicit 权威态 axis 是权威
        // 一等字段——缺失不可映射，不伪造、不假设 runtime 修复）。
        return runtime::Expected<runtime::JointDescription, ModelingError>::err(
            refMissing(src.objectId.toCanonical(), field + ".axis",
                       "可动关节权威轴（axis）缺失（Explicit 权威态必备输入——"
                       "Description 无缺失承载，拒绝映射）"));
    }

    // ---- origin：必备输入（全关节类型皆需 T_parent_joint——链几何结构）。
    if (const auto origin = src.origin.tryValue(); origin.has_value()) {
        // a) 零位折叠：origin_desc = origin · R(axis, zeroOffset)（旋转在
        // 关节局部轴上——与 runtime jointStaticTransform 的
        // T_parent_joint·R_axis(zeroOffset)·R_align 同侧同序，T09 同款）。
        const rw::math::Transform3D<double> originModel =
            static_cast<rw::math::Transform3D<double>>(*origin);
        const rw::math::Rotation3D<double> folded =
            rotMul(originModel.R(), axisAngleRotation(jd.axis, src.zeroOffset));
        jd.origin = rw::math::Transform3D<double>(originModel.P(), folded);
    } else {
        return runtime::Expected<runtime::JointDescription, ModelingError>::err(
            refMissing(src.objectId.toCanonical(), field + ".origin",
                       "关节原点位姿缺失（T_parent_joint 为链几何必备输入——"
                       "拒绝映射，不伪造）"));
    }

    // ---- Invalid 态前置拒绝（单位/量纲不合法的建模侧履行点——已提供但
    // 非法的值无 SI 真值可映射；置于限位映射之前，保证定位语义精确）。
    if (src.bounds.state() == core::FieldState::Invalid) {
        return runtime::Expected<runtime::JointDescription, ModelingError>::err(
            unitIllegal(field + ".bounds",
                        "限位为 Invalid 态（已提供但非法——无 SI 真值可映射，"
                        "NFR-COR-03 不静默丢弃；修正或清空为缺失后重编译）"));
    }
    if (src.workingRange.state() == core::FieldState::Invalid) {
        return runtime::Expected<runtime::JointDescription, ModelingError>::err(
            unitIllegal(field + ".workingRange",
                        "工作范围为 Invalid 态（无 SI 真值可映射）"));
    }

    // ---- 限位/工作范围（SI 真值门逐值过换算入口；原 provenance 透传——
    // 折叠是表示变换，不改变值的来源事实）。
    const auto mapBounds = [&](JointType t) -> std::optional<ModelingError> {
        if (t != JointType::Revolute && t != JointType::Prismatic) {
            // Continuous＝NotProvided（§4.2——工作范围另载）；Fixed 无限位。
            return std::nullopt;
        }
        const auto bounds = src.bounds.tryValue();
        if (!bounds.has_value()) {
            // Revolute/Prismatic 限位缺失：I-MDL-4 要求必填（解码门已拒）——
            // 此处为防御面（直接构造的值模型绕过编辑边界时）。
            return unitIllegal(field + ".bounds",
                               "有限限位可动关节限位缺失（I-MDL-4 必填）");
        }
        // 量纲随关节类型：Revolute rad／Prismatic m（zeroOffset 同量纲）。
        const core::QuantityKind kind = (t == JointType::Revolute)
            ? core::QuantityKind::Angle : core::QuantityKind::Length;
        const auto lo = siGate(bounds->first - src.zeroOffset, kind);
        const auto hi = siGate(bounds->second - src.zeroOffset, kind);
        if (!lo.has_value() || !hi.has_value()) {
            return unitIllegal(field + ".bounds",
                               "限位值未通过 SI 真值门（非有限值拒绝——NFR-COR-03）");
        }
        jd.lower = core::SourcedValue<double>::provided(*lo, src.bounds.provenance());
        jd.upper = core::SourcedValue<double>::provided(*hi, src.bounds.provenance());
        return std::nullopt;
    };
    if (auto err = mapBounds(src.type); err.has_value()) {
        return runtime::Expected<runtime::JointDescription, ModelingError>::err(
            std::move(*err));
    }

    // Continuous 的工程工作范围同规折叠平移（MDL-12 分析消费属性）。
    if (src.type == JointType::Continuous) {
        if (const auto range = src.workingRange.tryValue(); range.has_value()) {
            const auto lo = siGate(range->first - src.zeroOffset,
                                   core::QuantityKind::Angle);
            const auto hi = siGate(range->second - src.zeroOffset,
                                   core::QuantityKind::Angle);
            if (!lo.has_value() || !hi.has_value()) {
                return runtime::Expected<runtime::JointDescription, ModelingError>::err(
                    unitIllegal(field + ".workingRange",
                                "工作范围未通过 SI 真值门（非有限值拒绝）"));
            }
            jd.workingRange = runtime::WorkingRange{*lo, *hi};
        }
    }

    // ---- maxVelocity/maxAcceleration：建模 schema 无落点（§4.3-A 未登记——
    // 不发明字段）→NotProvided（runtime 能力缺失降级路径，非失败）。
    jd.maxVelocity = core::SourcedValue<double>::notProvided();
    jd.maxAcceleration = core::SourcedValue<double>::notProvided();
    return runtime::Expected<runtime::JointDescription, ModelingError>::ok(
        std::move(jd));
}

// =====================================================================
// 几何引用映射（§9.1 links/tools/scene 行的 resourceRefId 解引用半段）
// =====================================================================

/**
 * @brief modeling GeometryRef（resourceRefId 指向根 resourceManifest）→
 *        runtime GeometryRef（内容寻址 ResourceRef）。
 *
 * 映射（§9.1 资源行——路径不入身份）：
 *   - resourceRefId 在清单中找不到 → RefMissing（悬空资源引用，定位引用方）；
 *   - runtime ResourceRef.resourceId/contentDigest/state 按清单条目映射
 *     （Solidified→solidifiedObject.objectId；Recorded→确定性声明键，
 *     实现层细化④）；
 *   - sourcePathHint 恒空：Description 字节进 builtFrom 摘要——路径入
 *     Description 即入身份（§9.1"路径不入身份"的桥接侧履行）；
 *   - accessVersion 取 io::kAccessVersion 单点（P-RT-6 裁决值，不私写 1）。
 *
 * @param geometryRef [in] modeling 几何引用（resourceRefId＋局部位姿——
 *                    局部位姿不入 runtime GeometryRef：runtime 几何引用以
 *                    资源为粒度，位姿语义由几何资源自身承载/编译链处理）
 * @param design      [in] 根对象（resourceManifest 查找域）
 * @param field       [in] 定位字段（引用方——错误 params）
 * @return ok＝runtime 几何引用；err＝ModelingError
 *
 * 纯函数；确定性。
 */
runtime::Expected<runtime::GeometryRef, ModelingError>
    mapGeometryRef(const GeometryRef& geometryRef, const RobotDesign& design,
                   std::string_view field)
{
    // 清单解引用（resourceId 是模型内作用域编址——I-MDL-10 状态机载体）。
    const ResourceRef* entry = nullptr;
    for (const ResourceRef& candidate : design.resourceManifest) {
        if (candidate.resourceId == geometryRef.resourceRefId) {
            entry = &candidate;
            break;  // 清单键唯一（Codec 规范化＋I-MDL 复核）——首中即唯一
        }
    }
    if (entry == nullptr) {
        return runtime::Expected<runtime::GeometryRef, ModelingError>::err(
            refMissing(std::string(), std::string(field),
                       "几何引用的资源清单条目不存在（resourceRefId="
                           + geometryRef.resourceRefId + "——悬空资源引用）"));
    }

    runtime::GeometryRef out;
    // 逐态映射（§9.1 资源行：Solidified→项目对象字节键；Recorded→声明键）。
    if (entry->state == ResourceState::Solidified) {
        // Solidified 必带固化引用（I-MDL-10——解码门已保证，防御断言）。
        if (!entry->solidifiedObject.has_value()) {
            return runtime::Expected<runtime::GeometryRef, ModelingError>::err(
                refMissing(std::string(), std::string(field),
                           "Solidified 资源缺固化引用（I-MDL-10 违例态）"));
        }
        out.resource.resourceId = entry->solidifiedObject->objectId;
        out.resource.state = runtime::ResourceState::Solidified;
    } else {
        // Recorded：非对象——确定性声明键（实现层细化④；io 适配器按此键
        // 回查外部引用记录，每次编译重读＋digest 复核——runtime §9.1）。
        out.resource.resourceId =
            deriveRecordedResourceObjectId(entry->resourceId);
        out.resource.state = runtime::ResourceState::Recorded;
    }
    out.resource.contentDigest = entry->contentDigest;  // 内容摘要透传（CON-05——不自算）
    out.resource.sourcePathHint = std::nullopt;         // 路径不入 Description＝不入身份
    out.resource.accessVersion = io::kAccessVersion;    // 读取契约版本单点（P-RT-6）
    return runtime::Expected<runtime::GeometryRef, ModelingError>::ok(out);
}

/**
 * @brief BodyData 物性组 → runtime 物性三元映射（连杆/工具共用——§4.4
 *        "断言①②③同连杆"的映射侧同构）。
 *
 * mass（kg）/centerOfMass（m，连杆系）/inertia（kg·m²，质心系）逐值过
 * SI 真值门；原 provenance 透传（缺失＝NotProvided 原样——DataInsufficient
 * 降级语义不失真）；Invalid 态→UnitIllegal（无 SI 真值可映射）。
 * 惯量：modeling 六分量（质心系、连杆系参考姿态——M-2）→3×3 对称阵
 * （ixy/ixz/iyz 对称位展开——表示变换，不改变张量）。
 *
 * @param body  [in] modeling 物性组
 * @param field [in] 定位字段前缀（如 "links[1].body"）
 * @param out   [out] runtime 侧物性三元（引用传入就地填充）
 * @return nullopt＝成功；非空＝ModelingError（定位到字段）
 */
std::optional<ModelingError> mapBodyData(const BodyData& body,
                                         std::string_view field,
                                         core::SourcedValue<double>& outMass,
                                         core::SourcedValue<rw::math::Vector3D<double>>& outCom,
                                         core::SourcedValue<rw::math::InertiaMatrix<double>>& outInertia,
                                         std::optional<std::string>& outMaterial)
{
    // ---- Invalid 态前置拒绝（三件套任一非法即整体拒绝——无半成品）。
    if (body.mass.state() == core::FieldState::Invalid) {
        return unitIllegal(std::string(field) + ".mass",
                           "质量为 Invalid 态（无 SI 真值可映射）");
    }
    if (body.centerOfMass.state() == core::FieldState::Invalid) {
        return unitIllegal(std::string(field) + ".centerOfMass",
                           "质心为 Invalid 态（无 SI 真值可映射）");
    }
    if (body.inertia.state() == core::FieldState::Invalid) {
        return unitIllegal(std::string(field) + ".inertia",
                           "惯量为 Invalid 态（无 SI 真值可映射）");
    }

    // ---- 质量（kg；Provided 时有限——解码门 I-MDL-3/5 已保证，SI 门防御）。
    if (const auto mass = body.mass.tryValue(); mass.has_value()) {
        const auto gated = siGate(*mass, core::QuantityKind::Mass);
        if (!gated.has_value()) {
            return unitIllegal(std::string(field) + ".mass",
                               "质量未通过 SI 真值门（非有限值拒绝）");
        }
        outMass = core::SourcedValue<double>::provided(*gated, body.mass.provenance());
    }

    // ---- 质心（m，连杆系下表示——M-2 惯量基准；逐分量过门）。
    if (const auto com = body.centerOfMass.tryValue(); com.has_value()) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (!siGate((*com)[i], core::QuantityKind::Length).has_value()) {
                return unitIllegal(std::string(field) + ".centerOfMass",
                                   "质心含未通过 SI 真值门的分量（非有限值拒绝）");
            }
        }
        outCom = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            *com, body.centerOfMass.provenance());
    }

    // ---- 惯量（kg·m²，质心系张量——六分量对称展开为 3×3 行主序）。
    if (const auto inertia = body.inertia.tryValue(); inertia.has_value()) {
        const InertiaTensor& t = *inertia;
        for (const double component : {t.ixx, t.iyy, t.izz, t.ixy, t.ixz, t.iyz}) {
            if (!siGate(component, core::QuantityKind::Inertia).has_value()) {
                return unitIllegal(std::string(field) + ".inertia",
                                   "惯量含未通过 SI 真值门的分量（非有限值拒绝）");
            }
        }
        // 六分量 → 对称 3×3（行主序 9 参构造；ixy/ixz/iyz 各展开到对称位——
        // 对称性由表示层结构性满足，I-MDL-5 同口径）。
        outInertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            rw::math::InertiaMatrix<double>(
                t.ixx, t.ixy, t.ixz,
                t.ixy, t.iyy, t.iyz,
                t.ixz, t.iyz, t.izz),
            body.inertia.provenance());
    }

    // ---- 材料（估算来源标识——显示/追溯属性，不入身份，runtime §4.3.6）。
    if (body.material.has_value()) {
        outMaterial = body.material->materialId;
    }
    return std::nullopt;
}

// =====================================================================
// builder 内核（§9.1 字段映射表全量——CanonicalModelInputBuilder 与
// RobotDesignReader 共用的唯一映射实现，NFR-MNT-04）
// =====================================================================

/**
 * @brief StandardDH 权威链→显式关节几何（§7.4 无损展开——buildDescription
 *        的 DH 分支执行体；语义单一源＝注入的 IDhExplicitConverter）。
 *
 * 逐关节组装 DhChain：类型门（标准 DH 只参数化旋转关节——Revolute|
 * Continuous；Prismatic/Fixed 混入＝展开前提不成立）→权威参数门
 * （dhDerived 是 DH 态的编码权威字段，缺失即无可展开参数）→透传
 * （zeroOffset/bounds/workingRange/身份/名称——两态均权威字段，§7.2）。
 *
 * @param root      [in] 已解码根对象（StandardDH 态）
 * @param converter [in] DH 展开器（§7.4 展开唯一实现）
 * @param diags     [out] 诊断输出（透传给转换器签名——本路径两态不产诊断）
 * @return ok＝显式关节几何（链序；axis/origin 已按 §7.4 展开、归一化）；
 *         err＝DhExpandFailed（定位到对象 ObjectId/字段）
 *
 * 纯函数；确定性。
 */
runtime::Expected<std::vector<JointEntry>, ModelingError>
    expandStandardDhGeometry(const RobotDesign& root,
                             const IDhExplicitConverter& converter,
                             std::vector<core::DiagnosticRecord>& diags)
{
    DhChain chain;
    chain.joints.reserve(root.joints.size());
    for (std::size_t i = 0; i < root.joints.size(); ++i) {
        const JointEntry& joint = root.joints[i];
        const std::string field = "joints[" + std::to_string(i) + "]";
        // DH 链只参数化旋转关节（§7.4"基座与工具变换不在 DH 参数内"）；
        // Prismatic/Fixed 混入 DH 权威链＝展开前提不成立——值面拒绝，
        // 不入转换器 fail-fast 轨。
        if (joint.type != JointType::Revolute
            && joint.type != JointType::Continuous) {
            return runtime::Expected<std::vector<JointEntry>, ModelingError>::err(
                bridgeError(
                    ModelingErrorCode::DhExpandFailed,
                    {{"object-id", joint.objectId.toCanonical()},
                     {"field", field}},
                    "DH 权威链含非旋转关节（" + field + "）——标准 DH 只"
                    "参数化旋转关节，展开前提不成立"));
        }
        if (!joint.dhDerived.has_value()) {
            // DH 权威态权威参数缺失＝展开前提不成立（D-MDL-5：字节内应有
            // dhDerived——缺失即无可展开的权威参数，DhExpandFailed 展开族）。
            return runtime::Expected<std::vector<JointEntry>, ModelingError>::err(
                bridgeError(
                    ModelingErrorCode::DhExpandFailed,
                    {{"object-id", joint.objectId.toCanonical()},
                     {"field", field + ".dhDerived"}},
                    "DH 权威态权威参数缺失（dhDerived 是 DH 态的编码权威"
                    "字段——无可展开参数）"));
        }
        DhChainJoint entry;
        entry.dh = *joint.dhDerived;
        entry.zeroOffset = joint.zeroOffset;  // 权威零位透传（§7.4 分离纪律）
        entry.type = joint.type;
        entry.objectId = joint.objectId;
        entry.localName = joint.localName;
        entry.bounds = joint.bounds;        // 两态均权威字段透传（§7.2）
        entry.workingRange = joint.workingRange;
        chain.joints.push_back(std::move(entry));
    }
    // §7.4 无损展开（权威展开，C-5）——空链/基座混入在此不可达（解码门
    // 保证 joints≥1；builder 恒不混入基座变换），错误分支为防御面。
    const ExpandOutcome expanded = converter.dhToExplicit(chain, diags);
    if (!expanded.ok) {
        return runtime::Expected<std::vector<JointEntry>, ModelingError>::err(
            bridgeError(
                ModelingErrorCode::DhExpandFailed,
                {{"detail", std::string(dhErrorCodeToken(expanded.errorCode))}},
                "DH 权威链展开失败（§7.4——消费方不得到达 Description 构造）"));
    }
    return runtime::Expected<std::vector<JointEntry>, ModelingError>::ok(
        expanded.joints);
}

/**
 * @brief 由已解码根对象＋闭包域构造 Description（十类来源映射全集）。
 *
 * @param root      [in] 已解码根对象（RobotDesign——解码门已过 I-MDL 全量）
 * @param closure   [in] 闭包域字节源（部件/固化资源解引用）
 * @param converter [in] DH 展开器（§7.4 展开唯一实现——语义单一源）
 * @param diags     [out] 诊断预留位（本实现两态不产诊断记录——头文件契约）
 * @return ok＝Description；err＝ModelingError（定位到对象/字段）
 *
 * 纯函数；确定性（同输入同输出——ARC-03 的建模侧保证）。
 */
runtime::Expected<runtime::RobotDesignDescription, ModelingError>
    buildDescription(const RobotDesign& root, const ObjectClosureView& closure,
                     const IDhExplicitConverter& converter,
                     std::vector<core::DiagnosticRecord>& diags)
{
    runtime::RobotDesignDescription desc;

    // ---- ①descriptionContractVersion（§9.1 行 1：建模 schemaVersion——
    // 同源演进 R-MDL-3；解码门已保证主版本受支持，此处防御 0 值）。
    if (root.schemaVersion == 0) {
        return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::err(
            bridgeError(
                ModelingErrorCode::SchemaVersionUnsupported,
                {{"object-type", std::string(kRobotDesignObjectType)},
                 {"schema-version", "0"}},
                "根对象 schemaVersion 为 0（非法——契约版本须 ≥1）"));
    }
    desc.descriptionContractVersion = root.schemaVersion;

    // ---- ②robotLocalName（§9.1 行 2：实现层细化①——displayName 承载＋
    // 输入质量面；名称原样透传，合法化归 runtime，R-4）。
    if (!isLegalRobotLocalName(root.displayName)) {
        return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::err(
            illegalName("robotLocalName",
                        "robotLocalName 为空或含 '/'（runtime §4.3.3 硬规则的"
                        "建模侧输入面——进入 RuntimeNameMap 设备作用域的名称"
                        "不可缺失）"));
    }
    desc.robotLocalName = root.displayName;

    // ---- ③joints（§9.1 行 3：Explicit 直映／StandardDH 先经 §7.4 展开为
    // 显式表示——同一展开实现＝语义单一源，§7.6）。
    std::vector<JointEntry> geometry;
    if (root.authority == AuthorityMode::Explicit) {
        geometry = root.joints;  // 显式权威：axis/origin 是权威一等字段，直映
    } else {
        // StandardDH 权威：dhDerived 为权威（D-MDL-5——axis/origin 不在
        // 字节内、解码后为 NotProvided 待重算）。展开失败→错误侧携带
        // DhExpandFailed（定位到对象/字段）。
        auto expanded = expandStandardDhGeometry(root, converter, diags);
        if (!expanded.ok()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(expanded.error());
        }
        geometry = expanded.get();
    }

    // 关节链映射（零位折叠在 mapJoint 内统一执行——两权威模式同规，T09
    // 等价验证同款纪律：两侧折叠同规，零位错位即 FK 超差）。
    desc.joints.reserve(geometry.size());
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        auto mapped = mapJoint(geometry[i], i);
        if (!mapped.ok()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(mapped.error());
        }
        desc.joints.push_back(std::move(mapped.get()));
    }

    // ---- ④links（§9.1 行 4：n+1 含基座连杆——解码门 I-MDL-1 保证数量
    // 关系；物性/几何/材料全量映射）。
    desc.links.reserve(root.links.size());
    for (std::size_t i = 0; i < root.links.size(); ++i) {
        const LinkEntry& link = root.links[i];
        const std::string field = "links[" + std::to_string(i) + "]";
        if (!isLegalLocalName(link.localName)) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(
                illegalName(field + ".localName",
                            "连杆 localName 为空或含合法字符集之外的字符"));
        }
        runtime::LinkDescription ld;
        ld.objectId = link.objectId;
        ld.localName = link.localName;
        // 物性三元（kg/m/kg·m²——质心系，M-2）＋材料（连杆/工具共用实现）。
        if (auto err = mapBodyData(link.body, field + ".body", ld.mass,
                                   ld.centerOfMass, ld.inertia, ld.material);
            err.has_value()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(std::move(*err));
        }
        // visual/collision（§4.3-B 分离字段——经 resourceManifest 解引用；
        // 缺失＝optional 空透传，runtime 可空位）。
        if (link.visual.has_value()) {
            auto mapped = mapGeometryRef(*link.visual, root, field + ".visual");
            if (!mapped.ok()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(mapped.error());
            }
            ld.visual = std::move(mapped.get());
        }
        if (link.collision.has_value()) {
            auto mapped = mapGeometryRef(*link.collision, root, field + ".collision");
            if (!mapped.ok()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(mapped.error());
            }
            ld.collision = std::move(mapped.get());
        }
        desc.links.push_back(std::move(ld));
    }

    // ---- ⑤tools（§9.1 行 5：经 toolRefs 解引用；defaultTcp 选取与次序——
    // 实现层细化⑤；有 tools 则 defaultTcp 必填——KIN-14 默认 TCP 权威）。
    if (!root.toolRefs.empty() && !root.defaultTcp.has_value()) {
        return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::err(
            refMissing(std::string(), "defaultTcp",
                       "有工具引用而 defaultTcp 未设置（KIN-14 默认 TCP 必填"
                       "——runtime 侧以缺失拒收，建模侧输入面先行定位）"));
    }
    // defaultTcp 引用完整性（I-MDL-9：toolOid∈toolRefs；tcpKey∈该工具
    // tcpList——后者的存在性核查在解码后进行）。
    if (root.defaultTcp.has_value()) {
        const bool inRefs = std::find(root.toolRefs.begin(), root.toolRefs.end(),
                                      root.defaultTcp->toolOid)
            != root.toolRefs.end();
        if (!inRefs) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(
                refMissing(root.defaultTcp->toolOid.toCanonical(),
                           "defaultTcp.toolOid",
                           "defaultTcp 引用的工具不在 toolRefs 引用表内"
                           "（I-MDL-9——悬空默认 TCP 引用）"));
        }
    }
    // 次序：默认工具居首（runtime"首项为默认 TCP"），其余按 toolRefs 序。
    std::vector<core::ObjectId> orderedTools;
    orderedTools.reserve(root.toolRefs.size());
    if (root.defaultTcp.has_value()) {
        orderedTools.push_back(root.defaultTcp->toolOid);
    }
    for (const core::ObjectId& oid : root.toolRefs) {
        if (root.defaultTcp.has_value() && oid == root.defaultTcp->toolOid) {
            continue;  // 默认工具已居首——不重复
        }
        orderedTools.push_back(oid);
    }
    for (std::size_t i = 0; i < orderedTools.size(); ++i) {
        const core::ObjectId& oid = orderedTools[i];
        // 闭包解引用＋解码＋token 复核（CM-0 复核范围＝真实存储对象——O-36）。
        auto resolved = resolvePart<ToolDefinition>(closure, oid,
                                                    kToolDefinitionObjectType,
                                                    "toolRefs[" + std::to_string(i) + "]");
        if (!resolved.ok()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(resolved.error());
        }
        const ToolDefinition& tool = resolved.get();
        const std::string field = "tools[" + std::to_string(i) + "]";
        if (!isLegalLocalName(tool.localName)) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(
                illegalName(field + ".localName",
                            "工具 localName 为空或含合法字符集之外的字符"));
        }
        // 工具作用域名称唯一（V-26 同作用域无重复——工具是独立存储对象，
        // 解码门 I-MDL-2 的链作用域复核不覆盖跨对象作用域）。
        for (std::size_t j = 0; j < desc.tools.size(); ++j) {
            if (desc.tools[j].localName == tool.localName) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    illegalName(field + ".localName",
                                "工具 localName 在 tools 作用域内重复（与 tools["
                                    + std::to_string(j) + "] 同名——消歧归 runtime，"
                                    "R-4；输入面拒绝重复）"));
            }
        }
        runtime::ToolDescription td;
        td.objectId = tool.objectId;
        td.localName = tool.localName;
        // 工具几何（可空——runtime ToolDescription.geometry 为 optional 位）。
        if (tool.geometry.has_value()) {
            auto mapped = mapGeometryRef(*tool.geometry, root, field + ".geometry");
            if (!mapped.ok()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(mapped.error());
            }
            td.geometry = std::move(mapped.get());
        }
        // 物性三元（§4.4"断言①②③同连杆"——映射共用 mapBodyData）。
        std::optional<std::string> material;
        if (auto err = mapBodyData(tool.body, field + ".body", td.mass,
                                   td.centerOfMass, td.inertia, material);
            err.has_value()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(std::move(*err));
        }
        // 工具材料不映射（runtime ToolDescription 无 material 位——如实从
        // 对端契约，不发明字段）。
        // ---- tcpOffset（T_flange_tcp＝T_flange_tool ∘ T_tool_tcp）：默认
        // 工具取 defaultTcp.tcpKey 指向条目，其余工具取 tcpList 首条（实现
        // 层细化⑤——确定性选取）。
        const bool isDefault = root.defaultTcp.has_value()
            && oid == root.defaultTcp->toolOid;
        const std::string* wantKey = nullptr;
        if (isDefault) { wantKey = &root.defaultTcp->tcpKey; }
        const TcpEntry* chosen = nullptr;
        for (const TcpEntry& entry : tool.tcpList) {
            if (wantKey != nullptr) {
                if (entry.key == *wantKey) { chosen = &entry; break; }
            } else if (chosen == nullptr) {
                chosen = &entry;  // 无键约束——取首条（tcpList ≥1，I-MDL-13）
            }
        }
        if (chosen == nullptr) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(
                refMissing(oid.toCanonical(),
                           isDefault ? "defaultTcp.tcpKey" : field + ".tcpList",
                           isDefault ? "defaultTcp.tcpKey 不在被引工具 tcpList 内"
                                     : "工具 tcpList 为空（I-MDL-13 违例态）"));
        }
        // 法兰→TCP 合成：T_flange_tcp = T_flange_tool·T_tool_tcp（core §4.6
        // T_ab 读法；位姿乘法逐元素——rotVec/rotMul 冒烟纪律）。
        const rw::math::Vector3D<double> p =
            rotVec(tool.mountInterface.R(), chosen->offset.P())
            + tool.mountInterface.P();
        const rw::math::Rotation3D<double> r =
            rotMul(tool.mountInterface.R(), chosen->offset.R());
        td.tcpOffset = rw::math::Transform3D<double>(p, r);
        desc.tools.push_back(std::move(td));
    }

    // ---- ⑥scene（§9.1 行 6：经 sceneRefs 解引用；worldPose 世界系固连、
    // 不预乘安装旋转——M-11 禁止项，原样透传）。
    for (std::size_t i = 0; i < root.sceneRefs.size(); ++i) {
        const core::ObjectId& oid = root.sceneRefs[i];
        auto resolved = resolvePart<SceneObject>(closure, oid,
                                                kSceneObjectObjectType,
                                                "sceneRefs[" + std::to_string(i) + "]");
        if (!resolved.ok()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(resolved.error());
        }
        const SceneObject& sceneObject = resolved.get();
        const std::string field = "scene[" + std::to_string(i) + "]";
        if (!isLegalLocalName(sceneObject.localName)) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(
                illegalName(field + ".localName",
                            "场景对象 localName 为空或含合法字符集之外的字符"));
        }
        // 场景作用域名称唯一（V-26 同作用域无重复——场景对象是独立存储
        // 对象，解码门 I-MDL-2 的链作用域复核不覆盖跨对象作用域）。
        for (std::size_t j = 0; j < desc.scene.size(); ++j) {
            if (desc.scene[j].localName == sceneObject.localName) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    illegalName(field + ".localName",
                                "场景对象 localName 在 scene 作用域内重复（与 scene["
                                    + std::to_string(j) + "] 同名——消歧归 runtime，"
                                    "R-4；输入面拒绝重复）"));
            }
        }
        runtime::SceneObjectDescription sd;
        sd.objectId = sceneObject.objectId;
        sd.localName = sceneObject.localName;
        // 世界系固连位姿（不得预乘安装旋转——§6.4 禁止项 3；检测在 runtime
        // S6/S9，建模侧保证不引入预乘——原样透传即不预乘）。
        sd.worldPose = sceneObject.worldPose;
        // 环境几何：runtime CanonicalSceneObject.geometry 必有——modeling
        // 侧缺失不可映射（不伪造资源引用），RefMissing 定位。
        if (!sceneObject.geometry.has_value()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(
                refMissing(sceneObject.objectId.toCanonical(), field + ".geometry",
                           "场景对象无几何引用（runtime 环境几何引用必有——"
                           "不可伪造，补几何或移除引用）"));
        }
        auto mapped = mapGeometryRef(*sceneObject.geometry, root, field + ".geometry");
        if (!mapped.ok()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(mapped.error());
        }
        sd.geometry = std::move(mapped.get());
        desc.scene.push_back(std::move(sd));
    }

    // ---- ⑦base（§9.1 行 7：preset/customEaa/basePosition 编辑表示透传；
    // ★ 不在此计算 R_world_base——runtime 唯一计算，M-11/P-RT-4）。
    desc.base.preset = root.basePlacement.preset;  // 同一 runtime 枚举实体
    if (const auto eaa = root.basePlacement.customEaa.tryValue(); eaa.has_value()) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (!siGate((*eaa)[i], core::QuantityKind::Angle).has_value()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    unitIllegal("base.customEaa",
                                "custom 旋转矢量含未通过 SI 真值门的分量（rad）"));
            }
        }
        desc.base.customEaa = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            *eaa, root.basePlacement.customEaa.provenance());
    }
    if (const auto pos = root.basePlacement.basePosition.tryValue(); pos.has_value()) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (!siGate((*pos)[i], core::QuantityKind::Length).has_value()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    unitIllegal("base.basePosition",
                                "基座位置含未通过 SI 真值门的分量（m，世界系）"));
            }
        }
        desc.base.basePosition = *pos;
    }
    // NotProvided→零向量（runtime §4.3.2 默认平移 0——"未显式配置＝地面"
    // V15-04 的编译输入面形态；模板/导入层已带来源填默认，此处为防御面）。

    // ---- ⑧drivetrain（§9.1 行 8：经 drivetrainRef 解引用；ratioPerJoint＋
    // coupling——耦合矩阵 R2，R1 解码门已拒绝配置）。
    if (root.drivetrainRef.has_value()) {
        auto resolved = resolvePart<DrivetrainDesign>(
            closure, *root.drivetrainRef, kRobotDrivetrainObjectType,
            "drivetrainRef");
        if (!resolved.ok()) {
            return runtime::Expected<runtime::RobotDesignDescription,
                                     ModelingError>::err(resolved.error());
        }
        const DrivetrainDesign& drivetrain = resolved.get();
        desc.drivetrain.ratioPerJoint = drivetrain.ratioPerJoint;  // 无量纲透传
        for (std::size_t i = 0; i < desc.drivetrain.ratioPerJoint.size(); ++i) {
            if (const auto ratio = desc.drivetrain.ratioPerJoint[i].tryValue();
                ratio.has_value()
                && !siGate(*ratio, core::QuantityKind::Dimensionless).has_value()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    unitIllegal("drivetrain.ratioPerJoint[" + std::to_string(i) + "]",
                                "传动比未通过 SI 真值门（无量纲；非有限拒绝）"));
            }
        }
        if (drivetrain.coupling.has_value()) {
            // R2 耦合矩阵映射（R1 下解码门 I-MDL-12 已拒——此分支当前不可
            // 达，为 R2 阶段 D 预留的映射面；维度换算：闭区间 [first,last]
            // →半开区间 [first, count)）。
            runtime::CouplingMatrix cm;
            cm.rows = drivetrain.coupling->rows;
            cm.cols = drivetrain.coupling->cols;
            cm.c = drivetrain.coupling->c;
            cm.jointRange.firstIndex = drivetrain.coupling->jointRangeFirst;
            cm.jointRange.count = drivetrain.coupling->jointRangeLast
                >= drivetrain.coupling->jointRangeFirst
                ? (drivetrain.coupling->jointRangeLast
                   - drivetrain.coupling->jointRangeFirst + 1)
                : 0;
            cm.conditionNumber = drivetrain.coupling->conditionNumber;  // 申报值（校验器重算为准）
            desc.drivetrain.coupling = std::move(cm);
        }
        // ---- ⑨friction（§9.1 行 9：robot-drivetrain.frictionPerJoint 逐
        // 关节透传；缺失走 DataInsufficient 降级——判定归 dynamics，DYN-06）。
        desc.friction.reserve(drivetrain.frictionPerJoint.size());
        for (std::size_t i = 0; i < drivetrain.frictionPerJoint.size(); ++i) {
            const FrictionEntry& src = drivetrain.frictionPerJoint[i];
            runtime::JointFrictionDescription fd;
            // 摩擦系数族无 R1 量纲行（N·m·s/rad）——有限性门履行 SI 语义。
            for (const core::SourcedValue<double>* value :
                 {&src.viscous, &src.coulomb, &src.bias}) {
                if (value->state() == core::FieldState::Invalid) {
                    return runtime::Expected<runtime::RobotDesignDescription,
                                             ModelingError>::err(
                        unitIllegal("friction[" + std::to_string(i) + "]",
                                    "摩擦参数为 Invalid 态（无 SI 真值可映射）"));
                }
                if (const auto v = value->tryValue(); v.has_value()
                    && !finiteGate(*v).has_value()) {
                    return runtime::Expected<runtime::RobotDesignDescription,
                                             ModelingError>::err(
                        unitIllegal("friction[" + std::to_string(i) + "]",
                                    "摩擦参数未通过有限性门（非有限拒绝）"));
                }
            }
            fd.viscous = src.viscous;
            fd.coulomb = src.coulomb;
            fd.bias = src.bias;
            desc.friction.push_back(std::move(fd));
        }
    }

    // ---- ⑩resourceRefs（§9.1 行 10：根 resourceManifest 全量映射——
    // Solidified→项目对象（闭包存在性复核——O-36 真实存储对象）；Recorded→
    // 声明键（非对象，不复核闭包）；摘要透传不自算；路径不入 Description）。
    desc.resourceRefs.reserve(root.resourceManifest.size());
    for (const ResourceRef& entry : root.resourceManifest) {
        runtime::ResourceRef out;
        if (entry.state == ResourceState::Solidified) {
            if (!entry.solidifiedObject.has_value()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    refMissing(std::string(), "resourceManifest."
                               + entry.resourceId,
                               "Solidified 资源缺固化引用（I-MDL-10 违例态）"));
            }
            // 固化资源对象闭包存在性复核（真实存储对象——O-36 复核范围）。
            // 资源对象 token 归 project/io 词表——builder 只核存在性，不核
            // token（modeling 五 token 词表不覆盖资源对象）。
            if (!closure.tryObject(entry.solidifiedObject->objectId).has_value()) {
                return runtime::Expected<runtime::RobotDesignDescription,
                                         ModelingError>::err(
                    refMissing(entry.solidifiedObject->objectId.toCanonical(),
                               "resourceManifest." + entry.resourceId,
                               "固化资源对象不在闭包（悬空固化引用——"
                               "CON-03 三段边界）"));
            }
            out.resourceId = entry.solidifiedObject->objectId;
            out.state = runtime::ResourceState::Solidified;
        } else {
            // Recorded：确定性声明键（实现层细化④）；运行期行为（每次编译
            // 重读＋digest 复核）归 runtime §9.1——建模侧只做声明面。
            out.resourceId = deriveRecordedResourceObjectId(entry.resourceId);
            out.state = runtime::ResourceState::Recorded;
        }
        out.contentDigest = entry.contentDigest;  // 摘要透传（CON-05——不重复计算内容身份）
        out.sourcePathHint = std::nullopt;        // 路径不入 Description＝不入身份
        out.accessVersion = io::kAccessVersion;   // 读取契约版本单点（P-RT-6）
        desc.resourceRefs.push_back(std::move(out));
    }

    return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::ok(
        std::move(desc));
}

}  // namespace

// =====================================================================
// CanonicalModelInputBuilder（§9.4.5——build：闭包→Description）
// =====================================================================

runtime::Expected<runtime::RobotDesignDescription, ModelingError>
    CanonicalModelInputBuilder::build(const ObjectClosureView& closure,
                                      std::vector<core::DiagnosticRecord>& diags) const
{
    // 步① 根对象定位（token=robot-design 路由——runtime kRobotDesignObjectType
    // 单点常量；闭包无根对象→RefMissing，runtime S2"闭包无 robot-design
    // 对象"InputInvalid 的建模侧预演）。
    const std::optional<ClosureObject> rootBytes =
        closure.tryObjectByToken(std::string_view(kRobotDesignObjectType));
    if (!rootBytes.has_value()) {
        return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::err(
            refMissing(std::string(), std::string(kRobotDesignObjectType),
                       "闭包内无 robot-design 根对象（编译入口对象缺失——"
                       "§9.4.5 @pre）"));
    }
    // 步② 根对象解码（字节进入类型化世界的唯一闸口；MalformedPayload/
    // SchemaVersionUnsupported 原样透传——各为其生产者接口登记面）。
    RobotDesignCodec codec;
    const runtime::Expected<ObjectVariant, ModelingError> decoded =
        codec.decode(rootBytes->bytes, kCurrentFormatVersion);
    if (!decoded.ok()) {
        return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::err(
            decoded.error());
    }
    // 步③ 变体持类型复核（token 声称 robot-design 而字节是其他对象——错位）。
    if (!std::holds_alternative<RobotDesign>(decoded.get())) {
        return runtime::Expected<runtime::RobotDesignDescription, ModelingError>::err(
            refMissing(std::string(), std::string(kRobotDesignObjectType),
                       "根对象字节解码变体与 token 不符（期望 robot-design）"));
    }
    // 步④ 委托内核（DH 展开器为无状态实现——就地构造，§3.4 纯函数服务）。
    const DhExplicitConverter converter;
    return buildDescription(std::get<RobotDesign>(decoded.get()), closure,
                            converter, diags);
}

// =====================================================================
// RobotDesignReader（§9.4.5——runtime::IRobotDesignReader 产品实现）
// =====================================================================

RobotDesignReader::RobotDesignReader(const ObjectClosureView* closure)
    : m_closure(closure)
{
    // 装配契约违约 fail-fast（AGENTS 错误语义——编程错误不走值面）：
    // 无闭包域的 reader 无法解引用部件对象，§9.2 注入形态的构成性前提。
    if (m_closure == nullptr) {
        throw std::invalid_argument(
            "modeling/reader/closure-required: RobotDesignReader 须绑定闭包域"
            "字节源（§9.2——装配期与 CompileRequest.objects 同源绑定；空指针"
            "＝装配缺陷，fail-fast）");
    }
}

runtime::Expected<runtime::RobotDesignDescription, runtime::RuntimeError>
    RobotDesignReader::read(const std::vector<std::uint8_t>& objectBytes,
                            std::uint32_t objectTypeFormatVersion) const
{
    // 步① 格式版本门（NFR-DEP-04：不识别的版本拒绝而非尽力猜测——当前
    // 唯一支持值＝根对象 schema 主版本 1；runtime S2 固定传 1 的实现层登记）。
    if (objectTypeFormatVersion != kRobotDesignSchemaVersion) {
        return runtime::Expected<runtime::RobotDesignDescription,
                                 runtime::RuntimeError>::err(
            runtime::RuntimeError(
                runtime::RuntimeErrorCode::InputInvalid,
                "reader: SchemaVersionUnsupported（object-type=robot-design, "
                "format-version=" + std::to_string(objectTypeFormatVersion)
                    + ", supported=" + std::to_string(kRobotDesignSchemaVersion)
                    + "）——版本不识别拒绝（NFR-DEP-04）"));
    }
    // 步② 根对象解码（read() 的输入即根对象字节——runtime S2 按 token 路由
    // 取出后传入；解码错误与版本门同轨转译）。
    RobotDesignCodec codec;
    const runtime::Expected<ObjectVariant, ModelingError> decoded =
        codec.decode(objectBytes, kCurrentFormatVersion);
    if (!decoded.ok()) {
        return runtime::Expected<runtime::RobotDesignDescription,
                                 runtime::RuntimeError>::err(
            runtime::RuntimeError(
                runtime::RuntimeErrorCode::InputInvalid,
                "reader: " + std::string(modelingErrorCodeToken(decoded.error().code))
                    + "（根对象解码失败）：" + decoded.error().detail));
    }
    if (!std::holds_alternative<RobotDesign>(decoded.get())) {
        return runtime::Expected<runtime::RobotDesignDescription,
                                 runtime::RuntimeError>::err(
            runtime::RuntimeError(
                runtime::RuntimeErrorCode::InputInvalid,
                "reader: 根对象字节解码变体与 robot-design 不符（错位字节）"));
    }
    // 步③ 委托 builder 内核（部件对象经构造期绑定的闭包域字节源解析——
    // §9.2；diags 为契约预留位，本实现两态不产诊断记录）。
    std::vector<core::DiagnosticRecord> diags;
    const DhExplicitConverter converter;
    const runtime::Expected<runtime::RobotDesignDescription, ModelingError> built =
        buildDescription(std::get<RobotDesign>(decoded.get()), *m_closure,
                         converter, diags);
    if (!built.ok()) {
        // 步④ 值面错误→InputInvalid 转译（§5.2 S2 归属表"reader 失败→
        // InputInvalid 含定位"——modeling 错误 token＋定位参数全量并入
        // detail，runtime 不二次猜测；码恒 InputInvalid）。
        std::string detail = "reader: "
            + std::string(modelingErrorCodeToken(built.error().code));
        for (const auto& [key, value] : built.error().params) {
            detail += " " + key + "=" + value;
        }
        if (!built.error().detail.empty()) { detail += "——" + built.error().detail; }
        return runtime::Expected<runtime::RobotDesignDescription,
                                 runtime::RuntimeError>::err(
            runtime::RuntimeError(runtime::RuntimeErrorCode::InputInvalid,
                                  std::move(detail)));
    }
    return runtime::Expected<runtime::RobotDesignDescription,
                             runtime::RuntimeError>::ok(built.get());
}

}  // namespace sdurws::ird::modeling

