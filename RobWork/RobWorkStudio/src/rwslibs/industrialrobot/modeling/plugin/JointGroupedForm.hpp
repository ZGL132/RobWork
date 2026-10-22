/**
 * @file JointGroupedForm.hpp
 * @brief GF-A关节分组表单；只拥有Qt控件与暂存会话，权威采纳通过宿主回调。
 * 依据MDL-07、UX-05/07/09。父面板负责Qt生命周期，仅UI线程使用，非线程安全。
 */
#ifndef IRD_MODELING_JOINTGROUPEDFORM_HPP
#define IRD_MODELING_JOINTGROUPEDFORM_HPP
#include "JointFormSession.hpp"
#include <QWidget>
#include <functional>
#include <map>
class QLineEdit;class QLabel;class QComboBox;class QVBoxLayout;class QGridLayout;
class QPushButton;class QResizeEvent;
namespace sdurws::ird::modeling {
/** @brief 单一关节分组表单及内联差异窗口；回调均同步且由宿主持有有效工作集。 */
class JointGroupedForm final : public QWidget {
public:
    /** @brief 会话呈现偏好；跨关节保留，项目关闭即释放，不进入工程内容。 */
    struct Presentation {std::map<QString,bool> expanded;QString length="m",angle="rad";};
    /// 读取当前折叠和单位；返回独立值副本，仅UI线程。
    Presentation presentation() const {return {m_expanded,m_lengthUnit,m_angleUnit};}
    /// 新目标恢复浏览偏好，不改变SI候选或形成历史。
    void restorePresentation(const Presentation& state);
    using Submit=std::function<std::optional<JointFormEditError>(const JointEntry&,JointEntry&)>;
    /** @brief 创建表单；baseline采用SI，authority决定派生只读，submit仅在最终确认执行。 */
    JointGroupedForm(const JointEntry& baseline,AuthorityMode authority,Submit submit,QWidget* parent);
    /** @brief 刷新权威基线；脏页保留输入并标冲突，无脏值才跟随新基线。 */
    void bindCurrent(const JointEntry& joint,AuthorityMode authority,bool writable);
    /** @brief 查询未确认输入（含非法文本），供树/模式/项目离开守卫使用。 */
    bool hasPending() const {return m_session.dirty();}
    /** @brief 未确认时展现非模态离开区并保存action；无修改返回true，调用方可直接离开。 */
    bool requestLeave(std::function<void()> action);
    /** @brief 显式丢弃输入；历史恢复/关闭的调用方已完成其确认决议。 */
    void resetInputs();
    /** @brief 会话锚已变；保留旧输入供查看，显式重置后由宿主重新绑定新世代。 */
    void invalidateContext(std::function<void()> afterReset);
    /** @brief 当前目标OID来自打开基线，不借用树下标。 */
    core::ObjectId targetId() const {return m_session.baseline().objectId;}
protected:
    /** @brief 宽度协商后重排XYZ/RPY布局；不修改数据或折叠状态。 */
    void resizeEvent(QResizeEvent* event) override;
private:
    /** @brief 按当前关节类型重建分组字段；暂存会话仍由JointFormSession管理。 */
    void rebuildNumeric();
    /** @brief 同步主表文本/单位/错误；正在输入的非法原串保持可纠正。 */
    void refreshFields();
    /** @brief 定位字段，自动展开其分组；空键时仅显示全局原因。 */
    void showError(const std::string& key,const std::string& reason);
    /** @brief 组装并呈现完整差异，零工作集写入。 */
    void prepare();
    /** @brief 最终确认唯一写入口；失败保留输入，成功才刷新基线。 */
    void commit();
    /** @brief 单位选择仅修改显示模型，非法文本时回退单位下拉。 */
    void changeUnits();
    /** @brief 三分量宽窗同行、窄窗纵列，保持逻辑Tab序。 */
    void reflow();
    struct FieldView {QLabel* label;QLineEdit* edit;QLabel* unit;QGridLayout* grid;QWidget* group;int row;int column;};
    JointFormSession m_session; ///< 唯一暂存会话，不是正式模型。
    AuthorityMode m_authority; ///< 打开时权威模式，变化必须重新绑定。
    Submit m_submit; ///< 宿主回调，只借用，不接管工作集。
    QLineEdit* m_name=nullptr;QComboBox* m_type=nullptr;QComboBox* m_length=nullptr;QComboBox* m_angle=nullptr;
    QLabel* m_target=nullptr;QLabel* m_status=nullptr;QLabel* m_diff=nullptr;QLabel* m_recent=nullptr;
    QWidget* m_numeric=nullptr;QWidget* m_confirm=nullptr;QWidget* m_leave=nullptr;
    QVBoxLayout* m_body=nullptr;QPushButton* m_apply=nullptr;QPushButton* m_reset=nullptr;
    std::map<std::string,FieldView> m_fields;std::map<std::string,QString> m_raw;
    std::map<QString,bool> m_expanded; ///< 同页重建保留折叠态，仅呈现状态。
    std::function<void()> m_afterLeave; ///< 用户待决议的导航意图，面板销毁即释放。
    std::function<void()> m_afterContextReset; ///< 旧会话只允许重置，回调不采纳旧候选。
    std::optional<JointEntry> m_incoming; ///< 冲突时最新权威值，显式重置才采纳。
    bool m_writable=true,m_stale=false,m_updating=false,m_confirmThenLeave=false;
    QString m_lengthUnit="m",m_angleUnit="rad"; ///< 当前页显示偏好，不进工程内容。
};
}
#endif
