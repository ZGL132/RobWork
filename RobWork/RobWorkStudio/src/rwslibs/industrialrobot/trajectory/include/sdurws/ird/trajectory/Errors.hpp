/**
 * @file   Errors.hpp
 * @brief  trajectory 域错误类型——TrajectoryError（token 前缀 trajectory/...）
 *         与"调用方错误 fail-fast vs 环境失败素材"的错误语义分类落点。
 *
 * 设计依据：
 *   - units/trajectory.md §15.0（通用约定——错误类型行原文："域错误
 *     TrajectoryError : std::runtime_error（token 前缀 trajectory/...，如
 *     trajectory/input-invalid）；调用方契约违约 fail-fast（断言/异常，
 *     不转诊断码）；环境类失败返回素材/诊断（ERR-01），稳定码归属＝
 *     diagnostics StableCodeRegistry（TRJ-*，§14.4）"）、§15.1（PTP 规划
 *     接口的非法示例——前置违约 fail-fast 的两例）
 *   - 需求 ERR-01（错误语义与稳定诊断码分离）、TRJ-06（失败定位走素材轨
 *     ——FailedSegmentRecord，不在本头）、NFR-COR-03（非法输入拒绝，
 *     不钳制不静默）
 *   - 先例：core/Errors.hpp（CoreError 同款 token 异常形态——kinematics/
 *     dynamics 等域错误类型同构；本头按卡面 §15.0 明文取 runtime_error
 *     基类——与 core::CoreError（logic_error 系）的有意差异是卡面原文
 *     承接，非形态漂移）
 *
 * 背景说明（两类错误的分界——本头只承载第一类）：
 *   1. **调用方契约违约**（fail-fast，抛 TrajectoryError）：编程错误面——
 *      空候选解集却调用 PTP（§15.1 非法示例）、请求向量维度不一致、
 *      配置合法域违约、取消信号为空指针形态的误用等。此类错误表示
 *      "调用序列本身错了"，继续运行没有意义，必须在产生点显性失败；
 *      它们**不转诊断码**（诊断码是用户数据评估的产物，不是程序缺陷
 *      的包装）。
 *   2. **用户数据的领域失败**（素材轨，返回值承载）：IK 无解、端点越限
 *      位、序列环/悬空键、规划搜索未果等——是评估面对用户输入的合法
 *      结局，经 FailedSegmentRecord（TrjTypes.hpp）产出 TRJ-* 素材
 *      （§7.6 失败语义表），由评估器/证据面定级，不在本头。
 *
 * 线程安全：异常类型按值抛接，无共享状态；token() 纯函数。
 */

#ifndef IRD_TRAJECTORY_ERRORS_HPP
#define IRD_TRAJECTORY_ERRORS_HPP

#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::trajectory {

/**
 * @brief trajectory 域错误（调用方契约违约的 fail-fast 载体——§15.0）。
 *
 * token 语义：点分小写 token 形如 "trajectory/<面>/<违约点>"（§15.0 原文
 * 示例 "trajectory/input-invalid"），稳定可判别——调用方按 token 前缀
 * 分域捕获、按全文定位违约点；what() 返回 "token: 文案" 组合串（与
 * core::CoreError 同款呈现约定），文案为中文（AGENTS §7 沟通约定）。
 *
 * 生命周期：按值抛接（std::runtime_error 子类）；禁止以引用跨线程共享
 * 异常对象。线程安全（无共享可变状态）。
 */
class TrajectoryError : public std::runtime_error {
public:
    /**
     * @brief 构造域错误。
     *
     * @param token [in] 稳定错误 token（点分小写，"trajectory/" 前缀；
     *              空串属调用方编程错误——直接由断言面拒绝，本构造不校验，
     *              校验职责在各抛出点的书写纪律）
     * @param message [in] 中文违约文案（说明违了什么约、实际值是什么——
     *              不钳制不静默，NFR-COR-03）
     */
    TrajectoryError(std::string token, const std::string& message)
        : std::runtime_error(token + ": " + message),
          m_token(std::move(token))
    {
    }

    /**
     * @brief 稳定错误 token（"trajectory/" 前缀的点分小写串；不含文案）。
     * @return token 常引用（对象存活期内稳定——调用方在 catch 块内消费）
     */
    const std::string& token() const noexcept { return m_token; }

private:
    std::string m_token;   ///< 稳定错误 token（构造期一次冻结；不可变）
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_ERRORS_HPP
