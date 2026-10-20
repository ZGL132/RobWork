/**
 * @file   CollisionSession.hpp
 * @brief  运行时真实碰撞设施（MA-02——主线功能审计 20261021 立项批）。
 *
 * 设计依据：
 *   - 主线功能审计 REPORT.md MA-02（"正式宿主未装配真实碰撞设施——启用
 *     策略/需求无碰撞要求后只能 EvidenceMissing/DataInsufficient；诚实
 *     降级正确，但所需检测能力没有交付"）
 *   - units/runtime.md §5.2 S6（编译产物 WorkCell/Device——本设施的被测
 *     场景载体）；§8.6（资源几何挂接——MA-01 真实 mesh 即碰撞几何）
 *   - policy §5.2（CollisionRules：safetyClearance/相邻连杆过滤/必须对/
 *     忽略对——本会话的判定规则面；runtime 不依赖 policy 单元——规则值
 *     由宿主装配层投影传入，R-1 端口纪律）
 *
 * 背景说明：审计前宿主在策略启用碰撞检查时只能装配 EvidenceMissing 桩
 * （KIN-05：绝不视为无碰撞）。本文件交付**同一真实场景**的构型级碰撞
 * 判定：rw::proximity::CollisionDetector（PQP 窄相策略）在编译产物
 * WorkCell 上按构型 setQ 后判定，产出碰撞对象对明细。消费链＝宿主装配
 * 层（ui/L5）把本会话适配为 kinematics IKinCollisionSession（业务域互
 * 链禁止——runtime 不 include kinematics，R-1）。
 *
 * 线程安全：make 后并发 evaluate 只读（CollisionDetector::inCollision
 * const＋每次调用线程私有 State 值拷贝）；实例不可变。
 * 确定性：同快照同规则同 q 同判定（PQP 确定性窄相——同几何同构型同
 * 结果）；碰撞对输出序＝检测器返回序（过滤保序）。
 */

#ifndef SDURWS_IRD_RUNTIME_COLLISIONSESSION_HPP
#define SDURWS_IRD_RUNTIME_COLLISIONSESSION_HPP

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>   // ObjectId（碰撞对象对明细键）
#include <sdurws/ird/runtime/Snapshot.hpp>  // RuntimeSnapshot（场景载体＋名称映射）

namespace sdurws::ird::runtime {

/**
 * @brief 碰撞对规则值（宿主装配层从 policy PairRule/ScopeTarget 投影——
 *        Object 类别目标携 ObjectId 直映；非 Object 类别目标由装配层
 *        跳过，不私裁语义）。值语义纯结构。
 */
struct RuntimeCollisionPairRule {
    core::ObjectId first;   ///< 对的第一方对象身份（无序对——两侧可交换）
    core::ObjectId second;  ///< 对的第二方对象身份
};

/**
 * @brief 碰撞判定规则值（宿主装配层从 policy CollisionRules 投影——
 *        runtime 零 policy 依赖，R-1）。全部字段语义与 policy §5.2 一致：
 *   - safetyClearanceM      安全间距阈值，单位 m，[0,+∞)——0＝纯相交判定
 *                          （>0 时"距离小于阈值即判碰撞"的容差语义由
 *                          上层以容差策略承载——本批先交付 0 阈值纯相交，
 *                          >0 阈值面随验收批实测登记，不虚报已实现）；
 *   - excludeAdjacentLinksByDefault  相邻连杆默认过滤（链序 |i-j|==1）；
 *   - mandatoryPairs        必须检测对（本会话不过滤其判定——结构保证）；
 *   - excludedPairs         允许忽略对（命中即从明细剔除）。
 * 值语义纯结构；线程安全。
 */
struct RuntimeCollisionRules {
    double safetyClearanceM = 0.0;                 ///< 安全间距阈值（m；本批承载 0）
    bool excludeAdjacentLinksByDefault = true;     ///< 相邻连杆默认过滤
    std::vector<RuntimeCollisionPairRule> mandatoryPairs;  ///< 必须检测对
    std::vector<RuntimeCollisionPairRule> excludedPairs;   ///< 允许忽略对
};

/**
 * @brief 运行时真实碰撞会话（构型级判定——§8.1④端口的 runtime 侧设施）。
 *
 * 生命周期：make 持有快照 shared_ptr（场景载体＋名称映射同寿）；实例
 * 不可变、可并发 evaluate。构造失败面（设备缺失/无碰撞几何/自由度
 * 异常）＝make 返回 null——宿主据此回落 EvidenceMissing 诚实降级
 * （KIN-05 语义保持：设施不可用绝不冒充无碰撞）。
 */
class RuntimeCollisionSession {
public:
    /// 单构型判定值（构型级；对象对＝ObjectId 对明细——映射不到 ObjectId
    /// 的帧对按"明细缺失"省略，不影响 inCollision 布尔判定）。
    struct Verdict {
        bool evaluated = false;    ///< true＝判定有效（false＝设施异常/自由度失配）
        bool inCollision = false;  ///< 过滤后仍存在碰撞对（evaluated 时有效）
        std::vector<std::pair<core::ObjectId, core::ObjectId>>
            objectIdPairs;         ///< 碰撞对象对（成对展平——映射可达部分）
    };

    /**
     * @brief 构造真实碰撞会话（快照场景＋PQP 窄相＋规则面）。
     *
     * 构造内容：CollisionDetector（WC 全几何——MA-01 真实 mesh 与既有
     * 图元代理均入检测；碰撞几何与呈现几何同载体双 facet 时对判定冗余
     * 但不改变布尔结论）；链序连杆帧→ObjectId 映射（相邻/忽略对规则
     * 的解析键）；默认状态模板。
     *
     * @param snapshot [in] 编译快照（场景载体；null＝返回 null）
     * @param rules [in] 判定规则（宿主从 policy 投影——见类型注）
     * @return 会话实例；设施不可用（设备缺失/零几何）＝null（诚实降级
     *         面——调用方回落 EvidenceMissing，不虚构无碰撞）
     */
    static std::shared_ptr<RuntimeCollisionSession>
        make(std::shared_ptr<const RuntimeSnapshot> snapshot,
             const RuntimeCollisionRules& rules);

    /**
     * @brief 评价单个构型（§8.1④端口语义——构型级、纯查询、确定性）。
     * @param q [in] 权威关节向量（rad/m；链序——与 IK 请求同序）
     * @return 判定值（evaluated=false＝自由度失配/设施异常——调用方按
     *         FacilityFailed 语义消费，不中断整批）
     */
    Verdict evaluate(const std::vector<double>& q) const;

    /// 析构（默认——成员声明序承载释放序：检测器先于快照）。
    ~RuntimeCollisionSession();

    RuntimeCollisionSession(const RuntimeCollisionSession&) = delete;            ///< 不可拷贝
    RuntimeCollisionSession& operator=(const RuntimeCollisionSession&) = delete; ///< 不可赋值

private:
    RuntimeCollisionSession() = default;  ///< 仅 make 构造（见 make 注释）

    /// 实现状态（pimpl——rw proximity 头不进本公共头，消费面零传递）。
    struct Impl;
    Impl* m_impl = nullptr;  ///< 实现所有者（make 构造／析构释放）
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_COLLISIONSESSION_HPP
