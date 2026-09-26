/**
 * @file   Errors.hpp
 * @brief  kinematics 错误契约——KinematicsErrorCode 全表（稳定 token）、
 *         KinematicsError 值类型与域错误→稳定诊断码映射数据。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Errors.hpp/DiagCodes.hpp 行——
 *     "KinematicsError 与 KIN- 稳定码清单（§9.6）"，任务 T02＝WP-15-T02）、
 *     §9.1 接口契约总则（"纯函数服务非异常出口（Expected<T, KinematicsError>
 *     或结果对象＋诊断列表）；调用方错误 fail-fast"）、§9.2 IFkEvaluator
 *     @错误 行（枚举值的唯一卡面出处行）、§5.2 非法输入行、§5.5 失败分类
 *   - 先例与形态：requirements/Errors.hpp（WP-14-T02 同款——域级错误轨道
 *     ＋映射数据的分批纪律）、modeling/Errors.hpp、io/IoError.hpp
 *   - 需求 NFR-MNT-01（计算内核零 Qt——纯标准库头）、NFR-COR-03（非法
 *     输入拒绝不钳制）、ERR-01（错误可追溯）
 *   - 任务契约 tasks/foundation/WP-15-T02.json acceptance 4（Errors.hpp/
 *     DiagCodes.hpp 公共头落位——§3.3 布局表 T02 行）
 *
 * 背景说明（两个层面的码，不要混用——requirements/modeling/io 同款分工）：
 *   - 本表 KinematicsErrorCode＝kinematics 域内**错误面**：T03+ 各接口把
 *     调用方输入类失败以 KinematicsError{code, params, detail} 值返回给
 *     调用方的机器可判码（§9.1 非异常出口；§5.2"非法输入→fail-fast、
 *     不钳制不置零"的值承载——返回错误值并拒绝计算，属调用方错误语义）。
 *   - KIN-* 稳定诊断码＝**诊断内容**的码值，注册权威＝diagnostics
 *     StableCodeRegistry（§9.6、DiagCodes.hpp）；错误面→诊断面的换轨
 *     唯一经本头的映射数据行（mapping 数据，非产码动作）——本头不产出
 *     诊断记录，也禁把枚举 token 当字符串拼成诊断码（禁字符串拼码纪律）。
 *
 * 枚举值收编口径（为什么是这 4 值）：全量收编卡面 §9.2 接口契约已登记的
 * 域错误名——IFkEvaluator @错误 行原文
 * "KinematicsError{IllegalQ, NoDevice, NoTcp, FrameUnresolved}"（§5.2
 * 非法输入行的 IllegalQ 同源），枚举顺序＝该行原文序。T03+ 各接口落地时
 * 直接复用本表，不逐任务重开枚举（requirements 8 值收编同款先例）；
 * 持久化契约面纪律：只允许表尾追加并走单元卡增量修订（数值进入二进制
 * 契约，不重排既有值）。
 *
 * 映射数据的阶段纪律（与 DiagCodes.hpp §9.6 注册纪律对齐——acceptance 4
 * "注册义务随各消费任务展开"的执行口径）：映射行只登记到 §9.6 已到消费
 * 任务的码——§9.6 全表 15 行的任务列最早为 T03（KIN-NO-DEVICE/KIN-NO-TCP/
 * KIN-NEAR-SINGULAR），本任务（T02）零映射行，kinematicsDiagCode 当前对
 * 全表 4 值均返回 nullopt。nullopt 的调用方契约：不得产诊断——错误经
 * KinematicsError 值面返回；映射行随其生产者接口落位任务（T03 FK 族起）
 * 在 §9.6 纪律内表尾追加（NoDevice→KIN-NO-DEVICE、NoTcp→KIN-NO-TCP 为
 * §9.6 语义列同名锚定的在途行，届时登记），禁止私定新码值凑数。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token 为编译期固定表（switch 全枚举），同码同串、跨进程逐字节
 * 一致（NFR-COR-02 的码面子集）。
 */

#ifndef IRD_KINEMATICS_ERRORS_HPP
#define IRD_KINEMATICS_ERRORS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::kinematics {

// =====================================================================
// KinematicsErrorCode——kinematics 域稳定错误码全表（卡面 §9.2 IFkEvaluator
// @错误 行收编；枚举顺序＝卡面原文序，表尾追加纪律见文件头注）
// =====================================================================

/**
 * @brief kinematics 域稳定错误码全表（4 值——§9.2 @错误 行收编，逐值注释
 *        给出其产生接口与落位任务）。
 *
 * 底型 std::uint8_t：域错误面按卡面既有登记收编，后续任务表尾追加
 * （16 位以内充足且枚举体积恒定——requirements/modeling/io 同款取舍）。
 */
enum class KinematicsErrorCode : std::uint8_t {
    /// "IllegalQ"——关节向量非法：q 维度与设备自由度不符或含非有限分量
    /// （§5.2 非法输入行"q 维度不符/非有限→KinematicsError(IllegalQ)
    /// fail-fast（NFR-COR-03，不钳制不置零）"；§9.2 IFkEvaluator.evaluate
    /// @pre"q 维度=device DOF 且全部有限"的拒绝面——消费任务 T03）。
    IllegalQ,
    /// "NoDevice"——无可用设备（§9.2 @错误 行；§9.6 KIN-NO-DEVICE 行
    /// 同语义登记"无可用设备（NoDevice）"——消费任务 T03；映射行随
    /// T03 在 §9.6 纪律内登记，见文件头"映射数据的阶段纪律"）。
    NoDevice,
    /// "NoTcp"——TCP 未配置/悬空（§9.2 @错误 行；§9.6 KIN-NO-TCP 行
    /// 同语义登记"TCP 未配置/悬空（NoTcpFrame）"——消费任务 T03）。
    NoTcp,
    /// "FrameUnresolved"——坐标系引用无法解析（§9.2 @错误 行：tcpRef
    /// 指名的设备/法兰/TCP 帧在快照名称表中不存在——PA-3 名称解析语义，
    /// 本单元只消费快照不拼串定位；消费任务 T03）。
    FrameUnresolved,
};

