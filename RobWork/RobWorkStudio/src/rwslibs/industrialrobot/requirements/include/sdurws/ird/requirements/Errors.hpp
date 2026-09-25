/**
 * @file   Errors.hpp
 * @brief  requirements 错误契约——RequirementErrorCode 全表（稳定 token）、
 *         RequirementError 值类型与域错误→稳定诊断码映射数据。
 *
 * 设计依据：
 *   - units/requirements.md §3.3（公共头表 Errors.hpp 行："RequirementError
 *     (code) 与 REQ- 稳定码清单（§9.6）"——T02）、§9.2 接口契约总则
 *     （"纯函数服务非异常出口（结果对象＋诊断列表）、调用方错误 fail-fast
 *     语义返回"）、§9.3/§9.4/§9.5 各接口 @错误 行（枚举值的出处界面）
 *   - 先例与形态：modeling/Errors.hpp（域级错误轨道＋映射数据的同款
 *     口径）、io/IoError.hpp（码＋上下文参数＋原始细节的值形态）、
 *     diagnostics/Errors.hpp（单元设施异常轨——本头不是异常轨）
 *   - 需求 ERR-01（错误可追溯）、NFR-MNT-01（计算内核零 Qt——纯标准库
 *     头）、I-REQ-3（名称唯一，构造边界拒绝）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3（RequirementError
 *     落位——Errors.hpp 行）
 *
 * 背景说明（两个层面的码，不要混用——modeling/io 同款分工口径）：
 *   - 本表 RequirementErrorCode＝requirements 域内**错误面**：§9.3~§9.5
 *     各接口把失败以 RequirementError{code, params, detail} 值返回给调用
 *     方的机器可判码（程序判定用——编辑器就地错误呈现、导入映射报告、
 *     构造拒绝分支等）；codec 的 Expected<T, RequirementError> 轨道
 *     （§9.5 IRequirementCodec 原文签名）即以本类型为失败侧。
 *   - REQ-\* 稳定诊断码＝**诊断内容**的码值，注册权威＝diagnostics
 *     StableCodeRegistry（§9.6、DiagCodes.hpp）；错误面→诊断面的换轨
 *     唯一经本头的映射数据行（mapping 数据，非产码动作）＋已注册码的
 *     工厂构造——本头不产出诊断记录，也禁把枚举值当字符串拼成诊断码
 *     （modeling §9.5"禁字符串拼码"同款纪律，REQ 侧沿用）。
 *
 * 枚举值收编口径（为什么是这 9 值）：全量收编卡面 §9.3~§9.5 接口契约已
 * 登记的域错误名（modeling/Errors.hpp"按卡面原样承载建议值全集，使错误
 * 轨道自落位起即可编译"同款先例——§9 各消费者接口在 T03+ 逐个落地时
 * 直接复用本表，不逐任务重开枚举）。枚举顺序＝卡面首次出现序（§9.3 编辑
 * 器/解码→§9.4 createPoint→§9.5 buildPlan）；持久化契约面纪律：只允许
 * 表尾追加并走单元卡增量修订（数值进入二进制契约，不重排既有值）。
 *
 * 映射数据的阶段纪律（与 DiagCodes.hpp §9.6 注册纪律对齐）：映射行只
 * 登记到 §9.6 已分配任务行的码——当前一行：SchemaVersionUnsupported→
 * REQ-SCHEMA-UNSUPPORTED（§9.6 T02/T03 行，DiagCodes.hpp 工厂同批登记；
 * 生产界面＝editor loadBaseline 解码失败 §9.3 原文＋codec decode）。其余
 * 值的映射随其生产者接口落位任务（§9.6 表 T04/T05 行）在 §9.6 纪律内
 * 登记（映射函数返回 nullopt＝"暂无已登记映射码"——调用方此时不得产
 * 诊断，错误经值面返回；禁止私定新码值凑数）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token 为编译期固定表（switch 全枚举），同码同串、跨进程逐字节
 * 一致（NFR-COR-02 的码面子集）。
 */

#ifndef IRD_REQUIREMENTS_ERRORS_HPP
#define IRD_REQUIREMENTS_ERRORS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// RequirementErrorCode——requirements 域稳定错误码全表（卡面 §9.3~§9.5
// 接口契约 @错误 行收编；枚举顺序＝卡面首次出现序，表尾追加纪律见文件
// 头注）
// =====================================================================

