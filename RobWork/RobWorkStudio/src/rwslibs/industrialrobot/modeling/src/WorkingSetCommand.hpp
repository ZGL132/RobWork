/**
 * @file WorkingSetCommand.hpp
 * @brief 完整建模工作集命令的私有规划与固定修订读取（WP-13-T24 B4c-2）。
 *
 * PM-04要求一次应用一个修订。完整槽、临时身份和历史逆放在此规划，
 * 断言/确认/双编译仍走原公共管线；本头不跨单元暴露，无Qt和磁盘读取。
 */
#ifndef IRD_MODELING_SRC_WORKINGSETCOMMAND_HPP
#define IRD_MODELING_SRC_WORKINGSETCOMMAND_HPP

#include <sdurws/ird/modeling/CommandHandlers.hpp>

namespace sdurws::ird::modeling {

/**
 * @brief 从固定修订及计划覆盖层解引用根指定的活动部件，忽略未被根引用的历史对象。
 * @param query [in] 同步借用的project查询端口；对象按修订中的精确(oid,cv)读取。
 * @param revision [in] 固定引用集，禁止替换成可变HEAD。
 * @param root [in] 候选根；SI物理量/来源/引用顺序原样保留。
 * @param rootId [in] 对应有效根身份。
 * @param writes [in] 本次计划的完整覆盖层；仅本次调用借用。
 * @return 完整活动工作集；引用缺失、类型/内嵌身份或Codec错误返回空。
 * @throws project::StoreError 已有引用的真实字节不可读取，按原数据/环境错误透传。
 * 纯值结果归调用方；无共享状态，遵守原命令执行线程约束。
 */
std::optional<ModelingWorkingSet> readReferencedWorkingSet(
    const project::IProjectQueryPort& query, const project::RevisionView& revision,
    const RobotDesign& root, const core::ObjectId& rootId,
    const std::vector<project::ObjectWrite>& writes);

/**
 * @brief PM-18：按固定修订规划旧五命令的历史逆放，槽形状和原字节保持兼容。
 * @param query [in] 调用方持有的只读口，同步借用，不随HEAD取数。
 * @param revision [in] 本次prepare固定修订，包含未活动的历史对象。
 * @param payload [in] Restore帧；槽须已有、唯一、类型和内嵌身份匹配，允许不带根槽。
 * @param baseline [in] 当前根引用的活动闭包，SI物理量；其值不被修改。
 * @param out [out] 完整计划；成功时按实际活动运行时差异设置requiresDualCompile，
 *        仅位姿集内容/引用及编辑日志差异不要求编译；失败不发布半计划，原canonical字节不重编码。
 * @return Planned或RejectedInvalidInput；真实存储异常沿既有查询契约透传。
 * 无持久取号、无共享状态；仅既有命令执行线程同步调用。
 */
DecodeOutcome planLegacyFixedRestore(const project::IProjectQueryPort& query,
    const project::RevisionView& revision, const CommandPayload& payload,
    const ModelingWorkingSet& baseline, project::CommandPlan& out);

/**
 * @brief 创建无会话可变态的完整工作集处理器；所有权转交project注册表。
 * @param services [in] 共用断言/策略/转换端口，借用目标须覆盖处理器生命周期。
 * @return 新处理器，命令token为apply-modeling-working-set，载荷版本仍为2。
 * @throws std::bad_alloc 分配失败；运行期仍仅由原命令槽同步调用。
 */
std::unique_ptr<project::ICommandHandler> makeWorkingSetCommandHandler(const HandlerServices& services);

} // namespace sdurws::ird::modeling
#endif
