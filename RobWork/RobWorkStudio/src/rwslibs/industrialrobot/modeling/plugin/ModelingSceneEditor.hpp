/**
 * @file ModelingSceneEditor.hpp
 * @brief MDL-15/AT-28：场景草稿事务和内联管理页，世界位姿仍由既有域编辑器负责。
 *
 * 依据modeling §4.5/§6.7、WP-13-T24 B3f。仅私有装配，不新增对象格式。
 * 工作集和请求均按值消费；Qt父子树拥有窗口，仅UI线程使用，不跨域维护引用图。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGSCENEEDITOR_HPP
#define IRD_MODELING_PLUGIN_MODELINGSCENEEDITOR_HPP
#include <sdurws/ird/modeling/GeometryLinkEdit.hpp>
#include <QWidget>
#include <functional>
#include <cstdint>
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
namespace sdurws::ird::modeling {
/** @brief 单次场景事务的动作；删除仅解除当前草稿引用，历史由project持有。 */
enum class SceneEditAction { Create, Update, Remove };
/** @brief 几何来源；None表示固定帧，Existing引用清单，External经io探测后登记。 */
enum class SceneGeometrySource { None, Existing, External };
/** @brief 已显式确认的事务值；调用方拥有，纯值求值期间只读，全部位姿保持原世界系值。 */
struct SceneEditRequest {
    SceneEditAction action=SceneEditAction::Create;
    core::ObjectId target; ///< 创建时由交互层分配，更新/删除时必须精确命中已有身份。
    std::string localName; ///< 非空且仅[A-Za-z0-9_.-]，同工作集命名对象不能冲突。
    SceneObjectRole role=SceneObjectRole::EnvironmentObject; ///< 管理页只创建/改写环境或工件角色。
    SceneGeometrySource source=SceneGeometrySource::None;
    std::string resourceOrPath; ///< Existing资源键；External用户明确选择的绝对路径。
    bool confirmRemove=false; ///< 删除显式确认，默认不确认。
};
/**
 * @brief 对完整副本求值并返回候选；无写入/无Qt调用/无额外历史。
 * @param before [in] 当前基线，不被修改。
 * @param request [in] 一次完整事务；几何局部变换仅换来源时也保留，避免静默移动。
 * @param probe [in] 仅External使用的io端口缝；缺失或拒绝返回失败值。
 * @param error [out] 中文失败原因；成功清空。
 * @return 候选工作集；错误返回nullopt，恒等更新返回与before相等的值。
 * 资源键按res-N首空位，摘要由io提供；非线程安全编辑态由UI串行调用。
 */
std::optional<ModelingWorkingSet> evaluateSceneEdit(const ModelingWorkingSet& before,
    const SceneEditRequest& request,const ResourceProbeFn& probe,std::string& error);

/** @brief 场景管理内联页；Qt父对象拥有，持完整基线副本，仅UI线程访问。 */
class ModelingSceneEditor final : public QWidget {
public:
    /// 同步接受出口：核对before再一次移交after；id为空表示删除后清选择，失败填原因。
    using Accept=std::function<bool(const ModelingWorkingSet&,const ModelingWorkingSet&,
        std::optional<core::ObjectId>,std::string&)>;
    /// 选择出口：仅改变会话选中，不修改草稿；空值进入新增模式。
    using Choose=std::function<void(std::optional<core::ObjectId>)>;
    /** @brief 构造内联页；[in] parent负责释放，[in] accept/choose在本窗口生命周期内同步调用。 */
    ModelingSceneEditor(QWidget* parent,Accept accept,Choose choose);
    /** @brief 绑定现取草稿值；[in] ws空则无会话禁用，[in] id空为新增，[in] writable控制写入口。 */
    void bindTarget(const ModelingWorkingSet* ws,std::optional<core::ObjectId> id,bool writable);
    /** @brief 丢弃未确认输入并恢复基线；零工作集写入。 */
    void cancelPending();
private:
    /** @brief 解析完整显式输入，纯值求值，再经出口重查基线；失败保留输入。 */
    void submit(bool remove);
    /** @brief 更新来源选择及只读/目标有效性对应的使能，不触碰工作集。 */
    void updateInputs();
    Accept m_accept;
    Choose m_choose;
    std::optional<ModelingWorkingSet> m_before; ///< 仅本次待确认基线，非权威缓存。
    std::optional<core::ObjectId> m_id;
    bool m_writable=false;
    std::uint64_t m_pendingGeneration=0; ///< UI线程暂存代际；目标/基线/权限/取消变化使旧文件选择结果失效。
    QComboBox *m_objects=nullptr,*m_role=nullptr,*m_source=nullptr,*m_resource=nullptr;
    QLineEdit *m_name=nullptr,*m_path=nullptr;
    QCheckBox* m_confirm=nullptr;
    QLabel* m_feedback=nullptr;
    QPushButton *m_browse=nullptr,*m_apply=nullptr,*m_remove=nullptr;
};
}
#endif
