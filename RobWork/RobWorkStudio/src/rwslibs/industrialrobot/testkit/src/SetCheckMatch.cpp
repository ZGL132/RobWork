/**
 * @file   SetCheckMatch.cpp
 * @brief  最大二分图匹配内核——Boost.Graph 可配置 checked Edmonds 薄适配
 *         （SetCheck 阶段 2 的 detail::maximumBipartiteMatching 实现）。
 *
 * 设计依据：
 *   - units/testkit.md §5.4.2（最大匹配语义；"匹配＝Hopcroft–Karp"为复杂度
 *     期望命名，非算法强制——v0.6 实现取舍注；同输入同报告的确定性要求）；
 *   - REUSE-T01 审计 B17（compare-set-dataset-audit.md"集合最大匹配"节）与
 *     probe-r17/candidates（checked_edmonds_maximum_cardinality_matching／
 *     matching_size 两项公开 API 探测 passed——候选可运行证据，本 TU 为其
 *     生产接入面）；WP-02 定向测量核实 matching 的公开策略入口，保留
 *     Edmonds 增广及 maximum_cardinality_matching_verifier，只换初始化策略；
 *   - H1/H6：成熟库承担匹配核心（复用来源＝vcpkg 已装 Boost 1.92
 *     Boost::headers，header-only 编译期使用，零新增运行库/渠道变更）。
 *
 * 边界（本 TU 职责＝图构造/算法调用/结果转表，止于此）：
 *   - 业务规则零涉及：身份优先、C4 相容边构造、歧义检测、缺失/多余与诊断
 *     全在 SetCheck.hpp 模板层——本 TU 只见 (左,右) 索引边表；
 *   - Boost 头收敛于本 TU：消费方经 PRIVATE Boost::headers 取得编译期使用，
 *     公共模板头与任何消费者编译面零 Boost（依赖契约修订随治理登记）；
 *   - 物化 |E| 边的内存取舍在调用方模板层完成（求值 O(nL·nR) 与原谓词按需
 *     扫描同阶），本 TU 不做规模裁决（护栏归模板层 Usage）。
 *
 * 确定性：adjacency_list vecS 保持构造序，算法无随机源——同边表同结果
 * （§5.4.2"同输入同报告"；多解情形的配对见证由此保持逐次稳定）。
 *
 * 线程安全：纯函数；无共享状态。
 */

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/max_cardinality_matching.hpp>

#include <sdurws/ird/testkit/SetCheck.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace sdurws::ird::testkit::detail {

std::vector<std::size_t> maximumBipartiteMatching(
    std::size_t nLeft, std::size_t nRight,
    const std::vector<std::pair<std::size_t, std::size_t>>& edges)
{
    constexpr std::size_t kNil = static_cast<std::size_t>(-1);
    // 左点占 [0,nLeft)、右点占 [nLeft, nLeft+nRight)：二分结构由边构造保证
    // （相容边只在跨侧生成），无向图算法对二分输入给出同一最大基数结果。
    using Graph = boost::adjacency_list<boost::vecS, boost::vecS,
                                        boost::undirectedS>;
    using Vertex = boost::graph_traits<Graph>::vertex_descriptor;
    Graph graph(nLeft + nRight);
    for (const auto& [li, ri] : edges) {
        add_edge(static_cast<Vertex>(li), static_cast<Vertex>(nLeft + ri),
                 graph);
    }

    // 默认 checked 入口使用 extra_greedy_matching，会复制双向边再做两轮排序。
    // 本接缝的图已经按左右索引稳定构造，改用 Boost 自带 greedy_matching 初始化，
    // 避免排序暂存；它不是最终匹配结果，后续仍由同一 Edmonds 核增广到最大基数。
    // 最大性验证器保持不变，不能以贪心“配不全”冒充不存在最大匹配（REUSE-T20）。
    using MateMap = Vertex*;
    using IndexMap = boost::property_map<Graph, boost::vertex_index_t>::const_type;
    std::vector<Vertex> mate(nLeft + nRight,
                             boost::graph_traits<Graph>::null_vertex());
    if (!boost::matching<Graph, MateMap, IndexMap,
            boost::edmonds_augmenting_path_finder, boost::greedy_matching,
            boost::maximum_cardinality_matching_verifier>(
                graph, mate.data(), get(boost::vertex_index, graph))) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "usage: 最大匹配内核自验失败（Boost checked Edmonds "
                           "验证器返回 false）——testkit 内部不变量破坏，fail-fast");
    }

    // mate 表 → matchL 转表；计数与 matching_size（第二候选）正交互检——
    // 两条公开 API 路径的计数必须一致，否则同为内部不变量破坏。
    const auto boostSize = matching_size(graph, mate.data());
    std::vector<std::size_t> matchL(nLeft, kNil);
    std::size_t counted = 0;
    for (std::size_t li = 0; li < nLeft; ++li) {
        if (mate[li] != boost::graph_traits<Graph>::null_vertex()) {
            matchL[li] = static_cast<std::size_t>(mate[li]) - nLeft;
            ++counted;
        }
    }
    if (counted != boostSize) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "usage: 最大匹配计数互检失败（mate 转表 " +
                               std::to_string(counted) + " ≠ matching_size " +
                               std::to_string(boostSize) + "）——fail-fast");
    }
    return matchL;
}

}  // namespace sdurws::ird::testkit::detail
