/**
 * @file   View3DContract.hpp
 * @brief  三维视图占位契约（UI-T05 阶段 A 登记）——中央区三维视图的
 *         UX-11 交互清单与 KIN-06 会话姿态红线的代码侧钉住面。
 *
 * 设计依据：
 *   - 需求 UX-11（三维视图交互：标准视图〔顶/右/前〕与相机视图、缩放、
 *     透视/正交切换、线框/透明切换、渲染分组显隐〔Virtual/Physical/
 *     Drawable/Collision/User〕、碰撞高亮与碰撞组掩码、视图截图 PNG、
 *     三维拾取〔ray-cast 选 Frame/Drawable〕）、KIN-06（双击任务或候选
 *     只改变会话姿态，不修改设计模型、不触发结果失效）、AT-04（会话预览
 *     与修订——预览不产生项目修订，显式应用经命令端口）；
 *   - units/ui.md §4.1/§4.2（中央区阶段 A 占位面板——含"本阶段将在后续
 *     版本提供"说明，不虚构业务能力）、§14.1（三维视图交互全量归阶段 B，
 *     会话姿态红线已在单元卡 §2/§6.8 钉住）、§16.7 v0.7（本头登记行）；
 *   - 任务契约 tasks/foundation/UI-T05.json acceptance 1~3。
 *
 * 背景说明（为什么阶段 A 就要一个契约头）：中央区在产品形态下是三维视图
 * 宿主（RWStudioView3D 集成，§4.2 中央工作区行），UI-T05 阶段 A 只交付
 * 中央区占位面板——但占位不等于空白：阶段 B 交互实现必须承接的两件事在
 * 此用代码钉死，验收与 review 可直接对照、后续不得无声漂移——
 *   1. UX-11 清单（本头词表）：九项交互的范围边界。清单即阶段 B 的验收
 *      范围——不得缩水（清单外交互=未交付），也禁止在此清单之外"顺手"
 *      扩面（扩面须先升单元卡修订——词表一经交付只允许表尾追加）；
 *   2. KIN-06 红线（本头语义常量）：三维拾取/点回写类交互只改会话姿态
 *      ——会话级 UI 状态（会话态 O-2 轴），**绝不**写设计模型、**绝不**
 *      产生修订、**绝不**触发结果失效；显式应用才经命令端口产生修订
 *      （AT-04）。这是四轴正交（§6.8）在三维交互上的具体化：UI 会话态
 *      不得覆盖工程判定与当前性轴。
 * 本头只登记契约数据（交互词表＋语义常量），不承载任何运行时行为——
 * 交互实现归阶段 B（WP-10-T05 阶段 B 交付）；阶段 A 的唯一消费方是中央
 * 区三维视图占位面板（呈现清单文案，见 src/View3DPlaceholder.cpp）。
 *
 * O-31 处置（acceptance 3）：本头及其消费方（占位面板）不持有
 * C-3/4/5/7/8/10/11 对端类型——清单与语义均为 ui 自有词表/常量值，三维
 * 渲染与 IRuntimeModelView 共同消费点（C-11）归阶段 B，届时按 ui.md
 * §2.1.2 C-11 注入口径（v0.4 裁决）随增量修订复核。
 *
 * 线程模型：纯函数＋编译期固定数据，无共享可变状态——任意线程可调用
 * （阶段 A 实际只在 UI 线程的装配路径消费）。
 */

#ifndef SDURWS_IRD_UI_VIEW3DCONTRACT_HPP
#define SDURWS_IRD_UI_VIEW3DCONTRACT_HPP

#include <string>
#include <vector>

