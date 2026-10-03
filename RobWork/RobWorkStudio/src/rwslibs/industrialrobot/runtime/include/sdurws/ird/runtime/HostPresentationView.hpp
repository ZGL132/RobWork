/**
 * @file   HostPresentationView.hpp
 * @brief  宿主呈现视图（HostPresentationView）——已应用 WorkCell 的只读
 *         呈现契约：与计算 RuntimeSnapshot 生命周期隔离、复用同一构造
 *         规则（同一编译链产物＋同一 RuntimeNameMap＋同一基座—世界变换），
 *         并携带三类身份绑定与 ObjectId↔运行时名反解查询面。
 *
 * 设计依据：
 *   - units/runtime.md §12 RT-T14 行（任务卡：呈现视图契约——只读视图＋
 *     生命周期隔离声明＋反解查询面）、§8.2/§8.3（只读视图——WorkCellConst
 *     View 既有 const 面）、§9.1~§9.3（快照隔离对照——呈现视图不进运行
 *     身份/缓存身份/当前性判定）、§7（RuntimeNameMap 反解面——唯一权威）；
 *   - 方案 B.1 设计规格 B1-SPEC §4.3（D10 详述：呈现 WorkCell 与计算
 *     RuntimeSnapshot 生命周期隔离但复用同一构造规则；三类身份绑定
 *     modelIdentity/appliedRevisionId/presentationIdentity 及对账与失效
 *     规则；反向隔离；序列化旁路限定禁令）、§7.12（D10 承载）；
 *   - ARCHITECTURE.md §7.12＋SA-18（呈现 WorkCell 与计算 RuntimeSnapshot
 *     隔离但复用同一构造规则——呈现侧禁止私建第二构造路径）；§7.3/§7.4
 *     （编译链唯一性——本类型以既有快照为唯一构造输入，结构性执行）；
 *   - knownPitfalls P-RT-4（基座—世界变换单一不变量——worldToBase 经
 *     快照单点转发，禁止呈现侧二次旋转，BaseWorldTransform.hpp 单点）；
 *   - 需求 ARC-03（模型快照唯一真值）、ARC-04（身份可追溯——三类身份
 *     绑定可对账）、AT-37（基座—世界变换单一消费口径不破坏）。
 *
 * 背景说明（D10 的"隔离但复用"在本类型的落法）：
 *   宿主呈现（发布桥 UI-T20→TreeView/三维衔接）与评估计算（execution/
 *   worker）消费**同一个编译产物**：本视图持有的是已发布快照的共享只读
 *   句柄（shared_ptr<const RuntimeSnapshot>），不重建 WorkCell、不重建
 *   名称映射、不重算基座—世界变换——"同一构造规则"由构造输入即快照这一
 *   事实**结构性保证**（私建第二构造路径在类型层不可达：工厂只收快照）。
 *   隔离性同样由结构保证：本视图不提供任何写路径（全 const 面）；对呈现
 *   侧 State 副本（makeState() 值拷贝）的修改不触及快照内容；视图自身
 *   不进入运行身份/缓存身份/当前性判定（无任何 CacheKey/快照身份输入位）。
 *
 * 三类身份绑定（B1-SPEC §4.3 v1.1——可验证的绑定关系，不要求字段值相等）：
 *   - modelIdentity＝来源规范模型内容身份（＝快照 modelIdentity——对照
 *     来源模型可验证）；
 *   - appliedRevisionId＝来源已应用修订（工厂入参——对照项目已应用修订
 *     可验证；运行时单元不拥有修订语义，仅承载宿主供给的归属事实）；
 *   - presentationIdentity＝本次呈现构造自身身份（工厂内 ObjectId::
 *     generate() 每次构造新生成——重建即新身份；词表复用 core::ObjectId
 *     的生成机制，**不表示业务对象身份**）。
 *   对账规则：发布/重编译/项目切换时由消费方核对 appliedRevisionId——
 *   同一修订发布后绑定成立；修订切换后旧视图的 presentationIdentity 随
 *   旧视图废弃（"旧呈现身份失效"——失效动作在消费侧换用新视图，本类型
 *   无全局状态可失效）。
 *
 * 序列化旁路限定禁令（B1-SPEC §4.3 v1.1）：禁止"呈现→序列化中转→
 *   RuntimeSnapshot/计算输入"的复制回写与 const_cast 破坏只读视图——本
 *   类型全 const 面且不提供任何编解码出口，结构性服从；既有正式 Codec、
 *   项目导入导出、worker 物化传输协议不受该禁令限制（限定口径）。
 *
 * 线程模型（§9.1 同口径）：本视图为不可变值包装（构造后全只读），并发
 *   只读安全；底层快照本体构造后只读（§9.1）——跨线程交接经 shared_ptr
 *   （happens-before 由其保证，§9.2）。
 *
 * 生命周期/所有权：工厂返回 shared_ptr<const>（与 CompileOutcome 的快照
 *   句柄同惯例）；视图不延长快照生命周期之外的任何对象（持有快照句柄即
 *   持有 WorkCell/NameMap 的存活期）；快照释放后视图不可用（句柄为唯一
 *   存活锚——消费方持视图即持快照）。
 */

