/**
 * @file   Compare.hpp
 * @brief  容差与比较——Tolerance（附录 D C4 公式）＋allCloseWithin＋C7 转写。
 *
 * 设计依据：
 *   - units/core.md §4.5（类型/函数契约表）、§5.5（签名）、D-06（C7 转写唯一归 core，
 *     修改只能随需求变更）、§4.4（rw::math 聚合不包装——allCloseWithin 消费 Q）
 *   - 需求 NFR-COR-02/03（确定性；非有限不静默通过）；SA-12 精神（比较公式单点实现，
 *     testkit 黄金对照消费同一公式——NFR-COR-01）
 *   - 任务契约 tasks/foundation/CORE-T05.json（≙WP-03-T05）
 *
 * 核心公式（附录 D C4）：closeWithin ⇔ |value−reference| ≤ relative·|reference| + absolute。
 * 零参考退化为纯 absolute（不要求精确零）；任一输入非有限 → false（不抛、不静默通过
 * ——NFR-COR-03）。allCloseWithin 逐元素应用（防正负抵消——不以差值总和替代）。
 *
 * core 不持有任何业务阈值：runtimeAbsoluteTolerance 是附录 D C7 的机械转写
 * （angle/length 1e-12、velocity/acceleration/torque 1e-9、dimensionless 1e-12；
 * 其余量纲 nullopt——未声明默认的量不得引入产品侧相对校验）。
 *
 * rw::math::Q 重载的条件编译：Q 消费 sdurw_math（仅集成配置树存在——RT-T01
 * 探测先例）；冒烟模式无框架目标，vector<double> 重载始终可用。两模式差异在
 * ird-test-report 的 modes 字段如实分记。
 *
 * 线程安全：全部纯函数/纯值，无共享状态。
 */

#ifndef SDURWS_IRD_CORE_COMPARE_HPP
#define SDURWS_IRD_CORE_COMPARE_HPP

#include <optional>
#include <vector>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Units.hpp>   // QuantityKind（C7 转写的键）

#if __has_include(<rw/math/Q.hpp>)
#define IRD_CORE_HAS_RW_Q 1
#include <rw/math/Q.hpp>
#endif

namespace sdurws::ird::core {

/**
 * @brief 容差二元组（相对＋绝对——C4 公式的两个分量）。
 *
 * 数值来源归各所有者（附录 D 类别：固定/分析配置默认/工程策略默认）——
 * core 只校验合法性（两分量 ≥0 且有限）。
 * 值语义；线程安全。
 */
struct Tolerance {
    double relative = 0.0;   ///< 相对分量（×|reference|）
    double absolute = 0.0;   ///< 绝对分量（零参考退化项）

    /**
     * @brief 工厂：两分量均须 ≥0 且有限，否则抛 CoreError（"core/compare/tolerance:"）。
     */
    static Tolerance make(double relative, double absolute)
    {
        const bool relOk = relative >= 0.0 && relative == relative
                        && relative - relative == 0;
        const bool absOk = absolute >= 0.0 && absolute == absolute
                        && absolute - absolute == 0;
        if (!relOk || !absOk) {
            throw CoreError("core/compare/tolerance: 两分量均须 ≥0 且有限"
                            "（相对 " + std::to_string(relative) + "，绝对 "
                            + std::to_string(absolute) + "）");
        }
        Tolerance t;
        t.relative = relative;
        t.absolute = absolute;
        return t;
    }

    bool operator==(const Tolerance& o) const noexcept
    {
        return relative == o.relative && absolute == o.absolute;
    }
    bool operator!=(const Tolerance& o) const noexcept { return !(*this == o); }
};

/**
 * @brief C4 通用比较公式（noexcept）：|value−reference| ≤ relative·|reference| + absolute。
 *
 * 零参考退化为纯 absolute（不要求精确零）；任一输入非有限 → false
 * （不抛、不静默通过——NFR-COR-03）。
 */
bool closeWithin(double value, double reference, Tolerance t) noexcept;

/**
 * @brief 逐元素 C4（std::vector<double>）：全部满足才 true。
 *
 * 逐元素（C4：不以差值总和替代——防正负抵消，UT 用例钉住）；
 * 长度不等抛 CoreError（"core/compare/length:"——契约违约显性化）。
 */
bool allCloseWithin(const std::vector<double>& a, const std::vector<double>& b,
                    Tolerance t);

#ifdef IRD_CORE_HAS_RW_Q
/**
 * @brief 逐元素 C4（rw::math::Q 重载——轨迹速度限制校验等消费形态，TRJ-05）。
 * 语义同 vector 版本（长度不等抛 CoreError）。
 */
bool allCloseWithin(const rw::math::Q& a, const rw::math::Q& b, Tolerance t);
#endif

/**
 * @brief 附录 D C7"产品运行校验 ε_abs 默认值"的机械转写（D-06：单点转写，
 *        修改只能随需求变更）。
 *
 * 已声明量纲：angle→1e-12；length→1e-12；linear/angular-velocity→1e-9；
 * linear/angular-acceleration→1e-9；torque→1e-9；dimensionless→1e-12。
 * 其余量纲（mass/time/force/inertia/power/voltage）返回 nullopt——附录 D 未声明
 * 默认，按 C7"无默认且无配置来源的量不得引入产品侧相对校验"执行。
 */
std::optional<double> runtimeAbsoluteTolerance(QuantityKind k) noexcept;

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_COMPARE_HPP
