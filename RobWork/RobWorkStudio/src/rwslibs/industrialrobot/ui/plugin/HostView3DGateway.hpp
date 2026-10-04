/**
 * @file   HostView3DGateway.hpp
 * @brief  宿主三维网关（View3D 阶段 B——UI-T45）——上行拾取/下行呈现
 *         出口/TCP 会话态数据源（方案 spec
 *         docs/superpowers/specs/2026-10-03-view3d-stage-b-design.md）。
 *
 * 范围勘误（2026-10-03 实施期实证）：下行高亮桥已由既有 HostHighlightOutlet
 * （UiPlugin 匿名命名空间——IUiHighlightOutlet 完整实现＋SelectionService
 * 装配在位）承载，本网关**不重复实现高亮**——三桥收敛为"两桥一源"：
 *   - 上行拾取：Ctrl+双击 → pickFrame → SA-05 反解 → 域分发＋View3DPick 选中；
 *   - 下行呈现出口：IUiPresentationOutlet 宿主半区（宿主呈现对象挂接/
 *     摘除——原子替换/会话拆除释放）；
 *   - TCP 会话态数据源：宿主 State 只读 FK（需求域捕获流解除降级）。
 *
 * 可测性形态：本类对框架视图零直接依赖——视图访问全部经 Deps 函数缝
 * （生产装配在 UiPlugin 绑 RWStudioView3D 公开 API；测试绑替身函数）。
 * rw 类型仅出现在数据面（Frame 指针、DrawableNode 句柄、Transform3D
 * ——ui_plugin 目标框架链接面既有）。
 *
 * 红线：SA-02 零框架修改（事件过滤外挂）；O-38③/O-43 不接管场景
 * （引用随 open/close 两拍同步）；KIN-06 会话姿态零修订。
 * 线程模型：全部入口仅 UI 线程（§3.4 M-1）。
 */

#ifndef IRD_UI_PLUGIN_HOSTVIEW3DGATEWAY_HPP
#define IRD_UI_PLUGIN_HOSTVIEW3DGATEWAY_HPP

#include <functional>
#include <optional>
#include <string>

#include <QPoint>

#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Identity.hpp>                  // core::ObjectId（拾取反解目标——CON-01 表内登记边）
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>        // IUiPresentationOutlet/PresentationViewProjection/PresentationApplyReport
#include <sdurws/ird/ui/SelectionService.hpp>            // IUiRuntimeNameMapPort/SelectionService/SelectionSource（端口契约）
#include <sdurws/ird/ui/View3DPreviewContract.hpp>       // IUiView3DPreviewOutlet/View3DPreviewUpdate（会话预览协议——UI-T33）

