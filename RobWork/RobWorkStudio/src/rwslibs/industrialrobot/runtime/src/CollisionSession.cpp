/**
 * @file   CollisionSession.cpp
 * @brief  运行时真实碰撞设施实现（MA-02——见公共头 CollisionSession.hpp）。
 *
 * 实现口径（本批登记）：
 *   - 窄相策略＝PQP（rwlibs::proximitystrategies::ProximityStrategyPQP——
 *     本构建 "PQP ENABLED! Using RobWork native PQP"，configure 实测；
 *     FCL/Bullet 停用不引入）。
 *   - 阈值语义：safetyClearanceM>0 的"距离小于阈值即碰撞"需要容差策略
 *     （CollisionToleranceStrategy 族）的实机验证——本批如实承载 0 阈值
 *     纯相交；>0 输入在 make 侧拒绝（InputInvalid——不静默降级为 0，
 *     避免"以为有间距检查实际没有"的伪证据）。
 *   - 检测范围＝WC 全几何（MA-01 真实 mesh＋既有图元代理都挂在 Rigid
 *     Object 载体——都入检测；同帧双 facet 几何冗余不改变布尔结论）。
 *   - 过滤面（policy 语义投影）：相邻连杆对（|i-j|==1）与 excludedPairs
 *     命中对从明细剔除；mandatoryPairs 本会话不做特别放行（无过滤策略
 *     层——全对检测下"必须检测"天然满足）。
 */

#include "sdurws/ird/runtime/CollisionSession.hpp"

#include <sdurws/ird/runtime/Errors.hpp>       // RuntimeError/InputInvalid token 面
#include <sdurws/ird/runtime/NameMap.hpp>      // RuntimeNameMap（帧名→ObjectId 映射源）

#include <rw/models/SerialDevice.hpp>          // 设备句柄（setQ/自由度）
#include <rw/models/WorkCell.hpp>              // findDevice/默认状态
#include <rw/proximity/CollisionDetector.hpp>  // 碰撞检测器（宽相＋窄相编排）
#include <rwlibs/proximitystrategies/ProximityStrategyPQP.hpp>  // PQP 窄相

#include <map>
#include <stdexcept>

namespace sdurws::ird::runtime {

// =====================================================================
// pimpl——rw proximity 头不进公共头（消费面零传递——R-2 同精神）。
// =====================================================================
struct RuntimeCollisionSession::Impl
{
    std::shared_ptr<const RuntimeSnapshot> snapshot;  ///< 场景载体（WC/映射同寿背书）
    rw::core::Ptr<rw::proximity::CollisionDetector> detector;  ///< 检测器（PQP 窄相）
    rw::core::Ptr<rw::models::SerialDevice> device;   ///< 设备（setQ 目标）
    rw::kinematics::State stateTemplate;              ///< 默认状态模板（evaluate 线程私有拷贝）
    std::vector<std::string> linkFrameNames;          ///< 链序连杆帧名（相邻/忽略对解析键）
    std::vector<core::ObjectId> linkObjectIds;        ///< 链序连杆 ObjectId（明细映射值）
    std::map<std::string, core::ObjectId> frameOwner; ///< 全部几何帧→ObjectId（明细映射）
    RuntimeCollisionRules rules;                      ///< 判定规则（宿主投影值）
};

std::shared_ptr<RuntimeCollisionSession> RuntimeCollisionSession::make(
    std::shared_ptr<const RuntimeSnapshot> snapshot,
    const RuntimeCollisionRules& rules)
{
    if (snapshot == nullptr) {
        return nullptr;  // 无场景＝设施不可用（诚实降级面）
    }
    if (rules.safetyClearanceM > 0.0) {
        // 不静默降级为 0（伪证据风险）——显式拒绝，宿主按 FacilityFailed
        // 语义回落 EvidenceMissing；容差策略实机化归验收批（见文件头）。
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "runtime/collision-session: safetyClearance>0 m "
                           "的容差判定尚未实机化——本批仅承载 0 阈值纯相交");
    }

    // ---- 第一步：设备解析（无设备＝无可判构型——设施不可用）。
    const CanonicalModel& model = snapshot->model();
    const RobotChain& chain = model.chain();
    const std::string& deviceName = chain.deviceName;
    rw::core::Ptr<rw::models::WorkCell> wc = snapshot->workCellHandle();
    rw::core::Ptr<rw::models::SerialDevice> device =
        wc->findDevice(deviceName).cast<rw::models::SerialDevice>();
    if (device.isNull()) {
        return nullptr;
    }

    // ---- 第二步：帧名→ObjectId 映射（NameMap 全条目过滤几何相关范围
    // ——碰撞明细只映射"能定位到业务对象"的帧；映射不到的帧对按明细
    // 缺失省略，不影响布尔判定）。
    std::map<std::string, core::ObjectId> frameOwner;
    for (const RuntimeNameMap::Entry& entry : snapshot->nameMap().entries()) {
        const NameScope scope = entry.scope;
        if (scope == NameScope::BaseFrame || scope == NameScope::LinkFrame
            || scope == NameScope::Flange || scope == NameScope::Tcp
            || scope == NameScope::SceneObject) {
            frameOwner.emplace(entry.fullName, entry.objectId);
        }
    }

