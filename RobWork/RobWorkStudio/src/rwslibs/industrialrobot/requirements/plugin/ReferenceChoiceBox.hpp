/**
 * @file ReferenceChoiceBox.hpp
 * @brief REQ-01/03/04/09名称目录的私有值选择器（WP-14-T12，单元卡§9.8）。
 *
 * 只持宿主目录的浅引用副本，不解析建模或runtime，不执行草稿写入。
 * Qt父对象拥有控件；所有方法限UI线程，非线程安全。公共契约不在此扩展。
 */
#ifndef IRD_REQUIREMENTS_PLUGIN_REFERENCECHOICEBOX_HPP
#define IRD_REQUIREMENTS_PLUGIN_REFERENCECHOICEBOX_HPP
#include <QComboBox>
#include <QStandardItemModel>
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>
namespace sdurws::ird::requirements {
/** @brief 名称行与引用值一一对应；原悬空值可保留，新不可用值不可确认。 */
class ReferenceChoiceBox final : public QComboBox {
public:
    /**
     * @brief 复制已装配目录并按使用场景筛选，缺原值时追加保留项。
     * @param choices [in] 宿主当前目录快照；不保存调用方指针。
     * @param kinds [in] 新选允许的引用种类，不限制保留原历史值。
     * @param original [in] 原持久值；nullopt为未指定或新增行，不伪造默认目标。
     * @param optional [in] true允许返回nullopt；false的提示空行在确认时拒绝。
     * @param parent [in] 非拥有Qt父对象，负责本控件生命周期。
     */
    ReferenceChoiceBox(const std::vector<RequirementsReferenceChoice>& choices,
        std::initializer_list<RequirementRefKind> kinds,
        std::optional<RequirementReference> original, bool optional, QWidget* parent)
        : QComboBox(parent), m_original(std::move(original)), m_optional(optional)
    {
        addItem(optional ? "未指定" : "请选择目标");
        m_references.push_back(std::nullopt); m_available.push_back(true);
        for (const auto& entry : choices) {
            if (std::find(kinds.begin(),kinds.end(),entry.reference.kind)==kinds.end()) { continue; }
            addItem(QString::fromStdString(entry.displayName)+(entry.available ? QString() : QString("（不可用）")));
            m_references.push_back(entry.reference); m_available.push_back(entry.available);
            const int row=count()-1;
            setItemData(row,QString::fromStdString(entry.unavailableReason),Qt::ToolTipRole);
            if (!entry.available) { static_cast<QStandardItemModel*>(model())->item(row)->setEnabled(false); }
        }
        if (m_original && std::none_of(m_references.begin(),m_references.end(),[&](const auto& value) {
            return value==m_original;
        })) {
            addItem("当前引用（目录中缺失，保留原值）");
            m_references.push_back(m_original); m_available.push_back(false);
            static_cast<QStandardItemModel*>(model())->item(count()-1)->setEnabled(false);
        }
        resetSelection();
    }
    /** @brief 取消时回到进入基线的精确引用；零草稿写入，不猜首个候选。 */
    void resetSelection()
    {
        const auto found=std::find(m_references.begin(),m_references.end(),m_original);
        setCurrentIndex(found==m_references.end() ? 0 : int(found-m_references.begin()));
    }
    /**
     * @brief 读取确认候选，不可用新选或必填空行抛invalid_argument供表单原位呈现。
     * @return 浅引用值副本；仅可选空行返回nullopt，不解析坐标或做单位转换。
     */
    std::optional<RequirementReference> reference() const
    {
        const int row=currentIndex();
        if (row<0 || std::size_t(row)>=m_references.size()
            || (!m_optional && !m_references[row])) { throw std::invalid_argument("请按业务名称选择引用目标"); }
        if (m_references[row] && ((!m_available[row] && m_references[row]!=m_original)
            || !m_references[row]->wellFormed())) { throw std::invalid_argument("引用当前不可用，请重新选择业务目标"); }
        return m_references[row];
    }
private:
    std::vector<std::optional<RequirementReference>> m_references; ///< 行号到浅引用值，无跨域对象指针。
    std::vector<bool> m_available; ///< 与行同步，防止程序选中灰显行绕过确认校验。
    std::optional<RequirementReference> m_original; ///< 取消和保留历史缺失引用的唯一基线。
    bool m_optional; ///< 仅可选TCP允许清空；必需参考系/负载不自动填值。
};
}
#endif
