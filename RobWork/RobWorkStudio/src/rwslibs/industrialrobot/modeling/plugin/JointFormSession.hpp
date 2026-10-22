/**
 * @file JointFormSession.hpp
 * @brief GF-A：关节整表暂存会话；只保存用户意图，模型权威仍为ModelingWorkingSet。
 * 依据MDL-01/07、UX-05及2026-10-21分组表单设计。面板独占，仅UI线程使用。
 */
#ifndef IRD_MODELING_JOINTFORMSESSION_HPP
#define IRD_MODELING_JOINTFORMSESSION_HPP
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>
#include <memory>
#include <map>

namespace sdurws::ird::modeling {
/** @brief 一个稳定OID关节的编辑意图；面板拥有，不访问项目/三维，无线程共享。 */
class JointFormSession {
public:
    /** @brief 从草稿基线创建；位置m、角度rad，authority决定只读派生字段。 */
    JointFormSession(const JointEntry& baseline, AuthorityMode authority);
    /** @brief 取打开时值，只读借用，生命周期同本会话。 */
    const JointEntry& baseline() const { return m_baseline; }
    /** @brief 共享数值暂存模型；视图借用期间须持有返回的shared_ptr。 */
    std::shared_ptr<ui::ParamEditModel> model() const { return m_model; }
    /** @brief 当前候选类型；下拉变化不写工作集。 */
    JointType type() const { return m_type; }
    /** @brief 名称为原始文本，不修剪/消歧；合法性由领域确认裁决。 */
    void setName(std::string name) { m_name=std::move(name); }
    /** @brief 当前候选名称，只读借用。 */
    const std::string& name() const { return m_name; }
    /** @brief 切类型重建量纲；保留轴/原点，跨量纲清空运动输入，不自动把rad变m。 */
    bool stageType(JointType type);
    /** @brief 显式沿用原区间到新类型；仅角量类型间可用，无原区间则不填。 */
    void usePreviousInterval();
    /** @brief 组装最终候选；失败返回空并输出字段键和中文原因，不写任何外部值。 */
    std::optional<JointEntry> candidate(std::string& field, std::string& error) const;
    /** @brief 包含非法输入、名称/类型和数值的未确认状态；不使用模型脏标记替代整表比较。 */
    bool dirty() const;
    /** @brief 重置到当前已绑定草稿；确认成功后由宿主创建新会话。 */
    void reset();
private:
    /** @brief 用SI值构造14字段；空值保持缺供，模型自己管理显示单位。 */
    void rebuild(const std::map<std::string,std::optional<double>>& values);
    JointEntry m_baseline; ///< 打开时值；乐观并发检测用，不是可发布模型。
    AuthorityMode m_authority; ///< 打开时权威模式；宿主核对变化后拒绝旧页。
    JointType m_type; ///< 未确认类型。
    std::string m_name; ///< 未确认原始名称。
    std::shared_ptr<ui::ParamEditModel> m_model; ///< 唯一数值模型，所有分组字段共用。
};
}
#endif
