/**
 * @file   HostPresentationView.cpp
 * @brief  宿主呈现视图（HostPresentationView）的实现——对已发布快照的
 *         纯只读转发包装＋三类身份绑定（RT-T14；契约头
 *         include/sdurws/ird/runtime/HostPresentationView.hpp）。
 *
 * 设计依据：
 *   - units/runtime.md §12 RT-T14 行（落位物同名头）、§8.2/§8.3（只读
 *     视图——WorkCellConstView 既有 const 面的转发）、§9.1~§9.3（快照
 *     隔离对照）、§7（NameMap 反解面转发）；
 *   - B1-SPEC §4.3（D10 隔离＋复用；三类身份绑定；反向隔离；序列化旁路
 *     限定禁令）、ARCH §7.12/SA-18；
 *   - P-RT-4（基座—世界变换单点——worldToBase 经快照转发，本 TU 零变换
 *     算术，BaseWorldTransform 单一实现不二次封装）。
 *
 * 背景说明（为什么实现只有转发与身份字段）：D10 的验收是"隔离但复用"——
 *   复用由构造输入即快照结构性保证（工厂只收快照，第二构造路径在调用面
 *   不可达）；隔离由全 const 面结构性保证（视图无写路径，State 经
 *   makeState() 值拷贝隔离）。因此本 TU 不含任何呈现语义的新实现：任何
 *   超出转发的逻辑都属于"呈现侧私建语义"，恰是本任务禁止的形态。
 *
 * 线程模型：视图构造后全只读（并发只读安全）；m_presentationIdentity 在
 *   构造期生成（ObjectId::generate 的 thread_local 引擎——多线程并发构造
 *   安全，core/Identity.hpp 生成纪律）。
 */

#include <sdurws/ird/runtime/HostPresentationView.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws::ird::runtime {

HostPresentationView::HostPresentationView(std::shared_ptr<const RuntimeSnapshot> snapshot,
                                           core::RevisionId appliedRevision)
    : m_snapshot(std::move(snapshot))
    , m_appliedRevision(appliedRevision)
    , m_presentationIdentity(core::ObjectId::generate())
{
    // 构造即发布形态：三类身份在此刻全部定型（modelIdentity 来自快照、
    // appliedRevision 来自宿主供给、presentationIdentity 本次新生成——
    // "重建即新身份"的绑定关系由构造时序保证，验收经 RT-PRES 具名用例）。
}

const core::ContentIdentity& HostPresentationView::modelIdentity() const noexcept
{
    // 转发快照身份域（同一事实的两条读取路径——值恒等的对账面）。
    return m_snapshot->modelIdentity();
}

const core::RevisionId& HostPresentationView::appliedRevisionId() const noexcept
{
    // 宿主供给的归属事实原样承载（运行时单元不解释修订语义——PA-1）。
    return m_appliedRevision;
}

const core::ObjectId& HostPresentationView::presentationIdentity() const noexcept
{
    // 构造期生成的实例身份（每次 createHostPresentationView 都是新值——
    // 修订切换后旧视图废弃即旧身份失效，无全局状态可失效）。
    return m_presentationIdentity;
}

const WorkCellConstView& HostPresentationView::workCell() const noexcept
{
    // §8.3 只读视图转发（const 面——任何写路径在类型层不可达）。
    return m_snapshot->workCell();
}

rw::core::Ptr<rw::models::WorkCell> HostPresentationView::hostPresentationWorkCell() const
{
    // 宿主挂接出口（契约注释见头——B1-SPEC v1.1 const 面审计的唯一登记
    // 豁免点）。const 形态解除收编在本单元此一处：从只读视图取原句柄
    // （WorkCellConstView::workCellHandle——构造入参原样返回）后经标准
    // const_pointer_cast 交给宿主渲染。对象本体未变（同一 WorkCell 以
    // 引用计数共享——零复制零第二构造路径），写纪律由呈现装配契约承载
    // （宿主挂接＝渲染/查询的只读借用——头注口径澄清三条）。
    const rw::core::Ptr<const rw::models::WorkCell>& readOnly =
        m_snapshot->workCell().workCellHandle();
    return rw::core::Ptr<rw::models::WorkCell>(
        std::const_pointer_cast<rw::models::WorkCell>(
            readOnly.getCppSharedPtr()));
}

const RuntimeNameMap& HostPresentationView::nameMap() const noexcept
{
    // §7 权威映射转发（反解查询与计算侧同实例——结果逐字节一致的结构性
    // 保证，R-4"不私建名称映射"的执行面）。
    return m_snapshot->nameMap();
}

rw::kinematics::State HostPresentationView::makeState() const
{
    // 值拷贝转发（§9.1 同口径——呈现侧对副本的修改不触快照内容，反向
    // 隔离的执行面）。
    return m_snapshot->makeState();
}

rw::math::Transform3D<double> HostPresentationView::worldToBase() const
{
    // P-RT-4 单点转发：基座—世界变换与计算侧同源（零二次旋转零本地缓存）。
    return m_snapshot->worldToBase();
}

rw::math::Transform3D<double> HostPresentationView::baseToWorld() const
{
    // 同 worldToBase——单点逆变换转发。
    return m_snapshot->baseToWorld();
}

Expected<RuntimeName, RuntimeNameError>
HostPresentationView::resolveObjectId(core::ObjectId id) const noexcept
{
    // §7 反解面转发（未命中显性化——UnknownObject 语义与计算侧一致）。
    return m_snapshot->nameMap().resolveObjectId(id);
}

Expected<ObjectRef, RuntimeResolveError>
HostPresentationView::resolveRuntimeName(RuntimeNameView name) const noexcept
{
    // §7 正解面转发（整串匹配——大小写敏感语义不变，R-4 零拼装）。
    return m_snapshot->nameMap().resolveRuntimeName(name);
}

HostPresentationViewHandle createHostPresentationView(
    std::shared_ptr<const RuntimeSnapshot> snapshot,
    core::RevisionId appliedRevision)
{
    // 工厂即 fail-fast 收口：呈现构造失败整体失败不返回半成品（契约
    // acceptance 6）——空快照/全零修订都是调用方契约违约，立即抛出。
    if (!snapshot) {
        throw RuntimeError{RuntimeErrorCode::InputInvalid,
                           "createHostPresentationView: snapshot 为空（须为 CompileOutcome"
                           "::Published 产物）"};
    }
    if (!appliedRevision.isValid()) {
        throw RuntimeError{RuntimeErrorCode::InputInvalid,
                           "createHostPresentationView: appliedRevision 为全零保留值"
                           "（呈现视图必须可对账到具体修订）"};
    }
    // 构造走 new＋shared_ptr 直包（工厂是 private 构造的唯一 friend——
    // make_shared 的内部分配上下文不在友元授权内，此处直包等价且单点）。
    return std::shared_ptr<const HostPresentationView>(
        new HostPresentationView(std::move(snapshot), appliedRevision));
}

}  // namespace sdurws::ird::runtime