namespace sdurws::ird {
namespace ui {

// =====================================================================
// UX-11 交互清单（阶段 B 交付范围——验收边界即清单边界）
// =====================================================================

/**
 * @brief 三维视图交互清单条目（UX-11 交互登记的最小承载）。
 *
 * 生命周期：条目值编译期固定（字面常量），由 view3DInteractionManifest()
 * 以稳定序供给；调用方只读引用，不接管所有权（数据为静态存储期）。
 *
 * 字段语义：
 *   - id 是代码侧稳定标识（词法＝点分段＋小写蛇形段：小写字母/数字，段内
 *     下划线分词、点分段——与 §7.1 命令 id 的"点分小写"同族但独立词表，
 *     不混用命令命名空间）——阶段 B 实现各交互时的实现锚点与测试追溯键；
 *     一经交付只允许表尾追加并升单元卡修订（重命名/重排会破坏阶段 B 承接
 *     与登记序稳定性，禁止）；
 *   - label 是中文呈现名（工程用语——UX-02：零哈希/Schema 版本号/插件名；
 *     领域对象类型词如 Frame/Drawable 属工程语言不在禁区）。占位面板清单
 *     行直接呈现该值；UI-T09 UiText 资源化后 label 转为键解析的缺省值
 *     （键即 id 派生——迁移不换词表）。
 */
struct View3DInteractionItem {
    /// 稳定标识（点分段＋小写蛇形段；阶段 B 实现锚点与登记序键——见结构体注释）。
    const char* id;
    /// 中文呈现名（工程用语——占位面板清单行文案；见结构体注释）。
    const char* label;
};

/**
 * @brief UX-11 三维视图交互清单（九项；稳定序＝需求条目序）。
 *
 * 为什么序即契约：清单登记序与 REQUIREMENTS UX-11 条目书写序一致，验收
 * 逐项对照时不需要映射表；冻结该序（模型测试 View3DContractTest 逐项
 * 断言）使"顺手重排/增删"在测试面即失败——清单变更必须走单元卡增量修订
 * ＋测试同改（AGENTS.md §2.5 注释随代码更新同款纪律）。
 *
 * 九项语义（逐条对应需求原文）：
 *   1. view3d.host　　中央区三维视图宿主（RWStudioView3D 集成——§4.2
 *      中央工作区行；三维一切交互的承载前提）；
 *   2. view3d.standard_camera_views　标准视图（顶/右/前）与相机视图；
 *   3. view3d.zoom　　缩放；
 *   4. view3d.projection_toggle　透视/正交切换；
 *   5. view3d.wireframe_transparency　线框/透明切换；
 *   6. view3d.render_group_visibility　渲染分组显隐（Virtual/Physical/
 *      Drawable/Collision/User 五组——组词表归需求侧，本头不复述定义）；
 *   7. view3d.collision_highlight_mask　碰撞高亮与碰撞组掩码；
 *   8. view3d.screenshot_png　视图截图（PNG 导出）；
 *   9. view3d.pick_ray_cast　三维拾取（ray-cast 选择 Frame/Drawable——
 *      拾取结果的写回语义受 view3DSessionPoseContract() 红线约束，KIN-06）。
 *
 * @return 清单只读引用（静态存储期；九项恒定——阶段 A 冻结，阶段 B 按此
 *         交付，扩面须升单元卡修订后表尾追加）
 */
const std::vector<View3DInteractionItem>& view3DInteractionManifest();

// =====================================================================
// KIN-06 会话姿态红线（AT-04）——阶段 B 交互实现的语义钉住
// =====================================================================

/**
 * @brief 三维交互点回写（会话姿态）的语义契约（KIN-06/AT-04 钉住面）。
 *
 * "点回写"指三维视图中的候选点选、预览、动画等交互把一个位姿呈现到场景
 * 的行为（KIN-06 原文"双击任务或候选只改变会话姿态"）。四个布尔位把该
 * 语义钉成机器可断言的常量：阶段 B 实现拾取/回写时，任何一条 false 位被
 * 实现成 true（例如回写顺手推进修订、或触发结果失效投影）即违反 KIN-06
 * ——review 对照本结构即可判定，无需回读需求全文。
 *
 * 字段语义（KIN-06/AT-04/§6.8 逐条对应）：
 *   - sessionStateOnly＝true　回写效果仅是会话级 UI 状态（UI 会话态轴，
 *     §6.8 正交表 O-2 位）——切换项目/关闭会话即消亡，不入任何持久层；
 *   - writesDesignModel＝false　不修改设计模型（模型快照零触——ARC-04
 *     身份与不可变历史不受三维交互影响）；
 *   - producesRevision＝false　不产生修订（不可变历史 PA-2——预览类交互
 *     无写路径；显式应用才经命令端口产生修订，AT-04 原文）；
 *   - invalidatesResults＝false　不触发结果失效（当前性轴独立——CON-02
 *     正交：UI 会话态不得覆盖/触发 evidence 当前性计算）。
 */
struct View3DSessionPoseContract {
    /// 回写效果仅是会话级 UI 状态（恒 true——KIN-06"只改变会话姿态"）。
    bool sessionStateOnly = true;
    /// 是否修改设计模型（恒 false——KIN-06"不修改设计模型"）。
    bool writesDesignModel = false;
    /// 是否产生修订（恒 false——AT-04"预览不产生项目修订"）。
    bool producesRevision = false;
    /// 是否触发结果失效（恒 false——KIN-06"不触发结果失效"/CON-02 正交）。
    bool invalidatesResults = false;
};

/**
 * @brief 取会话姿态语义契约的钉住值（KIN-06/AT-04）。
 *
 * @return 契约常量（成员值编译期固定如结构体注释——sessionStateOnly=true、
 *         其余三位恒 false；模型测试逐位断言冻结，改动即测试失败＝必须
 *         走需求变更而非代码私改）
 */
View3DSessionPoseContract view3DSessionPoseContract();

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_VIEW3DCONTRACT_HPP
