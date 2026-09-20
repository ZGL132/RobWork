/**
 * @file   CommandPalette_p.hpp
 * @brief  命令面板私有实现头（R-2：不进 include/、不跨单元暴露——§3.3
 *         "私有实现（ui/src/）：……CommandPalette_p……"）。
 *
 * 设计依据：
 *   - units/ui.md §7.4（命令面板：CommandRegistry 的只读投影；模糊搜索；
 *     键盘导航 ↑↓ 选择/Enter 执行/Esc 关闭/Tab 补全类别；全程无鼠标可达
 *     ——UX-13；可见性按 §7.5 谓词过滤、"禁用＋说明"保留发现性；最近使用
 *     置顶分组、用户级持久化最近 20 条——PM-14）、§10.3（paletteSnapshot
 *     提供投影行）；
 *   - 任务契约 tasks/foundation/UI-T06.json acceptance 2（模糊搜索与键盘
 *     导航；未绑定命令经面板可达）。
 *
 * 背景说明（搜索逻辑为何不在本面板）：模糊匹配/加权/稳定排序的唯一实现点
 * 在 CommandRegistry::paletteSnapshot（§10.3 原文签名——模型层可断言，
 * 见 CommandRegistryModelTest）；本面板只做**消费**：取快照、渲染行、
 * 转发键盘事件、执行选中命令。面板不复制任何执行逻辑（§7.4 原文"不复制
 * 执行逻辑"）。
 *
 * 无 Q_OBJECT 声明（零信号槽/动属性——键盘交互经 keyPressEvent/
 * eventFilter 虚函数覆盖，CMake 不开 AUTOMOC 的既有纪律延续，登记
 * ui.md §16.7 v0.5 实现口径）。
 *
 * 线程模型：仅 UI 线程（§3.4 M-1）。
 */

#ifndef SDURWS_IRD_UI_COMMANDPALETTE_P_HPP
#define SDURWS_IRD_UI_COMMANDPALETTE_P_HPP

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QWidget>

#include <memory>
#include <optional>

#include <sdurws/ird/ui/ICommandRegistry.hpp>
#include <sdurws/ird/ui/IGlobalShortcutRegistry.hpp>

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

/// 面板结果上限（§7.4 原文数值——超出显示"继续输入以缩小范围"）。
inline constexpr int kPaletteLimit = 50;

/**
 * @brief 命令面板（§7.4）——工作台命令的键盘可达入口（UX-13）。
 *
 * 生命周期/所有权：由壳按需创建（一次创建反复显隐，父＝主窗口——Qt 树
 * 管理）；壳在 shutdown 时随窗口树一并销毁，不手动 delete。
 *
 * 非线程安全：仅 UI 线程（§3.4）。
 */
class CommandPalettePanel final : public QWidget {
public:
    /**
     * @brief 构造（主窗口的浮动子面板——顶部居中浮现，构造后隐藏，仅经
     *        open() 呈现）。
     *
     * 为什么不是 Qt::Popup 顶级窗口：Popup 的抓取语义会被事件泵中的任何
     * 外部事件关闭（键盘自动化/程序化提交场景不确定）；§7.4 键盘导航表
     * 以 Esc/Enter 收口，收口纪律不依赖点击外关闭（GUI 测试实测登记
     * ui.md §16.7 v0.8）。
     *
     * @param commands  [in] 命令注册表（投影与执行数据源；非拥有——须先于
     *                  面板装配完成/seal，生命周期覆盖面板）
     * @param shortcuts [in] 全局快捷键表（行内键显示文本的数据源；可空＝
     *                  无键显示——纯面板可达性场景）
     * @param parent    [in] 父控件（主窗口；Qt 父子析构纪律）
     */
    CommandPalettePanel(ICommandRegistry* commands, IGlobalShortcutRegistry* shortcuts,
                        QWidget* parent);

    /// @brief 打开面板（清空过滤词、重取快照、首行选中、焦点入过滤框）。
    void open();

    /// @brief 关闭面板（隐藏＋过滤态复位——Esc 路径与失焦路径共用）。
    void closePalette();

    /// @brief 面板当前是否可见（壳/测试观测面）。
    bool isOpen() const;

protected:
    /// @brief 键盘导航主路径（面板级：Esc 关闭/Enter 执行/Tab 补全类别）。
    void keyPressEvent(QKeyEvent* event) override;

    /// @brief 过滤框的键盘事件转发（↑↓/Enter 从输入框导向列表——输入不
    ///        离手完成导航，UX-13"全程无鼠标可达"）。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// @brief 按当前过滤词重建结果列表（paletteSnapshot 消费＋近期置顶
    ///        分组渲染＋"禁用＋说明"行）。
    void repopulate();

    /// @brief 执行当前选中命令（Enter——经 registry.submit 统一路径，
    ///        §10.4 副作用行"不经旁路"；执行后收起面板）。
    void executeCurrent();

    /// @brief Tab 补全类别（§7.4——以选中命令的分类收窄结果集；再按
    ///        Tab 清除分类过滤）。
    void completeCategory();

    /// @brief 行的显示文本组装（标题＋分组＋键位＋禁用原因——"禁用＋说明"）。
    static QString rowText(const CommandView& view, const QString& keyText);

    ICommandRegistry* m_commands;                 ///< 命令注册表（非拥有）
    IGlobalShortcutRegistry* m_shortcuts;         ///< 快捷键表（非拥有；可空）
    QLineEdit* m_filter = nullptr;                ///< 过滤输入框（窗口树子）
    QListWidget* m_list = nullptr;                ///< 结果列表（窗口树子）
    QLabel* m_hint = nullptr;                     ///< 底部提示行（上限/分类过滤态）
    std::optional<CommandCategory> m_categoryFilter; ///< Tab 补全的分类过滤态
};

/**
 * @brief 创建命令面板（壳集成入口——返回面板指针，所有权随 Qt 父子树）。
 *
 * @param commands  [in] 命令注册表（非拥有）
 * @param shortcuts [in] 快捷键表（非拥有；可空）
 * @param parent    [in] 父控件（主窗口）
 * @return 未显示的面板（open() 首次呈现）
 */
CommandPalettePanel* createCommandPalette(ICommandRegistry* commands,
                                          IGlobalShortcutRegistry* shortcuts,
                                          QWidget* parent);

}  // namespace detail
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_COMMANDPALETTE_P_HPP
