/**
 * @file   Errors.hpp
 * @brief  modeling 错误契约——ModelingErrorCode 全表（稳定 token）、
 *         ModelingError 值类型与域错误→稳定诊断码映射数据。
 *
 * 设计依据：
 *   - units/modeling.md §3.3（公共头表 Errors.hpp 行："ModelingError/
 *     ModelingErrorCode 与域错误→诊断码映射数据"——T02）、§9.5 尾段
 *     （域错误→码映射纪律："Errors.hpp 中 ModelingErrorCode 枚举每值登记
 *     映射码（如 AuthorityViolation→MDL-… 无独立码时复用校验码族）；产码
 *     唯一经 IDiagnosticFactory::create（码已注册校验），禁字符串拼码"）
 *   - 各 §9.4 接口契约的 @错误 行（枚举值的出处界面，逐值注释标注）：
 *     §9.4.1 编辑器 EditOutcome.errorCode、§9.4.2 模板工厂、§9.4.5 规范
 *     输入构造器
 *   - 先例与形态：io/IoError.hpp＋io/IoDiagnostics.hpp（错误面/诊断面
 *     两层分工的同款口径）、diagnostics/Errors.hpp（单元设施异常轨）
 *   - 需求 ERR-01（错误可追溯）、NFR-MNT-01（计算内核零 Qt——纯标准库头）
 *   - 任务契约 tasks/foundation/WP-13-T02.json acceptance 4（公共头落位
 *     ——Errors.hpp 行）
 *
 * 背景说明（两个层面的码，不要混用——io 同款分工口径）：
 *   - 本表 ModelingErrorCode＝modeling 域内**错误面**：§9.4 各接口把失败
 *     以 ModelingError{code, params, detail} 值返回给调用方的机器可判码
 *     （程序判定用——isXxx 分支、编辑器就地错误呈现定位等）；
 *   - MDL-* 稳定诊断码＝**诊断内容**的码值，注册权威＝diagnostics
 *     StableCodeRegistry（§9.5、DiagCodes.hpp）；错误面→诊断面的换轨
 *     唯一经本头的映射数据行（mapping 数据，非产码动作）＋已注册码的
 *     工厂构造——本头不产出诊断记录，也禁把枚举值当字符串拼成诊断码
 *     （§9.5"禁字符串拼码"原文）。
 *
 * 枚举值收编口径（为什么是这 12 值）：全量收编 §9.4 接口契约已登记的
 * 域错误名（io/IoError.hpp"按卡面原样承载建议值全集，使错误轨道自落位
 * 起即可编译"同款先例——§9.4 各消费者接口在 T03+ 逐个落地时直接复用
 * 本表，不逐任务重开枚举）。枚举顺序＝§9.4 首次出现序（§9.4.1→§9.4.2→
 * §9.4.5）；持久化契约面纪律：只允许表尾追加并走单元卡增量修订（数值
 * 进入二进制契约，不重排既有值——diagnostics::DiagnosticsErrorCode 同款）。
 *
 * 映射数据的阶段纪律（与 DiagCodes.hpp §9.5 注册纪律对齐）：映射行只
 * 登记到 §9.5 已分配任务行的码——当前两行：SchemaVersionUnsupported→
 * MDL-READINESS-SCHEMA-UNSUPPORTED（§9.5 T02/T03 行，DiagCodes.hpp 工厂
 * 同批登记）＋TemplateDisabled→MDL-TEMPLATE-DISABLED（§9.5 T07 行，
 * WP-13-T07 实现期增登随生产者接口 createDraft 落位同批登记）。其余值
 * 的映射随其生产者接口落位任务在 §9.5 纪律内登记（映射函数返回
 * nullopt＝"暂无已登记映射码"——调用方此时不得产诊断，错误经值面返回；
 * 禁止私定新码值凑数）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token 为编译期固定表（switch 全枚举），同码同串、跨进程逐字节
 * 一致（NFR-COR-02 的码面子集）。
 */

