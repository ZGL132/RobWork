/**
 * @file   UiTheme.hpp
 * @brief  工业风主题基建（UI-T37——调色板常量/QSS 模板/卡片容器）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T37.json acceptance 1（主题与卡片基建）
 *   - 设计规格 docs/superpowers/specs/2026-10-01-requirements-panel-ux-redesign.md
 *     §8（QSS 工业风规范五条：主色调/4px 栅格/卡片容器/字体/三件套）
 *
 * 背景说明：需求面板呈现层现代化需要统一的视觉词表——各批次各自内联
 * 颜色/间距会漂移成"彩虹化"。本头文件把设计规格 §8 的规范固化为单一
 * 出口：调色板常量（QSS 色值）、面板作用域主题安装、卡片容器工厂。
 * 仅消费 Qt Widgets（本单元即呈现单元——L2 零 Qt 红线不涉）。
 *
 * 线程约束：仅 UI 线程调用（QWidget 固有约束）。
 */

#ifndef IRD_UI_UITHEME_HPP
#define IRD_UI_UITHEME_HPP

#include <QString>
#include <QVBoxLayout>
#include <QWidget>

class QGroupBox;

namespace sdurws::ird::ui {

/**
 * @brief 工业风调色板（QSS 十六进制色值——设计规格 §8 第 1 条单一词表）。
 *
 * 全站仅 5 个强调色：主色工程蓝（选中/主按钮/焦点）、警示橙（警示条/
 * 必验标签）、成功绿（状态卡通过面）、正文/次级灰阶。任何新 UI 面禁止
 * 引入词表外色值（防彩虹化——呈现一致性 NFR-DEP-05）。UI-T65 增采样
 * 状态色族（见 kSampleGoodGl 起的词表块——F-495"颜色由统一 UI 词表
 * 提供"的单一供色点，三维后端与图例同源消费）。
 */
namespace palette {
inline constexpr const char* kWindow = "#F2F4F7";    ///< 窗口底（浅灰工业底）
inline constexpr const char* kCard = "#FFFFFF";      ///< 卡片底
inline constexpr const char* kCardBorder = "#DCDFE5";  ///< 卡片描边
inline constexpr const char* kPrimary = "#1E5AA8";   ///< 主色（工程蓝）
inline constexpr const char* kWarning = "#E8833A";   ///< 警示橙
inline constexpr const char* kSuccess = "#2E9E5B";   ///< 成功绿
inline constexpr const char* kText = "#1F2733";      ///< 正文
inline constexpr const char* kTextMuted = "#5B6470"; ///< 次级文本

// ---- 采样状态色（UI-T65——三维格元分色与图例色块的唯一供色点；GL 与
//      hex 两形态同源换算，消费面禁止各自硬编码）----

/// 达标绿（GL 三元组——三维格元 Good 档）。
inline constexpr float kSampleGoodGl[3] = {0.15f, 0.85f, 0.25f};
/// 临界/数据不足黄（GL——Weak 档）。
inline constexpr float kSampleWeakGl[3] = {0.95f, 0.80f, 0.15f};
/// 未达标红（GL——Failed 档）。
inline constexpr float kSampleFailedGl[3] = {0.90f, 0.15f, 0.15f};
/// 未采样灰（GL——NotSampled 档；工业灰阶——诚实空态辨识色）。
inline constexpr float kSampleNotSampledGl[3] = {0.55f, 0.58f, 0.62f};
/// 达标绿（QColor 呈现面——图例色块；与 kSampleGoodGl 同源换算）。
inline constexpr const char* kSampleGoodHex = "#26D940";
/// 临界黄（图例；与 kSampleWeakGl 同源换算）。
inline constexpr const char* kSampleWeakHex = "#F2CC26";
/// 未达标红（图例；与 kSampleFailedGl 同源换算）。
inline constexpr const char* kSampleFailedHex = "#E62626";
/// 未采样灰（图例；与 kSampleNotSampledGl 同源换算）。
inline constexpr const char* kSampleNotSampledHex = "#8C949E";

// ---- 区域框缺省蓝（UI-T65 返工——三维区域框 None/缺省档的唯一供色点；
//      值＝UI-T33 以来的区域框既有辨识蓝 (0.25, 0.45, 1.0)＝#4073FF。
//      返工缘由：首轮实现把缺省档改为 kPrimary 同值 (0.12, 0.35, 0.66)
//      且在三维后端硬编码——未评估态框色实际变更＋"单一供色点"纪律
//      自破（acc/ui-t65/1 阻断 B/E2）；本常量族恢复原值并词表化，GL 与
//      hex 同源换算，三维后端与图例禁止各自硬编码）----

/// 区域框缺省蓝（GL 三元组——三维后端 tintColor 的 None/不可达分支）。
inline constexpr float kRegionTintDefaultGl[3] = {0.25f, 0.45f, 1.0f};
/// 区域框缺省蓝（图例"当前选中区域"色块；与 kRegionTintDefaultGl 同源
/// 换算：0.25×255≈64=0x40、0.45×255≈115=0x73、1.0×255=255=0xFF）。
inline constexpr const char* kRegionTintDefaultHex = "#4073FF";
}

/**
 * @brief 分组容器工厂（UI-T37 返工对齐旧插件 QGroupBox 形态；返工⑤——
 *        标题行自绘＝折叠三角＋标题＋标题旁浅灰小号"?"，右侧机械"?"位
 *        退役；每卡可一键收起，折叠为纯呈现会话态零修订）。
 *
 * 结构：QGroupBox#ird_card（QSS 线框，原生标题退役——标题行走自绘行）＞
 * VBox［标题行（QToolButton#ird_card_fold 折叠三角 ▼/▶ ＋ QLabel#ird_card_
 * title ＋ QLabel#ird_card_help 浅灰小号"?"——helpText 挂 Tooltip，紧贴
 * 标题；去噪纪律：成段说明文字禁止直出面板）＋折叠体 QWidget（@p
 * contentOut 返回的内容布局挂其中；折叠时整体隐藏）］。
 *
 * @param parent         [in] Qt 父对象（常规所有权）
 * @param title          [in] 组标题（工程中文——UiText 词表消费方负责）
 * @param helpText       [in] 悬浮帮助文案（进 "?" 标签 Tooltip）
 * @param contentOut     [out] 返回内容布局指针（必填——分组必须被填充）
 * @param startCollapsed [in] 初始折叠态（默认展开；次要参数块如"高级参数"
 *                       传 true——与折叠三角同一机制，不再有第二折叠形态）
 * @return 分组框（所有权随 Qt 父子树）
 */
QGroupBox* createCard(QWidget* parent, const QString& title, const QString& helpText,
                      QVBoxLayout** contentOut, bool startCollapsed = false);

/**
 * @brief 应用工业风主题（面板作用域——widget 级 setStyleSheet）。
 *
 * 刻意不用 QApplication 级安装：宿主 chrome/其他域面板不受影响，主题
 * 词表的作用域＝装配本主题的容器子树。QSS 内容＝规格 §8 全部五条
 * （卡片/标题/分段钮/Switch/输入焦点/Tooltip——由常量拼装，避免第二
 * 色值源）。
 *
 * @param root [in] 主题作用域根控件（其子树生效；重复调用＝刷新样式）
 */
void applyIndustrialTheme(QWidget* root);

}  // namespace sdurws::ird::ui

#endif  // IRD_UI_UITHEME_HPP
