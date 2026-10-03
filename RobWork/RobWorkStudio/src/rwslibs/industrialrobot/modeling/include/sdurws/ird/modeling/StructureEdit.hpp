/**
 * @file   StructureEdit.hpp
 * @brief  关节链结构编辑原语（§5.2 v0.28"关节链结构编辑"四操作词表的域
 *         级实现——新增/删除/重排/六轴重置；UI-T47）。
 *
 * 设计依据：
 *   - units/modeling.md §5.2 v0.28 增补段（四操作语义的单元卡权威——本
 *     头为其代码落点；中间关节删除语义、C4 参数保留语义、六轴重置覆盖
 *     性均以该段登记为准）；
 *   - §5.1 T-MDL-1（六轴模板表值——重置与新增种子的参数来源）；
 *   - §4.3-A JointEntry/LinkEntry 值模型（I-MDL-1 计数关系
 *     links.size()==joints.size()+1——四操作共同守卫）；
 *   - §5.2 临时句柄纪律（新增/重置的关节/连杆身份＝确定性派生句柄——
 *     提交时命令 prepare 经 HandlerContext.objectId() 回填，PA-1）；
 *   - MDL-09（结构变更下游失效就地提示——摘要记录承载）；UX-03（拒绝
 *     路径工作集字节不变——域内强保证，与 applyJointFieldEdit 同款契约）；
 *   - 任务契约 tasks/foundation/UI-T47.json acceptance 1~5（G1＋G2）。
 *
 * 背景说明（第一读者须知——为什么是纯函数而不是 IRobotDesignEditor）：
 *   本头四个原语与 applyJointFieldEdit（Template.hpp，T07 内核）同一
 *   形态——**域纯函数裁决，工作集就地变更**；T08 编辑器/面板编辑流
 *   （PanelEditFlow）在其上组装提交流（接受→onEditApplied＋脏通知；
 *   拒绝→比较型呈现）。拒绝路径**零副作用**（工作集字节不变——先校验
 *   后变更的实现纪律）。变更一律追加 ModelingChangeRecord（§9.4.1 摘要
 *   记录——MDL-09 下游重算提示的载体）。
 *
 * 线程安全：非线程安全（编辑态仅 UI 线程——ModelingWorkingSet 注）。
 * 确定性：纯函数、同输入同工作集（NFR-COR-02——临时句柄为确定性派生，
 * 无随机源）。
 */

#ifndef SDURWS_IRD_MODELING_STRUCTUREEDIT_HPP
#define SDURWS_IRD_MODELING_STRUCTUREEDIT_HPP

#include <cstddef>
#include <optional>
#include <string_view>

#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet（编辑态载体——§9.4.1）

namespace sdurws::ird::modeling {

// =====================================================================
// StructureEditErrorCode——结构编辑局部错误码（JointEditErrorCode 同款
// "接口局部错误枚举"先例——不进域级错误轨道；呈现文案归 UI 按码映射）
// =====================================================================

/**
 * @brief 结构编辑拒绝面（§5.2 v0.28 四操作词表的拒绝项汇集；token 见
 *        structureEditErrorCodeToken）。
 */
enum class StructureEditErrorCode {
    /// "index-out-of-range"——操作下标越界（增：index>n；删：index≥n；
    /// 重排：index≥n）
    IndexOutOfRange,
    /// "would-delete-last-joint"——删除后零关节（I-MDL-1 ≥1 的拒绝面）
    WouldDeleteLastJoint,
    /// "reorder-at-boundary"——重排越界（首关节上移/尾关节下移）
    ReorderAtBoundary,
    /// "referenced-by-tcp"——引用保护（词表保留面：被删对象被 TCP/工具
    /// 引用时拒绝——I-MDL-9；常规链 TCP 挂工具不挂关节/连杆，此面为
    /// 将来引用扩展的防静默丢失保留，见 §5.2 v0.28 删除段）
    ReferencedByTcp,
};

/// 局部错误码稳定 token（枚举成员名连字符串——UT 判别与 UI 呈现映射承载）。
std::string_view structureEditErrorCodeToken(StructureEditErrorCode code) noexcept;

/**
 * @brief 结构编辑拒绝值（局部错误面：码＋定位细节——detail 面向诊断链/
 *        日志；呈现文案归 UI 层按码映射＋detail 直投，UX-03 比较型三要素）。
 */
struct StructureEditError {
    StructureEditErrorCode code = StructureEditErrorCode::IndexOutOfRange;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8——下标/链规模/原因）