    // ---- 第三步：链序连杆帧名（相邻/忽略对规则的解析键——每连杆恰一
    // 帧在 {BaseFrame[0], LinkFrame(中), Flange[末]} 三范围之一；缺失＝
    // 映射失实，fail-fast）。
    std::vector<std::string> linkFrameNames;
    std::vector<core::ObjectId> linkObjectIds;
    linkFrameNames.reserve(chain.links.size());
    linkObjectIds.reserve(chain.links.size());
    const std::size_t linkCount = chain.links.size();
    for (std::size_t i = 0; i < linkCount; ++i) {
        const CanonicalLink& link = chain.links.at(i);
        const NameScope wanted = (i == 0)     ? NameScope::BaseFrame
                                 : (i + 1 == linkCount) ? NameScope::Flange
                                                        : NameScope::LinkFrame;
        bool found = false;
        for (const RuntimeNameMap::Entry& entry : snapshot->nameMap().entries()) {
            if (entry.objectId == link.objectId && entry.scope == wanted) {
                linkFrameNames.push_back(entry.fullName);
                linkObjectIds.push_back(link.objectId);
                found = true;
                break;
            }
        }
        if (!found) {
            return nullptr;  // 映射失实＝场景不完整——设施不可用（诚实降级）
        }
    }

    // ---- 第四步：检测器装配（PQP 窄相——策略在自身内部按 Frame 建
    // 模型；检测器持有 WC 借持句柄，快照由本会话 shared_ptr 背书）。
    const rw::core::Ptr<rw::proximity::CollisionStrategy> strategy =
        rwlibs::proximitystrategies::ProximityStrategyPQP::make();
    auto detector = rw::core::ownedPtr(new rw::proximity::CollisionDetector(
        wc, strategy));

    auto session = std::shared_ptr<RuntimeCollisionSession>(
        new RuntimeCollisionSession());
    session->m_impl = new Impl;
    session->m_impl->snapshot = std::move(snapshot);
    session->m_impl->detector = detector;
    session->m_impl->device = device;
    session->m_impl->stateTemplate = wc->getDefaultState();
    session->m_impl->linkFrameNames = std::move(linkFrameNames);
    session->m_impl->linkObjectIds = std::move(linkObjectIds);
    session->m_impl->frameOwner = std::move(frameOwner);
    session->m_impl->rules = rules;
    return session;
}

RuntimeCollisionSession::Verdict RuntimeCollisionSession::evaluate(
    const std::vector<double>& q) const
{
    Verdict verdict;
    const Impl& impl = *m_impl;
    // ---- 自由度守卫：失配＝请求违约轨（不猜截断/补零）。
    if (impl.device->getDOF() != static_cast<std::size_t>(q.size())) {
        verdict.evaluated = false;
        return verdict;
    }
    // ---- 线程私有状态值拷贝＋构型写入（setQ 后全链 FK 由 State 承载）。
    rw::kinematics::State state = impl.stateTemplate;
    impl.device->setQ(q, state);

    // ---- 真实碰撞检测（PQP 窄相；宽相默认策略）。返回存在碰撞对＝真。
    rw::proximity::CollisionDetector::QueryResult queryResult;
    const bool rawCollision =
        impl.detector->inCollision(state, &queryResult);
    if (!rawCollision) {
        verdict.evaluated = true;
        verdict.inCollision = false;
        return verdict;
    }

    // ---- 规则过滤（保序遍历 FramePairSet——set 无序，先收集到稳定向量
    // 再按规则判定；明细序＝帧名对字典序，确定性输出）。
    std::vector<std::pair<std::string, std::string>> survivingPairs;
    for (const auto& framePair : queryResult.collidingFrames) {
        const std::string nameA = framePair.first->getName();
        const std::string nameB = framePair.second->getName();

        // 相邻连杆默认过滤（链序 |i-j|==1——运动学相邻事实，policy 同语义）。
        if (impl.rules.excludeAdjacentLinksByDefault) {
            auto indexInChain = [&](const std::string& name) -> int {
                for (std::size_t i = 0; i < impl.linkFrameNames.size(); ++i) {
                    if (impl.linkFrameNames[i] == name) {
                        return static_cast<int>(i);
                    }
                }
                return -1;  // 非连杆帧（场景/工具）——不适用相邻过滤
            };
            const int indexA = indexInChain(nameA);
            const int indexB = indexInChain(nameB);
            if (indexA >= 0 && indexB >= 0
                && (indexA - indexB == 1 || indexB - indexA == 1)) {
                continue;  // 相邻对——默认不报告
            }
        }

        // 允许忽略对（ObjectId 对命中即剔除——规则值由宿主从 policy
        // ScopeTarget.Object 直映；映射不可达的对不受忽略规则影响——
        // 保守保留）。
        bool excluded = false;
        for (const RuntimeCollisionPairRule& rule : impl.rules.excludedPairs) {
            auto ownerOf = [&](const std::string& name) -> const core::ObjectId* {
                const auto it = impl.frameOwner.find(name);
                return it != impl.frameOwner.end() ? &it->second : nullptr;
            };
            const core::ObjectId* ownerA = ownerOf(nameA);
            const core::ObjectId* ownerB = ownerOf(nameB);
            if (ownerA != nullptr && ownerB != nullptr
                && ((*ownerA == rule.first && *ownerB == rule.second)
                    || (*ownerA == rule.second && *ownerB == rule.first))) {
                excluded = true;
                break;
            }
        }
        if (excluded) {
            continue;
        }
        survivingPairs.emplace_back(nameA, nameB);
    }

    verdict.evaluated = true;
    verdict.inCollision = !survivingPairs.empty();
    if (verdict.inCollision) {
        // 明细映射（帧名→ObjectId；映射不可达的对省略——布尔不受影响）。
        for (const auto& namePair : survivingPairs) {
            const auto itA = impl.frameOwner.find(namePair.first);
            const auto itB = impl.frameOwner.find(namePair.second);
            if (itA != impl.frameOwner.end() && itB != impl.frameOwner.end()) {
                verdict.objectIdPairs.emplace_back(itA->second, itB->second);
            }
        }
    }
    return verdict;
}

RuntimeCollisionSession::~RuntimeCollisionSession()
{
    delete m_impl;
}

}  // namespace sdurws::ird::runtime
