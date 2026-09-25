/**
 * @file   PanelEditFlow.hpp
 * @brief  建模面板编辑流（零 Qt）——L-2 字段编辑流与 L-8 平行轴内联二选一
 *         的数据面（卡 §9.7.2）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7.2 L-2（applyEdit 接受→树/属性区/就绪条增量
 *     刷新＋notifySessionDirty；拒绝→就地比较型错误＋保留原值不弹模态——
 *     UX-03/05/07）、L-8（质心编辑→CentroidEditUnresolved→面板内联二选一
 *     〔平行轴迁移/覆盖完整张量，附迁移预览数值〕→带选择重提 applyEdit）
 *   - §9.4.1（编辑器唯一写入口的域内核＝applyJointFieldEdit——T07 落位）、
 *     §5.3 规则 2（质心确认流——applyCentroidEdit，T04 落位）
 *   - 需求 MDL-07/MDL-05、UX-03（就地原因）/UX-05（批量＋单位同显）/
 *     UX-07（内联非模态优先）
 *   - 任务契约 tasks/foundation/WP-13-T15.json acceptance 3
 *
 * 背景说明：插件零计算逻辑（DTB 禁止项）——本文件对编辑的**一切合法性
 * 判定零参与**：接受/拒绝唯一由计算库既有函数裁决（applyJointFieldEdit/
 * applyCentroidEdit——拒绝时工作集字节不变是其域内强保证）；本文件只负责
 * 调用编排与结果分流（接受→增量刷新＋脏通知；拒绝→就地错误出口）。"不弹
 * 模态"由结构保证：本头没有任何模态呈现面，拒绝一律经 sink 即时回传
 * （UX-07 内联非模态优先）。
 *
 * 线程约束：仅 UI 线程访问（§3.4——工作集为编辑态会话对象）。确定性：
 * 同输入序列→同调用序→同结果（无环境读取）。
 */

#ifndef IRD_MODELING_PLUGIN_PANELEDITFLOW_HPP
#define IRD_MODELING_PLUGIN_PANELEDITFLOW_HPP

#include <optional>
#include <string>

#include <sdurws/ird/modeling/PropertyEstimation.hpp>  // applyCentroidEdit/CentroidEditResolution（L-8 域级判定——T04）
#include <sdurws/ird/modeling/RobotDesign.hpp>         // BodyData/InertiaTensor（物性值面）
#include <sdurws/ird/modeling/Template.hpp>            // applyJointFieldEdit/JointEditError/JointEditField/JointEditValue（L-2 域级判定——T07）

namespace sdurws::ird::modeling {

// =====================================================================
// 编辑流出口（IPanelEditSink——widget 层实现；模型层测试以记录替身实现）
// =====================================================================

/**
 * @brief 就地编辑拒绝明细（L-2"就地比较型错误"的承载——UX-03）。
 *
 * codeToken＝域局部错误码稳定 token（jointEditErrorCodeToken 产出——
 * "authority-locked"/"limit-interval-invalid" 等）；detail＝域错误自带定位
 * 细节（subject/字段/原因——applyJointFieldError 的 detail 直投，零加工）。
 * 呈现文案的值解析归 widget 层文案设施（键即契约同案）——本结构只搬运
 * 域面事实。
 */
struct EditRejection {
    std::string codeToken;  ///< 域局部错误码 token（机器判别串）
    std::string detail;     ///< 域定位细节（UTF-8 原文——就地显示）