/**
 * @brief requirements 域稳定错误码全表（9 值——§9.3~§9.5 已登记域错误
 *        名收编，逐值注释给出其产生接口与落位任务）。
 *
 * 底型 std::uint8_t：域错误面按卡面既有登记收编，后续任务表尾追加
 * （16 位以内充足且枚举体积恒定——modeling/io 同款取舍）。
 */
enum class RequirementErrorCode : std::uint8_t {
    // ---- 解码/编解码族（§9.3 IRequirementEditor loadBaseline @pre＋
    //      §9.5 IRequirementCodec decode——T03；@pre 行原文：
    //      "解码失败→RequirementError(SchemaVersionUnsupported)"）----
    /// "SchemaVersionUnsupported"——对象 schema 主版本超出本程序支持
    /// （§4.2 schemaVersion 行 NFR-DEP-04 稳定拒绝面；§9.6
    /// REQ-SCHEMA-UNSUPPORTED 同义事件——升级程序/重新编辑；本值是当前
    /// 唯一已有已登记映射码的枚举值，见 requirementDiagCode）。
    SchemaVersionUnsupported,

    // ---- 任务点构造族（§9.4 ITaskPointService createPoint @错误 行——T03；
    //      @错误 行原文：DuplicateName|IllegalTolerance|ZeroVectorTarget|
    //      AllDofFree）----
    /// "DuplicateName"——集合内名称重复（I-REQ-3 名称唯一的构造边界
    /// 拒绝；不静默加后缀——NFR-COR-03）。
    DuplicateName,
    /// "IllegalTolerance"——容差非法（§4.3 ToleranceSpec 两分量 >0 且
    /// 有限——I-REQ-5 后半的构造面）。
    IllegalTolerance,
    /// "ZeroVectorTarget"——零向量目标（§4.3 PoseConstraint.position
    /// Provided 时三分量有限的边界反例；位姿目标不退化为零向量点）。
    ZeroVectorTarget,
    /// "AllDofFree"——受约束分量全 false（I-REQ-5：constrainedDof 至少
    /// 一真——任务点至少约束一个分量；全 false 非法的构造面拒绝）。
    AllDofFree,

    // ---- 采样计划族（§9.5 ISamplingPlanBuilder buildPlan @错误 行——T03；
    //      @错误 行原文：DegenerateRegion|NegativeCount|RegionNotBox）----
    /// "DegenerateRegion"——区域退化（§4.4/I-REQ-6：Box size 三分量 >0
    /// 且有限的构造面拒绝——零体积/负尺寸区域不可建立采样计划）。
    DegenerateRegion,
    /// "NegativeCount"——负采样计数（§4.4/I-REQ-6：采样计数 ≥0——0
    /// 合法（零样本场景由评估判 DataInsufficient，KIN-04 R8/§6.2），
    /// 负数非法）。
    NegativeCount,
    /// "RegionNotBox"——计划-区域形态失配（§9.5 buildPlan 输入的区域
    /// 不是本单元登记的 Box 形态——P-REQ-7：区域几何扩展走需求变更，
    /// 采样计划定义只承接 Box）。
    RegionNotBox,

    // ---- canonical 编解码族（§9.5 IRequirementCodec decode 校验链——T03
    //      表尾增列：WP-14-T03 落位时按本头"表尾追加纪律"增列的新值；
    //      units/requirements.md §14.6 v0.3 登记在案）----
    /// "MalformedPayload"——字节破损（decode 结构校验失败：magic/截断/
    /// 越界/非法枚举/非法 presence/非有限 double/UTF-8 不成形/集合未规范
    /// 序/不变量违例——modeling::ModelingErrorCode::MalformedPayload 同义
    /// 面；params 携 byte-offset/invariant 定位；无 REQ-* 诊断码映射——
    /// 字节面错误经值面返回，禁止私定映射码凑数）
    MalformedPayload,
};

/**
 * @brief 取错误码的稳定 token（枚举成员名原文串，如 "DuplicateName"）。
 *
 * token 与枚举成员一一对应（§9.4/V 矩阵行文即以成员名指称错误）；本
 * 函数是实现侧唯一映射点——头文件枚举注释与本函数若失同步，测试的全表
 * 机械比对即刻暴露（modeling errorCodeToken 同款防线）。返回值指向静态
 * 存储期字面量。
 *
 * 注意：本 token 是**域错误面**的判别串，不是 REQ-\* 稳定诊断码（诊断码
 * 的注册权威＝diagnostics StableCodeRegistry——文件头"两个层面"注）。
 *
 * @param code [in] 错误码（全表 9 值均有 token——switch 全枚举、无
 *              default，新增枚举值未登记表项时编译器告警暴露遗漏）
 * @return 稳定 token（静态存储期）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同串）。
 */
