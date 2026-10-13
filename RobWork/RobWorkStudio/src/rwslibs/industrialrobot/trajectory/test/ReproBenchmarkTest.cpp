/**
 * @file   ReproBenchmarkTest.cpp
 * @brief  trajectory 域确定性复现基准用例（WP-23-T09 抽样——B-08）：PTP 段
 *         链规划（planPtpSegment——§7.5 三键确定性选择序）同配置复跑的等价
 *         集合＋稳定排序（选择序/路点序逐位一致语义——NFR-COR-02）。
 *
 * 设计依据：
 *   - units/trajectory.md §7.5（构型选择三键全序——延续性优先→裕量降序→
 *     stableIndex 升序兜底；§15.0 确定性"同输入恒同输出"）
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行＋§4.6
 *     （不要求浮点逐字节相同——对照容差经 repro-reduction 档案）
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/WP-23-T09.json
 *
 * 抽样说明（各域抽样复现——本域取样点）：PTP 段规划是本域候选选择序的
 * 核心通道（多候选集＋三键全序＋稳定编号兜底）。本域无并行分片与随机
 * 种子（planPtpSegment 纯函数——§7"无归约歧义"；无随机性入口），线程
 * 等价腿不适用——如实只跑复跑腿（夹具按 spec.parallelThreadCount=0 裁剪）；
 * 报告如实记录两腿（reference/rerun）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/trajectory/Ptp.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/ReproBenchmark.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace tk = sdurws::ird::testkit;
using sdurws::ird::core::ObjectId;
using trj::PtpCandidate;
using trj::PtpRequest;
using trj::planPtpSegment;

// =====================================================================
// 元素投影（段规划结果 → SetMatchTraits 元素——§5.4.5 适配归域）。
// 元素与 traits 特化置于全局作用域（SetCheckTest 消费示范同款——特化须
// 在主模板的可达外围命名空间声明）。
// =====================================================================

/// 段元素：身份＝段序；数值＝终点构型 q（rad，逐轴）＋关节路径长度
/// （rad|m 混合计量——§6.2）。选择序（selectedCandidateIndex）以身份键
/// 承载（"seg-<i>/cand-<index>"——选择不同即身份不同＝集合/顺序违例，
/// 附录 D 第 12 项：枚举/序号无容差）。
struct ReproSegmentItem {
    std::string id;             ///< "seg-<i>/cand-<selected>"（含选择序结果）
    std::vector<double> endQ;   ///< 段终点构型，单位 rad
    double pathLengthJoint = 0.0; ///< 关节路径长度（rad|m 逐轴 |Δq| 之和）
};

namespace sdurws::ird::testkit {
template <>
struct SetMatchTraits<ReproSegmentItem> {
    static std::optional<std::string> identity(const ReproSegmentItem& e)
    {
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const ReproSegmentItem& e)
    {
        std::vector<NumericFieldView> out;
        out.reserve(e.endQ.size() + 1);
        for (const double v : e.endQ) { out.push_back({"repro.angle", v}); }
        out.push_back({"repro.length", e.pathLengthJoint});
        return out;
    }
};
}  // namespace sdurws::ird::testkit

namespace {

/// repro-reduction 档案（B-08 数值唯一来源——与 testkit 元测试同一文件）。
const tk::ToleranceProfile& reproProfile()
{
    static const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::toleranceProfileDir("repro-reduction") / "v1.0.0.json");
    return profile;
}

/// 权威关节向量（rad——单位显式）。
rw::math::Q makeQ(std::initializer_list<double> vals)
{
    rw::math::Q q(vals.size());
    std::size_t i = 0;
    for (const double v : vals) { q[i++] = v; }
    return q;
}

/// 段请求（3 轴解析面——区间 [(-1,-1,-1),(2,2,2)] rad；PtpSequenceTest 同款）。
trj::PtpRequest makeRequest(const rw::math::Q& start,
                            const std::vector<trj::PtpCandidate>& cands,
                            std::uint32_t segmentIndex)
{
    trj::PtpRequest req;
    req.startQ = start;
    req.lowerBoundQ = makeQ({-1.0, -1.0, -1.0});
    req.upperBoundQ = makeQ({2.0, 2.0, 2.0});
    req.candidates = cands;
    req.ikContinuityThreshold = 1e-6;      // rad 逐轴（延续性阈值）
    req.constraint = trj::SegmentConstraint{};
    req.tcpRef = ObjectId::fromCanonical(
        "obj-00000000000000000000000000000001");  // 固定合法身份（复现要素——
                                                  // 不用 generate 的随机 id）
    req.sourceTaskPoint = ObjectId::fromCanonical(
        "obj-00000000000000000000000000000002");  // 到达站来源（复现要素）
    req.segmentIndex = segmentIndex;
    return req;
}

/// 候选（q/stableIndex/裕量——裕量字面量；§7.5 三键输入面）。
trj::PtpCandidate cand(std::initializer_list<double> q, std::size_t stableIndex,
                       double margin)
{
    trj::PtpCandidate c;
    c.q = makeQ(q);
    c.stableIndex = stableIndex;
    c.minimumJointMargin = margin;
    return c;
}

/// 段链规划（8 段——上一段终点为下一段起点；候选集含延续解/裕量解/并列
/// 解三类选择路径；返回逐段元素投影）。
std::vector<ReproSegmentItem> planChain()
{
    static const std::vector<std::vector<trj::PtpCandidate>> kChainCands = {
        // 段 0：延续解胜（规则 1）。
        {cand({1.1, 0.8, -0.7}, 0U, 0.8), cand({-0.9, -0.9, -0.9}, 1U, 0.9)},
        // 段 1：无延续→裕量降序（规则 2）。
        {cand({1.0, 1.0, 1.0}, 0U, 0.4), cand({1.5, 1.5, 1.5}, 1U, 0.9)},
        // 段 2：裕量精确并列→stableIndex 升序兜底（规则 3）。
        {cand({1.0, 1.0, 1.0}, 1U, 0.7), cand({-1.0, -1.0, -1.0}, 0U, 0.7)},
        // 段 3~7：混合形态复跑（选择面覆盖足够即抽样成立——不加码）。
        {cand({0.5, 0.5, 0.5}, 0U, 0.6), cand({1.9, -0.9, -0.5}, 1U, 0.95)},
        {cand({-0.5, 0.9, 1.2}, 0U, 0.55), cand({0.6, 0.9, 1.2}, 1U, 0.4)},
        {cand({1.2, -0.3, 0.1}, 2U, 0.85), cand({1.2, -0.3, 0.1}, 0U, 0.85),
         cand({0.2, 0.2, 0.2}, 1U, 0.1)},
        {cand({-0.7, -0.7, -0.7}, 0U, 0.5), cand({1.8, 1.8, 1.8}, 1U, 0.99)},
        {cand({0.9, 0.9, 0.9}, 0U, 0.65), cand({-0.2, -0.2, -0.2}, 1U, 0.65)},
    };

    std::vector<ReproSegmentItem> out;
    out.reserve(kChainCands.size());
    rw::math::Q start = makeQ({0.1, -0.2, 0.3});   // rad——链起点（固定字面量）
    std::uint32_t seg = 0;
    for (const auto& cands : kChainCands) {
        const trj::PtpRequest req = makeRequest(start, cands, seg);
        const trj::PtpPlanResult res = planPtpSegment(req);
        if (res.status != trj::PtpStatus::Ok
            || !res.selectedCandidateIndex.has_value()) {
            // 规划失败＝夹具前置违约（输入面自检）——抽样继续无意义，
            // 返回空集让集合断言显式失败（不伪造样本）。
            return {};
        }
        ReproSegmentItem e;
        e.id = "seg-" + std::to_string(seg)
            + "/cand-" + std::to_string(*res.selectedCandidateIndex);
        const rw::math::Q& endQ = res.segment.waypoints.back().q.value();
        e.endQ.reserve(endQ.size());
        for (std::size_t k = 0; k < endQ.size(); ++k) {
            e.endQ.push_back(endQ[k]);   // rad——Q 向量逐轴拷贝（rad|m 混合计量）
        }
        e.pathLengthJoint = res.segment.pathLengthJoint;
        out.push_back(std::move(e));
        start = res.segment.waypoints.back().q.value();   // 链式续段
        ++seg;
    }
    return out;
}

}  // namespace

/**
 * trajectory 域复现基准（B-08 抽样）：8 段 PTP 链（三键选择全路径覆盖）
 * 同输入复跑——等价集合＋稳定排序；线程等价腿不适用（纯函数无并行分片）
 * ——如实两腿。
 */
