/**
 * @file   Commands.cpp
 * @brief  trajectory 领域命令适配器的实现（WP-16-T10）——词表查表与零
 *         修订契约应答；零 rw/零 io/零域计算设施消费（结构红线由契约
 *         测试词表扫描钉住）。
 *
 * 设计依据：
 *   - units/trajectory.md §15.11（命令处理器薄适配——"零计算逻辑（计算
 *     在评估器）"）、§14.5.1（七命令词表）、§16.2（零修订四常量机器可
 *     断言）
 *   - 先例：dynamics src/Commands.cpp（WP-17-T08——DynamicsCommandHandler::
 *     handle 的词表查表实现形态；本 TU 同构同纪律）
 *
 * 双模式编译（无 gating）：本 TU 仅消费 std 头与 Commands.hpp（std-only），
 * 不构造任何 rw 对象、不触碰 io 通道——冒烟/集成两模式无条件编译链接
 * （dynamics Commands.cpp 先例；与 Ptp.cpp 等集成条件源的 gating 差异＝
 * 符号面不同，见 CMakeLists 注释）。
 *
 * 线程安全：handle 纯函数（无共享可变状态）；确定性（查表全序——
 * NFR-COR-02）。
 */

#include <sdurws/ird/trajectory/Commands.hpp>

namespace sdurws::ird::trajectory {

TrajectoryCommandOutcome TrajectoryCommandHandler::handle(
    std::string_view commandToken, const TrajectoryCommandPayload& payload) const
{
    // 第一步：词表成员判定（§15.11 表外 token 一律不受理）。
    // 精确串比较、大小写敏感——命令 token 是机器契约（ui CommandRegistry
    // 注册键），任何大小写/空白变体都视为未知命令，不做归一化容错
    // （NFR-COR-03：非法输入拒绝，不钳制不静默）。
    bool known = false;
    for (const std::string_view token : kCommandTokens) {
        if (token == commandToken) {
            known = true;
            break;
        }
    }
    if (!known) {
        // 未受理路径：四语义位保持默认全 false——未受理的命令不得对会话
        // 作任何效果承诺（含"零修订"位本身，那只对受理结果有意义）。
        TrajectoryCommandOutcome rejected;
        rejected.rejectionToken = "unknown-token";
        return rejected;
    }

    // 第二步：负载交叉校验（负载与命令错配＝UI 侧组装缺陷的显式应答面）。
    // payload.commandToken 为空＝调用方选择不校验（合法缺省——测试/轻量
    // 场景）；非空且不一致＝错配，拒绝。
    if (!payload.commandToken.empty() && payload.commandToken != commandToken) {
        TrajectoryCommandOutcome mismatch;
        mismatch.rejectionToken = "payload-token-mismatch";
        return mismatch;
    }

    // 第三步：受理——四语义位逐位取 kTrajectorySessionContract 钉住值。
    // 这是 AT-04"动画/导出不产生项目修订"的机器断言面：本函数是七命令
    // 唯一受理点，产出位只来自 constexpr 契约，实现侧不存在产生修订的
    // 代码路径（受理后仍返回——真实的任务提交/查表/写出由 T12 装配层
    // 在本应答之后执行，本处理器零计算零副作用，§15.11 硬边界）。
    TrajectoryCommandOutcome accepted;
    accepted.accepted = kTrajectorySessionContract.sessionStateOnly;
    accepted.sessionStateOnly = kTrajectorySessionContract.sessionStateOnly;
    accepted.writesDesignModel = kTrajectorySessionContract.writesDesignModel;
    accepted.producesRevision = kTrajectorySessionContract.producesRevision;
    accepted.invalidatesResults = kTrajectorySessionContract.invalidatesResults;
    accepted.rejectionToken.clear();
    return accepted;
}

}  // namespace sdurws::ird::trajectory
