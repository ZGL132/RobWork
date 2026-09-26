/**
 * @file   Collision.cpp
 * @brief  ④端口碰撞接入的实现（WP-15-T07）——请求方场景装配、⑥端口
 *         名称上下文适配、恒定调用上下文、policy 会话→域端口适配器与
 *         会话接线入口（可用性三态）。
 *
 * 设计依据：
 *   - units/kinematics.md §8.1（与 policy 的交接——装配者/取消失败/
 *     策略版本/证据明细各行）、§9.6（KIN-COLLISION-* 产码素材面）、
 *     §9.4（CollisionSession 生命周期行）
 *   - policy 冻结契约：CollisionEvaluator.hpp §6.1（CollisionScene 装配
 *     契约——CR-04 字段映射）/§9.3（ICollisionEvaluator/createSession）、
 *     CollisionQuery.hpp §6.2（查询/发现/输出词表——Collision 与
 *     SafetyMarginViolation 两分）、Contexts.hpp §9.7（IPolicyNameContext/
 *     IPolicyCallContext 注入契约——适配器归请求方）
 *   - 需求 KIN-05/AT-19/NFR-COR-05；R-POL-2/R-POL-5/R-4/CON-06（红线
 *     逐条——见 Collision.hpp 文件头"实现纪律"）
 *   - 任务契约 tasks/foundation/WP-15-T07.json（acceptance 1/2/3）
 *
 * 背景说明（为什么本 TU 是集成模式专属）：本 TU 消费 policy 公共头的
 * 完整类型面（CollisionEvaluationSession 构造/evaluate 调用点）与
 * runtime 规范模型访问器——policy 侧同因 TU（CollisionQuery.cpp 等）
 * 已按 `if(TARGET sdurw_kinematics)` gating（框架库目标只在集成配置树
 * 存在）；冒烟模式下无任何 TU include Collision.hpp（公共头 include 面
 * 不影响冒烟口径——policy/CollisionEvaluator.hpp 同款登记口径）。
 *
 * 线程安全：本 TU 全部实体为无状态服务或构造后只读（适配器持不可变
 * 会话的 shared_ptr）——无共享可变状态。确定性：装配按规范模型既定序
 * 投影、接线序固定——同输入同结果（NFR-COR-02）。
 */

#include <sdurws/ird/kinematics/Collision.hpp>

#include <stdexcept>
#include <utility>

#include <rw/math/Q.hpp>

#include <sdurws/ird/policy/CollisionQuery.hpp>  // CollisionQuery/CollisionEvaluation/
                                                 //   CollisionFindingKind（§6.2 值面）
#include <sdurws/ird/policy/Contexts.hpp>        // IPolicyNameContext/IPolicyCallContext
                                                 //   （注入契约——适配器归请求方）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // CanonicalModel 三段投影（装配事实源）

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// SceneSourceNameContext——⑥端口名称上下文适配（IPolicyNameContext）
// =====================================================================

/**
 * @brief 以 IKinCollisionSceneSource 为应答源的 IPolicyNameContext 适配
 *        （policy.md §9.7"适配器归 L5/请求方"的请求方侧落点）。
 *
 * 只读转发纪律（R-4/P-POL-8）：三方法都原样转发 source 的应答——
 * nullopt 呈现为 nullopt（不可解析不猜测，ARC-04），**零**名称前缀
 * 拼接/剥离逻辑（红线由本适配器结构保证——转发体无字符串加工）。
 * 线程安全：转发实现无状态（source 保证并发只读安全——端口契约）。
 */
class SceneSourceNameContext final : public policy::IPolicyNameContext {
public:
    /// 以注入端口构造（非 owning——接线期间存活，构建完成后不再使用）。
    explicit SceneSourceNameContext(const IKinCollisionSceneSource& source) : m_source(source) {}

    /// 运行时整名→对象（⑥端口转发——nullopt 原样呈现，不猜测）。
    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override
    {
        return m_source.tryObjectId(runtimeName);
    }

    /// 对象→运行时整名（⑥端口转发——nullopt 原样呈现）。
    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        return m_source.tryRuntimeName(object);
    }

    /// 名称映射内容身份（CON-06——进会话身份）。
    core::ContentIdentity nameMapContentIdentity() const override
    {
        return m_source.nameMapIdentity();
    }

private:
    /// 注入端口（非 owning——接线/会话构建期间存活即可：解析结果在会话
    /// 构建期固化，policy §9.3"names 借用——构建期存活即可"）。
    const IKinCollisionSceneSource& m_source;
};

// =====================================================================
// AdapterCallContext——恒定调用上下文（IPolicyCallContext）
// =====================================================================

