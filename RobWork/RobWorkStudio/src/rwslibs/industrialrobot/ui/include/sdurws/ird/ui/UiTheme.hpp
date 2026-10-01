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

class QFrame;

namespace sdurws::ird::ui {

/**
 * @brief 工业风调色板（QSS 十六进制色值——设计规格 §8 第 1 条单一词表）。
 *
 * 全站仅 5 个强调色：主色工程蓝（选中/主按钮/焦点）、警示橙（警示条/
 * 必验标签）、成功绿（状态卡通过面）、正文/次级灰阶。任何新 UI 面禁止
 * 引入词表外色值（防彩虹化——呈现一致性 NFR-DEP-05）。
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
}

/**
 * @brief 卡片容器工厂（设计规格 §8 第 3 条——QFrame#card 视觉＋标题行）。
 *
 * 结构：QFrame#card（白底圆角描边）＞ VBox［标题行（QLabel#ird_card_title
 * ＋"?" 帮助 QLabel——helpText 挂 Tooltip）＋ @p contentOut 返回的内容
 * 布局（调用方向其中加行/加控件）］。标题行与内容间 4px 间隔（4px 基数
 * 网格——规格 §8 第 2 条）。
 *
 * @param parent      [in] Qt 父对象（常规所有权）
 * @param title       [in] 卡片标题（工程中文——UiText 词表消费方负责）
 * @param helpText    [in] 悬浮帮助文案（进 "?" 图标 Tooltip——去噪纪律：
 *                    成段说明文字禁止直出面板，一律收纳于此）
 * @param contentOut  [out] 返回内容布局指针（必填——卡片必须被填充）
 * @return 卡片框架（所有权随 Qt 父子树）
 */
QFrame* createCard(QWidget* parent, const QString& title, const QString& helpText,
                   QVBoxLayout** contentOut);

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