#ifndef SDURWS_IRD_RUNTIME_HOSTPRESENTATIONVIEW_HPP
#define SDURWS_IRD_RUNTIME_HOSTPRESENTATIONVIEW_HPP

#include <memory>
#include <string>

#include <sdurws/ird/core/Identity.hpp>       // core::ContentIdentity/ObjectId/RevisionId（三类身份承载）
#include <sdurws/ird/runtime/Adapter.hpp>     // runtime::WorkCellConstView（§8.3 只读视图——既有 const 面）
#include <sdurws/ird/runtime/NameMap.hpp>     // runtime::RuntimeNameMap/ObjectRef/RuntimeName＋解析错误类型（§7 反解面——唯一权威）
#include <sdurws/ird/runtime/Snapshot.hpp>    // runtime::RuntimeSnapshot（唯一构造输入——D10 复用的结构性保证）

namespace sdurws::ird::runtime {

class HostPresentationView;
class HostPresentationViewImpl;  // R-2：实现封闭在库内（src/HostPresentationView.cpp）

/// 呈现视图句柄（shared_ptr<const> 惯例同 CompileOutcome 快照句柄——§9.2）。
using HostPresentationViewHandle = std::shared_ptr<const HostPresentationView>;

// =====================================================================
// HostPresentationView——宿主呈现视图（只读；D10 隔离＋复用）
// =====================================================================

/**
 * @brief 宿主呈现视图（已应用 WorkCell 的只读呈现契约面）。
 *
 * 全部查询方法为对底层快照的**纯转发**（零加工零缓存零第二路径）：
 * "呈现侧与计算侧对同一 ObjectId/运行时名的互换解析结果一致"（RT-NM
 * 断言形态）由转发同一 RuntimeNameMap 实例**逐字节成立**；worldToBase()
 * 与计算侧同源（P-RT-4 单点——BaseWorldTransform 唯一实现）。
 *
 * 非法使用：经底层快照句柄为空的自造实例调用（不可能经工厂产生——工厂
 * 对空快照 fail-fast）；从快照释放后继续持视图使用（句柄即存活锚——持
 * 视图即持快照，该形态仅在调用方自行提前释放快照句柄时可达，属调用方
 * 契约违约）。
 */
class HostPresentationView final {
public:
    // ---- 三类身份绑定（B1-SPEC §4.3 v1.1——只读对账面）----

    /// @brief 来源规范模型内容身份（＝快照 modelIdentity——对照来源模型可验证；不抛）。
    const core::ContentIdentity& modelIdentity() const noexcept;

    /// @brief 来源已应用修订（工厂入参原样承载——对照项目已应用修订可验证；不抛）。
    const core::RevisionId& appliedRevisionId() const noexcept;

    /// @brief 本次呈现构造自身身份（每次构造新生成——重建即新身份；不抛）。
    const core::ObjectId& presentationIdentity() const noexcept;

    // ---- 只读视图面（转发快照 IRuntimeModelView——零第二构造路径）----

    /// @brief 已应用 WorkCell 只读视图（§8.3 const 面——任何写路径类型层不可达；不抛）。
    const WorkCellConstView& workCell() const noexcept;

    /**
     * @brief 宿主挂接 WorkCell 句柄（UI-T46 增量落位——units/runtime.md
     *        §15.4 v0.18 登记；B1-SPEC §4.3 v1.1 const 面审计的**唯一登记
     *        豁免点**）。
     *
     * 背景（为什么本出口必须存在）：宿主呈现装配的挂接动作＝
     * RobWorkStudio::setWorkCell（框架公开 API，TreeView 与三维场景从
     * 同一载体刷新——INV-B4 的宿主半区），框架签名接收非 const
     * 引用计数句柄（RobWork 基线惯例——历史签名，SA-02 零修改）。本
     * 视图原契约面全 const，宿主挂接无合法取数路径——呈现装配因此悬空
     * （UI-T45 落位时呈现出口即因本缺口保持降级）。
     *
     * 口径澄清（立法意图边界，对抗验收对账面）：B1-SPEC v1.1 的
     * const_cast/序列化旁路禁令禁止的是"**呈现侧改写**快照内容"与
     * "呈现→序列化中转→计算输入"的复制回写——本出口不产生任何写路径
     * 纪律的放松：①转换是**单点**的（仅本方法体内一处 const 形态解除，
     * 消费方零 cast）；②转换后**对象本体未变**（同一 WorkCell 实例以
     * 引用计数交给宿主渲染——零复制、零第二构造路径，"同一编译链产物"
     * 由对象同一性结构保证）；③消费契约＝宿主呈现借用（挂接/渲染/查询
     * ——框架 TreeView 与场景构建全部只读消费；结构写属调用方契约违约，
     * 不在本类型可执行面内强制，登记为呈现装配纪律）。反向隔离的验收
     * 口径（呈现副本修改前后快照内容身份不变）由 RT-PRES 族用例继续
     * 承载。
     *
     * @return 宿主挂接句柄（与非 const 视图同一对象——持句柄即持快照
     *         存活期，与本视图"持视图即持快照"一致；不抛）
     */
    rw::core::Ptr<rw::models::WorkCell> hostPresentationWorkCell() const;