/**
 * @brief 构型级查询的恒定调用上下文（存活＝真、取消＝否）。
 *
 * 为什么恒定：本适配器只承载 SingleState 单构型查询——policy 会话的
 * 协作取消检查点在**样本边界**（policy §6.6），单样本查询内部不存在
 * 取消窗；运动学侧的取消（§8.3"每批/每 N 次迭代查询一次"）由
 * IkRequest.cancellationProbe 在求解循环承载（批间/逐迭代粒度归求解
 * 器与宿主通道），不经碰撞会话重复传递。
 * alive()＝真的语义依据：适配器随请求构建/销毁（§9.4"每评估构建、
 * 评估结束销毁"）——任何经适配器到达的调用必然处于请求存活期内，
 * "迟到调用拒绝"（POL-LATE-1）由请求生命周期结构性排除。
 * 线程安全：无状态（全 const）。
 */
class AdapterCallContext final : public policy::IPolicyCallContext {
public:
    /// 恒假（单构型查询原子——见类注取消语义）。
    bool cancellationRequested() const override { return false; }

    /// 恒真（适配器存活期＝请求存活期——见类注存活语义）。
    bool alive() const override { return true; }
};

/// 调用上下文单例（无状态——共享实例即可，省每次查询构造）。
const AdapterCallContext kAdapterCallContext{};

}  // namespace

// =====================================================================
// assembleCollisionScene——请求方场景装配
// =====================================================================

runtime::Expected<policy::CollisionScene, KinematicsError>
assembleCollisionScene(const IKinRuntimeView& view, const IKinCollisionSceneSource& source)
{
    // 第一步：快照 WC 共享只读别名（CR-04——宿主别名构造；空指针＝宿主
    // 适配器契约违约——快照 hasWorkCell 恒真，编程错误 fail-fast）。
    std::shared_ptr<const rw::models::WorkCell> workcell = source.sharedWorkCell();
    if (!workcell) {
        throw std::invalid_argument(
            "assembleCollisionScene：IKinCollisionSceneSource::sharedWorkCell() 为空"
            "（宿主适配器契约违约——快照 hasWorkCell 恒真，fail-fast）");
    }

    // 第二步：主链事实（primaryDevice 与相邻对的模型事实源；链空＝环境
    // 事实——无设备模型无碰撞评估意义，Expected 错误轨——调用方据此走
    // KIN-COLLISION-UNAVAILABLE 证据缺失素材，不抛）。
    const runtime::CanonicalModel& model = view.model();
    const runtime::RobotChain& chain = model.chain();
    if (chain.links.empty()) {
        KinematicsError err;
        err.code = KinematicsErrorCode::NoDevice;
        err.params.emplace_back("reason", "collision-scene-assembly");
        err.detail = "碰撞场景装配：规范模型无主链（无可用设备——KIN-NO-DEVICE "
                     "同语义；碰撞评估不可行素材）";
        return runtime::Expected<policy::CollisionScene, KinematicsError>::err(std::move(err));
    }

    // 第三步：装配场景值（policy::CollisionScene §6.1 五字段；对象一律
    // ObjectId——R-4；清单按规范模型三段既定序投影，无重排——确定性）。
    policy::CollisionScene scene;
    scene.workcell = std::move(workcell);
    scene.sceneContentIdentity = source.workCellCompileIdentity();  // CR-04 值传递
    scene.primaryDevice = chain.robotObjectId;

    // 3a. 链连杆段：Role=RobotLink；几何事实＝连杆碰撞几何引用在场
    // （KIN-05：无几何只登记事实、不收窄作用域——构建期不据此过滤）。
    for (const runtime::CanonicalLink& link : chain.links) {
        policy::SceneObjectEntry entry;
        entry.objectId = link.objectId;
        entry.localName = link.localName;  // 显示辅助（CON-06——不作身份）
        entry.role = policy::SceneObjectRole::RobotLink;
        entry.hasCollisionGeometry = link.collision.has_value();
        scene.objects.push_back(std::move(entry));
    }

    // 3b. 工具段：Role=Tool；几何事实＝工具几何引用在场（MDL-13——
    // 工具几何以 ToolDefinition 资源引用固化）。
    for (const runtime::CanonicalTool& tool : model.tools()) {
        policy::SceneObjectEntry entry;
        entry.objectId = tool.objectId;
        entry.localName = tool.localName;
        entry.role = policy::SceneObjectRole::Tool;
        entry.hasCollisionGeometry = tool.geometry.has_value();
        scene.objects.push_back(std::move(entry));
    }

    // 3c. 场景对象段：Role=EnvironmentObject；几何必有（CanonicalScene-
    // Object 契约"环境几何资源引用（必有）"——事实恒真）。
    for (const runtime::CanonicalSceneObject& object : model.scene()) {
        policy::SceneObjectEntry entry;
        entry.objectId = object.objectId;
        entry.localName = object.localName;
        entry.role = policy::SceneObjectRole::EnvironmentObject;
        entry.hasCollisionGeometry = true;
        scene.objects.push_back(std::move(entry));
    }

    // 3d. 相邻对（模型事实——policy §7.1 默认相邻过滤消费源；"由建模侧
    // 含工具安装对"）：链序相邻对 (link_i, link_{i+1})＋工具安装对
    // (链末连杆, tool_i)（MDL-13 工具经法兰安装的相邻事实投影）。
    for (std::size_t i = 1; i < chain.links.size(); ++i) {
        scene.adjacentLinkPairs.emplace_back(chain.links[i - 1].objectId,
                                             chain.links[i].objectId);
    }
    for (const runtime::CanonicalTool& tool : model.tools()) {
        scene.adjacentLinkPairs.emplace_back(chain.links.back().objectId, tool.objectId);
    }

    return runtime::Expected<policy::CollisionScene, KinematicsError>::ok(std::move(scene));
}

