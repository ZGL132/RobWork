/**
 * @file ModelingBodyEditor.hpp
 * @brief MDL-05/F10：连杆权威物性编辑的私有纯值编排与内联窗口。
 *
 * 依据modeling §5.3/L-8，数学唯一归PropertyEstimator/applyCentroidEdit。
 * 本层持基线值，接受出口同步复核身份；Qt父对象拥有控件，仅UI线程使用。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGBODYEDITOR_HPP
#define IRD_MODELING_PLUGIN_MODELINGBODYEDITOR_HPP
#include <sdurws/ird/modeling/PropertyEstimation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <QWidget>
#include <array>
#include <functional>
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
namespace sdurws::ird::modeling {
/** @brief 一次显式物性编辑；空字段表示不修改，值为SI，不把未提供值猜成零。 */
struct ManualBodyEdit {
    std::optional<double> mass; ///< kg，输入须有限且正。
    std::optional<rw::math::Vector3D<double>> centerOfMass; ///< 连杆系m。
    std::optional<InertiaTensor> inertia; ///< 质心基准、连杆系姿态，kg·m²，完整六分量。
    std::optional<CentroidEditResolution> resolution; ///< 质心实际改变时显式选择，无默认。
};
/** @brief 一次已验证值与选择摘要；调用方拥有值，线程间可按值移交。 */
struct ManualBodyResult {
    BodyData body;
    std::string summary; ///< 编辑差值/后续命令摘要所携带的用户选择，不含隐式默认。
};
/**
 * @brief 对副本求值，复用唯一质心迁移和惯量判定，不写工作集。
 * @param before [in] 原物性，可缺失；不更改其来源和内容。
 * @param edit [in] 显式编辑，SI；迁移使用本轮已确认质量，缺基准拒绝。
 * @param error [out] 失败的中文纠错信息；成功清空。
 * @return 成功值；非法/未决议返回nullopt。无编辑时body等于before，调用方不记历史。
 * 纯函数、确定性；所有可预期输入错误走值面，不以异常代替用户纠错。
 */
std::optional<ManualBodyResult> evaluateManualBodyEdit(
    const BodyData& before,const ManualBodyEdit& edit,std::string& error);

/**
 * @brief 连杆物性内联编辑器；Qt父子树拥有，所有成员仅UI线程访问。
 * 基线为值副本，不持草稿引用；同目标同内容刷新保留暂存，换目标/基线清暂存。
 */
class ModelingBodyEditor final : public QWidget {
public:
    /// 同步接受出口；依次传目标、旧基线、已验证结果，失败应返回false并填写原因。
    using Accept = std::function<bool(const core::ObjectId&,const BodyData&,const ManualBodyResult&,std::string&)>;
    /** @brief 创建内联控件；[in] parent负责释放，[in] accept同步使用且不保留参数引用。 */
    explicit ModelingBodyEditor(QWidget* parent,Accept accept);
    /** @brief 绑定当前连杆值；[in] id空则禁用，[in] body为SI基线，[in] name为显示名，[in] writable为项目可写性。 */
    void bindTarget(std::optional<core::ObjectId> id,const BodyData& body,const std::string& name,bool writable);
    /** @brief 丢弃全部未确认输入并恢复当前基线；不写工作集，不增加局部历史。 */
    void cancelPending();
private:
    /** @brief 按勾选项解析SI并移交已验证值；失败原位提示，暂存保留。 */
    void submit();
    /** @brief 同步输入使能：未勾选不消费、迁移不消费旧的完整张量暂存。 */
    void updateInputs();
    Accept m_accept; ///< 面板拥有出口对象，生命周期被Qt父子树限制。
    std::optional<core::ObjectId> m_id;
    BodyData m_before; ///< 当前基线值副本，绝非第二个权威工作集。
    bool m_writable=false;
    std::array<QLineEdit*,10> m_values{}; ///< mass、com xyz、ixx/iyy/izz/ixy/ixz/iyz，Qt拥有。
    QCheckBox *m_mass=nullptr,*m_com=nullptr,*m_tensor=nullptr;
    QComboBox* m_resolution=nullptr;
    QLabel *m_title=nullptr,*m_feedback=nullptr;
};
}
#endif
