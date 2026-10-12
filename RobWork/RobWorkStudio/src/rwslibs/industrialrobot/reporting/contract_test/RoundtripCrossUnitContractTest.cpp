/**
 * @file   RoundtripCrossUnitContractTest.cpp
 * @brief  往回复算注入契约的跨单元契约面（RPT-T16）——复算请求值类型消费
 *         core 权威类型恒等＋复算注入源 try 轨形态＋Roundtrip.hpp 公共头
 *         零 execution/io/runtime include 与对端命名空间限定名扫描。
 *
 * 设计依据：
 *   - units/reporting.md §7.7（复算执行归 execution/各域——reporting 经
 *     注入的请求接口发起；比对入口归 reporting）、§3.3（注入边界实施形态
 *     ——零 execution/io/runtime 编译边，P-RPT-1/P-RPT-2 同款纪律）、
 *     §3.2（依赖图——四条登记单元边＋零表外同层边）
 *   - 需求 RPT-06（往返复算——比对入口的承载契约）、SA-10（表外边红线
 *     ——字符串级具名自证，与 BuildRedLineTest/LinkageContractTest 的
 *     include 面/链接图扫描互为多道防线）
 *   - 任务契约 tasks/foundation/RPT-T16.json（outputs＝implementation/
 *     unit-tests/traceability-update——本文件为跨单元契约面的具名自证）
 *
 * 替身边界声明（test/README.md §3 同源）：本文件的 ScriptedRecomputeSource
 *   仅验证注入契约形态（try 轨/值类型），不构成 execution 评估通道行为的
 *   证明——真实复算执行归 execution/各域适配（L5 装配）。
 *
 * 线程约束：单线程（gtest 串行——测试目标纪律）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/Roundtrip.hpp>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;

// =====================================================================
// ①复算请求值类型的跨单元权威消费（core 类型恒等——不本地重定义，O-24）
// =====================================================================

/**
 * RoundtripRecomputeRequest 的字段类型与 core/evidence 权威类型逐恒等
 * （static_assert 编译期钉住——若上游类型演化导致不再同型，此处立即编译
 * 失败而非静默漂移；P-RPT-9 基线消费纪律的执行面）。
 */
TEST(RoundtripCrossUnitContract, RecomputeRequestConsumesCoreAuthorityTypes)
{
    // runId 与 core::RunId 恒等（复算定位键——core §4.1 Id128 族）。
    static_assert(std::is_same<decltype(RoundtripRecomputeRequest::runId), core::RunId>::value,
                  "复算请求 runId 必须恒等 core::RunId（不本地重定义——O-24）");
    // 快照/切片/输入基准与 core::ContentIdentity 恒等（core §4.2 内容身份）。
    static_assert(std::is_same<decltype(RoundtripRecomputeRequest::snapshotId),
                               core::ContentIdentity>::value,
                  "复算请求 snapshotId 必须恒等 core::ContentIdentity");
    static_assert(std::is_same<decltype(RoundtripRecomputeRequest::sliceId),
                               core::ContentIdentity>::value,
                  "复算请求 sliceId 必须恒等 core::ContentIdentity");
    static_assert(std::is_same<decltype(RoundtripRecomputeRequest::inputBaselineId),
                               core::ContentIdentity>::value,
                  "复算请求 inputBaselineId 必须恒等 core::ContentIdentity");
    // 评估模式与 core::EvaluationMode 恒等（core §4.7 词表——零本地枚举）。
    static_assert(std::is_same<decltype(RoundtripRecomputeRequest::mode),
                               core::EvaluationMode>::value,
                  "复算请求 mode 必须恒等 core::EvaluationMode（CR-01 零本地词表）");
    // 复算返回值与 evidence::ResultEnvelope 恒等（复算产物只读值——evidence
    // 构造边界 make() 的产物类型，reporting 零第二信封类型）。
    static_assert(
        std::is_same<
            decltype(std::declval<const IRoundtripRecomputeSource&>().tryRecompute(
                std::declval<const RoundtripRecomputeRequest&>())),
            std::optional<evidence::ResultEnvelope>>::value,
        "复算返回值必须恒等 evidence::ResultEnvelope 只读值（§7.7 复算产物）");

    SUCCEED() << "复算请求/返回值类型恒等 core/evidence 权威（编译期钉住）";
}

// =====================================================================
// ②比对入口服务与已冻结注入缝的类型衔接（复用 IReportResolver——不新建
//   第二套报告解析；Export.hpp 契约同口径）
// =====================================================================

/**
 * ReportRoundtripChecker 构造首参就是 Export.hpp 的 IReportResolver（复用
 * 已冻结注入缝——§7.7 比对入口的 reportId 取值面与导出服务同源；两处
 * "reportId→报告值"语义单一实现，NFR-MNT-04）。
 */
