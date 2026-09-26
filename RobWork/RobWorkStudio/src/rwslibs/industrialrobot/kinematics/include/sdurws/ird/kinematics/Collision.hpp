/**
 * @file   Collision.hpp
 * @brief  ④端口碰撞接入（KIN-05/AT-19/NFR-COR-05）——碰撞场景的请求方
 *         装配（CollisionScene）、真实 policy 会话的域侧适配器
 *         （IKinCollisionSession 消费形态）与会话接线入口（可用性三态）。
 *
 * 设计依据：
 *   - units/kinematics.md §8.1（与 policy 的交接——构型碰撞/任务级不可行
 *     边界图、"输入运行时对象"行"CollisionScene 由本单元按快照组装"、
 *     "取消/失败"行"碰撞设施异常→该样本/解标记 DataInsufficient＋诊断、
 *     不中断整批"、"策略版本进入输入身份"行）、§8.2（证据明细交付——
 *     {subjectPair, configurationRef, verdict} 随 EvaluationOutput.evidence）、
 *     §9.6（KIN-COLLISION-FILTERED/KIN-COLLISION-UNAVAILABLE 行——T07
 *     产码面）、§4.3（Conditional 依赖 collision-models——碰撞启用状态
 *     只读自 policy，V13-01）、§9.4（CollisionSession 生命周期行——每评估
 *     构建、工作线程使用）
 *   - policy 冻结契约（POL-T06/POL-T07 已落位）：policy::CollisionScene/
 *     SceneObjectEntry（§6.1"谁装配＝请求方"——kinematics 可依赖 runtime，
 *     装配归本单元）、policy::ICollisionEvaluator/makeRobWorkCollision-
 *     Evaluator（§9.3 唯一实现入口）、policy::CollisionEvaluationSession::
 *     evaluate（§9.3 冻结签名）、policy::CollisionQuery（§6.2——无阈值/
 *     模式参数，R-POL-5 结构防覆盖）
 *   - 需求 KIN-05（缺检测器/未启用碰撞→证据缺失，绝不视为无碰撞）、
 *     AT-19/NFR-COR-05（三入口一致——同一 resolved policy＋同一评估器
 *     实例语义经④端口保证）、R-POL-2（碰撞实现唯一归 policy——本单元
 *     禁直链 RobWork 邻近计算库，token 面零命中由契约测试静态扫描
 *     钉住）、R-POL-5（不传阈值/模式参数）、R-4（对象
 *     一律 ObjectId，名称解析只经⑥端口转发）、CON-06（策略版本经
 *     policy.resolved 依赖键＋快照 policyRef 入输入身份——策略变更→切片
 *     失效）
 *   - 治理裁决 O-37（宿主注入形态——本头 IKinCollisionSceneSource 为其
 *     碰撞侧落点：快照级事实经宿主适配器注入）；P-KIN-7（policy 会话
 *     契约以 policy.md v0.14 现行文本为基线——POL-T07 已落位，本头按
 *     冻结签名消费）
 *   - 任务契约 tasks/foundation/WP-15-T07.json（acceptance 1~5 全条）
 *
 * 背景说明（本头在碰撞链路中的位置——第一读者须知）：
 *   T04 交付了 IKinCollisionSession 最小消费端口与测试替身先行（P-KIN-7
 *   处置）；本头（T07）交付**真实④端口半区**：
 *     评估宿主（L5/worker 装配）持有注入面——
 *       ① IKinCollisionSceneSource：快照级事实（WC 共享只读别名＋WC 编译
 *          身份＋⑥端口名称解析转发）——宿主以适配器实现（快照指针别名
 *          构造归宿主，CR-04 裁决）；
 *       ② resolved policy（EngineeringPolicySet——④端口 policy.resolved
 *          依赖键的解析产物）＋ ICollisionEvaluator 实例（唯一实现入口）。
 *     本单元（请求方）执行——
 *       ③ assembleCollisionScene：把注入事实＋规范模型装配为
 *          policy::CollisionScene（对象一律 ObjectId——R-4）；
 *       ④ makeCollisionSession：读 policy 启用状态（唯一开关来源，V13-01
 *          ——本单元不设碰撞布尔开关）→ 构建会话（经唯一实现入口）→
 *          产出可用性三态；
 *       ⑤ PolicyCollisionSessionAdapter：把 policy 会话适配为 T04 端口
 *          （构型级二值判定＋对象对明细）——每查询直穿会话，零本地副本、
 *          零二次缓存（AT-19/V-25，静态扫描＋运行断言双防线）。
 *   IK 硬过滤③（Ik.cpp）消费⑤的判定值；碰撞证据明细经批量组装器交付
 *   （Evidence.hpp kKinBatchCollisionRowId——acceptance 4）。
 *
 * 实现纪律（红线锚点）：
 *   - 本头对 rw 类型**仅经 policy::CollisionEvaluator.hpp 的前向声明间接
 *     可见、零直接 rw include**（T02 门禁六边——policy 边的首次真实消费；
 *     冒烟模式无任何 TU include 本头，集成模式专属 TU 见 src/Collision.cpp
 *     的 gating）；
 *   - 本单元**零 proximity 消费**（R-POL-2）：碰撞检测的全部实现细节
 *     （策略/几何注册/逐对查询）都在 policy 会话内——本头类型面只有
 *     ObjectId 级场景事实与判定值；
 *   - 诊断码只消费 DiagCodes.hpp 在册常量（KIN-COLLISION-FILTERED/
 *     KIN-COLLISION-UNAVAILABLE——禁字符串拼码）；码的注册权威仍在
 *     diagnostics（本单元产出描述符清单——T02 面，不变）。
 *
 * 线程安全：IKinCollisionSceneSource 实现须并发只读安全（§9.7 注入接口
 * 行）；PolicyCollisionSessionAdapter 不可变（构造后只读——会话本身
 * "构造后只读、可跨线程共享"，policy §9.3），并发布为不可变共享形态。
 * 确定性：同 (场景, 策略, 名称映射, 构型) → 同判定（会话确定性 §6.4 的
 * 直通传导——适配器无随机源、无聚合）。
 */