    bool operator==(const StructureEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const StructureEditError& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 四操作原语（§5.2 v0.28 词表逐条；全部"先校验后变更"——拒绝路径工作集
// 字节不变；接受路径追加一条 ModelingChangeRecord）
// =====================================================================

/**
 * @brief 新增（选中位插入）：在 joints[index] 之后插入新关节＋在
 *        links[index+1] 位置插入新连杆（I-MDL-1 恒保持；index==n 缺省
 *        ＝链尾追加）。
 *
 * 新对象为设计默认种子（§5.2 v0.28 ①）：关节＝T-MDL-1 J1 行同族
 * （Revolute＋axis Z＋bounds ±π＋zeroOffset 0＋origin 恒位姿）；连杆＝
 * 种子连杆（物性 NotProvided＋材料钢——DraftIdentity 共享实现）。
 * localName 按链序自动命名并在同 scope 冲突时顺序消歧（"j<序>"/
 * "l<序>"——I-MDL-2 作用域唯一）；身份为确定性临时句柄（提交时回填）。
 *
 * @param ws    [in,out] 编辑态工作集（接受时就地变更＋追加变更记录）
 * @param index [in] 插入位（0..n；n＝链尾——越界拒绝）
 * @return nullopt＝接受；错误值＝拒绝（工作集未动）
 */
std::optional<StructureEditError> addJointAt(ModelingWorkingSet& ws,
                                             std::size_t index);

/**
 * @brief 删除：移除 joints[index] 并连带移除 links[index+1]（下游连杆；
 *        尾关节 index==n−1 时移除 links[n]——I-MDL-1 恒保持）。
 *
 * 中间关节删除语义（§5.2 v0.28 ②登记）：下游重算提示（变更记录承载
 * MDL-09 提示语义——UI 呈现层按摘要呈现）而非拒绝；被删对象上的物性/
 * 几何引用随对象移除（资源清单悬空归 §8.2 L6 悬空检测呈现）。
 *
 * @param ws    [in,out] 编辑态工作集
 * @param index [in] 待删关节下标（0..n−1；越界拒绝）
 * @return nullopt＝接受；错误值＝拒绝（WouldDeleteLastJoint＝删除后零
 *         关节 I-MDL-1；ReferencedByTcp＝引用保护词表保留面）
 */
std::optional<StructureEditError> removeJointAt(ModelingWorkingSet& ws,
                                                std::size_t index);

/**
 * @brief 重排（上移/下移）：joints[index] 与相邻关节交换数组位置，
 *        links[index+1] 与相邻连杆同步交换（连杆随关节走——§5.2 v0.28 ③；
 *        C4 参数保留＝身份不变参数随动，dhDerived 重算不冒充权威）。
 *
 * @param ws    [in,out] 编辑态工作集
 * @param index [in] 待重排关节下标（0..n−1）
 * @param down  [in] false＝上移（与 index−1 交换）；true＝下移（与
 *              index+1 交换）；边界越界拒绝（首上移/尾下移）
 * @return nullopt＝接受；错误值＝拒绝（IndexOutOfRange/ReorderAtBoundary）
 */
std::optional<StructureEditError> reorderJoint(ModelingWorkingSet& ws,
                                               std::size_t index, bool down);

/**
 * @brief 六轴重置：整链重置为 §5.1 T-MDL-1 六轴模板参数（关节/连杆全部
 *        重建为模板种子——与模板创建同构；§5.2 v0.28 ④）。
 *
 * **有损操作**（既有链参数被表值覆盖）——UI 侧显式确认对话承载知情，
 * 本原语纯执行不内嵌确认。重建身份为新的确定性临时句柄（旧身份随旧
 * 草稿态废弃——无"保留用户参数"的混合态）。
 *
 * @param ws [in,out] 编辑态工作集（六轴以下/以上链均整体替换为六轴）
 * @return nullopt＝接受；错误值＝拒绝（当前词表无拒绝面——保留返回形态
 *         与三操作对称，将来引用面扩展不破坏签名）
 */
std::optional<StructureEditError> resetToSixAxis(ModelingWorkingSet& ws);

}  // namespace sdurws::ird::modeling

#endif  // SDURWS_IRD_MODELING_STRUCTUREEDIT_HPP