TEST(RoundtripCrossUnitContract, CheckerReusesFrozenResolverSeam)
{
    // MapResolver 式最小实现（contract 侧自持——不依赖 test 夹具）。
    class EmptyResolver final : public IReportResolver {
    public:
        std::optional<ReviewReport> tryResolve(ReportId) const override
        {
            return std::nullopt;
        }
    };
    class NullRecompute final : public IRoundtripRecomputeSource {
    public:
        std::optional<evidence::ResultEnvelope>
        tryRecompute(const RoundtripRecomputeRequest&) const override
        {
            return std::nullopt;   // try 轨形态：不可达 nullopt 不抛（§7.7 注入面）
        }
    };

    EmptyResolver resolver;
    NullRecompute recompute;
    ReportRoundtripChecker checker(resolver, recompute);

    // 经接口多态消费（IReportRoundtripChecker 服务级契约——L5 装配视图）。
    IReportRoundtripChecker& iface = checker;
    EXPECT_THROW(iface.roundtripCheck(ReportId::generate()), ReportError)
        << "空 resolver 下合法 reportId＝前置违约 Usage（IReportResolver 契约同口径）";

    SUCCEED() << "比对入口复用 IReportResolver 注入缝（类型衔接钉住）";
}

// =====================================================================
// ③复算注入契约的 try 轨与值语义（执行归 execution/各域——注入面形态）
// =====================================================================

/**
 * IRoundtripRecomputeSource 是抽象接口（复算执行不在 reporting——§7.7 职责
 * 边界的类型级表达）：无法默认实例化、只能经实现方适配注入；try 轨实现
 * 返回 nullopt 不抛（可恢复查询路径——§1.4）。
 */
TEST(RoundtripCrossUnitContract, RecomputeSourceIsAbstractInjectableContract)
{
    // 抽象性：接口不可实例化（复算执行归 execution/各域的类型级表达）。
    static_assert(std::is_abstract<IRoundtripRecomputeSource>::value,
                  "复算注入源必须保持抽象（reporting 零评估调用实现——§7.7）");
    static_assert(std::is_abstract<IReportRoundtripChecker>::value,
                  "比对入口接口保持抽象（服务级契约）");
    // 实现唯一形态在 reporting 侧（ReportRoundtripChecker——比对编排）。
    static_assert(!std::is_abstract<ReportRoundtripChecker>::value,
                  "比对入口实现可实例化（编排体）");

    SUCCEED() << "注入契约抽象性钉住（复算执行不在 reporting）";
}

// =====================================================================
// ④Roundtrip.hpp 公共头红线自证（零 execution/io/runtime include＋零 Qt
//   ＋零对端命名空间限定名——SA-10 表外边红线的字符串级具名自证）
// =====================================================================

/// 读取文件全文；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFileOrDie(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ADD_FAILURE() << "无法读取公共头：" << path;
        return {};
    }
    std::string content;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, n);
    }
    std::fclose(file);
    return content;
}

/**
 * Roundtrip.hpp 公共头纪律：零 execution/io/runtime include（§7.7 复算执行
 * 归 execution/各域——注入形态，P-RPT-1/P-RPT-2 同款）；零 Qt（D-01）；
 * 零对端命名空间限定名（类型级注入面核查）。
 */
TEST(RoundtripCrossUnitContract, RoundtripHeaderZeroOffTreeIncludes_RPT16)
{
    const std::string unitRoot = IRD_REPORTING_UNIT_ROOT;
    // 根＝industrialrobot 目录（CMake 注入形 "<dir>/.."——既有扫描同款口径，
    // 单元相对路径带 reporting/ 前缀）。
    const std::string header =
        unitRoot + "/reporting/include/sdurws/ird/reporting/Roundtrip.hpp";
    const std::string content = readFileOrDie(header);

    EXPECT_EQ(content.find("sdurws/ird/execution/"), std::string::npos)
        << "Roundtrip.hpp 出现 execution include——复算执行归注入（零 execution "
           "编译边，SA-10/P-RPT-1 同纪律）";
    EXPECT_EQ(content.find("sdurws/ird/io/"), std::string::npos)
        << "Roundtrip.hpp 出现 io include——P-RPT-1 注入纪律违约";
    EXPECT_EQ(content.find("sdurws/ird/runtime/"), std::string::npos)
        << "Roundtrip.hpp 出现 runtime include——P-RPT-2 注入纪律违约";
    EXPECT_EQ(content.find("#include <Qt"), std::string::npos)
        << "Roundtrip.hpp 出现 Qt include——D-01 零 Qt 纪律违约";
    // 对端命名空间禁入（即使经其他头间接可达，本头文本亦不得引用对端限定名）。
    EXPECT_EQ(content.find("ird::execution::"), std::string::npos)
        << "Roundtrip.hpp 出现 execution 命名空间限定名（零 execution 类型）";
    EXPECT_EQ(content.find("ird::runtime::"), std::string::npos)
        << "Roundtrip.hpp 出现 runtime 命名空间限定名（零 runtime 类型）";
    EXPECT_EQ(content.find("ird::io::"), std::string::npos)
        << "Roundtrip.hpp 出现 io 命名空间限定名（零 io 类型）";
}

}  // namespace