TEST(TrjReproBenchmark, PtpChainRerunEquivalentSetAndOrder_WP23T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-06"});

    tk::ReproCaseSpec spec;
    spec.caseId = "trj-ptp-chain-rerun";
    spec.domain = "trajectory";
    spec.inputProvenance =
        "ptpChainFixture(8 segments, 3-axis analytic, 2-3 candidates per "
        "segment covering rules 1-3, fixed object ids)";
    spec.repro.seed = 20260909;      // 无随机消费域——记录默认口径
    spec.repro.threadCount = 1;
    spec.parallelThreadCount = 0;    // 纯函数无分片——线程腿不适用（夹具裁剪）

    const tk::ReproCaseReport report = tk::runReproCase<ReproSegmentItem>(
        spec, reproProfile(), tk::SetMatchTraits<ReproSegmentItem>{},
        [](std::uint32_t) { return planChain(); });

    // 判定（NFR-COR-02：等价集合＋稳定排序——本域抽样面）。
    ASSERT_EQ(report.verdicts.size(), 1U);
    EXPECT_EQ(report.verdicts.front().legId, "rerun");
    EXPECT_TRUE(report.verdicts.front().setEquivalent);
    EXPECT_TRUE(report.verdicts.front().sameOrder);
    EXPECT_TRUE(report.passed) << (report.violations.empty()
        ? "" : report.violations.front());
    // 元素面：8 段全部在场（规划前置失败会以空集显式失败——不伪造样本）。
    EXPECT_EQ(report.legs.front().elementCount, 8U);

    // 报告拖挂（IRD_REPRO_REPORT_DIR 设置时落盘——基准执行留痕）。
    if (const auto dir = tk::reproReportDirFromEnv()) {
        EXPECT_TRUE(tk::writeReproReportJson(*dir, report));
    }
}