std::string_view requirementErrorCodeToken(RequirementErrorCode code) noexcept;

// =====================================================================
// RequirementError——requirements 错误值类型（§9.3/§9.5 接口签名
// Expected<T, RequirementError> 的 E 形态；§9.2 总则"非异常出口"的值承载）
// =====================================================================

/**
 * @brief requirements 域错误值类型：稳定码＋上下文参数＋原始细节。
 *
 * 形态与 modeling::ModelingError/io::IoError 同款（码＋有序参数表＋开发
 * 级细节），原因同样成立：构造拒绝/解码失败是需要用户确认或修正的
 * **正常业务路径**，经值返回而不是异常穿越单元边界（§9.2 总则原文；
 * §9.5 codec Expected 签名即本类型的消费面）。异常轨（fail-fast）只留给
 * 调用方契约违约（AGENTS.md 错误语义），那是编程错误，不构造本类型。
 *
 * 生命周期/所有权：纯值类型，随返回值按值传递，调用方所有。
 *
 * 确定性（NFR-COR-01/02）：同输入同错误同 params 序——params 用保序
 * vector 而非关联容器，遍历序＝构造序，不经哈希/树序重排；数值参数在
 * 构造处按稳定格式化规则（定点、不经 locale）转文本。
 *
 * 敏感性约束（io/modeling 同款两级脱敏的源头防线）：detail 与 params
 * 只允许进入内部诊断构造链，成为用户可见文案前必须经 diagnostics 脱敏
 * 设施；本类型自身不做脱敏（职责分层——脱敏归 diagnostics）。
 */
struct RequirementError {
    /**
     * 稳定错误码。缺省＝SchemaVersionUnsupported（枚举首值）：仅为聚合
     * 容器默认初始化留位（"失败清单"占位项语义），业务代码应总是经接口
     * 返回的 RequirementError 携带语义明确的码，不手工构造缺省值当作
     * 有效错误。
     */
    RequirementErrorCode code = RequirementErrorCode::SchemaVersionUnsupported;

    /**
     * 上下文参数（键值对，构造序保序——确定性要求）。键的取值随码而异
     * （如 SchemaVersionUnsupported→object-type/schema-version/supported-
     * major，与 DiagCodes.hpp 同码 paramSchema 对齐；DuplicateName→
     * name 等）。值为字符串：敏感值（路径原文等）入表前由生产方自评，
     * 最终用户可见文案必须经 diagnostics 脱敏。
     */
    std::vector<std::pair<std::string, std::string>> params;

    /// 原始细节（底层错误文本/定位上下文）。仅供内部诊断链与日志；不得
    /// 未经脱敏直接呈现（文件头"敏感性约束"）。
    std::string detail;
};

// =====================================================================
// 域错误→稳定诊断码映射数据（§9.6 表的 Errors.hpp 承载义务；映射阶段
// 纪律见文件头注）
// =====================================================================

/**
 * @brief 域错误码 → 已登记 REQ-\* 稳定诊断码的映射（§9.6"每码随对应
 *        任务注册"纪律在错误面的数据落点）。
 *
 * 返回 nullopt 的语义＝该域错误**当前没有已登记的映射码**——生产者接口
 * 尚未落位（§9.4/§9.5 各构造面随 T03 落地、其 §9.6 码行属 T04/T05 批次
 * ——分批注册，不预建无消费者条目）。nullopt 的调用方契约：不得产诊断
 * ——错误经 RequirementError 值面返回/呈现；产码唯一经 diagnostics 工厂
 * （码已注册校验；P-IO-6/modeling 同款"禁字符串拼码"）。
 *
 * @param code [in] 域错误码（全表 9 值均可入参——switch 全枚举）
 * @return 已登记稳定码文本（当前：SchemaVersionUnsupported→
 *         "REQ-SCHEMA-UNSUPPORTED"，§9.6 T02/T03 行，与 DiagCodes.hpp
 *         工厂登记同源同串，测试交叉核对；nullopt＝暂无已登记映射
 *         ——文件头注）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同映射）。
 */
std::optional<std::string_view> requirementDiagCode(RequirementErrorCode code) noexcept;

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_ERRORS_HPP