/**
 * @brief 取错误码的稳定 token（枚举成员名原文串，如 "IllegalQ"）。
 *
 * token 与枚举成员一一对应（§9.2 契约行文即以成员名指称错误）；本函数
 * 是实现侧唯一映射点——头文件枚举注释与本函数若失同步，测试的全表机械
 * 比对即刻暴露（requirements errorCodeToken 同款防线）。返回值指向静态
 * 存储期字面量。
 *
 * 注意：本 token 是**域错误面**的判别串，不是 KIN-* 稳定诊断码（诊断码
 * 的注册权威＝diagnostics StableCodeRegistry——文件头"两个层面"注）。
 *
 * @param code [in] 错误码（全表 4 值均有 token——switch 全枚举、无
 *              default，新增枚举值未登记表项时编译器告警暴露遗漏）
 * @return 稳定 token（静态存储期）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同串）。
 */
std::string_view kinematicsErrorCodeToken(KinematicsErrorCode code) noexcept;

// =====================================================================
// KinematicsError——kinematics 错误值类型（§9.1 接口签名
// Expected<T, KinematicsError> 的 E 形态；"非异常出口"的值承载）
// =====================================================================

/**
 * @brief kinematics 域错误值类型：稳定码＋上下文参数＋原始细节。
 *
 * 形态与 requirements::RequirementError/modeling::ModelingError/io::IoError
 * 同款（码＋有序参数表＋开发级细节），原因同样成立：调用方输入类失败
 * （q 非法/设备缺失/TCP 悬空/帧未解析）是评估流程的**正常业务出口**，
 * 经值返回而不是异常穿越单元边界（§9.1 总则原文"纯函数服务非异常出口"）。
 * 异常轨（fail-fast 抛出）只留给调用方契约违约（如跨 snapshot 视图——
 * §9.2"非法调用"行），那是编程错误，不构造本类型。
 *
 * 生命周期/所有权：纯值类型，随返回值按值传递，调用方所有。
 *
 * 确定性（NFR-COR-01/02）：同输入同错误同 params 序——params 用保序
 * vector 而非关联容器，遍历序＝构造序，不经哈希/树序重排；数值参数在
 * 构造处按稳定格式化规则（定点、不经 locale）转文本。
 *
 * 敏感性约束（requirements/modeling/io 同款两级脱敏的源头防线）：detail
 * 与 params 只允许进入内部诊断构造链，成为用户可见文案前必须经
 * diagnostics 脱敏设施；本类型自身不做脱敏（职责分层——脱敏归
 * diagnostics）。
 */
struct KinematicsError {
    /**
     * 稳定错误码。缺省＝IllegalQ（枚举首值）：仅为聚合容器默认初始化
     * 留位（"失败清单"占位项语义），业务代码应总是经接口返回的
     * KinematicsError 携带语义明确的码，不手工构造缺省值当作有效错误。
     */
    KinematicsErrorCode code = KinematicsErrorCode::IllegalQ;

    /**
     * 上下文参数（键值对，构造序保序——确定性要求）。键的取值随码而异
     * （映射行随消费者任务登记时与 DiagCodes.hpp 同码 paramSchema 对齐；
     * 如 IllegalQ→expected-dof/actual-dof 等语义键随 T03 生产者落地时
     * 增量登记，本任务不预造参数名——diagnostics "不私造参数名"纪律）。
     * 值为字符串：敏感值入表前由生产方自评，最终用户可见文案必须经
     * diagnostics 脱敏。
     */
    std::vector<std::pair<std::string, std::string>> params;

    /// 原始细节（底层错误文本/定位上下文）。仅供内部诊断链与日志；不得
    /// 未经脱敏直接呈现（文件头"敏感性约束"）。
    std::string detail;
};

// =====================================================================
// 域错误→稳定诊断码映射数据（§9.6 表的 Errors.hpp 承载义务；映射阶段
// 纪律见文件头注——本任务零映射行）
// =====================================================================

/**
 * @brief 域错误码 → 已登记 KIN-* 稳定诊断码的映射（§9.6"注册义务随各
 *        消费任务展开——装配期注册"纪律在错误面的数据落点）。
 *
 * 返回 nullopt 的语义＝该域错误**当前没有已登记的映射码**——§9.6 全表
 * 15 行的任务列最早为 T03，本任务（T02）尚无任何已到消费任务的码行
 * （acceptance 4 分批纪律）。nullopt 的调用方契约：不得产诊断——错误经
 * KinematicsError 值面返回/呈现；产码唯一经 diagnostics 工厂（码已注册
 * 校验；"禁字符串拼码"纪律）。映射行随其生产者接口落位任务（T03 FK 族
 * 起）在 §9.6 纪律内表尾追加，测试交叉核对与 DiagCodes.hpp 工厂登记
 * 同源同串。
 *
 * @param code [in] 域错误码（全表 4 值均可入参——switch 全枚举）
 * @return 已登记稳定码文本（当前全表 4 值均 nullopt——文件头"映射数据
 *         的阶段纪律"；非 nullopt 时与 DiagCodes.hpp 码值常量同串）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02：同码同映射）。
 */
std::optional<std::string_view> kinematicsDiagCode(KinematicsErrorCode code) noexcept;

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_ERRORS_HPP
