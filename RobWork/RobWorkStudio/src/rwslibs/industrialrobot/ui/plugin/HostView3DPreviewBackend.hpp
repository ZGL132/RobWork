/**
 * @file   HostView3DPreviewBackend.hpp
 * @brief  需求域三维会话预览的宿主渲染后端（UI-T33 收口——网关预览半
 *         区的场景原语绑定；WorkCellScene 渲染分组编排）。
 *
 * 设计依据：
 *   - View3D 阶段 B 方案 spec §2.4（呈现应用链——渲染分组挂接；着色
 *     判定归域侧，本后端零判定只分色）；
 *   - View3DPreviewContract.hpp 协议值面（世界系几何＋挂帧标记）；
 *   - 场景绑定下沉适配器纪律（spec 勘误注——SceneGraph 依赖渲染库，
 *     构造期自绑场景；headless 测试面经网关替身缝全链路驱动，本后端
 *     不入 gui 替身路径）。
 *
 * 渲染编排（draw 的原子替换语义——网关 applyPreview 契约的落地）：
 *   ①按名清除旧组（ird-req-preview 前缀——节点名清单逐个删）；
 *   ②工位标记：逐 marker findFrame 挂 FrameAxis（坐标轴随帧动——
 *     会话/示教移动零重投；帧不可解析＝跳过并计数，summaryText 追加
 *     失败可见面）；
 *   ③区域边界框：八角点 12 棱线框（RegionOutlineRender——世界系挂
 *     world 帧）；
 *   ④采样格：格线＋逐点着色（SampleGridRender——Good 绿/Weak 黄/
 *     Failed 红；cellStates 空＝中性格线，不虚构判定）。
 *
 * 线程约束：仅 UI 线程（宿主三维交互面——§3.4）。
 * 所有权：studio 非 owning（调用方保证存活期覆盖）；Render 节点由场
 *   景持有（removeDrawable 后释放）。
 */

#ifndef IRD_UI_PLUGIN_HOSTVIEW3DPREVIEWBACKEND_HPP
#define IRD_UI_PLUGIN_HOSTVIEW3DPREVIEWBACKEND_HPP

#include <string>
#include <vector>

#include <rw/models/WorkCell.hpp>  // 会话预览锚 WC（成员持有——F-550②）

#include <sdurws/ird/ui/View3DPreviewContract.hpp>  // 协议值面（会话预览——UI-T33）

namespace rws {
class RobWorkStudio;
}

namespace rw {
namespace graphics {
class WorkCellScene;
}
}  // namespace rw

namespace sdurws {
namespace ird {
namespace ui {

class HostView3DPreviewBackend {
public:
    /**
     * @brief 构造渲染后端（studio 非 owning——调用方保证 apply 调用期
     *        存活；场景随 open/close 两拍经 studio 现取）。
     *
     * @param studio [in] 宿主注入面（getView→getWorkCellScene 现取；
     *               空指针/场景不可得＝draw 如实 false）
     */
    explicit HostView3DPreviewBackend(rws::RobWorkStudio* studio);

    /**
     * @brief 整组绘制（原子替换——先清旧组再重建；网关 applyPreview
     *        的后端半区）。
     *
     * @param update [in] 预览值（三层聚合——标记/框/格）
     * @return true＝已挂接；false＝场景不可得（诚实失败——网关保留旧
     *         呈现）
     */
    bool draw(const View3DPreviewUpdate& update);

    /// 整组清除（幂等——按节点名清单逐个删；removePreview/场景清除拍共用）。
    void clear();

private:
    /**
     * @brief 确保场景存在可挂接的 WorkCell 锚并返回（F-550② 修复——
     *        会话预览是会话编辑面〔PM-11〕，草稿期（首应用前）必须可见；
     *        此前实现把"宿主已有发布 WorkCell"当前置，新项目增工位/
     *        区域后三维恒空白）。
     *
     * 三级语义（draw 的挂接锚现取顺序）：
     *   ①场景已挂 WorkCell（宿主装载/发布链 setWorkCell，或上次装入的
     *     本会话锚）＝原样复用（幂等——重复 setWorkCell 会无谓重建场景
     *     节点树并触发查看器世界节点再同步）；
     *   ②场景无 WorkCell＝新建仅含框架根帧的"会话预览锚"WC 并装入场景
     *     （仅场景层——studio->getWorkCell() 保持空，发布链"WorkCell 即
     *     编译产物本体"〔INV-B4 宿主单入口〕与 TreeView 等订阅 studio
     *     工作单元事件的宿主组件对本锚零感知、零干扰）。
     *
     * 为什么必须有锚：场景挂接 API（addRender/addFrameAxis）在场景无
     * WorkCell 时直接抛异常（WorkCellScene::addDrawable 的框架防御）
     * ——"无锚不画"是框架硬约束，本修复让锚在会话期恒在位，而非放弃
     * 渲染。
     *
     * @param scene [in] 宿主场景（draw 已判空——本函数不重复判）
     * @return 挂接锚 WC（锚装入失败〔视图缺位等装配降级〕＝空——调用方
     *         如实返回 false，网关保留旧呈现）
     */
    rw::core::Ptr<rw::models::WorkCell> ensureSessionPreviewAnchor(
        rw::graphics::WorkCellScene& scene);

    rws::RobWorkStudio* m_studio;               ///< 宿主注入面（非 owning）
    std::vector<std::string> m_nodeNames;       ///< 已创建节点名清单（clear 的删除面）
    /// 会话预览锚 WC（成员持有保证存活——场景 _wc 亦持共享属主，双向
    /// 所有权冗余为可读性；发布/宿主装载后本锚被场景置换，成员随之更新）。
    rw::core::Ptr<rw::models::WorkCell> m_sessionAnchor;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_HOSTVIEW3DPREVIEWBACKEND_HPP
