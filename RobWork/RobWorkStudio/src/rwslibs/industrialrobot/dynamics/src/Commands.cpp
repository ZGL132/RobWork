/**
 * @file   Commands.cpp
 * @brief  dynamics 领域命令适配器实现（Commands.hpp 的执行体——词表
 *         查表＋零修订语义应答）。
 *
 * 设计依据：见 Commands.hpp 文件头（§10.7 契约＋§9.5 词表＋零修订会话
 *   契约的对账口径）。本 TU 的结构性红线（契约测试钉住）：
 *   - 零动力学计算：本 TU 不 include/不消费任何评估器/统计器/投影器
 *     （Replay/Envelope/SeriesBuilder/InverseDynamics/ForwardDynamics/
 *     PowerEnergy/EvidenceBuilder）——handle 只是词表查表（§10.7"处理
 *     器内零动力学计算"的结构性保证）；
 *   - 零副作用：纯函数、零写盘、零修订（§10.0 副作用行——AT-04）。
 *
 * 线程安全：纯函数；确定性：词表精确匹配、无环境依赖（NFR-COR-02）。
 */

#include <sdurws/ird/dynamics/Commands.hpp>

#include <algorithm>

namespace sdurws::ird::dynamics {

CommandOutcome DynamicsCommandHandler::handle(std::string_view commandToken,
                                              const CommandPayload& payload) const
{
    CommandOutcome out;  // 默认＝拒绝形态（accepted=false＋四语义位 false）

    // ---- 第 1 步：词表收录校验（§9.5 词表封闭——表外 token 一律不受理；
    //      精确匹配、大小写敏感：token 是机器契约非自由文本，"dynamics.
    //      Analyze" 与 "dynamics.analyze" 是不同串，前者不受理）----
    const bool known = std::find(kCommandTokens.begin(), kCommandTokens.end(),
                                 commandToken)
                       != kCommandTokens.end();
    if (!known) {
        out.accepted = false;
        out.rejectionToken = "unknown-token";  // 表外 token——UI 侧可呈现
                                               //   "命令不可用"态（§10.7
                                               //   受理语义而非异常轨）
        return out;
    }

    // ---- 第 2 步：负载交叉校验（payload.commandToken 可空＝不校验；
    //      非空时必须与命令 token 一致——不一致＝UI 侧负载组装错位，
    //      受理将把 A 命令的效果挂到 B 命令的负载上，拒绝暴露）----
    if (!payload.commandToken.empty()
        && payload.commandToken != commandToken) {
        out.accepted = false;
        out.rejectionToken = "payload-token-mismatch";
        return out;
    }

    // ---- 第 3 步：受理——四语义位逐位取零修订契约钉住值（AT-04 机器
    //      断言面：任何命令的受理结果 producesRevision 恒 false；
    //      sessionStateOnly 恒 true——§9.5 表修订列全部"无"，全部会话
    //      命令）。零计算零副作用：本方法不触任何评估器/序列/归档。----
    out.accepted = true;
    out.sessionStateOnly = kReplaySessionContract.sessionStateOnly;   // true
    out.writesDesignModel = kReplaySessionContract.writesDesignModel; // false
    out.producesRevision = kReplaySessionContract.producesRevision;   // false
    out.invalidatesResults = kReplaySessionContract.invalidatesResults; // false
    out.rejectionToken.clear();
    return out;
}

}  // namespace sdurws::ird::dynamics