// =====================================================================
// PolicyCollisionSessionAdapter——真实④端口会话的域侧适配器
// =====================================================================

PolicyCollisionSessionAdapter::PolicyCollisionSessionAdapter(
    std::shared_ptr<const policy::CollisionEvaluationSession> session)
    : m_session(std::move(session))
{
    // 调用方契约违约 fail-fast：空会话无评估意义（编程错误——不产出
    // 空适配器静默通过，NFR-COR-03）。
    if (!m_session) {
        throw std::invalid_argument(
            "PolicyCollisionSessionAdapter：会话指针为空（调用方契约违约——"
            "fail-fast）");
    }
}

IkCollisionVerdict PolicyCollisionSessionAdapter::evaluate(const std::vector<double>& q) const
{
    // 查询装配（policy §6.2 六字段）：SingleState 恰 1 构型；基准状态
    // 缺省＝场景基准（编译产物默认态）；无距离请求（构型级二元判定，
    // 不做间距检查——阈值语义归 policy）；无提前终止（发现全量保留——
    // 对象对明细是证据交付素材）。★ 查询不携带任何阈值/模式参数
    // （R-POL-5——成员清单层面不存在，D-9 结构防覆盖）。
    policy::CollisionQuery query;
    query.kind = policy::CollisionQueryKind::SingleState;
    query.configurations.push_back(rw::math::Q(q));

    // 直穿会话（每查询恰一次底层调用——零缓存、零本地判定副本；
    // AT-19/V-25 运行断言的对象行为）。调用上下文恒定（见
    // AdapterCallContext 类注——单构型查询原子）。
    const policy::CollisionEvaluation evaluation =
        m_session->evaluate(query, kAdapterCallContext);

    // 三态映射（§8.1 取消/失败行——acceptance 2；状态机见 policy §6.3）。
    IkCollisionVerdict verdict;
    switch (evaluation.status) {
    case policy::CollisionEvaluationStatus::Completed: {
        if (evaluation.applicability == policy::ScopeApplicability::Applicable) {
            // 完成且适用：仅 Collision 种类发现构成"碰撞"（policy §7.4
            // 决策表——SafetyMarginViolation＝间距不足，非碰撞，不过滤
            // 该解；其阈值语义归 policy/汇总面）。对象对取发现规范序
            // （A<B——ObjectId，R-4：不拼装名称）。
            for (const policy::CollisionFinding& finding : evaluation.findings) {
                if (finding.kind != policy::CollisionFindingKind::Collision) {
                    continue;
                }
                verdict.inCollision = true;
                verdict.objectIdPairs.push_back(finding.objectA);
                verdict.objectIdPairs.push_back(finding.objectB);
            }
            verdict.state = IkCollisionEvaluationState::Evaluated;
        } else if (evaluation.applicability
                   == policy::ScopeApplicability::EmptyScope) {
            // 空作用域≠无碰撞（KIN-05——policy §6.2 原文口径由 coverage
            // 表达）：证据缺失轨，绝不产出"无碰撞"判定。
            verdict.state = IkCollisionEvaluationState::EvidenceMissing;
            verdict.statusDetail = "碰撞作用域为空（EmptyScope——无几何/无对可检，"
                                   "KIN-05：不视为无碰撞）";
        } else {
            // 策略禁用被误调用（接线已按 enabled 分流——到达此处＝接线
            // 缺陷或策略态漂移）：保守证据缺失轨（fail-safe——绝不当
            // 无碰撞放行）。
            verdict.state = IkCollisionEvaluationState::EvidenceMissing;
            verdict.statusDetail = "策略禁用被误调用（CollisionDisabledByPolicy——"
                                   "V13-01 启用状态只读自 policy；保守按证据缺失）";
        }
        break;
    }
    case policy::CollisionEvaluationStatus::Canceled:
        // 取消非终态（TASK-02/CON-04——非终态输出不得进正式证据）：证据
        // 缺失轨（求解器自身的取消轨由 IkRequest.cancellationProbe 承载
        // ——到达此处的取消是会话侧瞬时事实，按证据缺失保守处理）。
        verdict.state = IkCollisionEvaluationState::EvidenceMissing;
        verdict.statusDetail = "碰撞会话取消（Canceled——非终态，不产正式判定）";
        break;
    case policy::CollisionEvaluationStatus::Failed:
        // 设施异常（RobWork 异常/几何资源错误/上下文失效——policy §6.3
        // Failed 臂）：该解 DataInsufficient 素材＋诊断、不中断整批
        // （§8.1 取消/失败行）；会话诊断 cause 入素材（内部诊断链文本
        // ——呈现前须脱敏）。
        verdict.state = IkCollisionEvaluationState::FacilityFailed;
        if (!evaluation.diagnostics.empty()) {
            verdict.statusDetail = evaluation.diagnostics.front().cause;
        } else {
            verdict.statusDetail = "碰撞设施异常（Failed——会话未附诊断）";
        }
        break;
    }
    return verdict;
}