#ifndef IRD_MODELING_ERRORS_HPP
#define IRD_MODELING_ERRORS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::modeling {

// =====================================================================
// ModelingErrorCode——modeling 域稳定错误码全表（§9.4 接口契约 @错误 行
// 收编；枚举顺序＝§9.4 首次出现序，表尾追加纪律见文件头注）
// =====================================================================

/**
 * @brief modeling 域稳定错误码全表（13 值——§9.4 已登记域错误名收编＋
 *        编解码族表尾追加 1 值，逐值注释给出其产生接口与落位任务）。
 *
 * 底型 std::uint8_t：域错误面按 §9.4 既有登记收编，后续任务表尾追加
 * （16 位以内充足且枚举体积恒定——IoErrorCode 同款取舍）。注意与本表
 * 并行的两族专用错误枚举（各接口局部载体，随其任务落位，不并入本表）：
 * EstimateErrorCode（§9.4.6 物性估算——T04）、DhErrorCode（§9.4.7 DH
 * 转换——T09）。本表是跨接口复用的**域级**错误轨道。
 */
enum class ModelingErrorCode : std::uint8_t {
    // ---- 编辑器族（§9.4.1 IRobotDesignEditor EditOutcome.errorCode——T03）----
    /// "DuplicateObjectId"——对象稳定身份重复（同 objectId 二次登记；ARC-04
    /// 身份唯一性的编辑面拒绝）。
    DuplicateObjectId,
    /// "RefProtected"——删除被引用对象被拒绝（I-MDL-9 引用保护；V-04：
    /// 对象字节与旧修订闭包完整，仅移除调用方请求）。
    RefProtected,
    /// "AuthorityViolation"——双权威参数化冲突（§7.3 C-1/C-2：DH 权威下
    /// 编辑派生字段；V-14：调用方错误拒绝，工作集字节不变）。
    AuthorityViolation,
    /// "UnitIllegal"——单位/量纲不合法（一切物理量 SI 存储的边界断言，
    /// §3.4 总约定 4；换算唯一经 core Units，SA-12）。
    UnitIllegal,
    /// "NotFinite"——物理量非有限值（NaN/Inf；附录 D 有限性判定的输入面）。
    NotFinite,
    /// "CentroidEditUnresolved"——质心编辑未决议（§5.3 规则 2：平行轴
    /// 迁移/覆盖完整张量二选一，两者都不选即拒绝；V-17）。
    CentroidEditUnresolved,
    /// "BatchPartial"——批量编辑部分失败（附逐行定位清单；§9.4.1 批量
    /// 粘贴语义——非法行保留原值，合法行不连带回滚的边界由调用方决定）。
    BatchPartial,

    // ---- 模板族（§9.4.2 IRobotDesignTemplateFactory——T07）----
    /// "TemplateDisabled"——模板登记未启用（P-03：七轴模板冻结前
    /// createDraft→TemplateDisabled，不静默替换为六轴；V-02）。
    TemplateDisabled,
    /// "IllegalName"——localName 非法字符集/重复（AT-18 输入面：名称
    /// 合法字符集与唯一性在建模侧登记，解析归 runtime）。
    IllegalName,

    // ---- 规范输入构造族（§9.4.5 ICanonicalModelInputBuilder——T12；
    //      @错误 行原文：RefMissing→runtime InputInvalid 预演|UnitIllegal|
    //      DhExpandFailed|SchemaVersionUnsupported）----
    /// "RefMissing"——引用表对象不在闭包（引用悬空；§9.4.5 原文——
    /// runtime 侧按 InputInvalid 预演转译，modeling 侧先以本值拒绝）。
    RefMissing,
    /// "DhExpandFailed"——DH 权威展开失败（§7.5 五状态判定未产出可用
    /// 展开；消费方不得到达 Description 构造）。
    DhExpandFailed,
    /// "SchemaVersionUnsupported"——对象 schema 主版本超出本程序支持
    /// （§9.5 MDL-READINESS-SCHEMA-UNSUPPORTED 同义事件——升程序/重新
    /// 编辑；本值是当前唯一已有已登记映射码的枚举值，见 modelingDiagCode）。
    SchemaVersionUnsupported,