    bool operator==(const EditRejection& o) const noexcept
    {
        return codeToken == o.codeToken && detail == o.detail;
    }
    bool operator!=(const EditRejection& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 编辑流出口（widget 层实现；满足 L-2 的三路分流——卡 §9.7.2 行）。
 *
 * 生命周期/所有权：面板 widget 实现（非 owning 引用传入编辑流函数——
 * 调用期存活由调用方保证）。线程约束：全部回调仅在 UI 线程触发（§3.4）。
 */
class IPanelEditSink {
public:
    virtual ~IPanelEditSink() = default;

    /**
     * @brief 编辑接受（L-2：树/属性区/就绪条增量刷新信号——widget 据此
     *        现取工作集重投影，不使用旧副本）。
     *
     * @param subjectPath [in] 变更定位路径（如 "joints[1]"——域变更记录的
     *                    subject 值；定位精度供增量刷新裁剪刷新范围）
     */
    virtual void onEditApplied(const std::string& subjectPath) = 0;

    /**
     * @brief 会话脏通知（PM-04/PM-11——标题 `*` 标记的生产者接线点；
     *        与 ui DraftController.notifySessionDirty 汇合——接线随装配）。
     */
    virtual void notifySessionDirty() = 0;

    /**
     * @brief 编辑拒绝（L-2：就地错误＋保留原值——非模态；widget 在字段行
     *        原位呈现 reason，值控件回退显示工作集权威值）。
     *
     * @param rejection [in] 拒绝明细（域错误 token＋定位细节）
     */
    virtual void onEditRejected(const EditRejection& rejection) = 0;
};

// =====================================================================
// L-2 字段编辑流
// =====================================================================

/**
 * @brief 字段编辑流提交结果（L-2 两态——分流结果供测试/调用方判别）。
 */
enum class EditSubmitOutcome {
    Applied,  ///< 域接受（工作集已更新＋onEditApplied＋notifySessionDirty 已发）
    Rejected  ///< 域拒绝（工作集不变＋onEditRejected 已发——原值保留）
};

/**
 * @brief 提交一次关节字段编辑（L-2 唯一入口——§9.7.2 行"控件提交→
 *        applyEdit(ModelingEdit)"的建模侧落位）。
 *
 * 编排序（零判定——全部裁决在域函数内）：
 *   ① 调 applyJointFieldEdit（计算库唯一判定点——拒绝时工作集字节不变
 *      是其域内强保证，本函数不重复校验、不回滚）；
 *   ② nullopt＝接受：sink.onEditApplied（增量刷新）→sink.notifySessionDirty
 *      （脏标记——PM-04/PM-11）；
 *   ③ 非 nullopt＝拒绝：sink.onEditRejected（就地呈现；无任何模态路径）。
 *
 * @param ws         [in,out] 目标工作集（接受时域函数已更新——仅 UI 线程）
 * @param sink       [in] 编辑流出口（调用期存活——非 owning）
 * @param jointIndex [in] 关节下标（链序 0 起；越界＝域函数 fail-fast——
 *                    调用方契约违约直接异常上抛，不吞错）
 * @param field      [in] 待编辑字段（四值词表——JointEditField）
 * @param value      [in] 新值载荷（备择须与 field 匹配——违约 fail-fast）
 * @return 分流结果（Applied/Rejected——与 sink 回调一一对应）
 *
 * @throws std::invalid_argument 越界下标/备择不匹配（域函数上抛——
 *               调用方契约违约 fail-fast）
 *
 * 非 UI 线程调用＝契约违约（§3.4；调试期经 PanelRefresh 的线程守卫钉住）。
 */
EditSubmitOutcome submitJointFieldEdit(ModelingWorkingSet& ws, IPanelEditSink& sink,
                                       std::size_t jointIndex, JointEditField field,
                                       const JointEditValue& value);

// =====================================================================
// L-8 平行轴内联二选一（数据面——GUI 呈现登记为 V-30 用例，本任务不启动）
// =====================================================================

/**
 * @brief 质心编辑二选一的内联预览值（L-8"附迁移预览数值"的承载）。
 *
 * 数值全部来自域级 dry-run（applyCentroidEdit 在**副本**上按各分支执行
 * ——插件零计算逻辑：预览值是计算库产出，非面板推算）。migratedText＝
 * 分支 (a) 迁移后张量六分量摘要；overwrittenText＝分支 (b) 覆盖后张量
 * 摘要（即用户输入张量的回显）。两分支任一域拒绝（前置违约）时对应串
 * 为空——内联只呈现可得预览，不伪造数值。
 */
struct CentroidChoicePreview {
    std::string migratedText;    ///< 分支 (a) 迁移后惯量（"ixx, iyy, izz, ixy, ixz, iyz"，kg·m²）
    std::string overwrittenText; ///< 分支 (b) 覆盖后惯量（同格式；无输入张量时为空）
};

/**
 * @brief 计算 L-8 内联二选一的预览数值（dry-run——输入 body 不变）。
 *
 * @param body              [in] 当前物性组（只读；前置由域函数把关——
 *                          缺基准则两分支均拒绝，预览两串为空）
 * @param newCenterOfMass   [in] 用户输入的新质心（m，连杆系）
 * @param replacementTensor [in] 分支 (b) 的候选张量（kg·m²；nullopt＝该
 *                          分支无输入——overwrittenText 为空）
 * @return 预览值（migratedText 恒可得〔前置满足时〕；零工作集触碰）
 *
 * 纯函数；确定性；不抛（域前置违约走值面——见 applyCentroidEdit @pre）。
 */
CentroidChoicePreview centroidChoicePreview(const BodyData& body,
                                            const rw::math::Vector3D<double>& newCenterOfMass,
                                            const std::optional<InertiaTensor>& replacementTensor);

/**
 * @brief 提交一次质心编辑（L-8 主干——§5.3 规则 2 三分支的面板编排）。
 *
 * 编排序（零判定——三分支裁决唯一在 applyCentroidEdit）：
 *   ① choice=nullopt（用户尚未二选一）→ 域函数返回 CentroidEditUnresolved
 *      →本函数投递 onEditRejected（codeToken="CentroidEditUnresolved" 的
 *      域错误 token——Errors.hpp 映射面）并返回 Unresolved：widget 据此
 *      打开**内联**二选一（非模态——UX-07），预览数值经
 *      centroidChoicePreview 取得；
 *   ② choice=MigrateInertia/OverwriteInertia（用户带选择重提）→ 域接受：
 *      onEditApplied＋notifySessionDirty（选择已随域变更记录留痕——
 *      V-17"编辑差值记录选择"）；
 *   ③ 其余域拒绝（前置违约等）→ onEditRejected（就地——同 L-2）。
 *
 * @param body              [in,out] 目标物性组（拒绝时域保证不变）
 * @param sink              [in] 编辑流出口（同 submitJointFieldEdit）
 * @param newCenterOfMass   [in] 新质心（m，连杆系）
 * @param choice            [in] 用户处置选择（nullopt＝未选择——分支 c）
 * @param replacementTensor [in] 分支 (b) 新张量（kg·m²；他分支忽略）
 * @return 三态：Applied＝接受；Unresolved＝需内联二选一（分支 c）；
 *         Rejected＝其余就地拒绝
 *
 * 非 UI 线程调用＝契约违约（§3.4）。
 */
enum class CentroidSubmitOutcome { Applied, Unresolved, Rejected };
CentroidSubmitOutcome submitCentroidEdit(BodyData& body, IPanelEditSink& sink,
                                         const rw::math::Vector3D<double>& newCenterOfMass,
                                         std::optional<CentroidEditResolution> choice,
                                         const std::optional<InertiaTensor>& replacementTensor);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_PANELEDITFLOW_HPP
