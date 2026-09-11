/**
 * @file   CoreIntegrationExampleTest.cpp
 * @brief  core 接入示例（附录 A.1）——testkit 断言消费 core 值类型的样板
 *         用例（units/testkit.md §A.1 原样落地；任务契约 TK-T10 acceptance②）。
 *
 * 设计依据：
 *   - units/testkit.md 附录 A.1（core 单位/数值基础测试接入示例——作为
 *     sdurws_ird_core_test 的样板用例建议：core 侧复制时保持用例结构与
 *     IRD_TEST_INFO 追溯登记形态）；§5.3/§7.3
 *   - 任务契约 tasks/foundation/TK-T10.json acceptance②：core 接入示例
 *     （附录 A.1）随测试运行通过
 *
 * 落位说明：core/test/** 不在本契约 allowedFiles 内——示例在本目标
 * （sdurws_ird_core_test 同形态：core 类型＋testkit 断言＋IRD_TEST_INFO）
 * 运行通过；core 侧正式接入由 CORE-T04/T05 交付面按附录 A.1 登记复制
 * （样板建议随 testkit.md v0.9 与 README 登记）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/testkit/Check.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <limits>

namespace {
namespace core = sdurws::ird::core;   // 别名：与 A.1 示例的 core:: 引用形态一致
using sdurws::ird::testkit::checkCloseWithin;
}  // namespace

/** 附录 A.1 原样用例：mm 换算往返对照（附录 D 第 9 项容差）＋换算入口
 * 拒绝 NaN（NFR-COR-03）。IRD_TEST_INFO 首行追溯登记（示例形态示范）。 */
TEST(UnitsTolerance, MmToDegRoundtrip_AppendixD_Item9) {
    IRD_TEST_INFO("KIN-12", /*at*/ {}, /*dataset*/ std::nullopt);  // 追溯登记（无数据集时省略）

    const auto mm = core::UnitToken::find("mm").value();
    const auto m = core::UnitToken::find("m").value();
    const double x = 1234.0;                       // mm
    const double y = core::convert(x, mm, m);      // 1.234 m（唯一换算入口）
    const double z = core::convert(y, m, mm);      // 往返
    // 容差＝附录 D 第 9 项（标量相对 1e-9；测试对照容差）＋ε_abs 由档案条目给出
    const auto t = core::Tolerance::make(1e-9, 1e-12);
    const auto r = checkCloseWithin("units.roundtrip.mm", z, x, t, "appendixD#9");
    EXPECT_TRUE(r.passed);  // 失败时 IRD 宏会逐点输出 actual/expected/容差/差值

    // 非有限反例：换算入口拒绝 NaN（NFR-COR-03）
    EXPECT_FALSE(
        core::tryConvert(std::numeric_limits<double>::quiet_NaN(), mm, m).has_value());
}