    // ---- 编解码族（§9.4.9 IRobotDesignCodec——T03；表尾追加登记见下）----
    /// "MalformedPayload"——canonical 编码字节破损（decode 校验链失败：
    /// magic 不符/截断/越界/尾随字节/非法枚举或 presence 值/非有限
    /// double/UTF-8 不成形/集合未规范化/解码产物不变量违例——Codec.hpp
    /// 校验链全文）。表尾追加登记（枚举数值进入二进制契约，只增不插）：
    /// §9.4.9 codec 行明文列举的错误仅 SchemaVersionUnsupported，本值
    /// 承载"其余解码失败"（Expected 值面需要可判别的失败侧——外部字节
    /// 属可恢复数据错误，不走异常轨）；单元卡 §15 增量同步登记。
    MalformedPayload,

    // ---- 规范包族（§9.4.9 IModelPackagePort——T13；表尾追加登记）----
    /// "ExportFailed"——规范包导出失败（MDL-20"导出失败恢复先前输出"：
    /// io AtomicFile prepare/写入/自检/commit 任一环的环境或写入失败——
    /// 目标路径不可写/预算超限/中途失败；项目状态不变且旧输出文件完好，
    /// V-29 文件层观测）。诊断面＝MDL-EXPORT-FAILED（§9.5 T13 行，映射
    /// 见 modelingDiagCode）。闭包内容缺陷（缺根/缺被引对象）不走本值
    /// ——那是调用方数据错误，走 RefMissing 值面。表尾追加登记（枚举
    /// 数值进入二进制契约，只增不插）；单元卡 §15 增量同步登记。
    ExportFailed,
    /// "PackageUnknown"——非本软件导出的规范工件（MDL-20 导入门：manifest
    /// 缺失/不可读、formatId/producer 不符、容器无法以本格式打开——
    /// fail-closed，绝不猜测解释外部格式）。诊断面＝
    /// MDL-IMPORT-PACKAGE-UNKNOWN（§9.5 T13 行：引导用户走 MDL-18/R2
    /// WorkCell 反向导入通道；不与 mapWorkCellXml 实现混淆——该通道仍为
    /// R1 NotImplemented 边界）。表尾追加登记；单元卡 §15 增量同步登记。
    PackageUnknown,
};

/**
 * @brief 取错误码的稳定 token（枚举成员名原文串，如 "RefProtected"）。
 *
 * token 与枚举成员一一对应（§9.4/V 矩阵行文即以成员名指称错误，如
 * V-04"RefProtected 错误码"）；本函数是实现侧唯一映射点——头文件枚举
 * 注释与本函数若失同步，测试的全表机械比对即刻暴露（io errorCodeToken
 * 同款防线）。返回值指向静态存储期字面量。
 *
 * 注意：本 token 是**域错误面**的判别串，不是 MDL-* 稳定诊断码（诊断码
 * 的注册权威＝diagnostics StableCodeRegistry——文件头"两个层面"注）。
 *
 * @param code [in] 错误码（全表 12 值均有 token——switch 全枚举、无
 *              default，新增枚举值未登记表项时编译器告警暴露遗漏）
 * @return 稳定 token（静态存储期）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同串）。
 */
std::string_view modelingErrorCodeToken(ModelingErrorCode code) noexcept;

// =====================================================================
// ModelingError——modeling 错误值类型（§9.3/§9.4 各接口的失败承载；
// Expected<T, ModelingError> 的 E 形态，§9.4.5 原文）
// =====================================================================

