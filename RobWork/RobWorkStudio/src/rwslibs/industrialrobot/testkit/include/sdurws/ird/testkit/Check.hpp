/**
 * @file   Check.hpp
 * @brief  数值断言与比较详情——CheckResult/checkCloseWithin/checkAllCloseWithin/
 *         checkAtMost/checkIdentical（比较公式唯一实现点＝core，本头只包转与报告）。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.3（比较语义细则＋失败行为总表）、§5.3.1/§5.3.2（签名）、
 *     §8 TK-CMP（反例矩阵）、§10.3（core 交接——C4 公式与 allCloseWithin 归 core）
 *   - 需求 NFR-COR-01（黄金对照同公式——NFR-COR-03 不静默通过）
 *   - 任务契约 tasks/foundation/TK-T05.json（≙WP-02-T05，CR-06 diff 通过后的合法消费）
 *
 * 三级断言语义（§4.3.3——不可混用）：
 *   容差匹配（close/allClose）——连续量对照；上界校验（atMost）——"≤阈值"类判定；
 *   精确相等（identical）——身份/名称/枚举 token（附录 D 第 12 项：无容差）。
 *
 * 失败行为（§4.3.3 总表）：
 *   actual/reference 非有限 → 断言失败（不静默通过——NFR-COR-03）；
 *   长度不匹配 → 失败并列出缺失/额外索引；双方皆空（集合等价）→ 通过（平凡等价）。
 *
 * 本头零 gtest（D-06：断言只返回 CheckResult 机器可读结果；宏适配层在
 * gtest/AssertMacros.hpp——展开于消费方 TU，要求已链 gtest）。
 * 线程安全：纯函数，无共享状态。
 */

#ifndef SDURWS_IRD_TESTKIT_CHECK_HPP
#define SDURWS_IRD_TESTKIT_CHECK_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Compare.hpp>   // core::Tolerance（C4 两分量）
#include <sdurws/ird/core/Units.hpp>     // UnitToken（checkAtMost 单位校验）

namespace sdurws::ird::testkit {

/// 单个失败点的机器可读详情（§7.2 ComparisonRecord 的来源——TK-T10 Report 消费）。
struct CompareDetail {
    std::string fieldPath;               ///< 具体路径（含索引）
    std::size_t elementIndex = 0;        ///< 无索引语义时 0 且 hasElement=false
    bool hasElement = false;             ///< 序列比较时 true
    double actual = 0.0;                 ///< 实际值（SI）
    double expected = 0.0;               ///< 期望值（SI）
    double diff = 0.0;                   ///< |a−b|（SI）
    std::string unit;                    ///< SI 规范 token（"m"——比较在 SI 域执行）
    core::Tolerance tolerance;           ///< 采用的容差
};

/// 断言结果：passed＝全部满足；failures＝逐失败点详情（机器可读——IRD_EXPECT_* 宏
/// 据此输出 ADD_FAILURE_AT，TK-T10 Report 据此写 ComparisonRecord）。
struct CheckResult {
    bool passed = true;
    std::vector<CompareDetail> failures;
};

/**
 * @brief 单点容差匹配（C4 公式包转——委托 core::closeWithin，唯一公式实现点）。
 *
 * @param fieldPath   [in] 具体字段路径（如 fk[3].tcp.position.x）
 * @param actualSi    [in] 实际值（SI）
 * @param referenceSi [in] 期望值（SI）
 * @param tolerance   [in] 容差（C4 两分量）
 * @param sourceTag   [in] 来源标记（如 appendixD#4——详情报告透传）
 * @return passed=true 满足；false 时 failures 含一条本点详情
 *         （非有限输入亦记为失败——不静默通过，NFR-COR-03；noexcept 不抛）
 */
CheckResult checkCloseWithin(std::string_view fieldPath, double actualSi,
                             double referenceSi, core::Tolerance tolerance,
                             std::string_view sourceTag) noexcept;

/**
 * @brief 序列容差匹配：逐元素应用 C4（不以差值总和替代——防正负抵消）。
 *
 * 长度不匹配 → passed=false 且 failures 含缺失/额外索引详情（§4.3.3：
 * 失败并列清单，非异常）；'*' 模板按元素索引展开为具体路径。
 * noexcept；不抛（与 core 版抛错的语义差异＝测试侧失败而非契约违约）。
 */
CheckResult checkAllCloseWithin(std::string_view fieldPathTemplate,
                                const std::vector<double>& actualSi,
                                const std::vector<double>& referenceSi,
                                core::Tolerance tolerance,
                                std::string_view sourceTag) noexcept;

/**
 * @brief 上界校验（"≤阈值"类——残差/误差度量/内存/耗时上限）。
 *
 * @param unit [in] 显示单位 token（须已注册；未注册抛
 *             TestKitError(UnitMismatch)——数据集非法级，§4.3.3 表）
 */
CheckResult checkAtMost(std::string_view fieldPath, double actualSi, double boundSi,
                        std::string_view unit);

/**
 * @brief 精确相等（字符串——身份/名称/枚举 token，附录 D 第 12 项：无容差）。
 */
CheckResult checkIdentical(std::string_view fieldPath, std::string_view actual,
                           std::string_view expected);

/// 精确相等（整数重载）。
CheckResult checkIdentical(std::string_view fieldPath, std::int64_t actual,
                           std::int64_t expected);

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_CHECK_HPP
