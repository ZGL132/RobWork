/**
 * @file ModelingToolEditor.hpp
 * @brief MDL-13/04/05：工具完整草稿事务与内联管理，依据WP-13-T24 B3h。
 * 工具按独立ObjectId引用；项目历史/跨域删除保护由project权威处理。本页仅UI线程使用。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGTOOLEDITOR_HPP
#define IRD_MODELING_PLUGIN_MODELINGTOOLEDITOR_HPP
#include "GeometryResourceFlow.hpp"
#include "ModelingBodyEditor.hpp"
#include <cstdint>
#include <functional>
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
namespace sdurws::ird::modeling {
/** @brief 单次已确认工具事务；删除仅解除当前草稿引用，历史永存。 */
enum class ToolEditAction { Create, Update, Remove };
/** @brief 交互层拥有的工具事务纯值；安装/TCP/物性由原独立编辑页处理。 */
struct ToolEditRequest {
    ToolEditAction action=ToolEditAction::Create;
    core::ObjectId target; ///< 创建时新分配；更新/删除须唯一命中现有工具及根引用。
    std::string localName; ///< [A-Za-z0-9_.-]，完整模型内唯一，不拼装runtime名称。
    std::string displayName; ///< 仅人读，不改变运行名称；可空。
    ManagedGeometrySource source=ManagedGeometrySource::None;
    std::string resourceOrPath; ///< 已有资源键或用户明确选择的UTF-8绝对路径。
    bool makeDefault=false; ///< 明确设置本工具为默认TCP；更新沿原默认键，新增初始键tcp。
    bool confirmRemove=false; ///< 显式确认删除；默认拒绝。
};
/**
 * @brief 在完整副本上校验并求值工具事务，不写项目/草稿。
 * @param before [in] 当前完整基线；错误、取消和成功均不修改它。
 * @param request [in] 用户确认值；新工具初始TCP为工具原点，安装单位m/rad。
 * @param probe [in] External资源端口，真实安全读取由io负责；其他来源不调用。
 * @param error [out] 中文失败原因；成功清空。
 * @return 完整候选；拒绝返回nullopt。多工具不能直接删除当前默认，先显式选择其他默认。
 * 无Qt操作和共享状态；工作集仅UI串行使用。恒等更新不添加摘要或历史。
 */
std::optional<ModelingWorkingSet> evaluateToolEdit(const ModelingWorkingSet& before,
    const ToolEditRequest& request,const ResourceProbeFn& probe,std::string& error);

/** @brief Qt父对象拥有的内联工具管理器，基线按值保存，仅UI线程使用。 */
class ModelingToolEditor final : public QWidget {
public:
    /// 同步接受出口先核对完整基线再交换候选；selected为空为删除后的新增模式。
    using Accept=std::function<bool(const ModelingWorkingSet&,const ModelingWorkingSet&,
        std::optional<core::ObjectId>,std::string&)>;
    /// 仅改变选中；空值进入新增，不产生模型修改。
    using Choose=std::function<void(std::optional<core::ObjectId>)>;
    /** @brief 构造页面；[in] parent拥有窗口，[in] accept/choose同步消费值，不保存参数引用。 */
    ModelingToolEditor(QWidget* parent,Accept accept,Choose choose);
    /** @brief [in] ws当前完整基线，[in] id空为新增，[in] writable禁止写入时执行端也拒绝。 */
    void bindTarget(const ModelingWorkingSet* ws,std::optional<core::ObjectId> id,bool writable);
    /** @brief 取消本页及物性暂存，作废未返回的文件选择；零草稿写入。 */
    void cancelPending();
private:
    /** @brief 从控件读取确认值并求完整候选，出口复核，失败原位保留输入。 */
    void submit(bool remove);
    /** @brief 按来源、目标及权限刷新使能，无模型写入。 */
    void updateInputs();
    Accept m_accept;
    Choose m_choose;
    std::optional<ModelingWorkingSet> m_before; ///< 确认守卫值，非权威副本。
    std::optional<core::ObjectId> m_id;
    bool m_writable=false;
    std::uint64_t m_generation=0; ///< UI暂存代际，跨嵌套对话返回时复核。
    QComboBox *m_objects=nullptr,*m_source=nullptr,*m_resource=nullptr;
    QLineEdit *m_name=nullptr,*m_display=nullptr,*m_path=nullptr;
    QCheckBox *m_default=nullptr,*m_confirm=nullptr;
    QLabel* m_feedback=nullptr;
    QPushButton *m_browse=nullptr,*m_apply=nullptr,*m_remove=nullptr;
    ModelingBodyEditor* m_body=nullptr; ///< Qt子对象，复用唯一物性编辑/迁移求值。
};
}
#endif
