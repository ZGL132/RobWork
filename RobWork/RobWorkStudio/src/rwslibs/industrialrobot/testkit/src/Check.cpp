/**
 * @file   Check.cpp
 * @brief  数值断言实现——包转 core::closeWithin（唯一公式）＋失败详情组织。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.3（失败行为总表）/§5.3.1/§5.3.2
 *   - 任务契约 tasks/foundation/TK-T05.json（≙WP-02-T05）
 */

#include <sdurws/ird/testkit/Check.hpp>

#include <cmath>

#include <sdurws/ird/testkit/TestPaths.hpp>   // TestKitError/UnitMismatch 分类

namespace sdurws::ird::testkit {
namespace {

/// 非有限检测（NaN/±Inf）。
bool nonFinite(double v) noexcept
{
    return std::isnan(v) || std::isinf(v);
}

/// 组装一条失败详情（公共字段填充——unit 固定 SI 规范 token "m"？否：unit 由
/// 调用方语境给出；close 族在 SI 域比较故固定 "m" 会失真——本实现以空串表达
/// "SI 域"，详情的 unit 语义由 §7.3 Report 定义）。
CompareDetail makeDetail(std::string fieldPath, bool hasElement, std::size_t index,
                         double actual, double expected, core::Tolerance t)
{
    CompareDetail d;
    d.fieldPath = std::move(fieldPath);
    d.hasElement = hasElement;
    d.elementIndex = index;
    d.actual = actual;
    d.expected = expected;
    d.diff = std::fabs(actual - expected);
    d.tolerance = t;
    return d;
}

}  // namespace

CheckResult checkCloseWithin(std::string_view fieldPath, double actualSi,
                             double referenceSi, core::Tolerance tolerance,
                             std::string_view sourceTag) noexcept
{
    (void)sourceTag;   // 详情透传字段——TK-T10 Report 消费；本函数只判 passed
    CheckResult result;
    // 委托 core::closeWithin（唯一公式实现点——CR-06 diff 通过后的合法消费；
    // 非有限输入 core 侧返回 false——不静默通过，NFR-COR-03）。
    if (!core::closeWithin(actualSi, referenceSi, tolerance)) {
        result.failures.push_back(
            makeDetail(std::string{fieldPath}, false, 0, actualSi, referenceSi, tolerance));
        result.passed = false;
    }
    return result;
}

CheckResult checkAllCloseWithin(std::string_view fieldPathTemplate,
                                const std::vector<double>& actualSi,
                                const std::vector<double>& referenceSi,
                                core::Tolerance tolerance,
                                std::string_view sourceTag) noexcept
{
    (void)sourceTag;
    CheckResult result;

    // 长度不匹配：失败并列出缺失/额外索引（§4.3.3 表——非异常，测试失败级）。
    // 缺失侧值以 NaN 占位（机器可读——非有限即显式缺失语义）。
    const std::size_t common = std::min(actualSi.size(), referenceSi.size());
    for (std::size_t i = 0; i < common; ++i) {
        // 逐元素 C4（防正负抵消——不以差值总和替代）。
        if (!core::closeWithin(actualSi[i], referenceSi[i], tolerance)) {
            std::string path{fieldPathTemplate};
            path += std::string("[");
            path += std::to_string(i);
            path += std::string("]");
            result.failures.push_back(
                makeDetail(path, true, i, actualSi[i], referenceSi[i], tolerance));
            result.passed = false;
        }
    }
    if (actualSi.size() != referenceSi.size()) {
        // 额外索引逐条登记（较长侧的超出元素缺失对侧）。
        const auto& longer = actualSi.size() > referenceSi.size() ? actualSi : referenceSi;
        const bool actualLonger = actualSi.size() > referenceSi.size();
        for (std::size_t i = common; i < longer.size(); ++i) {
            CompareDetail d = makeDetail(std::string(fieldPathTemplate) + "[" + std::to_string(i) + "]",
                                         true, i, actualLonger ? longer[i] : 0.0,
                                         actualLonger ? 0.0 : longer[i], tolerance);
            d.actual = actualLonger ? longer[i] : std::nan("");
            d.expected = actualLonger ? std::nan("") : longer[i];
            result.failures.push_back(d);
            result.passed = false;
        }
    }
    return result;
}

CheckResult checkAtMost(std::string_view fieldPath, double actualSi, double boundSi,
                        std::string_view unit)
{
    CheckResult result;
    // 单位须已注册（§4.3.3 表：未注册→TestKitError(unit-mismatch)——数据集非法级）。
    const auto token = core::UnitToken::find(unit);
    if (!token.has_value()) {
        throw TestKitError(TestKitErrorKind::UnitMismatch,
                           "unit-mismatch: 单位未注册: " + std::string{unit});
    }
    if (nonFinite(actualSi) || nonFinite(boundSi)) {
        // 非有限上界/实际值：数据集非法级（§4.3.3 表——与容差失败不同分类）。
        throw TestKitError(TestKitErrorKind::UnitMismatch,
                           "unit-mismatch: atMost 输入非有限（NaN/±Inf）");
    }
    if (actualSi > boundSi) {
        core::Tolerance t = core::Tolerance::make(0.0, boundSi);
        result.failures.push_back(
            makeDetail(std::string{fieldPath}, false, 0, actualSi, boundSi, t));
        result.passed = false;
    }
    return result;
}

CheckResult checkIdentical(std::string_view fieldPath, std::string_view actual,
                           std::string_view expected)
{
    CheckResult result;
    if (actual != expected) {
        // 精确相等（无容差）——字符串形态以 actual/expected 承载（diff 不适用，置 0）。
        core::Tolerance zero = core::Tolerance::make(0.0, 0.0);
        result.failures.push_back(makeDetail(std::string{fieldPath}, false, 0,
                                             0.0, 0.0, zero));
        result.passed = false;
    }
    return result;
}

CheckResult checkIdentical(std::string_view fieldPath, std::int64_t actual,
                           std::int64_t expected)
{
    CheckResult result;
    if (actual != expected) {
        core::Tolerance zero = core::Tolerance::make(0.0, 0.0);
        result.failures.push_back(makeDetail(std::string{fieldPath}, false, 0,
                                             static_cast<double>(actual),
                                             static_cast<double>(expected), zero));
        result.passed = false;
    }
    return result;
}

}  // namespace sdurws::ird::testkit