    /// @brief 名称映射（§7 权威实例——反解查询与计算侧同源；不抛）。
    const RuntimeNameMap& nameMap() const noexcept;

    /// @brief WC 默认状态值拷贝（转发快照——线程私有副本，呈现侧修改不触快照）。
    rw::kinematics::State makeState() const;

    /// @brief 基座→世界变换（P-RT-4 单点转发——与计算侧同源；不抛）。
    rw::math::Transform3D<double> worldToBase() const;

    /// @brief 世界→基座变换（同上——单点逆变换；不抛）。
    rw::math::Transform3D<double> baseToWorld() const;

    // ---- 反解查询面（转发 RuntimeNameMap——R-4 零拼装零第二映射）----

    /**
     * @brief 对象身份→运行时名（转发 RuntimeNameMap::resolveObjectId——
     *        与计算侧解析逐字节一致；不抛）。
     *
     * @param id [in] 对象稳定身份（构建输入闭包内 ObjectId）
     * @return ok＝RuntimeName；err＝UnknownObject（未命中显性化，同计算侧）
     */
    Expected<RuntimeName, RuntimeNameError>
        resolveObjectId(core::ObjectId id) const noexcept;

    /**
     * @brief 运行时名→对象（转发 RuntimeNameMap::resolveRuntimeName——
     *        整串匹配语义不变；不抛）。
     *
     * @param name [in] 完整名（"RobotScope.LocalName"形态；大小写敏感）
     * @return ok＝ObjectRef；err＝UnknownObject（requestedName 回显原名）
     */
    Expected<ObjectRef, RuntimeResolveError>
        resolveRuntimeName(RuntimeNameView name) const noexcept;

private:
    friend HostPresentationViewHandle
    createHostPresentationView(std::shared_ptr<const RuntimeSnapshot> snapshot,
                               core::RevisionId appliedRevision);

    /// 私有构造（工厂唯一入口——空快照 fail-fast 在工厂完成）。
    HostPresentationView(std::shared_ptr<const RuntimeSnapshot> snapshot,
                         core::RevisionId appliedRevision);

    /// 底层快照（D10 复用的唯一构造输入与存活锚；shared_ptr<const>）。
    std::shared_ptr<const RuntimeSnapshot> m_snapshot;
    /// 来源已应用修订（宿主供给的归属事实——运行时单元不拥有修订语义）。
    core::RevisionId m_appliedRevision;
    /// 本次呈现构造身份（构造期 ObjectId::generate()——重建即新身份）。
    core::ObjectId m_presentationIdentity;
};

// =====================================================================
// 装配入口
// =====================================================================

/**
 * @brief 创建宿主呈现视图（唯一构造入口——输入即既有快照，D10"同一构造
 *        规则"的结构性执行面）。
 *
 * 本工厂是 B1-SPEC §4.3"禁止呈现侧私建第二构造路径"的类型层执行点：
 * 构造只能从已发布快照出发——任何"从规范模型直接重建呈现 WorkCell"的
 * 尝试在调用面不可达（无此类签名）。
 *
 * @param snapshot       [in] 已发布快照（CompileOutcome::Published 的产物
 *                       ——空指针＝调用方错误，fail-fast；呈现构造失败整体
 *                       失败不返回半成品）
 * @param appliedRevision [in] 来源已应用修订（宿主从项目已应用修订供给；
 *                       全零保留值＝"未设置"语义违约，fail-fast——呈现
 *                       视图必须可对账到具体修订）
 * @return 只读呈现视图句柄（shared_ptr<const>；持视图即持快照存活期）
 *
 * @throws RuntimeError 码＝InputInvalid（调用方错误轨，fail-fast）：
 *         snapshot 为空；appliedRevision 为全零保留值（isValid()==false）
 */
HostPresentationViewHandle createHostPresentationView(
    std::shared_ptr<const RuntimeSnapshot> snapshot,
    core::RevisionId appliedRevision);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_HOSTPRESENTATIONVIEW_HPP
