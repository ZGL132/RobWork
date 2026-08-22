#ifndef SDURWS_FOURPLUGINACCEPTANCE_FIXTURE_HPP
#define SDURWS_FOURPLUGINACCEPTANCE_FIXTURE_HPP

#include <QString>

namespace sdurws { namespace fourpluginacceptance {

//! 四插件验收夹具的最小结果对象，供 CTest 和后续 oracle 复用。
struct FixtureResult
{
    bool passed;
    QString errorCode;
    QString errorMessage;
    qint64 durationMs;
};

//! 加载 GenericSixAxis 两个 WorkCell 资源并验证六自由度结构。
FixtureResult runFourPluginAcceptanceFixture ();

}}    // namespace sdurws::fourpluginacceptance

#endif    // SDURWS_FOURPLUGINACCEPTANCE_FIXTURE_HPP