#ifndef IRD_KINEMATICS_COLLISION_HPP
#define IRD_KINEMATICS_COLLISION_HPP

#include <memory>
#include <optional>
#include <string>

#include <sdurws/ird/core/Identity.hpp>              // ObjectId/ContentIdentity
#include <sdurws/ird/kinematics/Errors.hpp>          // KinematicsError（Expected 错误侧）
#include <sdurws/ird/kinematics/Ik.hpp>              // IKinCollisionSession/IkCollisionVerdict（T04 端口）
#include <sdurws/ird/kinematics/KinTypes.hpp>        // IKinRuntimeView（注入视图）
#include <sdurws/ird/policy/CollisionEvaluator.hpp>  // CollisionScene/ICollisionEvaluator/
                                                     //   CollisionEvaluationSession/EngineeringPolicySet
#include <sdurws/ird/runtime/Errors.hpp>             // runtime::Expected（非抛出查询轨）

namespace sdurws::ird::kinematics {

// =====================================================================
// IKinCollisionSceneSource——快照级事实的宿主注入端口（O-37 碰撞侧落点）
// =====================================================================

/**
 * @brief 碰撞场景装配所需的快照级事实端口（§8.1"按快照组装"的注入面；
 *        O-37 裁决同款纪律——宿主构建、经工厂闭包/请求装配注入）。
 *
 * 语义边界（IKinRuntimeView 同款——不新增对端语义）：本接口只投影
 * CollisionScene 装配真正需要而 IKinRuntimeView 不承载的快照事实：
 *   - sharedWorkCell()：快照 WC 编译产物的**共享只读别名**——宿主以
 *     "快照 shared_ptr＋WC 视图引用"别名构造（CR-04 裁决原文），别名
 *     所有权保证会话存续期编译产物不可变且不被释放（policy §6.1 只读
 *     生命周期第一层）；返回空指针＝宿主适配器契约违约（快照
 *     hasWorkCell 恒真——fail-fast）；
 *   - workCellCompileIdentity()：场景内容身份取值（CR-04：＝runtime WC
 *     层编译缓存键——policy 不重算，本单元值传递）；
 *   - tryObjectId/tryRuntimeName：⑥端口名称解析转发（IPolicyNameContext
 *     的对端；**只转发 runtime 解析结果**——本单元与适配器都不得拼接/
 *     剥离名称前缀，R-4/P-POL-8）；不可解析→nullopt（不猜测，ARC-04）；
 *   - nameMapIdentity()：名称映射内容身份（CON-06——映射变化可观测，
 *     进会话身份）。
 *
 * 生命周期与线程：宿主持有并保证装配/会话构建期间存活（非 owning——
 * 与视图同请求生命周期，O-37 裁决"视图随请求生命周期"）；实现须并发
 * 只读安全（§9.7 注入接口行）。
 */
class IKinCollisionSceneSource {
public:
    virtual ~IKinCollisionSceneSource() = default;

