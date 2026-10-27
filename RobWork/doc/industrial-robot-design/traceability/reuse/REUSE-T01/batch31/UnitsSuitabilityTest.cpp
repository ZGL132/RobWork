/**
 * @file UnitsSuitabilityTest.cpp
 * @brief REUSE-T01 B31：单位候选与C4极值反例，只读审计不替换产品。
 *
 * 依据SA-12、NFR-COR-03、附录D C4/C7；复用rw数学值与原core测试/report。
 * 所有对象当前测试线程局部持有，不创建模型或Qt对象；异常按原API区分。
 */
#include <gtest/gtest.h>
#include <rw/core/DOMCoreBasisTypes.hpp>
#include <rw/core/Exception.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/Math.hpp>
#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <limits>
#include <vector>

#ifndef IRD_CORE_HAS_RW_Q
#error B31必须真实编译Q重载，不能通过条件编译跳过该验证面
#endif

namespace {
using namespace sdurws::ird::core;

// SA-12：角度三类因子可直接沿用上游math常量；不需要导入XML加载器。
TEST(ReuseUnitsAudit, MathAngleConstants_CORE_T04) {
    EXPECT_DOUBLE_EQ(rw::math::Deg2Rad,UnitToken::find("deg")->siFactor());
    EXPECT_DOUBLE_EQ(rw::math::Deg2Rad,UnitToken::find("deg/s")->siFactor());
    EXPECT_DOUBLE_EQ(rw::math::Deg2Rad,UnitToken::find("deg/s^2")->siFactor());
    EXPECT_DOUBLE_EQ(rw::math::Pi,convert(180,*UnitToken::find("deg"),*UnitToken::find("rad")));
}

// H1：发现core包也提供DOM比例表；它不带量纲/产品词表，不能整体冒充UnitToken。
TEST(ReuseUnitsAudit, DomCoreScaleAndSemanticLimits_CORE_T04) {
    for(const char* token: {"mm","cm","m","deg","rad","m/s","mm/s","m/s^2","mm/s^2","deg/s","rad/s","deg/s^2","rad/s^2"}) {
        SCOPED_TRACE(token);
        ASSERT_TRUE(UnitToken::find(token));
        EXPECT_DOUBLE_EQ(UnitToken::find(token)->siFactor(),rw::core::DOMCoreBasisTypes::getUnit(token));
    }
    EXPECT_DOUBLE_EQ(0.0254,rw::core::DOMCoreBasisTypes::getUnit("inch"));
    EXPECT_FALSE(UnitToken::find("inch")); // 产品当前冻结词表没有inch，不能默许扩展输入。
    EXPECT_THROW(rw::core::DOMCoreBasisTypes::getUnit("N*m"),rw::core::Exception);
    EXPECT_THROW(rw::core::DOMCoreBasisTypes::getUnit("kg"),rw::core::Exception);
    EXPECT_DOUBLE_EQ(rw::core::DOMCoreBasisTypes::getUnit("m"),rw::core::DOMCoreBasisTypes::getUnit("rad"));
    EXPECT_FALSE(tryConvert(1,*UnitToken::find("m"),*UnitToken::find("rad")));
}

// C4：先有健康对照，Q逐元素比较与精确Q相等的语义不同，不能简单替换operator==。
TEST(ReuseUnitsAudit, QApproximationAndNanLimits_CORE_T05) {
    rw::math::Q one(1),near(1);one[0]=1;near[0]=1.0005;
    EXPECT_FALSE(one==near);
    EXPECT_TRUE(allCloseWithin(near,one,Tolerance::make(0,0.001)));
    EXPECT_TRUE(rw::math::Math::isNaN(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(rw::math::Math::isNaN(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(closeWithin(std::numeric_limits<double>::infinity(),1,Tolerance::make(0,1)));
}

// C4：精确有理数约去DBL_MAX后为2>1.5，应拒绝；不以另一份浮点实现生成期望。
TEST(ReuseUnitsAudit, ScalarFiniteOverflowMustReject_CORE_T05) {
    const double maximum=std::numeric_limits<double>::max();
    EXPECT_FALSE(closeWithin(maximum,-maximum,Tolerance::make(1.5,0)));
}
// C4：公共vector入口必须继承正确的标量判定，不能因两个中间无穷大误绿。
TEST(ReuseUnitsAudit, VectorFiniteOverflowMustReject_CORE_T05) {
    const double maximum=std::numeric_limits<double>::max();
    EXPECT_FALSE(allCloseWithin(std::vector<double>{maximum},std::vector<double>{-maximum},Tolerance::make(1.5,0)));
}
// C4：真实rw::math::Q重载也验证同一反例；Q值由本测试所有，不携带模型句柄。
TEST(ReuseUnitsAudit, QFiniteOverflowMustReject_CORE_T05) {
    const double maximum=std::numeric_limits<double>::max();
    rw::math::Q value(1),reference(1);value[0]=maximum;reference[0]=-maximum;
    EXPECT_FALSE(allCloseWithin(value,reference,Tolerance::make(1.5,0)));
}

// §4.4各一次乘除：记录次正规数中间下溢，不擅改舍入契约或判作已批准修复。
TEST(ReuseUnitsAudit, SubnormalConversionCharacterization_CORE_T04) {
    const double tiny=std::numeric_limits<double>::denorm_min();
    const auto mm=*UnitToken::find("mm");
    const auto result=tryConvert(tiny,mm,mm);
    ASSERT_TRUE(result);
    EXPECT_EQ(0,*result);
    RecordProperty("subnormal_same_unit_zero",*result==0?"observed":"not observed");
    EXPECT_EQ(tiny,Length::fromSi(tiny).siValue()); // 纯SI构造本身没有把值清零。
}
} // namespace