/**
 * @brief modeling 域错误值类型：稳定码＋上下文参数＋原始细节。
 *
 * 形态与 io::IoError 同款（§9.0 公共约定的域内落地——码＋有序参数表＋
 * 开发级细节），原因同样成立：编辑拒绝/导入报告/构造失败大多是需要用户
 * 确认或修正的**正常业务路径**，经值返回而不是异常穿越单元边界（§9.4.1
 * EditOutcome 轨道同构）。异常轨（fail-fast）只留给调用方契约违约
 * （AGENTS.md 错误语义），那是编程错误，不构造本类型。
 *
 * 生命周期/所有权：纯值类型，随返回值/输出参数按值传递，调用方所有。
 *
 * 确定性（NFR-COR-01/02）：同输入同错误同 params 序——params 用保序
 * vector 而非关联容器，遍历序＝构造序，不经哈希/树序重排；数值参数在
 * 构造处按稳定格式化规则（定点、不经 locale）转文本。
 *
 * 敏感性约束（io/IoError.hpp 同款两级脱敏的源头防线）：detail 与 params
 * 只允许进入内部诊断构造链，成为用户可见文案前必须经 diagnostics 脱敏
 * 设施；本类型自身不做脱敏（职责分层——脱敏归 diagnostics）。
 */
struct ModelingError {
    /**
     * 稳定错误码。缺省＝DuplicateObjectId（枚举首值）：仅为聚合容器默认
     * 初始化留位（"失败清单"占位项语义），业务代码应总是经接口返回的
     * ModelingError 携带语义明确的码，不手工构造缺省值当作有效错误。
     */
    ModelingErrorCode code = ModelingErrorCode::DuplicateObjectId;

    /**
     * 上下文参数（键值对，构造序保序——确定性要求）。键的取值随码而异
     * （如 SchemaVersionUnsupported→object-type/schema-version/supported-
     * major，与 DiagCodes.hpp 同码 paramSchema 对齐；RefProtected→
     * object-id 等）。值为字符串：敏感值（路径原文等）入表前由生产方
     * 自评，最终用户可见文案必须经 diagnostics 脱敏。
     */
    std::vector<std::pair<std::string, std::string>> params;

    /// 原始细节（底层错误文本/定位上下文）。仅供内部诊断链与日志；不得
    /// 未经脱敏直接呈现（文件头"敏感性约束"）。
    std::string detail;
};

// =====================================================================
// 域错误→稳定诊断码映射数据（§9.5 尾段——Errors.hpp 承载的映射义务；
// 映射阶段纪律见文件头注）
// =====================================================================

/**
 * @brief 域错误码 → 已登记 MDL-* 稳定诊断码的映射（§9.5"每值登记映射
 *        码"的数据面）。
 *
 * 返回 nullopt 的语义＝该域错误**当前没有已登记的映射码**——两种情形：
 *   ①§9.5 明文"无独立码时复用校验码族"的值（如 AuthorityViolation）：
 *     具体复用哪一族属产出点语义裁决，随其生产者接口落位任务登记，
 *     本任务不私定（P-MDL-8 草稿态纪律：以当周卡面为准，冻结后增量同步）；
 *   ②生产者接口尚未落位的值（T03+ 的编辑器/模板/构造族）：映射随
 *     §9.5 对应码行注册同批落地。
 * nullopt 的调用方契约：不得产诊断——错误经 ModelingError 值面返回/
 * 呈现；产码唯一经 IDiagnosticFactory::create（码已注册校验，§9.5 原文）。
 *
 * @param code [in] 域错误码（全表 12 值均可入参——switch 全枚举）
 * @return 已登记稳定码文本（当前：SchemaVersionUnsupported→
 *         "MDL-READINESS-SCHEMA-UNSUPPORTED"，与 DiagCodes.hpp 工厂登记
 *         同源同串，测试交叉核对；ExportFailed→"MDL-EXPORT-FAILED"与
 *         PackageUnknown→"MDL-IMPORT-PACKAGE-UNKNOWN"——WP-13-T13 随
 *         Package 落位同批登记；nullopt＝暂无已登记映射（文件头注））
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同映射）。
 */
std::optional<std::string_view> modelingDiagCode(ModelingErrorCode code) noexcept;

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_ERRORS_HPP