// =====================================================================
// makeCollisionSession——④端口会话接线入口
// =====================================================================

CollisionSessionWiring
makeCollisionSession(const IKinRuntimeView& view, const IKinCollisionSceneSource& sceneSource,
                     const policy::EngineeringPolicySet& resolvedPolicy,
                     const policy::ICollisionEvaluator* evaluator)
{
    CollisionSessionWiring wiring;
    wiring.policyContentIdentity = resolvedPolicy.contentIdentity;  // CON-06 观测面

    // 第 1 步：碰撞启用状态只读自 policy（V13-01——唯一开关读取源；
    // 本单元不设碰撞布尔开关）。未启用＝碰撞检查不在范围（非降级）：
    // 会话不构建，查询的碰撞会话指针为空（collisionNotEvaluated 语义）。
    if (!resolvedPolicy.collision.enabled) {
        wiring.availability = CollisionSessionAvailability::PolicyDisabled;
        return wiring;
    }

    // 第 2 步：检测器在场核对（缺评估器注入＝缺检测器——KIN-05 证据
    // 缺失轨，绝不视为无碰撞）。
    if (evaluator == nullptr) {
        wiring.availability = CollisionSessionAvailability::DetectorUnavailable;
        wiring.detail = "碰撞评估器未注入（缺检测器——KIN-COLLISION-UNAVAILABLE "
                        "证据缺失轨）";
        return wiring;
    }

    // 第 3 步：请求方场景装配（policy.md §6.1"谁装配"行——本单元按
    // 快照组装；装配环境事实错误→证据缺失轨）。
    const runtime::Expected<policy::CollisionScene, KinematicsError> scene =
        assembleCollisionScene(view, sceneSource);
    if (!scene.ok()) {
        wiring.availability = CollisionSessionAvailability::DetectorUnavailable;
        wiring.detail = "碰撞场景装配不可行（"
            + std::string(kinematicsErrorCodeToken(scene.error().code)) + "："
            + scene.error().detail + "）";
        return wiring;
    }

    // 第 4 步：经唯一实现入口构建会话（policy §9.3——createSession 三步
    // 任一失败即 PolicyError：场景校验/名称不可解析/规则冲突——数据与
    // 环境错误，证据缺失轨，不猜测不静默收窄）。名称上下文以 source
    // 端口转发适配（R-4——只转发不加工）；端口借用至构建完成（解析
    // 结果固化进会话）。
    try {
        const SceneSourceNameContext names(sceneSource);
        std::shared_ptr<const policy::CollisionEvaluationSession> session =
            evaluator->createSession(resolvedPolicy, scene.get(), names);
        wiring.availability = CollisionSessionAvailability::SessionReady;
        wiring.session = std::make_shared<const PolicyCollisionSessionAdapter>(
            std::move(session));
        return wiring;
    } catch (const std::exception& ex) {
        wiring.availability = CollisionSessionAvailability::DetectorUnavailable;
        wiring.detail = std::string("碰撞会话构建失败（policy 会话三步校验未过——")
            + ex.what() + "）";
        return wiring;
    }
}

}  // namespace sdurws::ird::kinematics
