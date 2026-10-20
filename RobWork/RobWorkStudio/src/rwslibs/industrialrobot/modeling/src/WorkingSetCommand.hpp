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
 * @brief 创建无会话可变态的完整工作集处理器；所有权转交project注册表。
 * @param services [in] 共用断言/策略/转换端口，借用目标须覆盖处理器生命周期。
 * @return 新处理器，命令token为apply-modeling-working-set，载荷版本仍为2。
 * @throws std::bad_alloc 分配失败；运行期仍仅由原命令槽同步调用。
 */
std::unique_ptr<project::ICommandHandler> makeWorkingSetCommandHandler(const HandlerServices& services);

} // namespace sdurws::ird::modeling
#endif
