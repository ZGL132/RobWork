/**
 * @file   Recompute.hpp
 * @brief  批量复算请求构造（KIN-08/§7.4）——"按原 slice 重新提交
 *         execution 新 run"的域侧请求值与构造函数（本单元保证重提交
 *         请求构造正确性，提交动作归 execution、触发方在插件/编排侧）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Recompute.hpp 行——T09 增列，
 *     登记随 §14.6 v0.9）、§7.4（"批量复算＝按原 slice 重新提交任务
 *     （execution 新 run）——不复用会话残留，结果当前性重新判定"）、
 *     §2.3（不拥有：任务调度/进程管理/缓存淘汰归 execution；结果当前性
 *     归 evidence）
 *   - REQUIREMENTS KIN-08（批量复算）、CON-02（结果完整性/当前性/工程
 *     判定正交；旧结果保留为原快照历史证据）、CON-05（当前性与失效基于
 *     切片内容身份）、AT-04（不产生修订的显式边界）
 *   - 任务契约 tasks/foundation/WP-15-T09.json acceptance 3
 *
 * 背景说明（§7.4 三要件的构造面落点，登记随卡 §14.6 v0.9）：
 *   1. "按原 slice 重新提交"——请求携带**原切片身份**（语义锚：复算的
 *      任务闭包定义来源）。切片本体是内容寻址值（CON-05）：新 run 的输入
 *      切片由执行管线自当前快照重推导——工程未变则重推导出同一切片内容
 *      身份（内容寻址的"按原"自动成立），已变更则新切片身份，两者皆由
 *      evidence 按条目级内容身份比对重新判定当前性（本请求**不携带**任
 *      何当前性结论——当前性正交，CON-02）。
 *   2. "不复用会话残留"——构造函数签名只接受显式值（原结果身份＋当前
 *      快照绑定），**不存在会话输入形参**：referenceQ 从原结果身份显式
 *      携带（D-KIN-4——禁止隐式读会话姿态，KIN-06/AT-04 边界），碰撞会
 *      话/模型视图由重提交装配方按新 run 重建，本值不承载任何会话句柄。
 *   3. "execution 新 run"——本值不携带旧 run 的 runId/attempt/结果载荷
 *      （类型面上无这些字段——结构性保证）；新 run 标识由 execution 接纳
 *      时分配。
 *
 * 线程安全：纯值/纯函数（无共享可变状态，§3.4 总约定）。
 */

#ifndef IRD_KINEMATICS_RECOMPUTE_HPP
#define IRD_KINEMATICS_RECOMPUTE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // ContentIdentity（切片/快照身份）
#include <sdurws/ird/core/Evaluation.hpp>  // EvaluationMode（模式词表）
#include <sdurws/ird/core/Identity.hpp>    // ObjectId（subject 对象引用）
#include <sdurws/ird/kinematics/KinTypes.hpp>  // IkRequestIdentity/IkTargetRef（来源值）

namespace sdurws::ird::kinematics {

// =====================================================================
// KinRecomputeRequest——批量复算请求值（§7.4 的构造面承载）
// =====================================================================

/**
 * @brief 批量复算请求值（重提交装配方据此经 execution 提交新 run；本
 *        值本身不提交任何东西——触发方在插件/编排侧，§7.4）。
 *
 * 值语义纯结构（全部成员值持有——拷贝/移动均安全）；线程安全（并发
 * 只读）。等值比较逐成员（无容差——身份面比较禁用数值容差）。
 */
struct KinRecomputeRequest {
    /// 原切片内容身份（"按原 slice 重新提交"的语义锚——复算任务闭包的
    /// 定义来源；新 run 输入切片由执行管线自当前快照重推导——CON-05
    /// 内容寻址，见文件头注第 1 条）。
    core::ContentIdentity sliceId;
    /// 重提交绑定的**当前**快照内容身份（调用方按当前会话装配——结果
    /// 当前性重新判定的输入面；不继承原结果的快照锚）。
    core::ContentIdentity snapshotId;
    /// 评估键（同键复算——原 run 的 kin-* 评估器键，如 kin-task-point-ik；
    /// execution 按当前注册表 descriptor 核对契约版本——本值不携带版本，
    /// 契约演进判 Superseded 归 evidence）。
    std::string evaluationKey;
    /// 目标对象引用（溯源/呈现面——单点复算携带任务点 oid；整批/区域
    /// 复算为 nullopt，即按原 slice 全闭包重跑）。
    std::optional<core::ObjectId> subjectOid;
    /// 评估模式（同键同模式复算——Quick 结果仅筛选依据，EVI-01）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 确定性种子（初值/采样策略的序列源——同原 run 同种子；0＝原 run
    /// 未用随机策略）。
    std::uint64_t seed = 0;
    /// 排序参考构型（rad|m；从原结果身份显式携带——D-KIN-4：禁止隐式
    /// 读会话姿态；重提交装配方如需换参考构型，走新评估请求而非本复算值
    /// ——显式性优先，KIN-06/AT-04 边界）。
    std::vector<double> referenceQ;

    bool operator==(const KinRecomputeRequest& o) const
    {
        return sliceId == o.sliceId && snapshotId == o.snapshotId
            && evaluationKey == o.evaluationKey && subjectOid == o.subjectOid
            && mode == o.mode && seed == o.seed && referenceQ == o.referenceQ;
    }
    bool operator!=(const KinRecomputeRequest& o) const { return !(*this == o); }
};

// =====================================================================
// buildKinRecomputeRequest——构造函数（构造正确性 UT 的被测面）
// =====================================================================

/**
 * @brief 从已归档/会话结果的身份构造批量复算请求（§7.4 三要件——文件
 *        头注逐条兑现）。
 *
 * 携带规则（构造正确性的断言面）：
 *   - sliceId/mode/seed/referenceQ **逐字段取自原结果身份**（复算同一
 *     评估定义——同键同模式同种子同参考构型）；
 *   - snapshotId 取调用方提供的**当前**绑定（参数直传——当前性重新判定
 *     的输入，不继承原结果）；
 *   - 目标对象引用 subjectOid 为调用方显式给定（可空）。
 *
 * @param original          [in] 原结果的请求身份（IkSolutionSet::
 *                          requestIdentity / 覆盖计算绑定块同构值；只读）
 * @param evaluationKey     [in] 原 run 评估键（kin-* 词表——kTaskPointIk-
 *                          EvaluationKey 等常量的值；非空）
 * @param currentSnapshotId [in] 当前快照绑定（非全零——重提交必须锚定
 *                          真实当前快照；当前性重新判定的输入面）
 * @param subjectOid        [in] 目标对象引用（可空——nullopt＝整闭包
 *                          复算；单点复算携带任务点 oid）
 * @return 复算请求值（字段携带规则见上；确定性——同输入同值）
 *
 * @throws std::invalid_argument 若 evaluationKey 为空串或 currentSnapshotId
 *         为全零保留值（调用方错误 fail-fast，§9.1——无键请求无法被
 *         execution 接纳、无快照锚的请求无法重推导切片，两者都没有
 *         "宁错带病提交"的合法形态）
 *
 * 纯函数；线程安全；确定性（NFR-COR-01/02）。
 */
KinRecomputeRequest buildKinRecomputeRequest(const IkRequestIdentity& original,
                                             std::string evaluationKey,
                                             const core::ContentIdentity& currentSnapshotId,
                                             std::optional<core::ObjectId> subjectOid
                                             = std::nullopt);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_RECOMPUTE_HPP
