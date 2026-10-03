/**
 * @file   HostPresentationAdapters.hpp
 * @brief  宿主呈现装配适配器族（UI-T46）——名称映射真值端口＋宿主呈现
 *         对象（setWorkCell 挂接）＋呈现构造源（RT-T14 工厂折叠投影）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T46.json（宿主呈现装配批次）；
 *   - ui.md §13 UI-T45 行（HostView3DGateway——IUiPresentationOutlet 已
 *     在位，HostPresentationObject 为"装配侧适配器包裹 RT-T14 工厂产品"
 *     的约定型；HostEmptyNameMapPort 注释"真映射注入点单一随呈现装配
 *     替换，网关经成员共享实例同步升级"——本文件即该替换的实现）；
 *   - runtime 公共契约 HostPresentationView（RT-T14——唯一构造入口
 *     createHostPresentationView；三类身份只读对账面；hostPresentation
 *     WorkCell 宿主挂接出口——UI-T46 增量登记）；
 *   - RuntimePublishBridge（UI-T20——IUiPresentationSource 契约"只经
 *     createHostPresentationView 构造呈现"的语义冻结；折叠投影字段
 *     与 RT-T14 面的对应表）；
 *   - B1-SPEC §4.3 D10（隔离＋复用——source 只消费编译链产物快照，
 *     零第二构造路径；INV-B4 宿主单入口＝setWorkCell）。
 *
 * 背景说明（三件适配器在呈现装配链中的位置）：
 *   呈现刷新事务（UI-T20 四步）中：HostPresentationSource 是**事务第一
 *   步**（完整构造——RT-T14 工厂产品折叠为 ui 侧投影）；网关
 *   （HostView3DGateway，UI-T45 已落位）是**事务第三步**（原子替换——
 *   经 HostWorkCellPresentationObject 把 WorkCell 交给宿主单入口）；
 *   HostRuntimeNameMapPort 是呈现的**伴生真值面**（发布成功即绑定当前
 *   呈现视图的 NameMap——拾取反解/高亮/需求域三维缝三消费面经插件成员
 *   单一实例同步升级，"替换即全链激活"）。
 *
 * 线程模型：全部入口仅 UI 线程（§3.4 M-1——桥事务与呈现路径同线程）。
 * 生命周期：由 L5 装配层（IrdWorkbenchHostPlugin）持有——端口 shared_ptr
 *   共享（桥/网关/SelectionService 各持一份引用同一实例），适配器内部
 *   状态（绑定视图）由装配编排单点更新。
 */

#ifndef IRD_UI_PLUGIN_HOSTPRESENTATIONADAPTERS_HPP
#define IRD_UI_PLUGIN_HOSTPRESENTATIONADAPTERS_HPP

#include <memory>
#include <optional>
#include <string>

#include <rws/RobWorkStudio.hpp>  // rws::RobWorkStudio（宿主单入口——setWorkCell）

#include <sdurws/ird/core/Identity.hpp>              // ObjectId（反解值面）
#include <sdurws/ird/runtime/HostPresentationView.hpp>  // HostPresentationView/Handle（RT-T14 契约面）
#include <sdurws/ird/runtime/Snapshot.hpp>           // RuntimeSnapshot（source 供数面）
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>    // IUiPresentationSource/PresentationViewProjection/事件事实
#include <sdurws/ird/ui/SelectionService.hpp>        // IUiRuntimeNameMapPort（真值端口契约）

#include "HostView3DGateway.hpp"  // HostPresentationObject（网关约定型——本文件产品适配其接口）

namespace rws {
class RobWorkStudio;  // 前置复述（框架头已含——注释面；防循环无虞）
}

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// HostRuntimeNameMapPort——名称映射真值端口（HostEmptyNameMapPort 的替换
// 形态：绑当前呈现视图，未就位＝同构空二态）
// =====================================================================