namespace sdurws {
namespace ird {
namespace ui {

class HostView3DGateway final : public IUiPresentationOutlet,
                                public IUiView3DPreviewOutlet {
public:
    /**
     * @brief 宿主呈现对象约定型（hostPayload 的取回类型——UI-T45 自己
     *        定义、自己解释；RT-T14 工厂产品由装配侧适配器包裹为本型，
     *        "实现按原类型取回，禁止跨适配器解释"——RuntimePublishBridge
     *        hostPayload 契约原文）。
     *
     * 场景绑定由适配器自持（构造期捕获宿主 WorkCellScene——网关零场景
     * 依赖，headless 测试面可全链路驱动）。方法非 const：挂接态是对象
     * 内部可变状态。
     */
    class HostPresentationObject {
    public:
        virtual ~HostPresentationObject() = default;
        /// 挂接宿主场景（呈现组；false＝挂接失败——网关如实报告并保留
        /// 旧呈现，"失败保持原状"的事务语义。const 语义：挂接态为实现
        /// 内部可变状态——载荷恒以 const 形态经网关转手）。
        virtual bool apply() const = 0;
        /// 整组摘除（对称收口——"不残留旧呈现"）。
        virtual void remove() const = 0;
    };

    /**
     * @brief 网关装配依赖（全部函数缝——生产绑框架 API/测试绑替身）。
     *
     * 所有权：非 owning（selection/nameMap 裸指针由装配层保证存活期）；
     * 函数可空性见各注（required＝构造校验，nullable＝运行时诚实降级）。
     */
    struct Deps {
        /// 视图拾取（required——屏幕坐标→Frame；空返回＝未命中，常态）。
        std::function<rw::kinematics::Frame*(int x, int y)> pickFrame;
        /// 设备名→TCP 帧（nullable——空函数/空返回＝TCP 源诚实降级）。
        std::function<const rw::kinematics::Frame*(const std::string& deviceName)>
            resolveTcpFrame;
        /// 宿主当前 State（nullable——空＝TCP 源诚实降级；只读借用）。
        std::function<const rw::kinematics::State*()> currentState;
        /// 业务选中汇聚（required——View3DPick 来源唯一写入口）。
        SelectionService* selection = nullptr;
        /// 运行时名称映射（required——SA-05 反解唯一通道）。
        const IUiRuntimeNameMapPort* nameMap = nullptr;
        /// 建模域分发（nullable——缺省＝该域不参与拾取分发）。
        std::function<bool(const core::ObjectId&)> dispatchToModeling;
        /// 需求域分发（nullable——同上；未命中本域＝域内诚实 false）。
        std::function<bool(const core::ObjectId&)> dispatchToRequirements;

        // ---- 会话预览渲染后端（UI-T33——预览半区的场景原语缝）--------
        // 生产绑定 WorkCellScene 渲染分组（装配层适配——SceneGraph 依赖
        // 渲染库，构造下沉适配器与呈现对象同款纪律）；测试绑替身记录。
        // nullable＝预览出口诚实降级（applyPreview 如实 false——不虚构
        // 预览；呈现/TCP/预览三面可独立降级）。
        struct PreviewBackend {
            /// 整组绘制（原子替换语义在网关编排——后端只画：标记坐标轴
            /// ＋标签/盒线框/格线着色；false＝场景不可得等渲染失败）。
            std::function<bool(const View3DPreviewUpdate&)> draw;
            /// 整组清除（幂等——removePreview/场景清除拍共用）。
            std::function<void()> clear;
        };
        PreviewBackend previewBackend;
    };

    /**
     * @brief 构造网关（UI 线程——§3.4）。
     *
     * @param deps [in] 装配依赖（pickFrame/selection/nameMap 必填非空，
     *             缺失抛 std::invalid_argument；scene/TCP 两缝可空＝
     *             呈现/TCP 诚实降级的显式形态）
     */
    explicit HostView3DGateway(Deps deps);

    // ---- 上行拾取链（Ctrl+双击语义等价承接）--------------------------

    /**
     * @brief 处理一次视图双击（事件过滤器的转发落点；UI 线程）。
     *
     * 链路：pickFrame→帧名→SA-05 反解→①域分发（modeling/requirements，
     * 未命中本域＝域内诚实 false）→②View3DPick 来源选中（L2 高亮判定
     * 经既有 HostHighlightOutlet 回环）。反解失败＝零选中变化（分支②
     * 语义——不伪造 ObjectId）。
     *
     * @param pos [in] 视图坐标（事件位形——与 pickFrame 约定同系）
     * @return true＝反解成功且链路有产出（选中/分发至少其一）；false＝
     *         未命中/反解失败（调用方不过滤事件——放行框架默认处理）
     */
    bool handleViewDoubleClick(const QPoint& pos);

    // ---- 下行呈现出口（IUiPresentationOutlet——事务第三步宿主半区）--

    /**
     * @brief 原子应用呈现视图（hostPayload 取回 HostPresentationObject
     *        ——视图不完整/类型不符/场景不可得＝失败报告且旧呈现保持
     *        原状；成功＝整组摘除旧呈现后挂接新呈现）。
     */
    PresentationApplyReport
    applyPresentation(const PresentationViewProjection& view) override;

    /// 释放宿主呈现（会话拆除/项目关闭拍——整组摘除；幂等）。
    void releasePresentation() override;

    // ---- TCP 会话态数据源（需求域捕获流解除降级）--------------------

    /**
     * @brief 取设备当前 TCP 位姿（宿主会话态——base→TCP，KIN-06 零修订）。
     *
     * @param deviceName [in] 设备名（WorkCell findDevice<JointDevice>）
     * @return base→TCP 位姿；设备/帧/State 不可得＝nullopt（诚实降级
     *         ——调用方呈现失败原因，不虚构位姿）
     */
    std::optional<rw::math::Transform3D<>>
    currentTcpPose(const std::string& deviceName);

    // ---- 会话预览出口（UI-T33——IUiView3DPreviewOutlet 半区）-----------

    /**
     * @brief 原子应用一次会话预览（整组替换——工位标记/区域框/采样格
     *        三层聚合，杜绝半新半旧）。
     *
     * 编排：previewBackend 缺位＝false（预览诚实降级——呈现/TCP/预览
     * 三面独立降级的显式形态）；后端 draw 失败＝false 且**保留旧呈现**
     * （"失败保持原状"事务语义——与修订呈现同款）；成功＝更新挂接态。
     * 着色判定零参与（cellStates 已由域侧判定——网关零判定，spec §2.4）。
     */
    bool applyPreview(const View3DPreviewUpdate& update) override;

    /// 整组摘除（幂等——项目关闭/会话拆除拍对称收口，"不残留旧呈现"）。
    void removePreview() override;

    /// 预览挂接态（观测面——true＝有已挂接预览）。
    bool previewAttached() const { return m_previewAttached; }

    /// 当前预览值（观测面——最近一次成功应用的整组值；未挂接＝空聚合）。
    const View3DPreviewUpdate& currentPreview() const { return m_currentPreview; }

    // ---- 会话/场景拍（宿主生命周期同步）----------------------------

    /// 场景清除拍（宿主 WorkCell 关闭——呈现残留整组清理；幂等）。
    void onSceneCleared();

    /// 呈现挂接态（观测面——true＝有已挂接呈现对象）。
    bool presentationAttached() const
    {
        return m_attachedPresentation != nullptr;
    }

    /// 网关功能态（true＝拾取两缝齐备——呈现/TCP 缺省属局部降级）。
    bool functional() const
    {
        return m_deps.pickFrame != nullptr && m_deps.nameMap != nullptr
               && m_deps.selection != nullptr;
    }

private:
    Deps m_deps;                                        ///< 装配依赖（构造冻结）
    std::shared_ptr<const HostPresentationObject> m_attachedPresentation;   ///< 已挂接呈现（原子替换的当前面）
    bool m_previewAttached = false;                     ///< 预览挂接态（预览半区——UI-T33）
    View3DPreviewUpdate m_currentPreview;               ///< 当前预览值（成功应用面——观测）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_HOSTVIEW3DGATEWAY_HPP
