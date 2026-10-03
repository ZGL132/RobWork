/**
 * @file   DomainReadinessSummaryCard.hpp
 * @brief  跨域就绪摘要卡（Right 区右栏）——三域就绪判定的只读汇总呈现
 *         （UI-T44；§4.2 右栏行"诊断摘要"的最小兑现）。
 *
 * 设计依据：
 *   - units/ui.md §4.2（右栏行：诊断摘要——UI-T13 规划的落地半区）；
 *   - units/ui.md §6.5/§11.2（DomainReadinessItem 值语义直投；数据源＝
 *     各域 IPluginUiModule::readonlyProjections——N-11 汇聚输入不加工，
 *     判定权威在域侧 checker，本卡零判定）；
 *   - B1-SPEC D5（共享检查器为右 Dock 唯一跨域属性呈现面——本卡是诊断
 *     半区，与属性半区分工并存，不承载属性编辑）；
 *   - UX-02（判定词经 UiText verdict.* 键——kVerdictTable 单点词表）。
 *
 * 背景说明：域面板各自呈现本域草稿的就绪条（判定明细）；本卡做跨域
 * 一眼总览（建模/需求/运动学三行判定词），消费时机＝项目打开/应用/
 * 撤销/重做等共享面刷新点（refreshSharedSurfaces 编排——编辑未应用的
 * 中间态不实时投递，卡头注明刷新时机，不冒名实时）。
 *
 * 所有权/生命周期：由宿主装配层（UiPlugin buildDockBody）创建持有；
 * setRows 值语义输入（零缓存外部状态）。线程约束：仅 UI 线程（§3.4）。
 */

#ifndef SDURWS_IRD_UI_DOMAINREADINESSSUMMARYCARD_HPP
#define SDURWS_IRD_UI_DOMAINREADINESSSUMMARYCARD_HPP

#include <QString>
#include <QWidget>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>  // core::EngineeringStatus（verdict 词表键映射）

class QLabel;
class QVBoxLayout;

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 摘要行值（宿主装配层由 DomainReadinessItem 投影而来——本卡不
 *        直接依赖域模块类型，消费面只见本值结构）。
 *
 * 所有权：值语义；verdictText/noteText 已是呈现文本（词表解析在装配侧
 * 的 engineeringStatusDisplayName 单点完成——本卡零词表依赖）。
 */
struct DomainReadinessSummaryRow {
    QString domainLabel;   ///< 域呈现名（建模/需求/运动学——宿主装配词）
    QString verdictText;   ///< 判定词（verdict.* 键解析值——UX-02）
    QString noteText;      ///< 附注（空＝无；如"缺项 N 项"的诚实提示面）
};

/**
 * @brief 判定状态→呈现词（verdict.* 键 UiText 解析；解析空回退键名原文
 *        ——呈现面不空洞，DomainAssembly standardTextResolver 同款兜底）。
 *
 * 纯函数（EngineeringStatus switch 全枚举）；UI 线程调用（实际约束——
 * 消费点全在装配/刷新路径）。
 *
 * @param status [in] 判定状态（core 词表——§6.5 值语义直投，不镜像枚举）
 * @return 判定呈现词（中文工程用语）
 */
QString engineeringStatusDisplayName(core::EngineeringStatus status);

/**
 * @brief 跨域就绪摘要卡（右栏只读卡——标题＋逐域判定行；非模态零交互）。
 *
 * 渲染形态：QFrame 卡（objectName＝ird_readiness_summary_card，供测试与
 * 布局定位）＋ QLabel 正文（逐行"域名：判定词[｜附注]"，setWordWrap）。
 * 空行集＝诚实空态行（"暂无域就绪投影"——不虚构三域齐全）。
 */
class DomainReadinessSummaryCard final : public QWidget {
public:
    /**
     * @brief 构造摘要卡（UI 线程——§3.4）。
     *
     * @param title  [in] 卡标题（如"跨域就绪摘要"）
     * @param parent [in] Qt 父对象（常规所有权）
     */
    explicit DomainReadinessSummaryCard(const QString& title,
                                        QWidget* parent = nullptr);

    /**
     * @brief 整卡重渲（值语义直投——rows 全量替换，零增量合并）。
     *
     * @param rows [in] 摘要行（宿主由各域 readonlyProjections 投影装配；
     *               空＝诚实空态行）
     */
    void setRows(const std::vector<DomainReadinessSummaryRow>& rows);

private:
    QLabel* m_body = nullptr;      ///< 正文（逐行判定词——重建成本低）
    QVBoxLayout* m_layout = nullptr;  ///< 布局（标题＋正文——预留扩展）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_DOMAINREADINESSSUMMARYCARD_HPP
