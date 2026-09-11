/**
 * @file   AssertMacros.hpp
 * @brief  gtest 适配层——IRD_EXPECT_* 宏（§5.3.3；仅在消费方 _test 目标的
 *         翻译单元展开；要求已链 gtest；Check.hpp 本体零 gtest）。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.3（三级断言不可混用）、§5.3.3（宏行为：失败时逐
 *     CompareDetail 输出 ADD_FAILURE_AT，容差缺失/单位不匹配抛 TestKitError
 *     ——两类失败不得混淆）
 *   - 任务契约 tasks/foundation/TK-T05.json（TK-CMP 宏行为用例）
 *
 * 宏语义（失败总表 §4.3.3）：
 *   - CheckResult.passed=false → 逐详情 ADD_FAILURE（测试失败级）；
 *   - TestKitError 抛出 → 直接传播（数据集非法级——由夹具转 EnvUnavailable/
 *     DatasetInvalid 结果，§7.2；两类失败不得混淆）。
 *
 * 展开要求：消费方 TU 已链 gtest 且已包含本头（宏仅在 TU 内展开——testkit
 * 库本体零 gtest，D-06）。
 */

#ifndef SDURWS_IRD_TESTKIT_GTEST_ASSERTMACROS_HPP
#define SDURWS_IRD_TESTKIT_GTEST_ASSERTMACROS_HPP

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Check.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

/// 逐失败详情输出（宏内公共体——失败点数与详情字段全量报告）。
#define IRD_CHECK_REPORT_(resultExpr)                                                  \
    do {                                                                               \
        const auto irdCheckResult_ = (resultExpr);                                     \
        if (!irdCheckResult_.passed) {                                                 \
            for (const auto& irdDetail_ : irdCheckResult_.failures) {                  \
                ADD_FAILURE() << irdDetail_.fieldPath << ": actual="                   \
                              << irdDetail_.actual << " expected="                     \
                              << irdDetail_.expected << " diff="                       \
                              << irdDetail_.diff;                                      \
            }                                                                          \
        }                                                                              \
    } while (false)

/// 容差匹配（连续量对照）——容差经 profile.resolve(fieldPath) 取条目。
#define IRD_EXPECT_CLOSE(fieldPath, actualSi, referenceSi, profile, unitToken)         \
    IRD_CHECK_REPORT_(checkCloseWithin(                                                \
        fieldPath, actualSi, referenceSi,                                              \
        (profile).resolve(fieldPath).tolerance, unitToken))

/// 序列容差匹配（'*' 模板逐元素展开）。
#define IRD_EXPECT_ALL_CLOSE(fieldPathTemplate, actualVecSi, referenceVecSi,           \
                             profile, unitToken)                                       \
    IRD_CHECK_REPORT_(checkAllCloseWithin(                                             \
        fieldPathTemplate, actualVecSi, referenceVecSi,                                \
        (profile).resolve(fieldPathTemplate).tolerance, unitToken))

/// 上界校验（"≤阈值"类判定）。
#define IRD_EXPECT_AT_MOST(fieldPath, actualSi, boundSi, unit)                         \
    IRD_CHECK_REPORT_(checkAtMost(fieldPath, actualSi, boundSi, unit))

/// 精确相等（字符串）。
#define IRD_EXPECT_IDENTICAL(fieldPath, actualText, expectedText)                      \
    IRD_CHECK_REPORT_(checkIdentical(fieldPath, actualText, expectedText))

/// 非有限守卫（前置检查——NaN/Inf 出现即断言失败，防下游静默传播）。
#define IRD_EXPECT_FINITE(fieldPath, value)                                            \
    do {                                                                               \
        if (std::isnan(static_cast<double>(value))                                     \
            || std::isinf(static_cast<double>(value))) {                               \
            ADD_FAILURE() << fieldPath << ": 非有限值（NaN/±Inf）——NFR-COR-03";       \
        }                                                                              \
    } while (false)

#endif  // SDURWS_IRD_TESTKIT_GTEST_ASSERTMACROS_HPP
