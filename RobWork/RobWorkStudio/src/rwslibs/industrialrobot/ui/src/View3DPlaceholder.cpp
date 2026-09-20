/**
 * @file   View3DPlaceholder.cpp
 * @brief  三维视图占位（UI-T05 阶段 A）——View3DContract.hpp 契约数据的
 *         唯一定义点＋中央区三维视图占位面板的构建。
 *
 * 设计依据：
 *   - units/ui.md §4.1/§4.2（中央区阶段 A：占位面板，含"本阶段将在后续
 *     版本提供"说明，不虚构业务能力——该占位为契约显式设计，非未完成
 *     实现）、§14.1（三维视图交互交互实现归阶段 B，本任务只登记契约）；
 *   - 需求 UX-11（交互清单）、KIN-06/AT-04（会话姿态红线——语义钉住）；
 *   - 任务契约 tasks/foundation/UI-T05.json acceptance 1~3。
 *
 * 背景说明（本翻译单元的两个职责为何放在一起）：阶段 A 的交付物是"占位
 * 面板＋契约登记"一体面——契约数据（清单/语义常量）没有面板之外的第二
 * 个消费者，面板又只呈现契约内容；单一定义点让"改清单漏改面板呈现"这类
 * 漂移在物理上不可能（面板逐行渲染 view3DInteractionManifest()，不存在
 * 第二份文案清单）。阶段 B 接管交互实现时，把实现 TU 并入即可，契约数据
 * 定义点不变（词表稳定性——View3DContract.hpp 头注）。
 *
 * 面板形态（红线——不虚构业务能力，§4.2/§11.3）：纯 QLabel 纵向清单，
 * 零按钮/输入/可勾选控件（无任何可交互业务入口可供性）；每行清单项以
 * 只读属性 view3dId 携带契约 id（阶段 B 定位锚点与 GUI 测试断言键），
 * 行序＝清单序（构造序即 QWidget 子对象序——确定性，无排序逻辑）。
 *
 * 线程模型：构建只在 UI 线程（壳装配路径 buildCentralArea 调用）；契约
 * 数据访问为编译期固定值，无跨线程共享可变状态。
 */

#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <sdurws/ird/ui/View3DContract.hpp>

#include "WorkbenchShell_p.hpp"  // WorkbenchText（壳层文案单点登记表——kStagePlaceholder 等）

#include <cstddef>

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 契约数据定义（View3DContract.hpp 声明——唯一权威定义点）
// =====================================================================

const std::vector<View3DInteractionItem>& view3DInteractionManifest()
{
    // 九项字面清单（序＝REQUIREMENTS UX-11 条目书写序——契约头 §清单注释
    // 逐条对应；static 存储期保证引用返回安全，条目值编译期常量）。
    // 冻结锚：test/View3DContractTest.cpp 逐项断言——此处改一字测试即失败，
    // 清单变更必须同步单元卡修订与测试（不得绕过）。
    static const std::vector<View3DInteractionItem> kManifest = {
        {"view3d.host", u8"三维视图（中央工作区）"},
        {"view3d.standard_camera_views", u8"标准视图（顶/右/前）与相机视图"},
        {"view3d.zoom", u8"缩放"},
        {"view3d.projection_toggle", u8"透视/正交切换"},
        {"view3d.wireframe_transparency", u8"线框/透明切换"},
        {"view3d.render_group_visibility", u8"渲染分组显隐（Virtual/Physical/Drawable/Collision/User）"},
        {"view3d.collision_highlight_mask", u8"碰撞高亮与碰撞组掩码"},
        {"view3d.screenshot_png", u8"视图截图（PNG）"},
        {"view3d.pick_ray_cast", u8"三维拾取（ray-cast 选择 Frame/Drawable）"},
    };
    return kManifest;
}

View3DSessionPoseContract view3DSessionPoseContract()
{
    // KIN-06/AT-04 钉住值：仅会话级 UI 状态，零模型写入、零修订、零结果
    // 失效（逐位语义见 View3DSessionPoseContract 字段注释；test/
    // View3DContractTest.cpp SessionPoseContractPinned_KIN06_AT04 逐位冻结）。
    return View3DSessionPoseContract{};
}

}  // namespace ui
}  // namespace ird

namespace sdurws::ird {
namespace ui {
namespace detail {

// =====================================================================
// 中央区三维视图占位面板（WorkbenchShell_p.hpp 声明）
// =====================================================================

QWidget* createView3DPlaceholder(QWidget* parent)
{
    // 面板根：objectName 是 GUI 测试与阶段 B 的定位键（ird_* 前缀＝ui
    // 自有控件命名域，壳内既有惯例——ird_central_stack/ird_homepage 同款）。
    auto* panel = new QWidget(parent);
    panel->setObjectName("ird_view3d_placeholder");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 24, 24, 24);  // 与首页同款留白（§4.1 视觉一致——非功能值）

    // 标题：中央区在产品形态下承载三维视图（§4.2 中央工作区行）——占位
    // 面板以"三维视图"点明区域身份，避免误读为泛化空白页。
    auto* title = new QLabel(QString::fromUtf8(WorkbenchText::kView3dTitle), panel);
    title->setObjectName("ird_view3d_placeholder_title");
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);

    // 占位说明（§4.1 原文口径文案——契约显式设计，明确"现在没有"而非
    // 虚构能力；文案与壳层其余占位共用 WorkbenchText 单点登记）。
    auto* notice = new QLabel(QString::fromUtf8(WorkbenchText::kStagePlaceholder), panel);
    notice->setObjectName("ird_view3d_placeholder_notice");
    notice->setAlignment(Qt::AlignHCenter);
    layout->addWidget(notice);

    // 清单引导行：显式声明"以下内容尚未提供"——清单呈现的是阶段 B 交付
    // 计划（UX-11 登记），不是当前可用功能（§4.2 不虚构业务能力红线）。
    auto* lead = new QLabel(QString::fromUtf8(WorkbenchText::kView3dManifestLead), panel);
    lead->setObjectName("ird_view3d_manifest_lead");
    layout->addWidget(lead);

    // 交互清单行：逐行渲染契约清单（行序＝清单序——构造序即子对象序）；
    // 每行以只读动态属性 view3dId 携带契约 id（阶段 B 实现锚点＋GUI 测试
    // 断言键）。用 QLabel 而非 QListWidget：清单是纯呈现，滚动/选中交互
    // 属可交互可供性，占位态一律不做（§4.2 红线的控件面落实）。
    const std::vector<View3DInteractionItem>& manifest = view3DInteractionManifest();
    for (std::size_t i = 0; i < manifest.size(); ++i) {
        auto* row = new QLabel(QString::fromUtf8(u8"%1．%2")
                                       .arg(i + 1)
                                       .arg(QString::fromUtf8(manifest[i].label)),
                               panel);
        row->setObjectName("ird_view3d_manifest_item");
        // 契约 id 随行登记（只读属性——不进文案，仅供测试/阶段 B 锚定；
        // 界面上不出现 id 字面——UX-02 工程用语，内部标识不入呈现层）。
        row->setProperty("view3dId", QString::fromLatin1(manifest[i].id));
        layout->addWidget(row);
    }

    // 尾部弹性：清单行数变化（阶段 B 前的词表增量）时内容保持顶部聚拢。
    layout->addStretch(1);
    return panel;
}

}  // namespace detail
}  // namespace ui
}  // namespace ird