    /// 快照 WC 编译产物的共享只读别名（CR-04 别名构造——见类注；空指针
    /// ＝宿主适配器契约违约）。
    virtual std::shared_ptr<const rw::models::WorkCell> sharedWorkCell() const = 0;

    /// 场景内容身份（CR-04：值＝workCellCompileIdentity——policy 不重算）。
    virtual core::ContentIdentity workCellCompileIdentity() const = 0;

    /// 运行时整名 → 对象身份（⑥端口转发——R-4 禁拼拆；不可解析→nullopt）。
    virtual std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const = 0;

    /// 对象身份 → 运行时整名（⑥端口转发；不可解析→nullopt）。
    virtual std::optional<std::string> tryRuntimeName(const core::ObjectId& object) const = 0;

    /// 名称映射内容身份（CON-06——进会话身份）。
    virtual core::ContentIdentity nameMapIdentity() const = 0;
};

// =====================================================================
// assembleCollisionScene——请求方场景装配（policy.md §6.1"谁装配"行）
// =====================================================================

/**
 * @brief 把注入事实＋规范模型装配为 policy::CollisionScene（§8.1"输入
 *        运行时对象"行的唯一执行点——请求方装配者语义）。
 *
 * 装配规则（全部对象一律 ObjectId——R-4；确定性：同模型同注入事实→
 * 同场景值，NFR-COR-02）：
 *   - workcell ← source.sharedWorkCell()（共享只读别名——会话保活）；
 *   - sceneContentIdentity ← source.workCellCompileIdentity()（CR-04）；
 *   - primaryDevice ← 规范模型主链的机器人对象身份；
 *   - objects ← 规范模型三段投影（对象身份/局部名（显示辅助，CON-06
 *     不作身份）/角色/几何事实）：
 *       链连杆 → Role=RobotLink、hasCollisionGeometry=连杆碰撞几何引用
 *       在场；工具 → Role=Tool、hasCollisionGeometry=工具几何引用在场；
 *       场景对象 → Role=EnvironmentObject、hasCollisionGeometry=true
 *       （规范模型场景几何必有——CanonicalSceneObject 契约）；
 *   - adjacentLinkPairs ← 模型相邻事实：链序相邻对 (link_i, link_{i+1})
 *     ＋工具安装对 (链末连杆, tool_i)（MDL-13 工具经法兰安装——默认
 *     相邻过滤的模型事实消费源，policy §7.1）。
 *
 * 错误分轨（§9.1"调用方错误 fail-fast；环境错误走诊断/素材"）：
 *   - source.sharedWorkCell() 为空＝宿主适配器契约违约（编程错误）——
 *     抛 std::invalid_argument（fail-fast，不产出半装配场景）；
 *   - 规范模型无主链（链空）＝环境事实（无设备模型无碰撞评估意义）——
 *     Expected 错误 KinematicsError{NoDevice}（装配不可行素材，调用方
 *     据此走 KIN-COLLISION-UNAVAILABLE 证据缺失轨，不抛）。
 *
 * @param view   [in] 请求绑定的只读模型视图（规范模型事实源；非 owning，
 *                    调用期间存活；空指针由调用方契约保证不出现——同
 *                    评估器构造口径）
 * @param source [in] 快照级事实端口（非 owning——装配期间存活）
 * @return 装配成功的场景值（policy::CollisionScene——值聚合，WC 为共享
 *         只读别名）或 NoDevice 错误
 *
 * @throws std::invalid_argument source.sharedWorkCell() 为空（宿主适配器
 *         契约违约）
 *
 * 纯函数（不修改 view/source）；线程安全（无共享可变状态）；确定性
 * （同输入同场景——对象清单按规范模型既定序投影，无重排）。
 */
runtime::Expected<policy::CollisionScene, KinematicsError>
assembleCollisionScene(const IKinRuntimeView& view, const IKinCollisionSceneSource& source);

// =====================================================================
// PolicyCollisionSessionAdapter——真实④端口会话的域侧适配器
// =====================================================================

/**
 * @brief 把 policy::CollisionEvaluationSession 适配为 T04 消费端口
 *        IKinCollisionSession（§8.1④端口语义的执行件——KIN-05 判定值
 *        与对象对明细的唯一供给点）。
 *
 * 形态要点（acceptance 1/3 逐项）：
 *   - **每查询直穿会话**：evaluate(q) 构造 policy::CollisionQuery
 *     {SingleState, 1 构型} 后立即调用会话 evaluate——适配器无可变状态、
 *     无记忆化、无本地判定副本（三入口一致 AT-19/V-25 的运行断言对象：
 *     N 次查询→N 次底层后端查询）；查询**不携带任何阈值/模式参数**
 *     （R-POL-5——CollisionQuery 成员清单层面无阈值，D-9 结构防覆盖）；
 *   - **构型级二元消费**（构型级碰撞评估作用域——§8.1）：仅消费
 *     Collision 种类发现（构型级碰撞→该解过滤，C8）；SafetyMargin
 *     Violation（间距不足——policy §7.4"非碰撞"）不过滤该解，其阈值
 *     语义归 policy/汇总面（本适配器的判定词表只有碰撞二值）；
 *   - **对象对一律 ObjectId**：判定明细取发现对象对（policy 规范序
 *     A<B），本适配器不做任何名称拼装（R-4）；
 *   - **失败三态映射**（§8.1 取消/失败行——acceptance 2）：
 *       Completed＋Applicable → Evaluated（findings 非空⇔碰撞）；
 *       Completed＋EmptyScope/CollisionDisabledByPolicy → EvidenceMissing
 *       （空作用域≠无碰撞、禁用被误调用≠无碰撞——KIN-05）；
 *       Canceled → EvidenceMissing（非终态输出不得当正式判定——TASK-02/
 *       CON-04）；Failed → FacilityFailed（会话诊断 cause 入 statusDetail
 *       ——该解 DataInsufficient 素材，不中断整批）。
 *   - 调用上下文：恒存活＋无取消（单构型查询原子——样本边界取消窗在
 *     单样本查询中不存在；取消经 IkRequest.cancellationProbe 在求解
 *     循环承载，§8.3 批间/逐迭代粒度归求解器与宿主通道）。
 *
 * 生命周期与线程：会话以 shared_ptr<const> 持有（可跨任务复用同会话——
 * 同 resolved policy＋同一评估器实例语义的载体）；适配器构造后只读，
 * 并发只读安全；由请求装配方（评估宿主）构造，随请求销毁（§9.4
 * CollisionSession 行"每评估构建、评估结束销毁"）。
 */
class PolicyCollisionSessionAdapter final : public IKinCollisionSession {
public:
    /**
     * @brief 以已构建的 policy 会话构造适配器（接线入口 makeCollision-
     *        Session 的产物；直构仅供测试——生产路径经接线入口）。
     *
     * @param session [in] 不可变碰撞会话（非空——空指针＝调用方契约
     *                     违约，fail-fast）
     *
     * @throws std::invalid_argument session 为空
     */
    explicit PolicyCollisionSessionAdapter(
        std::shared_ptr<const policy::CollisionEvaluationSession> session);

