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

#include <sdurws/ird/ui/View3DPreviewContract.hpp>  // 协议值面（会话预览——UI-T33）

namespace rws {
class RobWorkStudio;
}

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
    rws::RobWorkStudio* m_studio;               ///< 宿主注入面（非 owning）
    std::vector<std::string> m_nodeNames;       ///< 已创建节点名清单（clear 的删除面）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_HOSTVIEW3DPREVIEWBACKEND_HPP