/**
 * @brief 运行时名称映射端口的产品实现（SA-05 双向反解——绑**当前呈现
 *        视图**的 RuntimeNameMap）。
 *
 * 为什么"绑定呈现视图"而不是"绑定快照"：呈现视图（RT-T14）是宿主三维
 * 与 TreeView 实际显示的内容载体——拾取命中的 Frame 名来自该视图的
 * WorkCell，其名称映射必须同源（反解结果与显示内容一致——INV-B4 的
 * 名称面）。视图未就位（本会话尚无成功呈现）＝两向 nullopt——与
 * HostEmptyNameMapPort 的诚实空映射同构（L2 未应用分支/L3 反解失败
 * 分支的合法触发面，零语义漂移——升级是纯增量替换）。
 *
 * 注入点单一纪律（UI-T45 冻结面的兑现）：插件成员 m_nameMapPort 持本
 * 实例——SelectionService/三维网关/需求域三维缝三消费面共享同一指针，
 * bindPresentation/clearPresentation 单点更新即三面同步升级（替换零改
 * 消费方代码）。
 *
 * 线程安全：仅 UI 线程（绑定/查询同线程——§3.4 M-1）。
 */
class HostRuntimeNameMapPort final : public IUiRuntimeNameMapPort {
public:
    HostRuntimeNameMapPort() = default;

    /**
     * @brief 绑定当前呈现视图（呈现发布成功拍——桥 onPresentationReplaced
     *        的编排面；nullopt＝解绑回空二态）。
     * @param view [in] 当前呈现视图（共享持有——持视图即持快照存活期）
     */
    void bindPresentation(std::shared_ptr<const runtime::HostPresentationView> view);

    /// 解绑（会话拆除/项目关闭拍——回诚实空二态；幂等）。
    void clearPresentation();

    /// 当前呈现视图（nullopt＝未就位——真值端口的现取面；行程评估名称
    /// 上下文经此复用同一绑定态，零双绑定漂移）。
    std::shared_ptr<const runtime::HostPresentationView> currentPresentation() const
    {
        return m_view;
    }

    std::optional<core::ObjectId>
        resolveObjectIdFromRuntimeName(const std::string& runtimeName) const override;
    std::optional<std::string> resolveRuntimeName(const core::ObjectId& id) const override;

    /// 绑定态（观测面——true＝真值映射在位）。
    bool hasPresentation() const
    {
        return m_view != nullptr;
    }

private:
    std::shared_ptr<const runtime::HostPresentationView> m_view; ///< 当前呈现视图（空＝诚实空二态）
};

// =====================================================================
// HostWorkCellPresentationObject——宿主呈现对象（RT-T14 工厂产品的挂接
// 适配——apply＝宿主单入口 setWorkCell）
// =====================================================================

/**
 * @brief 网关约定型（HostPresentationObject）的产品适配：持有呈现视图，
 *        apply() 把视图的 WorkCell 以宿主单入口交给宿主（setWorkCell——
 *        框架 TreeView 与三维场景从同一载体刷新，INV-B4 的宿主半区）。
 *
 * 挂接语义（与 B1-SPEC D10 的对齐）：交给宿主的 WorkCell 就是编译链
 * 产物本体（hostPresentationWorkCell 出口——对象同一性，零复制零第二
 * 构造路径）；宿主挂接＝渲染/查询的只读借用（出口契约——结构写属调用
 * 方违约）。remove() 登记为**空动作**：宿主 WorkCell 的清空归项目关闭
 * 编排（宿主自身的关闭流程——会话拆除时插件 teardown 已挂网关
 * onSceneCleared 收口呈现残留；setWorkCell(null) 无框架契约支撑，不
 * 伪造清空动作）。
 *
 * 生命周期：由 HostPresentationSource 构造、以 hostPayload
 * shared_ptr<const void> 形态随投影流转（ui 拷贝投影即持有存活期——
 * 与 RT-T14"持视图即持快照"一致）。
 */