    /**
     * @brief 评价单个构型的碰撞状态（§8.1④端口——每查询直穿会话）。
     *
     * @param q [in] 权威关节向量（rad／m；链序——与 IK 请求同序）
     * @return 判定值（三态映射见类注；objectIdPairs 仅 Evaluated＋
     *         inCollision 时非空）
     *
     * 纯查询（适配器无可变状态）；确定性（同会话同 q 同判定——会话
     * §6.4 纯度直通）；不抛（会话评估内部异常已转 Failed＋诊断——映射
     * 为 FacilityFailed 素材；查询契约违约〔q 维度不符〕由会话以
     * PolicyError fail-fast 抛出——调用方错误轨原样传播）。
     */
    IkCollisionVerdict evaluate(const std::vector<double>& q) const override;

    /// 适配的不可变会话（观测面——三入口一致断言与证据绑定消费）。
    const policy::CollisionEvaluationSession& session() const { return *m_session; }

    /// 会话身份（§6.4——f(policy, scene, nameMap, backend)；观测面转发）。
    core::ContentIdentity sessionIdentity() const { return m_session->sessionIdentity(); }

    /// 会话绑定的策略内容身份（CON-06 观测面——policy.resolved 解析产物
    /// 的版本可观测；策略变更→新会话身份→切片失效）。
    core::ContentIdentity policyContentIdentity() const
    {
        return m_session->policy().contentIdentity;
    }

private:
    /// 适配的不可变会话（构造期非空——唯一成员；共享所有权随适配器）。
    std::shared_ptr<const policy::CollisionEvaluationSession> m_session;
};

// =====================================================================
// CollisionSessionWiring／makeCollisionSession——④端口会话接线入口
// =====================================================================

/**
 * @brief 会话接线的可用性三态（acceptance 2/5 的判定面——碰撞启用状态
 *        只读自 policy，V13-01；本单元不设碰撞布尔开关）。
 */
enum class CollisionSessionAvailability : std::uint8_t {
    /// 会话就绪（policy 启用碰撞且④端口会话构建成功——session 非空）。
    SessionReady,
    /// 策略未启用碰撞（policy.collision.enabled==false——唯一读取源；
    /// session 恒空；碰撞检查不在范围，非降级）。
    PolicyDisabled,
    /// 检测器不可用（评估器未注入/场景装配不可行/会话构建失败——
    /// KIN-COLLISION-UNAVAILABLE 证据缺失轨；session 恒空）。
    DetectorUnavailable,
};

/**
 * @brief 会话接线结果（makeCollisionSession 的值承载——评估宿主据此
 *        填充查询的 collisionSession 指针与诊断素材）。
 *
 * 值语义纯结构；线程安全（并发只读）。session 仅 SessionReady 态非空；
 * detail 仅 DetectorUnavailable 态非空（开发级细节——进诊断 cause 前须
 * 经 diagnostics 脱敏）。
 */
struct CollisionSessionWiring {
    /// 可用性三态（判定语义见枚举注）。
    CollisionSessionAvailability availability = CollisionSessionAvailability::PolicyDisabled;
    /// 适配器（T04 端口实现——直接可填入 IkRequest/查询的碰撞会话指针；
    /// 非 SessionReady 恒空）。
    std::shared_ptr<const PolicyCollisionSessionAdapter> session;
    /// 策略内容身份（CON-06——resolved policy 的版本观测；PolicyDisabled
    /// 态同样回填——"策略未启用"本身是策略版本相关事实）。
    core::ContentIdentity policyContentIdentity;
    /// 不可用原因素材（DetectorUnavailable 非空；其余态为空串）。
    std::string detail;
};

/**
 * @brief ④端口会话接线（acceptance 1 的装配入口——评估宿主按请求调用；
 *        把注入事实装配为场景（assembleCollisionScene）并经唯一实现
 *        入口构建会话，产出可用性三态）。
 *
 * 接线序（固定——确定性）：
 *   1. 读 policy 启用状态（唯一开关来源——policy.collision.enabled，
 *      V13-01）：未启用→{PolicyDisabled, 空}（会话不构建——"策略未
 *      启用不进切片"的查询面形态：collisionSession 指针为空）；
 *   2. 评估器空指针→{DetectorUnavailable, detail="碰撞评估器未注入"}；
 *   3. assembleCollisionScene：NoDevice→{DetectorUnavailable, detail=
 *      域错误细节}（环境事实——证据缺失轨）；宿主适配器违约（空 WC）
 *      →异常原样传播（fail-fast，编程错误不入素材轨）；
 *   4. evaluator->createSession(policy, scene, names)：PolicyError
 *      （场景校验/名称不可解析/规则冲突——§6.1 会话构建三步）→
 *      {DetectorUnavailable, detail=异常原因}（数据/环境错误——证据
 *      缺失轨，不猜测不静默收窄）；成功→{SessionReady, 适配器}。
 *
 * 名称上下文：内部以 source 端口转发的 IPolicyNameContext 适配形态
 * 提供（只转发 runtime 解析结果——R-4；适配归请求方，policy.md §9.7
 * "适配器归 L5/请求方"）。
 *
 * @param view          [in] 请求绑定的只读模型视图（规范模型事实源）
 * @param sceneSource   [in] 快照级事实端口（非 owning——接线期间存活；
 *                          产出的会话经共享别名保活编译产物，端口本身
 *                          会话构建后不再使用——名称解析已固化进会话）
 * @param resolvedPolicy [in] 已发布策略（policy.resolved 依赖键的解析
 *                          产物——Valid 态；禁用态合法，走 PolicyDisabled）
 * @param evaluator     [in] 碰撞评估唯一实现入口（§9.3——进程级共享
 *                          实例；空＝检测器不可用轨）
 * @return 接线结果（三态——见 CollisionSessionWiring 注）
 *
 * @throws std::invalid_argument sceneSource.sharedWorkCell() 为空（宿主
 *         适配器契约违约——fail-fast）
 *
 * 纯函数；线程安全（无共享可变状态——会话经 evaluator 并发构建安全，
 * policy §9.3）；确定性：同输入→同三态与会话等价（会话构建确定性
 * §6.4 直通）。
 */
CollisionSessionWiring
makeCollisionSession(const IKinRuntimeView& view, const IKinCollisionSceneSource& sceneSource,
                     const policy::EngineeringPolicySet& resolvedPolicy,
                     const policy::ICollisionEvaluator* evaluator);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_COLLISION_HPP