class HostWorkCellPresentationObject final
    : public HostView3DGateway::HostPresentationObject {
public:
    /**
     * @brief 构造（studio/view 均非 owning——调用方保证 apply() 调用期
     *        存活；view 共享句柄自带存活锚）。
     */
    HostWorkCellPresentationObject(rws::RobWorkStudio* studio,
                                   runtime::HostPresentationViewHandle view);

    /**
     * @brief 挂接宿主（setWorkCell 单入口；studio/视图缺位＝false——
     *        网关如实报告并保留旧呈现，"失败保持原状"事务语义）。
     */
    bool apply() const override;

    /// 对称收口（空动作——语义见类注；幂等）。
    void remove() const override;

    /// 呈现视图句柄（现取面——发布观察者经 hostPayload 取回后绑定名称
    /// 映射真值端口；装配层自家类型的取回——网关"本类型本解释"同款）。
    const runtime::HostPresentationViewHandle& view() const
    {
        return m_view;
    }

private:
    rws::RobWorkStudio* m_studio;                  ///< 宿主注入面（非 owning）
    runtime::HostPresentationViewHandle m_view;    ///< 呈现视图（共享——存活锚）
};

// =====================================================================
// HostPresentationSource——呈现构造端口（IUiPresentationSource → RT-T14
// 工厂折叠投影）
// =====================================================================

/**
 * @brief 呈现构造源（刷新事务第一步——完整构造）。语义冻结面（不改义）：
 *        只经 createHostPresentationView(已发布快照, 已应用修订) 构造
 *        （工厂唯一入口）；产物折叠为 ui 侧投影（字段对应表见
 *        RuntimePublishBridge 头）。
 *
 * 供数面：Deps.snapshot 缝（装配绑 HostModelCompilePort::lastPublished
 * Snapshot——编译链产物的唯一来源；nullopt＝本会话尚无编译产物，如实
 * nullopt——桥按 construct-failed 处置，不虚构呈现）。
 */
class HostPresentationSource final : public IUiPresentationSource {
public:
    /**
     * @brief 装配依赖（函数缝——生产绑编译端口/测试绑替身）。
     */
    struct Deps {
        /// 最近发布快照（required——nullopt 返回＝本会话无编译产物，常态
        /// 诚实降级而非错误）。
        std::function<std::shared_ptr<const runtime::RuntimeSnapshot>()> snapshot;
        /// 宿主注入面（**可空**——headless 测试面投影构造不需要宿主；
        /// 挂接对象 apply() 对空宿主诚实失败＝网关"失败保持原状"事务
        /// 语义的既有承载，投影构造不受影响）。
        rws::RobWorkStudio* studio = nullptr;
        /// 开发日志通道（可空＝静默）。
        std::function<void(const std::string&)> devLog;
    };

    /**
     * @brief 构造（装配校验 fail-fast——snapshot 缝必填）。
     */
    explicit HostPresentationSource(Deps deps);

    /**
     * @brief 构造完整呈现投影（事务第一步）。
     *
     * 链路：取最近发布快照（缺→nullopt）→ createHostPresentationView
     * （appliedRevision 取事件事实——对账基准同源）→ 折叠投影
     * （三类身份直取＋hostPayload＝HostWorkCellPresentationObject 包裹
     * ＋objectExists＝视图 NameMap 反解）。工厂对非法输入异常穿透
     * （RuntimeError——装配缺陷不吞，桥不捕获的既有契约）。
     *
     * @param facts [in] 事件事实（appliedRevision 为呈现视图归属修订）
     * @return 完整投影（isComplete()==true）；nullopt＝本修订不可呈现
     */
    std::optional<PresentationViewProjection>
        fetchPresentation(const PresentationEventFacts& facts) override;

private:
    Deps m_deps;  ///< 装配依赖（构造冻结）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_HOSTPRESENTATIONADAPTERS_HPP